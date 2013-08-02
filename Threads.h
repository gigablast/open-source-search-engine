// Matt Wells, Copyright June 2002


// . this calls let's us use threads

#ifndef _GBTHREADS_
#define _GBTHREADS_

#define MAX_THREAD_QUEUES 7

#include <sys/types.h>  // pid_t

// user-defined thread types
#define DISK_THREAD      0
#define MERGE_THREAD     1
#define INTERSECT_THREAD 2
#define FILTER_THREAD    3
#define SAVETREE_THREAD  4
#define UNLINK_THREAD    5
#define GENERIC_THREAD   6
#define GB_SIGRTMIN	 (SIGRTMIN+4)
#define MAX_NICENESS     2
// . a ThreadQueue has a list of thread entries
// . each thread entry represents a thread in progress or waiting to be created
class ThreadEntry {

 public:
	long         m_niceness                 ;
	void        (* m_callback)(void *state,class ThreadEntry *) ;
	void         *m_state                   ;
	// returns a void * :
	void        *(* m_startRoutine)(void *,class ThreadEntry *) ; 
	pid_t        m_pid                      ; // process id for waitpid()
	bool         m_isOccupied               ; // is thread waiting/going?
	bool         m_isLaunched               ; // has it been launched?
	bool         m_isDone                   ; // is it done running?
	bool         m_readyForBail             ; // BigFile.cpp stuck reads
	char        *m_allocBuf                 ; // BigFile.cpp stuck reads
	long         m_allocSize                ; // BigFile.cpp stuck reads
	long         m_errno                    ; // BigFile.cpp stuck reads
	long         m_bytesToGo                ; // BigFile.cpp stuck reads
	long long    m_queuedTime               ; // when call() was called
	long long    m_launchedTime             ; // when thread was launched
	long long    m_preExitTime              ; // when thread was about done
	long long    m_exitTime                 ; // when thread was done
	char         m_qnum                     ; // what thread queue we r in
	char         m_doWrite                  ; // BigFile.cpp stuck reads
	char         m_isCancelled              ; // got cancel sig?

	char        *m_stack                    ;
	long         m_stackSize                ;
	long         m_si                       ; // s_stackPtrs[i] = m_stack

	pthread_t m_joinTid;
};

//#define MAX_THREAD_ENTRIES 1024

// our Thread class has one ThreadQueue per thread type
class ThreadQueue {

 public:
	// what type of threads are in this queue (used-defined)?
	char         m_threadType;
	// how many threads have been launched total over time?
	long long    m_launched;
	// how many threads have returned total over time?
	long long    m_returned;
	// how many can we launch at one time?
	long         m_maxLaunched;
	// how many are in the queue now?
	//long         m_entriesUsed;
	// m_top is the first unused entry with nothing used above it
	long         m_top;
	// the list of entries in this queue
	//ThreadEntry  m_entries [ MAX_THREAD_ENTRIES ];
	ThreadEntry *m_entries ;
	long         m_entriesSize;
	long         m_maxEntries;

	// counts the high/low priority (niceness <= 0) threads
	long long   m_hiLaunched;
	long long   m_hiReturned;
	long long   m_mdLaunched;
	long long   m_mdReturned;
	long long   m_loLaunched;
	long long   m_loReturned;
	// disk writing
	long long   m_writesLaunched;
	long long   m_writesReturned;
	// now for disk threads we partition by the read size
	long long   m_hiLaunchedBig;
	long long   m_hiReturnedBig;
	long long   m_mdLaunchedBig;
	long long   m_mdReturnedBig;
	long long   m_loLaunchedBig;
	long long   m_loReturnedBig;
	long long   m_hiLaunchedMed;
	long long   m_hiReturnedMed;
	long long   m_mdLaunchedMed;
	long long   m_mdReturnedMed;
	long long   m_loLaunchedMed;
	long long   m_loReturnedMed;
	long long   m_hiLaunchedSma;
	long long   m_hiReturnedSma;
	long long   m_mdLaunchedSma;
	long long   m_mdReturnedSma;
	long long   m_loLaunchedSma;
	long long   m_loReturnedSma;

	// init
	bool         init (char threadType, long maxThreads, long maxEntries);

	ThreadQueue();
	void reset();

	long getNumThreadsOutOrQueued();

	// . for adding an entry
	// . returns false and sets errno on error
	ThreadEntry *addEntry ( long   niceness                     ,
				void  *state                        , 
				void  (* callback    )(void *state,
						       class ThreadEntry *t) ,
				void *(* startRoutine)(void *state,
						       class ThreadEntry *t) );
	// calls the callback of threads that are done (exited) and then
	// removes them from the queue
	bool         cleanUp      ( ThreadEntry *tt , long maxNiceness );
	bool         timedCleanUp ( long maxNiceness );

