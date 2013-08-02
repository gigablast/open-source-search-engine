// Matt Wells, Copyright Jan 2004

// . each Rdb has its own m_pageCache member
// . a ptr to this class is passed to all File::open() calls
// . that ptr is stored in the File class as File::m_pageCachePtr
// . the File class uses the virtual file descriptor, vfd, for use with
//   the pageCache since we tend to open and close files a lot when we run
//   out of actual fds
// . every subsequent read/write to that file will then use the pageCache
// . before doing a read in File::read() we try to increase the offset
//   by filling the beginning of the buffer with data from the page cache.
//   We also try to decrease the bytes to read by filling the end of the
//   buffer. What is left to actually read, if anything, is the middle.
// . after File::read() completes it call DiskPageCache::storePages (buf,size,off)
//   to fill the page cache.
// . when maxMem is reached, the DiskPageCache will unfrequently used pages by 
//   using a linked list
// . when File class releases its vfd it must call m_pageCachePtr->close(vfd)

// . we use PAGESIZE defined in RdbMap.h as our page size
// . TODO: convert PAGESIZE to 8000 not 8192

#ifndef _PAGECACHE_H_
#define _PAGECACHE_H_

// each page in memory has the bytes stored (4 bytes), store offset (4 bytes),
// prev page char ptr (4 bytes) and next page char ptr (4 bytes) in addition
// to PAGE_SIZE worth of bytes for holding the data from the disk.
// the lsat 4 bytes are a long ptr to m_bufOffs[vfd][pageNum] so if page gets
// kicked out (cuz he's the tail) we can set *ptr to -1.
#define HEADERSIZE 20

// . use 128 disk megabytes per set of pages
// . this MUST be a multiple of (PAGE_SIZE+HEADERSIZE) now
//#define PAGE_SET_SIZE (128*1024*1024)
//#define PHSIZE (GB_PAGE_SIZE+HEADERSIZE)
//#define PAGE_SET_SIZE (((128*1024*1024)/PHSIZE)*PHSIZE)

// how many page sets can we have?
#define MAX_PAGE_SETS 128

// how many BigFiles can be using the same DiskPageCache?
#define MAX_NUM_VFDS2 1024

extern void freeAllSharedMem ( long max );

class DiskPageCache {

 public:

	DiskPageCache();
	~DiskPageCache();
	void reset();

	// returns false and sets g_errno if unable to alloc the memory,
	// true otherwise
	bool init ( const char *dbname ,
		    char rdbId, // use 0 for none
		    long maxMem ,
		    long pageSize,
		    bool useRAMDisk = false,
		    bool minimizeDiskSeeks = false );
		//    long maxMem ,
		//    void (*getPages2)(DiskPageCache*, long, char*, long, 
		//	    	      long long, long*, long long*) = NULL,
		//    void (*addPages2)(DiskPageCache*, long, char*, long,
		//	    	      long long) = NULL,
		//    long (*getVfd2)(DiskPageCache*, long long) = NULL,
		//    void (*rmVfd2)(DiskPageCache*, long) = NULL );

	bool initRAMDisk( const char *dbname, long maxMem );

	// . grow/shrink m_memOff[] which maps vfd/page to a mem offset
	// . returns false and sets g_errno on error
	// . called by DiskPageCache::open()/close() respectively
	// . maxFileSize is so we can alloc m_memOff[vfd] big enough for all
	//   pages that are in or will be in the file (if it is being created)
	long getVfd ( long long maxFileSize, bool vfdAllowed );
	void rmVfd  ( long vfd );

	// . this returns true iff the entire read was copied into
	//   "buf" from the page cache
	// . it will move the used pages to the head of the linked list
	void getPages   ( long       vfd         ,
			  char     **buf         ,
			  long       numBytes    ,
			  long long  offset      ,
			  long      *newNumBytes ,
			  long long *newOffset   ,
			  char     **allocBuf    , //we alloc this if buf==NULL
			  long      *allocSize   , //size of the alloc
			  long       allocOff    );

	// after you read/write from/to disk, copy into the page cache
	void addPages ( long vfd, char *buf , long numBytes, long long offset,
			long niceness );

	long long getNumHits   () { return m_hits; };
	long long getNumMisses () { return m_misses; };
	void      resetStats   () { m_hits = 0 ; m_misses = 0; };

	long getMemUsed    () ;
	long getMemAlloced () { return m_memAlloced; };
	long getMemMax     () { return m_maxMemOff; };

