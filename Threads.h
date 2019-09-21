// Matt Wells, Copyright June 2002


// . this calls let's us use threads

#ifndef _GBTHREADS_
#define _GBTHREADS_

#define MAX_THREAD_QUEUES 7

#include <sys/types.h>  // pid_t

// this also limit the maximum number of outstanding (live) threads
#define MAX_STACKS 20
// try going up to 40, we use about 2MB per stack... so this is 80MB
//#define MAX_STACKS 40

// if we are a thread this gets the threadid, otherwise, the main process id
//pid_t getpidtid();
// on 64-bit architectures pthread_t is 64 bits and pid_t is still 32 bits
pthread_t getpidtid();

// user-defined thread types
#define DISK_THREAD      0
#define MERGE_THREAD     1
#define INTERSECT_THREAD 2
#define FILTER_THREAD    3
#define SAVETREE_THREAD  4
#define UNLINK_THREAD    5
#define GENERIC_THREAD   6
//#define SSLACCEPT_THREAD 7
//#define GB_SIGRTMIN	 (SIGRTMIN+4)
#define MAX_NICENESS     2
// . a ThreadQueue has a list of thread entries
// . each thread entry represents a thread in progress or waiting to be created
class ThreadEntry {

 public:
	int32_t         m_niceness                 ;
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
	int32_t         m_allocSize                ; // BigFile.cpp stuck reads
	int32_t         m_errno                    ; // BigFile.cpp stuck reads
	int32_t         m_bytesToGo                ; // BigFile.cpp stuck reads
	int64_t    m_queuedTime               ; // when call() was called
	int64_t    m_launchedTime             ; // when thread was launched
	int64_t    m_preExitTime              ; // when thread was about done
	int64_t    m_exitTime                 ; // when thread was done
	char         m_qnum                     ; // what thread queue we r in
	char         m_doWrite                  ; // BigFile.cpp stuck reads
	char         m_isCancelled              ; // got cancel sig?

	char        *m_stack                    ;
	int32_t         m_stackSize                ;
	int32_t         m_si                       ; // s_stackPtrs[i] = m_stack

	bool      m_needsJoin;
	pthread_t m_joinTid;

	class ThreadEntry *m_nextLink;
	class ThreadEntry *m_prevLink;

	// the waiting linked list we came from
	ThreadEntry **m_bestHeadPtr;
	ThreadEntry **m_bestTailPtr;
};

//#define MAX_THREAD_ENTRIES 1024

// our Thread class has one ThreadQueue per thread type
class ThreadQueue {

 public:
	// what type of threads are in this queue (used-defined)?
	char         m_threadType;
	// how many threads have been launched total over time?
	int64_t    m_launched;
	// how many threads have returned total over time?
	int64_t    m_returned;
	// how many can we launch at one time?
	int32_t         m_maxLaunched;
	// how many are in the queue now?
	//int32_t         m_entriesUsed;
	// m_top is the first unused entry with nothing used above it
	int32_t         m_top;
	// the list of entries in this queue
	//ThreadEntry  m_entries [ MAX_THREAD_ENTRIES ];
	ThreadEntry *m_entries ;
	int32_t         m_entriesSize;
	int32_t         m_maxEntries;

	// linked list head for launched thread entries
	ThreadEntry *m_launchedHead;

	// linked list head for empty thread entries
	ThreadEntry *m_emptyHead;

	// 8 heads/tails for linked lists of thread entries waiting to launch
	ThreadEntry *m_waitHead0;
	ThreadEntry *m_waitHead1;
	ThreadEntry *m_waitHead2;
	ThreadEntry *m_waitHead3;
	ThreadEntry *m_waitHead4;
	ThreadEntry *m_waitHead5;
	ThreadEntry *m_waitHead6;

	ThreadEntry *m_waitTail0;
	ThreadEntry *m_waitTail1;
	ThreadEntry *m_waitTail2;
	ThreadEntry *m_waitTail3;
	ThreadEntry *m_waitTail4;
	ThreadEntry *m_waitTail5;
	ThreadEntry *m_waitTail6;


	/*
	// counts the high/low priority (niceness <= 0) threads
	int64_t   m_hiLaunched;
	int64_t   m_hiReturned;
	int64_t   m_mdLaunched;
	int64_t   m_mdReturned;
	int64_t   m_loLaunched;
	int64_t   m_loReturned;
	// disk writing
	int64_t   m_writesLaunched;
	int64_t   m_writesReturned;
	// now for disk threads we partition by the read size
	int64_t   m_hiLaunchedBig;
	int64_t   m_hiReturnedBig;
	int64_t   m_mdLaunchedBig;
	int64_t   m_mdReturnedBig;
	int64_t   m_loLaunchedBig;
	int64_t   m_loReturnedBig;
	int64_t   m_hiLaunchedMed;
	int64_t   m_hiReturnedMed;
	int64_t   m_mdLaunchedMed;
	int64_t   m_mdReturnedMed;
	int64_t   m_loLaunchedMed;
	int64_t   m_loReturnedMed;
	int64_t   m_hiLaunchedSma;
	int64_t   m_hiReturnedSma;
	int64_t   m_mdLaunchedSma;
	int64_t   m_mdReturnedSma;
	int64_t   m_loLaunchedSma;
	int64_t   m_loReturnedSma;
	*/

	// init
	bool         init (char threadType, int32_t maxThreads, int32_t maxEntries);

	ThreadQueue();
	void reset();

