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

// . use 128 disk megabytes per set of pages
// . this MUST be a multiple of (PAGE_SIZE+HEADERSIZE) now
//#define PAGE_SET_SIZE (128*1024*1024)
//#define PHSIZE (GB_PAGE_SIZE+HEADERSIZE)
//#define PAGE_SET_SIZE (((128*1024*1024)/PHSIZE)*PHSIZE)

// how many page sets can we have?
#define MAX_PAGE_SETS 128

// how many BigFiles can be using the same DiskPageCache?
#include "File.h"
#define MAX_NUM_VFDS2 MAX_NUM_VFDS

extern void freeAllSharedMem ( int32_t max );

class DiskPageCache {

 public:

	DiskPageCache();
	~DiskPageCache();
	void reset();

	// returns false and sets g_errno if unable to alloc the memory,
	// true otherwise
	bool init ( const char *dbname ,
		    char rdbId, // use 0 for none
		    int32_t maxMem ,
		    int32_t pageSize,
		    bool useRAMDisk = false,
		    bool minimizeDiskSeeks = false );
		//    int32_t maxMem ,
		//    void (*getPages2)(DiskPageCache*, int32_t, char*, int32_t, 
		//	    	      int64_t, int32_t*, int64_t*) = NULL,
		//    void (*addPages2)(DiskPageCache*, int32_t, char*, int32_t,
		//	    	      int64_t) = NULL,
		//    int32_t (*getVfd2)(DiskPageCache*, int64_t) = NULL,
		//    void (*rmVfd2)(DiskPageCache*, int32_t) = NULL );

	bool initRAMDisk( const char *dbname, int32_t maxMem );

	int32_t getMemUsed    () ;
	int32_t getMemAlloced () { return m_memAlloced; };
	int32_t getMemMax     () { return m_maxMem; };

	int64_t getNumHits   () { return m_hits; };
	int64_t getNumMisses () { return m_misses; };
	void      resetStats   () { m_hits = 0 ; m_misses = 0; };

	// verify each page in cache for this file is what is on disk
	bool verifyData ( class BigFile *f );
	bool verifyData2 ( int32_t vfd );

	void disableCache ( ) { m_enabled = false; };
	void enableCache  ( ) { m_enabled = true; };

	// . grow/shrink m_memOff[] which maps vfd/page to a mem offset
	// . returns false and sets g_errno on error
	// . called by DiskPageCache::open()/close() respectively
	// . maxFileSize is so we can alloc m_memOff[vfd] big enough for all
	//   pages that are in or will be in the file (if it is being created)
	int32_t getVfd ( int64_t maxFileSize, bool vfdAllowed );
	void rmVfd  ( int32_t vfd );

	// . this returns true iff the entire read was copied into
	//   "buf" from the page cache
	// . it will move the used pages to the head of the linked list
	void getPages   ( int32_t       vfd         ,
			  char     **buf         ,
			  int32_t       numBytes    ,
			  int64_t  offset      ,
			  int32_t      *newNumBytes ,
			  int64_t *newOffset   ,
			  char     **allocBuf    , //we alloc this if buf==NULL
			  int32_t      *allocSize   , //size of the alloc
			  int32_t       allocOff    );

	// after you read/write from/to disk, copy into the page cache
	void addPages ( int32_t vfd, char *buf , int32_t numBytes, int64_t offset,
			int32_t niceness );


	// used for minimize disk seeks
	bool m_minimizeDiskSeeks;

	int32_t m_diskPageSize;

private:

	void addPage   (int32_t vfd,int32_t pageNum,char *page,int32_t size,int32_t skip);
	void enhancePage ( int32_t poff,char *page,int32_t size,int32_t skip) ;
	void promotePage ( int32_t poff , bool isNew ) ;
	void excisePage  ( int32_t poff ) ;

	bool growCache ( int32_t mem );

	//bool needsMerge();

	void writeToCache ( int32_t memOff, int32_t memPageOff, void *inBuf, 
			    int32_t size );
	void readFromCache( void *outBuf, int32_t memOff, int32_t memPageOff,
			    int32_t size );

