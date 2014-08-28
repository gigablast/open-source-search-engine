#include "gb-include.h"

#include "Mem.h"
#include <sys/time.h>      // setrlimit()
#include <sys/resource.h>  // setrlimit()

#include "Threads.h"
#include "SafeBuf.h"
#include "PingServer.h"
//#include "MemPoolVar.h"
//#include "malloc.h"
//#include "Stats.h"
#include "Pages.h"

// put me back
//#define EFENCE
//#define EFENCE_SIZE 50000

// uncomment this for EFENCE to do underflow checks instead of the
// default overflow checks
//#define _CHECKUNDERFLOW_

// only Mem.cpp can call ::malloc, everyone else must call mmalloc() so
// we can keep tabs on memory usage. in Mem.h we #define this to be coreme()

#undef malloc
#undef calloc
#undef realloc

// from malloc.c (dlmalloc)
//void *dlmalloc(size_t);
//void  dlfree(void*);
//void* dlcalloc(size_t, size_t);
//void* dlrealloc(void*, size_t);
//#include "malloc.c"

#define sysmalloc ::malloc
#define syscalloc ::calloc
#define sysrealloc ::realloc
#define sysfree ::free
/*
// try using dlmalloc to see how it cores
#define sysmalloc dlmalloc
#define syscalloc dlcalloc
#define sysrealloc dlrealloc
#define sysfree dlfree
*/

// allocate an extra space before and after the allocated memory to try
// to catch sequential buffer underruns and overruns. if the write is way
// beyond this padding radius, chances are it will seg fault right then and
// there because it will hit a different PAGE, to be more sure we could
// make UNDERPAD and OVERPAD PAGE bytes, although the overrun could still write
// to another allocated area of memory and we can never catch it.
#if defined(EFENCE) || defined(EFENCE_SIZE)
#define UNDERPAD 0
#define OVERPAD  0
#else
#define UNDERPAD 4
#define OVERPAD  4
#endif

#define MAGICCHAR 0xda

class Mem g_mem;

extern bool g_isYippy;

bool freeCacheMem();

#if defined(EFENCE) || defined(EFENCE_SIZE)
static void *getElecMem ( long size ) ;
static void  freeElecMem ( void *p  ) ;
#endif

/*
static long s_mutexLockAvail = 1;

// usually we can use the UdpServer ptr as the pid, or if the main process,
// then just use 0
void mutexLock ( ) {

	//log("gb: mutex lock");
 loop:

	//log("gb: mutex lock loop 1");

	// wait for the lock if already taken
	while ( s_mutexLockAvail != 1 ) sched_yield();
	
	//log("gb: mutex lock loop 2");
	
	// . it now *seems* to be available, i.e. equal to 1 so try to get it
	// . similar to atomic.h in kernel source
	
	// Atomically decrements @s_mutexLockAvail by 1
	// and returns true if the result is zero, or false for all
	// other cases.  Note that the guaranteed
	// useful range of an atomic_t is only 24 bits.
	unsigned char c;
	__asm__ __volatile__(
			     "lock;"
			     "decl %0; sete %1"
			     :"=m" (s_mutexLockAvail), "=qm" (c)
			     :"m" (s_mutexLockAvail) 
			     : "memory");


	//log("gb c=%li mutexAvail=%li",c,s_mutexLockAvail);
	
	// if c is 0, we got the lock, otherwise, keep trying
	if ( c != 1 ) {
		//log("gb: failed to get lock. retrying.");
		goto loop;
	}
	// log("gb: got mutex lock");
	s_mutexLockAvail = 0;
}

void mutexUnlock ( ) {
	//if ( s_mutexLockAvail != 0 ) {
	//	log("gb: mutex unlock HEY lock=%li",s_mutexLockAvail);
	//	//char *xx = NULL; *xx = 0;
	//}

	// a single instruction is atomic
	__asm__ __volatile__(
			     //"lock;"
			     "movl $1,%0;"
			     :"=m" (s_mutexLockAvail)
			     :"m" (s_mutexLockAvail) 
			     : "memory");

	if ( s_mutexLockAvail != 1 )
		logf(LOG_INFO,"gb: mutex unlock lock=%li",s_mutexLockAvail);
}
*/

// if we alloc too much in one call, pthread_create() fails for some reason
//#define MAXMEMPERCALL (256*1024*1024-1)

//[mwells@lenny c]$ echo "inuse on" > .psrc
//[mwells@lenny c]$ ./slowleak
//** Insure messages will be written to insra **
//[mwells@lenny c]$ tca -X

// the thread lock
//static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

// make it big for production machines
//#define DMEMTABLESIZE (1024*602)
// there should not be too many mallocs any more
// i boosted from 300k to 600k so we can get summaries for 150k results
// for the csv download...
#define DMEMTABLESIZE (1024*602)
//#define DMEMTABLESIZE (1024*202)
// and small for local machine
//#define DMEMTABLESIZE (1024*50)

// a table used in debug to find mem leaks
static void **s_mptrs ;
static long  *s_sizes ;
static char  *s_labels;
static char  *s_isnew;
static long   s_n = 0;
static bool   s_initialized = 0;

// our own memory manager
//static MemPoolVar s_pool;
void operator delete (void *ptr) throw () {
	// now just call this
	g_mem.gbfree ( (char *)ptr , -1 , NULL );
}

void operator delete [] ( void *ptr ) throw () {
	// now just call this
	g_mem.gbfree ( ((char *)ptr-4) , -1 , NULL );
}

#define MINMEM 6000000
//#define MINMEM 0

// caution -- put {}'s around the "new"
//#define new(X) new X; g_mem.addMem(X,sizeof(*X),"new");

void Mem::addnew ( void *ptr , long size , const char *note ) {
	// 1 --> isnew
	addMem ( ptr , size , note , 1 );
}

void Mem::delnew ( void *ptr , long size , const char *note ) {
	// we don't need to use mdelete() if checking for leaks is enabled
	// because the size of the allocated mem is in the hash table under
	// s_sizes[]. and the delete() operator is overriden below to
	// catch this.
	return;
	/*
	// don't let electric fence zap us
	//if ( size == 0 && ptr==(void *)0x7fffffff) return;
	if ( size == 0 ) return;
	// watch out for bad sizes
	if ( size < 0 ) {
		log(LOG_LOGIC,"mem: delete(%li): Bad size.", size );
		return;
	}
	// debug
	if ( size > MINMEM )
		log(LOG_INFO,"mem: delete(%li): %s.",size,note);
	// count it
	if ( size > 0 ) {
		m_used -= size;
		m_numAllocated--;
	}
	*/
}

// this must be defined for newer libc++
//int bad_alloc ( ) { return 1; };

// . global override of new and delete operators
// . seems like constructor and destructor are still called
// . just use to check if enough memory
// . before this just called mmalloc which sometimes returned NULL which 
//   would cause us to throw an unhandled signal. So for now I don't
//   call mmalloc since it is limited in the mem it can use and would often
//   return NULL and set g_errno to ENOMEM
void * operator new (size_t size) throw (std::bad_alloc) {
	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;

	// . fail randomly
	// . good for testing if we can handle out of memory gracefully
	//static long s_mcount = 0;
	//s_mcount++;
	//if ( s_mcount > 57 && (rand() % 1000) < 2 ) { 
	if ( g_conf.m_testMem && (rand() % 100) < 2 ) { 
		g_errno = ENOMEM; 
		log("mem: new-fake(%lu): %s",size, mstrerror(g_errno));
		throw std::bad_alloc(); 
		// return NULL; }
	} 

	//char unlock = true;
	//if ( ! g_stats.m_gotLock || g_threads.amThread() ) mutexLock();
	//else                                               unlock = false;

	// don't go over max
	if ( g_mem.m_used + size >= g_mem.m_maxMem &&
	     g_mem.m_maxMem > 1000000 ) {
		log("mem: new(%lu): Out of memory.", size );
		//if ( unlock ) mutexUnlock();
		throw std::bad_alloc();
		//throw 1;
	}
#ifdef EFENCE
	void *mem = getElecMem(size);
#elif EFENCE_SIZE
	void *mem;
	if ( size > EFENCE_SIZE )
		mem = getElecMem(size);
	else
		mem = sysmalloc ( size );
#else
	//void *mem = dlmalloc ( size );
	void *mem = sysmalloc ( size );
#endif
	long  memLoop = 0;
newmemloop:
	//void *mem = s_pool.malloc ( size );
	if ( ! mem && size > 0 ) {
		g_errno = errno;
		log("mem: new(%lu): %s",size, mstrerror(g_errno));
		//if ( unlock ) mutexUnlock();
		throw std::bad_alloc();
		//throw 1;
		//return NULL;
	}
	if ( (unsigned long)mem < 0x00010000 ) {
#ifdef EFENCE
		void *remem = getElecMem(size);
#else
		void *remem = sysmalloc(size);
#endif
		log ( LOG_WARN, "mem: Caught low memory allocation at %08lx, "
				"reallocated to %08lx", (unsigned long)mem,
				(unsigned long)remem );
#ifdef EFENCE
		freeElecMem (mem);
#else
		sysfree(mem);
#endif
		mem = remem;
		if ( memLoop > 100 ) {
			log ( LOG_WARN, "mem: Attempted to reallocate low "
					"memory allocation 100 times, "
					"aborting and returning ENOMEM." );
			g_errno = ENOMEM;
			//if ( unlock ) mutexUnlock();
			throw std::bad_alloc();
		}
		goto newmemloop;
	}

	g_mem.addMem ( mem , size , "TMPMEM" , 1 );

	//if ( unlock ) mutexUnlock();
	return mem;
}


