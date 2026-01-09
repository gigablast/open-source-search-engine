#include "gb-include.h"

#include "Mem.h"

#include "Threads.h"
#include "SafeBuf.h"
#include "PingServer.h"
#include "Pages.h"

// uncomment this #define to electric fence just on umsg00 buffers:
//#define SPECIAL

// put me back
//#define EFENCE
//#define EFENCE_SIZE 50000

// uncomment this for EFENCE to do underflow checks instead of the
// default overflow checks
//#define CHECKUNDERFLOW

// only Mem.cpp can call ::malloc, everyone else must call mmalloc() so
// we can keep tabs on memory usage. in Mem.h we #define this to be coreme()

#undef malloc
#undef calloc
#undef realloc

bool g_inMemFunction = false;

#define sysmalloc ::malloc
#define sysrealloc ::realloc
#define sysfree ::free

// allocate an extra space before and after the allocated memory to try
// to catch sequential buffer underruns and overruns. if the write is way
// beyond this padding radius, chances are it will seg fault right then and
// there because it will hit a different PAGE, to be more sure we could
// make UNDERPAD and OVERPAD PAGE bytes, although the overrun could still write
// to another allocated area of memory and we can never catch it.
#if defined(EFENCE) || defined(EFENCE_SIZE)
static const int32_t UNDERPAD = 0;
static const int32_t OVERPAD  = 0;
#else
static const int32_t UNDERPAD = (int32_t)alignof(int64_t);
static const int32_t OVERPAD  = (int32_t)alignof(int64_t);
#endif

#define MAGICCHAR 0xda

class Mem g_mem;

extern bool g_isYippy;

bool freeCacheMem();

#if defined(EFENCE) || defined(EFENCE_SIZE) || defined(SPECIAL)
static void *getElecMem ( int32_t size ) ;
static void  freeElecMem ( void *p  ) ;
#endif

// a table used in debug to find mem leaks
static void **s_mptrs ;
static int32_t  *s_sizes ;
static char  *s_labels;
static char  *s_isnew;
static int32_t   s_n = 0;
static bool   s_initialized = 0;

// our own memory manager
void operator delete (void *ptr) throw () {
	// now just call this
	g_mem.gbfree ( (char *)ptr , -1 , NULL );
}

void operator delete [] ( void *ptr ) throw () {
	// now just call this
	// the 4 bytes are # of objects in the array
	g_mem.gbfree ( ((char *)ptr-4) , -1 , NULL );
}

#define MINMEM 6000000

void Mem::addnew ( void *ptr , int32_t size , const char *note ) {
	// 1 --> isnew
	addMem ( ptr , size , note , 1 );
}

void Mem::delnew ( void *ptr , int32_t size , const char *note ) {
	// we don't need to use mdelete() if checking for leaks is enabled
	// because the size of the allocated mem is in the hash table under
	// s_sizes[]. and the delete() operator is overridden below to
	// catch this.
	return;
}

// . global override of new and delete operators
// . seems like constructor and destructor are still called
// . just use to check if enough memory
// . before this just called mmalloc which sometimes returned NULL which 
//   would cause us to throw an unhandled signal. So for now I don't
//   call mmalloc since it is limited in the mem it can use and would often
//   return NULL and set g_errno to ENOMEM
void * operator new (size_t size) {
	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;

	// . fail randomly
	// . good for testing if we can handle out of memory gracefully
	if ( g_conf.m_testMem && (rand() % 100) < 2 ) {
		g_errno = ENOMEM; 
		log("mem: new-fake(%" UINT32 "): %s",(uint32_t)size, 
		    mstrerror(g_errno));
		throw std::bad_alloc(); 
	}

	int64_t max = g_conf.m_maxMem;

	// don't go over max
	if ( g_mem.m_used + (int32_t)size >= max &&
	     g_conf.m_maxMem > 1000000 ) {
		log("mem: new(%" UINT32 "): Out of memory.", (uint32_t)size );
		throw std::bad_alloc();
	}

	g_inMemFunction = true;

#ifdef EFENCE
	void *mem = getElecMem(size);
#elif EFENCE_SIZE
	void *mem;
	if ( size > EFENCE_SIZE )
		mem = getElecMem(size);
	else
		mem = sysmalloc ( size );
#else
	void *mem = sysmalloc ( size );
#endif

	g_inMemFunction = false;

	int32_t  memLoop = 0;
newmemloop:
	if ( ! mem && size > 0 ) {
		g_mem.m_outOfMems++;
		g_errno = errno;
		log("mem: new(%" INT32 "): %s",(int32_t)size,mstrerror(g_errno));
		throw std::bad_alloc();
	}
	if ( (PTRTYPE)mem < 0x00010000 ) {
#ifdef EFENCE
		void *remem = getElecMem(size);
#else
		void *remem = sysmalloc(size);
#endif
		log ( LOG_WARN, "mem: Caught low memory allocation "
		      "at %08" PTRFMT ", "
		      "reallocated to %08" PTRFMT , 
		      (PTRTYPE)mem,
		      (PTRTYPE)remem );
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
			throw std::bad_alloc();
		}
		goto newmemloop;
	}

	g_mem.addMem ( mem , size , "TMPMEM" , 1 );

	return mem;
}


//WARNING: Use this construct only when your datatype has a destructor!
//the compiler checks to see if a destructor is defined, if it is it
//will add 4 bytes to your requested size and put the number of objects
//in those bytes and returns a mem ptr at the malloced address + 4.
//this can screw up the subsequent call to addmem because the size and
//ptrs are off.
void * operator new [] (size_t size) {
	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;
	// . fail randomly
	// . good for testing if we can handle out of memory gracefully

	//static int32_t s_count = 0;
	//s_count++;
	//if ( s_count > 3000 && (rand() % 100) < 2 ) { 
	//	g_errno = ENOMEM; 
	//	log("mem: new-fake(%i): %s",size, mstrerror(g_errno));
	//	throw bad_alloc(); 
	//	// return NULL; }
	//} 
	
	int64_t max = g_conf.m_maxMem;

	// don't go over max
	if ( g_mem.m_used + (int32_t)size >= max &&
	     g_conf.m_maxMem > 1000000 ) {
		log("mem: new(%" UINT32 "): Out of memory.", (uint32_t)size );
		throw std::bad_alloc();
	}

	g_inMemFunction = true;

#ifdef EFENCE
	void *mem = getElecMem(size);
#elif EFENCE_SIZE
	void *mem;
	if ( size > EFENCE_SIZE )
		mem = getElecMem(size);
	else
		mem = sysmalloc ( size );
#else
	void *mem = sysmalloc ( size );
#endif

	g_inMemFunction = false;


	int32_t  memLoop = 0;
newmemloop:
	if ( ! mem && size > 0 ) {
		g_errno = errno;
		g_mem.m_outOfMems++;
		log("mem: new(%" UINT32 "): %s",
		    (uint32_t)size, mstrerror(g_errno));
		throw std::bad_alloc();
	}
	if ( (PTRTYPE)mem < 0x00010000 ) {
#ifdef EFENCE
		void *remem = getElecMem(size);
#else
		void *remem = sysmalloc(size);
#endif
		log ( LOG_WARN, "mem: Caught low memory allocation at "
		      "%08" PTRFMT ", "
				"reallocated to %08" PTRFMT "", 
		      (PTRTYPE)mem, (PTRTYPE)remem );
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
			throw std::bad_alloc();
		}
		goto newmemloop;
	}

	//offset by 4 because that is metadata describing the number of objs
	//in the array
	g_mem.addMem ( (char*)mem+4 , size-4, "TMPMEM" , 1 );

	return mem;
}


