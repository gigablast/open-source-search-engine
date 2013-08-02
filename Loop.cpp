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

//#define _POLLONLY_

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

long g_numAlarms = 0;
long g_numQuickPolls = 0;
long g_missedQuickPolls = 0;

// since we can't call gettimeofday() while in a sig handler, we use this
// and update it periodically to keep it somewhat accurate
long long g_now = 0;
//long long g_nowGlobal = 0;
long long g_nowApprox = 0;

char g_inWaitState = false;

// a global class extern'd in .h file
Loop g_loop;

// the global niceness
char g_niceness = 0;

// we make sure the same callback/handler is not hogging the cpu when it is
// niceness 0 and we do not interrupt it, so this is a critical check
class UdpSlot *g_callSlot = NULL;
long g_lastTransId  = 0;
long g_transIdCount = 0;

// keep the sig wait time static so we can change it based on m_minTick
static struct timespec s_sigWaitTime ;
static struct timespec s_sigWaitTime2 ;
static struct timespec* s_sigWaitTimePtr ;

// use this in case we unregister the "next" callback
static Slot *s_callbacksNext;

// this is defined in main.cpp
//extern bool mainShutdown ( bool urgent );

// set it from milliseconds
void Loop::setSigWaitTime ( long ms ) {
	long secs = ms / 1000;
	ms -= secs * 1000;
	s_sigWaitTime.tv_sec  = secs;
	s_sigWaitTime.tv_nsec = ms * 1000000;
}

// free up all our mem
void Loop::reset() {
	if ( m_slots ) {
		log("db: resetting loop");
		mfree ( m_slots , MAX_SLOTS * sizeof(Slot) , "Loop" );
	}
	m_slots = NULL;
	/*
	for ( long i = 0 ; i < MAX_NUM_FDS+2 ; i++ ) {
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

static void sigHandler_r  ( int x , siginfo_t *info , void *v ) ;
static void sigHandlerRT  ( int x , siginfo_t *info , void *v ) ;
static void sigbadHandler ( int x , siginfo_t *info , void *y ) ;
static void sigpwrHandler ( int x , siginfo_t *info , void *y ) ;
static void sighupHandler ( int x , siginfo_t *info , void *y ) ;
static void sigioHandler  ( int x , siginfo_t *info , void *y ) ;
static void sigalrmHandler( int x , siginfo_t *info , void *y ) ;

void Loop::unregisterReadCallback ( int fd, void *state ,
				    void (* callback)(int fd,void *state),
				    bool silent ){
	if ( fd < 0 ) return;
	// from reading
	unregisterCallback ( m_readSlots  , fd          , state , callback ,
			     silent );
}

void Loop::unregisterWriteCallback ( int fd, void *state ,
				    void (* callback)(int fd,void *state)){
	// from writing
	unregisterCallback ( m_writeSlots , fd          , state , callback );
}

void Loop::unregisterSleepCallback ( void *state ,
				     void (* callback)(int fd,void *state)){
	unregisterCallback (m_readSlots,MAX_NUM_FDS,state,callback);
}

void Loop::unregisterCallback ( Slot **slots , int fd , void *state ,
				void (* callback)(int fd,void *state) ,
				bool silent ) {
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
	long min     = 40; // 0x7fffffff;
	long lastMin = min;
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
		// debug msg
		//log("Loop::unregistered fd=%li state=%lu", fd, (long)state );
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
	log(LOG_LOGIC,
	    "loop: unregisterCallback: callback not found (fd=%i).",fd);
}

bool Loop::registerReadCallback  ( int fd,
				   void *state, 
				   void (* callback)(int fd,void *state ) ,
				   long  niceness ) {
	// the "true" answers the question "for reading?"
	if ( addSlot ( true, fd, state, callback, niceness ) ) return true;
	return log("loop: Unable to register read callback.");
}


bool Loop::registerWriteCallback ( int fd,
				   void *state, 
				   void (* callback)(int fd, void *state ) ,
				   long  niceness ) {
	// the "false" answers the question "for reading?"
	if ( addSlot ( false, fd, state, callback, niceness ) )return true;
	return log("loop: Unable to register write callback.");
}

// tick is in milliseconds
bool Loop::registerSleepCallback ( long tick ,
				   void *state, 
				   void (* callback)(int fd,void *state ) ,
				   long niceness ) {
	if ( ! addSlot ( true, MAX_NUM_FDS, state, callback , niceness ,tick) )
		return log("loop: Unable to register sleep callback");
	if ( tick < m_minTick ) m_minTick = tick;
	// wait this long in the sig wait loop
	setSigWaitTime ( m_minTick );
	return true;
}

// . returns false and sets g_errno on error
bool Loop::addSlot ( bool forReading , int fd, void *state, 
		     void (* callback)(int fd, void *state), long niceness ,
		     long tick ) {

	// ensure fd is >= 0
	if ( fd < 0 ) {
		g_errno = EBADENGINEER;
		return log(LOG_LOGIC,"loop: fd to register is negative.");
	}
	// sanity
	if ( fd > MAX_NUM_FDS ) {
		log("loop: bad fd of %li",(long)fd);
		char *xx=NULL;*xx=0; 
	}
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
			log(LOG_LOGIC,"loop: fd %i is already registered.",fd);
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
	//log("Loop::registered fd=%i state=%lu",fd,state);
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
bool Loop::setNonBlocking ( int fd , long niceness ) {
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
	// truncate nicess cuz we only get GB_SIGRTMIN+1 to GB_SIGRTMIN+2 signals
	if ( niceness < -1             ) niceness = -1;
	if ( niceness >  MAX_NICENESS  ) niceness = MAX_NICENESS;
	// debug msg
	//log("fd on niceness = %li sig = %li",niceness,GB_SIGRTMIN +1+niceness);
 retry6:
	// . tell kernel to send this signal when fd is ready for read/write
	// . reserve GB_SIGRTMIN for unmaskable interrupts (niceness = -1)
	//   as used by high priority udp server, g_udpServer2
	if ( fcntl (fd, F_SETSIG , GB_SIGRTMIN/*32?*/ + 1 + niceness ) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry6;
		g_errno = errno;
		return log("loop: fcntl(F_SETSIG): %s.",strerror(errno));
	}
	return true;
}