//WARNING: Use this construct only when your datatype has a destructor!
//the compiler checks to see if a destructor is defined, if it is it
//will add 4 bytes to your requested size and put the number of objects
//in those bytes and returns a mem ptr at the malloced address + 4.
//this can screw up the subsequent call to addmem because the size and
//ptrs are off.
void * operator new [] (size_t size) throw (std::bad_alloc) {
	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;
	// . fail randomly
	// . good for testing if we can handle out of memory gracefully

	//static long s_count = 0;
	//s_count++;
	//if ( s_count > 3000 && (rand() % 100) < 2 ) { 
	//	g_errno = ENOMEM; 
	//	log("mem: new-fake(%i): %s",size, mstrerror(g_errno));
	//	throw bad_alloc(); 
	//	// return NULL; }
	//} 
	
	// don't go over max
	if ( g_mem.m_used + size >= g_mem.m_maxMem &&
	     g_mem.m_maxMem > 1000000 ) {
		log("mem: new(%lu): Out of memory.", size );
		throw std::bad_alloc();
		//throw 1;
	}
#ifdef EFENCE
	void *mem = getElecMem(size);
#elif EFENCE_SIZE
	void *mem;
	if ( size > EFENCE_SIZE )
		mem = getElecMem(size);
	else
		mem = sysmalloc ( size );
#else
	//void *mem = dlmalloc ( size );
	void *mem = sysmalloc ( size );
#endif

	long  memLoop = 0;
newmemloop:
	//void *mem = s_pool.malloc ( size );
	if ( ! mem && size > 0 ) {
		g_errno = errno;
		log("mem: new(%lu): %s",size, mstrerror(g_errno));
		//if ( unlock ) mutexUnlock();
		throw std::bad_alloc();
		//throw 1;
		//return NULL;
	}
	if ( (unsigned long)mem < 0x00010000 ) {
#ifdef EFENCE
		void *remem = getElecMem(size);
#else
		void *remem = sysmalloc(size);
#endif
		log ( LOG_WARN, "mem: Caught low memory allocation at %08lx, "
				"reallocated to %08lx", 
		      (long)mem, (long)remem );
#ifdef EFENCE
		freeElecMem (mem);
#else
		sysfree(mem);
#endif
		mem = remem;
		if ( memLoop > 100 ) {
			log ( LOG_WARN, "mem: Attempted to reallocate low "
					"memory allocation 100 times, "
					"aborting and returning ENOMEM." );
			g_errno = ENOMEM;
			//if ( unlock ) mutexUnlock();
			throw std::bad_alloc();
		}
		goto newmemloop;
	}

	//offset by 4 because that is metadata describing the number of objs
	//in the array
	g_mem.addMem ( (char*)mem+4 , size-4, "TMPMEM" , 1 );

	//if ( unlock ) mutexUnlock();
	return mem;
}


Mem::Mem() {
	m_used = 0;
	// assume large max until this gets set for real
	m_maxMem  = 50000000;
	m_numAllocated = 0;
	m_numTotalAllocated = 0;
	m_maxAlloc = 0;
	m_maxAllocBy = "";
	m_maxAlloced = 0;
	m_memtablesize = DMEMTABLESIZE;
 	m_stackStart = NULL;
	// shared mem used
	m_sharedUsed = 0LL;
}

Mem::~Mem() {
	if ( getUsedMem() == 0 ) return;
	//log(LOG_INIT,"mem: Memory allocated now: %li.\n", getUsedMem() );
	// this is now called from main.cpp::allExit() because it freezes
	// up in printMem()'s call to sysmalloc() for some reason
	//	printMem();
}

//long Mem::getUsedMem    () { return 0; }; //return mallinfo().usmblks; };
long long Mem::getAvailMem   () { return 0; };
//long long Mem::getMaxAlloced () { return 0; };
long long Mem::getMaxMem     () { return g_conf.m_maxMem; }
long Mem::getNumChunks  () { return 0; };

// process id of the main process
pid_t s_pid = (pid_t) -1;

void Mem::setPid() {
	s_pid = getpid();
	//log("mem: pid is %li",(long)s_pid);
	if(s_pid == -1 ) { log("monitor: bad s_pid"); char *xx=NULL;*xx=0; } 
}

pid_t Mem::getPid() {
	return s_pid;
}

bool Mem::init  ( long long maxMem ) { 
	// set main process pid
	s_pid = getpid();

	// . don't swap our memory out, man...
	// . damn, linux 2.4.17 seems to crash the kernel sometimes w/ this
	//if ( mlockall( MCL_CURRENT | MCL_FUTURE ) == -1 ) {
	//	log("Mem::init: mlockall: %s" , strerror(errno) );
	//	errno = 0;
	//}
	m_maxMem  = maxMem;
	// set it 
	//struct rlimit lim;
	//lim.rlim_max = maxMem;
	//setrlimit ( RLIMIT_AS , &lim ); // ulimit -v
	// note
	//log(LOG_INIT,"mem: Max memory usage set to %lli bytes.", maxMem);
	// warning msg
	if ( g_conf.m_detectMemLeaks )
		log(LOG_INIT,"mem: Memory leak checking is enabled.");

#if defined(EFENCE) || defined(EFENCE_SIZE)
	log(LOG_INIT,"mem: using electric fence!!!!!!!");
#endif

	/*
	  take this out for now it seems to hang the OS when running
	  as root

#ifndef TITAN
	// if we can't alloc 3gb exit and retry
	long long start = gettimeofdayInMilliseconds();
	char *pools[30];
	long long count = 0LL;
	long long chunk = 100000000LL; // 100MB chunks
	long long need = 3000000000LL; // 3GB
	long i = 0; for ( i = 0 ; i < 30 ; i++ ) {
		pools[i] = (char *)mmalloc(chunk,"testmem");
		count += chunk;
		if ( pools[i] ) continue;
		count -= chunk;
		log("mem: could only alloc %lli bytes of the "
		    "%lli required to run gigablast. exiting.",
		    count , need );
	}
	for ( long j = 0 ; j < i ; j++ )
		mfree ( pools[j] , chunk , "testmem" );
	long long now = gettimeofdayInMilliseconds();
	long long took = now - start;
	if ( took > 20 ) log("mem: took %lli ms to check memory ceiling",took);
	// return if could not alloc the full 3GB
	if ( i < 30 ) return false;
#endif
	*/

	// reset this, our max mem used over time ever because we don't
	// want the mem test we did above to count towards it
	m_maxAlloced = 0;

	// init or own malloc stuff in malloc.c (from doug leay)
	//if ( mdw_init_sbrk ( maxMem ) ) return true;
	// bitch
	//return log("Mem::init: failed to malloc %li bytes", maxMem);
	return true;
}

//bool  Mem::reserveMem ( long long bytesToReserve ) {
	// TODO: use sbrk()?
//	char *s = (char *) malloc ( bytesToReserve );
//	if ( s ) { free ( s ); return true; }
	// TODO: try smaller blocks
//	return false;
//}

