#include "gb-include.h"

#include "Loop.h"
#include "Threads.h"    // g_threads.launchThreads()
#include "UdpServer.h"  // g_udpServer2.makeCallbacks()
#include "Profiler.h"
#include "Process.h"
#include "PageParser.h"

#ifdef __APPLE__
static const uintptr_t KQ_TIMER_QUICK = (uintptr_t)(MAX_NUM_FDS + 100);
static const uintptr_t KQ_TIMER_REAL  = (uintptr_t)(MAX_NUM_FDS + 101);
static const uintptr_t KQ_EVENT_WAKE  = (uintptr_t)(MAX_NUM_FDS + 200);
#endif

// raised from 5000 to 10000 because we have more UdpSlots now and Multicast
// will call g_loop.registerSleepCallback() if it fails to get a UdpSlot to
// send on.
#define MAX_SLOTS 10000

// TODO: . if signal queue overflows another signal is sent
//       . capture that signal and use poll or something???

// Tricky Gotchas:
// TODO: if an event happens on a TCP fd/socket before we fully accept it
//       we should just register it then call the read callback in case
//       we just missed a ready for reading signal!!!!!
// TODO: signals can be gotten off the queue after we've closed an fd
//       in which case the handler should be removed from Loop's registry
//       BEFORE being closed... so the handler will be NULL... ???
// NOTE: keep in mind that the signals might be delayed or be really fast!

// TODO: don't mask signals, catch them as they arrive? (like in phhttpd)

// . set this to false to disable async signal handling
// . that will make our udp servers less responsive
#ifdef __APPLE__
bool g_isHot = false;
#else
bool g_isHot = true;
#endif

// extern this for all to use
bool g_inSigHandler = false ;

// so we know if interrupts are supposed to be enabled/disabled
bool g_interruptsOn = false;

// are some signals to call g_udpServer2.makeCallbacks() queued?
bool g_someAreQueued = false;

int32_t g_numAlarms = 0;
int32_t g_numVTAlarms = 0;
int32_t g_numQuickPolls = 0;
int32_t g_missedQuickPolls = 0;
int32_t g_numSigChlds = 0;
int32_t g_numSigPipes = 0;
int32_t g_numSigIOs = 0;
int32_t g_numSigQueues = 0;
int32_t g_numSigOthers = 0;

// since we can't call gettimeofday() while in a sig handler, we use this
// and update it periodically to keep it somewhat accurate
int64_t g_now = 0;
int64_t g_nowApprox = 0;

char g_inWaitState = false;

// a global class extern'd in .h file
Loop g_loop;

// the global niceness
char g_niceness = 0;

// we make sure the same callback/handler is not hogging the cpu when it is
// niceness 0 and we do not interrupt it, so this is a critical check
class UdpSlot *g_callSlot = NULL;
int32_t g_lastTransId  = 0;
int32_t g_transIdCount = 0;

// keep the sig wait time static so we can change it based on m_minTick
static struct timespec s_sigWaitTime ;
static struct timespec s_sigWaitTime2 ;
static struct timespec* s_sigWaitTimePtr ;

// use this in case we unregister the "next" callback
static Slot *s_callbacksNext;

static void quickpollTimerCore ( int x , siginfo_t *info , void *y );
static void realTimerCore      ( int x , siginfo_t *info , void *y );

// set it from milliseconds
void Loop::setSigWaitTime ( int32_t ms ) {
	int32_t secs = ms / 1000;
	ms -= secs * 1000;
	s_sigWaitTime.tv_sec  = secs;
	s_sigWaitTime.tv_nsec = ms * 1000000;
}

// free up all our mem
void Loop::reset() {
	if ( m_slots ) {
		log(LOG_DEBUG,"db: resetting loop");
		mfree ( m_slots , MAX_SLOTS * sizeof(Slot) , "Loop" );
	}
	m_slots = NULL;
#ifdef __APPLE__
	closeKqueue();
	memset(m_kqReadEnabled , 0 , sizeof(m_kqReadEnabled ));
	memset(m_kqWriteEnabled, 0 , sizeof(m_kqWriteEnabled));
	m_kqQuickTimerEnabled = false;
	m_kqRealTimerEnabled  = false;
	m_kqUserEventEnabled  = false;
#endif
}

static void sigbadHandler ( int x , siginfo_t *info , void *y ) ;
static void sigpwrHandler ( int x , siginfo_t *info , void *y ) ;
static void sighupHandler ( int x , siginfo_t *info , void *y ) ;
static void sigalrmHandler( int x , siginfo_t *info , void *y ) ;
static void sigvtalrmHandler( int x , siginfo_t *info , void *y ) ;

void Loop::unregisterReadCallback ( int fd, void *state ,
				    void (* callback)(int fd,void *state),
				    bool silent ){
	if ( fd < 0 ) return;
	// from reading
	unregisterCallback ( m_readSlots,fd, state , callback, silent,true );
}

void Loop::unregisterWriteCallback ( int fd, void *state ,
				    void (* callback)(int fd,void *state)){
	// from writing
	unregisterCallback ( m_writeSlots , fd  , state,callback,false,false);
}

void Loop::unregisterSleepCallback ( void *state ,
				     void (* callback)(int fd,void *state)){
	unregisterCallback (m_readSlots,MAX_NUM_FDS,state,callback,false,true);
}

static fd_set s_selectMaskRead;
static fd_set s_selectMaskWrite;
static fd_set s_selectMaskExcept;

static int s_readFds[MAX_NUM_FDS];
static int32_t s_numReadFds = 0;
static int s_writeFds[MAX_NUM_FDS];
static int32_t s_numWriteFds = 0;