Mem::Mem() {
	m_used = 0;
	m_numAllocated = 0;
	m_numTotalAllocated = 0;
	m_maxAlloc = 0;
	m_maxAllocBy = "";
	m_maxAlloced = 0;
	m_memtablesize = 0;
 	m_stackStart = NULL;
	// shared mem used
	m_sharedUsed = 0LL;
	// count how many allocs/news failed
	m_outOfMems = 0;
}

Mem::~Mem() {
	if ( getUsedMem() == 0 ) return;
}

//int32_t Mem::getUsedMem    () { return 0; }; //return mallinfo().usmblks; };
int64_t Mem::getAvailMem   () { return 0; };
//int64_t Mem::getMaxAlloced () { return 0; };
int64_t Mem::getMaxMem     () { return g_conf.m_maxMem; }
int32_t Mem::getNumChunks  () { return 0; };

// process id of the main process
pid_t s_pid = (pid_t) -1;

void Mem::setPid() {
	s_pid = getpid();
    gbassert(s_pid != -1 /* bad s_pid */);
}

pid_t Mem::getPid() {
	return s_pid;
}

bool Mem::init  ( ) {
	// set main process pid
	s_pid = getpid();
	// warning msg
	if ( g_conf.m_detectMemLeaks )
		log(LOG_INIT,"mem: Memory leak checking is enabled.");

#if defined(EFENCE) || defined(EFENCE_SIZE)
	log(LOG_INIT,"mem: using electric fence!!!!!!!");
#endif

	// reset this, our max mem used over time ever because we don't
	// want the mem test we did above to count towards it
	m_maxAlloced = 0;

	return true;
}

// this is called by C++ classes' constructors to register mem
void Mem::addMem ( void *mem , int32_t size , const char *note , char isnew ) {

	if ( ! s_initialized ) {
		m_memtablesize = 8194*1024;//m_maxMem / 6510;
	}

	if ( (int32_t)m_numAllocated + 100 >= (int32_t)m_memtablesize ) { 
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
		gbassert(false);
	}
	if ( g_conf.m_logDebugMem )
		log("mem: add %08" PTRFMT " %" INT32 " bytes (%" INT64 ") (%s)",
		    (PTRTYPE)mem,size,m_used,note);

	// check for breech after every call to alloc or free in order to
	// more easily isolate breeching code.. this slows things down a lot
	// though.
	if ( g_conf.m_logDebugMem ) printBreeches(1);

	// copy the magic character, iff not a new() call
    gbassert( size != 0 );
	// sanity check
	if ( size < 0 ) {
		log("mem: addMem: Negative size.");
		return;
	}

	// sanity check -- for machines with > 4GB ram?
	if ( (PTRTYPE)mem + (PTRTYPE)size < (PTRTYPE)mem ) {
		log(LOG_LOGIC,"mem: Kernel returned mem at "
		    "%08" PTRFMT " of size %" INT32 " "
		    "which would wrap. Bad kernel.",
		    (PTRTYPE)mem,(int32_t)size);
        gbassert(false);
	}

	// umsg00
	bool useElectricFence = false;
#ifdef SPECIAL
	if ( note[0] == 'u' &&
	     note[1] == 'm' &&
	     note[2] == 's' &&
	     note[3] == 'g' &&
	     note[4] == '0' &&
	     note[5] == '0' )
		useElectricFence = true;
#endif
	if ( ! isnew && ! useElectricFence ) {
		for ( int32_t i = 0 ; i < UNDERPAD ; i++ )
			((char *)mem)[0-i-1] = MAGICCHAR;
		for ( int32_t i = 0 ; i < OVERPAD ; i++ )
			((char *)mem)[0+size+i] = MAGICCHAR;
	}

	if ( s_pid == -1 && m_numTotalAllocated >1000 ) {
		log(LOG_WARN, "pid is %i and numAllocs is %i", (int)s_pid,  
		    (int)m_numTotalAllocated);
    }

	// threads can't be here!
	if ( s_pid != -1 && getpid() != s_pid ) {
		log("mem: addMem: Called from thread.");
		sleep(50000);
        gbassert(false);
	}

	// if no label!
	if ( ! note[0] ) log(LOG_LOGIC,"mem: addmem: NO note.");

	// clear mem ptrs if this is our first call
	if ( ! s_initialized ) {

		s_mptrs  = (void **)sysmalloc ( m_memtablesize*sizeof(void *));
		s_sizes  = (int32_t  *)sysmalloc ( m_memtablesize*sizeof(int32_t  ));
		s_labels = (char  *)sysmalloc ( m_memtablesize*16            );
		s_isnew  = (char  *)sysmalloc ( m_memtablesize               );
		if ( ! s_mptrs || ! s_sizes || ! s_labels || ! s_isnew ) {
			if ( s_mptrs  ) sysfree ( s_mptrs  );
			if ( s_sizes  ) sysfree ( s_sizes  );
			if ( s_labels ) sysfree ( s_labels );
			if ( s_isnew  ) sysfree ( s_isnew );
			log("mem: addMem: Init failed. Disabling checks.");
			g_conf.m_detectMemLeaks = false;
			return;
		}
		s_initialized = true;
		memset ( s_mptrs , 0 , sizeof(char *) * m_memtablesize );
	}
	// try to add ptr/size/note to leak-detecting table
	if ( (int32_t)s_n > (int32_t)m_memtablesize ) {
		log("mem: addMem: No room in table for %s size=%" INT32 ".",
		    note,size);
		return;
	}
	// hash into table
	uint32_t u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
	uint32_t h = u % (uint32_t)m_memtablesize;
	// chain to an empty bucket
	int32_t count = (int32_t)m_memtablesize;
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
			    "rmMem not called? label=%c%c%c%c%c%c"
			    ,s_labels[h*16+0]
			    ,s_labels[h*16+1]
			    ,s_labels[h*16+2]
			    ,s_labels[h*16+3]
			    ,s_labels[h*16+4]
			    ,s_labels[h*16+5]
			    );
            gbassert(false);
		}
		h++;
		if ( h == m_memtablesize ) h = 0;
		if ( --count == 0 ) {
			log("mem: addMem: Mem table is full.");
			printMem();
            gbassert(false);
		}
	}
	// add to debug table
	s_mptrs  [ h ] = mem;
	s_sizes  [ h ] = size;
	s_isnew  [ h ] = isnew;
	s_n++;
	// debug
	if ( (size > MINMEM && g_conf.m_logDebugMemUsage) || size>=100000000 )
		log(LOG_INFO,"mem: addMem(%" INT32 "): %s. ptr=0x%" PTRFMT " "
		    "used=%" INT64 "",
		    size,note,(PTRTYPE)mem,m_used);
	// now update used mem
	// we do this here now since we always call addMem() now
	m_used += size;
	m_numAllocated++;
	m_numTotalAllocated++;
	if ( size > m_maxAlloc ) { m_maxAlloc = size; m_maxAllocBy = note; }
	if ( m_used > m_maxAlloced ) m_maxAlloced = m_used;


 skipMe:
	int32_t len = gbstrlen(note);
	if ( len > 15 ) len = 15;
	char *here = &s_labels [ h * 16 ];
	gbmemcpy ( here , note , len );
	// make sure NULL terminated
	here[len] = '\0';
}