// this is called by C++ classes' constructors to register mem
void Mem::addMem ( void *mem , long size , const char *note , char isnew ) {

	// enforce safebuf::setLabel being called
	//if ( size>=100000 && note && strcmp(note,"SafeBuf")==0 ) {
	//	char *xx=NULL;*xx=0; }

	//validate();

	if ( (long)m_numAllocated + 100 >= (long)m_memtablesize ) { 
		bool s_printed = false;
		if ( ! s_printed ) {
			log("mem: using too many slots");
			printMem();
			s_printed = true;
		}
	}

	// sanity check
	if ( g_inSigHandler ) {
		log(LOG_LOGIC,"mem: In sig handler.");
		char *xx = NULL; *xx = 0;
	}
	// debug msg (mdw)
	//char bb[100];
	//bb[0]=0;
	//if ( strcmp(note,"UdpSlot")== 0 ) {
	//	unsigned char c = (*(unsigned char *)mem) & 0x3f;
	//	sprintf(bb," msgType=0x%lx",(long)c);
	//}
	if ( g_conf.m_logDebugMem )
		log("mem: add %08lx %li bytes (%lli) (%s)",
		    (long)mem,size,m_used,note);

	//if ( strcmp(note,"RdbList") == 0 ) 
	//	log("mem: freelist%08lx %libytes (%s)",(long)mem,size,note);

	// check for breech after every call to alloc or free in order to
	// more easily isolate breeching code.. this slows things down a lot
	// though.
	if ( g_conf.m_logDebugMem ) printBreeches(1);

	// copy the magic character, iff not a new() call
	if ( size == 0 ) { char *xx = NULL; *xx = 0; }
	// don't add 0 bytes
	//if ( size == 0 ) return;
	// sanity check
	if ( size < 0 ) {
		log("mem: addMem: Negative size.");
		return;
	}

	// sanity check -- for machines with > 4GB ram?
	if ( (unsigned long)mem + (unsigned long)size < (unsigned long)mem ) {
		log(LOG_LOGIC,"mem: Kernel returned mem at %08lx of size %li "
		    "which would wrap. Bad kernel.",(long)mem,(long)size);
		char *xx = NULL; 
		*xx = 0;
	}

	if ( ! isnew ) {
		for ( long i = 0 ; i < UNDERPAD ; i++ )
			((char *)mem)[0-i-1] = MAGICCHAR;
		for ( long i = 0 ; i < OVERPAD ; i++ )
			((char *)mem)[0+size+i] = MAGICCHAR;
	}
	// hey!
	if ( s_pid == -1 && m_numTotalAllocated >1000 ) { char *xx=NULL;*xx=0;}
	// threads can't be here!
	if ( s_pid != -1 && getpid() != s_pid ) {
		log("mem: addMem: Called from thread.");
		sleep(50000);
		//char *p = NULL;
		//*p = 1;
		char *xx = NULL; *xx = 0;
	}

	// if no label!
	if ( ! note[0] ) log(LOG_LOGIC,"mem: addmem: NO note.");

	// lock for threads
	//pthread_mutex_lock ( &s_lock );
	// return NULL if we'd go over our limit
	//if ( getUsedMem() + size > s_maxMem ) {
	//	log("Mem::addMem: max mem limit breeched");
	//	sleep(50000); 
	//	return;
	//}
	// clear mem ptrs if this is our first call
	if ( ! s_initialized ) {
		s_mptrs  = (void **)sysmalloc ( m_memtablesize*sizeof(void *));
		s_sizes  = (long  *)sysmalloc ( m_memtablesize*sizeof(long  ));
		s_labels = (char  *)sysmalloc ( m_memtablesize*16            );
		s_isnew  = (char  *)sysmalloc ( m_memtablesize               );
		if ( ! s_mptrs || ! s_sizes || ! s_labels || ! s_isnew ) {
			if ( s_mptrs  ) sysfree ( s_mptrs  );
			if ( s_sizes  ) sysfree ( s_sizes  );
			if ( s_labels ) sysfree ( s_labels );
			if ( s_isnew  ) sysfree ( s_labels );
			log("mem: addMem: Init failed. Disabling checks.");
			g_conf.m_detectMemLeaks = false;
			return;
		}
		s_initialized = true;
		memset ( s_mptrs , 0 , 4 * m_memtablesize );
	}
	// try to add ptr/size/note to leak-detecting table
	if ( (long)s_n > (long)m_memtablesize ) {
		log("mem: addMem: No room in table for %s size=%li.",
		    note,size);
		// unlock for threads
		//pthread_mutex_unlock ( &s_lock );
		return;
	}
	// hash into table
	unsigned long u = (unsigned long)mem * (unsigned long)0x4bf60ade;
	unsigned long h = u % (unsigned long)m_memtablesize;
	// chain to an empty bucket
	long count = (long)m_memtablesize;
	while ( s_mptrs[h] ) {
		// if an occupied bucket as our same ptr then chances are
		// we freed without calling rmMem() and a new addMem() got it
		if ( s_mptrs[h] == mem ) {
			// if we are being called from addnew(), the 
			// overloaded "operator new" function above should
			// have stored a temp ptr in here... allow that, it
			// is used in case an engineer forgets to call 
			// mnew() after calling new() so gigablast would never
			// realize that the memory was allocated.
			if ( s_sizes[h] == size &&
			     s_labels[h*16+0] == 'T' &&
			     s_labels[h*16+1] == 'M' &&
			     s_labels[h*16+2] == 'P' &&
			     s_labels[h*16+3] == 'M' &&
			     s_labels[h*16+4] == 'E' &&
			     s_labels[h*16+5] == 'M'  )
				goto skipMe;
			log("mem: addMem: Mem already added. "
			    "rmMem not called?");
			char *xx = NULL; *xx = 0; //sleep(50000);
		}
		h++;
		if ( h == m_memtablesize ) h = 0;
		if ( --count == 0 ) {
			log("mem: addMem: Mem table is full.");
			printMem();
			char *xx = NULL; *xx = 0; //sleep(50000);
		}
	}
	// add to debug table
	s_mptrs  [ h ] = mem;
	s_sizes  [ h ] = size;
	s_isnew  [ h ] = isnew;
	//log("adding %li size=%li to [%li] #%li (%s)",
	//(long)mem,size,h,s_n,note);
	s_n++;
	// debug
	if ( size > MINMEM && g_conf.m_logDebugMemUsage )
		log(LOG_INFO,"mem: addMem(%li): %s. ptr=0x%lx used=%lli",
		    size,note,(long)mem,m_used);
	// now update used mem
	// we do this here now since we always call addMem() now
	m_used += size;
	m_numAllocated++;
	m_numTotalAllocated++;
	if ( size > m_maxAlloc ) { m_maxAlloc = size; m_maxAllocBy = note; }
	if ( m_used > m_maxAlloced ) m_maxAlloced = m_used;


 skipMe:
	long len = gbstrlen(note);
	if ( len > 15 ) len = 15;
	char *here = &s_labels [ h * 16 ];
	memcpy ( here , note , len );
	// make sure NULL terminated
	here[len] = '\0';
	// unlock for threads
	//pthread_mutex_unlock ( &s_lock );
	//validate();
}


#define PRINT_TOP 40

class MemEntry {
public:
	long  m_hash;
	char *m_label;
	long  m_allocated;
	long  m_numAllocs;
};

// print out the mem table
// but combine allocs with the same label
// sort by mem allocated
bool Mem::printMemBreakdownTable ( SafeBuf* sb, 
				   char *lightblue, 
				   char *darkblue) {
	char *ss = "";

	// make sure the admin viewing this table knows that there will be
	// frees in here that are delayed if electric fence is enabled.
#ifdef EFENCE
	ss = " <font color=red>*DELAYED FREES ENABLED*</font>";
#endif

	sb->safePrintf (
		       "<table>"

		       "<table %s>"
		       "<tr>"
		       "<td colspan=3 bgcolor=#%s>"
		       "<center><b>Mem Breakdown%s</b></td></tr>\n"

		       "<tr bgcolor=#%s>"
		       "<td><b>allocator</b></td>"
		       "<td><b>num allocs</b></td>"
		       "<td><b>allocated</b></td>"
		       "</tr>" ,
		       TABLE_STYLE, darkblue , ss , darkblue );

	long n = m_numAllocated * 2;
	MemEntry *e = (MemEntry *)mcalloc ( sizeof(MemEntry) * n , "Mem" );
	if ( ! e ) {
		log("admin: Could not alloc %li bytes for mem table.",
		    (long)sizeof(MemEntry)*n);
		return false;
	}

	// hash em up, combine allocs of like label together for this hash
	for ( long i = 0 ; i < (long)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		// get label ptr, use as a hash
		char *label = &s_labels[i*16];
		long  h     = hash32n ( label );
		if ( h == 0 ) h = 1;
		// accumulate the size
		long b = (unsigned long)h % n;
		// . chain till we find it or hit empty
		// . use the label as an indicator if bucket is full or empty
		while ( e[b].m_hash && e[b].m_hash != h )
			if ( ++b >= n ) b = 0;
		// add it in
		e[b].m_hash       = h;
		e[b].m_label      = label;
		e[b].m_allocated += s_sizes[i];
		e[b].m_numAllocs++;
	}

	// get the top 20 users of mem
	MemEntry *winners [ PRINT_TOP ];

	long i = 0;
	long count = 0;
	for ( ; i < n && count < PRINT_TOP ; i++ )
		// if non-empty, add to winners array
		if ( e[i].m_hash ) winners [ count++ ] = &e[i];

	// compute new min
	long min  = 0x7fffffff;
	long mini = -1000;
	for ( long j = 0 ; j < count ; j++ ) {
		if ( winners[j]->m_allocated > min ) continue;
		min  = winners[j]->m_allocated;
		mini = j;
	}

	// now the rest must compete
	for ( ; i < n ; i++ ) {
		// if empty skip
		if ( ! e[i].m_hash ) continue;
		//if ( e[i].m_allocated > 120 && e[i].m_allocated < 2760 )
		//	log("hey %li", e[i].m_allocated);
		// skip if not a winner
		if ( e[i].m_allocated <= min ) continue;
		// replace the lowest winner
		winners[mini] = &e[i];
		// compute new min
		min = 0x7fffffff;
		for ( long j = 0 ; j < count ; j++ ) {
			if ( winners[j]->m_allocated > min ) continue;
			min  = winners[j]->m_allocated;
			mini = j;
		}
	}

	// now sort them
	bool flag = true;
	while ( flag ) {
		flag = false;
		for ( long i = 1 ; i < count ; i++ ) {
			// no need to swap?
			if ( winners[i-1]->m_allocated >= 
			     winners[i]->m_allocated ) continue;
			// swap
			flag = true;
			MemEntry *tmp = winners[i-1];
			winners[i-1]  = winners[i];
			winners[i  ]  = tmp;
		}
	}
			
	// now print into buffer
	for ( long i = 0 ; i < count ; i++ ) 
		sb->safePrintf (
			       "<tr bgcolor=%s>"
			       "<td>%s</td>"
			       "<td>%li</td>"
			       "<td>%li</td>"
			       "</tr>\n",
			       LIGHT_BLUE,
			       winners[i]->m_label,
			       winners[i]->m_numAllocs,
			       winners[i]->m_allocated);

	sb->safePrintf ( "</table>\n");

	// don't forget to release this mem
	mfree ( e , (long)sizeof(MemEntry) * n , "Mem" );

	return true;
}