void Loop::unregisterCallback ( Slot **slots , int fd , void *state ,
				void (* callback)(int fd,void *state) ,
				bool silent , bool forReading ) {
	// bad fd
	if ( fd < 0 ) {log(LOG_LOGIC,
			   "loop: fd to unregister is negative.");return;}
	// set a flag if we found it
	bool found = false;
	// slots is m_readSlots OR m_writeSlots
	Slot *s        = slots [ fd ];
	Slot *lastSlot = NULL;
	// . keep track of new min tick for sleep callbacks
	// . sleep a min of 40ms so g_now is somewhat up to date
	int32_t min     = 40; // 0x7fffffff;
	int32_t lastMin = min;

	// chain through all callbacks registerd with this fd
	while ( s ) {
		// get the next slot (NULL if no more)
		Slot *next = s->m_next;
		// if we're unregistering a sleep callback
		// we might have to recalculate m_minTick 
		if ( s->m_tick < min ) { lastMin = min; min = s->m_tick; }
		// skip this slot if callbacks don't match
		if ( s->m_callback != callback ) { lastSlot = s; goto skip; }
		// skip this slot if states    don't match
		if ( s->m_state    != state    ) { lastSlot = s; goto skip; }
		// free this slot since it callback matches "callback"
		returnSlot ( s );
		found = true;
		// if the last one, then remove the FD from s_fdList
		// so and clear a bit so doPoll() function is fast
		if ( slots[fd] == s && s->m_next == NULL ) {
			for (int32_t i = 0; i < s_numReadFds ; i++ ) {
				if ( ! forReading ) break;
				if ( s_readFds[i] != fd ) continue;
				s_readFds[i] = s_readFds[s_numReadFds-1];
				s_numReadFds--;
				// remove from select mask too
				FD_CLR(fd,&s_selectMaskRead );
				if ( g_conf.m_logDebugLoop ||
				     g_conf.m_logDebugTcp )
					log("loop: unregistering read "
					    "callback for fd=%i",fd);
				break;
			}
			for (int32_t i = 0; i < s_numWriteFds ; i++ ) {
				if ( forReading ) break;
			 	if ( s_writeFds[i] != fd ) continue;
			 	s_writeFds[i] = s_writeFds[s_numWriteFds-1];
			 	s_numWriteFds--;
			 	// remove from select mask too
			 	FD_CLR(fd,&s_selectMaskWrite);
				if ( g_conf.m_logDebugLoop ||
				     g_conf.m_logDebugTcp )
					log("loop: unregistering write "
					    "callback for fd=%" INT32 " from "
					    "write #wrts=%" INT32 "",
					    (int32_t)fd,
					    (int32_t)s_numWriteFds);
			 	break;
			}
#ifdef __APPLE__
			if ( forReading ) unregisterKqueueEvent(fd,true);
			else              unregisterKqueueEvent(fd,false);
#endif
		}
		// revert back to old min if this is the Slot we're removing
		min = lastMin;
		// excise the previous slot from linked list
		if   ( lastSlot ) lastSlot->m_next = next;
		else              slots[fd]        = next;
		// watch out if we're in the previous callback, we need to
		// fix the linked list in callCallbacks_ass
		if ( s_callbacksNext == s ) s_callbacksNext = next;
	skip:
		// advance to the next slot
		s = next;
	}	
	// set our new minTick if we were unregistering a sleep callback
	if ( fd == MAX_NUM_FDS ) {
		m_minTick = min;
		// . set s_sigWaitTime to m_minTick
		// . 1 billion nanoseconds = 1 second	
		// . m_minTick is in milliseconds, 1000 ms in a second
		// . multiply m_minTick in ms by 1 million to get nano
		setSigWaitTime ( m_minTick );
	}

	// return now if found
	if ( found ) return;
	// . otherwise, bitch if we're not silent
	// . HttpServer.cpp always calls this even if it did not register its
	//   File's fd just to make sure.
	if ( silent ) return;

	return;
	// sometimes the socket is abruptly closed and that calls the
	// unregisterWriteCallback() for us... so skip this
	log(LOG_LOGIC,
	    "loop: unregisterCallback: callback not found (fd=%i).",fd);
}

bool Loop::registerReadCallback  ( int fd,
				   void *state, 
				   void (* callback)(int fd,void *state ) ,
				   int32_t  niceness ) {
#ifdef __APPLE__
	bool needKqueue = false;
	if ( fd >= 0 && fd < MAX_NUM_FDS && ! m_readSlots[fd] )
		needKqueue = true;
#endif
	// the "true" answers the question "for reading?"
	if ( ! addSlot ( true, fd, state, callback, niceness ) )
		return log("loop: Unable to register read callback.");
#ifdef __APPLE__
	if ( needKqueue && ! registerKqueueEvent(fd,true) ) {
		unregisterReadCallback ( fd, state, callback , true );
		return log("loop: Unable to register read callback.");
	}
#endif
	return true;
}


bool Loop::registerWriteCallback ( int fd,
				   void *state, 
				   void (* callback)(int fd, void *state ) ,
				   int32_t  niceness ) {
#ifdef __APPLE__
	bool needKqueue = false;
	if ( fd >= 0 && fd < MAX_NUM_FDS && ! m_writeSlots[fd] )
		needKqueue = true;
#endif
	// the "false" answers the question "for reading?"
	if ( ! addSlot ( false, fd, state, callback, niceness ) )
		return log("loop: Unable to register write callback.");
#ifdef __APPLE__
	if ( needKqueue && ! registerKqueueEvent(fd,false) ) {
		unregisterWriteCallback ( fd, state, callback );
		return log("loop: Unable to register write callback.");
	}
#endif
	return true;
}

// tick is in milliseconds
bool Loop::registerSleepCallback ( int32_t tick ,
				   void *state, 
				   void (* callback)(int fd,void *state ) ,
				   int32_t niceness ) {
	if ( ! addSlot ( true, MAX_NUM_FDS, state, callback , niceness ,tick) )
		return log("loop: Unable to register sleep callback");
	if ( tick < m_minTick ) m_minTick = tick;
	// wait this int32_t in the sig wait loop
	setSigWaitTime ( m_minTick );
	return true;
}

// . returns false and sets g_errno on error
bool Loop::addSlot ( bool forReading , int fd, void *state, 
		     void (* callback)(int fd, void *state), int32_t niceness ,
		     int32_t tick ) {

	// ensure fd is >= 0
	if ( fd < 0 ) {
		g_errno = EBADENGINEER;
		return log(LOG_LOGIC,"loop: fd to register is negative.");
	}
	// sanity
	if ( fd > MAX_NUM_FDS ) {
		log("loop: bad fd of %" INT32 "",(int32_t)fd);
		gbassert(false); 
	}
	// debug note
	if (  forReading && (g_conf.m_logDebugLoop || g_conf.m_logDebugTcp) )
		log("loop: registering read callback sd=%i",fd);
	else if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
		log("loop: registering write callback sd=%i",fd);

	// . ensure fd not already registered with this callback/state
	// . prevent dups so you can keep calling register w/o fear
	Slot *s;
	if ( forReading ) s = m_readSlots  [ fd ];
	else              s = m_writeSlots [ fd ];
	while ( s ) {
		if ( s->m_callback == callback &&
		     s->m_state    == state      ) {
			log(LOG_LOGIC,"loop: fd=%i is already registered.",fd);
			return true;
		}
		s = s->m_next;
	}
	// . make a new slot
	// . TODO: implement mprimealloc() to pre-alloc slots for us for speed
	s = getEmptySlot ( );
	if ( ! s ) return false;
	// for pointing to slot already in position for fd
	Slot *next ;
	// store ourselves in the slot for this fd
	if ( forReading ) {
		next = m_readSlots [ fd ];
		m_readSlots  [ fd ] = s;
		// if not already registered, add to list
		if ( fd<MAX_NUM_FDS && ! FD_ISSET ( fd,&s_selectMaskRead ) ) {
			s_readFds[s_numReadFds++] = fd;
			FD_SET ( fd,&s_selectMaskRead  );
			// sanity
			gbassert_false( s_numReadFds>MAX_NUM_FDS);
		}
	}
	else {
	 	next = m_writeSlots [ fd ];
	 	m_writeSlots [ fd ] = s;
	 	// if not already registered, add to list
	 	if ( fd<MAX_NUM_FDS && ! FD_ISSET ( fd,&s_selectMaskWrite ) ) {
	 		s_writeFds[s_numWriteFds++] = fd;
	 		FD_SET ( fd,&s_selectMaskWrite  );
	 		// sanity
	 		gbassert_false( s_numWriteFds>MAX_NUM_FDS);
	 	}
	}
	// set our callback and state
	s->m_callback  = callback;
	s->m_state     = state;
	// point to the guy that was registered for fd before us
	s->m_next      = next;
	// save our niceness for doPoll()
	s->m_niceness  = niceness;
	// store the tick for sleep wrappers (should be max for others)
	s->m_tick      = tick;
	// and the last called time for sleep wrappers only really
	if ( fd == MAX_NUM_FDS ) s->m_lastCall = gettimeofdayInMilliseconds();
	// if fd == MAX_NUM_FDS if it's a sleep callback
	if ( fd == MAX_NUM_FDS ) return true;
	// watch out for big bogus fds used for thread exit callbacks
	if ( fd >  MAX_NUM_FDS ) return true;
	// set fd non-blocking
	return setNonBlocking ( fd , niceness ) ;
}