#define PRINT_TOP 40

class MemEntry {
public:
	int32_t  m_hash;
	char *m_label;
	int32_t  m_allocated;
	int32_t  m_numAllocs;
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

	int32_t n = m_numAllocated * 2;
	MemEntry *e = (MemEntry *)mcalloc ( sizeof(MemEntry) * n , "Mem" );
	if ( ! e ) {
		log("admin: Could not alloc %" INT32 " bytes for mem table.",
		    (int32_t)sizeof(MemEntry)*n);
		return false;
	}

	// hash em up, combine allocs of like label together for this hash
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		// get label ptr, use as a hash
		char *label = &s_labels[i*16];
		int32_t  h     = hash32n ( label );
		if ( h == 0 ) h = 1;
		// accumulate the size
		int32_t b = (uint32_t)h % n;
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

	int32_t i = 0;
	int32_t count = 0;
	for ( ; i < n && count < PRINT_TOP ; i++ )
		// if non-empty, add to winners array
		if ( e[i].m_hash ) winners [ count++ ] = &e[i];

	// compute new min
	int32_t min  = 0x7fffffff;
	int32_t mini = -1000;
	for ( int32_t j = 0 ; j < count ; j++ ) {
		if ( winners[j]->m_allocated > min ) continue;
		min  = winners[j]->m_allocated;
		mini = j;
	}

	// now the rest must compete
	for ( ; i < n ; i++ ) {
		// if empty skip
		if ( ! e[i].m_hash ) continue;
		// skip if not a winner
		if ( e[i].m_allocated <= min ) continue;
		// replace the lowest winner
		winners[mini] = &e[i];
		// compute new min
		min = 0x7fffffff;
		for ( int32_t j = 0 ; j < count ; j++ ) {
			if ( winners[j]->m_allocated > min ) continue;
			min  = winners[j]->m_allocated;
			mini = j;
		}
	}