	int32_t getNumThreadsOutOrQueued();
	int32_t getNumWriteThreadsOut() ;
	int32_t getNumActiveThreadsOut() ;


	// . for adding an entry
	// . returns false and sets errno on error
	ThreadEntry *addEntry ( int32_t   niceness                     ,
				void  *state                        , 
				void  (* callback    )(void *state,
						       class ThreadEntry *t) ,
				void *(* startRoutine)(void *state,
						       class ThreadEntry *t) );
	// calls the callback of threads that are done (exited) and then
	// removes them from the queue
	bool         cleanUp      ( ThreadEntry *tt , int32_t maxNiceness );
	bool         timedCleanUp ( int32_t maxNiceness );

	void bailOnReads ();
	bool isHittingFile ( class BigFile *bf );

	// . launch a thread from our queue
	// . returns false and sets errno on error
	bool         launchThread2 ( );

	bool launchThreadForReals ( ThreadEntry **headPtr ,
				    ThreadEntry **tailPtr ) ;

	void removeThreads2 ( ThreadEntry **headPtr ,
			      ThreadEntry **tailPtr ,
			      class BigFile *bf ) ;

	void print ( ) ;

	// these are called by g_udpServer2, the high priority udp server
	void suspendLowPriorityThreads();
	void resumeLowPriorityThreads();

	void killAllThreads();

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

	Threads();

	// returns false and sets errno on error, true otherwise
	bool init();

	int32_t getStack ( ) ;
	void returnStack ( int32_t si );
	void setPid();
	void reset ( ) ;

	// . we restrict the # of threads based on their type
	// . for instance we allow up to say, 5, disk i/o threads,
	//   but only 1 thread for doing IndexList intersections
	// . threads with higher niceness values always wait for any thread
	//   with lower niceness to complete
	// . returns false and sets errno on error, true otherwise
	bool registerType ( char type , int32_t maxThreads , int32_t maxEntries );

	// is the caller a thread?
	bool amThread ( );

	void printQueue ( int32_t q ) { m_threadQueues[q].print(); };
	void printState();


	// disable all threads... no more will be created, those in queues
	// will never be spawned
	void disableThreads () { m_disabled = true;  };
	void enableThreads  () { m_disabled = false; };
	bool areThreadsDisabled() { return m_disabled; };
	bool areThreadsEnabled () { return ! m_disabled; };

	void killAllThreads();

	// . returns false and sets errno if thread launch failed
	// . returns true on success
	// . when thread is done a signal will be put on the g_loop's
	//   sigqueue to call "callback" with "state" as the parameter
	// . niceness deteremines the niceness of this signal as well as
	//   the thread's priority
	bool call ( char   type                               ,
		    int32_t   niceness                           ,
		    void  *state                              , 
		    void  (* threadDoneCallback)(void *state,
						 class ThreadEntry *t) ,
		    void *(* startRoutine      )(void *state,
						 class ThreadEntry *t) );

	bool call ( char type , int32_t niceness ,
		    void *state  , void (* callback)(void *state,
						     class ThreadEntry *t) );

	// try to launch threads waiting to be launched in any queue
	int32_t launchThreads ();

	// call cleanUp() for each thread queue
	bool cleanUp ( ThreadEntry *tt , int32_t maxNiceness ) ;

	void bailOnReads ();
	bool isHittingFile ( class BigFile *bf ) ;

	//calls callbacks and launches all threads
	int32_t timedCleanUp (int32_t maxTime, int32_t niceness );//= MAX_NICENESS);

	// these are called by g_udpServer2, the high priority udp server
	void suspendLowPriorityThreads();
	void resumeLowPriorityThreads();

	// cancels low priority threads running in each ThreadQueue
	//void cancelLowPriorityThreads();

	// . used by Msg34 for computing the disk load
	// . gets the number of disk threads (seeks) and total bytes to read
	// . ignores disk threads that are too nice (over maxNiceness)
	int32_t getDiskThreadLoad ( int32_t maxNiceness , int32_t *totalToRead ) ;

	ThreadQueue* getThreadQueues() { return &m_threadQueues[0];}
	int32_t      getNumThreadQueues() { return m_numQueues; }

	// used by UdpServer to see if it should call a low priority callback
	//int32_t getNumActiveHighPriorityCpuThreads() ;
	// all high priority threads...
	int32_t getNumActiveHighPriorityThreads() ;

	bool hasHighPriorityCpuThreads() ;

	int32_t getNumThreadsOutOrQueued();
	int32_t getNumWriteThreadsOut() ;

	int32_t getNumActiveWriteUnlinkRenameThreadsOut() ;

	// counts the high/low priority (niceness <= 0) threads
	//int64_t   m_hiLaunched;
	//int64_t   m_hiReturned;
	//int64_t   m_loLaunched;
	//int64_t   m_loReturned;

	bool m_needsCleanup;
	//bool m_needBottom;

	bool m_initialized;

	// private:

	// . allow up to MAX_THREAD_QUEUES different thread types for now
	// . types are user-defined numbers
	// . each type has a corresponding thread queue
	// . when a thread is done we place a signal on g_loop's sigqueue so
	//   that it will call m_callback w/ m_state 
	ThreadQueue m_threadQueues  [ MAX_THREAD_QUEUES ];
	int32_t        m_numQueues;

	bool        m_disabled;
};

extern class Threads g_threads;

void ohcrap ( void *state , class ThreadEntry *t ) ;

#endif