// . if "forReading" is true  call callbacks registered for reading on "fd" 
// . if "forReading" is false call callbacks registered for writing on "fd" 
// . if fd is MAX_NUM_FDS and "forReading" is true call all sleepy callbacks
void Loop::callCallbacks_ass ( bool forReading , int fd , long long now ,
			       long niceness ) {
	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("callCallbacks_ass start");
	//if ( fd != 1024 ) {
	//if (forReading) fprintf(stderr,"got read  sig on fd=%li\n",(long)fd);
	//else            fprintf(stderr,"got write sig on fd=%li\n",(long)fd);
	//}
	// save the g_errno to send to all callbacks
	int saved_errno = g_errno;
	// get the first Slot in the chain that is waiting on this fd
	Slot *s ;
	if ( forReading ) s = m_readSlots  [ fd ];
	else              s = m_writeSlots [ fd ];
	// ensure we called something
	long numCalled = 0;

	// a hack fix
	if ( niceness == -1 && m_inQuickPoll ) niceness = 0;

	// . now call all the callbacks
	// . most will re-register themselves (i.e. call registerCallback...()
	//long long startTime = gettimeofdayInMilliseconds();
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
		//	log("Loop::callCallbacks_ass: for fd=%li state=%lu",
		//	    fd,(long)s->m_state);
		// do the callback
		//long address = 0;
		// 		unsigned long long profilerStart,profilerEnd;
		// 		unsigned long long statStart, statEnd;
		/*
		if(g_conf.m_profilingEnabled){
			address=(long)s->m_callback;
			g_profiler.startTimer(address, 
					      __PRETTY_FUNCTION__);
		      //profilerStart=gettimeofdayInMillisecondsLocal();
		      //statStart = gettimeofdayInMilliseconds();
		}
		*/
		//startBlockedCpuTimer();

		// log it now
		if (  g_conf.m_logDebugLoop )
			log(LOG_DEBUG,"loop: enter fd callback fd=%li "
			    "nice=%li",(long)fd,(long)s->m_niceness);

		// sanity check. -1 no longer supported
		if ( s->m_niceness < 0 ) { char *xx=NULL;*xx=0; }

		// save it
		long saved = g_niceness;
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
			log(LOG_DEBUG,"loop: exit fd callback fd=%li "
			    "nice=%li", (long)fd,(long)s->m_niceness);

		/*
		if(g_conf.m_profilingEnabled){
			//profilerEnd =gettimeofdayInMillisecondsLocal();
			if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
				log(LOG_WARN,"admin: Couldn't add the fn %li",
				    (long)address);
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
// 	long long now2 = gettimeofdayInMilliseconds();
// 	long long took = now2 - startTime;

// 	if(g_conf.m_profilingEnabled && took > 10) {	
// 		g_stats.addStat_r ( 0      , 
// 				    startTime, 
// 				    now2,
// 				    0 ,
// 				    STAT_GENERIC,
// 				    __PRETTY_FUNCTION__,__LINE__);


// 		if(g_conf.m_sequentialProfiling) {
// 			log(LOG_TIMING, 
// 			    "admin: loop time to do %li callbacks: %lli ms", 
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


	// set all callbacks to NULL so we know they're empty
	for ( long i = 0 ; i < MAX_NUM_FDS+2 ; i++ ) {
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

bool Loop::init ( ) {
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
	m_slots = (Slot *) mmalloc ( MAX_SLOTS * (long)sizeof(Slot) , "Loop" );
	if ( ! m_slots ) return false;
	// log it
	log(LOG_INIT,"loop: Allocated %li bytes for %li callbacks.",
	     MAX_SLOTS * (long)sizeof(Slot),(long)MAX_SLOTS);
	// init link list ptr
	for ( long i = 0 ; i < MAX_SLOTS - 1 ; i++ ) {
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
	//log("GB_SIGRTMIN=%li", GB_SIGRTMIN );
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
#ifndef _VALGRIND_
	sigaddset   ( &sigs , GB_SIGRTMIN     );
#endif
	sigaddset   ( &sigs , GB_SIGRTMIN + 1 );
	sigaddset   ( &sigs , GB_SIGRTMIN + 2 );
	sigaddset   ( &sigs , GB_SIGRTMIN + 3 );
	sigaddset   ( &sigs , SIGCHLD      );
	// . block on any signals in this set (in addition to current sigs)
	// . use SIG_UNBLOCK to remove signals from block list
	// . this returns -1 and sets g_errno on error
	if ( sigprocmask ( SIG_BLOCK, &sigs, 0 ) < 0 ) {
		g_errno = errno;
		return log("loop: sigprocmask: %s.",strerror(g_errno));
	}
	// . we turn this signal on/off to turn interrupts off/on
	// . clear all signals from the set	
	//sigemptyset ( &m_sigrtmin );
	// tmp debug hack, so we don't have real time signals now...
	//sigaddset   ( &m_sigrtmin, GB_SIGRTMIN );
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
#ifndef _VALGRIND_
	if ( sigaction ( GB_SIGRTMIN, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction GB_SIGRTMIN: %s.", mstrerror(errno));
#endif

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
	if ( sigaction ( SIGIO, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGIO: %s.", mstrerror(errno));


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


	// if the UPS is about to go off it sends a SIGPWR
	sa.sa_sigaction = sigpwrHandler;
	if ( sigaction ( SIGPWR, &sa, 0 ) < 0 ) g_errno = errno;


	//now set up our alarm for quickpoll
	m_quickInterrupt.it_value.tv_sec = 0;
	m_quickInterrupt.it_value.tv_usec = QUICKPOLL_INTERVAL * 1000;
	m_quickInterrupt.it_interval.tv_sec = 0;
	m_quickInterrupt.it_interval.tv_usec = QUICKPOLL_INTERVAL * 1000;
 	m_noInterrupt.it_value.tv_sec = 0;
 	m_noInterrupt.it_value.tv_usec = 0;
 	m_noInterrupt.it_interval.tv_sec = 0;
 	m_noInterrupt.it_interval.tv_usec = 0;

	// set the interrupts to off for now
	disableTimer();

	//setitimer(ITIMER_REAL, &m_quickInterrupt, NULL);
	//setitimer(ITIMER_VIRTUAL, &m_quickInterrupt, NULL);

	sa.sa_sigaction = sigalrmHandler;
	//if ( sigaction ( SIGALRM, &sa, 0 ) < 0 ) g_errno = errno;
	if ( sigaction ( SIGVTALRM, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGBUS: %s.", mstrerror(errno));


	if ( g_errno ) return log("loop: sigaction: %s.", mstrerror(errno));

	// success
	return true;
}

// TODO: if we get a segfault while saving, what then?
void sigpwrHandler ( int x , siginfo_t *info , void *y ) {
	// let main process know to shutdown
	g_loop.m_shutdown = 3;
}

// TODO: if we get a segfault while saving, what then?
void sigbadHandler ( int x , siginfo_t *info , void *y ) {

	// thread should set it errno to 0x7fffffff which means that
	// Threads.cpp should not look for its ThreadEntry::m_isDone flag
	// to be set before calling waitpid() on it
	if ( g_threads.amThread() ) errno = 0x7fffffff;

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
	sigaction ( SIGALRM, &sa, 0 ) ;
	// if we've already been here, or don't need to be, then bail
	if ( g_loop.m_shutdown ) {
		log("loop: sigbadhandler. shutdown already called.");
		return;
	}
	// if we're a thread, let main process know to shutdown
	g_loop.m_shutdown = 2;
	log("loop: sigbadhandler. trying to save now. mode=%li",
	    (long)g_process.m_mode);
	// . this will save all Rdb's 
	// . if "urgent" is true it will dump core
	// . if "urgent" is true it won't broadcast its shutdown to all hosts
	//#ifndef NO_MAIN
	//	mainShutdown ( true ); // urgent?
	//#endif
	g_process.shutdown ( true );
}


void sigalrmHandler ( int x , siginfo_t *info , void *y ) {

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
		// panic if hogging
		if ( logIt ) {
			if ( g_callSlot->m_callback )
				log("loop: msg type 0x%hhx reply callback "
				    "hogging cpu for %li ticks", 
				    g_callSlot->m_msgType,
				    g_transIdCount);
			else
				log("loop: msg type 0x%hhx handler "
				    "hogging cpu for %li ticks", 
				    g_callSlot->m_msgType,
				    g_transIdCount);
		}
	}

	g_nowApprox += QUICKPOLL_INTERVAL; // 10 ms
	// stats
	g_numAlarms++;

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
		// i guess sometimes niceness 1 things call niceness 0 things?
		log("loop: crap crap crap!!!");
		//char *xx=NULL;*xx=0; }
	}
	// basically ignore this alarm if already in a quickpoll
	if ( g_loop.m_inQuickPoll ) return;

	if ( ! g_conf.m_useQuickpoll ) return;

	g_loop.m_needsToQuickPoll = true;

	//fprintf(stderr,"missed=%li\n",g_missedQuickPolls);

	// another missed quickpoll
	if ( g_niceness == 1 ) g_missedQuickPolls++;
	// reset if niceness is 0
	else if ( g_niceness == 0 ) g_missedQuickPolls = 0;

	// if we missed to many, then dump core
	if ( g_niceness == 1 && g_missedQuickPolls >= 4 ) {
		log("loop: missed quickpoll");
		// seems to core a lot in gbcompress() we need to
		// put a quickpoll into zlib deflate() or
		// deflat_slot() or logest_match() function
		// for now do not dump core --- re-enable this later
		// mdw TODO
		//char *xx=NULL;*xx=0; 
	}

	// . see where we are in the code
	// . for computing cpu usage
	// . if idling we will be in sigtimedwait() at the lowest level
	Host *h = g_hostdb.m_myHost;
	// . i guess this means we were doing something... (otherwise idle)
	// . this is KINDA like a 100 point sample, but it has crazy decay
	//   logic built into it
	if (h) {
		if ( ! g_inWaitState )
			h->m_cpuUsage = .99 * h->m_cpuUsage + .01 * 100;
		else
			h->m_cpuUsage = .99 * h->m_cpuUsage + .01 * 000;
	}

	// if it has been a while since heartbeat (> 10000ms) dump core so
	// we can see where the process was... that is a missed quick poll?
	if ( g_process.m_lastHeartbeatApprox == 0 ) return;
	if ( g_conf.m_maxHeartbeatDelay <= 0 ) return;
	if ( g_nowApprox - g_process.m_lastHeartbeatApprox > 
	     g_conf.m_maxHeartbeatDelay ) {
		logf(LOG_DEBUG,"gb: CPU seems blocked. Forcing core.");
		//char *xx=NULL; *xx=0; 
	}

	//logf(LOG_DEBUG, "xxx now: %lli! approx: %lli", g_now, g_nowApprox);
}


// shit, we can't make this realtime!! RdbClose() cannot be called by a
// real time sig handler
void sighupHandler ( int x , siginfo_t *info , void *y ) {
	// let main process know to shutdown
	g_loop.m_shutdown = 1;
}

// . keep a timestamp for the last time we called the sleep callbacks
// . we have to call those every 1 second
long long s_lastTime = 0;

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
#ifndef _VALGRIND_
	sigaddset ( &sigs0, GB_SIGRTMIN     );
#endif
	sigaddset ( &sigs0, GB_SIGRTMIN + 1 );
	sigaddset ( &sigs0, GB_SIGRTMIN + 2 );
	sigaddset ( &sigs0, GB_SIGRTMIN + 3 );
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
	siginfo_t info ;
	long sigNum ; //= sigwaitinfo ( &sigs1, &info );
#endif
	s_lastTime = 0;

	// . allow us to be interrupted
	// . UNBLOCKs GB_SIGRTMIN
	// . makes g_udpServer2 quite jumpy
	g_loop.interruptsOn();

	enableTimer();

	long long elapsed; 

	// . now loop forever waiting for signals
	// . but every second check for timer-based events
	do {

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
		long j;
		for ( long k = 0 ; k < 2000000000 ; k++ ) {
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
				//log (0,"Thread is saving and shutting down urgently.");
				//while ( 1 == 1 ) sleep (50000);
				log("loop: Resuming despite thread crash.");
				m_shutdown = 0;
				continue;
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

		//g_udpServer2.sendPoll_ass(true,g_now);
		//g_udpServer2.process_ass ( g_now );
		// MDW: see if this works without this junk, if not then
		// put it back in
		g_udpServer.sendPoll_ass (true,g_now);
		g_udpServer.process_ass ( g_now );
		// and dns now too
		g_dns.m_udpServer.sendPoll_ass(true,g_now);
		g_dns.m_udpServer.process_ass ( g_now );

		// if there was a high niceness  http request within a 
		// quickpoll, we stored it and now we'll call it here.
		//g_httpServer.callQueuedPages();

		//g_udpServer.printState ( );

		if ( g_someAreQueued ) {
			// assume none are queued now, we may get interrupted
			// and it may get set back to true
			g_someAreQueued = false;
			//g_udpServer2.makeCallbacks_ass (  0 );
			//g_udpServer2.makeCallbacks_ass (  1 );
		}

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
		g_threads.timedCleanUp(4, MAX_NICENESS ) ; // 4 ms

		// do it anyway
		doPoll();

		while ( m_needToPoll ) doPoll();

		long elapsed = g_now - s_lastTime;
		// if someone changed the system clock on us, this could be negative
		// so fix it! otherwise, times may NEVER get called in our lifetime
		if ( elapsed < 0 ) elapsed = m_minTick;
		// call this every (about) 1 second
		if ( elapsed >= m_minTick ) {
			// MAX_NUM_FDS is the fd for sleep callbacks
			callCallbacks_ass ( true , MAX_NUM_FDS , g_now );
			// note the last time we called them
			//g_now = gettimeofdayInMilliseconds();
			s_lastTime = g_now;
		}

#ifndef _POLLONLY_

		// hack
		//char buffer[100];
		//if ( recv(27,buffer,99,MSG_PEEK|MSG_DONTWAIT) == 0 ) {
		//	logf(LOG_DEBUG,"CLOSED CLOSED!!");
		//}
		//g_errno = 0;

		//check for pending signals, return right away if none.
		//then we'll do the low priority stuff while we were 
		//supposed to be sleeping.
		g_inWaitState = true;
		sigNum = sigtimedwait (&sigs0, &info, s_sigWaitTimePtr ) ;

		// if no signal, we just waited 20 ms and nothing happened
		if ( sigNum == -1 )
			sigalrmHandler( 0,&info,NULL);
		//logf(LOG_DEBUG,"loop: sigNum=%li signo=%li alrm=%li",
		//     (long)sigNum,info.si_signo,(long)SIGVTALRM);
		// no longer in a wait state...
		g_inWaitState = false;

		if ( sigNum <  0 ) {
			if ( errno == EAGAIN || errno == EINTR ||
					errno == EILSEQ || errno == 0 ) { 
				sigNum = 0; 
				errno = 0;
			}
			else if ( errno != ENOMEM ) {
				log("loop: sigtimedwait(): %s.",strerror(errno));
				continue;
			}
		}
		if ( sigNum == 0 ) {
			//no signals pending, try to take care of anything left undone:

			long long startTime = gettimeofdayInMillisecondsLocal();
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
		else {
			if   ( info.si_code == SIGIO ) {
				log("loop: got sigio");
				m_needToPoll = true;
			}
			// handle the signal
			else sigHandler_r ( 0 , &info , NULL );
		}
#endif
	} while (1);
	


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
		g_threads.m_needsCleanup = false;
		// check thread queue for any threads that completed
		// so we can call their callbacks and remove them
		g_threads.cleanUp ( 0 , 1000/*max niceness*/);
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
	//for ( long long i = 0 ; i < 1000000000000LL ; i++ );
	// . we use g_now in UdpServer.cpp and should make it
	//   as accurate as possible
	// . but it's main use is because gettimeofday() is not async sig safe
	g_now = gettimeofdayInMilliseconds();
	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("Loop: processing signal");
	// call sighandler on other queued signals and continue looping
	//log("Loop::runLoop:got queued signum=%li",sigNum);
	// was it a SIGIO?
	if   ( info.si_code == SIGIO ) doPoll();
	// handle the signal
	else                           sigHandler_r ( 0 , &info , NULL );