// Relabels memory in table.  Returns true on success, false on failure.
// Purpose is for times when UdpSlot's buffer is not owned and freed by someone
// else.  Now we can verify that passed memory is freed.
bool Mem::lblMem( void *mem, long size, const char *note ) {
	// seems to be a bad bug in this...
	return true;

	bool val = false;

	// Make sure we're not relabeling a NULL or dummy memory address,
	// if so, error then exit
	if( !mem ){		
		//log( "mem: lblMem: Mem addr (0x%08X) invalid/NULL, not "
		//     "relabeling.", mem );
		return val;
	}
	else if( (unsigned long)mem == 0x7fffffff ) {
		//log( "mem: lblMem: Mem addr (0x%08X) is dummy address, not "
		//     "relabeling.", mem );
		return val;
	}

	unsigned long u = (unsigned long)mem * (unsigned long)0x4bf60ade;
	unsigned long h = u % (unsigned long)m_memtablesize;
	// chain to bucket
	while( s_mptrs[h] ) {
		if( s_mptrs[h] == mem ) {
			if( s_sizes[h] != size ) {
				val = false;
				log( "mem: lblMem: Mem addr (0x%08X) exists, "
				     "size is %li off.", (unsigned int)mem,
					 s_sizes[h]-size );
				break;
			}
			long len = gbstrlen(note);
			if ( len > 15 ) len = 15;
			char *here = &s_labels [ h * 16 ];
			memcpy ( here , note , len );
			// make sure NULL terminated
			here[len] = '\0';
			val = true;
			break;
		}
		h++;
		if ( h == m_memtablesize ) h = 0;
	}

	if( !val ) log( "mem: lblMem: Mem addr (0x%08X) not found.", 
			(unsigned int)mem );

	return val;
}

// this is called by C++ classes' destructors to unregister mem
bool Mem::rmMem  ( void *mem , long size , const char *note ) {

	//validate();

	// sanity check
	if ( g_inSigHandler ) {
		log(LOG_LOGIC,"mem: In sig handler 2.");
		char *xx = NULL; *xx = 0;
	}
	// debug msg (mdw)
	if ( g_conf.m_logDebugMem )
		log("mem: free %08lx %libytes (%s)",(long)mem,size,note);

	//if ( strcmp(note,"RdbList") == 0 ) 
	//	log("mem: freelist%08lx %libytes (%s)",(long)mem,size,note);

	// check for breech after every call to alloc or free in order to
	// more easily isolate breeching code.. this slows things down a lot
	// though.
	if ( g_conf.m_logDebugMem ) printBreeches(1);

	// don't free 0 bytes
	if ( size == 0 ) return true;
	// hey!
	if ( s_pid == -1 && m_numTotalAllocated >1000 ) { char *xx=NULL;*xx=0;}
	// threads can't be here!
	if ( s_pid != -1 && getpid() != s_pid ) {
		log("mem: rmMem: Called from thread.");
		sleep(50000);
		// throw a bogus sig so we crash
		char *xx=NULL;*xx=0;
		//sigval_t svt; 
		//svt.sival_int = 1; // fd;
		//sigqueue ( s_pid, GB_SIGRTMIN+1 , svt ) ;
		//return true;
	}
	// lock for threads
	//pthread_mutex_lock ( &s_lock );
	// . hash by first hashing "mem" to mix it up some
	// . balance the mallocs/frees
	// . hash into table
	unsigned long u = (unsigned long)mem * (unsigned long)0x4bf60ade;
	unsigned long h = u % (unsigned long)m_memtablesize;
	// . chain to an empty bucket
	// . CAUTION: loops forever if no empty bucket
	while ( s_mptrs[h] && s_mptrs[h] != mem ) {
		h++;
		if ( h == m_memtablesize ) h = 0;
	}
	// if not found, bitch
	if ( ! s_mptrs[h] ) {
		log("mem: rmMem: Unbalanced free. "
		    "note=%s size=%li.",note,size);
		// . return false for now to prevent coring
		// . NOTE: but if entry was not added to table because there 
		//   was no room, we really need to be decrementing m_used
		//   and m_numAllocated here
		// . no, we should core otherwise it can result in some
		//   pretty hard to track down bugs later.
		//return false;
#ifndef _VALGRIND_
		char *xx = NULL;
		*xx = 0;
#endif
		//sleep(50000);
		// unlock for threads
		//pthread_mutex_unlock ( &s_lock );
		return false;
	}
	// are we from the "new" operator
	bool isnew = s_isnew[h];
	// set our size
	if ( size == -1 ) size = s_sizes[h];
	// must be legit now
	if ( size <= 0 ) { char *xx=NULL;*xx=0; }
	// . bitch is sizes don't match
	// . delete operator does not provide a size now (it's -1)
	if ( s_sizes[h] != size ) {
		log(
		    "mem: rmMem: Freeing %li should be %li. (%s)",
		    size,s_sizes[h],note);
		if ( g_isYippy ) {
			size = s_sizes[h];
			goto keepgoing;
		}
#ifndef _VALGRIND_
		char *xx = NULL;
		*xx = 0;
#endif
		//sleep(50000);
		// unlock for threads
		//pthread_mutex_unlock ( &s_lock );
		return false;
	}

 keepgoing:
	// debug
	if ( size > MINMEM && g_conf.m_logDebugMemUsage )
		log(LOG_INFO,"mem: rmMem (%li): ptr=0x%lx %s.",size,(long)mem,note);

	//
	// we do this here now since we always call rmMem() now
	//
	// decrement freed mem
	m_used -= size;
	// new/delete does not have padding because the "new"
	// function can't support it right now
	//if ( ! isnew ) m_used -= (UNDERPAD + OVERPAD);
	m_numAllocated--;

	// check for breeches, if we don't do it here, we won't be able
	// to check this guy for breeches later, cuz he's getting 
	// removed
	if ( ! isnew ) printBreech ( h , 1 );
	// empty our bucket, and point to next bucket after us
	s_mptrs[h++] = NULL;
	// dec the count
	s_n--;
	// wrap if we need to
	if ( h >= m_memtablesize ) h = 0;
	// var decl.
	unsigned long k;
	// shit after us may has to be rehashed in case it chained over us
	while ( s_mptrs[h] ) {
		// get mem ptr in bucket #h
		unsigned long mem = (unsigned long)s_mptrs[h];
		// find the most wanted bucket for this mem ptr
		u = (unsigned long)mem * (unsigned long)0x4bf60ade;
		k= u % (unsigned long)m_memtablesize;
		// if it's in it, continue
		if ( k == h ) { h++; continue; }
		// otherwise, move it back to fill the gap
		s_mptrs[h] = NULL;
		// dec count
		//s_n--;
		// if slot #k is full, chain
		for ( ; s_mptrs[k] ; )
			if ( ++k >= m_memtablesize ) k = 0;
		// re-add it to table
		s_mptrs[k] = (void *)mem;
		s_sizes[k] = s_sizes[h];
		s_isnew[k] = s_isnew[h];
		memcpy(&s_labels[k*16],&s_labels[h*16],16);
		// try next bucket now
		h++;
		// wrap if we need to
		if ( h >= m_memtablesize ) h = 0;
	}

	//validate();

	// unlock for threads
	//pthread_mutex_unlock ( &s_lock );
	return true;
}

long Mem::validate ( ) {
	if ( ! s_mptrs ) return 1;
	// stock up "p" and compute total bytes alloced
	long long total = 0;
	long count = 0;
	for ( long i = 0 ; i < (long)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		total += s_sizes[i];
		count++;
	}
	// see if it matches
	if ( total != m_used ) { char *xx=NULL;*xx=0; }
	if ( count != m_numAllocated ) { char *xx=NULL;*xx=0; }
	return 1;
}


long Mem::getMemSlot ( void *mem ) {
	// hash into table
	unsigned long u = (unsigned long)mem * (unsigned long)0x4bf60ade;
	unsigned long h = u % (unsigned long)m_memtablesize;
	// . chain to an empty bucket
	// . CAUTION: loops forever if no empty bucket
	while ( s_mptrs[h] && s_mptrs[h] != mem ) {
		h++;
		if ( h == m_memtablesize ) h = 0;
	}
	// if not found, return -1
	if ( ! s_mptrs[h] ) return -1;
	return h;
}