// . now make sure we're listening for an interrupt on this fd
// . set it non-blocing and enable signal catching for it
// . listen for an interrupt for this fd
bool Loop::setNonBlocking ( int fd , int32_t niceness ) {
 retry:
	int flags = fcntl ( fd , F_GETFL ) ;
	if ( flags < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry;
		g_errno = errno;
		return log("loop: fcntl(F_GETFL): %s.",strerror(errno));
	}
 retry9:
#ifdef __APPLE__
	int newFlags = flags | O_NONBLOCK;
#else
	int newFlags = flags | O_NONBLOCK | O_ASYNC;
#endif
	if ( fcntl ( fd, F_SETFL, newFlags ) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry9;
		g_errno = errno;
		return log("loop: fcntl(NONBLOCK): %s.",strerror(errno));
	}

	// we use select()/poll now so skip stuff below
	return true;
}

// . if "forReading" is true  call callbacks registered for reading on "fd" 
// . if "forReading" is false call callbacks registered for writing on "fd" 
// . if fd is MAX_NUM_FDS and "forReading" is true call all sleepy callbacks
void Loop::callCallbacks_ass ( bool forReading , int fd , int64_t now ,
			       int32_t niceness ) {

	// save the g_errno to send to all callbacks
	int saved_errno = g_errno;
	// get the first Slot in the chain that is waiting on this fd
	Slot *s ;
	if ( forReading ) s = m_readSlots  [ fd ];
	else              s = m_writeSlots [ fd ];
	// ensure we called something
	int32_t numCalled = 0;

	// a hack fix
	if ( niceness == -1 && m_inQuickPoll ) niceness = 0;

	// . now call all the callbacks
	// . most will re-register themselves (i.e. call registerCallback...()
	while ( s ) {
		// skip this slot if he has no callback
		if ( ! s->m_callback ) continue;
		// NOTE: callback can unregister fd for Slot s, so get next
		s_callbacksNext = s->m_next;
		// watch out if clock was set back
		if ( s->m_lastCall > now ) s->m_lastCall = now;
		// if we're a sleep callback, check to make sure not premature
		if ( fd == MAX_NUM_FDS && s->m_lastCall + s->m_tick > now ) {
			s = s_callbacksNext; continue; }
		// skip if not a niceness match
		if ( niceness == 0 && s->m_niceness != 0 ) {
			s = s_callbacksNext; continue; }
		// update the lastCall timestamp for this slot
		if ( fd == MAX_NUM_FDS ) s->m_lastCall = now;

		// log it now
		if (  g_conf.m_logDebugLoop )
			log(LOG_DEBUG,"loop: enter fd callback fd=%" INT32 " "
			    "nice=%" INT32 "",(int32_t)fd,(int32_t)s->m_niceness);

		// sanity check. -1 no longer supported
		gbassert_false( s->m_niceness < 0 );

		// save it
		int32_t saved = g_niceness;
		// set the niceness
		g_niceness = s->m_niceness;
		// make sure not 2
		if ( g_niceness >= 2 ) g_niceness = 1;

		s->m_callback ( fd , s->m_state );

		// restore it
		g_niceness = saved;

		// log it now
		if ( g_conf.m_logDebugLoop )
			log(LOG_DEBUG,"loop: exit fd callback fd=%" INT32 " "
			    "nice=%" INT32 "", (int32_t)fd,(int32_t)s->m_niceness);

		// inc the flag
		numCalled++;
		// reset g_errno so all callbacks for this fd get same g_errno
		g_errno = saved_errno;
		// get the next n (will be -1 if no slot after it)
		s = s_callbacksNext;
	}
	s_callbacksNext = NULL;
}

Loop::Loop ( ) {
	// . default sig wait time to 10 ms (10,000,000 nanoseconds)
	// . 1 billion nanoseconds = 1 second	
	setSigWaitTime ( 1000 /*ms*/ );

	s_sigWaitTime2.tv_sec  = 0;
	s_sigWaitTime2.tv_nsec = 0;
	s_sigWaitTimePtr = &s_sigWaitTime;

	m_inQuickPoll      = false;
	m_needsToQuickPoll = false;
	m_canQuickPoll     = false;
	m_isDoingLoop      = false;

	// set all callbacks to NULL so we know they're empty
	for ( int32_t i = 0 ; i < MAX_NUM_FDS+2 ; i++ ) {
		m_readSlots [i] = NULL;
		m_writeSlots[i] = NULL;
	}
	m_slots = NULL;
#ifdef __APPLE__
	m_kq = -1;
	memset(m_kqReadEnabled , 0 , sizeof(m_kqReadEnabled ));
	memset(m_kqWriteEnabled, 0 , sizeof(m_kqWriteEnabled));
	m_kqQuickInterval    = QUICKPOLL_INTERVAL;
	m_kqRealInterval     = 1;
	m_kqQuickTimerEnabled = false;
	m_kqRealTimerEnabled  = false;
	m_kqUserEventEnabled  = false;
#endif
}

// free all slots from addSlots
Loop::~Loop ( ) {
	reset();
}

// returns NULL and sets g_errno if none are left
Slot *Loop::getEmptySlot ( ) {
	Slot *s = m_head;
	if ( ! s ) {
		g_errno = EBUFTOOSMALL; 
		log("loop: No empty slots available. "
		    "Increase #define MAX_SLOTS.");
		return NULL;
	}
	m_head = s->m_nextAvail;
	return s;
}

void Loop::returnSlot ( Slot *s ) {
	s->m_nextAvail = m_head;
	m_head = s;
}

#ifdef __APPLE__
bool Loop::initKqueue() {
	if ( m_kq >= 0 ) return true;
	m_kq = kqueue();
	if ( m_kq < 0 ) {
		log("loop: kqueue(): %s", mstrerror(errno));
		return false;
	}
	memset(m_kqReadEnabled , 0 , sizeof(m_kqReadEnabled ));
	memset(m_kqWriteEnabled, 0 , sizeof(m_kqWriteEnabled));
	return true;
}

void Loop::closeKqueue() {
	if ( m_kq < 0 ) return;
	close ( m_kq );
	m_kq = -1;
	m_kqQuickTimerEnabled = false;
	m_kqRealTimerEnabled  = false;
	m_kqUserEventEnabled  = false;
}

bool Loop::registerKqueueEvent(int fd, bool forReading) {
	if ( fd < 0 || fd >= MAX_NUM_FDS ) return true;
	if ( m_kq < 0 ) return false;
	bool *enabled = forReading ? m_kqReadEnabled : m_kqWriteEnabled;
	if ( enabled[fd] ) return true;
	struct kevent kev;
	EV_SET(&kev, fd, forReading ? EVFILT_READ : EVFILT_WRITE,
	       EV_ADD | EV_CLEAR, 0, 0, NULL);
	if ( kevent(m_kq, &kev, 1, NULL, 0, NULL) < 0 ) {
		log("loop: kevent add fd=%i: %s", fd, mstrerror(errno));
		return false;
	}
	enabled[fd] = true;
	return true;
}

void Loop::unregisterKqueueEvent(int fd, bool forReading) {
	if ( fd < 0 || fd >= MAX_NUM_FDS ) return;
	if ( m_kq < 0 ) return;
	bool *enabled = forReading ? m_kqReadEnabled : m_kqWriteEnabled;
	if ( ! enabled[fd] ) return;
	struct kevent kev;
	EV_SET(&kev, fd, forReading ? EVFILT_READ : EVFILT_WRITE,
	       EV_DELETE, 0, 0, NULL);
	if ( kevent(m_kq, &kev, 1, NULL, 0, NULL) < 0 && errno != ENOENT )
		log("loop: kevent delete fd=%i: %s", fd, mstrerror(errno));
	enabled[fd] = false;
}

