#include "gb-include.h"

#include "Loop.h"
#include "Threads.h"    // g_threads.launchThreads()
#include "UdpServer.h"  // g_udpServer2.makeCallbacks()
#include "HttpServer.h" // g_httpServer.m_tcp.m_numQueued
#include "Profiler.h"
#include "Process.h"
#include "PageParser.h"
#include "Threads.h"

#include "Stats.h"
// raised from 5000 to 10000 because we have more UdpSlots now and Multicast
// will call g_loop.registerSleepCallback() if it fails to get a UdpSlot to
// send on.
#define MAX_SLOTS 10000

// apple mac os x does not have real-time signal support
// #ifdef __APPLE__
// #define _POLLONLY_
// #endif

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
bool g_isHot = true;

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
//int64_t g_nowGlobal = 0;
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

// this is defined in main.cpp
//extern bool mainShutdown ( bool urgent );

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
	/*
	for ( int32_t i = 0 ; i < MAX_NUM_FDS+2 ; i++ ) {
		Slot *s = m_readSlots  [ i ];
		while ( s ) {
			Slot *next = s->m_next;
			mfree ( s , sizeof(Slot) ,"Loop" );
			s = next;
		}
		m_readSlots [ i ] = NULL;
		s = m_writeSlots [ i ];
		while ( s ) {
			Slot *next = s->m_next;
			mfree ( s , sizeof(Slot) , "Loop" );
			s = next;
		}
		m_writeSlots [ i ] = NULL;
	}
	*/
}

//static void sigHandler_r  ( int x , siginfo_t *info , void *v ) ;
//static void sigHandlerRT  ( int x , siginfo_t *info , void *v ) ;
static void sigbadHandler ( int x , siginfo_t *info , void *y ) ;
static void sigpwrHandler ( int x , siginfo_t *info , void *y ) ;
static void sighupHandler ( int x , siginfo_t *info , void *y ) ;
//static void sigioHandler  ( int x , siginfo_t *info , void *y ) ;
static void sigalrmHandler( int x , siginfo_t *info , void *y ) ;
static void sigvtalrmHandler( int x , siginfo_t *info , void *y ) ;

//int32_t g_fdWriteBits[MAX_NUM_FDS/32];
//int32_t g_fdReadBits [MAX_NUM_FDS/32];

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
		//mfree ( s , sizeof(Slot) , "Loop" );
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
					    "callback for fd=%"INT32" from "
					    "write #wrts=%"INT32"",
					    (int32_t)fd,
					    (int32_t)s_numWriteFds);
			// 	FD_CLR(fd,&s_selectMaskExcept);
			 	break;
			}
		}
		// debug msg
		//log("Loop::unregistered fd=%"INT32" state=%"UINT32"", fd, (int32_t)state );
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
	// the "true" answers the question "for reading?"
	if ( addSlot ( true, fd, state, callback, niceness ) ) return true;
	return log("loop: Unable to register read callback.");
}


bool Loop::registerWriteCallback ( int fd,
				   void *state, 
				   void (* callback)(int fd, void *state ) ,
				   int32_t  niceness ) {
	// the "false" answers the question "for reading?"
	if ( addSlot ( false, fd, state, callback, niceness ) )return true;
	return log("loop: Unable to register write callback.");
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
		log("loop: bad fd of %"INT32"",(int32_t)fd);
		char *xx=NULL;*xx=0; 
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
			// don't set g_errno for this anymore, just bitch
			//g_errno = EBADENGINEER;
			log(LOG_LOGIC,"loop: fd=%i is already registered.",fd);
			return true;
		}
		s = s->m_next;
	}
	// . make a new slot
	// . TODO: implement mprimealloc() to pre-alloc slots for us for speed
	//s = (Slot *) mmalloc ( sizeof(Slot ) ,"Loop");
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
			if ( s_numReadFds>MAX_NUM_FDS){char *xx=NULL;*xx=0;}
		}
		// fd == MAX_NUM_FDS if it's a sleep callback
		//if ( fd < MAX_NUM_FDS ) {
		//FD_SET ( fd , &m_readfds   );
		//FD_SET ( fd , &m_exceptfds );
		//}
	}
	else {
	 	next = m_writeSlots [ fd ];
	 	m_writeSlots [ fd ] = s;
	 	//FD_SET ( fd , &m_writefds );
	 	// if not already registered, add to list
	 	if ( fd<MAX_NUM_FDS && ! FD_ISSET ( fd,&s_selectMaskWrite ) ) {
	 		s_writeFds[s_numWriteFds++] = fd;
	 		FD_SET ( fd,&s_selectMaskWrite  );
	 		// sanity
	 		if ( s_numWriteFds>MAX_NUM_FDS){char *xx=NULL;*xx=0;}
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
	// debug msg
	//log("Loop::registered fd=%i state=%"UINT32"",fd,state);
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
	if ( fcntl ( fd, F_SETFL, flags|O_NONBLOCK|O_ASYNC) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry9;
		g_errno = errno;
		return log("loop: fcntl(NONBLOCK): %s.",strerror(errno));
	}

	// we use select()/poll now so skip stuff below
	return true;

	/*

 retry8:
	// tell kernel to send the signal to us when fd is ready for read/write
	if ( fcntl (fd, F_SETOWN , getpid() ) < 0 ) {
		g_errno = errno;
		// valgrind
		if ( errno == EINTR ) goto retry8;
		return log("loop: fcntl(F_SETOWN): %s.",strerror(errno));
	}

	// . tell kernel what signal we'd like to recieve when this happens
	// . additional signal info (including fd) should be available
#ifdef _POLLONLY_
	return true;
#endif
	// trunc nicess cuz we only get GB_SIGRTMIN+1 to GB_SIGRTMIN+2 signals
	if ( niceness < -1             ) niceness = -1;
	if ( niceness >  MAX_NICENESS  ) niceness = MAX_NICENESS;
	// debug msg
	//log("fd on niceness = %"INT32" sig = %"INT32"",niceness,GB_SIGRTMIN +1+niceness);
 retry6:
	// . tell kernel to send this signal when fd is ready for read/write
	// . reserve GB_SIGRTMIN for unmaskable interrupts (niceness = -1)
	//   as used by high priority udp server, g_udpServer2
	if ( fcntl (fd, F_SETSIG , GB_SIGRTMIN + 1 + niceness ) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry6;
		g_errno = errno;
		return log("loop: fcntl(F_SETSIG): %s.",strerror(errno));
	}
	return true;
	*/
}

// . if "forReading" is true  call callbacks registered for reading on "fd" 
// . if "forReading" is false call callbacks registered for writing on "fd" 
// . if fd is MAX_NUM_FDS and "forReading" is true call all sleepy callbacks
void Loop::callCallbacks_ass ( bool forReading , int fd , int64_t now ,
			       int32_t niceness ) {

	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("callCallbacks_ass start");
	//if ( fd != 1024 ) {
	//if (forReading) fprintf(stderr,"got read  sig on fd=%"INT32"\n",(int32_t)fd);
	//else            fprintf(stderr,"got write sig on fd=%"INT32"\n",(int32_t)fd);
	//}
	// save the g_errno to send to all callbacks
	int saved_errno = g_errno;
	// get the first Slot in the chain that is waiting on this fd
	Slot *s ;
	if ( forReading ) s = m_readSlots  [ fd ];
	else              s = m_writeSlots [ fd ];
	//s = m_readSlots [ fd ];
	// ensure we called something
	int32_t numCalled = 0;

	// a hack fix
	if ( niceness == -1 && m_inQuickPoll ) niceness = 0;

	// . now call all the callbacks
	// . most will re-register themselves (i.e. call registerCallback...()
	//int64_t startTime = gettimeofdayInMilliseconds();
	while ( s ) {
		// skip this slot if he has no callback
		if ( ! s->m_callback ) continue;
		// NOTE: callback can unregister fd for Slot s, so get next
		//Slot *next = s->m_next;
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
		// . debug msg
		// . this is called a lot cuz we process all dgrams/whatever
		//   in one clump so there's a lot of redundant signals
		//if ( g_conf.m_logDebugUdp && fd != 1024 ) 
		//	log("Loop::callCallbacks_ass: for fd=%"INT32" state=%"UINT32"",
		//	    fd,(int32_t)s->m_state);
		// do the callback
		//int32_t address = 0;
		// 		uint64_t profilerStart,profilerEnd;
		// 		uint64_t statStart, statEnd;
		/*
		if(g_conf.m_profilingEnabled){
			address=(int32_t)s->m_callback;
			g_profiler.startTimer(address, 
					      __PRETTY_FUNCTION__);
		      //profilerStart=gettimeofdayInMillisecondsLocal();
		      //statStart = gettimeofdayInMilliseconds();
		}
		*/
		//startBlockedCpuTimer();

		// log it now
		if (  g_conf.m_logDebugLoop )
			log(LOG_DEBUG,"loop: enter fd callback fd=%"INT32" "
			    "nice=%"INT32"",(int32_t)fd,(int32_t)s->m_niceness);

		// sanity check. -1 no longer supported
		if ( s->m_niceness < 0 ) { char *xx=NULL;*xx=0; }

		// save it
		int32_t saved = g_niceness;
		// set the niceness
		g_niceness = s->m_niceness;
		// make sure not 2
		if ( g_niceness >= 2 ) g_niceness = 1;

		// sanity check -- need to be able to quickpoll!
		//if ( s->m_niceness > 0 && ! g_loop.m_canQuickPoll ) {
		//	char *xx=NULL;*xx=0; }

		s->m_callback ( fd , s->m_state );

		// restore it
		g_niceness = saved;

		// log it now
		if ( g_conf.m_logDebugLoop )
			log(LOG_DEBUG,"loop: exit fd callback fd=%"INT32" "
			    "nice=%"INT32"", (int32_t)fd,(int32_t)s->m_niceness);

		/*
		if(g_conf.m_profilingEnabled){
			//profilerEnd =gettimeofdayInMillisecondsLocal();
			if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
				log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
				    (int32_t)address);
		}
		*/
		// . debug msg
		// . this is called a lot cuz we process all dgrams/whatever
		//   in one clump so there's a lot of redundant signals
		//if ( g_conf.m_logDebugUdp && fd != 1024 ) 
		//	log("Loop::callCallbacks_ass: back");
		// inc the flag
		numCalled++;
		// reset g_errno so all callbacks for this fd get same g_errno
		g_errno = saved_errno;
		// get the next n (will be -1 if no slot after it)
		s = s_callbacksNext;
	}
	s_callbacksNext = NULL;
// 	int64_t now2 = gettimeofdayInMilliseconds();
// 	int64_t took = now2 - startTime;

// 	if(g_conf.m_profilingEnabled && took > 10) {	
// 		g_stats.addStat_r ( 0      , 
// 				    startTime, 
// 				    now2,
// 				    0 ,
// 				    STAT_GENERIC,
// 				    __PRETTY_FUNCTION__,__LINE__);


// 		if(g_conf.m_sequentialProfiling) {
// 			log(LOG_TIMING, 
// 			    "admin: loop time to do %"INT32" callbacks: %"INT64" ms", 
// 			    numCalled,took);
// 		}
// 	}

	// log an error if nothing called
	//if ( ! called ) log("Loop::callCallbacks: no callback for signal");
	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("callCallbacks_ass end");
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
	// the extra sleep slots
	//m_readSlots [ MAX_NUM_FDS ] = NULL;
	m_slots = NULL;
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
		//log("admin: got sigqueue");
		// the thread is done
		g_threads.m_needsCleanup = true;
		return;
	}



	// wtf is this?
	g_numSigOthers++;

	// the stuff below should no longer be used since we
	// do not use F_SETSIG now
	return;

	/*
	// extract the file descriptor that needs attention
	int fd   = info->si_fd;

	if ( fd >= MAX_NUM_FDS ) {
		log("loop: CRITICAL ERROR. fd=%i > %i",fd,(int)MAX_NUM_FDS);
		return;
	}

	// set the right callback
	
	// info->si_band values:
	//#define POLLIN      0x0001    // There is data to read 
        //#define POLLPRI     0x0002    // There is urgent data to read 
	//#define POLLOUT     0x0004    // Writing now will not block 
	//#define POLLERR     0x0008    // Error condition 
	//#define POLLHUP     0x0010    // Hung up 
	//#define POLLNVAL    0x0020    // Invalid request: fd not open
	int band = info->si_band;  
	// translate SIGPIPE's to band of POLLHUP
	if ( info->si_signo == SIGPIPE ) {
		band = POLLHUP;
		//log("loop: Received SIGPIPE signal. Broken pipe.");
	}

	// . call the appropriate handler(s)
	// . TODO: bitch if no callback to handle the read!!!!!!!
	// . NOTE: when it's connected it sets both POLLIN and POLLOUT
	// . NOTE: or when a socket is trying to connect to it if it's listener
	//if      ( band & (POLLIN | POLLOUT) == (POLLIN | POLLOUT) ) 
	// g_loop.callCallbacks_ass ( true , fd ); // for reading
	if ( band & POLLIN  ) {
		// keep stats on this now since some linuxes dont work right
		g_stats.m_readSignals++;
		//log("Loop: read %"INT64" fd=%i",gettimeofdayInMilliseconds(),fd);
		//g_loop.callCallbacks_ass ( true  , fd ); 
		g_fdReadBits[fd/32] = 1<<(fd%32);
	}
	else if ( band & POLLPRI ) {
		// keep stats on this now since some linuxes dont work right
		g_stats.m_readSignals++;
		//log("Loop: read %"INT64" fd=%i",gettimeofdayInMilliseconds(),fd);
		//g_loop.callCallbacks_ass ( true  , fd ) ;
		g_fdReadBits[fd/32] = 1<<(fd%32);
	}
	else if ( band & POLLOUT ) {
		// keep stats on this now since some linuxes dont work right
		g_stats.m_writeSignals++;
		//log("Loop: write %"INT64" fd=%i",gettimeofdayInMilliseconds(),fd)
		//g_loop.callCallbacks_ass ( false , fd ); 
		g_fdWriteBits[fd/32] = 1<<(fd%32);
	}
	// fix qainject1() test with this
	else if ( band & POLLERR )  {
		//log(LOG_INFO,"loop: got POLLERR on fd=%i.",fd);
	}
	//g_loop.callCallbacks_ass ( false , fd ); 
	// this happens if the socket closes abruptly
	// or out of band data, etc... see "man 2 poll" for more info
	else if ( band & POLLHUP ) { 
		// i see these all the time for fd == 0, so don't print it
		//if ( fd != 0 ) 
		//	log(LOG_INFO,"loop: Received hangup on fd=%i.",fd);
		// it is ready for writing i guess
		g_fdWriteBits[fd/32] = 1<<(fd%32);
	}
	*/
}