	void bailOnReads ();
	bool isHittingFile ( class BigFile *bf );

	// . launch a thread from our queue
	// . returns false and sets errno on error
	bool         launchThread ( ThreadEntry *te = NULL );

	void print ( ) ;

	// these are called by g_udpServer2, the high priority udp server
	void suspendLowPriorityThreads();
	void resumeLowPriorityThreads();

	// this is true if low priority threads are temporarily suspended
	bool m_isLowPrioritySuspended ;

	// . cancel running low priority threads
	// . called by suspendLowPriorityThreads() when first called
	//void cancelLowPriorityThreads();

	// return m_threadType as a NULL-terminated string
	const char *getThreadType () ;

	void removeThreads ( class BigFile *bf ) ;
};

// this Threads class has a list of ThreadQueues, 1 per thread type
class Threads {

 public:

	Threads() { m_numQueues = 0; };

	// returns false and sets errno on error, true otherwise
	bool init();

	long getStack ( ) ;
	void returnStack ( long si );
	void reset ( ) ;

	// . we restrict the # of threads based on their type
	// . for instance we allow up to say, 5, disk i/o threads,
	//   but only 1 thread for doing IndexList intersections
	// . threads with higher niceness values always wait for any thread
	//   with lower niceness to complete
	// . returns false and sets errno on error, true otherwise
	bool registerType ( char type , long maxThreads , long maxEntries );

	// is the caller a thread?
	bool amThread ( );

	void printQueue ( long q ) { m_threadQueues[q].print(); };
	void printState();


	// disable all threads... no more will be created, those in queues
	// will never be spawned
	void disableThreads () { m_disabled = true;  };
	void enableThreads  () { m_disabled = false; };
	bool areThreadsDisabled() { return m_disabled; };
	bool areThreadsEnabled () { return ! m_disabled; };

	// . returns false and sets errno if thread launch failed
	// . returns true on success
	// . when thread is done a signal will be put on the g_loop's
	//   sigqueue to call "callback" with "state" as the parameter
	// . niceness deteremines the niceness of this signal as well as
	//   the thread's priority
	bool call ( char   type                               ,
		    long   niceness                           ,
		    void  *state                              , 
		    void  (* threadDoneCallback)(void *state,
						 class ThreadEntry *t) ,
		    void *(* startRoutine      )(void *state,
						 class ThreadEntry *t) );

	bool call ( char type , long niceness ,
		    void *state  , void (* callback)(void *state,
						     class ThreadEntry *t) );

	// try to launch threads waiting to be launched in any queue
	long launchThreads ();

	// call cleanUp() for each thread queue
	bool cleanUp ( ThreadEntry *tt , long maxNiceness ) ;

	void bailOnReads ();
	bool isHittingFile ( class BigFile *bf ) ;

	//calls callbacks and launches all threads
	long timedCleanUp (long maxTime, long niceness );//= MAX_NICENESS);

	// these are called by g_udpServer2, the high priority udp server
	void suspendLowPriorityThreads();
	void resumeLowPriorityThreads();

	// cancels low priority threads running in each ThreadQueue
	//void cancelLowPriorityThreads();

	// . used by Msg34 for computing the disk load
	// . gets the number of disk threads (seeks) and total bytes to read
	// . ignores disk threads that are too nice (over maxNiceness)
	long getDiskThreadLoad ( long maxNiceness , long *totalToRead ) ;

	ThreadQueue* getThreadQueues() { return &m_threadQueues[0];}
	long      getNumThreadQueues() { return m_numQueues; }

	// used by UdpServer to see if it should call a low priority callback
	long getNumActiveHighPriorityCpuThreads() ;
	// all high priority threads...
	long getNumActiveHighPriorityThreads() ;

	long getNumThreadsOutOrQueued();

	// counts the high/low priority (niceness <= 0) threads
	//long long   m_hiLaunched;
	//long long   m_hiReturned;
	//long long   m_loLaunched;
	//long long   m_loReturned;

	bool m_needsCleanup;
	//bool m_needBottom;


	// private:

	// . allow up to MAX_THREAD_QUEUES different thread types for now
	// . types are user-defined numbers
	// . each type has a corresponding thread queue
	// . when a thread is done we place a signal on g_loop's sigqueue so
	//   that it will call m_callback w/ m_state 
	ThreadQueue m_threadQueues  [ MAX_THREAD_QUEUES ];
	long        m_numQueues;

	bool        m_disabled;
};

extern class Threads g_threads;

void ohcrap ( void *state , class ThreadEntry *t ) ;

#endif