#endif
	// don't call any time handlers until no more signals waiting
	//goto subloop;
	// we need to make g_now as accurate as possible for hot UdpServer...
	goto loop;
	// make compiler happy
	return 0;
}

// . the kernel sends a SIGIO signal when the sig queue overflows
// . we resort to polling the fd's when that happens
void sigioHandler ( int x , siginfo_t *info , void *y ) {
	// set the m_needToPoll flag
	g_loop.m_needToPoll = true;
	return;
}

//--- TODO: flush the signal queue after polling until done
//--- are we getting stale signals resolved by flush so we get
//--- read event on a socket that isnt in read mode???
// TODO: set signal handler to SIG_DFL to prevent signals from queuing up now
// . this handles high priority fds first (lowest niceness)
void Loop::doPoll ( ) {
	// set time
	g_now = gettimeofdayInMilliseconds();
	// debug msg
	//log("**************** GOT SIGIO *************");
	// . turn it off here so it can be turned on again after we've
	//   called select() so we don't lose any fd events through the cracks
	// . some callbacks we call make trigger another SIGIO, but if they
	//   fail they should set Loop::g_needToPoll to true
	m_needToPoll = false; 
	// debug msg
	//if ( g_conf.m_logDebugLoop ) log(LOG_DEBUG,"loop: Entered doPoll.");
	log(LOG_DEBUG,"loop: Entered doPoll.");
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


	bool processedOne;
	long n;
	//	long repeats = 0;
	// skipLowerPriorities:
	// descriptor bits for calling select()
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
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		if ( m_readSlots [i] ) {
			FD_SET ( i , &readfds   );
			FD_SET ( i , &exceptfds );
		}
		if ( m_writeSlots[i] ) {
			FD_SET ( i , &writefds );
			FD_SET ( i , &exceptfds );
		}
	}
 again:
	// poll the fd's searching for socket closes
	n = select (MAX_NUM_FDS, &readfds, &writefds, &exceptfds, &v);
	if ( n < 0 ) { 
		// valgrind
		if ( errno == EINTR ) goto again;
		g_errno = errno;
		log("loop: select: %s.",strerror(g_errno));
		return;
	}
	// debug msg
	if ( g_conf.m_logDebugLoop) 
		log(LOG_DEBUG,"loop: Got %li fds waiting.",n);
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
	processedOne = false;

	// a Slot ptr
	Slot *s;
	g_now = gettimeofdayInMilliseconds();
	/*
	// call g_udpServer sig handlers for niceness -1 here
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
		// continue if not set for reading
		if ( ! FD_ISSET ( i , &readfds ) ) continue;
		// if niceness is not -1, handle it below
		s = m_readSlots  [ i  ];
		if ( s && s->m_niceness != -1 ) continue;
		callCallbacks_ass (true,i, g_now); // for reading = true
		processedOne = true;
	}
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {
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

	// . call the callback for each fd we got
	// . only call callbacks for fds that have a nice of 0 here
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
		// continue if not set for reading
		if ( ! FD_ISSET ( i , &readfds ) ) continue;
		// if niceness is not 0, handle it below
		s = m_readSlots  [ i /*fd*/ ];
		if ( s && s->m_niceness != 0 ) continue;
		callCallbacks_ass (true/*forReading?*/,i, g_now);
		processedOne = true;
	}
	// only call callbacks for fds that have a nice of 0 here
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		if ( ! FD_ISSET ( i , &writefds ) ) continue;
		// if niceness is not 0, handle it below
		s = m_writeSlots  [ i /*fd*/ ];
		if ( s && s->m_niceness != 0 ) continue;
		callCallbacks_ass (false/*forReading?*/,i, g_now);
		processedOne = true;
	}
	// handle returned threads for niceness 0
	g_threads.timedCleanUp(3,0); // 3 ms