bool Loop::init ( ) {

	// clear this up here before using in doPoll()
	FD_ZERO(&s_selectMaskRead);
	FD_ZERO(&s_selectMaskWrite);
	FD_ZERO(&s_selectMaskExcept);

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
	log(LOG_DEBUG,"loop: Allocated %"INT32" bytes for %"INT32" callbacks.",
	     MAX_SLOTS * (int32_t)sizeof(Slot),(int32_t)MAX_SLOTS);
	// init link list ptr
	for ( int32_t i = 0 ; i < MAX_SLOTS - 1 ; i++ ) {
		m_slots[i].m_nextAvail = &m_slots[i+1];
	}
	m_slots[MAX_SLOTS - 1].m_nextAvail = NULL;
	m_head = &m_slots[0];
	m_tail = &m_slots[MAX_SLOTS - 1];
	// an innocent log msg
	//log ( 0 , "Loop: starting the i/o loop");
	// . when using threads GB_SIGRTMIN becomes 35, not 32 anymore
	//   since threads use these signals to reactivate suspended threads
	// . debug msg
	//log("admin: GB_SIGRTMIN=%"INT32"", (int32_t)GB_SIGRTMIN );
	// . block the GB_SIGRTMIN signal
	// . anytime this is raised it goes onto the signal queue
	// . we use sigtimedwait() to get signals off the queue
	// . sigtimedwait() selects the lowest signo first for handling
	// . therefore, GB_SIGRTMIN is higher priority than (GB_SIGRTMIN + 1)
	//sigfillset ( &sigs );
	// set of signals to block
	sigset_t sigs;
	sigemptyset ( &sigs                );	
	sigaddset   ( &sigs , SIGPIPE      ); //if we write to a close socket
// #ifndef _VALGRIND_
// 	sigaddset   ( &sigs , GB_SIGRTMIN     );
// #endif
// 	sigaddset   ( &sigs , GB_SIGRTMIN + 1 );
// 	sigaddset   ( &sigs , GB_SIGRTMIN + 2 );
// 	sigaddset   ( &sigs , GB_SIGRTMIN + 3 );
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
	// if ( sigprocmask ( SIG_BLOCK, &sigs, 0 ) < 0 ) {
	// 	g_errno = errno;
	// 	return log("loop: sigprocmask: %s.",strerror(g_errno));
	// }
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
	// if ( sigaction ( GB_SIGRTMIN    , &sa2, 0 ) < 0 ) g_errno = errno;
	// if ( sigaction ( GB_SIGRTMIN + 1, &sa2, 0 ) < 0 ) g_errno = errno;
	// if ( sigaction ( GB_SIGRTMIN + 2, &sa2, 0 ) < 0 ) g_errno = errno;
	// if ( sigaction ( GB_SIGRTMIN + 3, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( sigaction ( SIGCHLD, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( sigaction ( SIGIO, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction(): %s.", mstrerror(g_errno) );
	

	// . we turn this signal on/off to turn interrupts off/on
	// . clear all signals from the set	
	//sigemptyset ( &m_sigrtmin );
	// tmp debug hack, so we don't have real time signals now...
	//sigaddset   ( &m_sigrtmin, GB_SIGRTMIN );

	/*
	// now set up a signal handler to handle just/only SIGIO
	struct sigaction sa;
	// . sa_mask is the set of signals that should be blocked when
	//   we're handling the GB_SIGRTMIN, make this empty
	// . GB_SIGRTMIN signals will be automatically blocked while we're
	//   handling a SIGIO signal, so don't worry about that
	sigemptyset (&sa.sa_mask);
	// . these flags determine the signal handling process
	// . SA_SIGINFO means to use sa.sa_sigaction() as the sig handler
	//   and not sa_.sa_handler()  (added in Linux 2.1.86)
	// . this allows us to get the siginfo_t structure to get the fd
	//   that generated the signal
	// . SA_ONESHOT restores the handler to the default state when
	//   our sig handler is called so we don't get interuppted by another
	//   signal when we're handling that one
	// . incoming GB_SIGRTMIN sigs should be queued (sigtimedwait())
	sa.sa_flags = SA_SIGINFO ; //| SA_ONESHOT;
	// the handler for unblocked signals, same as other signals really
	sa.sa_sigaction = sigHandlerRT;
	// clear g_errno
	g_errno = 0;
	// now when we got an unblocked GB_SIGRTMIN signal go here right away
// #ifndef _VALGRIND_
// 	if ( sigaction ( GB_SIGRTMIN, &sa, 0 ) < 0 ) g_errno = errno;
// 	if ( g_errno)log("loop: sigaction GB_SIGRTMIN: %s.", mstrerror(errno));
// #endif

	// set it this way for SIGIO's
	sa.sa_flags = SA_SIGINFO ; // | SA_ONESHOT;
	// . define the actual routine that handles the SIGIO signal
	// . void (*sa_sigaction)(int, siginfo_t *, void *);
	sa.sa_sigaction = sigioHandler;
	// . register our sigHandler() to handle the GB_SIGRTMIN signal
	// . when a file/socket is made non-blocking it should have done a:
	//   fcntl(fd,SET_SIG,GB_SIGRTMIN) so we're notified with that signal
	// . this returns -1 and sets g_errno on error
	// . TODO: is this the SOURCE signal or the altered signal?

#ifndef PTHREADS
	// i think this was supposed to be sent when the signal queue was
	// overflowing so we needed to do a poll operation when we got this
	// signal, however, newer kernel seems to throw this signal all the
	// time when it just gets IO causing cpu to be 100% floored!
	// i'm afraid without this code we might miss data on a socket
	// or not read it promptly, but let's see how it goes.
	if ( sigaction ( SIGIO, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGIO: %s.", mstrerror(errno));
#endif
	*/

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
	// if ( sigaction ( SIGABRT, &sa, 0 ) < 0 ) g_errno = errno;
	// if ( g_errno ) log("loop: sigaction SIGTERM: %s.",mstrerror(errno));

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
	// if ( sigaction ( SIGQUIT , &sa, 0 ) < 0 ) g_errno = errno;
	// if ( g_errno ) log("loop: sigaction SIGBUS: %s.", mstrerror(errno));
	// if ( sigaction ( SIGSYS , &sa, 0 ) < 0 ) g_errno = errno;
	// if ( g_errno ) log("loop: sigaction SIGBUS: %s.", mstrerror(errno));


	// if the UPS is about to go off it sends a SIGPWR
	sa.sa_sigaction = sigpwrHandler;
	if ( sigaction ( SIGPWR, &sa, 0 ) < 0 ) g_errno = errno;


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
	//m_realInterrupt.it_value.tv_sec = 0;
	//m_realInterrupt.it_value.tv_usec = QUICKPOLL_INTERVAL * 1000;

	// set the interrupts to off for now
	//mdw:disableTimer();

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
void printStackTrace ( int signum , siginfo_t *info , void *ptr ) {

	// int arch = 32;
	// if ( __WORDSIZE == 64 ) arch = 64;
	// if ( __WORDSIZE == 128 ) arch = 128;
	// right now only works for 32 bit
	//if ( arch != 32 ) return;

	logf(LOG_DEBUG,"gb: Printing stack trace. use "
	     "'addr2line -e gb' to decode the hex below.");

	if ( g_inMemFunction ) {
		logf(LOG_DEBUG,"gb: in mem function not doing backtrace");
		return;
	}

	static void *s_bt[200];
	int sz = backtrace(s_bt, 200);
	//char **strings = backtrace_symbols(s_bt, sz);
	for( int i = 0; i < sz; ++i) {
		//unsigned long long ba;
		//ba = g_profiler.getFuncBaseAddr((PTRTYPE)s_bt[i]);
		//sigsegv_outp("%s", strings[i]);
		//logf(LOG_DEBUG,"[0x%llx->0x%llx] %s"
		logf(LOG_DEBUG,"addr2line -e gb 0x%"XINT64""
		     ,(uint64_t)s_bt[i]
		     //,ba
		     //,g_profiler.getFnName(ba,0));
		     );
#ifdef INLINEDECODE
		char cmd[256];
		sprintf(cmd,"addr2line -e gb 0x%"XINT64" > ./tmpout"
			,(uint64_t)s_bt[i]);
		gbsystem ( cmd );
		char obuf[1024];
		SafeBuf fb (obuf,1024);
		fb.load("./tmpout");
		log("stack: %s",fb.getBufStart());
#endif
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
	log("loop: sigbadhandler. trying to save now. mode=%"INT32"",
	    (int32_t)g_process.m_mode);
	// . this will save all Rdb's 
	// . if "urgent" is true it will dump core
	// . if "urgent" is true it won't broadcast its shutdown to all hosts
	//#ifndef NO_MAIN
	//	mainShutdown ( true ); // urgent?
	//#endif
	g_process.shutdown ( true );
}

void sigvtalrmHandler ( int x , siginfo_t *info , void *y ) {

#ifdef PTHREADS
	// do not allow threads
	// this call is very fast, can be called like 400M times per second
	if ( g_threads.amThread() ) return;
#endif

	// stats
	g_numVTAlarms++;

	// see if a niceness 0 algo is hogging the cpu
	if ( g_callSlot && g_niceness == 0 ) {
		// are we handling the same request or callback?
		if ( g_callSlot->m_transId == g_lastTransId ) g_transIdCount++;
		else                                          g_transIdCount=1;
		// set it
		g_lastTransId = g_callSlot->m_transId;
		// sanity check
		//if ( g_transIdCount >= 10 ) { char *xx=NULL;*xx=0; }
		bool logIt = false;
		if ( g_transIdCount >= 4 ) logIt = true;
		// do not spam for msg99 handler so much
		if ( g_callSlot->m_msgType == 0x99 && g_transIdCount != 50 )
			logIt = false;
		// it's not safe to call fprintf() even with 
		// mutex locks for sig handlers with pthreads
		// going on!!!
#ifdef PTHREADS
		logIt = false;
#endif
		// panic if hogging
		if ( logIt ) {
			if ( g_callSlot->m_callback )
				log("loop: msg type 0x%hhx reply callback "
				    "hogging cpu for %"INT32" ticks", 
				    g_callSlot->m_msgType,
				    g_transIdCount);
			else
				log("loop: msg type 0x%hhx handler "
				    "hogging cpu for %"INT32" ticks", 
				    g_callSlot->m_msgType,
				    g_transIdCount);
		}
	}

	g_nowApprox += QUICKPOLL_INTERVAL; // 10 ms

	// sanity check
	if ( g_loop.m_inQuickPoll && 
	     g_niceness != 0 &&
	     // seems to happen a lot when doing a qa test because we slow
	     // things down a lot when that happens
	     ! g_conf.m_testParserEnabled &&
	     ! g_conf.m_testSpiderEnabled &&
	     ! g_conf.m_testSearchEnabled &&
	     // likewise if doing a page parser test...
	     ! g_inPageParser &&
	     ! g_inPageInject     ) {
#ifndef PTHREADS
		// i guess sometimes niceness 1 things call niceness 0 things?
		log("loop: crap crap crap!!!");
#endif
		//char *xx=NULL;*xx=0; }
	}
	// basically ignore this alarm if already in a quickpoll
	if ( g_loop.m_inQuickPoll ) return;

	if ( ! g_conf.m_useQuickpoll ) return;

	g_loop.m_needsToQuickPoll = true;

	//fprintf(stderr,"missed=%"INT32"\n",g_missedQuickPolls);

	// another missed quickpoll
	if ( g_niceness == 1 ) g_missedQuickPolls++;
	// reset if niceness is 0
	else if ( g_niceness == 0 ) g_missedQuickPolls = 0;

	// if we missed to many, then dump core
	if ( g_niceness == 1 && g_missedQuickPolls >= 4 ) {
		//g_inSigHandler = true;
		// NOT SAFE for pthreads cuz we're in sig handler
#ifndef PTHREADS
		log("loop: missed quickpoll. Dumping stack.");
		printStackTrace( x , info , y );
#endif
		//g_inSigHandler = false;
		// seems to core a lot in gbcompress() we need to
		// put a quickpoll into zlib deflate() or
		// deflat_slot() or logest_match() function
		// for now do not dump core --- re-enable this later
		// mdw TODO
		//char *xx=NULL;*xx=0; 
	}

	// if it has been a while since heartbeat (> 10000ms) dump core so
	// we can see where the process was... we are in a long niceness 0
	// function or a niceness 1 function without a quickpoll, so that
	// heartbeatWrapper() function never gets called.
	if ( g_process.m_lastHeartbeatApprox == 0 ) return;
	if ( g_conf.m_maxHeartbeatDelay <= 0 ) return;
	if ( g_nowApprox - g_process.m_lastHeartbeatApprox > 
	     g_conf.m_maxHeartbeatDelay ) {
#ifndef PTHREADS
		logf(LOG_DEBUG,"gb: CPU seems blocked. Dumping stack.");
		printStackTrace( x , info , y );
#endif
		//char *xx=NULL; *xx=0; 

	}

	//logf(LOG_DEBUG, "xxx now: %"INT64"! approx: %"INT64"", g_now, g_nowApprox);

}

float g_cpuUsage = 0.0;

void sigalrmHandler ( int x , siginfo_t *info , void *y ) {

#ifdef PTHREADS
	// do not allow threads
	// this call is very fast, can be called like 400M times per second
	if ( g_threads.amThread() ) return;
#endif

	// so we don't call gettimeofday() thousands of times a second...
	g_clockNeedsUpdate = true;

	// stats
	g_numAlarms++;

	if ( ! g_inWaitState )
		g_cpuUsage = .99 * g_cpuUsage + .01 * 100;
	else
		g_cpuUsage = .99 * g_cpuUsage + .01 * 000;

	if ( g_profiler.m_realTimeProfilerRunning )
		g_profiler.getStackFrame(0);

	return;
	/*
	// . see where we are in the code
	// . for computing cpu usage
	// . if idling we will be in sigtimedwait() at the lowest level
	Host *h = g_hostdb.m_myHost;
	// if doing injects...
	if ( ! h ) return;
	// . i guess this means we were doing something... (otherwise idle)
	// . this is KINDA like a 100 point sample, but it has crazy decay
	//   logic built into it
	if ( ! g_inWaitState )
		h->m_pingInfo.m_cpuUsage = 
			.99 * h->m_pingInfo.m_cpuUsage + .01 * 100;
	else
		h->m_pingInfo.m_cpuUsage = 
			.99 * h->m_pingInfo.m_cpuUsage + .01 * 000;

	if ( g_profiler.m_realTimeProfilerRunning )
		g_profiler.getStackFrame(0);
	*/
}

/*
static sigset_t s_rtmin;

void maskSignals() {

	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		sigemptyset ( &s_rtmin );
		// sigaddset ( &s_rtmin, GB_SIGRTMIN     );
		// sigaddset ( &s_rtmin, GB_SIGRTMIN + 1 );
		// sigaddset ( &s_rtmin, GB_SIGRTMIN + 2 );
		// sigaddset ( &s_rtmin, GB_SIGRTMIN + 3 );
		sigaddset ( &s_rtmin, SIGCHLD );
		sigaddset ( &s_rtmin, SIGIO   );
		sigaddset ( &s_rtmin, SIGPIPE ); 
	}

	// block it
	if ( sigprocmask ( SIG_BLOCK  , &s_rtmin, 0 ) < 0 ) {
		log("loop: maskSignals: sigprocmask: %s.", strerror(errno));
		return;
	}
}

void unmaskSignals() {
	// unblock it
	if ( sigprocmask ( SIG_UNBLOCK  , &s_rtmin, 0 ) < 0 ) {
		log("loop: unmaskSignals: sigprocmask: %s.", strerror(errno));
		return;
	}
}
*/

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
	//sigset_t sigs1;
	// clear all signals from the set
	sigemptyset ( &sigs0 );
	//sigemptyset ( &sigs1 );
	// . set sigs on which sigtimedwait() listens for
	// . add this signal to our set of signals to watch (currently NONE)
	sigaddset ( &sigs0, SIGPIPE      ); 
// #ifndef _VALGRIND_
// 	sigaddset ( &sigs0, GB_SIGRTMIN     );
// #endif
// 	sigaddset ( &sigs0, GB_SIGRTMIN + 1 );
// 	sigaddset ( &sigs0, GB_SIGRTMIN + 2 );
// 	sigaddset ( &sigs0, GB_SIGRTMIN + 3 );
	sigaddset ( &sigs0, SIGCHLD      );
	//sigaddset ( &sigs0, SIGVTALRM    );
	// . TODO: do we need to mask SIGIO too? (sig queue overflow?)
	// . i would think so, because what if we tried to queue an important
	//   handler to be called in the high priority UdpServer but the queue
	//   was full? Then we would finish processing the signals on the queue
	//   before we would address the excluded high priority signals by 
	//   calling doPoll()
	sigaddset ( &sigs0, SIGIO );
	// . set up a time to block waiting for signals to be 1/2 a second
	// . 1 billion nanoseconds = 1 second
	//struct timespec t = { 0 /*seconds*/, 500000000 /*nanoseconds*/};
	//struct timespec t = { 0 /*seconds*/, 10000000 /*nanoseconds*/};

	// grab any high priority sig first
	//siginfo_t info ;
	//int32_t sigNum ; //= sigwaitinfo ( &sigs1, &info );

#endif
	s_lastTime = 0;

	// . allow us to be interrupted
	// . UNBLOCKs GB_SIGRTMIN
	// . makes g_udpServer2 quite jumpy
	g_loop.interruptsOn();

	m_isDoingLoop = true;

	//mdw:enableTimer();

	// . now loop forever waiting for signals
	// . but every second check for timer-based events

 BIGLOOP:

	g_now = gettimeofdayInMilliseconds();
		
	//set the time back to its exact value and reset
	//the timer.
	g_nowApprox = g_now;
	// MDW: won't this hog cpu? just don't disable it in
	//      Process::save2() any more and it should be ok
	//enableTimer();
	m_lastPollTime = g_now;
	m_needsToQuickPoll = false;


	/*
	// test the heartbeat core...
	if ( g_numAlarms > 100 ) {
	goo:
	int32_t j;
	for ( int32_t k = 0 ; k < 2000000000 ; k++ ) {
	j=k *5;
	}
	goto goo;
	}
	*/

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
		// if thread got the signal, just wait for him to save all
		// Rdbs and then dump core
		if ( m_shutdown == 2 ) {
			//log(0,"Thread is saving & shutting down urgently.");
			//while ( 1 == 1 ) sleep (50000);
			//log("loop: Resuming despite thread crash.");
			//m_shutdown = 0;
			//goto BIGLOOP;
		}
		// otherwise, thread did not save, so we must do it
		log ( LOG_INIT ,"loop: Saving and shutting down urgently.");
		// . this will save all Rdb's and dump core
		// . since "urgent" is true it won't broadcast its shutdown
		//   to all hosts
		//#ifndef NO_MAIN
		//mainShutdown( true ); // urgent?
		//#endif
		g_process.shutdown ( true );
	}


	//
	//
	// THE HEART OF GB. process events/signals on FDs.
	//
	//
	doPoll();

 	goto BIGLOOP;

 	// make compiler happy
 	return 0;



	//g_udpServer2.sendPoll_ass(true,g_now);
	//g_udpServer2.process_ass ( g_now );
	// MDW: see if this works without this junk, if not then
	// put it back in
	// seems like never getting signals on redhat 16-core box.
	// we always process dgrams through this... wtf? try taking
	// it out and seeing what's happening
	//g_udpServer.sendPoll_ass (true,g_now);
	//g_udpServer.process_ass ( g_now );
	// and dns now too
	//g_dns.m_udpServer.sendPoll_ass(true,g_now);
	//g_dns.m_udpServer.process_ass ( g_now );

	// if there was a high niceness  http request within a 
	// quickpoll, we stored it and now we'll call it here.
	//g_httpServer.callQueuedPages();

	//g_udpServer.printState ( );

	// if ( g_someAreQueued ) {
	// 	// assume none are queued now, we may get interrupted
	// 	// and it may get set back to true
	// 	g_someAreQueued = false;
	// 	//g_udpServer2.makeCallbacks_ass (  0 );
	// 	//g_udpServer2.makeCallbacks_ass (  1 );
	// }

	//	if ( g_threads.m_needsCleanup ) {
	//		// bitch about
	//		static bool s_bitched = false;
	//		if ( ! s_bitched ) {
	//			log(LOG_REMIND,"loop: Lost thread signal.");
	//			s_bitched = true;
	//		}
	

	// 	}
	//cleanup and launch threads:
	//g_threads.printState();
	//g_threads.timedCleanUp(4, MAX_NICENESS ) ; // 4 ms

	// do it anyway
	// take this out as well to see if signals are coming in
	//doPoll();

	//while ( m_needToPoll ) doPoll();

	//#ifndef _POLLONLY_

	// hack
	//char buffer[100];
	//if ( recv(27,buffer,99,MSG_PEEK|MSG_DONTWAIT) == 0 ) {
	//	logf(LOG_DEBUG,"CLOSED CLOSED!!");
	//}
	//g_errno = 0;

	//check for pending signals, return right away if none.
	//then we'll do the low priority stuff while we were 
	//supposed to be sleeping.
	//g_inWaitState = true;

	//sigNum = sigtimedwait (&sigs0, &info, s_sigWaitTimePtr ) ;

	//#undef usleep

	// now we just usleep(). an arriving signal will call
	// sigHandlerQueue_r() then break us out of this sleep.
	// 10000 microseconds is 10 milliseconds. it should break out 
	// when a signal comes in just like the sleep() function.
	//usleep(1000 * 10);

	// reinstate the thing that prevents us from non-chalantly adding
	// usleeps() which could degrade performance
	//#define usleep(a) { char *xx=NULL;*xx=0; }

	// if no signal, we just waited 20 ms and nothing happened
	// why do we need this now? MDW
	//if ( sigNum == -1 )
	//	sigalrmHandler( 0,&info,NULL);

	//logf(LOG_DEBUG,"loop: sigNum=%"INT32" signo=%"INT32" alrm=%"INT32"",
	//     (int32_t)sigNum,info.si_signo,(int32_t)SIGVTALRM);
	// no longer in a wait state...
	//g_inWaitState = false;


	// int32_t n = MAX_NUM_FDS / 32;

	// // process file descriptor callbacks for file descriptors
	// // we queued in sigHandlerQueue_r() function above.
	// // we use an array of 1024 bits like the poll function i guess.
	// for ( int32_t i = 0 ; i < n ; i++ ) {
	// 	// this is a 32-bit number
	// 	if ( ! g_fdReadBits[i] ) continue;
	// 	// scan the individual bits now
	// 	for ( int32_t j = 0 ; j < 32 ; j++ ) {
	// 		// mask mask
	// 		uint32_t mask = 1 << j;
	// 		// skip jth bit if not on
	// 		if ( ! g_fdReadBits[i] & mask ) continue;
	// 		// block signals for just a sec so we can
	// 		// clear it now that we've handled it
	// 		//maskSignals();
	// 		// clear it
	// 		g_fdReadBits[i] &= ~mask;
	// 		// reinstate signals
	// 		//unmaskSignals();
	// 		// construct the file descriptor
	// 		int32_t fd = i*32 + j;
	// 		// . call all callbacks registered on this fd
	// 		// . forReading = true
	// 		callCallbacks_ass ( true , fd , g_now );
	// 	}
	// }

	// // do the same thing but for writing now
	// for ( int32_t i = 0 ; i < n ; i++ ) {
	// 	// this is a 32-bit number
	// 	if ( ! g_fdWriteBits[i] ) continue;
	// 	// scan the individual bits now
	// 	for ( int32_t j = 0 ; j < 32 ; j++ ) {
	// 		// mask mask
	// 		uint32_t mask = 1 << j;
	// 		// skip jth bit if not on
	// 		if ( ! g_fdWriteBits[i] & mask ) continue;
	// 		// block signals for just a sec so we can
	// 		// clear it now that we've handled it
	// 		//maskSignals();
	// 		// clear it
	// 		g_fdWriteBits[i] &= ~mask;
	// 		// reinstate signals
	// 		//unmaskSignals();
	// 		// construct the file descriptor
	// 		int32_t fd = i*32 + j;
	// 		// . call all callbacks registered on this fd. 
	// 		// . forReading = false.
	// 		callCallbacks_ass ( false , fd , g_now );
	// 	}
	// }

	// int64_t elapsed = g_now - s_lastTime;
	// // if someone changed the system clock on us, this could be negative
	// // so fix it! otherwise, times may NEVER get called in our lifetime
	// if ( elapsed < 0 ) elapsed = m_minTick;
	// // call this every (about) m_minTicks milliseconds
	// if ( elapsed >= m_minTick ) {
	// 	// MAX_NUM_FDS is the fd for sleep callbacks
	// 	callCallbacks_ass ( true , MAX_NUM_FDS , g_now );
	// 	// note the last time we called them
	// 	//g_now = gettimeofdayInMilliseconds();
	// 	s_lastTime = g_now;
	// }

	// // call remaining callbacks for udp msgs
	// if ( g_udpServer.needBottom() )
	// 	g_udpServer.makeCallbacks_ass ( 2 );

	//if(g_udpServer2.needBottom()) 
	//	g_udpServer2.makeCallbacks_ass ( 2 );

	// if(gettimeofdayInMillisecondsLocal() - 
	//    startTime > 10)
	// 	goto notime;
					
	// if ( g_conf.m_sequentialProfiling )
	// 	g_threads.printState();

	// if ( g_threads.m_needsCleanup )
	// 	// limit to 4ms. cleanup any niceness thread.
	// 	g_threads.timedCleanUp(4 ,MAX_NICENESS);

// #endif

	

	/*

	if ( sigNum <  0 ) {
		if ( errno == EAGAIN || errno == EINTR ||
		     errno == EILSEQ || errno == 0 ) { 
			sigNum = 0; 
			errno = 0;
		}
		else if ( errno != ENOMEM ) {
			log("loop: sigtimedwait(): %s.",
			    strerror(errno));
			continue;
		}
	}
	if ( sigNum == 0 ) {
		//no signals pending, try to take care of anything 
		// left undone:

		int64_t startTime =gettimeofdayInMillisecondsLocal();
		if(g_now & 1) {
			if(g_udpServer.needBottom())  
				g_udpServer.makeCallbacks_ass ( 2 );
			//if(g_udpServer2.needBottom()) 
			//	g_udpServer2.makeCallbacks_ass ( 2 );

			if(gettimeofdayInMillisecondsLocal() - 
			   startTime > 10)
				goto notime;
					
			if(g_conf.m_sequentialProfiling) 
				g_threads.printState();
			if(g_threads.m_needsCleanup)
				g_threads.timedCleanUp(4 , // ms
						       MAX_NICENESS);
		}
		else {
			if(g_conf.m_sequentialProfiling) 
				g_threads.printState();
			if(g_threads.m_needsCleanup)
				g_threads.timedCleanUp(4 , // ms
						       MAX_NICENESS);

			if(gettimeofdayInMillisecondsLocal() - 
			   startTime > 10)
				goto notime;

			if(g_udpServer.needBottom())  
				g_udpServer.makeCallbacks_ass ( 2 );
			//if(g_udpServer2.needBottom()) 
			//	g_udpServer2.makeCallbacks_ass ( 2 );
		}
			
	notime:
		//if we still didn't get all of them cleaned up set
		//sleep time to none.
		if(g_udpServer.needBottom() ) {
			//g_udpServer2.needBottom()) {
			s_sigWaitTimePtr = &s_sigWaitTime2;
		}
		else {
			//otherwise set it to minTick
			s_sigWaitTimePtr = &s_sigWaitTime;
		}
	}
	//
	// we got a signal, process it
	//
	else {
		if   ( info.si_code == SIGIO ) {
			log("loop: got sigio");
			m_needToPoll = true;
		}
		// handle the signal
		else sigHandler_r ( 0 , &info , NULL );
	}
	*/

	// } while(1)

	/*
 loop:
	g_now = gettimeofdayInMilliseconds();
	g_errno = 0;

	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("Loop: top of loop");
	// shutdown if we got a critical signal
	if ( m_shutdown ) {
		// a msg
		if      ( m_shutdown == 1 ) 
			log("loop: got SIGHUP or SIGTERM.");
		else if ( m_shutdown == 2 ) 
			log("loop: got SIGBAD in thread.");
		else                    
			log("loop: got SIGPWR.");
		// . turn off interrupts here because it doesn't help to do
		//   it in the thread
		// . TODO: turn off signals for sigbadhandler()
		interruptsOff();
		// if thread got the signal, just wait for him to save all
		// Rdbs and then dump core
		if ( m_shutdown == 2 ) {
			log("loop: Cored in thread.");
			//log ("Thread is saving and shutting down urgently.");
			//while ( 1 == 1 ) sleep (50000);
		        //log("loop: Resuming despite thread crash.");
			//m_shutdown = 0;
			//goto resume;
		}
		// otherwise, thread did not save, so we must do it
		log ( "loop: Saving and shutting down urgently.");
		// sleep forver now so we can call findPtr() function
		// after calling g_mem.printBreeches(0)
		log("loop: sleeping forever.");
		sleep(1000000);
		// . this will save all Rdb's and dump core
		// . since "urgent" is true it won't broadcast its shutdown
		//   to all hosts
		//#ifndef NO_MAIN
		//mainShutdown( true ); // urgent?
		//#endif
		// urgent = true
		g_process.shutdown ( true );
	}
	// resume:
	// . get the time now, sigHandlerRT() can use this, cuz gettimeofday()
	//   ain't async signal safe
	// . g_now only used in hot udpServer for doing time outs really
	g_now = gettimeofdayInMilliseconds();
	// clear any g_errno before possibly calling sendPoll_ass()
	g_errno = 0;
	// occasionaly call to sendto() will not send a dgram and since we
	// don't count on receiving ready-to-write signals on our 
	// UdpServer's fds we check them here... it sucks, but hopefully it 
	// fixes the problem of requests not getting fully transmitted 
	// and stagnating in the UdpServer.
	//if (g_udpServer2.m_needToSend) g_udpServer2.sendPoll_ass(true,g_now);
	//if (g_udpServer.m_needToSend ) g_udpServer.sendPoll_ass (true,g_now);
	// . well, sender may be choking even with m_needsToSend var
	// . i bet we're just losing signals
	// . TODO: do we lose them when in the handler? if so, send a signal
	//   so loop will read/write after coming out of the async handler
	//g_udpServer2.sendPoll_ass(true,g_now);
	// and also read just in case, too
	//g_udpServer2.process_ass ( g_now );
	// and the low priority guy
	g_udpServer.sendPoll_ass (true,g_now);
	g_udpServer.process_ass ( g_now );
	// and dns now too
	g_dns.m_udpServer.sendPoll_ass(true,g_now);
	g_dns.m_udpServer.process_ass ( g_now );
	// tcp server contained in http server has the same problem
	//if ( g_httpServer.m_tcp.m_numQueued > 0 )
	//	g_httpServer.m_tcp.writeSocketsInQueue();

	// . likewise, we may have not got the SIGQUEUE signal from when 
	//   UdpServer wanted to call a callback but couldn't because it was
	//   in an async signal handler, and then at SIGQUEUE sig got lost...
	// . this should clear those completed transactions we sometimes
	//   see in the UdpServer socket table
	if ( g_someAreQueued ) {
		// assume none are queued now, we may get interrupted
		// and it may get set back to true
		g_someAreQueued = false;
		//g_udpServer2.makeCallbacks_ass (  0 );
		//g_udpServer2.makeCallbacks_ass (  1 );
	}
	// clean up threads in case signal got lost somehow
	if ( g_threads.m_needsCleanup ) {
		// bitch about
		static bool s_bitched = false;
		if ( ! s_bitched ) {
			log(LOG_REMIND,"loop: Lost thread signal.");
			s_bitched = true;
		}
		// assume not any more
		//g_threads.m_needsCleanup = false;
		// check thread queue for any threads that completed
		// so we can call their callbacks and remove them
		g_threads.cleanUp ( 0 , 1000) ; // max niceness
		// launch any threads in waiting since this sig was 
		// from a terminating one
		g_threads.launchThreads();
	}
	// clear any g_errno before calling sleep callbacks
	g_errno = 0;
	// get time difference
	elapsed = g_now - s_lastTime;
	// if someone changed the system clock on us, this could be negative
	// so fix it! otherwise, times may NEVER get called in our lifetime
	if ( elapsed < 0 ) elapsed = m_minTick;
	// print log msgs we accumulated while in a signal handler
	//if ( g_log.needsPrinting() ) g_log.printBuf();
	// . poll if we need to
	// . make it a while loop incase a hot sig handler resets m_needToPoll
	// . CAUTION: WE CAN LOOP IN HERE FOR ~ A MINUTE! I've seen it happen
	//   when RdbDump was going...
	while ( m_needToPoll ) doPoll();
	// call this every (about) 1 second
	if ( elapsed >= m_minTick ) {
		// MAX_NUM_FDS is the fd for sleep callbacks
		callCallbacks_ass ( true , MAX_NUM_FDS , g_now );
		// note the last time we called them
		s_lastTime = g_now;
	}
	// just do polls if this linux doesn't support this:
#ifdef _POLLONLY_
	doPoll();
#endif
#ifndef _POLLONLY_
	// cancel silly errors
	//if ( g_errno == EAGAIN ) { sigNum = 0; g_errno = 0; }
	//if ( g_errno == EINTR  ) { sigNum = 0; g_errno = 0; }
	// if sigNum is valid then handle it
	//sigHandler_r ( 0 , &info , NULL );
	// subloop:
	// debug msg


	//if ( g_conf.m_logDebugUdp ) log("Loop: entering sigwait");
	// . this has a timer resolution of 20ms, I imagine due to how the 
	//   kernel time slices between processes
	// . this means UdpServer can not effectively have a wait between
	//   resends of less than 20ms which makes it a little less zippy
	sigNum = sigtimedwait (&sigs0, &info, &s_sigWaitTime ) ;
	// cancel silly errors
	if ( sigNum < 0 && errno == EAGAIN ) { 
		sigNum = 0; errno = 0; }
	if ( sigNum < 0 && errno == EINTR  ) { 
		sigNum = 0; errno = 0; }
	// a zero signal is no signal, just a wake up call
	if ( sigNum == 0 ) goto loop;
	// sigNum is < 0 on error
	if ( sigNum <  0 ) {
		// this error happens on the newer libc for some reason
		// so ignore it
		if ( errno != ENOMEM )
			log("loop: sigtimedwait(): %s.",strerror(errno));
		goto loop;
	}
	// sleep test
	//log("SLEEPING");
	//sleep(10);
	//for ( int64_t i = 0 ; i < 1000000000000LL ; i++ );
	// . we use g_now in UdpServer.cpp and should make it
	//   as accurate as possible
	// . but it's main use is because gettimeofday() is not async sig safe
	g_now = gettimeofdayInMilliseconds();
	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("Loop: processing signal");
	// call sighandler on other queued signals and continue looping
	//log("Loop::runLoop:got queued signum=%"INT32"",sigNum);
	// was it a SIGIO?
	if   ( info.si_code == SIGIO ) doPoll();
	// handle the signal
	else                           sigHandler_r ( 0 , &info , NULL );
#endif
	// don't call any time handlers until no more signals waiting
	//goto subloop;
	// we need to make g_now as accurate as possible for hot UdpServer...
	goto loop;
*/

}