int Mem::printBreech ( long i , char core ) {
	// skip if empty
	if ( ! s_mptrs    ) return 0;
	if ( ! s_mptrs[i] ) return 0;
	// skip if isnew is true, no padding there
	if ( s_isnew[i] ) return 0;

	// if no label!
	if ( ! s_labels[i*16] ) 
		log(LOG_LOGIC,"mem: NO label found.");
	// do not test "Stack" allocated in Threads.cpp because it
	// uses mprotect() which messes up the magic chars
	if ( s_labels[i*16+0] == 'T' &&
	     s_labels[i*16+1] == 'h' &&
	     !strcmp(&s_labels[i*16  ],"ThreadStack" ) ) return 0;
	char flag = 0;
	// check for underruns
	char *mem = (char *)s_mptrs[i];
	char *bp = NULL;
	for ( long j = 0 ; j < UNDERPAD ; j++ ) {
		if ( (unsigned char)mem[0-j-1] == MAGICCHAR ) continue;
		log(LOG_LOGIC,"mem: underrun at %lx loff=%li size=%li "
		    "i=%li note=%s",
		    (long)mem,0-j-1,(long)s_sizes[i],i,&s_labels[i*16]);

		// mark it for freed mem re-use check below
		if ( ! bp ) bp = &mem[0-j-1];

		// now scan the whole hash table and find the mem buffer
		// just before that! but only do this once
		if ( flag == 1 ) continue;
		unsigned long min = 0;
		long mink = -1;
		for ( long k = 0 ; k < (long)m_memtablesize ; k++ ) {
			// skip empties
			if ( ! s_mptrs[k] ) continue;
			// do not look at mem after us
			if ( (unsigned long)s_mptrs[k] >= (unsigned long)mem ) 
				continue;
			// get min diff
			if ( mink != -1 && (unsigned long)s_mptrs[k] < min ) 
				continue;
			// new winner
			min = (unsigned long)s_mptrs[k];
			mink = k;
		}
		// now report it
		if ( mink == -1 ) continue;
		log("mem: possible breeching buffer=%s dist=%li",
		    &s_labels[mink*16],
		    (unsigned long)mem-
		  ((unsigned long)s_mptrs[mink]+(unsigned long)s_sizes[mink]));
		flag = 1;
	}		    

	// check for overruns
	long size = s_sizes[i];
	for ( long j = 0 ; j < OVERPAD ; j++ ) {
		if ( (unsigned char)mem[size+j] == MAGICCHAR ) continue;
		log(LOG_LOGIC,"mem: overrun  at %lx roff=%li note=%s",
		    (long)mem,j,&s_labels[i*16]);

		// mark it for freed mem re-use check below
		if ( ! bp ) bp = &mem[size+j];

		// now scan the whole hash table and find the mem buffer
		// just before that! but only do this once
		if ( flag == 1 ) continue;
		unsigned long min = 0;
		long mink = -1;
		for ( long k = 0 ; k < (long)m_memtablesize ; k++ ) {
			// skip empties
			if ( ! s_mptrs[k] ) continue;
			// do not look at mem before us
			if ( (unsigned long)s_mptrs[k] <= (unsigned long)mem ) 
				continue;
			// get min diff
			if ( mink != -1 && (unsigned long)s_mptrs[k] > min ) 
				continue;
			// new winner
			min = (unsigned long)s_mptrs[k];
			mink = k;
		}
		// now report it
		if ( mink == -1 ) continue;
		log("mem: possible breeching buffer=%s dist=%li",
		    &s_labels[mink*16],
		    (long)s_mptrs[mink]-((long)mem+s_sizes[i]));
		flag = 1;
	}
	
	// return now if no breach
	if ( flag == 0 ) return 1;

	// need this
	if ( ! bp ) { char *xx=NULL;*xx=0; }

	/*
	//
	// check for freed memory re-use
	//
	FreeInfo *fi  = s_cursor;
	FreeInfo *end = s_cursorEnd;
	if ( ! s_looped ) end = s_cursor;
	for ( ; ; ) {
		// decrement
		fi--;
		// wrap?
		if ( fi < s_cursorStart ) {
			// do not wrap if did not loop though!
			if ( ! s_looped ) break;
			// otherwise, wrap back to top
			fi = s_cursorEnd - 1;
		}
		// see if contains an overwritten magic char
		if ( bp >= (char *)fi->m_ptr &&
		     bp <  (char *)fi->m_ptr + fi->m_size ) {
			log("mem: reused freed buffer note=%s",
			    fi->m_note);
			break;
		}
		// all done?
		if ( fi == s_cursor ) break;
	}
	*/

	if ( flag && core ) { char *xx = NULL; *xx = 0; }
	return 1;
}

// check all allocated memory for buffer under/overruns
int Mem::printBreeches ( char core ) {
	if ( ! s_mptrs ) return 0;
	// do not bother if no padding at all
	if ( (long)UNDERPAD == 0 && (long)OVERPAD == 0 ) return 0;
	// loop through the whole mem table
	for ( long i = 0 ; i < (long)m_memtablesize ; i++ )
		// only check if non-empty
		if ( s_mptrs[i] ) printBreech ( i , core );
	return 0;
}


int Mem::printMem ( ) {
	// has anyone breeched their buffer?
	printBreeches ( 0 ) ;

	// print table entries sorted by most mem first
	long *p = (long *)sysmalloc ( m_memtablesize * 4 );
	if ( ! p ) return 0;
	// stock up "p" and compute total bytes alloced
	long long total = 0;
	long np    = 0;
	for ( long i = 0 ; i < (long)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		total += s_sizes[i];
		p[np++] = i;
	}
	// . sort p by size
	// . skip this because it blocks for like 30 seconds
	bool flag ;
	goto skipsort;
	flag = 1;
	while ( flag ) {
		flag = 0;
		for ( long i = 1 ; i < np ; i++ ) {
			long a = p[i-1];
			long b = p[i  ];
			if ( s_sizes[a] <= s_sizes[b] ) continue;
			// switch these 2
			p[i  ] = a;
			p[i-1] = b;
			flag = 1;
		}
	}
 skipsort:

	// print out table sorted by sizes
	for ( long i = 0 ; i < np ; i++ ) {
		long a = p[i];
		log(LOG_INFO,"mem: %05li) %li %lx %s", 
		    i,s_sizes[a] , (long)s_mptrs[a] , &s_labels[a*16] );
	}
	sysfree ( p );
	log(LOG_INFO,"mem: # current objects allocated now = %li", np );
	log(LOG_INFO,"mem: totalMem alloced now = %lli", total );
	//log("mem: max alloced at one time = %li", (long)(m_maxAlloced));
	log(LOG_INFO,"mem: Memory allocated now: %lli.\n", getUsedMem() );
	log(LOG_INFO,"mem: Num allocs %li.\n", m_numAllocated );
	return 1;
}

void *Mem::gbmalloc ( int size , const char *note ) {
	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;
	
	// random oom testing
	//static long s_mcount = 0;
	//s_mcount++;
	if ( g_conf.m_testMem && (rand() % 100) < 2 ) { 
		//if ( s_mcount > 1055 && (rand() % 1000) < 2 ) { 
		g_errno = ENOMEM; 
		log("mem: malloc-fake(%i,%s): %s",size,note,
		    mstrerror(g_errno));
		return NULL;
	} 

 retry:
	// don't go over max
	if ( m_used + size + UNDERPAD + OVERPAD >= m_maxMem ) {
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = ENOMEM;
		log("mem: malloc(%i): Out of memory", size );
		return NULL;
	}
	if ( size < 0 ) {
		g_errno = EBADENGINEER;
		log("mem: malloc(%i): Bad value.", size );
		char *xx = NULL; *xx = 0;
		return NULL;
	}

	void *mem;

	// to find bug that cores on malloc do this
	//printBreeches(true);
	//g_errno=ENOMEM;return (void *)log("Mem::malloc: reached mem limit");}
#ifdef EFENCE
	mem = getElecMem(size+UNDERPAD+OVERPAD);

	// conditional electric fence?
#elif EFENCE_SIZE
	if ( size >= EFENCE_SIZE )
		mem = getElecMem(size+0+0);
	else
		mem = (void *)sysmalloc ( size + UNDERPAD + OVERPAD );
#else			

	//void *mem = dlmalloc ( size );
	mem = (void *)sysmalloc ( size + UNDERPAD + OVERPAD );
#endif
	// initialization debug
	//char *pend = (char *)mem + UNDERPAD + size;
	//for ( char *p = (char *)mem + UNDERPAD ; p < pend ; p++ )
	//	*p = (char )(rand() % 256);
	// test mem fragmentation email
	//static long s_count = 0;
	//s_count++;
	//if ( s_count > 1500 && (rand() % 100) < 2 ) { 
	//	log("mem: malloc-system(%i,%s): %s",size,note,
	//	    mstrerror(g_errno));
	//	mem = NULL;
	//} 
	// special log
	//if ( size > 1000000 ) 
	//	log("allocated %i. (%s) current=%lli",size,note,m_used);
	//void *mem = s_pool.malloc ( size ); 
	long memLoop = 0;
mallocmemloop:
	if ( ! mem && size > 0 ) {
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = errno;
		static long long s_lastTime;
		static long s_missed = 0;
		long long now = gettimeofdayInMillisecondsLocal();
		long long avail = (long long)m_maxMem - 
			(long long)m_used;
		if ( now - s_lastTime >= 1000LL ) {
			log("mem: system malloc(%i,%s) availShouldBe=%lli: "
			    "%s (%s) (ooms suppressed since "
			    "last log msg = %li)",
			    size+UNDERPAD+OVERPAD,
			    note,
			    avail,
			    mstrerror(g_errno),
			    note,
			    s_missed);
			s_lastTime = now;
			s_missed = 0;
		}
		else 
			s_missed++;
		// to debug oom issues:
		//char *xx=NULL;*xx=0;
		// send an email alert if this happens! it is a sign of
		// "memory fragmentation"
		//static bool s_sentEmail = false;
		// stop sending these now... seems to be problematic. says
		// 160MB is avail and can't alloc 20MB...
		static bool s_sentEmail = true;
		// assume only 90% is really available because of 
		// inefficient mallocing
		avail = (long long)((float)avail * 0.80);
		// but if it is within about 15MB of what is theoretically
		// available, don't send an email, because there is always some
		// minor fragmentation
		if ( ! s_sentEmail && avail > size ) {
			s_sentEmail = true;
			char msgbuf[1024];
			Host *h = g_hostdb.m_myHost;
			snprintf(msgbuf, 1024,
				 "Possible memory fragmentation "
				 "on host #%li %s",
				 h->m_hostId,h->m_note);
			log(LOG_WARN, "query: %s",msgbuf);
			g_pingServer.sendEmail(NULL, msgbuf,true,true);
		}
		return NULL;
	}
	if ( (unsigned long)mem < 0x00010000 ) {
#ifdef EFENCE
		void *remem = getElecMem(size);
#else
		void *remem = sysmalloc(size);
#endif
		log ( LOG_WARN, "mem: Caught low memory allocation at %08lx, "
				"reallocated to %08lx",
				(unsigned long)mem, (unsigned long)remem );
#ifdef EFENCE
		freeElecMem (mem);
#else
		sysfree(mem);
#endif
		mem = remem;
		memLoop++;
		if ( memLoop > 100 ) {
			log ( LOG_WARN, "mem: Attempted to reallocate low "
					"memory allocation 100 times, "
					"aborting and returning NOMEM." );
			g_errno = ENOMEM;
			return NULL;
		}
		goto mallocmemloop;
	}

	addMem ( (char *)mem + UNDERPAD , size , note , 0 );
	return (char *)mem + UNDERPAD;
}