#if 0
	if(processedOne && repeats < QUERYPRIORITYWEIGHT) {
		//m_needToPoll = true; 
		repeats++;
		goto skipLowerPriorities;
	}

// 	log(LOG_WARN, 
// 	"Loop: repeated %li times before moving to lower priority threads", 
// 		repeats);
#endif

	// handle low priority fds here
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
		// continue if not set for reading
		if ( ! FD_ISSET ( i , &readfds ) ) continue;
		// if niceness is 0, we already handled it above
		s = m_readSlots  [ i /*fd*/ ];
		if ( s && s->m_niceness <= 0 ) continue;
		callCallbacks_ass (true/*forReading?*/,i, g_now);
	}
	// only call callbacks for fds that have a nice of 0 here
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		if ( ! FD_ISSET ( i , &writefds ) ) continue;
		// if niceness is 0, we already handled it above
		s = m_writeSlots  [ i /*fd*/ ];
		if ( s && s->m_niceness <= 0 ) continue;
		callCallbacks_ass (false/*forReading?*/,i, g_now);
	}
	// handle returned threads for all other nicenesses
	g_threads.timedCleanUp(4,MAX_NICENESS); // 4 ms

	// set time
	g_now = gettimeofdayInMilliseconds();
	// call sleepers if they need it
	// call this every (about) 1 second
	{
	long elapsed = g_now - s_lastTime;
	// if someone changed the system clock on us, this could be negative
	// so fix it! otherwise, times may NEVER get called in our lifetime
	if ( elapsed < 0 ) elapsed = m_minTick;
	if ( elapsed >= m_minTick ) {
		// MAX_NUM_FDS is the fd for sleep callbacks
		callCallbacks_ass ( true , MAX_NUM_FDS , g_now );
		// note the last time we called them
		s_lastTime = g_now;
		// handle returned threads for all other nicenesses
		g_threads.timedCleanUp(4,MAX_NICENESS); // 4 ms
	}
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
#ifndef _VALGRIND_
	sigaddset   ( &rtmin, GB_SIGRTMIN );
#endif
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
#ifndef _VALGRIND_
	sigaddset   ( &rtmin, GB_SIGRTMIN );
#endif
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
	g_inSigHandler = true;
	// debug msg
	//if ( g_conf.m_timingDebugEnabled ) 
	//	log("sigHandlerRT entered");
	// save errno
	long old_gerrno = g_errno;
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

// come here when we get a GB_SIGRTMIN+X signal
void sigHandler_r ( int x , siginfo_t *info , void *v ) {
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
	//#define POLLIN      0x0001    /* There is data to read */
        //#define POLLPRI     0x0002    /* There is urgent data to read */
	//#define POLLOUT     0x0004    /* Writing now will not block */
	//#define POLLERR     0x0008    /* Error condition */
	//#define POLLHUP     0x0010    /* Hung up */
	//#define POLLNVAL    0x0020    /* Invalid request: fd not open */
	int band = info->si_band;  
	/*
	fprintf(stderr,"got fd         = %i\n", fd   );
	fprintf(stderr,"got band       = %i\n", band );
	fprintf(stderr,"band & POLLIN  = %i\n", band & POLLIN  );
	fprintf(stderr,"band & POLLPRI = %i\n", band & POLLPRI );
	fprintf(stderr,"band & POLLOUT = %i\n", band & POLLOUT );
	fprintf(stderr,"band & POLLERR = %i\n", band & POLLERR );
	fprintf(stderr,"band & POLLHUP = %i\n", band & POLLHUP );
	*/
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

// 		//		g_threads.cleanUp ( (ThreadEntry *)val , x/*max niceness*/);
// 		g_threads.cleanUp ( (ThreadEntry *)val , 1000/*max niceness*/);

// 		// launch any threads in waiting since this sig was 
// 		// from a terminating one
// 		g_threads.launchThreads();
	}

	// if we just needed a cleanup
	if ( info->si_signo == SIGCHLD ) return;

	// if we don't got a signal for an fd, just a sigqueue() call, bail now
	if ( info->si_code == SI_QUEUE ) return;

	// . call the appropriate handler(s)
	// . TODO: bitch if no callback to handle the read!!!!!!!
	// . NOTE: when it's connected it sets both POLLIN and POLLOUT
	// . NOTE: or when a socket is trying to connect to it if it's listener
	//if      ( band & (POLLIN | POLLOUT) == (POLLIN | POLLOUT) ) 
	// g_loop.callCallbacks_ass ( true/*forReading?*/ , fd );
	if ( band & POLLIN  ) {
		//log("Loop: read %lli",gettimeofdayInMilliseconds());
		g_loop.callCallbacks_ass ( true  , fd ); 
	}
	else if ( band & POLLPRI ) {
		//log("Loop: read %lli",gettimeofdayInMilliseconds());
		g_loop.callCallbacks_ass ( true  , fd ) ;
	}
	else if ( band & POLLOUT ) {
		//log("Loop: read %lli",gettimeofdayInMilliseconds());
		g_loop.callCallbacks_ass ( false , fd ); 
	}
	//else if ( band & POLLERR )  
	//g_loop.callCallbacks_ass ( false , fd ); 
	// this happens if the socket closes abruptly
	// or out of band data, etc... see "man 2 poll" for more info
	else { 
		// i see these all the time for fd == 0, so don't print it
		if ( fd != 0 ) 
			log(LOG_INFO,"loop: Received hangup on fd=%i.",fd);
		g_errno = ESOCKETCLOSED; 
		g_loop.callCallbacks_ass ( false , fd );
	}
}


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