// . the kernel sends a SIGIO signal when the sig queue overflows
// . we resort to polling the fd's when that happens
// void sigioHandler ( int x , siginfo_t *info , void *y ) {
// 	// set the m_needToPoll flag
// 	g_loop.m_needToPoll = true;
// 	return;
// }

//--- TODO: flush the signal queue after polling until done
//--- are we getting stale signals resolved by flush so we get
//--- read event on a socket that isnt in read mode???
// TODO: set signal handler to SIG_DFL to prevent signals from queuing up now
// . this handles high priority fds first (lowest niceness)
void Loop::doPoll ( ) {
	// set time
	//g_now = gettimeofdayInMilliseconds();
	// debug msg
	//log("**************** GOT SIGIO *************");
	// . turn it off here so it can be turned on again after we've
	//   called select() so we don't lose any fd events through the cracks
	// . some callbacks we call make trigger another SIGIO, but if they
	//   fail they should set Loop::g_needToPoll to true
	m_needToPoll = false; 
	// debug msg
	//if ( g_conf.m_logDebugLoop ) log(LOG_DEBUG,"loop: Entered doPoll.");
	if ( g_conf.m_logDebugLoop) log(LOG_DEBUG,"loop: Entered doPoll.");
	// print log
	if ( g_log.needsPrinting() ) g_log.printBuf();
	 
	// sigqueue() might have been called from a hot udp server and 
	// we queued some handlers to be called
	if ( g_someAreQueued ) {
		// assume none are queued now, we may get interrupted
		// and it may get set back to true
		g_someAreQueued = false;
		//g_udpServer2.makeCallbacks_ass (  0 );
		//g_udpServer2.makeCallbacks_ass (  1 );
	}
	if(g_udpServer.needBottom()) g_udpServer.makeCallbacks_ass ( 1 );
	//if(g_udpServer2.needBottom()) g_udpServer2.makeCallbacks_ass ( 1 );


	//bool processedOne;
	int32_t n;
	//	int32_t repeats = 0;
	// skipLowerPriorities:
	// descriptor bits for calling select()
	// fd_set readfds;
	// fd_set writefds;
	// fd_set exceptfds;
	// clear fds for select()
	//FD_ZERO ( &readfds   );
	//FD_ZERO ( &writefds  );
	//FD_ZERO ( &exceptfds );
	timeval v;
	v.tv_sec  = 0;
	if ( m_inQuickPoll ) v.tv_usec = 0;
	// 10ms for sleepcallbacks so they can be called...
	// and we need this to be the same as sigalrmhandler() since we
	// keep track of cpu usage here too, since sigalrmhandler is "VT"
	// based it only goes off when that much "cpu time" has elapsed.
	else                 v.tv_usec = QUICKPOLL_INTERVAL * 1000;  

	//int32_t count = v.tv_usec;

	// set descriptors we should watch
	// MDW: no longer necessary since we have s_selectMaskRead, etc.
	// for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {
	// 	if ( m_readSlots [i] ) {
	// 		FD_SET ( i , &readfds   );
	// 		FD_SET ( i , &exceptfds );
	// 	}
	// 	if ( m_writeSlots[i] ) {
	// 		FD_SET ( i , &writefds );
	// 		FD_SET ( i , &exceptfds );
	// 	}
	// }

	// gotta copy to our own since bits get cleared by select() function
	fd_set readfds;
	fd_set writefds;

	// only register write callbacks if TcpServer.cpp failed to write
	// the # of bytes that it wanted to a socket descriptor. and it
	// should unregister the writecallback as soon as it is able to
	// write the bytes it wanted to write.

	//FD_ZERO ( &writefds );

	//int64_t startTime = gettimeofdatInMillisecondsLocal();

 again:

	//fd_set exceptfds;
	gbmemcpy ( &readfds, &s_selectMaskRead , sizeof(fd_set) );
	gbmemcpy ( &writefds, &s_selectMaskWrite , sizeof(fd_set) );
	//gbmemcpy ( &exceptfds, &s_selectMaskExcept , sizeof(fd_set) );

	// what is the point of fds for writing... its for when we
	// get a new socket via accept() it is read for writing...
	//FD_ZERO ( &writefds );
	//FD_ZERO ( &exceptfds );

	if ( g_conf.m_logDebugLoop )
		log("loop: in select");

	// used to measure cpu usage. sigalarm needs to know if we are
	// sitting idle in select() or are actively doing something w/ the cpu
	g_inWaitState = true;

	// for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
	// 	// continue if not set for reading
	// 	if ( FD_ISSET ( i , &s_selectMaskRead ) ||
	// 	     FD_ISSET ( i , &writefds ) ||
	// 	     FD_ISSET ( i , &exceptfds ) )
	// 	// debug
	// 	log("loop: fd %"INT32" is set",i);
	// 	// if niceness is not -1, handle it below
	// }

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
		    NULL,//&exceptfds,
		    &v );

	g_inWaitState = false;

	if ( n >= 0 ) errno = 0;

	if ( g_conf.m_logDebugLoop )
		log("loop: out select n=%"INT32" errno=%"INT32" errnomsg=%s "
		    "ms_wait=%i",
		    (int32_t)n,(int32_t)errno,mstrerror(errno),
		    (int)v.tv_sec*1000);

	if ( n < 0 ) { 
		// valgrind
		if ( errno == EINTR ) {
			// got it. if we get a sig alarm or vt alarm or
			// SIGCHLD (from Threads.cpp) we end up here.
			//log("loop: got errno=%"INT32"",(int32_t)errno);
			// if not linux we have to decrease this by 1ms
			//count -= 1000; 
			// and re-assign to wait less time. we are
			// assuming SIGALRM goes off once per ms and if
			// that is not what interrupted us we may end
			// up exiting early
			//if ( count <= 0 && m_shutdown ) return;
			// wait less this time around
			//v.tv_usec = count;
			// if shutting down was it a sigterm ?
			if ( m_shutdown ) goto again;
			// handle returned threads for niceness 0
			//if ( g_threads.m_needsCleanup )
			g_threads.timedCleanUp(-3,0); // 3 ms
			if ( m_inQuickPoll ) goto again;
			// high niceness threads
			//if ( g_threads.m_needsCleanup )
			g_threads.timedCleanUp(-4,MAX_NICENESS); //3 ms

			goto again;
		}
		g_errno = errno;
		log("loop: select: %s.",strerror(g_errno));
		return;
	}

	// if we wait for 10ms with nothing happening, fix cpu usage here too
	// if ( n == 0 ) {
	// 	Host *h = g_hostdb.m_myHost;
	// 	h->m_cpuUsage = .99 * h->m_cpuUsage + .01 * 000;
	// }

	// debug msg
	if ( g_conf.m_logDebugLoop) 
		logf(LOG_DEBUG,"loop: Got %"INT32" fds waiting.",n);

	for ( int32_t i = 0 ; 
	      (g_conf.m_logDebugLoop || g_conf.m_logDebugTcp) && i<MAX_NUM_FDS;
	      i++){
	  	// continue if not set for reading
		 if ( FD_ISSET ( i , &readfds ) )
			 log("loop: fd=%"INT32" is on for read qp=%i",i,
			     (int)m_inQuickPoll);
	 	if ( FD_ISSET ( i , &writefds ) )
			log("loop: fd=%"INT32" is on for write qp=%i",i,
			    (int)m_inQuickPoll);
	 	// if ( FD_ISSET ( i , &exceptfds ) )
	 	// 	log("loop: fd %"INT32" is on for except",i);
	  	// debug

	 	// if niceness is not -1, handle it below
	}

	// . reset the need to poll flag if everything is caught up now
	// . let's take this out for now ... won't this leave some
	//   threads hanging, they do not always generate SIGIO's if 
	//   the sigqueue is full!! LET'S SEE... mdw
	//if ( n == 0 ) {
	//	// deal with any threads before returning
	//	g_threads.cleanUp ( NULL , 1000 /*max niceness*/ );
	//	g_threads.launchThreads();
	//	return;
	//}
	//processedOne = false;

	// a Slot ptr
	Slot *s;
	g_now = gettimeofdayInMilliseconds();
	/*
	// call g_udpServer sig handlers for niceness -1 here
	for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
		// continue if not set for reading
		if ( ! FD_ISSET ( i , &readfds ) ) continue;
		// if niceness is not -1, handle it below
		s = m_readSlots  [ i  ];
		if ( s && s->m_niceness != -1 ) continue;
		callCallbacks_ass (true,i, g_now); // for reading = true
		processedOne = true;
	}
	for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		if ( ! FD_ISSET ( i , &writefds ) ) continue;
		// if niceness is not -1, handle it below
		s = m_writeSlots  [ i ];
		if ( s && s->m_niceness != -1 ) continue;
		callCallbacks_ass (false,i, g_now); // for reading = false
		processedOne = true;
	}
	*/
	// handle returned threads for niceness -1
	//g_threads.timedCleanUp(2/*ms*/);

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
			log("loop: calling cback0 niceness=%"INT32" "
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
			log("loop: calling wcback0 niceness=%"INT32" fd=%i"
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
			log("loop: calling cback1 niceness=%"INT32" "
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
			log("loop: calling wcback1 niceness=%"INT32" "
			    "fd=%i", s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (false,fd, g_now,1);//forread?
	}

	//if ( ! calledOne )
	//	log("loop: select returned n=%"INT32" but nothing called.",n);


	// . MDW: replaced this with more efficient logic above
	// . call the callback for each fd we got
	// . only call callbacks for fds that have a nice of 0 here
	// for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
	// 	// continue if not set for reading
	// 	if ( ! FD_ISSET ( i , &readfds ) ) continue;
	// 	// if niceness is not 0, handle it below
	// 	s = m_readSlots  [ i /*fd*/ ];
	// 	if ( s && s->m_niceness != 0 ) continue;
	// 	callCallbacks_ass (true/*forReading?*/,i, g_now);
	// 	processedOne = true;
	// }
	// // only call callbacks for fds that have a nice of 0 here
	// for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {
	// 	if ( ! FD_ISSET ( i , &writefds ) ) continue;
	// 	// if niceness is not 0, handle it below
	// 	s = m_writeSlots  [ i /*fd*/ ];
	// 	if ( s && s->m_niceness != 0 ) continue;
	// 	callCallbacks_ass (false/*forReading?*/,i, g_now);
	// 	processedOne = true;
	// }


// #if 0
// 	if(processedOne && repeats < QUERYPRIORITYWEIGHT) {
// 		//m_needToPoll = true; 
// 		repeats++;
// 		goto skipLowerPriorities;
// 	}
// // 	log(LOG_WARN, 
// // 	"Loop: repeated %"INT32" times before moving to lower priority threads", 
// // 		repeats);
// #endif

	// // handle low priority fds here
	// for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
	// 	// continue if not set for reading
	// 	if ( ! FD_ISSET ( i , &readfds ) ) continue;
	// 	// if niceness is 0, we already handled it above
	// 	s = m_readSlots  [ i /*fd*/ ];
	// 	if ( s && s->m_niceness <= 0 ) continue;
	// 	callCallbacks_ass (true/*forReading?*/,i, g_now);
	// }
	// // only call callbacks for fds that have a nice of 0 here
	// for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {
	// 	if ( ! FD_ISSET ( i , &writefds ) ) continue;
	// 	// if niceness is 0, we already handled it above
	// 	s = m_writeSlots  [ i /*fd*/ ];
	// 	if ( s && s->m_niceness <= 0 ) continue;
	// 	callCallbacks_ass (false/*forReading?*/,i, g_now);
	// }

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

// for FileState class
#include "BigFile.h"

// call this when you don't want to be interrupted
void Loop::interruptsOff ( ) {
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
	// tmp debug hack, so we don't have real time signals now...
// #ifndef _VALGRIND_
// 	sigaddset   ( &rtmin, GB_SIGRTMIN );
// #endif
	// block it
	if ( sigprocmask ( SIG_BLOCK  , &rtmin, 0 ) < 0 ) {
		log("loop: interruptsOff: sigprocmask: %s.", strerror(errno));
		return;
	}
	g_interruptsOn = false;
	// debug msg
	//log("interruptsOff");
}
// and this to resume being interrupted
void Loop::interruptsOn ( ) {
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
	// uncomment this next line to easily disable real time interrupts
// #ifndef _VALGRIND_
// 	sigaddset   ( &rtmin, GB_SIGRTMIN );
// #endif
	// debug msg
	//log("interruptsOn");
	// let everyone know before we are vulnerable to an interrupt
	g_interruptsOn = true;
	// unblock it so interrupts flow
	if ( sigprocmask ( SIG_UNBLOCK, &rtmin, 0 ) < 0 ) {
		log("loop: interruptsOn: sigprocmask: %s.", strerror(errno));
		return;
	}
}

/*
// handle hot real time signals here
void sigHandlerRT ( int x , siginfo_t *info , void *v ) {
	// if we're not hot, what are we doing here?
	if ( ! g_isHot ) {
		fprintf(stderr,"SHIT\n");
		exit(-1);
	}
	// bitch if we shouldn't have gotten this
	// fprintf(stderr,"Hey! why are we here?\n");
	if ( ! g_interruptsOn ) {
		log(LOG_LOGIC,"loop: Interrupts not on. Bad engineer.");
		return;
	}
	//fprintf (stderr,"in rt handler\n");
	// let everyone know it
	// MDW: turn this off for now, how is it getting set? we dont use
	// real time signals any more. maybe a pthread is getting such
	// a signal?
	//g_inSigHandler = true;
	// debug msg
	//if ( g_conf.m_timingDebugEnabled ) 
	//	log("sigHandlerRT entered");
	// save errno
	int32_t old_gerrno = g_errno;
	int  old_errno  = errno;
	// call normal handler
	sigHandler_r ( x , info , v );
	// restore
	errno   = old_errno;
	g_errno = old_gerrno;
	// debug msg
	//if ( g_conf.m_timingDebugEnabled ) 
	//	log("sigHandlerRT exited");
	// revert
	g_inSigHandler = false;
	// debug msg
	//fprintf (stderr,"out of rt handler\n");
}
*/

/*
// come here when we get a GB_SIGRTMIN+X signal
void sigHandler_r ( int x , siginfo_t *info , void *v ) {

	// cygwin lacks the si_fd and si_band members
#ifdef CYGWIN
	g_loop.doPoll();
#else

	// extract the file descriptor that needs attention
	int fd   = info->si_fd;
	// debug note
	//if ( fd == g_httpServer.m_tcp.m_sock )
	//	logf(LOG_DEBUG,"loop: got http server activity");
	// print signal number for debugging purposes (should be SIGPOLL/IO)
	// debug msg
	//fprintf(stderr,"got sigcode=%i\n",info->si_code );
	//fprintf(stderr,"got signo  =%i\n",info->si_signo);
	//if ( info->si_signo == SIGCHLD )
	//	fprintf(stderr,"got SIGCHLD\n");
	//if ( info->si_code == SIGCHLD )
	//	fprintf(stderr,"got SIGCHLD2\n");
	//fprintf(stderr,"got sigval =%i\n",(int)(info->si_value.sival_int) );
	// clear g_errno before callling handlers
	g_errno = 0;
	// info->si_band values:
	//#define POLLIN      0x0001    // There is data to read 
        //#define POLLPRI     0x0002    // There is urgent data to read 
	//#define POLLOUT     0x0004    // Writing now will not block 
	//#define POLLERR     0x0008    // Error condition 
	//#define POLLHUP     0x0010    // Hung up 
	//#define POLLNVAL    0x0020    // Invalid request: fd not open 
	int band = info->si_band;  
	// fprintf(stderr,"got fd         = %i\n", fd   );
	// fprintf(stderr,"got band       = %i\n", band );
	// fprintf(stderr,"band & POLLIN  = %i\n", band & POLLIN  );
	// fprintf(stderr,"band & POLLPRI = %i\n", band & POLLPRI );
	// fprintf(stderr,"band & POLLOUT = %i\n", band & POLLOUT );
	// fprintf(stderr,"band & POLLERR = %i\n", band & POLLERR );
	// fprintf(stderr,"band & POLLHUP = %i\n", band & POLLHUP );
	// translate SIGPIPE's to band of POLLHUP
	if ( info->si_signo == SIGPIPE ) {
		band = POLLHUP;
		log("loop: Received SIGPIPE signal. Broken pipe.");
	}
	// . SI_QUEUE signals used to just be from BigFile
	// . but now they're used by threads so we can call a callback
	//   when the thread is completed
	if ( g_someAreQueued && ! g_inSigHandler ) {
		g_someAreQueued = false;
		//g_udpServer2.makeCallbacks_ass (  0 );
		//g_udpServer2.makeCallbacks_ass (  1 );
	}

	if ( g_threads.m_needsCleanup && ! g_inSigHandler ) {
		// assume not any more
		//g_threads.m_needsCleanup = false;
		// get the value
		//int val = (int)(info->si_value.sival_int);
		// debug msg
		//if ( g_conf.m_logDebugThreadEnabled ) 
		//	log("Loop: got thread done signal");
		// check thread queue for any threads that completed
		// so we can call their callbacks and remove them
		g_threads.timedCleanUp(4,MAX_NICENESS); // 4 ms

// 		//		g_threads.cleanUp ( (ThreadEntry *)val , x);// max niceness
// 		g_threads.cleanUp ( (ThreadEntry *)val , 1000);//max niceness

// 		// launch any threads in waiting since this sig was 
// 		// from a terminating one
// 		g_threads.launchThreads();
	}

	// if we just needed to cleanup a thread
	if ( info->si_signo == SIGCHLD ) return;

	// if we don't got a signal for an fd, just a sigqueue() call, bail now
	if ( info->si_code == SI_QUEUE ) return;

	// . call the appropriate handler(s)
	// . TODO: bitch if no callback to handle the read!!!!!!!
	// . NOTE: when it's connected it sets both POLLIN and POLLOUT
	// . NOTE: or when a socket is trying to connect to it if it's listener
	//if      ( band & (POLLIN | POLLOUT) == (POLLIN | POLLOUT) ) 
	// g_loop.callCallbacks_ass ( true , fd ); // for reading
	if ( band & POLLIN  ) {
		// keep stats on this now since some linuxes dont work right
		g_stats.m_readSignals++;
		//log("Loop: read %"INT64" fd=%i",gettimeofdayInMilliseconds(),fd);
		g_loop.callCallbacks_ass ( true  , fd ); 
	}
	else if ( band & POLLPRI ) {
		// keep stats on this now since some linuxes dont work right
		g_stats.m_readSignals++;
		//log("Loop: read %"INT64" fd=%i",gettimeofdayInMilliseconds(),fd);
		g_loop.callCallbacks_ass ( true  , fd ) ;
	}
	else if ( band & POLLOUT ) {
		// keep stats on this now since some linuxes dont work right
		g_stats.m_writeSignals++;
		//log("Loop: write %"INT64" fd=%i",gettimeofdayInMilliseconds(),fd)
		g_loop.callCallbacks_ass ( false , fd ); 
	}
	// fix qainject1() test with this
	else if ( band & POLLERR )  {
		log(LOG_INFO,"loop: got POLLERR on fd=%i.",fd);
	}
	//g_loop.callCallbacks_ass ( false , fd ); 
	// this happens if the socket closes abruptly
	// or out of band data, etc... see "man 2 poll" for more info
	else if ( band & POLLHUP ) { 
		// i see these all the time for fd == 0, so don't print it
		if ( fd != 0 ) 
			log(LOG_INFO,"loop: Received hangup on fd=%i.",fd);
		g_errno = ESOCKETCLOSED; 
		g_loop.callCallbacks_ass ( false , fd );
	}
// end ifdef CYGWIN
#endif
}
*/

/*
#if 1 || (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,31)) 
struct pollfd pfd;
printf ("Trying fallback poll()\n");
pfd.fd = info.si_fd;
pfd.events = POLLIN|POLLOUT|POLLHUP;
if (poll (&pfd, 1, 0) < 0)	{
	p_error ("poll(): %s", strerror (g_errno));
	exit (1);
}
info.si_band = pfd.revents;
#endif
*/


void Loop::startBlockedCpuTimer() {
	
	if(m_inQuickPoll) return;
	m_lastPollTime = gettimeofdayInMilliseconds();
	g_profiler.resetLastQpoll();
}


void Loop::quickPoll(int32_t niceness, const char* caller, int32_t lineno) {
	if ( ! g_conf.m_useQuickpoll ) return;

	// convert
	//if ( niceness > 1 ) niceness = 1;

	// sometimes we init HashTableX's with a niceness of 0 even though
	// g_niceness is 1. so prevent a core below.
	if ( niceness == 0 ) return;

	// sanity check
	if ( g_niceness > niceness ) { 
		log(LOG_WARN,"loop: niceness mismatch!");
		//char *xx=NULL;*xx=0; }
	}

	// sanity -- temporary -- no quickpoll in a thread!!!
	//if ( g_threads.amThread() ) { char *xx=NULL;*xx=0; }

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
		//if(g_conf.m_quickpollCoreOnError) { 
		char*xx=NULL;*xx=0;
		//		}
		//		else return;
	}
	int64_t now = g_now;
	//int64_t took = now - m_lastPollTime;
	int64_t now2 = g_now;
	int32_t gerrno = g_errno;

	/*
	if(g_conf.m_profilingEnabled){
		now = gettimeofdayInMilliseconds();
		took = now - m_lastPollTime;

		g_profiler.pause(caller, lineno, took);

		if(took > g_conf.m_minProfThreshold) {
			if(g_conf.m_dynamicPerfGraph) {
				g_stats.addStat_r ( 0      , 
						    m_lastPollTime, 
						    now,
						    0 ,
						    STAT_GENERIC,
						    caller);
			}
			if(g_conf.m_sequentialProfiling) {
				log(LOG_TIMING, "admin: quickpolling from %s "
				    "after %"INT64" ms", caller, took);
			}
		}
	}
	*/

	g_numQuickPolls++;

	m_inQuickPoll = true;

	// doPoll() will since we are in quickpoll and only call niceness 0
	// callbacks for all the fds. and it will set the timer to 0.
	doPoll ();

	/*
	//g_udpServer2.process_ass ( g_now , 0 );
 	g_udpServer.process_ass  ( g_now , 0 );
	g_threads.timedCleanUp( 100 , 0 ); // ms ms, niceness 0
	int32_t n;
	
	fd_set readfds;
	fd_set writefds;
	fd_set exceptfds;
	// clear fds for select()
	FD_ZERO ( &readfds   );
	FD_ZERO ( &writefds  );
	FD_ZERO ( &exceptfds );
	timeval v;
	v.tv_sec  = 0;
	v.tv_usec = 0; 
	// set descriptors we should watch
	for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		if ( m_readSlots [i] && m_readSlots[i]->m_niceness == 0 ) {
			FD_SET ( i , &readfds   );
			FD_SET ( i , &exceptfds );
		}
		if ( m_writeSlots[i] && m_writeSlots[i]->m_niceness == 0 ) {
			FD_SET ( i , &writefds );
			FD_SET ( i , &exceptfds );
		}
	}
	// poll the fd's searching for socket closes
	// this is for httpServer, since we handled
	// udpserver and diskthreads above
	n = select (MAX_NUM_FDS, &readfds, &writefds, &exceptfds, &v);
	// a Slot ptr
	Slot *s;
	if ( n <= 0 ) goto theend;

	for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
		// continue if not set for reading
		if ( ! FD_ISSET ( i , &readfds ) ) continue;
		// if niceness is not -1, handle it below
		s = m_readSlots  [ i ]; // i = fd
		// now we have niceness 2 if Sections.cpp
		if ( s && s->m_niceness >= niceness ) continue;
		callCallbacks_ass (true,i, now); // reading = true
		// sanity check
		if ( g_niceness > niceness ) { char*xx=NULL;*xx=0; }
	}
	for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		if ( ! FD_ISSET ( i , &writefds ) ) continue;
		// if niceness is not -1, handle it below
		s = m_writeSlots  [ i  ]; // i = fd
		// now we have niceness 2 if Sections.cpp
		if ( s && s->m_niceness >= niceness ) continue;
		callCallbacks_ass (false,i, now); // forReading = false
		// sanity check
		if ( g_niceness > niceness ) { char*xx=NULL;*xx=0; }
	}


	// now we can have niceness 0 dns slots because of the niceness
	// conversion algorithm...
	g_dns.m_udpServer.sendPoll_ass(true,g_now);
	g_dns.m_udpServer.process_ass ( g_now );
	g_dns.m_udpServer.makeCallbacks_ass(0);

 theend:
	*/

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
		//char*xx=NULL;*xx=0; }
	}

	/*
	now2 = g_now;
	if(g_conf.m_profilingEnabled) {
		took = now2 - now;
		g_profiler.unpause();
	}
	*/
	//log(LOG_WARN, "xx quickpolled took %"INT64", waited %"INT64" from %s", 
	//    now2 - now, now - m_lastPollTime, caller);
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
	//logf(LOG_WARN,"xxx disabling");
	m_canQuickPoll = false;
	setitimer(ITIMER_VIRTUAL, &m_noInterrupt, NULL);
	setitimer(ITIMER_REAL, &m_noInterrupt, NULL);
}

int gbsystem(char *cmd ) {
	// if ( ! g_conf.m_runAsDaemon )
	// 	setitimer(ITIMER_REAL, &g_loop.m_noInterrupt, NULL);
	g_loop.disableTimer();
	log("gb: running system(\"%s\")",cmd);
	int ret = system(cmd);
	g_loop.enableTimer();
	// if ( ! g_conf.m_runAsDaemon )
	// 	setitimer(ITIMER_REAL, &g_loop.m_realInterrupt, NULL);
	return ret;
}
	

void Loop::enableTimer() {
	m_canQuickPoll = true;
	//	logf(LOG_WARN, "xxx enabling");
	setitimer(ITIMER_VIRTUAL, &m_quickInterrupt, NULL);
	setitimer(ITIMER_REAL, &m_realInterrupt, NULL);
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


//calling with a 0 niceness will turn off the timer interrupt
// void Loop::setitimerInterval(int32_t niceness) {
// 	if(niceness) {
// 		setitimer(ITIMER_VIRTUAL, &m_quickInterrupt, NULL);
// 		m_canQuickPoll = true;
// 	}
// 	else {
// 		setitimer(ITIMER_VIRTUAL, &m_noInterrupt, NULL);
// 		m_canQuickPoll = false;
// 	}
// }