	// now sort them
	bool flag = true;
	while ( flag ) {
		flag = false;
		for ( int32_t i = 1 ; i < count ; i++ ) {
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
	for ( int32_t i = 0 ; i < count ; i++ ) 
		sb->safePrintf (
			       "<tr bgcolor=%s>"
			       "<td>%s</td>"
			       "<td>%" INT32 "</td>"
			       "<td>%" INT32 "</td>"
			       "</tr>\n",
			       LIGHT_BLUE,
			       winners[i]->m_label,
			       winners[i]->m_numAllocs,
			       winners[i]->m_allocated);

	sb->safePrintf ( "</table>\n");

	// don't forget to release this mem
	mfree ( e , (int32_t)sizeof(MemEntry) * n , "Mem" );

	return true;
}


// Relabels memory in table.  Returns true on success, false on failure.
// Purpose is for times when UdpSlot's buffer is not owned and freed by someone
// else.  Now we can verify that passed memory is freed.
bool Mem::lblMem( void *mem, int32_t size, const char *note ) {
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

	uint32_t u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
	uint32_t h = u % (uint32_t)m_memtablesize;
	// chain to bucket
	while( s_mptrs[h] ) {
		if( s_mptrs[h] == mem ) {
			if( s_sizes[h] != size ) {
				val = false;
				log( "mem: lblMem: Mem addr (0x%08" PTRFMT ") "
				     "exists, "
				     "size is %" INT32 " off.", 
				     (PTRTYPE)mem,
					 s_sizes[h]-size );
				break;
			}
			int32_t len = gbstrlen(note);
			if ( len > 15 ) len = 15;
			char *here = &s_labels [ h * 16 ];
			gbmemcpy ( here , note , len );
			// make sure NULL terminated
			here[len] = '\0';
			val = true;
			break;
		}
		h++;
		if ( h == m_memtablesize ) h = 0;
	}

	if( !val ) log( "mem: lblMem: Mem addr (0x%08" PTRFMT ") not found.", 
			(PTRTYPE)mem );

	return val;
}

// this is called by C++ classes' destructors to unregister mem
bool Mem::rmMem  ( void *mem , int32_t size , const char *note ) {

	// sanity check
	if ( g_inSigHandler ) {
		log(LOG_LOGIC,"mem: In sig handler 2.");
        gbassert(false);
	}
	// debug msg (mdw)
	if ( g_conf.m_logDebugMem )
		log("mem: free %08" PTRFMT " %" INT32 "bytes (%s)",
		    (PTRTYPE)mem,size,note);

	// check for breech after every call to alloc or free in order to
	// more easily isolate breeching code.. this slows things down a lot
	// though.
	if ( g_conf.m_logDebugMem ) printBreeches(1);

	// don't free 0 bytes
	if ( size == 0 ) return true;
	if ( s_pid == -1 && m_numTotalAllocated >1000 ) {
		log(LOG_WARN, "pid is %i and numAllocs is %i", 
		    (int)s_pid,  (int)m_numTotalAllocated);
	}
	// threads can't be here!
	if ( s_pid != -1 && getpid() != s_pid ) {
		log("mem: rmMem: Called from thread.");
		sleep(50000);
		// throw a bogus sig so we crash
        gbassert(false);
	}
	// . hash by first hashing "mem" to mix it up some
	// . balance the mallocs/frees
	// . hash into table
	uint32_t u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
	uint32_t h = u % (uint32_t)m_memtablesize;
	// . chain to an empty bucket
	// . CAUTION: loops forever if no empty bucket
	while ( s_mptrs[h] && s_mptrs[h] != mem ) {
		h++;
		if ( h == m_memtablesize ) h = 0;
	}
	// if not found, bitch
	if ( ! s_mptrs[h] ) {
		log("mem: rmMem: Unbalanced free. "
		    "note=%s size=%" INT32 ".",note,size);
		// . return false for now to prevent coring
		// . NOTE: but if entry was not added to table because there 
		//   was no room, we really need to be decrementing m_used
		//   and m_numAllocated here
		// . no, we should core otherwise it can result in some
		//   pretty hard to track down bugs later.
#ifndef _VALGRIND_
        gbassert(false);
#endif
		return false;
	}
	// are we from the "new" operator
	bool isnew = s_isnew[h];
	// set our size
	if ( size == -1 ) size = s_sizes[h];
	// must be legit now
    gbassert(size > 0);
	// . bitch is sizes don't match
	// . delete operator does not provide a size now (it's -1)
	if ( s_sizes[h] != size ) {
		log(
		    "mem: rmMem: Freeing %" INT32 " should be %" INT32 ". (%s)",
		    size,s_sizes[h],note);
		if ( g_isYippy ) {
			size = s_sizes[h];
			goto keepgoing;
		}
#ifndef _VALGRIND_
        gbassert(false);
#endif
		return false;
	}

 keepgoing:
	// debug
	if ( (size > MINMEM && g_conf.m_logDebugMemUsage) || size>=100000000 )
		log(LOG_INFO,"mem: rmMem (%" INT32 "): "
		    "ptr=0x%" PTRFMT " %s.",size,(PTRTYPE)mem,note);

	//
	// we do this here now since we always call rmMem() now
	//
	// decrement freed mem
	m_used -= size;
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
	uint32_t k;
	// shit after us may has to be rehashed in case it chained over us
	while ( s_mptrs[h] ) {
		// get mem ptr in bucket #h
		char *mem = (char *)s_mptrs[h];
		// find the most wanted bucket for this mem ptr
		u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
		k= u % (uint32_t)m_memtablesize;
		// if it's in it, continue
		if ( k == h ) { h++; continue; }
		// otherwise, move it back to fill the gap
		s_mptrs[h] = NULL;
		// if slot #k is full, chain
		for ( ; s_mptrs[k] ; )
			if ( ++k >= m_memtablesize ) k = 0;
		// re-add it to table
		s_mptrs[k] = (void *)mem;
		s_sizes[k] = s_sizes[h];
		s_isnew[k] = s_isnew[h];
		gbmemcpy(&s_labels[k*16],&s_labels[h*16],16);
		// try next bucket now
		h++;
		// wrap if we need to
		if ( h >= m_memtablesize ) h = 0;
	}

	return true;
}

int32_t Mem::validate ( ) {
	if ( ! s_mptrs ) return 1;
	// stock up "p" and compute total bytes allocated
	int64_t total = 0;
	int32_t count = 0;
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		total += s_sizes[i];
		count++;
	}
	// see if it matches
    gbassert(total == m_used);
    gbassert(count == m_numAllocated);
	return 1;
}


int32_t Mem::getMemSlot ( void *mem ) {
	// hash into table
	uint32_t u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
	uint32_t h = u % (uint32_t)m_memtablesize;
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


int Mem::printBreech ( int32_t i , char core ) {
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
#ifdef SPECIAL
	// for now this is efence. umsg00
	bool useElectricFence = false;
	if ( s_labels[i*16+0] == 'u' &&
	     s_labels[i*16+1] == 'm' &&
	     s_labels[i*16+2] == 's' &&
	     s_labels[i*16+3] == 'g' &&
	     s_labels[i*16+4] == '0' &&
	     s_labels[i*16+5] == '0' )
		useElectricFence = true;
	if ( useElectricFence ) return 0;
#endif
	char flag = 0;
	// check for underruns
	char *mem = (char *)s_mptrs[i];
	char *bp = NULL;
	for ( int32_t j = 0 ; j < UNDERPAD ; j++ ) {
		if ( (unsigned char)mem[0-j-1] == MAGICCHAR ) continue;
		log(LOG_LOGIC,"mem: underrun at %" PTRFMT " loff=%" INT32 " "
		    "size=%" INT32 " "
		    "i=%" INT32 " note=%s",
		    (PTRTYPE)mem,0-j-1,(int32_t)s_sizes[i],i,&s_labels[i*16]);

		// mark it for freed mem re-use check below
		if ( ! bp ) bp = &mem[0-j-1];

		// now scan the whole hash table and find the mem buffer
		// just before that! but only do this once
		if ( flag == 1 ) continue;
		PTRTYPE min = 0;
		int32_t mink = -1;
		for ( int32_t k = 0 ; k < (int32_t)m_memtablesize ; k++ ) {
			// skip empties
			if ( ! s_mptrs[k] ) continue;
			// do not look at mem after us
			if ( (PTRTYPE)s_mptrs[k] >= (PTRTYPE)mem ) 
				continue;
			// get min diff
			if ( mink != -1 && (PTRTYPE)s_mptrs[k] < min ) 
				continue;
			// new winner
			min = (PTRTYPE)s_mptrs[k];
			mink = k;
		}
		// now report it
		if ( mink == -1 ) continue;
		log("mem: possible breeching buffer=%s dist=%" UINT32 ,
		    &s_labels[mink*16],
		    (uint32_t)(
		    (PTRTYPE)mem-
		    ((PTRTYPE)s_mptrs[mink]+(uint32_t)s_sizes[mink])));
		flag = 1;
	}		    

	// check for overruns
	int32_t size = s_sizes[i];
	for ( int32_t j = 0 ; j < OVERPAD ; j++ ) {
		if ( (unsigned char)mem[size+j] == MAGICCHAR ) continue;
		log(LOG_LOGIC,"mem: overrun  at 0x%" PTRFMT " (size=%" INT32 ")"
		    "roff=%" INT32 " note=%s",
		    (PTRTYPE)mem,size,j,&s_labels[i*16]);

		// mark it for freed mem re-use check below
		if ( ! bp ) bp = &mem[size+j];

		// now scan the whole hash table and find the mem buffer
		// just before that! but only do this once
		if ( flag == 1 ) continue;
		PTRTYPE min = 0;
		int32_t mink = -1;
		for ( int32_t k = 0 ; k < (int32_t)m_memtablesize ; k++ ) {
			// skip empties
			if ( ! s_mptrs[k] ) continue;
			// do not look at mem before us
			if ( (PTRTYPE)s_mptrs[k] <= (PTRTYPE)mem ) 
				continue;
			// get min diff
			if ( mink != -1 && (PTRTYPE)s_mptrs[k] > min ) 
				continue;
			// new winner
			min = (PTRTYPE)s_mptrs[k];
			mink = k;
		}
		// now report it
		if ( mink == -1 ) continue;
		log("mem: possible breeching buffer=%s at 0x%" PTRFMT " "
		    "breaching at offset of %" PTRFMT " bytes",
		    &s_labels[mink*16],
		    (PTRTYPE)s_mptrs[mink],
		    (PTRTYPE)s_mptrs[mink]-((PTRTYPE)mem+s_sizes[i]));
		flag = 1;
	}
	
	// return now if no breach
	if ( flag == 0 ) return 1;

	// need this
    gbassert(bp);

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

    gbassert_false(flag && core);
	return 1;
}

// check all allocated memory for buffer under/overruns
int Mem::printBreeches ( char core ) {
	if ( ! s_mptrs ) return 0;
	// do not bother if no padding at all
	if ( (int32_t)UNDERPAD == 0 && (int32_t)OVERPAD == 0 ) return 0;
	
	log("mem: checking mem for breeches");

	// loop through the whole mem table
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ )
		// only check if non-empty
		if ( s_mptrs[i] ) printBreech ( i , core );
	return 0;
}


int Mem::printMem ( ) {
	// has anyone breeched their buffer?
	printBreeches ( 0 ) ;

	// print table entries sorted by most mem first
	int32_t *p = (int32_t *)sysmalloc ( m_memtablesize * 4 );
	if ( ! p ) return 0;
	// stock up "p" and compute total bytes allocated
	int64_t total = 0;
	int32_t np    = 0;
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ ) {
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
		for ( int32_t i = 1 ; i < np ; i++ ) {
			int32_t a = p[i-1];
			int32_t b = p[i  ];
			if ( s_sizes[a] <= s_sizes[b] ) continue;
			// switch these 2
			p[i  ] = a;
			p[i-1] = b;
			flag = 1;
		}
	}
 skipsort:

	// print out table sorted by sizes
	for ( int32_t i = 0 ; i < np ; i++ ) {
		int32_t a = p[i];
		log(LOG_INFO,"mem: %05" INT32 ") %" INT32 " 0x%" PTRFMT " %s",
		    i,s_sizes[a] , (PTRTYPE)s_mptrs[a] , &s_labels[a*16] );
	}
	sysfree ( p );
	log(LOG_INFO,"mem: # current objects allocated now = %" INT32 "", np );
	log(LOG_INFO,"mem: totalMem allocated now = %" INT64 "", total );
	log(LOG_INFO,"mem: Memory allocated now: %" INT64 ".\n", getUsedMem() );
	log(LOG_INFO,"mem: Num allocs %" INT32 ".\n", m_numAllocated );
	return 1;
}

void *Mem::gbmalloc ( int size , const char *note ) {
	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;
	
	// random oom testing
	if ( g_conf.m_testMem && (rand() % 100) < 2 ) {
		g_errno = ENOMEM;
		log("mem: malloc-fake(%i,%s): %s",size,note,
		    mstrerror(g_errno));
		return NULL;
	} 

 retry:

	// hack so hostid #0 can use more mem
	int64_t max = g_conf.m_maxMem;

	// don't go over max
	if ( m_used + size + UNDERPAD + OVERPAD >= max ) {
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = ENOMEM;
		log("mem: malloc(%i): Out of memory", size );
		return NULL;
	}
	if ( size < 0 ) {
		g_errno = EBADENGINEER;
		log("mem: malloc(%i): Bad value.", size );
        gbassert(false);
		return NULL;
	}

	void *mem;

	g_inMemFunction = true;

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

#ifdef SPECIAL	
	// debug where tagrec in xmldoc.cpp's msge0 tag list is overrunning
	// for umsg00
	bool useElectricFence = false;
	if ( note[0] == 'u' &&
	     note[1] == 'm' &&
	     note[2] == 's' &&
	     note[3] == 'g' &&
	     note[4] == '0' &&
	     note[5] == '0' ) 
		useElectricFence = true;
	if ( useElectricFence ) {
		mem = getElecMem(size+0+0);
		addMem ( (char *)mem + 0 , size , note , 0 );
		return (char *)mem + 0;
	}
#endif

	mem = (void *)sysmalloc ( size + UNDERPAD + OVERPAD );
#endif

	g_inMemFunction = false;

	int32_t memLoop = 0;
mallocmemloop:
	if ( ! mem && size > 0 ) {
		g_mem.m_outOfMems++;
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = errno;
		static int64_t s_lastTime;
		static int32_t s_missed = 0;
		int64_t now = gettimeofdayInMillisecondsLocal();
		int64_t avail = (int64_t)g_conf.m_maxMem - 
			(int64_t)m_used;
		if ( now - s_lastTime >= 1000LL ) {
			log("mem: system malloc(%i,%s) availShouldBe=%" INT64 ": "
			    "%s (%s) (ooms suppressed since "
			    "last log msg = %" INT32 ")",
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
		//gbassert(false);
		// send an email alert if this happens! it is a sign of
		// "memory fragmentation"
		//static bool s_sentEmail = false;
		// stop sending these now... seems to be problematic. says
		// 160MB is avail and can't alloc 20MB...
		static bool s_sentEmail = true;
		// assume only 90% is really available because of 
		// inefficient mallocing
		avail = (int64_t)((float)avail * 0.80);
		// but if it is within about 15MB of what is theoretically
		// available, don't send an email, because there is always some
		// minor fragmentation
		if ( ! s_sentEmail && avail > size ) {
			s_sentEmail = true;
			char msgbuf[1024];
			Host *h = g_hostdb.m_myHost;
			snprintf(msgbuf, 1024,
				 "Possible memory fragmentation "
				 "on host #%" INT32 " %s",
				 h->m_hostId,h->m_note);
			log(LOG_WARN, "query: %s",msgbuf);
			g_pingServer.sendEmail(NULL, msgbuf,true,true);
		}
		return NULL;
	}
	if ( (PTRTYPE)mem < 0x00010000 ) {
#ifdef EFENCE
		void *remem = getElecMem(size);
#else
		void *remem = sysmalloc(size);
#endif
		log ( LOG_WARN, "mem: Caught low memory allocation "
		      "at %08" PTRFMT ", "
		      "reallocated to %08" PTRFMT "",
		      (PTRTYPE)mem, (PTRTYPE)remem );
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
    gbassert(newSize >= 0);
	// if newSize is 0...
	if ( newSize == 0 ) { 
		//mfree ( ptr , oldSize , note );
		gbfree ( ptr , oldSize , note );
		return (void *)0x7fffffff;
	}
	// don't do more than 128M at a time, it hurts pthread_create
	//if ( newSize > MAXMEMPERCALL ) {
	//	g_errno = ENOMEM;
	//	log("Mem::realloc(%i): can only alloc %" INT32 " bytes per call",
	//	    newSize,MAXMEMPERCALL);
	//	return NULL;
	//}
 retry:

	// hack so hostid #0 can use more mem
	int64_t max = g_conf.m_maxMem;
	//if ( g_hostdb.m_hostId == 0 )  max += 2000000000;

	// don't go over max
	if ( m_used + newSize - oldSize >= max ) {
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
	gbmemcpy ( mem , ptr , oldSize );
	// free the old
	mfree ( ptr , oldSize , note );
	// done
	return mem;
#endif


#ifdef SPECIAL
	int32_t slot = g_mem.getMemSlot ( ptr );
	// debug where tagrec in xmldoc.cpp's msge0 tag list is overrunning
	// for umsg00
	if ( slot >= 0 ) {
		char *label = &s_labels[slot*16];
		bool useElectricFence = false;
		if ( label[0] == 'u' &&
		     label[1] == 'm' &&
		     label[2] == 's' &&
		     label[3] == 'g' &&
		     label[4] == '0' &&
		     label[5] == '0' ) 
			useElectricFence = true;
		if ( useElectricFence ) {
			// just make a new buf
			mem = (char *)mmalloc ( newSize , note );
			if ( ! mem ) return NULL;
			// copy over to it
			gbmemcpy ( mem , ptr , oldSize );
			// free the old
			mfree ( ptr , oldSize , note );
			return mem;
		}
	}
#endif

	// assume it will be successful. we can't call rmMem() after
	// calling sysrealloc() because it will mess up our MAGICCHAR buf
	rmMem  ( ptr , oldSize , note );

	// . do the actual realloc
	// . CAUTION: don't pass in 0x7fffffff in as "ptr" 
	// . this was causing problems
	mem = (char *)sysrealloc ( (char *)ptr - UNDERPAD , 
				   newSize + UNDERPAD + OVERPAD);

	// remove old guy on success
	if ( mem ) {
		addMem ( (char *)mem + UNDERPAD , newSize , note , 0 );
		char *returnMem = mem + UNDERPAD;
		// set magic char bytes for mem
		for ( int32_t i = 0 ; i < UNDERPAD ; i++ )
			returnMem[0-i-1] = MAGICCHAR;
		for ( int32_t i = 0 ; i < OVERPAD ; i++ )
			returnMem[0+newSize+i] = MAGICCHAR;
		return returnMem;
	}

	// ok, just try using malloc then!
	mem = (char *)mmalloc ( newSize , note );
	// bail on error
	if ( ! mem ) {
		g_mem.m_outOfMems++;
		// restore the original buf we tried to grow
		addMem ( ptr , oldSize , note , 0 );
		errno = g_errno = ENOMEM;
		return NULL;
	}
	// log a note
	log(LOG_INFO,"mem: had to use malloc/gbmemcpy instead of "
	    "realloc.");
	// copy over to it
	gbmemcpy ( mem , ptr , oldSize );
	// we already called rmMem() so don't double call
	sysfree ( (char *)ptr - UNDERPAD );	
	// free the old. this was coring because it was double calling rmMem()
	//mfree ( ptr , oldSize , note );
	// mmalloc() and mfree() should have taken care of it
	return mem;
}

char *Mem::dup ( const void *data , int32_t dataSize , const char *note ) {
	// keep it simple
	char *mem = (char *)mmalloc ( dataSize , note );
	if ( mem ) gbmemcpy ( mem , data , dataSize );
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
	int32_t slot = g_mem.getMemSlot ( ptr );
	if ( slot < 0 ) {
		log(LOG_LOGIC,"mem: could not find slot (note=%s)",note);
		//log(LOG_LOGIC,"mem: FIXME!!!");
		// return for now so procog does not core all the time!
		return;
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


#ifdef SPECIAL
	g_inMemFunction = true;
	// debug where tagrec in xmldoc.cpp's msge0 tag list is overrunning
	// for umsg00
	bool useElectricFence = false;
	char *label = &s_labels[slot*16];
	if ( label[0] == 'u' &&
	     label[1] == 'm' &&
	     label[2] == 's' &&
	     label[3] == 'g' &&
	     label[4] == '0' &&
	     label[5] == '0' ) 
		useElectricFence = true;
	if ( useElectricFence ) {
		// this calls rmMem() itself
		freeElecMem ((char *)ptr - 0 );
		g_inMemFunction = false;
		// if this returns false it was an unbalanced free
		//if ( ! rmMem ( ptr , size , note ) ) return;
		return;
	}
	g_inMemFunction = false;
#endif

	// if this returns false it was an unbalanced free
	if ( ! rmMem ( ptr , size , note ) ) return;

	g_inMemFunction = true;
	if ( isnew ) sysfree ( (char *)ptr );
	else         sysfree ( (char *)ptr - UNDERPAD );
	g_inMemFunction = false;
}

int32_t getLowestLitBitLL ( uint64_t bits ) {
	// count how many bits we have to shift so that the first bit is 0
	int32_t shift = 0;
	while ( (bits & (1LL<<shift)) == 0 && (shift < 63 ) ) shift++;
	return shift;
}

uint32_t getHighestLitBitValue ( uint32_t bits ) {
	// count how many bits we have to shift so that the first bit is 0
	uint32_t highest =  0;
	for ( int32_t shift = 0 ; shift < 32 ; shift++ ) 
		if ( bits & (1<<shift) ) highest = (1 << shift);
	return highest;
}

uint64_t getHighestLitBitValueLL ( uint64_t bits ) {
	// count how many bits we have to shift so that the first bit is 0
	uint64_t highest =  0;
	for ( int32_t shift = 0 ; shift < 64 ; shift++ ) 
		if ( bits & (1LL<<shift) ) highest = (1LL << shift);
	return highest;
}

#ifndef htonll
// TODO: speed up
int64_t htonll ( uint64_t a ) {
	int64_t b;
	unsigned int int0 = htonl ( ((uint32_t *)&a)[0] );
	unsigned int int1 = htonl ( ((uint32_t *)&a)[1] );

	((unsigned int *)&b)[0] = int1;
	((unsigned int *)&b)[1] = int0;
	return b;
}
#endif

#ifndef ntohll
// just swap 'em back
int64_t ntohll ( uint64_t a ) { 
	return htonll ( a );
}
#endif

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
uint32_t reverseBits ( uint32_t x ) {
	// init groupId
	uint32_t y = 0;
	// go through each bit in hostId
	for ( int32_t srcBit = 0 ; srcBit < 32 ; srcBit++ ) {
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
int32_t membitcmp ( void *dst     ,
		 int32_t  dstBits ,   // bit offset into "dst"
		 void *src     ,
		 int32_t  srcBits ,   // bit offset into "src"
		 int32_t  nb      ) { // # bits to compare
	char *s;
	char *d;
	char  smask;
	char  dmask;
	for ( int32_t b = nb - 1 ; b >= 0 ; b-- ) {
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
int32_t membitcmp2 ( void *dst     ,
		  int32_t  dstBits ,   // bit offset into "dst"
		  void *src     ,
		  int32_t  srcBits ,   // bit offset into "src"
		  int32_t  nb      ) { // # bits to compare
	char *s;
	char *d;
	char  smask;
	char  dmask;
	int32_t  nc = 0;
	for ( int32_t b = nb - 1 ; b >= 0 ; b-- ) {
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
		  int32_t  dstBits ,   // bit offset into "dst"
		  void *src     ,
		  int32_t  srcBits ,   // bit offset into "src"
		  int32_t  nb      ) { // # bits to copy
	// debug msg
	//log("nb=%" INT32 "",nb);
	// if src and dst overlap, it matters if b moves up or down
	char *s;
	char *d;
	char  smask;
	char  dmask;
	for ( int32_t b = 0 ; b < nb ; b++ ) {
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
		  int32_t  dstBits ,   // bit offset into "dst"
		  void *src     ,
		  int32_t  srcBits ,   // bit offset into "src"
		  int32_t  nb      ) { // # bits to copy
	// if src and dst overlap, it matters if b moves up or down
	char *s;
	char *d;
	char  smask;
	char  dmask;
	for ( int32_t b = nb - 1 ; b >= 0 ; b-- ) {
		s     =(char *)src + ((b + srcBits) >> 3 );
		d     =(char *)dst + ((b + dstBits) >> 3 );
		smask = 0x01 << ((b + srcBits) & 0x07);
		dmask = 0x01 << ((b + dstBits) & 0x07);
		if ( *s & smask ) *d |=  dmask;
		else              *d &= ~dmask;
	}
}

int32_t Mem::printBits  ( void *src, int32_t srcBits , int32_t nb ) {
	char *s;
	char  smask;
	fprintf(stdout,"low %" INT32 " bits = ",nb);
	for ( int32_t b = 0 ; b < nb ; b++ ) {
		s     =(char *)src + ((b + srcBits) >> 3 );
		smask = 0x01 << ((b + srcBits) & 0x07);
		if ( *s & smask ) fprintf(stdout,"1");
		else              fprintf(stdout,"0");
	}
	fprintf(stdout,"\n");
	return 0;
}

// ass = async signal safe, dumb ass
void memset_ass ( void *dest , const char c , int32_t len ) {
	char *end  = (char *)dest + len;
	// JAB: so... the optimizer should take care of the extra
	// register declaration for d, below...  see note below.
	char *d    = (char *)dest;
	// JAB: gcc-3.4 did not like the cast in the previous version
	// while ( dest < end ) *((char *)dest)++ = c;
	while ( d < end ) { *d++ = c; }
}

void memset_nice( void *dest , const char c , int32_t len ,
		  int32_t niceness ) {
	char *end  = (char *)dest + len;
	// JAB: so... the optimizer should take care of the extra
	// register declaration for d, below...  see note below.
	char *d    = (char *)dest;
	// JAB: gcc-3.4 did not like the cast in the previous version
	// while ( dest < end ) *((char *)dest)++ = c;
 loop:
	char *seg = d + 5000;
	if ( seg > end ) seg = end;
	QUICKPOLL ( niceness );
	while ( d < seg ) { *d++ = c; }
	QUICKPOLL ( niceness );
	// do more?
	if ( d < end ) goto loop;
}


// . TODO: avoid byteCopy by copying remnant bytes
// . ass = async signal safe, dumb ass
// . NOTE: src/dest should not overlap in this version of gbmemcpy
// . MDW: i replaced this is a #define bcopy in gb-include.h
/*
void memcpy_ass ( register void *dest2, register const void *src2, int32_t len ) {
	// for now keep it simple!!
	len--;
	while ( len >= 0 ) { 
		((char *)dest2)[len] = ((char *)src2)[len]; 
		len--; 
	}
*/
	/*
	// debug test
	//gbmemcpy ( dest2 , src2 , len );
	//return;
	// the end for the fast copy by word with partially unrolled loop
	register int32_t *dest = (int32_t *)dest2;
	register int32_t *src  = (int32_t *)src2 ;
	register int32_t *end  = dest + (len >> 2);
	int32_t *oldEnd = end;
	//int64_t start = gettimeofdayInMilliseconds();
	//fprintf(stderr,"ln=%" INT32 ",dest=%" INT32 ",src=%" INT32 "\n",len,(int32_t)dest,(int32_t)src);
	if ((len&0x03)!=0 || ((int32_t)dest&0x03)!=0 || ((int32_t)src&0x03)!=0 ) 
		goto byteCopy;
	// truncate n so we can unroll this loop
	end = (int32_t *)  ( (int32_t)end & 0x07 );
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
	// copy remaining 7 or less int32_ts
	while ( dest < oldEnd ) { *dest = *src; dest++; src++; }
	//fprintf(stderr,"t=%" INT32 "\n",(int32_t)(gettimeofdayInMilliseconds()-start));
	return;
 byteCopy:
	len--;
	while ( len >= 0 ) { dest2[len] = src2[len]; len--; }
	*/
//}

// Check the current stack usage
int32_t Mem::checkStackSize() {
	if ( !m_stackStart )
		return 0;
	char final;
	char *stackEnd = &final;
	int32_t size = m_stackStart - stackEnd;
	log("gb: stack size is %" INT32 "",size);
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
int32_t Mem::findPtr ( void *target ) {
	if ( ! s_mptrs ) return 0;
	PTRTYPE maxDelta = (PTRTYPE)-1;
	PTRTYPE topDelta[MAXBEST];
	PTRTYPE topOff  [MAXBEST];
	PTRTYPE topi    [MAXBEST];
	int32_t nt = 0;
	int32_t minnt = 0;
	int32_t i;
	// loop through the whole mem table
	for ( i = 0 ; i < (int32_t)m_memtablesize ; i++ ) {
		// only check if non-empty
		if ( ! s_mptrs[i] ) continue;
		// get size to scan
		char *p    = (char *)s_mptrs[i];
		int32_t  size = s_sizes[i];
		char *pend = p + size;
		PTRTYPE bestDelta = (PTRTYPE)-1;
		PTRTYPE bestOff   = (PTRTYPE)-1;
		char *note = &s_labels[i*16];
		// skip thread stack
		if ( strcmp(note,"ThreadStack") == 0 ) 
			continue;
		// scan that
		for ( ; p +sizeof(char *) < pend ; p++ ) {
			// get ptr it might have
			//char *pp = (char *)(*(int32_t *)p);
			char *pp = *(char **)p;
			// delta from target
			PTRTYPE delta = (PTRTYPE)pp - (PTRTYPE)target;
			// make positive
			if ( delta < 0 ) delta *= -1;
			// is it a min?
			//if ( delta > 100 ) continue;
			// get top 10
			if ( delta < bestDelta ) {
				bestDelta = delta;
				bestOff   = (PTRTYPE)p - (PTRTYPE)(s_mptrs[i]);
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
		for ( int32_t j = 1 ; j < nt ; j++ )
			if ( topDelta[j] > topDelta[minnt] )
				minnt = j;
	}
	// print out top MAXBEST. "note" is the note attached to the allocated 
	// memory the suspicious write ptr is in
	for ( int32_t j = 0 ; j < nt ; j++ ) {
		// get it
		PTRTYPE bi = topi[j];
		char *note = (char *)&s_labels[bi*16];
		if ( ! note ) note = "unknown";
		PTRTYPE *x = (PTRTYPE *)((char *)s_mptrs[bi] + topOff[j]);
		log("mem: topdelta=%" PTRFMT " bytes away from corrupted mem. "
		    "note=%s "
		    "memblock=%" PTRFMT " and memory of ptr is %" PTRFMT " "
		    "bytes into that "
		    "memblock. and ptr is pointing to 0x%" PTRFMT "(%" PTRFMT ")",
		    topDelta[j],
		    note,bi,topOff[j], *x,*x);
	}

	return 0;
}

//#include <limits.h>    /* for MEMPAGESIZE */
#define MEMPAGESIZE ((uint32_t)(8*1024))

void *getElecMem ( int32_t size ) {
	// a page above OR a page below
	// let's go below this time since that seems to be the problem

#ifdef CHECKUNDERFLOW
	// how much to alloc
	// . assume sysmalloc returns one byte above a page, so we need
	//   MEMPAGESIZE-1 bytes to move p up to page boundary, another
	//   MEMPAGESIZE bytes for protected page, then the actual mem,
	//   THEN possibly another MEMPAGESIZE-1 bytes to hit the next page
	//   boundary for protecting the "freed" mem below, but can get
	//   by with (MEMPAGESIZE-(size%MEMPAGESIZE)) more
	int32_t need = size + sizeof(char *)*2 + MEMPAGESIZE + MEMPAGESIZE ;
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
	int32_t remainder = (uint64_t)realMem % MEMPAGESIZE;
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
	*(char **)(returnMem- sizeof(char *)) = realMem;
	*(char **)(returnMem- sizeof(char *)*2) = realMemEnd;
	//log("protect2 0x%" PTRFMT "\n",(PTRTYPE)protMem);
	// protect that after we wrote our ptr
	if ( mprotect ( protMem , MEMPAGESIZE , PROT_NONE) < 0 )
		log("mem: mprotect failed: %s",mstrerror(errno));
	// advance over user data
	p += size;
	// now when we free this it should all be protected, so make sure
	// we have enough room on top
	int32_t leftover = MEMPAGESIZE  - ((uint64_t)p % MEMPAGESIZE);
	// skip that
	p += leftover;
	// inefficient?
	gbassert_false( realMemEnd - p > (int32_t)MEMPAGESIZE );
	// ensure we do not breach
	gbassert_false( p > realMemEnd );
	// test it, this should core
	//protmem[0] = 32;
	// return that for them
	return returnMem;
#else
	// how much to alloc
	int32_t need = size + MEMPAGESIZE + MEMPAGESIZE + MEMPAGESIZE;
	need += sizeof(char *)*2;
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
	int32_t remainder = (PTRTYPE)end % MEMPAGESIZE;
	// back down to that
	char *protMem = end - remainder;
	// get return mem
	char *returnMem = protMem - size;
	// back beyond that
	int32_t leftover = (PTRTYPE)returnMem % MEMPAGESIZE;
	// back up
	char *p = returnMem - leftover;
	// we are now on a page boundary, so we can protect this mem
	// after we "free" it below
    gbassert(p >= realMem);
	// store mem ptrs before protecting
	*(char **)(returnMem- sizeof(char *)  ) = realMem;
	*(char **)(returnMem- sizeof(char *)*2) = realMemEnd;
	// sanity
    gbassert(returnMem - sizeof(char *)*2 >= realMem);
	//log("protect3 0x%" PTRFMT "\n",(PTRTYPE)protMem);
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
	int32_t  m_fakeSize;
	char *m_note;
	char *m_realMem;
	int32_t  m_realSize;
	char *m_protMem;
	int32_t  m_protSize;
};
static FreeInfo s_freeBuf[4000];
static FreeInfo *s_cursor      = &s_freeBuf[0];
static FreeInfo *s_cursorEnd   = &s_freeBuf[4000];
static FreeInfo *s_cursorStart = &s_freeBuf[0];
static bool      s_looped = false;
static FreeInfo *s_freeCursor  = &s_freeBuf[0];
static int64_t s_totalInRing = 0LL;

// . now we must unprotect before freeing
// . let's do delayed freeing because i think the nasty bug that is
//   corrupting malloc's space is overruning a freed buffer perhaps?
void freeElecMem ( void *fakeMem ) {
	// cast it
	char *cp = (char *)fakeMem;

	// get mem info from the hash table
	int32_t h = g_mem.getMemSlot ( cp );
	if ( h < 0 ) { 
		log("mem: unbalanced free ptr");
		gbassert(false);
	}
	char *label    = &s_labels[((uint32_t)h)*16];
	int32_t  fakeSize =  s_sizes[h];

#ifdef CHECKUNDERFLOW
	char *oldProtMem = cp - MEMPAGESIZE;
#else
	char *oldProtMem = cp + fakeSize;
#endif

	// hack
	//oldProtMem -= 4;
	//log("unprotect1 0x%" PTRFMT "\n",(PTRTYPE)oldProtMem);
	// unprotect it
	if ( mprotect ( oldProtMem , MEMPAGESIZE, PROT_READ|PROT_WRITE) < 0 )
		log("mem: munprotect failed: %s",mstrerror(errno));

	// now original memptr is right before "p" and we can
	// read it now that we are unprotected
	char *realMem    = *(char **)(cp-sizeof(char *));
	// set real mem end (no!?)
	char *realMemEnd = *(char **)(cp-sizeof(char *)*2);

	// set it all to 0x99
	memset ( realMem , 0x99 , realMemEnd - realMem );

	// ok, back up to page boundary before us
	char *protMem = realMem + (MEMPAGESIZE - 
				   (((PTRTYPE)realMem) % MEMPAGESIZE));
	// get end point
	char *protEnd = realMemEnd - ((PTRTYPE)realMemEnd % MEMPAGESIZE);
	// sanity
    gbassert(protMem >= realMem);
    gbassert( protMem - realMem <= (int32_t)MEMPAGESIZE);
	//log("protect1 0x%" PTRFMT "\n",(PTRTYPE)protMem);
	// before adding it into the ring, protect it
	if ( mprotect ( protMem , protEnd-protMem, PROT_NONE) < 0 )
		log("mem: mprotect2 failed: %s",mstrerror(errno));

	// add our freed memory to the freed ring
    gbassert(s_cursor <= s_cursorEnd);

	// to avoid losing free info by eating our own tail
	if ( s_cursor == s_freeCursor && s_looped ) {
		// free it
		g_mem.rmMem ( s_freeCursor->m_fakeMem,
			      s_freeCursor->m_fakeSize,
			      s_freeCursor->m_note );
		// log("unprotect2 0x%" PTRFMT "\n",
		//     (PTRTYPE)s_freeCursor->m_protMem);
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


	// now free beginning at s_freeCursor 
	for ( ; s_freeCursor != s_cursor && s_totalInRing > 150000000 ; ) {
		// free it
		g_mem.rmMem ( s_freeCursor->m_fakeMem,
			      s_freeCursor->m_fakeSize,
			      s_freeCursor->m_note );
		// log("unprotect3 0x%" PTRFMT "\n",
		//     (PTRTYPE)s_freeCursor->m_protMem);
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