bool Loop::addKqueueTimer(uintptr_t ident, int intervalMs) {
	if ( m_kq < 0 ) return false;
	struct kevent kev;
	EV_SET(&kev, ident, EVFILT_TIMER, EV_ADD | EV_CLEAR,
	       0, intervalMs, NULL);
	if ( kevent(m_kq, &kev, 1, NULL, 0, NULL) < 0 ) {
		log("loop: kevent timer add id=%" PRIuPTR ": %s",
		    ident, mstrerror(errno));
		return false;
	}
	if ( ident == KQ_TIMER_QUICK ) {
		m_kqQuickTimerEnabled = true;
		m_kqQuickInterval    = intervalMs;
	}
	else if ( ident == KQ_TIMER_REAL ) {
		m_kqRealTimerEnabled = true;
		m_kqRealInterval     = intervalMs;
	}
	return true;
}

void Loop::deleteKqueueTimer(uintptr_t ident) {
	if ( m_kq < 0 ) return;
	struct kevent kev;
	EV_SET(&kev, ident, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
	if ( kevent(m_kq, &kev, 1, NULL, 0, NULL) < 0 && errno != ENOENT )
		log("loop: kevent timer delete id=%" PRIuPTR ": %s",
		    ident, mstrerror(errno));
	if ( ident == KQ_TIMER_QUICK ) m_kqQuickTimerEnabled = false;
	else if ( ident == KQ_TIMER_REAL ) m_kqRealTimerEnabled = false;
}
#endif


// . come here when we get a GB_SIGRTMIN+X signal etc.
// . do not call anything from here because the purpose of this is to just
//   queue the signals up and DO DEDUPING which linux does not do causing
//   the sigqueue to overflow.
// . we should break out of the sleep loop after the signal is handled
//   so we can handle/process the queued signals properly. 'man sleep'
//   states "sleep()  makes  the  calling  process  sleep until seconds 
//   seconds have elapsed or a signal arrives which is not ignored."
void sigHandlerQueue_r ( int x , siginfo_t *info , void *v ) {

	// if we just needed to cleanup a thread
	if ( info->si_signo == SIGCHLD ) {
		g_numSigChlds++;
		// this has no fd really, Threads.cpp just sends it when
		// the thread is done
		g_threads.m_needsCleanup = true;
		return;
	}

	if ( info->si_signo == SIGPIPE ) {
		g_numSigPipes++;
		return;
	}

	if ( info->si_signo == SIGIO ) {
		g_numSigIOs++;
		return;
	}

	if ( info->si_code == SI_QUEUE ) {
		g_numSigQueues++;
		g_threads.m_needsCleanup = true;
		return;
	}

	// wtf is this?
	g_numSigOthers++;

	return;
}



bool Loop::init ( ) {

	// clear this up here before using in doPoll()
	FD_ZERO(&s_selectMaskRead);
	FD_ZERO(&s_selectMaskWrite);
	FD_ZERO(&s_selectMaskExcept);
#ifdef __APPLE__
	if ( ! initKqueue() ) return false;
	if ( ! addKqueueTimer(KQ_TIMER_QUICK, m_kqQuickInterval ) ) return false;
	if ( ! addKqueueTimer(KQ_TIMER_REAL , m_kqRealInterval  ) ) return false;
	struct kevent wakeKev;
	EV_SET(&wakeKev, KQ_EVENT_WAKE, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if ( kevent(m_kq, &wakeKev, 1, NULL, 0, NULL) < 0 ) {
		log("loop: kevent add wakeup: %s", mstrerror(errno));
		return false;
	}
	m_kqUserEventEnabled = true;
	int sigsToWatch[] = { SIGHUP, SIGTERM, SIGINT };
	for ( size_t i = 0 ; i < sizeof(sigsToWatch)/sizeof(int) ; i++ ) {
		struct kevent sigKev;
		EV_SET(&sigKev, sigsToWatch[i], EVFILT_SIGNAL,
		       EV_ADD | EV_CLEAR, 0, 0, NULL);
		if ( kevent(m_kq, &sigKev, 1, NULL, 0, NULL) < 0 )
			log("loop: kevent add signal %i: %s",
			    sigsToWatch[i], mstrerror(errno));
	}
#endif

	// redhat 9's NPTL doesn't like our async signals
	if ( ! g_conf.m_allowAsyncSignals ) g_isHot = false;
#ifdef _VALGRIND_
	g_isHot = false;
#endif
	// sighupHandler() will set this to true so we know when to shutdown
	m_shutdown  = 0;
	// . reset this cuz we have no sleep callbacks right now
	// . sleep a min of 40ms so g_now is somewhat up to date
	m_minTick = 40; //0x7fffffff;
	// reset the need to poll flag
	m_needToPoll = false;
	// let 'em know if we're hot
	if ( g_isHot ) log ( LOG_INIT , "loop: Using asynchronous signals "
			     "for udp server.");
	// make slots
	m_slots = (Slot *) mmalloc ( MAX_SLOTS * (int32_t)sizeof(Slot) , "Loop" );
	if ( ! m_slots ) return false;
	// log it
	log(LOG_DEBUG,"loop: Allocated %" INT32 " bytes for %" INT32 " callbacks.",
	     MAX_SLOTS * (int32_t)sizeof(Slot),(int32_t)MAX_SLOTS);
	// init link list ptr
	for ( int32_t i = 0 ; i < MAX_SLOTS - 1 ; i++ ) {
		m_slots[i].m_nextAvail = &m_slots[i+1];
	}
	m_slots[MAX_SLOTS - 1].m_nextAvail = NULL;
	m_head = &m_slots[0];
	m_tail = &m_slots[MAX_SLOTS - 1];
	// set of signals to block
	sigset_t sigs;
	sigemptyset ( &sigs                );	
	sigaddset   ( &sigs , SIGPIPE      ); //if we write to a close socket
	sigaddset   ( &sigs , SIGCHLD      );

#ifdef PTHREADS
	// now since we took out SIGIO... (see below)
	// we should ignore this signal so it doesn't suddenly stop the gb
	// process since we took out the SIGIO handler because newer kernels
	// were throwing SIGIO signals ALL the time, on every datagram
	// send/receive it seemed and bogged us down.
	sigaddset   ( &sigs , SIGIO );
#endif
	// . block on any signals in this set (in addition to current sigs)
	// . use SIG_UNBLOCK to remove signals from block list
	// . this returns -1 and sets g_errno on error
	// . we block a signal so it does not interrupt us, then we can
	//   take it off using our call to sigtimedwait()
	// . allow it to interrupt us now and we will queue it ourselves
	//   to prevent the linux queue from overflowing
	// . see 'cat /proc/<pid>/status | grep SigQ' output to see if
	//   overflow occurs. linux does not dedup the signals so when a
	//   host cpu usage hits 100% it seems to miss a ton of signals. 
	//   i suspect the culprit is pthread_create() so we need to get
	//   thread pools out soon.
	// . now we are handling the signals and queueing them ourselves
	//   so comment out this sigprocmask() call
	struct sigaction sa2;
	// . sa_mask is the set of signals that should be blocked when
	//   we're handling the GB_SIGRTMIN, make this empty
	// . GB_SIGRTMIN signals will be automatically blocked while we're
	//   handling a SIGIO signal, so don't worry about that
	// . what sigs should be blocked when in our handler? the same
	//   sigs we are handling i guess
	gbmemcpy ( &sa2.sa_mask , &sigs , sizeof(sigs) );
	sa2.sa_flags = SA_SIGINFO ; //| SA_ONESHOT;
	// call this function
	sa2.sa_sigaction = sigHandlerQueue_r;
	g_errno = 0;
	if ( sigaction ( SIGPIPE, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( sigaction ( SIGCHLD, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( sigaction ( SIGIO, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction(): %s.", mstrerror(g_errno) );
	
	struct sigaction sa;
	// . sa_mask is the set of signals that should be blocked when
	//   we're handling the signal, make this empty
	// . GB_SIGRTMIN signals will be automatically blocked while we're
	//   handling a SIGIO signal, so don't worry about that
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO ; // | SA_ONESHOT;

	// handle HUP signals gracefully by saving and shutting down
	sa.sa_sigaction = sighupHandler;
	if ( sigaction ( SIGHUP , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGHUP: %s.", mstrerror(errno));
	if ( sigaction ( SIGTERM, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGTERM: %s.", mstrerror(errno));

	// we should save our data on segv, sigill, sigfpe, sigbus
	sa.sa_sigaction = sigbadHandler;
	if ( sigaction ( SIGSEGV, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGSEGV: %s.", mstrerror(errno));
	if ( sigaction ( SIGILL , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGILL: %s.", mstrerror(errno));
	if ( sigaction ( SIGFPE , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGFPE: %s.", mstrerror(errno));
	if ( sigaction ( SIGBUS , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGBUS: %s.", mstrerror(errno));

#ifndef __APPLE__
	// if the UPS is about to go off it sends a SIGPWR
	sa.sa_sigaction = sigpwrHandler;
	if ( sigaction ( SIGPWR, &sa, 0 ) < 0 ) g_errno = errno;
#endif

	//now set up our alarm for quickpoll
	m_quickInterrupt.it_value.tv_sec = 0;
	m_quickInterrupt.it_value.tv_usec = QUICKPOLL_INTERVAL * 1000;
	m_quickInterrupt.it_interval.tv_sec = 0;
	m_quickInterrupt.it_interval.tv_usec = QUICKPOLL_INTERVAL * 1000;

	m_realInterrupt.it_value.tv_sec = 0;
	// 1000 microseconds in a millisecond
	m_realInterrupt.it_value.tv_usec = 1 * 1000;
	m_realInterrupt.it_interval.tv_sec = 0;
	m_realInterrupt.it_interval.tv_usec = 1 * 1000;


 	m_noInterrupt.it_value.tv_sec = 0;
 	m_noInterrupt.it_value.tv_usec = 0;
 	m_noInterrupt.it_interval.tv_sec = 0;
 	m_noInterrupt.it_interval.tv_usec = 0;

	// make this 10ms i guess
	setitimer(ITIMER_REAL, &m_realInterrupt, NULL);
	// this is 10ms
	setitimer(ITIMER_VIRTUAL, &m_quickInterrupt, NULL);

	sa.sa_sigaction = sigalrmHandler;
	// it's gotta be real time, not virtual cpu time now
	if ( sigaction ( SIGALRM, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) return log("loop: sigaction: %s.", mstrerror(errno));

	// block sigvtalarm
	sa.sa_sigaction = sigvtalrmHandler;
	if ( sigaction ( SIGVTALRM, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGVTALRM: %s.", mstrerror(errno));

	// success
	return true;
}

// TODO: if we get a segfault while saving, what then?
void sigpwrHandler ( int x , siginfo_t *info , void *y ) {
	// let main process know to shutdown
	g_loop.m_shutdown = 3;
}

#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
void printStackTrace ( int signum , siginfo_t *info , void *ptr ) {

	logf(LOG_DEBUG,"gb: Printing stack trace...");

	if ( g_inMemFunction ) {
		logf(LOG_DEBUG,"gb: in mem function not doing backtrace");
		return;
	}

	static void *s_bt[200];
	int sz = backtrace(s_bt, 200);
	char **bt_strings = backtrace_symbols(s_bt, sz);
	for( int i = 0; i < sz; ++i) {
		Dl_info info;
		if (dladdr(s_bt[i], &info)) {
			int status;
			char *demangled = abi::__cxa_demangle(info.dli_sname, NULL, 0, &status);
			logf(LOG_DEBUG,"%s: %s", status == 0 ? demangled : info.dli_sname, bt_strings[i]);
			free(demangled);
		} else {
			logf(LOG_DEBUG,"%s", bt_strings[i]);
		}
	}
}


// TODO: if we get a segfault while saving, what then?
void sigbadHandler ( int x , siginfo_t *info , void *y ) {

	// thread should set it errno to 0x7fffffff which means that
	// Threads.cpp should not look for its ThreadEntry::m_isDone flag
	// to be set before calling waitpid() on it
	if ( g_threads.amThread() ) errno = 0x7fffffff;

	// turn off sigalarms
	g_loop.disableTimer();

	log("loop: sigbadhandler. disabling handler from recall.");
	// . don't allow this handler to be called again
	// . does this work if we're in a thread?
	struct sigaction sa;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO ; //| SA_ONESHOT;
	sa.sa_sigaction = NULL;
	sigaction ( SIGSEGV, &sa, 0 ) ;
	sigaction ( SIGILL , &sa, 0 ) ;
	sigaction ( SIGFPE , &sa, 0 ) ;
	sigaction ( SIGBUS , &sa, 0 ) ;
	sigaction ( SIGQUIT, &sa, 0 ) ;
	sigaction ( SIGSYS , &sa, 0 ) ;
	//sigaction ( SIGALRM, &sa, 0 ) ;
	// if we've already been here, or don't need to be, then bail
	if ( g_loop.m_shutdown ) {
		log("loop: sigbadhandler. shutdown already called.");
		return;
	}

	// unwind
	printStackTrace( x , info , y );


	// if we're a thread, let main process know to shutdown
	g_loop.m_shutdown = 2;
	log("loop: sigbadhandler. trying to save now. mode=%" INT32 "",
	    (int32_t)g_process.m_mode);
	// . this will save all Rdb's 
	// . if "urgent" is true it will dump core
	// . if "urgent" is true it won't broadcast its shutdown to all hosts
	g_process.shutdown ( true );
}

static void quickpollTimerCore ( int x , siginfo_t *info , void *y ) {
#ifdef PTHREADS
	if ( g_threads.amThread() ) return;
#endif

	g_numVTAlarms++;

	if ( g_callSlot && g_niceness == 0 ) {
		if ( g_callSlot->m_transId == g_lastTransId ) g_transIdCount++;
		else                                          g_transIdCount=1;
		g_lastTransId = g_callSlot->m_transId;
		bool logIt = (g_transIdCount >= 4);
		if ( g_callSlot->m_msgType == 0x99 && g_transIdCount != 50 )
			logIt = false;
#ifdef PTHREADS
		logIt = false;
#endif
		if ( logIt ) {
			if ( g_callSlot->m_callback )
				log("loop: msg type 0x%hhx reply callback hogging cpu for %" INT32 " ticks",
				    g_callSlot->m_msgType, g_transIdCount);
			else
				log("loop: msg type 0x%hhx handler hogging cpu for %" INT32 " ticks",
				    g_callSlot->m_msgType, g_transIdCount);
		}
	}

	g_nowApprox += QUICKPOLL_INTERVAL;

	if ( g_loop.m_inQuickPoll &&
	     g_niceness != 0 &&
	     ! g_conf.m_testParserEnabled &&
	     ! g_conf.m_testSpiderEnabled &&
	     ! g_conf.m_testSearchEnabled &&
	     ! g_inPageParser &&
	     ! g_inPageInject ) {
#ifndef PTHREADS
		log("loop: crap crap crap!!!");
#endif
	}
	if ( g_loop.m_inQuickPoll ) return;
	if ( ! g_conf.m_useQuickpoll ) return;

	g_loop.m_needsToQuickPoll = true;
	if ( g_niceness == 1 ) g_missedQuickPolls++;
	else if ( g_niceness == 0 ) g_missedQuickPolls = 0;

	if ( g_niceness == 1 && g_missedQuickPolls >= 4 ) {
#ifndef PTHREADS
		log("loop: missed quickpoll. Dumping stack.");
		printStackTrace( x , info , y );
#endif
	}

	if ( g_process.m_lastHeartbeatApprox == 0 ) return;
	if ( g_conf.m_maxHeartbeatDelay <= 0 ) return;
	if ( g_nowApprox - g_process.m_lastHeartbeatApprox >
	     g_conf.m_maxHeartbeatDelay ) {
#ifndef PTHREADS
		logf(LOG_DEBUG,"gb: CPU seems blocked. Dumping stack.");
		printStackTrace( x , info , y );
#endif
	}
}

void sigvtalrmHandler ( int x , siginfo_t *info , void *y ) {
	quickpollTimerCore ( x , info , y );
}

float g_cpuUsage = 0.0;

static void realTimerCore ( int x , siginfo_t *info , void *y ) {
#ifdef PTHREADS
	if ( g_threads.amThread() ) return;
#endif

	g_clockNeedsUpdate = true;
	g_numAlarms++;

	if ( ! g_inWaitState )
		g_cpuUsage = .99 * g_cpuUsage + .01 * 100;
	else
		g_cpuUsage = .99 * g_cpuUsage + .01 * 000;

	if ( g_profiler.m_realTimeProfilerRunning )
		g_profiler.getStackFrame(0);
}

void sigalrmHandler ( int x , siginfo_t *info , void *y ) {
	realTimerCore ( x , info , y );
}

// shit, we can't make this realtime!! RdbClose() cannot be called by a
// real time sig handler
void sighupHandler ( int x , siginfo_t *info , void *y ) {
	// let main process know to shutdown
	g_loop.m_shutdown = 1;
}

// . keep a timestamp for the last time we called the sleep callbacks
// . we have to call those every 1 second
int64_t s_lastTime = 0;

bool Loop::runLoop ( ) {

#ifndef _POLLONLY_
	// set of signals to watch for
	sigset_t sigs0;
	// clear all signals from the set
	sigemptyset ( &sigs0 );
	sigaddset ( &sigs0, SIGPIPE      );
	sigaddset ( &sigs0, SIGCHLD      );
	sigaddset ( &sigs0, SIGIO );
#endif
	s_lastTime = 0;

	// . allow us to be interrupted
	// . UNBLOCKs GB_SIGRTMIN
	// . makes g_udpServer2 quite jumpy
	g_loop.interruptsOn();

	m_isDoingLoop = true;

	// . now loop forever waiting for signals
	// . but every second check for timer-based events

 BIGLOOP:

	g_now = gettimeofdayInMilliseconds();
		
	//set the time back to its exact value and reset
	//the timer.
	g_nowApprox = g_now;
	m_lastPollTime = g_now;
	m_needsToQuickPoll = false;

	g_errno = 0;

 	if ( m_shutdown ) {
		// a msg
		if      (m_shutdown==1) 
			log(LOG_INIT,"loop: got SIGHUP or SIGTERM.");
		else if (m_shutdown==2) 
			log(LOG_INIT,"loop: got SIGBAD in thread.");
		else                    
			log(LOG_INIT,"loop: got SIGPWR.");
		// . turn off interrupts here because it doesn't help to do
		//   it in the thread
		// . TODO: turn off signals for sigbadhandler()
		interruptsOff();
		log ( LOG_INIT ,"loop: Saving and shutting down urgently.");
		// . this will save all Rdb's and dump core
		// . since "urgent" is true it won't broadcast its shutdown
		//   to all hosts
		g_process.shutdown ( true );
	}


	//
	// THE HEART OF GB. process events/signals on FDs.
	//
	doPoll();

 	goto BIGLOOP;

 	// make compiler happy
 	return 0;
}

//--- TODO: flush the signal queue after polling until done
//--- are we getting stale signals resolved by flush so we get
//--- read event on a socket that isn't in read mode???
// TODO: set signal handler to SIG_DFL to prevent signals from queuing up now
// . this handles high priority fds first (lowest niceness)
void Loop::doPoll ( ) {
	m_needToPoll = false;
#ifdef __APPLE__
	if ( m_kq >= 0 ) {
		if ( g_conf.m_logDebugLoop) log(LOG_DEBUG,"loop: Entered doPoll.");
		if ( g_log.needsPrinting() ) g_log.printBuf();
		if ( g_someAreQueued ) g_someAreQueued = false;
		if(g_udpServer.needBottom()) g_udpServer.makeCallbacks_ass ( 1 );

		const int MAX_KQUEUE_EVENTS = 128;
		struct kevent eventList[MAX_KQUEUE_EVENTS];
		const struct timespec *timeoutPtr = NULL;
		struct timespec ts;
		if ( m_inQuickPoll ) {
			ts.tv_sec  = 0;
			ts.tv_nsec = 0;
			timeoutPtr = &ts;
		}

		g_inWaitState = true;
		int nev;
againKq:
		nev = kevent(m_kq, NULL, 0, eventList, MAX_KQUEUE_EVENTS,
			     timeoutPtr);
		if ( nev < 0 && errno == EINTR ) goto againKq;
		g_inWaitState = false;
		if ( nev < 0 ) {
			log("loop: kevent: %s", mstrerror(errno));
			return;
		}
		if ( g_conf.m_logDebugLoop )
			log("loop: out kevent n=%" INT32 "", (int32_t)nev);

		g_now = gettimeofdayInMilliseconds();
		g_threads.timedCleanUp(-3,0);

		int readFds [MAX_KQUEUE_EVENTS];
		int writeFds[MAX_KQUEUE_EVENTS];
		int numRead = 0;
		int numWrite = 0;
		for ( int32_t i = 0 ; i < nev ; i++ ) {
			if ( eventList[i].filter == EVFILT_READ ) {
				if ( numRead < MAX_KQUEUE_EVENTS )
					readFds[numRead++] = (int)eventList[i].ident;
			}
			else if ( eventList[i].filter == EVFILT_WRITE ) {
				if ( numWrite < MAX_KQUEUE_EVENTS )
					writeFds[numWrite++] = (int)eventList[i].ident;
			}
			else if ( eventList[i].filter == EVFILT_TIMER ) {
				if ( eventList[i].ident == KQ_TIMER_QUICK )
					quickpollTimerCore ( 0 , NULL , NULL );
				else if ( eventList[i].ident == KQ_TIMER_REAL )
					realTimerCore ( 0 , NULL , NULL );
			}
			else if ( eventList[i].filter == EVFILT_SIGNAL ) {
				int signo = (int)eventList[i].ident;
				if ( signo == SIGHUP || signo == SIGTERM )
					sighupHandler ( signo , NULL , NULL );
				else if ( signo == SIGINT )
					g_loop.m_shutdown = 1;
			}
			else if ( eventList[i].filter == EVFILT_USER ) {
				// nothing to do; event merely wakes the loop
			}
		}

		for ( int32_t i = 0 ; i < numRead ; i++ )
			callCallbacks_ass ( true , readFds[i] , g_now , 0 );
		for ( int32_t i = 0 ; i < numWrite ; i++ )
			callCallbacks_ass ( false, writeFds[i], g_now , 0 );

		g_threads.timedCleanUp(-3,0);

		if ( ! m_inQuickPoll ) {
			for ( int32_t i = 0 ; i < numRead ; i++ )
				callCallbacks_ass ( true , readFds[i] , g_now , 1 );
			for ( int32_t i = 0 ; i < numWrite ; i++ )
				callCallbacks_ass ( false, writeFds[i], g_now , 1 );
		}

		g_threads.timedCleanUp(-4,MAX_NICENESS);

		g_now = gettimeofdayInMilliseconds();
		int32_t elapsed = g_now - s_lastTime;
		if ( elapsed < 0 ) elapsed = m_minTick;
		if ( elapsed >= m_minTick ) {
			callCallbacks_ass ( true , MAX_NUM_FDS , g_now );
			s_lastTime = g_now;
			g_threads.timedCleanUp(-4,MAX_NICENESS);
		}
		if ( g_conf.m_logDebugLoop ) log(LOG_DEBUG,"loop: Exited doPoll.");
		return;
	}
#endif
	// debug msg
	if ( g_conf.m_logDebugLoop) log(LOG_DEBUG,"loop: Entered doPoll.");
	// print log
	if ( g_log.needsPrinting() ) g_log.printBuf();
	 
	// sigqueue() might have been called from a hot udp server and 
	// we queued some handlers to be called
	if ( g_someAreQueued ) {
		// assume none are queued now, we may get interrupted
		// and it may get set back to true
		g_someAreQueued = false;
	}
	if(g_udpServer.needBottom()) g_udpServer.makeCallbacks_ass ( 1 );

	int32_t n;
	timeval v;
	v.tv_sec  = 0;
	if ( m_inQuickPoll ) v.tv_usec = 0;
	// 10ms for sleepcallbacks so they can be called...
	// and we need this to be the same as sigalrmhandler() since we
	// keep track of cpu usage here too, since sigalrmhandler is "VT"
	// based it only goes off when that much "cpu time" has elapsed.
	else                 v.tv_usec = QUICKPOLL_INTERVAL * 1000;  

	// gotta copy to our own since bits get cleared by select() function
	fd_set readfds;
	fd_set writefds;

 again:

	gbmemcpy ( &readfds, &s_selectMaskRead , sizeof(fd_set) );
	gbmemcpy ( &writefds, &s_selectMaskWrite , sizeof(fd_set) );

	if ( g_conf.m_logDebugLoop )
		log("loop: in select");

	// used to measure cpu usage. sigalarm needs to know if we are
	// sitting idle in select() or are actively doing something w/ the cpu
	g_inWaitState = true;

	// . poll the fd's searching for socket closes
	// . the sigalrms and sigvtalrms and SIGCHLDs knock us out of this
	//   select() with n < 0 and errno equal to EINTR.
	// . crap the sigalarms kick us out here every 1ms. i noticed
	//   then when running disableTimer() above and we don't get
	//   any EINTRs... can we mask those out here? it only seems to be
	//   the SIGALRMs not the SIGVTALRMs that interrupt us.
	n = select (MAX_NUM_FDS, 
		    &readfds,
		    &writefds,
		    NULL,
		    &v );

	g_inWaitState = false;

	if ( n >= 0 ) errno = 0;

	if ( g_conf.m_logDebugLoop )
		log("loop: out select n=%" INT32 " errno=%" INT32 " errnomsg=%s "
		    "ms_wait=%i",
		    (int32_t)n,(int32_t)errno,mstrerror(errno),
		    (int)v.tv_sec*1000);

	if ( n < 0 ) { 
		// valgrind
		if ( errno == EINTR ) {
			// got it. if we get a sig alarm or vt alarm or
			// SIGCHLD (from Threads.cpp) we end up here.

			// if shutting down was it a sigterm ?
			if ( m_shutdown ) goto again;
			// handle returned threads for niceness 0
			g_threads.timedCleanUp(-3,0); // 3 ms
			if ( m_inQuickPoll ) goto again;
			// high niceness threads
			g_threads.timedCleanUp(-4,MAX_NICENESS); //3 ms

			goto again;
		}
		g_errno = errno;
		log("loop: select: %s.",strerror(g_errno));
		return;
	}

	// debug msg
	if ( g_conf.m_logDebugLoop) 
		logf(LOG_DEBUG,"loop: Got %" INT32 " fds waiting.",n);

	for ( int32_t i = 0 ; 
	      (g_conf.m_logDebugLoop || g_conf.m_logDebugTcp) && i<MAX_NUM_FDS;
	      i++){
	  	// continue if not set for reading
		 if ( FD_ISSET ( i , &readfds ) )
			 log("loop: fd=%" INT32 " is on for read qp=%i",i,
			     (int)m_inQuickPoll);
	 	if ( FD_ISSET ( i , &writefds ) )
			log("loop: fd=%" INT32 " is on for write qp=%i",i,
			    (int)m_inQuickPoll);
	}

	// a Slot ptr
	Slot *s;
	g_now = gettimeofdayInMilliseconds();

	// handle returned threads for niceness 0
	g_threads.timedCleanUp(-3,0); // 3 ms


	bool calledOne = false;

	// now keep this fast, too. just check fds we need to.
	for ( int32_t i = 0 ; i < s_numReadFds ; i++ ) {
		if ( n == 0 ) break;
		int fd = s_readFds[i];
	 	s = m_readSlots  [ fd ];
	 	// if niceness is not 0, handle it below
		if ( s && s->m_niceness > 0 ) continue;
		// must be set
		if ( ! FD_ISSET ( fd , &readfds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling cback0 niceness=%" INT32 " "
			    "fd=%i", s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (true,fd, g_now,0);//read?
	}
	for ( int32_t i = 0 ; i < s_numWriteFds ; i++ ) {
		if ( n == 0 ) break;
		int fd = s_writeFds[i];
	 	s = m_writeSlots  [ fd ];
	 	// if niceness is not 0, handle it below
		if ( s && s->m_niceness > 0 ) continue;
		// fds are always ready for writing so take this out.
		if ( ! FD_ISSET ( fd , &writefds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling wcback0 niceness=%" INT32 " fd=%i"
			    , s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (false,fd, g_now,0);//false=forRead?
	}

	// handle returned threads for niceness 0
	g_threads.timedCleanUp(-3,0); // 3 ms

	//
	// the stuff below is not super urgent, do not do if in quickpoll
	//
	if ( m_inQuickPoll ) return;

	// now for lower priority fds
	for ( int32_t i = 0 ; i < s_numReadFds ; i++ ) {
		if ( n == 0 ) break;
		int fd = s_readFds[i];
	 	s = m_readSlots  [ fd ];
	  	// if niceness is <= 0 we did it above
		if ( s && s->m_niceness <= 0 ) continue;
		// must be set
		if ( ! FD_ISSET ( fd , &readfds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling cback1 niceness=%" INT32 " "
			    "fd=%i", s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (true,fd, g_now,1);//read?
	}

	for ( int32_t i = 0 ; i < s_numWriteFds ; i++ ) {
		if ( n == 0 ) break;
	 	int fd = s_writeFds[i];
	  	s = m_writeSlots  [ fd ];
	  	// if niceness is <= 0 we did it above
	 	if ( s && s->m_niceness <= 0 ) continue;
	 	// must be set
	 	if ( ! FD_ISSET ( fd , &writefds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling wcback1 niceness=%" INT32 " "
			    "fd=%i", s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (false,fd, g_now,1);//forread?
	}

	// handle returned threads for all other nicenesses
	g_threads.timedCleanUp(-4,MAX_NICENESS); // 4 ms

	// set time
	g_now = gettimeofdayInMilliseconds();
	// call sleepers if they need it
	// call this every (about) 1 second
	int32_t elapsed = g_now - s_lastTime;
	// if someone changed the system clock on us, this could be negative
	// so fix it! otherwise, times may NEVER get called in our lifetime
	if ( elapsed < 0 ) elapsed = m_minTick;
	if ( elapsed >= m_minTick ) {
		// MAX_NUM_FDS is the fd for sleep callbacks
		callCallbacks_ass ( true , MAX_NUM_FDS , g_now );
		// note the last time we called them
		s_lastTime = g_now;
		// handle returned threads for all other nicenesses
		g_threads.timedCleanUp(-4,MAX_NICENESS); // 4 ms
	}
	// debug msg
	if ( g_conf.m_logDebugLoop ) log(LOG_DEBUG,"loop: Exited doPoll.");
}

// call this when you don't want to be interrupted
void Loop::interruptsOff ( ) {
#ifdef __APPLE__
	g_interruptsOn = false;
	return;
#else
	// . debug
	// . until we have our own malloc, don't turn them on
	if ( ! g_isHot ) return; 
	// bail in already in a sig handler
	if ( g_inSigHandler ) return;
	// if interrupts already off bail
	if ( ! g_interruptsOn ) return;
	// looks like sigprocmask is destructive on our sigset
	sigset_t rtmin;
	sigemptyset ( &rtmin );
	// block it
	if ( sigprocmask ( SIG_BLOCK  , &rtmin, 0 ) < 0 ) {
		log("loop: interruptsOff: sigprocmask: %s.", strerror(errno));
		return;
	}
	g_interruptsOn = false;
#endif
}
// and this to resume being interrupted
void Loop::interruptsOn ( ) {
#ifdef __APPLE__
	g_interruptsOn = true;
	return;
#else
	// . debug
	// . until we have our own malloc, don't turn them on
	if ( ! g_isHot ) return; 
	// bail in already in a sig handler
	if ( g_inSigHandler ) return;
	// if interrupts already on bail
	if ( g_interruptsOn ) return;
	// looks like sigprocmask is destructive on our sigset
	sigset_t rtmin;
	sigemptyset ( &rtmin );
	// let everyone know before we are vulnerable to an interrupt
	g_interruptsOn = true;
	// unblock it so interrupts flow
	if ( sigprocmask ( SIG_UNBLOCK, &rtmin, 0 ) < 0 ) {
		log("loop: interruptsOn: sigprocmask: %s.", strerror(errno));
		return;
	}
#endif
}

void Loop::startBlockedCpuTimer() {
	
	if(m_inQuickPoll) return;
	m_lastPollTime = gettimeofdayInMilliseconds();
	g_profiler.resetLastQpoll();
}


void Loop::quickPoll(int32_t niceness, const char* caller, int32_t lineno) {
	if ( ! g_conf.m_useQuickpoll ) return;

	// sometimes we init HashTableX's with a niceness of 0 even though
	// g_niceness is 1. so prevent a core below.
	if ( niceness == 0 ) return;

	// sanity check
	if ( g_niceness > niceness ) { 
		log(LOG_WARN,"loop: niceness mismatch!");
	}

	// if we are niceness 1 and not in a handler, make it niceness 2
	// so the handlers can be answered and we don't slow other
	// spiders down and we don't slow turks' injections down as much
	if ( ! g_inHandler && niceness == 1 ) niceness = 2;

	// reset this
	g_missedQuickPolls = 0;

	if(m_inQuickPoll) {
		log(LOG_WARN, 
		    "admin: tried to quickpoll from inside quickpoll");
		// this happens when handleRequest3f is called from
		// a quickpoll and it deletes a collection and BigFile::close
		// calls ThreadQueue::removeThreads and Msg3::doneScanning()
		// has niceness 2 and calls quickpoll again!
		return;
	}
	int64_t now = g_now;
	int64_t now2 = g_now;
	int32_t gerrno = g_errno;

	g_numQuickPolls++;

	m_inQuickPoll = true;

	// doPoll() will since we are in quickpoll and only call niceness 0
	// callbacks for all the fds. and it will set the timer to 0.
	doPoll ();

	// reset this again
	g_missedQuickPolls = 0;

	// . avoid quickpolls within a quickpoll
	// . was causing seg fault in diskHeartbeatWrapper()
	//   which call Threads::bailOnReads()
	m_canQuickPoll = false;

	// . call sleepcallbacks, like the heartbeat in Process.cpp
	// . MAX_NUM_FDS is the fd for sleep callbacks
	// . specify a niceness of 0 so only niceness 0 sleep callbacks
	//   will be called
	callCallbacks_ass ( true , MAX_NUM_FDS , now , 0 );
	// sanity check
	if ( g_niceness > niceness ) { 
		log("loop: niceness mismatch");
	}

	m_lastPollTime = now2;
	m_inQuickPoll = false;
	m_needsToQuickPoll = false;
	m_canQuickPoll = true;
	g_errno = gerrno;

	// reset this again
	g_missedQuickPolls = 0;
}


void Loop::canQuickPoll(int32_t niceness) {
	if(niceness && !m_shutdown) m_canQuickPoll = true;
	else         m_canQuickPoll = false;
}

void Loop::disableTimer() {
	m_canQuickPoll = false;
#ifdef __APPLE__
	deleteKqueueTimer(KQ_TIMER_QUICK);
	deleteKqueueTimer(KQ_TIMER_REAL);
#else
	setitimer(ITIMER_VIRTUAL, &m_noInterrupt, NULL);
	setitimer(ITIMER_REAL, &m_noInterrupt, NULL);
#endif
}

int gbsystem(char *cmd ) {
	g_loop.disableTimer();
	log("gb: running system(\"%s\")",cmd);
	int ret = system(cmd);
	g_loop.enableTimer();
	return ret;
}
	

void Loop::enableTimer() {
	m_canQuickPoll = true;
#ifdef __APPLE__
	if ( ! m_kqQuickTimerEnabled )
		addKqueueTimer(KQ_TIMER_QUICK, m_kqQuickInterval);
	if ( ! m_kqRealTimerEnabled )
		addKqueueTimer(KQ_TIMER_REAL , m_kqRealInterval );
#else
	setitimer(ITIMER_VIRTUAL, &m_quickInterrupt, NULL);
	setitimer(ITIMER_REAL, &m_realInterrupt, NULL);
#endif
}

void Loop::wakeup() {
#ifdef __APPLE__
	if ( m_kq < 0 ) return;
	struct kevent kev;
	EV_SET(&kev, KQ_EVENT_WAKE, EVFILT_USER, NOTE_TRIGGER, 0, 0, NULL);
	kevent(m_kq, &kev, 1, NULL, 0, NULL);
#else
	// no-op on other platforms; existing signal handlers wake the loop
#endif
}


FILE* gbpopen(char* cmd) {
    // Block everything from interrupting this system call because
    // if there is an alarm or a child thread crashes (pdftohtml)
    // then this will hang forever.
    // We should actually write our own popen so that we do
    // fork, close all fds in the child, then exec.  
    // These child processes can hold open the http server and
    // prevent a new gb from running even after it has died.
	g_loop.disableTimer();

	sigset_t oldSigs;
	sigset_t sigs;
	sigfillset ( &sigs );	

	if ( sigprocmask ( SIG_BLOCK  , &sigs, &oldSigs ) < 0 ) {
        log("build: had error blocking signals for popen");
    }
	FILE* fh = popen(cmd, "r");            
    
	if ( sigprocmask ( SIG_SETMASK  , &oldSigs, NULL ) < 0 ) {
        log("build: had error unblocking signals for popen");
    }

	g_loop.enableTimer();
    return fh;
}