void *Mem::gbcalloc ( int size , const char *note ) {
	void *mem = gbmalloc ( size , note );
	// init it
	if ( mem ) memset ( mem , 0, size );
	return mem;
}

void *Mem::gbrealloc ( void *ptr , int oldSize , int newSize ,
			const char *note ) {
	// return dummy values since realloc() returns NULL if failed
	if ( oldSize == 0 && newSize == 0 ) return (void *)0x7fffffff;
	// do nothing if size is same
	if ( oldSize == newSize ) return ptr;
	// crazy?
	if ( newSize < 0 ) { char *xx=NULL;*xx=0; }
	// if newSize is 0...
	if ( newSize == 0 ) { 
		//mfree ( ptr , oldSize , note );
		gbfree ( ptr , oldSize , note );
		return (void *)0x7fffffff;
	}
	// don't do more than 128M at a time, it hurts pthread_create
	//if ( newSize > MAXMEMPERCALL ) {
	//	g_errno = ENOMEM;
	//	log("Mem::realloc(%i): can only alloc %li bytes per call",
	//	    newSize,MAXMEMPERCALL);
	//	return NULL;
	//}
 retry:
	// don't go over max
	if ( m_used + newSize - oldSize >= m_maxMem ) {
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = ENOMEM;
		log("mem: realloc(%i,%i): Out of memory.",oldSize,newSize);
		return NULL;
	}
	// if oldSize is 0, use our malloc() instead
	if ( oldSize == 0 ) return gbmalloc ( newSize , note );

	char *mem;

	// even though size may be < 100k for EFENCE_SIZE, do it this way
	// for simplicity...
#if defined(EFENCE) || defined(EFENCE_SIZE)
	mem = (char *)mmalloc ( newSize , note );
	if ( ! mem ) return NULL;
	// copy over to it
	memcpy ( mem , ptr , oldSize );
	// free the old
	mfree ( ptr , oldSize , note );
	// done
	return mem;
#endif

	// assume it will be successful. we can't call rmMem() after
	// calling sysrealloc() because it will mess up our MAGICCHAR buf
	rmMem  ( ptr , oldSize , note );

	// . do the actual realloc
	// . CAUTION: don't pass in 0x7fffffff in as "ptr" 
	// . this was causing problems
	mem = (char *)sysrealloc ( (char *)ptr - UNDERPAD , 
				   newSize + UNDERPAD + OVERPAD);

	// remove old guy on sucess
	if ( mem ) {
		addMem ( (char *)mem + UNDERPAD , newSize , note , 0 );
		char *returnMem = mem + UNDERPAD;
		// set magic char bytes for mem
		for ( long i = 0 ; i < UNDERPAD ; i++ )
			returnMem[0-i-1] = MAGICCHAR;
		for ( long i = 0 ; i < OVERPAD ; i++ )
			returnMem[0+newSize+i] = MAGICCHAR;
		return returnMem;
	}

	// ok, just try using malloc then!
	mem = (char *)mmalloc ( newSize , note );
	// bail on error
	if ( ! mem ) {
		// restore the original buf we tried to grow
		addMem ( ptr , oldSize , note , 0 );
		errno = g_errno = ENOMEM;
		return NULL;
	}
	// log a note
	log(LOG_INFO,"mem: had to use malloc/memcpy instead of "
	    "realloc.");
	// copy over to it
	memcpy ( mem , ptr , oldSize );
	// we already called rmMem() so don't double call
	sysfree ( (char *)ptr - UNDERPAD );	
	// free the old. this was coring because it was double calling rmMem()
	//mfree ( ptr , oldSize , note );
	// mmalloc() and mfree() should have taken care of it
	return mem;
}

char *Mem::dup ( const void *data , long dataSize , const char *note ) {
	// keep it simple
	char *mem = (char *)mmalloc ( dataSize , note );
	if ( mem ) memcpy ( mem , data , dataSize );
	return mem;
}

void Mem::gbfree ( void *ptr , int size , const char *note ) {
	// don't let electric fence zap us
	//if ( size == 0 && ptr==(void *)0x7fffffff) return;
	if ( size == 0 ) return;
	// huh?
	if ( ! ptr ) return;

	//if ( size==65536 && strcmp(note,"UdpServer")==0)
	//	log("hey");

	// . get how much it was from the mem table
	// . this is used for alloc/free wrappers for zlib because it does
	//   not give us a size to free when it calls our mfree(), so we use -1
	long slot = g_mem.getMemSlot ( ptr );
	if ( slot < 0 ) {
		log(LOG_LOGIC,"mem: could not find slot (note=%s)",note);
		log(LOG_LOGIC,"mem: FIXME!!!");
		// return for now so procog does not core all the time!
		return;
		//char *xx = NULL; *xx = 0;
	}

	bool isnew = s_isnew[slot];

#ifdef EFENCE
	// this does a delayed free so do not call rmMem() just yet
	freeElecMem ((char *)ptr - UNDERPAD );
	return;
#endif

#ifdef EFENCE_SIZE
	if ( size == -1 ) size = s_sizes[slot];
	if ( size >= EFENCE_SIZE ) {
		freeElecMem ((char *)ptr - 0 );
		return;
	}
#endif	

	// if this returns false it was an unbalanced free
	if ( ! rmMem ( ptr , size , note ) ) return;

	if ( isnew ) sysfree ( (char *)ptr );
	else         sysfree ( (char *)ptr - UNDERPAD );
}

long getLowestLitBitLL ( unsigned long long bits ) {
	// count how many bits we have to shift so that the first bit is 0
	long shift = 0;
	while ( (bits & (1LL<<shift)) == 0 && (shift < 63 ) ) shift++;
	return shift;
}

unsigned long getHighestLitBitValue ( unsigned long bits ) {
	// count how many bits we have to shift so that the first bit is 0
	unsigned long highest =  0;
	for ( long shift = 0 ; shift < 32 ; shift++ ) 
		if ( bits & (1<<shift) ) highest = (1 << shift);
	return highest;
}

unsigned long long getHighestLitBitValueLL ( unsigned long long bits ) {
	// count how many bits we have to shift so that the first bit is 0
	unsigned long long highest =  0;
	for ( long shift = 0 ; shift < 64 ; shift++ ) 
		if ( bits & (1LL<<shift) ) highest = (1LL << shift);
	return highest;
}

// TODO: speed up
long long htonll ( unsigned long long a ) {
	long long b;
	unsigned int int0 = htonl ( ((unsigned long *)&a)[0] );
	unsigned int int1 = htonl ( ((unsigned long *)&a)[1] );

	((unsigned int *)&b)[0] = int1;
	((unsigned int *)&b)[1] = int0;
	return b;
}

// just swap 'em back
long long ntohll ( unsigned long long a ) { 
	return htonll ( a );
}

key_t htonkey ( key_t key ) {
	key_t newKey;
	newKey.n0 = htonll ( key.n0 );
	newKey.n1 = htonl  ( key.n1 );
	return newKey;
}

key_t ntohkey ( key_t key ) {
	key_t newKey;
	newKey.n0 = ntohll ( key.n0 );
	newKey.n1 = ntohl  ( key.n1 );
	return newKey;
}

// this can be sped up drastiaclly if needed
unsigned long reverseBits ( unsigned long x ) {
	// init groupId
	unsigned long y = 0;
	// go through each bit in hostId
	for ( long srcBit = 0 ; srcBit < 32 ; srcBit++ ) {
		// get status of bit # srcBit
		bool isOn = x & (1 << srcBit);
		// set destination bit in the groupId
		if ( isOn ) y |= (1 << (31 - srcBit) );
	}
	// return the bit reversal of x
	return y;
}

// . returns -1 if dst < src, 0 if equal, +1 if dst > src
// . bit #0 is the least significant bit on this little endian machine
// . TODO: should we speed this up?
long membitcmp ( void *dst     ,
		 long  dstBits ,   // bit offset into "dst"
		 void *src     ,
		 long  srcBits ,   // bit offset into "src"
		 long  nb      ) { // # bits to compare
	char *s;
	char *d;
	char  smask;
	char  dmask;
	for ( long b = nb - 1 ; b >= 0 ; b-- ) {
		s     =(char *)src + ((b + srcBits) >> 3 );
		d     =(char *)dst + ((b + dstBits) >> 3 );
		smask = 0x01 << ((b + srcBits) & 0x07);
		dmask = 0x01 << ((b + dstBits) & 0x07);
		if ( *s & smask ) { if ( ! (*d &  dmask) ) return -1; }
		else              { if (    *d &  dmask  ) return  1; }
	}
	return 0;
}