void Loop::quickPoll(long niceness, const char* caller, long lineno) {
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
		//if(g_conf.m_quickpollCoreOnError) { 
		char*xx=NULL;*xx=0;
		//		}
		//		else return;
	}
	long long now = g_now;
	//long long took = now - m_lastPollTime;
	long long now2 = g_now;
	long gerrno = g_errno;

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
				    "after %lli ms", caller, took);
			}
		}
	}
	*/

	g_numQuickPolls++;

	m_inQuickPoll = true;

	//g_udpServer2.process_ass ( g_now , 0 );
 	g_udpServer.process_ass  ( g_now , 0 );
	g_threads.timedCleanUp( 100 , 0 ); // ms ms, niceness 0
	long n;
	
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
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {
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

	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {	
		// continue if not set for reading
		if ( ! FD_ISSET ( i , &readfds ) ) continue;
		// if niceness is not -1, handle it below
		s = m_readSlots  [ i /*fd*/ ];
		// now we have niceness 2 if Sections.cpp
		if ( s && s->m_niceness >= niceness ) continue;
		callCallbacks_ass (true/*forReading?*/,i, now);
		// sanity check
		if ( g_niceness > niceness ) { char*xx=NULL;*xx=0; }
	}
	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		if ( ! FD_ISSET ( i , &writefds ) ) continue;
		// if niceness is not -1, handle it below
		s = m_writeSlots  [ i /*fd*/ ];
		// now we have niceness 2 if Sections.cpp
		if ( s && s->m_niceness >= niceness ) continue;
		callCallbacks_ass (false/*forReading?*/,i, now);
		// sanity check
		if ( g_niceness > niceness ) { char*xx=NULL;*xx=0; }
	}


	// now we can have niceness 0 dns slots because of the niceness
	// conversion algorithm...
	g_dns.m_udpServer.sendPoll_ass(true,g_now);
	g_dns.m_udpServer.process_ass ( g_now );
	g_dns.m_udpServer.makeCallbacks_ass(0);

 theend:

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
	//log(LOG_WARN, "xx quickpolled took %lli, waited %lli from %s", 
	//    now2 - now, now - m_lastPollTime, caller);
	m_lastPollTime = now2;
	m_inQuickPoll = false;
	m_needsToQuickPoll = false;
	m_canQuickPoll = true;
	g_errno = gerrno;

	// reset this again
	g_missedQuickPolls = 0;
}


void Loop::canQuickPoll(long niceness) {
	if(niceness && !m_shutdown) m_canQuickPoll = true;
	else         m_canQuickPoll = false;
}

void Loop::disableTimer() {
	//logf(LOG_WARN,"xxx disabling");
	m_canQuickPoll = false;
	setitimer(ITIMER_VIRTUAL, &m_noInterrupt, NULL);
}


void Loop::enableTimer() {
	m_canQuickPoll = true;
	//	logf(LOG_WARN, "xxx enabling");
	setitimer(ITIMER_VIRTUAL, &m_quickInterrupt, NULL);
	//setitimer(ITIMER_REAL, &m_quickInterrupt, NULL);
}




//calling with a 0 niceness will turn off the timer interrupt
void Loop::setitimerInterval(long niceness) {
	if(niceness) {
		setitimer(ITIMER_VIRTUAL, &m_quickInterrupt, NULL);
		m_canQuickPoll = true;
	}
	else {
		setitimer(ITIMER_VIRTUAL, &m_noInterrupt, NULL);
		m_canQuickPoll = false;
	}
}