	char *getMemPtrFromMemOff ( int32_t off );

	// . the pages are here
	// . there are 1024 page sets
	// . each page set can have up to 128 megabytes of pages
	// . much more than that and pthread_create() fails
	char *m_pageSet     [ MAX_PAGE_SETS ];
	int32_t  m_pageSetSize [ MAX_PAGE_SETS ];
	int32_t  m_numPageSets;

	// . next available page offset
	// . when storing a page we read from disk into a pageSet we first
	//   try to get a memory offset from m_availMemOff, if none are there
	//   then we use m_nextMemOff and increment it by PAGE_SIZE+HEADERSIZE
	// . if m_nextMemOff would breech m_upperMemOff then we call 
	//   growCache to increase m_upperMemOff
	// . we try to grow 100k with each call to growCache
	// . if m_upperMemOff would breech m_maxMem, then we kick out the
	//   least used page using
	// . we store a linked list in bytes 4-12 of each page in memory
	int32_t  m_nextMemOff;  // next available mem offset to hold a page
	int32_t  m_upperMemOff; // how many bytes are allocated in page sets?
	int32_t  m_maxMem;   // max we can allocate

	// . available offsets of released pages
	// . offsets are into the page sets, m_pageSet[]
	int32_t *m_availMemOff;
	int32_t  m_numAvailMemOffs;
	int32_t  m_maxAvailMemOffs;

	// . m_memOffFromDiskPage[vfd][diskPageNum] --> memOff
	// . maps a vfd and disk page number to a memory offset
	// . maps to -1 if not in page cache
	// . try to keep the number of pages down, under 100,000
	// . 100,000 pages would be about 800 megabytes
	// . I am only planning on using this for tfndb and Checksumdb so
	//   we should be under or around this limit
	int32_t  *m_memOffFromDiskPage [ MAX_NUM_VFDS2 ];

	// . how many offsets are in m_memOffFromDiskPage?
	// . we have one offset per page in the file
	int32_t m_maxPagesInFile [ MAX_NUM_VFDS2 ];

	// max number of pages that this file shall have
	int32_t m_maxPagesPerFile [ MAX_NUM_VFDS2 ];
	// max number of pages of file currently in the cache
	int32_t m_numPagesPresentOfFile[ MAX_NUM_VFDS2 ];
	// mem that has not been used
	int32_t m_memFree;

	// how much memory is currently allocated?
	int32_t m_memAlloced;

	// stats (partial hits/misses supported)
	int64_t m_hits;
	int64_t m_misses;

	// . linked list boundary info
	// . linked list is actually stored in bytes 2-8 (next/prev) of pages
	//   in memory
	int32_t m_headOff;
	int32_t m_tailOff;

	// for selecting the next vfd in line and preventing sudden closing
	// and opening of a vfd, resulting in a thread returning and calling
	// addPages() for the wrong file!!
	int32_t m_nexti;

	bool m_enabled;

	int32_t m_maxPageSetSize;

	const char *m_dbname;
	char m_rdbId;
	bool *m_switch;

	char m_memTag[16];

	//bool m_useRAMDisk;
	//bool m_useSHM;

	//int m_ramfd;

	//int   m_shmids    [ 4096 ];
	//int32_t  m_shmidSize [ 4096 ];
	//int32_t  m_numShmids;
	//int32_t  m_maxAllocSize;
	//int32_t  m_spageSize;

	// for overriding the disk page cache with custom functions
	//bool m_isOverriden;
	//void (*m_getPages2)(DiskPageCache*, int32_t, char*, int32_t, int64_t, 
	//		    int32_t*, int64_t*);
	//void (*m_addPages2)(DiskPageCache*, int32_t, char*, int32_t, int64_t);
	//int32_t (*m_getVfd2)(DiskPageCache*, int64_t);
	//void (*m_rmVfd2)(DiskPageCache*, int32_t);
};

#endif