	// verify each page in cache for this file is what is on disk
	bool verify ( class BigFile *f );

	void disableCache ( ) { m_enabled = false; };
	void enableCache  ( ) { m_enabled = true; };

//private:

	void addPage   (long vfd,long pageNum,char *page,long size,long skip);
	void enhancePage ( long poff,char *page,long size,long skip) ;
	void promotePage ( long poff , bool isNew ) ;
	void excisePage  ( long poff ) ;

	bool growCache ( long mem );

	//bool needsMerge();

	void writeToCache ( long bigOff, long smallOff, void *inBuf, 
			    long size );
	void readFromCache( void *outBuf, long bigOff, long smallOff,
			    long size );

	char *getMemPtrFromOff ( long off );

	// . the pages are here
	// . there are 1024 page sets
	// . each page set can have up to 128 megabytes of pages
	// . much more than that and pthread_create() fails
	char *m_pageSet     [ MAX_PAGE_SETS ];
	long  m_pageSetSize [ MAX_PAGE_SETS ];
	long  m_numPageSets;

	// . next available page offset
	// . when storing a page we read from disk into a pageSet we first
	//   try to get a memory offset from m_availMemOff, if none are there
	//   then we use m_nextMemOff and increment it by PAGE_SIZE+HEADERSIZE
	// . if m_nextMemOff would breech m_upperMemOff then we call 
	//   growCache to increase m_upperMemOff
	// . we try to grow 100k with each call to growCache
	// . if m_upperMemOff would breech m_maxMemOff, then we kick out the
	//   least used page using
	// . we store a linked list in bytes 4-12 of each page in memory
	long  m_nextMemOff;  // next available mem offset to hold a page
	long  m_upperMemOff; // how many bytes are allocated in page sets?
	long  m_maxMemOff;   // max we can allocate

	// . available offsets of released pages
	// . offsets are into the page sets, m_pageSet[]
	long *m_availMemOff;
	long  m_numAvailMemOffs;
	long  m_maxAvailMemOffs;

	// . m_memOff[vfd][diskPageNum] --> memOff
	// . maps a vfd and disk page number to a memory offset
	// . maps to -1 if not in page cache
	// . try to keep the number of pages down, under 100,000
	// . 100,000 pages would be about 800 megabytes
	// . I am only planning on using this for tfndb and Checksumdb so
	//   we should be under or around this limit
	long  *m_memOff [ MAX_NUM_VFDS2 ];

	// . how many offsets are in m_memOff?
	// . we have one offset per page in the file
	long m_maxPagesInFile [ MAX_NUM_VFDS2 ];

	// used for minimize disk seeks
	bool m_minimizeDiskSeeks;
	// max number of pages that this file shall have
	long m_maxPagesPerFile [ MAX_NUM_VFDS2 ];
	// max number of pages of file currently in the cache
	long m_numPagesPresentOfFile[ MAX_NUM_VFDS2 ];
	// mem that has not been used
	long m_memFree;

	// how much memory is currently allocated?
	long m_memAlloced;

	// stats (partial hits/misses supported)
	long long m_hits;
	long long m_misses;

	// . linked list boundary info
	// . linked list is actually stored in bytes 2-8 (next/prev) of pages
	//   in memory
	long m_headOff;
	long m_tailOff;

	// for selecting the next vfd in line and preventing sudden closing
	// and opening of a vfd, resulting in a thread returning and calling
	// addPages() for the wrong file!!
	long m_nexti;

	bool m_enabled;

	long m_pageSize;
	long m_maxPageSetSize;

	const char *m_dbname;
	char m_rdbId;
	bool *m_switch;

	char m_memTag[16];

	bool m_useRAMDisk;
	bool m_useSHM;

	int m_ramfd;

	int   m_shmids    [ 4096 ];
	long  m_shmidSize [ 4096 ];
	long  m_numShmids;
	long  m_maxAllocSize;
	long  m_spageSize;

	// for overriding the disk page cache with custom functions
	//bool m_isOverriden;
	//void (*m_getPages2)(DiskPageCache*, long, char*, long, long long, 
	//		    long*, long long*);
	//void (*m_addPages2)(DiskPageCache*, long, char*, long, long long);
	//long (*m_getVfd2)(DiskPageCache*, long long);
	//void (*m_rmVfd2)(DiskPageCache*, long);
};

#endif