// . returns # of bits in common
// . bit #0 is the least significant bit on this little endian machine
// . TODO: should we speed this up?
long membitcmp2 ( void *dst     ,
		  long  dstBits ,   // bit offset into "dst"
		  void *src     ,
		  long  srcBits ,   // bit offset into "src"
		  long  nb      ) { // # bits to compare
	char *s;
	char *d;
	char  smask;
	char  dmask;
	long  nc = 0;
	for ( long b = nb - 1 ; b >= 0 ; b-- ) {
		s     =(char *)src + ((b + srcBits) >> 3 );
		d     =(char *)dst + ((b + dstBits) >> 3 );
		smask = 0x01 << ((b + srcBits) & 0x07);
		dmask = 0x01 << ((b + dstBits) & 0x07);
		if ( *s & smask ) { if ( ! (*d &  dmask) ) return nc; }
		else              { if (    *d &  dmask  ) return nc; }
		nc++;
	}
	return nc;
}

// . bit #0 is the least significant bit on this little endian machine
// . TODO: should we speed this up?
// . we start copying at LOW bit
void membitcpy1 ( void *dst     ,
		  long  dstBits ,   // bit offset into "dst"
		  void *src     ,
		  long  srcBits ,   // bit offset into "src"
		  long  nb      ) { // # bits to copy
	// debug msg
	//log("nb=%li",nb);
	// if src and dst overlap, it matters if b moves up or down
	char *s;
	char *d;
	char  smask;
	char  dmask;
	for ( long b = 0 ; b < nb ; b++ ) {
		s     =(char *)src + ((b + srcBits) >> 3 );
		d     =(char *)dst + ((b + dstBits) >> 3 );
		smask = 0x01 << ((b + srcBits) & 0x07);
		dmask = 0x01 << ((b + dstBits) & 0x07);
		if ( *s & smask ) *d |=  dmask;
		else              *d &= ~dmask;
	}
}

// like above, but we start copying at HIGH bit so you can
// shift your recs without interference
void membitcpy2 ( void *dst     ,
		  long  dstBits ,   // bit offset into "dst"
		  void *src     ,
		  long  srcBits ,   // bit offset into "src"
		  long  nb      ) { // # bits to copy
	// if src and dst overlap, it matters if b moves up or down
	char *s;
	char *d;
	char  smask;
	char  dmask;
	for ( long b = nb - 1 ; b >= 0 ; b-- ) {
		s     =(char *)src + ((b + srcBits) >> 3 );
		d     =(char *)dst + ((b + dstBits) >> 3 );
		smask = 0x01 << ((b + srcBits) & 0x07);
		dmask = 0x01 << ((b + dstBits) & 0x07);
		if ( *s & smask ) *d |=  dmask;
		else              *d &= ~dmask;
	}
}

long Mem::printBits  ( void *src, long srcBits , long nb ) {
	char *s;
	char  smask;
	fprintf(stdout,"low %li bits = ",nb);
	for ( long b = 0 ; b < nb ; b++ ) {
		s     =(char *)src + ((b + srcBits) >> 3 );
		smask = 0x01 << ((b + srcBits) & 0x07);
		if ( *s & smask ) fprintf(stdout,"1");
		else              fprintf(stdout,"0");
	}
	fprintf(stdout,"\n");
	return 0;
}

// ass = async signal safe, dumb ass
void memset_ass ( register void *dest , register const char c , long len ) {
	register char *end  = (char *)dest + len;
	// JAB: so... the optimizer should take care of the extra
	// register declaration for d, below...  see note below.
	register char *d    = (char *)dest;
	// JAB: gcc-3.4 did not like the cast in the previous version
	// while ( dest < end ) *((char *)dest)++ = c;
	while ( d < end ) { *d++ = c; }
}

void memset_nice( register void *dest , register const char c , long len ,
		  long niceness ) {
	char *end  = (char *)dest + len;
	// JAB: so... the optimizer should take care of the extra
	// register declaration for d, below...  see note below.
	register char *d    = (char *)dest;
	// JAB: gcc-3.4 did not like the cast in the previous version
	// while ( dest < end ) *((char *)dest)++ = c;
 loop:
	register char *seg = d + 5000;
	if ( seg > end ) seg = end;
	QUICKPOLL ( niceness );
	while ( d < seg ) { *d++ = c; }
	QUICKPOLL ( niceness );
	// do more?
	if ( d < end ) goto loop;
}


// . TODO: avoid byteCopy by copying remnant bytes
// . ass = async signal safe, dumb ass
// . NOTE: src/dest should not overlap in this version of memcpy
void memcpy_ass ( register void *dest2, register const void *src2, long len ) {
	// for now keep it simple!!
	len--;
	while ( len >= 0 ) { 
		((char *)dest2)[len] = ((char *)src2)[len]; 
		len--; 
	}
	/*
	// debug test
	//memcpy ( dest2 , src2 , len );
	//return;
	// the end for the fast copy by word with partially unrolled loop
	register long *dest = (long *)dest2;
	register long *src  = (long *)src2 ;
	register long *end  = dest + (len >> 2);
	long *oldEnd = end;
	//long long start = gettimeofdayInMilliseconds();
	//fprintf(stderr,"ln=%li,dest=%li,src=%li\n",len,(long)dest,(long)src);
	if ((len&0x03)!=0 || ((long)dest&0x03)!=0 || ((long)src&0x03)!=0 ) 
		goto byteCopy;
	// truncate n so we can unroll this loop
	end = (long *)  ( (long)end & 0x07 );
	while ( dest < end ) { 
		dest[0] = src[0]; 
		dest[1] = src[1]; 
		dest[2] = src[2]; 
		dest[3] = src[3]; 
		dest[4] = src[4]; 
		dest[5] = src[5]; 
		dest[6] = src[6]; 
		dest[7] = src[7]; 
		dest += 8;
		src  += 8;
	}
	// copy remaining 7 or less longs
	while ( dest < oldEnd ) { *dest = *src; dest++; src++; }
	//fprintf(stderr,"t=%li\n",(long)(gettimeofdayInMilliseconds()-start));
	return;
 byteCopy:
	len--;
	while ( len >= 0 ) { dest2[len] = src2[len]; len--; }
	*/
}

// Check the current stack usage
long Mem::checkStackSize() {
	if ( !m_stackStart )
		return 0;
	char final;
	char *stackEnd = &final;
	long size = m_stackStart - stackEnd;
	log("gb: stack size is %li",size);
	return size;
}

// Set the stack's start point (called in main.cpp)
void Mem::setStackPointer( char *ptr ) {
	m_stackStart = ptr;
}

char g_a[256] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 
		  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
		  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
		  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
		  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
		  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
		  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
		  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
		  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
		  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
		  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
		  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
		  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
		  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
		  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
		  4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};


#include "Msg20.h"

bool freeCacheMem() {
	// returns true if it did free some stuff
	//if ( resetMsg20Cache() ) {
	//	log("mem: freed cache mem.");
	//	return true;
	//}
	return false;
}

#define MAXBEST 50

// scan all allocated memory, assume each byte is starting a character ptr,
// and find such ptrs closes to "target"
long Mem::findPtr ( void *target ) {
	if ( ! s_mptrs ) return 0;
	long maxDelta = 0x7fffffff;
	long topDelta[MAXBEST];
	long topOff  [MAXBEST];
	long topi    [MAXBEST];
	long nt = 0;
	long minnt = 0;
	long i;
	// loop through the whole mem table
	for ( i = 0 ; i < (long)m_memtablesize ; i++ ) {
		// only check if non-empty
		if ( ! s_mptrs[i] ) continue;
		// get size to scan
		char *p    = (char *)s_mptrs[i];
		long  size = s_sizes[i];
		char *pend = p + size;
		long bestDelta = 0x7fffffff;
		long bestOff   = 0x7fffffff;
		char *note = &s_labels[i*16];
		// skip thread stack
		if ( strcmp(note,"ThreadStack") == 0 ) 
			continue;
		// scan that
		for ( ; p +4 < pend ; p++ ) {
			// get ptr it might have
			char *pp = (char *)(*(long *)p);
			// delta from target
			long delta = (unsigned long)pp - (unsigned long)target;
			// make positive
			if ( delta < 0 ) delta *= -1;
			// is it a min?
			//if ( delta > 100 ) continue;
			// get top 10
			if ( delta < bestDelta ) {
				bestDelta = delta;
				bestOff   = (long)p - (long)(s_mptrs[i]);
			}
		}
		// bail if not good enough
		if ( bestDelta >= maxDelta ) continue;
		if ( bestDelta > 50000 ) continue;
		// add to top list
		if ( nt < MAXBEST ) {
			topDelta[nt] = bestDelta;
			topOff  [nt] = bestOff;
			topi    [nt] = i;
			nt++;
		}
		else {
			topDelta[minnt] = bestDelta;
			topOff  [minnt] = bestOff;
			topi    [minnt] = i;
		}
		// compute minnt
		minnt = 0;
		for ( long j = 1 ; j < nt ; j++ )
			if ( topDelta[j] > topDelta[minnt] )
				minnt = j;
	}
	// print out top MAXBEST. "note" is the note attached to the allocated 
	// memory the suspicious write ptr is in
	for ( long j = 0 ; j < nt ; j++ ) {
		// get it
		long bi = topi[j];
		char *note = (char *)&s_labels[bi*16];
		if ( ! note ) note = "unknown";
		long *x = (long *)((char *)s_mptrs[bi] + topOff[j]);
		log("mem: topdelta=%li bytes away from corrupted mem. note=%s "
		    "memblock=%li and memory of ptr is %li bytes into that "
		    "memblock. and ptr is pointing to 0x%lx(%lu)",
		    topDelta[j],note,bi,topOff[j], *x,*x);
	}

	return 0;
}

//#include <limits.h>    /* for MEMPAGESIZE */
#define MEMPAGESIZE ((unsigned long)(8*1024))

void *getElecMem ( long size ) {
	// a page above OR a page below
	// let's go below this time since that seems to be the problem

#ifdef _CHECKUNDERFLOW_
	// how much to alloc
	// . assume sysmalloc returs one byte above a page, so we need
	//   MEMPAGESIZE-1 bytes to move p up to page boundary, another
	//   MEMPAGESIZE bytes for protected page, then the actual mem,
	//   THEN possibly another MEMPAGESIZE-1 bytes to hit the next page
	//   boundary for protecting the "freed" mem below, but can get
	//   by with (MEMPAGESIZE-(size%MEMPAGESIZE)) more
	long need = size + 8 + MEMPAGESIZE + MEMPAGESIZE ;
	// want to end on a page boundary too!
	need += (MEMPAGESIZE-(size%MEMPAGESIZE));
	// get that
	char *realMem = (char *)sysmalloc ( need );	
	if ( ! realMem ) return NULL;
	// set it all to 0x11
	memset ( realMem , 0x11 , need );
	// use this
	char *realMemEnd = realMem + need;
	// parser
	char *p = realMem;
	// align p DOWN to nearest 8k boundary
	long remainder = (uint32_t)realMem % MEMPAGESIZE;
	// complement
	remainder = MEMPAGESIZE - remainder;
	// and add to ptr to be aligned on 8k boundary
	p += remainder;
	// save that
	char *protMem = p;
	// skip that
	p += MEMPAGESIZE;
	// save this
	char *returnMem = p;
	// store the ptrs
	*(char **)(returnMem- 4) = realMem;
	*(char **)(returnMem- 8) = realMemEnd;
	// protect that after we wrote our ptr
	if ( mprotect ( protMem , MEMPAGESIZE , PROT_NONE) < 0 )
		log("mem: mprotect failed: %s",mstrerror(errno));
	// advance over user data
	p += size;
	// now when we free this it should all be protected, so make sure
	// we have enough room on top
	long leftover = MEMPAGESIZE  - ((uint32_t)p % MEMPAGESIZE);
	// skip that
	p += leftover;
	// inefficient?
	if ( realMemEnd - p > (long)MEMPAGESIZE ) { char *xx=NULL;*xx=0;}
	// ensure we do not breach
	if ( p > realMemEnd ) { char *xx=NULL;*xx=0; }
	// test it, this should core
	//protmem[0] = 32;
	// return that for them
	return returnMem;
#else
	// how much to alloc
	long need = size + 8 + MEMPAGESIZE + MEMPAGESIZE + MEMPAGESIZE;
	// get that
	char *realMem = (char *)sysmalloc ( need );	
	if ( ! realMem ) return NULL;
	// set it all to 0x11
	memset ( realMem , 0x11 , need );
	// use this
	char *realMemEnd = realMem + need;
	// get the end of it
	char *end = realMemEnd;
	// back down from what we need
	end -= MEMPAGESIZE;
	// get remainder from that
	long remainder = (uint32_t)end % MEMPAGESIZE;
	// back down to that
	char *protMem = end - remainder;
	// get return mem
	char *returnMem = protMem - size;
	// back beyond that
	long leftover = (uint32_t)returnMem % MEMPAGESIZE;
	// back up
	char *p = returnMem - leftover;
	// we are now on a page boundary, so we can protect this mem
	// after we "free" it below
	if ( p < realMem ) { char *xx=NULL;*xx=0; }
	// store mem ptrs before protecting
	*(char **)(returnMem- 4) = realMem;
	*(char **)(returnMem- 8) = realMemEnd;
	// sanity
	if ( returnMem - 8 < realMem ) { char *xx=NULL;*xx=0; }
	// protect that after we wrote our ptr
	if ( mprotect ( protMem , MEMPAGESIZE , PROT_NONE) < 0 )
		log("mem: mprotect failed: %s",mstrerror(errno));
	// test it, this should core
	//protmem[0] = 32;
	// return that for them
	return returnMem;
#endif
}

// stuff for detecting if a class frees memory and then re-uses it after
// freeing it...
class FreeInfo {
public:
	char *m_fakeMem;
	long  m_fakeSize;
	char *m_note;
	char *m_realMem;
	long  m_realSize;
	char *m_protMem;
	long  m_protSize;
};
static FreeInfo s_freeBuf[4000];
static FreeInfo *s_cursor      = &s_freeBuf[0];
static FreeInfo *s_cursorEnd   = &s_freeBuf[4000];
static FreeInfo *s_cursorStart = &s_freeBuf[0];
static bool      s_looped = false;
static FreeInfo *s_freeCursor  = &s_freeBuf[0];
static long long s_totalInRing = 0LL;

// . now we must unprotect before freeing
// . let's do delayed freeing because i think the nasty bug that is
//   corrupting malloc's space is overruning a freed buffer perhaps?
void freeElecMem ( void *fakeMem ) {
	// cast it
	char *cp = (char *)fakeMem;

	// get mem info from the hash table
	long h = g_mem.getMemSlot ( cp );
	if ( h < 0 ) { 
		log("mem: unbalanced free ptr");
		char *xx=NULL;*xx=0;
	}
	char *label    = &s_labels[((unsigned long)h)*16];
	long  fakeSize =  s_sizes[h];

#ifdef _CHECKUNDERFLOW_
	char *oldProtMem = cp - MEMPAGESIZE;
#else
	char *oldProtMem = cp + fakeSize;
#endif

	// unprotect it
	if ( mprotect ( oldProtMem , MEMPAGESIZE, PROT_READ|PROT_WRITE) < 0 )
		log("mem: munprotect failed: %s",mstrerror(errno));

	// now original memptr is right before "p" and we can
	// read it now that we are unprotected
	char *realMem    = *(char **)(cp-4);
	// set real mem end (no!?)
	char *realMemEnd = *(char **)(cp-8);

	// set it all to 0x99
	memset ( realMem , 0x99 , realMemEnd - realMem );

	// ok, back up to page boundary before us
	char *protMem = realMem + (MEMPAGESIZE - 
				   (((unsigned long)realMem) % MEMPAGESIZE));
	// get end point
	char *protEnd = realMemEnd - ((unsigned long)realMemEnd % MEMPAGESIZE);
	// sanity
	if ( protMem < realMem ) { char *xx=NULL;*xx=0; }
	if ( protMem - realMem > (long)MEMPAGESIZE ) { char *xx=NULL;*xx=0; }
	// before adding it into the ring, protect it
	if ( mprotect ( protMem , protEnd-protMem, PROT_NONE) < 0 )
		log("mem: mprotect2 failed: %s",mstrerror(errno));

	// add our freed memory to the freed ring
	if ( s_cursor > s_cursorEnd ) { char *xx=NULL;*xx=0; }

	// to avoid losing free info by eating our own tail
	if ( s_cursor == s_freeCursor && s_looped ) {
		// free it
		g_mem.rmMem ( s_freeCursor->m_fakeMem,
			      s_freeCursor->m_fakeSize,
			      s_freeCursor->m_note );
		// unprotect it
		if ( mprotect (s_freeCursor->m_protMem,
			       s_freeCursor->m_protSize,
			       PROT_READ|PROT_WRITE) < 0 )
			log("mem: munprotect2 failed: %s",mstrerror(errno));
		// get the original mem and nuke
		sysfree ( s_freeCursor->m_realMem );
		// dec count. use fake mem size
		s_totalInRing -= s_freeCursor->m_realSize;
		// wrap it if we need to
		if ( ++s_freeCursor == s_cursorEnd ) 
			s_freeCursor = s_cursorStart;
	}


	s_cursor->m_fakeMem  = cp;
	s_cursor->m_fakeSize = fakeSize;
	s_cursor->m_note     = label;
	s_cursor->m_realSize = realMemEnd - realMem;
	s_cursor->m_realMem  = realMem;
	s_cursor->m_protMem  = protMem;
	s_cursor->m_protSize = protEnd - protMem;

	// keep tabs on how much unfreed mem we need to free
	s_totalInRing += s_cursor->m_realSize;

	if ( ++s_cursor == s_cursorEnd ) {
		s_looped = true;
		s_cursor = s_cursorStart;
	}


	// now free begining at s_freeCursor 
	for ( ; s_freeCursor != s_cursor && s_totalInRing > 150000000 ; ) {
		// free it
		g_mem.rmMem ( s_freeCursor->m_fakeMem,
			      s_freeCursor->m_fakeSize,
			      s_freeCursor->m_note );
		// unprotect it
		if ( mprotect (s_freeCursor->m_protMem,
			       s_freeCursor->m_protSize,
			       PROT_READ|PROT_WRITE) < 0 )
			log("mem: munprotect2 failed: %s",mstrerror(errno));
		// get the original mem and nuke
		sysfree ( s_freeCursor->m_realMem );
		// dec count. use fake mem size
		s_totalInRing -= s_freeCursor->m_realSize;
		// wrap it if we need to
		if ( ++s_freeCursor == s_cursorEnd ) 
			s_freeCursor = s_cursorStart;
	}
}

// only Mem.cpp can call ::malloc, everyone else must call mmalloc() so
// we can keep tabs on memory usage.
#define malloc coreme
#define calloc coreme
#define realloc coreme
