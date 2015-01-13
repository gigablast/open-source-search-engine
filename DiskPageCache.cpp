#undef _XOPEN_SOURCE // needed for pread and pwrite
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "DiskPageCache.h"
#include "RdbMap.h"    // GB_PAGE_SIZE
#include "Indexdb.h"
#include "Profiler.h"
// types.h uses key_t type that shmget uses
//#undef key_t

/*
#ifdef GBUSESHM
#include <sys/ipc.h>  // shmget()
#include <sys/shm.h>  // shmget()
#endif
*/

// FORMAT of a MEMORY PAGE representing a DISK PAGE
//
// HEADER:
//
// bbbbbbbb bbbbbbbb bbbbbbbb bbbbbbb # of disk data bytes stored in this page
// ffffffff ffffffff ffffffff fffffff Offset into memory page they are stored
// pppppppp pppppppp pppppppp ppppppp Offset of prev mem page in linked list
// nnnnnnnn nnnnnnnn nnnnnnnn nnnnnnn Offset of next mem page in linked list
// dddddddd dddddddd dddddddd ddddddd Disk page # mem page is mapping.
// vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvv vfd of file page is mapping
//
// DDDDDDDD ........                  raw disk data at that page...


// offsets in bytes in the header each entry has. 
// should total HEADERSIZE bytes.
#define OFF_SIZE 0
#define OFF_SKIP (int)(sizeof(int32_t))
#define OFF_PREV (int)(sizeof(int32_t)*2)
#define OFF_NEXT (int)(sizeof(int32_t)*3)
#define OFF_DISKPAGENUM (int)(sizeof(int32_t)*4)
#define OFF_VFD (int)(sizeof(int32_t)*5)
// store disk data iteself into page at this offset
#define HEADERSIZE (int)(sizeof(int32_t)*6)


DiskPageCache::DiskPageCache () {
	m_numPageSets = 0;
	// sometimes db may pass an unitialized DiskPageCache to a BigFile
	// so make sure when BigFile::close calls DiskPageCache::rmVfd() our
	// m_memOffFromDiskPage vector is all NULLed out, otherwise 
	// it will core
	//memset ( m_memOff , 0 , sizeof(int32_t *) * MAX_NUM_VFDS2 );
	for ( int32_t i = 0 ; i < MAX_NUM_VFDS2 ; i++ )
		m_memOffFromDiskPage[i] = NULL;

	m_availMemOff = NULL;
	//m_isOverriden = false;
	reset();
}

DiskPageCache::~DiskPageCache() {
	reset();
}

/*
#ifdef GBUSESHM
static char *s_mem = NULL;
static int   s_shmid = -1;
#endif
*/

void DiskPageCache::reset() {

	if ( m_numPageSets > 0 ) 
		log("db: resetting page cache for %s",m_dbname);

	// . "m_pageSet[]" the actual memory buffers for holding disk pages 
	// . we allocate one m_pageSet[] at a time like pools
	for ( int32_t i = 0 ; i < m_numPageSets ; i++ ) {
		mfree ( m_pageSet[i], m_pageSetSize[i], "DiskPageCache");
		m_pageSet    [i] = NULL;
		m_pageSetSize[i] = 0;
	}
	// . free all the m_memOffs[] arrays
	// . free map that maps this files pages on disk to pages/offs in mem
	// . m_memOffs[DISKPAGENUM] -> MEMPAGEOFFSET
	for ( int32_t i = 0 ; i < MAX_NUM_VFDS2 ; i++ ) {
		if ( ! m_memOffFromDiskPage [ i ] ) continue;
		int32_t size = m_maxPagesInFile[i] * sizeof(int32_t);
		mfree ( m_memOffFromDiskPage [ i ] , size , "DiskPageCache" );
		m_memOffFromDiskPage [ i ] = NULL;
	}
	// . and these contain offsets to available memory pages
	// . there are m_numAvailMemOffs of them
	// . m_availMemOff[0] would map to the memory offset of the next
	//   available memory page. kinda like m_memOffFromDiskPage[] but that one is
	//   for used pages
	if ( m_availMemOff ) {
		int32_t size = m_maxAvailMemOffs * sizeof(int32_t);
		mfree ( m_availMemOff , size , "DiskPageCache" );
	}
	/*
#ifdef GBUSESHM
	// free current one, if exists
	if ( s_shmid >= 0 && s_mem ) {
		if ( shmdt ( s_mem ) == -1 )
			log("disk: shmdt: reset: %s",mstrerror(errno));
		s_mem   = NULL;
		s_shmid = -1;
	}
	// mark shared mem for destruction
	for ( int32_t i = 0 ; m_useSHM && i < m_numShmids ; i++ ) {
		int shmid = m_shmids[i];
		if ( shmctl ( shmid , IPC_RMID , NULL) == -1 )
			log("db: shmctlt shmid=%"INT32": %s",
			    (int32_t)shmid,mstrerror(errno));
		else
			log("db: shmctl freed shmid=%"INT32"",(int32_t)shmid);
	}
#endif
	*/

	m_numPageSets     = 0;
	m_nextMemOff      = 0;
	m_upperMemOff     = 0;
	m_maxMem          = 0;
	m_memAlloced      = 0;
	m_availMemOff     = NULL;
	m_numAvailMemOffs = 0;
	m_maxAvailMemOffs = 0;
	m_headOff         = -1;
	m_tailOff         = -1;
	m_enabled         = true;
	m_nexti           = 0;
	//m_ramfd = -1;
	//m_useRAMDisk = false;
	//m_useSHM = false;
}

bool DiskPageCache::init ( const char *dbname ,
			   char rdbId,
			   int32_t maxMem  ,
			   int32_t pageSize,
			   bool useRAMDisk,
			   bool minimizeDiskSeeks ) {
			//   int32_t maxMem ,
			//   void (*getPages2)(DiskPageCache*, int32_t, char*,
			//		     int32_t, int64_t, int32_t*,
			//		     int64_t*),
			//   void (*addPages2)(DiskPageCache*, int32_t, char*,
			//	   	     int32_t, int64_t),
			//   int32_t (*getVfd2)(DiskPageCache*, int64_t),
			//   void (*rmVfd2)(DiskPageCache*, int32_t) ) {
	reset();

	// seems like we lose data when it prints "Caught add breach"
	// so let's stop using until we fix that... happens while we are
	// dumping i think and somehow the data seems to get lost that
	// we were dumping.
	//maxMem = 0;

	m_rdbId = rdbId;

	bool *tog = NULL;
	if (m_rdbId==RDB_INDEXDB   ) tog=&g_conf.m_useDiskPageCacheIndexdb;
	if (m_rdbId==RDB_POSDB   ) tog=&g_conf.m_useDiskPageCachePosdb;
	if (m_rdbId==RDB_DATEDB    ) tog=&g_conf.m_useDiskPageCacheDatedb;
	if (m_rdbId==RDB_TITLEDB   ) tog=&g_conf.m_useDiskPageCacheTitledb;
	if (m_rdbId==RDB_SPIDERDB  ) tog=&g_conf.m_useDiskPageCacheSpiderdb;
	if (m_rdbId==RDB_TFNDB     ) tog=&g_conf.m_useDiskPageCacheTfndb;
	if (m_rdbId==RDB_TAGDB     ) tog=&g_conf.m_useDiskPageCacheTagdb;
	if (m_rdbId==RDB_CLUSTERDB ) tog=&g_conf.m_useDiskPageCacheClusterdb;
	if (m_rdbId==RDB_CATDB     ) tog=&g_conf.m_useDiskPageCacheCatdb;
	if (m_rdbId==RDB_LINKDB    ) tog=&g_conf.m_useDiskPageCacheLinkdb;
	m_switch = tog;

	/*
	bool useSHM = false;
	// a quick hacky thing, force them to use shared mem instead of ram dsk
	if ( useRAMDisk ) { 
		useRAMDisk = false;
		useSHM     = true;
	}
	*/

	// not for tmp cluster
	//if ( g_hostdb.m_useTmpCluster ) useSHM = false;
	// it is off by default because it leaks easily (if u Ctrl+C the process)
	//if ( ! g_conf.m_useSHM ) useSHM = false;
	// right now shared mem only supports a single page size because
	// we use s_mem/s_shmid, and if we have a small page size which
	// we free, then shmat() may get ENOMEM when trying to get the larger
	// of the two page sizes
	//if(useSHM && pageSize != GB_INDEXDB_PAGE_SIZE) {char *xx=NULL;*xx=0;}
	// don't use it until we figure out how to stop the memory from being
	// counted as being the process's memory space. i think we can make
	// shmat() use the same mem address each time...
	// if ( useSHM ) {
	// 	log("disk: shared mem currently not supported. Turn off "
	// 	    "in gb.conf <useSharedMem>");
	// 	char *xx=NULL;*xx=0;
	// }
	// save it;
	//m_useSHM = useSHM;
	// clear it
	//m_numShmids = 0;
	// set this
	//m_maxAllocSize = 33554432;
	// the shared mem page size is a little more than the disk page size
	//m_spageSize = pageSize + HEADERSIZE;
	// . this is /proc/sys/kernel/shmmax DIVIDED BY 2 on titan and gk0 now
	// . which is the max to get per call to shmat()
	// . making this smaller did not seem to have much effect on speed
	//int32_t max = 33554432/2;
	// make sure it is "pageSize" aligned so we don't split pages
	//m_maxAllocSize = (max / m_spageSize) * m_spageSize;

	// max of ~16MB worth of pages
	//int32_t adjPageSize = pageSize + HEADERSIZE;
	//m_maxAllocSize = 2000000000; // 2GB (16000000 / adjPageSize) * adjPageSize;

	/*
#ifdef GBUSESHM
	// set it up
	if ( m_useSHM ) {
		// we can only use like 30MB shared mem pieces
		int32_t need = maxMem;
	shmloop:
		// how much to alloc now?
		int32_t alloc = need;
		// this is /proc/sys/kernel/shmmax on titan and gk0 now
		if ( alloc > m_maxAllocSize ) alloc = m_maxAllocSize;
		// don't allow anything lower than this because we always
		// "swap out" one for another below. that is, we call shmdt()
		// to free it then shmat() to reclaim it. otherwise, shmat()
		// will run out of memory!!
		if ( alloc < m_maxAllocSize ) alloc = m_maxAllocSize;
		// get it     // SHM_R|SHM_W|SHM_R>>3|SHM_R>>6|...
		int shmid = shmget(IPC_PRIVATE, alloc, SHM_R|SHM_W|IPC_CREAT);
		// on error, bail
		if ( shmid == -1 )
			return log("db: shmget: %s",mstrerror(errno));
		// don't swap it out (only 2.6 kernel i think)
		//if ( shmctl ( shmid , SHM_LOCK , NULL ) )
		//	return log("db: shmctl: %s",mstrerror(errno));
		// log it
		log("db: allocated %"INT32" bytes shmid=%"INT32"",alloc,(int32_t)shmid);
		// add it to our list
		m_shmids    [ m_numShmids ] = shmid;
		m_shmidSize [ m_numShmids ] = alloc;
		m_numShmids++;
		// count it
		g_mem.m_sharedUsed += alloc;
		// log it for now
		//logf(LOG_DEBUG,"db: new shmid id is %"INT32", size=%"INT32"",
		//     (int32_t)shmid,(int32_t)alloc);
		// subtract it
		need -= alloc;
		// get more
		if ( need > 0 ) goto shmloop;
	}
#endif
	*/

	// a malloc tag, must be LESS THAN 16 bytes including the NULL
	char *p = m_memTag;
	gbmemcpy  ( p , "pgcache-" , 8 ); p += 8;
	if ( dbname ) strncpy ( p , dbname    , 8 ); 
	// so we know what db we are caching for
	m_dbname = p;
	p += 8;
	*p++ = '\0';
	// sanity check, we store bytes used as a int16_t at top of page
	//if ( m_diskPageSize > 0x7fff ) { char *xx = NULL; *xx = 0; }
	// . do not use more than this much memory for caching
	// . it may go over by like 2% for header information
	m_maxMem = maxMem ;
	// set m_pageSetSize. use this now instead of m_maxPageSetSize #define
	int32_t phsize = pageSize + HEADERSIZE;
	m_maxPageSetSize = (((128*1024*1024)/phsize)*phsize);
	m_diskPageSize     = pageSize;

	m_minimizeDiskSeeks = minimizeDiskSeeks;

	// we need to keep a count memory of files being cached
	if ( m_minimizeDiskSeeks )
		m_memFree = m_maxMem;

	// check for overriding functions
	//if ( getPages2 && addPages2 && getVfd2 && rmVfd2 ) {
	//	// set override flag
	//	m_isOverriden = true;
	//	// set override functions
	//	m_getPages2 = getPages2;
	//	m_addPages2 = addPages2;
	//	m_getVfd2   = getVfd2;
	//	m_rmVfd2    = rmVfd2;
	//	// return here
	//	return true;
	//}

	/*
	// for now only indexdb will use the ramdisk
	if ( strcmp ( dbname, "indexdb" ) == 0 && useRAMDisk ){
		if ( !initRAMDisk( dbname, maxMem ) )
			return log ( "db: failed to init RAM disk" );
	}
	*/

	// . use up to 800k for starters
	// . it will grow more as needed
	if ( ! growCache ( maxMem ) ) 
		return log("db: pagecache init failed: %s.",
			   mstrerror(g_errno));
	// success
	return true;
}

// use Linux's ram disk for caching disk pages, in addition to the ram it
// already uses. I would like to be able to pass in a "maxMemForRamDisk" parm
// to its init() function and have it open a single, ram-disk file descriptor
// for writing up to that many bytes.

// then i would like only Indexdb (and later on Datedb) to pass in an 800MB
// "maxMemForRamDisk" value, and, furthermore, i do not want to cache disk
// pages from the indexdb root file, nor, any indexdb file that is larger than
// twice the "maxMemForRamDisk" value (in this case 1.6GB). this will be used
// exclusively for smaller indexdb files to eliminate excessive disk seeks and
// utilize ALL the 4GB of ram in each machine.

// lastly, we need some way to "force" a merge at around midnight when traffic
// is minimal, or when there are 3 or more indexdb files that are less than
// 80% in the indexdb disk page cache. because that means we are starting to
// do a lot of disk seeks. 
/*
bool DiskPageCache::initRAMDisk( const char *dbname, int32_t maxMem ){
	m_useRAMDisk = true;
	if ( !dbname ) {char *xx=NULL; *xx=0;}
	// open a file descriptor 
	char ff [1024];
	sprintf ( ff, "/mnt/RAMDisk/%sPageCache", dbname );
	// unlink it first
	unlink (ff);

	m_ramfd = open ( ff, O_RDWR | O_CREAT );
	if ( m_ramfd < 0 ) 
		return log ( LOG_WARN,"db: could not open fd in RAMdisk" );

	return true;
}
*/

// . this returns true iff the entire read was copied into
//   "buf" from the page cache
// . it will move the used pages to the head of the linked list
// . if *buf is NULL we allocate here
void DiskPageCache::getPages   ( int32_t       vfd         ,
				 char     **buf         ,
				 int32_t       numBytes    ,
				 int64_t  diskOffset      ,
				 int32_t      *newNumBytes ,
				 int64_t *newOffset   ,
				 char     **allocBuf    ,
				 int32_t      *allocSize   ,
				 int32_t       allocOff    ) {

	// check for override function
	//if ( m_isOverriden ) {
	//	//log ( LOG_INFO, "cache: Get Pages [%"INT32"] [%"INT32"][%"INT64"]",
	//	//		vfd, numBytes, offset );
	//	m_getPages2 ( this,
	//		      vfd,
	//		      buf,
	//		      numBytes,
	//		      offset,
	//		      newNumBytes,
	//		      newOffset );
	//	return;
	//}
	
	// return new disk offset, assume unchanged
	*newOffset   = diskOffset;
	*newNumBytes = numBytes;

	// return if no pages allowed in page cache
	if ( m_maxMem == 0 ) return;
	// or disabled
	if ( ! m_enabled ) return;
	// disabled at the master controls?
	if ( m_switch && ! *m_switch ) return;

	// or if minimizeDiskSeeks did not accept the vfd
	if ( m_minimizeDiskSeeks && vfd < 0 )
		return;

	// or if no pages in this vfd
	if ( ! m_memOffFromDiskPage[vfd] ) 
		return;

	// debug point
	//if ( offset == 16386 && numBytes == 16386 ) 
	//	log("hey");

	// what is the page range of in-memory pages?
	int32_t sp = diskOffset / m_diskPageSize ;
	int32_t ep = (diskOffset + (numBytes-1)) / m_diskPageSize ;

	// . sanity check
	// . we establish the maxPagesInFile when BigFile::open is called
	//   by RdbDump. Rdb.cpp calls m_dump.set with a maxFileSize based on
	//   the mem occupied by the RdbTree. BUT,recs can be added to the tree
	//   WHILE we are dumping, so we end up with a bigger file, and this
	//   disk page cache is not prepared for it!
	if ( ep >= m_maxPagesInFile[vfd] ) { 
		// happens because rdbdump did not get a high enough 
		// maxfilesize so we did not make enough pages! we endedup
		// dumping more than what was end the tree because stuff was
		// added to the tree while dumping!
		log("db: pagecache: Caught get breach. "
		    "ep=%"INT32" max=%"INT32" vfd=%"INT32""
		    , ep,m_maxPagesInFile[vfd] ,vfd);
		return;
		//char *xx = NULL; *xx = 0; 
	}

	char *bufPtr = *buf;
	char *bufEnd = *buf + numBytes;

	// our offset into first page on disk ( as well as memory page)
	int32_t start1 = diskOffset - sp * m_diskPageSize;
	// this is for second while loop
	int32_t start2 = 0;
	if ( ep == sp ) start2 = start1;

	// store start pages
	while ( sp <= ep ) {
		// map disk page # sp into memory offset, "poff"
		int32_t poff = m_memOffFromDiskPage[vfd][sp];
		// get a ptr to it
		//char *s = getMemPtrFromMemOff ( poff );
		//if ( ! s ) break;
		// break if we do not have page in memory
		if ( poff < 0 ) break;
		// first 4 bytes of page is how many bytes are used in page
		int32_t size = 0;
		readFromCache( &size, poff, OFF_SIZE, sizeof(int32_t));
		//int32_t size = *(int32_t *)(s+OFF_SIZE);
		// second set of 4 bytes is offset of data from page boundary
		int32_t skip = 0;
		readFromCache( &skip, poff, OFF_SKIP, sizeof(int32_t));
		//int32_t skip = *(int32_t *)(s+OFF_SKIP);
		// debug msg
		// log("getPage: pageNum=%"INT32" poff=%"INT32" size=%"INT32" "
		//     "skip=%"INT32"",
		//     sp,poff,(int32_t)size,(int32_t)skip);
		// if this mem page data starts AFTER our offset, it is no good
		if ( skip > start1 ) break;
		// adjust size by our page offset, we won't necessarily be
		// starting our read at "skip"
		size -= (start1 - skip);
		// if size is 0 or less all cached data was 
		// below our disk offset and is useless
		if ( size <= 0 ) break;
		// . promote this memory page in the linked list
		// . 16 byte header of each memory page houses the
		//   linked lists' next and prev ptrs to pages in memory
		//   just for putting the most frequently used pages on top
		promotePage ( poff , false );

		// allocate the read buffer if we need to
		if ( ! *buf ) {
			// allocate enough room for allocOff, too
			int32_t need = numBytes + allocOff;
			char *p = (char *) mmalloc ( need,"PageCacheReadBuf" );
			// let FileState know what needs to be freed
			*allocBuf  = p;
			*allocSize = need;
			// if couldn't allocate, return now, what's the point
			if ( ! p ) return;
			// let caller know his new read buffer
			*buf       = p + allocOff;
			// assign the ptrs now
			bufPtr     = *buf ;
			bufEnd     = *buf + numBytes;
		}
		// don't store more than asked for
		if ( bufPtr + size > bufEnd ) size = bufEnd - bufPtr;
		// . read in "size" bytes from memory into "bufPtr"
		// . start reading at an offset of "HEADERSIZE+start1" into
		//   the memory page
		readFromCache(bufPtr, poff, HEADERSIZE + start1 , size);
		//gbmemcpy ( bufPtr , s + HEADERSIZE + start1 , size );
		bufPtr       += size;
		*newOffset   += size;
		*newNumBytes -= size;
		// return if we got it all
		if ( bufPtr >= bufEnd ) { m_hits += 1; return; }
		// otherwise, advance to next page
		sp++;
		// and our page relative offset is zero now, iff ep > sp
		if ( sp <= ep ) start1 = 0;
		// if the memory page ended before the disk page, break out
		// because we don't want any holes
		readFromCache( &size, poff, OFF_SIZE, sizeof(int32_t));
		if ( skip + size < m_diskPageSize ) break;
		//if ( skip + *(int32_t *)(s+OFF_SIZE) < m_diskPageSize )break;
	}

	// now store from tail down
	/*
	while ( ep > sp ) {
		// the page offset in memory
		int32_t poff = m_memOffFromDiskPage[vfd][ep];
		// get a ptr to it
		char *s = getMemPtrFromMemOff ( poff );
		// break if we do not have page in memory
		if ( ! s ) break;
		// first 2 bytes of page is how many bytes are used
		int32_t size = *(int32_t *)s;
		// second set of 2 bytes is offset from boundary
		int32_t skip = *(int32_t *)(s+OFF_SKIP);
		// adjust size by our page offset, if not zero
		if ( start2 > skip ) size -= (start2 - skip);
		// his skip point could be beyond us, too
		if ( skip > 
		// . promote this page in the linked list
		// . bytes 8-16 of each page in memory houses the
		//   next and prev ptrs to pages in memory
		promotePage ( s , poff , false );
		// don't store more than asked for
		if ( bufEnd - size < bufPtr ) size = bufEnd - bufPtr;
		gbmemcpy ( bufEnd - size , s + HEADERSIZE + start2 , size );
		bufEnd       -= size;
		*newNumBytes -= size;
		// return if we got it all
		if ( bufEnd <= bufPtr ) { m_hits += 1; return; }
		// if this page had a skip, break out, we don't wany any holes
		if ( skip > 0 ) break;
		// otherwise, advance to next page
		ep--;
	}
	*/
	m_misses += 1;
}

// after you read/write from/to disk, copy into the page cache
void DiskPageCache::addPages ( int32_t vfd,
			       char *buf,
			       int32_t numBytes,
			       int64_t diskOffset ,
			       int32_t niceness ){

	// check for override function
	//if ( m_isOverriden ) {
	//	m_addPages2 ( this,
	//		      vfd,
	//		      buf,
	//		      numBytes,
	//		      offset );
	//	return;
	//}
	// if vfd is -1, then we were not able to add a map for this file
	if ( vfd < 0 ) return;
	// no NULL ptrs
	if ( ! buf ) return;
	// return if no pages allowed in page cache
	if ( m_maxMem == 0 ) return;
	// or disabled
	if ( ! m_enabled ) return;
	// disabled at the master controls?
	if ( m_switch && ! *m_switch ) return;
	// sometimes the file got unlinked on us
	if ( ! m_memOffFromDiskPage[vfd] ) return;
	// for some reason profiler cores all the time in here
	//if ( g_profiler.m_realTimeProfilerRunning ) return;

	// . "diskPageNum" is the first DISK page #
	// . "offset" is the offset on disk the data was read from
	// . "m_diskPageSize" is the size of the disk pages
	int64_t diskPageNum = diskOffset / m_diskPageSize ;

	// point to the data that was read from disk
	char *bufPtr = buf;
	char *bufEnd = buf + numBytes;

	// . how much did we exceed the mem page boundary by?
	// . "skip" is offset into the memory page where we store the disk data
	int32_t skip = diskOffset - diskPageNum * m_diskPageSize ;

	// how many bytes of disk data should we store into the memory page?
	int32_t  size = m_diskPageSize - skip;

	// now add the remaining data into memory pages
	while ( bufPtr < bufEnd ) {
		// breathe
		QUICKPOLL(niceness);
		// ensure "size" is not too big.
		// adjust "size" if so,so we won't exceed the mem page boundary
		if ( bufPtr + size > bufEnd ) size = bufEnd - bufPtr;

		// add the page to memory. 
		// "bufPtr" is the data we read from disk. 
		// "size" is where to start writing relative to this memory 
		//  page's start.
		// "skip" is how many bytes to write into this "page". 
		addPage ( vfd , diskPageNum , bufPtr , size , skip );

		// advance disk data buf over what we stored into the mem page
		bufPtr += size;
		// advance DISK page # 
		diskPageNum++;
		// assume we will be filling up the next mem page fully
		size    = m_diskPageSize;
		// skip is offset from beginning of the memory page 
		skip    = 0;
	}

}

// . convert our MEMORY offset into an actual ptr to a chunk of memory
// . this makes our memory pooling approach transparent
// . "off" is offset into the memory
// . "off" includes HEADERSIZE headers in it
char *DiskPageCache::getMemPtrFromMemOff ( int32_t off ) {

	if ( off < 0 ) return NULL; // NULL means not in DiskPageCache

	// for some reason profiler cores all the time in here
	// and m_numPageSets is 0 like we got reset
	//if ( g_profiler.m_realTimeProfilerRunning ) return NULL;

	// get set number
	int32_t sn = off / m_maxPageSetSize ;
	// get offset from within the chunk of memory (within the set)
	//int32_t poff = off & (m_maxPageSetSize-1);
	int32_t poff = off % (m_maxPageSetSize);
	// . sanity check
	// . offset must be multiple of m_diskPageSize+HEADERSIZE, no cuz we skip
	//   ahead X bytes of a page set boundary...
	//int32_t off2 = off - sn * m_maxPageSetSize;
	//if ( off2 != 0 && (off2% (m_diskPageSize+HEADERSIZE)) != 0) {
	//	char *xx = NULL; *xx = 0; }
	// if we are not in the first page set, advance by one chunk
	// because the first page is often mapped to by a truncated poff from
	// the previous page set
	//if ( sn > 0 && poff == 0 ) poff += m_diskPageSize + HEADER_SIZE;
	// if it would breech our PAGE_SET, up it
	if ( poff + m_diskPageSize + HEADERSIZE > m_maxPageSetSize) {poff=0; sn++;}
	// sanity check
	if ( sn >= m_numPageSets ) { char *xx = NULL; *xx = 0; }
	// return the proper ptr
	return (m_pageSet[sn]) + poff;
}

// . "diskPageNum" is the disk page # of the file with "vfd"
// . "page" points to the disk data we read from disk
// . "size" is how many bytes to write into the memory page, #pageNum
// . "skip" is the offset into the memory page we will write the disk data into
void DiskPageCache::addPage(int32_t vfd,
			    int32_t diskPageNum,
			    char *pageData,
			    int32_t size,
			    int32_t skip){

	// . if pageNum is beyond the file size
	// . see the explanation for this same error msg above
	if ( diskPageNum >= m_maxPagesInFile[vfd] ) { 
		// this has happened during a merge before!! (at startup)
		//log(LOG_LOGIC,"db: pagecache: addPage: Bad engineer. "
		// happens because rdbdump did not get a high enough 
		// maxfilesize so we did not make enough pages! we endedup
		// dumping more than what was end the tree because stuff was
		// added to the tree while dumping!
		log("db: pagecache: Caught add breach. "
		    "pageNum=%"INT32" max=%"INT32" db=%s", 
		    diskPageNum,m_maxPagesInFile[vfd],m_dbname);
		return;
	}

	// debug msg
	// log("addPage: vfd=%"INT32" diskPageNum=%"INT32" pageData[0]=%hhx "
	//     "size=%"INT32" skip=%"INT32"",
	//     vfd,diskPageNum,pageData[0],size,(int32_t)skip);

	// "poff" is the DISK page # for "vfd" (virtual file descriptor) and
	// it returns an offset to the page in memory.
	int32_t poff = m_memOffFromDiskPage [ vfd ] [ diskPageNum ] ;

	int32_t oldDiskPage;

	// p will be NULL if page does not have any data in memory yet
	//char *p = getMemPtrFromMemOff ( poff );

	// if page already exists in cache and needs data on the boundaries
	// we may be able to supply it
	if ( poff >= 0 ) {
		// debug msg
		//log("ENHANCING off=%"INT32"",poff);
		enhancePage ( poff , pageData , size , skip );
		return;
	}

	// don't add any more if we're minimizing disk seeks and are full
	if ( m_minimizeDiskSeeks && 
	     m_numPagesPresentOfFile[vfd] >= m_maxPagesPerFile[vfd] )
		return;
		
	// top:
	// try to get an available memory spot from list
	if ( m_numAvailMemOffs > 0 ) {
		poff = m_availMemOff [ --m_numAvailMemOffs ] ;
		// debug msg
		//log("RECYCLING off=%"INT32" numAvailMemOffs-1=%"INT32""
		//    ,poff,m_numAvailMemOffs);
	}
	// can we grab a page from memory without having to grow?
	else if ( m_nextMemOff + m_diskPageSize + HEADERSIZE < m_upperMemOff) {
		poff = m_nextMemOff;
		m_nextMemOff += m_diskPageSize + HEADERSIZE;
		// debug msg
		// log("CLAIMING off=%"INT32" (nextmemoff=%"INT32"",poff,
		//     m_nextMemOff);
	}
	// . we now grow everything at start
	// . otherwise, try to grow the page cache by 200k
	//else if ( m_nextMemOff + m_diskPageSize + HEADERSIZE < m_maxMem ) {
	//	// grow by 100k worth of pages each time
	//	if ( ! growCache ( m_upperMemOff + 200*1024 ) ) return;
	//	goto top;
	//}
	// this should never happen. Since in minimizeDiskSeek we have
	// an exact number of pages per file
	else if ( m_minimizeDiskSeeks ) {
		char *xx = NULL; *xx = 0;
	}
	// if no freebies left, take over the tail page in memory
	else {

		// STEAL IT!!
		poff = m_tailOff;


		// remove it from linked list. it will be re-added below @ head
		////
		// CAUTION:  THIS CHANGES m_tailOff!!!!!!
		///
		excisePage ( m_tailOff );


		// . the file no longer owns him
		// . this is a int32_t ptr to &m_bufOffs[vfd][pageNum]
		// . if that vfd no longer exists it should have added all its
		//   pages to m_avail list
		//int32_t tmp = -1;
		// WHY DOING THIS? 
		//int32_t memOff = -1;//NULL;
		//readFromCache(&memOff, poff, OFF_PTR, sizeof(int32_t));

		// the tail may actualy belong to a separated file with
		// a different vfd
		int oldVfd;
		readFromCache (&oldVfd,poff,OFF_VFD,sizeof(int32_t));
		readFromCache (&oldDiskPage,poff,OFF_DISKPAGENUM,
			       sizeof(int32_t));

		// did excise work?
		// this cored here from m_memOffFroMDiskPage[oldVfd] being
		// NULL, so how could that happen?
		if ( m_memOffFromDiskPage[oldVfd] &&
		     m_memOffFromDiskPage[oldVfd][oldDiskPage] != -1 ) {
			char *xx=NULL;*xx=0; }
		// did ex
		// seg faultint here: mdw:
		//*memOffPtr = -1;
		// how can this be, we subverted a valid buffer
		//if ( memOff == -1 ) { char *xx=NULL;*xx=0; }
		//poff = memOff;
		//m_cacheBuf.writeToCache(poff, OFF_PTR, &tmp, sizeof(int32_t));
		// testing
		//m_cacheBuf.readFromCache ( &tmp, poff+OFF_PTR, sizeof(int32_t) );
		//if ( tmp != -1 ){
		//char *xx=NULL; *xx=0;}
		//**(int32_t **)(p+OFF_PTR) = -1;
		// debug msg
		//log("KICKINGTAIL off=%"INT32"",poff);
	}
	// sanity check
	if ( poff < 0 ) { char *xx = NULL; *xx = 0; }
	// get ptr to the page in memory from the memory offset
	//p = getMemPtrFromMemOff ( poff );

	// store how many bytes we wrote into the memory page residing @ poff
	writeToCache(poff, OFF_SIZE, &size, sizeof(int32_t));

	// int32_t tmp = 0;
	// m_cacheBuf.readFromCache ( &tmp, poff, OFF_SIZE, sizeof(int32_t) );
	// if ( tmp != size ){
	//  char *xx=NULL; *xx=0;}
	//*(int32_t *)(p+OFF_SIZE) = size; 

	// store "skip" which is the offset into the memory page we start
	// storing the disk data into
	writeToCache( poff, OFF_SKIP, &skip, sizeof(int32_t) );

	//*(int32_t *)(p+OFF_SKIP) = skip;
	// sanity check
	if ( size + skip > m_diskPageSize ) { char *xx = NULL; *xx = 0; }

	// then store a ptr to m_memOffFromDiskPage[vfd][pageNum] so we can set
	// *ptr to -1 if they page gets replaced by another
	
	// store the offset of this memory page 
	//int32_t *memOffPtr = &m_memOffFromDiskPage[ vfd ][ pageNum ];


	// m_memOffFromDiskPage maps a vfd/pagenum to a memory page offset. 
	// -1 means none.
	// why do we need to store the memory offset in the memory page???
	//int32_t memOff = m_memOffFromDiskPage[ vfd ][ pageNum ];
	//writeToCache( poff, OFF_PTR, &memOff, sizeof(int32_t));
				 
	//*(int32_t **)(p+OFF_PTR) = &m_memOffFromDiskPage [ vfd ] [ pageNum ];

	// then the data from disk (skip over linked list info)
	// "skip" is how far into the memory page we should write the 
	// disk data because it is not aligned perfectly with the mem page.
	writeToCache( poff, HEADERSIZE + skip, pageData, size);

	//gbmemcpy ( p + HEADERSIZE + skip , page , size );

	// transform mem ptr to memory offset
	//if ( !m_useRAMDisk && ! m_useSHM ) {
	/*
	int32_t off = -1;
	char *p = getMemPtrFromMemOff ( poff );
	for ( int32_t i = 0 ; i < m_numPageSets ; i++ ) {
		if ( p < m_pageSet[i] ) continue;
		if ( p > m_pageSet[i] + m_pageSetSize[i] ) 
			continue;
		off = p - m_pageSet[i] + i * m_maxPageSetSize ;
		break;
	}
	*/

	// gotta record this now too!
	writeToCache( poff, OFF_DISKPAGENUM, &diskPageNum, sizeof(int32_t) );
	writeToCache( poff, OFF_VFD, &vfd, sizeof(int32_t) );

	// store the linked list information in the remaining header bytes
	// that we use for promoting heaviliy hit pages to the top of
	// thereby replacing the tail when adding new pages. this will
	// insert our page into the linked list. it will set the prev/next
	// mem page offsets in the header of this memory page.
	promotePage ( poff , true/*isNew?*/ ); 

	// update map. map disk page # to mem offset.
	m_memOffFromDiskPage [ vfd ] [ diskPageNum ] = poff;


	// sanity check
	//if ( off != poff ) { char *xx=NULL; *xx=0; }
	//}
	//else
	//	m_memOffFromDiskPage [ vfd ] [ pageNum ] = poff;


	// update the header of that page
	
	// we have added the page!
	if ( m_minimizeDiskSeeks )
		m_numPagesPresentOfFile[vfd]++;

}

// . add data from "page" (we just read it from disk or wrote to disk) 
// . "poff" is the memory page # that will receive the disk data
// . "page" points to the disk data we read from disk to be stored into mem pg
// . "size" is how many bytes to write into the memory page, #pageNum
// . "skip" is the offset into the memory page we will write the disk data into
void DiskPageCache::enhancePage (int32_t poff, char *page, int32_t size, 
				 int32_t skip) {

	int32_t psize = 0;
	readFromCache( &psize, poff, OFF_SIZE, sizeof(int32_t));
	//int32_t psize = *(int32_t *)(p+OFF_SIZE);
	int32_t pskip = 0;
	readFromCache( &pskip, poff, OFF_SKIP, sizeof(int32_t));
	//int32_t pskip = *(int32_t *)(p+OFF_SKIP);
	// can we add to front of page?
	if ( skip < pskip ) {
		int32_t diff = pskip - skip;
		// . we cored here because page[diff-1] was out of bounds. why?
		// . do not allow gap in between cached data, that is, we have 
		//   cached bytes at the end of the page, then we try to cache 
		//   some at the beginning, and it's not contiguous... we are
		//   not built for that... this can happen when dumping a file,
		//   if your first reads up to the file end (somewhere in the
		//   middle of the page) and your second read starts somewhere
		//   else.... mmmm... i dunno....
		if ( skip + size < pskip || diff > size ) { 
			log("db: Avoided cache gap in %s. diff=%"INT32" "
			    "size=%"INT32" pskip=%"INT32" skip=%"INT32".",
			    m_dbname,diff,size,(int32_t)pskip,(int32_t)skip);
			return;
		}
		writeToCache(poff, HEADERSIZE + skip , page , diff);
		//gbmemcpy ( p + HEADERSIZE + skip , page , diff );
		psize += diff;
		pskip -= diff;
		writeToCache(poff, OFF_SIZE, &psize, sizeof(int32_t));
		//*(int32_t *)(p+OFF_SIZE) = psize ;
		writeToCache(poff, OFF_SKIP, &pskip, sizeof(int32_t));
		//*(int32_t *)(p+OFF_SKIP) = pskip ;
	}
	// can we add to end of page?
	int32_t pend = pskip + psize;
	int32_t  end = skip  +  size;
	if ( end <= pend ) return;
	int32_t diff = end - pend ;
	// if the read's starting point is beyond our ending point, bail,
	// we don't want any holes...
	if ( diff > size ) return;
	writeToCache(poff, HEADERSIZE + pend, page + size - diff, diff);
	//gbmemcpy ( p + HEADERSIZE + pend , page + size - diff , diff );
	int32_t tmp = psize+diff;
	writeToCache(poff, OFF_SIZE, &tmp, sizeof(int32_t));
	//*(int32_t *)(p+OFF_SIZE) = (int32_t)psize + diff;
}

// the link information is bytes 8-16 of each page in mem (next/prev mem ptrs)
void DiskPageCache::promotePage ( int32_t poff , bool isNew ) {

	if ( isNew ) {
	here:
		// store a -1 to indicate previous page offset.
		// we are the head of the linked list now, so -1 means none.
		int32_t tmp = -1;
		writeToCache(poff, OFF_PREV, &tmp, sizeof(int32_t));
		// testing
		readFromCache ( &tmp, poff, OFF_PREV, sizeof(int32_t) );
		if ( tmp != -1 ){
			char *xx=NULL; *xx=0;}
		//*(int32_t *)(p + OFF_PREV) = -1 ;// our prev is -1 (none)
		// store the next page in the linked list who WAS the head
		// it could be -1 if we are the first entry intothe linked list
		writeToCache(poff, OFF_NEXT, &m_headOff, sizeof(int32_t));
		//*(int32_t *)(p+OFF_NEXT)=m_headOff;//our next is the old head
		// the old head's prev is us
		if ( m_headOff >= 0 ) {
			writeToCache(m_headOff,OFF_PREV,&poff,sizeof(int32_t));
			//char *headPtr = getMemPtrFromMemOff ( m_headOff ) ;
			//*(int32_t *)(headPtr + OFF_PREV) = poff;
		}
		// and we're the new head
		m_headOff = poff;
		// if no tail, we become that, too, we must be the first
		if ( m_tailOff < 0 ) m_tailOff = poff;
		return;
	}
	// otherwise, we have to excise
	excisePage ( poff );
	// and add as new
	goto here;
}

// remove a page from the linked list
void DiskPageCache::excisePage ( int32_t poff ) {

	// get our neighbors, NULL if none
	int32_t prev = 0;
	readFromCache(&prev, poff, OFF_PREV, sizeof(int32_t));
	//int32_t prev = *(int32_t *)(p + OFF_PREV);
	int32_t next = 0;
	readFromCache(&next, poff, OFF_NEXT, sizeof(int32_t));
	//int32_t next = *(int32_t *)(p + OFF_NEXT);
	// if we were the head or tail, then pass it off to our neighbor
	if ( poff == m_headOff ) m_headOff = next;
	if ( poff == m_tailOff ) m_tailOff = prev;
	// our prev's next becomes our old next
	if ( prev >= 0 ) {
		//char *prevPtr = getMemPtrFromMemOff ( prev );
		writeToCache(prev, OFF_NEXT, &next, sizeof(int32_t));
		//*(int32_t *)(prevPtr + OFF_NEXT ) = next;
	}
	// our next's prev becomes our old prev
	if ( next >= 0 ) {
		//char *nextPtr = getMemPtrFromMemOff ( next );
		writeToCache(next, OFF_PREV, &prev, sizeof(int32_t));
		//int32_t *)(nextPtr + OFF_PREV ) = prev;
	}

	// what is the tail's disk page # so we can update
	// m_memOffFromDiskPage[vfd][tailDiskPageNum] ?
	int32_t diskPageNum;
	readFromCache ( &diskPageNum,poff,OFF_DISKPAGENUM,sizeof(int32_t) );

	int vfd;
	readFromCache ( &vfd,poff,OFF_VFD,sizeof(int32_t) );
	
	// the memory page we are commandeering should no longer be 
	// mapped to from its disk page
	if ( m_memOffFromDiskPage [ vfd ] )
		m_memOffFromDiskPage [ vfd ] [ diskPageNum ] = -1;
}

// . grow/shrink m_memOffFromDiskPage[] which maps vfd/page to a mem offset
// . returns false and sets g_errno on error
// . called by DiskPageCache::open()/close() respectively
// . fileSize is so we can alloc m_memOffFromDiskPage[vfd] big enough 
//   for all pgs
int32_t DiskPageCache::getVfd ( int64_t maxFileSize, bool vfdAllowed ) {

	// check for override function
	//if ( m_isOverriden ) {
	//	return m_getVfd2 ( this, maxFileSize );
	//}

	// for RAMDisks, do not cache disk
	// pages from the indexdb root file, nor, any indexdb file that is
	// larger than twice the "maxMemForRamDisk" value
	/*
	if ( m_useRAMDisk && maxFileSize > (m_maxMem * 2) ){
		log (LOG_INFO,"db: getvfd: cannot cache on RAMDisk files that "
		     "larger than twice the max mem value. fileSize=%"INT32"",
		     m_maxMem);
		return -1;
	}
	*/

	int32_t  numPages = (maxFileSize / m_diskPageSize) + 1;

	// RESTRICT to only the first m_maxMemOff worth of files, 
	// starting with the SMALLEST file first. so if maxMemoff is 50MB, and
	// we have 5 files that are 10,20,30 & 40MB,
        // then we use 10MB for the first file, 20MB of the 2nd BUT only
        // 20MB for the 3rd file, and the 4th file does not get any page cache.
        // if doing "biased lookups" each file is virtually half the actual
        // size, and this allocates page cache appropriately.
	
	// don't to do a page cache for an indexdb0001.dat that is 100GB 
	// because we'd have to allocate too much mem for the 
	// m_memOffFromDiskPage[] array
	// so for the parital file make sure its less than 1 GB
	if ( m_minimizeDiskSeeks && !vfdAllowed ){
		log (LOG_INFO,"db: getVfd: cannot cache because minimizing "
		     "disk seeks. numPages=%"INT32"", numPages);
		return -1;
	}
			
	// . pick a vfd for this BigFile to use
	// . start AFTER last pick in case BigFile closed, released its
	//   m_vfd, a read thread returned and called addPages() using that
	//   old m_vfd!!!!!!! TODO: can we fix this better?
	int32_t i ;
	int32_t count = MAX_NUM_VFDS2;
	for ( i = m_nexti ; count-- > 0 ; i++ ) {
		if ( i >= MAX_NUM_VFDS2 ) i = 0; // wrap
		if ( ! m_memOffFromDiskPage [ i ] ) break;
	}
	// bail if none left
	if ( count == 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"db: pagecache: getvfd: no vfds remaining.");
		//char *xx = NULL; *xx = 0;
		return -1;
	}
	// . file size has to be below 2 gigs because m_memOffFromDiskPage is 
	//   only a int32_t
	// . if we need to we could transform m_memOffFromDiskPage into 
	//   m_memPageNum
	//if ( maxFileSize > 0x7fffffffLL ) {
	//	g_errno = EBADENGINEER;
	//	log("DiskPageCache::getVfd: maxFileSize too big");
	//	return -1;
	//}
	// assign it
	int32_t vfd = i;
	// start here next time
	m_nexti = i + 1;
	// say which cache it is
	

	// alloc the map space for this file
	int32_t  need     = numPages * sizeof(int32_t) ;
	int32_t *buf      = (int32_t *)mmalloc ( need , m_memTag );
	if ( ! buf ) {
		log("db: Failed to allocate %"INT32" bytes for page cache "
		    "structures for caching pages for vfd %"INT32". "
		    "MaxfileSize=%"INT64". Not enough memory.",
		    need,i,maxFileSize);
		return -1;
	}
	m_memOffFromDiskPage [ vfd ] = buf;
	m_maxPagesInFile     [ vfd ] = numPages;

	// keep a tab on the number of pages we can store of the file
	if ( m_minimizeDiskSeeks ){
		m_numPagesPresentOfFile[vfd] = 0;
		if ( m_memFree > numPages * ( HEADERSIZE + m_diskPageSize ) )
			m_maxPagesPerFile[vfd] = numPages;
		else 
			m_maxPagesPerFile[vfd] = m_memFree / ( m_diskPageSize + 
							       HEADERSIZE );
	}
			
	// add it in
	m_memAlloced += need;
	// debug msg
	//log("%s adding %"INT32"",m_dbname,need);
	// no pages are in memory yet, so set offsets to -1
	for ( i = 0 ; i < numPages ; i++ ) 
		m_memOffFromDiskPage [ vfd ] [ i ] = -1;

	// if minimizing disk seeks then calculate the memory used
	if ( m_minimizeDiskSeeks ){
		m_memFree -= maxFileSize;
		// if the file is bigger than the mem only partially store it
		if ( m_memFree < 0 )
			m_memFree = 0;
	}
	// debug msg
	//log("ALLOCINGFILE pages=%"INT32"",numPages);
	return vfd;
}

// when a file loses its vfd this is called
void DiskPageCache::rmVfd  ( int32_t vfd ) {

	// check for override function
	//if ( m_isOverriden ) {
	//	m_rmVfd2 ( this, vfd );
	//	return;
	//}
	// ensure validity
	if ( vfd < 0 ) return;

	// if 0 bytes are allocated for disk cache, just skip this junk
	if ( m_maxMem <= 0 ) return;

	// this vfd may have already been nuked by call to unlink!
	if ( ! m_memOffFromDiskPage [ vfd ] ) return;
	// add valid offsets used by vfd into m_availMemOff
	for ( int32_t i = 0 ; i < m_maxPagesInFile [ vfd ] ; i++ ) {
		int32_t off = m_memOffFromDiskPage [ vfd ] [ i ];
		// a -1 offset means empty
		if ( off < 0 ) continue;
		// sanity check
		if ( m_numAvailMemOffs >= m_maxAvailMemOffs ) {
			char *xx = NULL; *xx = 0; }
		// debug msg
		//log("MAKING off=%"INT32" available. na=%"INT32"",
		// off,m_numAvailMemOffs+1);
		// store it in list of available memory offsets so some other
		// file can use it
		m_availMemOff [ m_numAvailMemOffs++ ] = off;
		//log("disk: m_numAvailMemOffs+1 -> %"INT32,m_numAvailMemOffs);
		// set this to -1 i guess. it'll be freed below anyway.
		m_memOffFromDiskPage [ vfd ] [i] = -1;
		// remove that page from linked list, too
		//char *p = getMemPtrFromMemOff ( off );
		excisePage ( off );
	}
	// free the map that maps this files pages on disk to pages/offs in mem
	int32_t size = m_maxPagesInFile[vfd] * sizeof(int32_t);
	mfree ( m_memOffFromDiskPage [ vfd ] , size , "DiskPageCache" );
	m_memOffFromDiskPage [ vfd ] = NULL;
	// debug msg
	//log("%s rmVfd: vfd=%"INT32" down %"INT32"",m_dbname,vfd,size);
	m_memAlloced -= size;
	if ( m_minimizeDiskSeeks ){
		m_memFree += m_maxPagesPerFile[vfd] * m_diskPageSize;
		m_maxPagesPerFile[vfd] = 0;
		m_numPagesPresentOfFile[vfd] = 0;
	}
}

// use "mem" bytes of memory for the cache
bool DiskPageCache::growCache ( int32_t mem ) {
	// debug msg
	//log("GROWING PAGE CACHE from %"INT32" to %"INT32" bytes (%"XINT64")" 
	//    ,m_upperMemOff, mem ,(uint64_t)this);
	// don't exceed the max
	if ( mem > m_maxMem ) mem = m_maxMem; 
	// bail if we wouldn't be growing
	if ( mem <= m_upperMemOff ) return true;
	// how many pages? round up.
	int32_t npages = mem/(m_diskPageSize+HEADERSIZE) + 1;

	// . we need one "available" slot for each page in the cache
	// . this is a list of memory offsets that are available
	int32_t oldSize = m_maxAvailMemOffs * sizeof(int32_t) ;
	int32_t newSize = npages            * sizeof(int32_t) ;
	int32_t *a=(int32_t *)mrealloc(m_availMemOff,oldSize,newSize,m_memTag);
	if ( ! a ) return log("db: Failed to regrow page cache from %"INT32" to "
			      "%"INT32" bytes. Not enough memory.",oldSize,newSize);
	m_availMemOff     = a;
	m_maxAvailMemOffs = npages;
	m_memAlloced += (newSize - oldSize);
	// debug msg
	//log("%s growCache: up %"INT32"",m_dbname,(newSize - oldSize));

	// how much more mem do we need to alloc?
	int32_t need = mem - m_upperMemOff ;
	// how big is our last page set?
	int32_t size = 0;
	char *ptr = NULL;
	int32_t    i = 0;
	if ( m_numPageSets > 0 ) {
		// since we allocate everything at init this shouldn't happen
		char *xx=NULL; *xx=0;
		i    = m_numPageSets - 1;
		ptr  = m_pageSet     [ i ];
		size = m_pageSetSize [ i ];
	}
	// realloc him
	int32_t extra = m_maxPageSetSize - size ;
	if ( extra > need ) extra = need;
	/*
	if ( m_useRAMDisk ){
		// since RAMdisk it creates a file, no reason to alloc
		m_memAlloced = need;
		m_upperMemOff = need;
		return true;
	}
	// and shared mem already has the mem at this point
	if ( m_useSHM ) {
		m_memAlloced = need;
		m_upperMemOff = need;
		return true;
	}
	*/

	char *s = (char *)mrealloc ( ptr , size , size + extra, 
				     m_memTag);
	if ( ! s ) return log("db: Failed to allocate %"INT32" bytes more "
			      "for pagecache.",extra);
	m_pageSet     [ i ] = s;
	m_pageSetSize [ i ] = size + extra;
	// if we are not adding to an existing, we are a new page set
	if ( ! ptr ) m_numPageSets++;
	// discount it
	need -= extra;
	// add to alloc count
	m_memAlloced  += extra;
	m_upperMemOff += extra;
	// debug msg
	//log("%s growCache2: up %"INT32"",m_dbname,extra);
	// if we do not need more, we are done
	if ( need == 0 ) return true;
	// otherwise, alloc new page sets until we hit it
	for ( i++ ; i < MAX_PAGE_SETS && need > 0 ; i++ ) {
		int32_t size = need;
		if ( size > m_maxPageSetSize ) size = m_maxPageSetSize;
		need -= size;
		m_pageSet[i] = (char *) mmalloc ( size , m_memTag );
		if ( ! m_pageSet[i] ) break;
		m_pageSetSize[i]  = size;
		m_memAlloced     += size;
		m_upperMemOff    += size;
		m_numPageSets++;
		// debug msg
		//log("%s growCache3: up %"INT32"",m_dbname,size);
	}
	// update upper bound
	if ( need == 0 ) return true;
	return log(LOG_LOGIC,"db: pagecache: Bad engineer. Weird problem.");
}

int32_t DiskPageCache::getMemUsed ( ) {
	return m_nextMemOff - m_numAvailMemOffs * (m_diskPageSize+HEADERSIZE);
}

bool DiskPageCache::verifyData2 ( int32_t vfd ) {
	// ensure validity
	//if ( vfd < 0 ) return true;
	for ( int vfd = 0 ; vfd < 10 ; vfd++ ) {
	// this vfd may have already been nuked by call to unlink!
		if ( ! m_memOffFromDiskPage [ vfd ] ) continue;//return true;
	// debug msg
	//log("VERIFYING PAGECACHE vfd=%"INT32" fn=%s",vfd,f->getFilename());
	// read into here
	// add valid offsets used by vfd into m_availMemOff
	for ( int32_t i = 0 ; i < m_maxPagesInFile [ vfd ] ; i++ ) {
		int32_t off = m_memOffFromDiskPage [ vfd ] [ i ];
		// if page not in use, skip it
		if ( off < 0 ) continue;
		// check this now too
		int32_t storedvfd;
		readFromCache ( &storedvfd,
				off , 
				OFF_VFD,
				sizeof(int32_t) );
		if ( storedvfd != vfd ) { char *xx=NULL;*xx=0; }
		// ensure we are in sync with the map of diskpage to mem
		int32_t storedDiskPageNum;
		readFromCache ( &storedDiskPageNum , 
				off , 
				OFF_DISKPAGENUM,
				sizeof(int32_t) );
		if ( storedDiskPageNum != i ) { char *xx=NULL;*xx=0; }

	}
	}
	return true;
}


#include "BigFile.h"
#include "Threads.h"

bool DiskPageCache::verifyData ( BigFile *f ) {
	int32_t vfd = f->getVfd();
	// ensure validity
	if ( vfd < 0 ) return true;
	// this vfd may have already been nuked by call to unlink!
	if ( ! m_memOffFromDiskPage [ vfd ] ) return true;
	// debug msg
	//log("VERIFYING PAGECACHE vfd=%"INT32" fn=%s",vfd,f->getFilename());
	// read into here
	char buf [ 32 * 1024 ];//GB_PAGE_SIZE ]; //m_diskPageSize ];
	// ensure threads disabled
	bool on = ! g_threads.areThreadsDisabled();
	if ( on ) g_threads.disableThreads();
	// disable ourselves
	disableCache();
	// add valid offsets used by vfd into m_availMemOff
	for ( int32_t i = 0 ; i < m_maxPagesInFile [ vfd ] ; i++ ) {
		int32_t off = m_memOffFromDiskPage [ vfd ] [ i ];
		// if page not in use, skip it
		if ( off < 0 ) continue;

		// ensure we are in sync with the map of diskpage to mem
		int32_t storedDiskPageNum;
		readFromCache ( &storedDiskPageNum , 
				off , 
				OFF_DISKPAGENUM,
				sizeof(int32_t) );
		if ( storedDiskPageNum != i ) { char *xx=NULL;*xx=0; }

		// check this now too
		int32_t storedvfd;
		readFromCache ( &storedvfd,
				off , 
				OFF_VFD,
				sizeof(int32_t) );
		if ( storedvfd != vfd ) { char *xx=NULL;*xx=0; }

		//char *p = getMemPtrFromMemOff ( off );
		int32_t size = 0;
		readFromCache(&size, off, OFF_SIZE, sizeof(int32_t));
		//int32_t size = *(int32_t *)(p+OFF_SIZE);
		int32_t skip = 0;
		readFromCache(&skip, off, OFF_SKIP, sizeof(int32_t));
		if ( size > 32 * 1024 ){
			char *xx=NULL; *xx=0; }
		//int32_t skip = *(int32_t *)(p+OFF_SKIP);
		FileState fstate;
		if ( ! f->read ( buf           ,
				 size          ,
				 ((int64_t)i * (int64_t)m_diskPageSize) + 
				                 (int64_t)skip ,
				 &fstate       ,
				 NULL          ,  // state
				 NULL          ,  // callback
				 0             )){// niceness
			// core if it did not complete
			char *xx = NULL; *xx = 0; }
		// compare to what we have in mem
		log("checking vfd=%"INT32" "
		    "diskpage # %"INT32" size=%"INT32" skip=%"INT32""
		    , (int32_t)vfd , i, size, skip);
		char buf2[32 * 1024];
		readFromCache( buf2, off, HEADERSIZE + skip, size );
		if ( memcmp ( buf, buf2, size ) != 0 ){
			char *xx = NULL; *xx = 0; }
		//if ( memcmp ( buf , p + HEADERSIZE + skip, size ) != 0 ) {
		//char *xx = NULL; *xx = 0; }
	}
	if ( on ) g_threads.enableThreads();
	enableCache();
	// debug msg
	log("DONE VERIFYING PAGECACHE");
	return true;
}

// bigOff is used to get the MemPtr, smallOff is the offset in the Mem
void DiskPageCache::writeToCache( int32_t memOff,
				  int32_t memPageOff ,
				  void *inBuf, 
				  int32_t size ){

	/*
#ifdef GBUSESHM
	if ( m_useSHM ) {
		// what page are we on?
		int32_t page = ( bigOff + smallOff ) / m_maxAllocSize;
		// offset within that page
		int32_t poff = ( bigOff + smallOff ) % m_maxAllocSize;
		// sanity check
		if ( page >= m_numShmids ) { char *xx=NULL; *xx=0; }
		// sanity check
		if ( poff + size > m_shmidSize[page] ) { char *xx=NULL;*xx=0; }
		// get first byte
		int shmid = m_shmids[page];
		// assume we already have it loaded in
		char *mem = s_mem;
		// . is this the page we currently have loaded?
		// . th shmdt and shmat() seems to take about 12 microseconds
		//   on avg to execute. so about 100 times per milliseconds.
		// . seems like the writeToCache() is 3x slower than the
		//   readFromCache() perhaps because the dirty pages are 
		//   COPIED back into system mem?
		if ( shmid != s_shmid ) {
			// time it
			//int64_t start = gettimeofdayInMicroseconds();
			// free current i guess
			if ( s_mem && shmdt ( s_mem ) == -1 ) {
				log("disk: shmdt: %s",mstrerror(errno));
				char *xx=NULL;*xx=0;
			}
			// load it in if not
			mem = (char *) shmat ( shmid , NULL, SHM_R|SHM_W );
			// if this happens at startup, try calling shmat
			// when we init this page cache above...
			if ( mem == (char *)-1 ) {
				log("disk: shmat: %s",mstrerror(errno));
				char *xx=NULL;*xx=0; 
			}
			// store it
			s_mem   = mem;
			s_shmid = shmid;
			// time it
			//int64_t took = gettimeofdayInMicroseconds() -start;
			//if ( took > 1 ) 
			//	logf(LOG_DEBUG,"disk: took %"INT64" us to write "
			//	     "to shm page cache shmid=%"INT32".",took,
			//	     (int32_t)shmid);
		}
		// store it into the cache
		gbmemcpy ( mem + poff , inBuf , size );
		return;
	}
#endif

	if ( m_useRAMDisk ){
		int32_t numBytesWritten = pwrite( m_ramfd, inBuf, size, 
					       bigOff + smallOff );
		if ( numBytesWritten != size ){
			char *xx=NULL; *xx=0;
		}
		return;
	}

	*/
	char *p = getMemPtrFromMemOff ( memOff );
	gbmemcpy(p + memPageOff, inBuf, size);
}

// . store cached disk info into "outBuf". up to "size" bytes of it.
void DiskPageCache::readFromCache( void *outBuf, 
				   int32_t memOff,
				   int32_t pageOffset,
				   int32_t bytesToCopy ) {
	/*
#ifdef GBUSESHM
	if ( m_useSHM ) {
		// what page are we on?
		int32_t page = ( bigOff + smallOff ) / m_maxAllocSize;
		// offset within that page
		int32_t poff = ( bigOff + smallOff ) % m_maxAllocSize;
		// sanity check
		if ( page >= m_numShmids ) { char *xx=NULL; *xx=0; }
		// sanity check
		if ( poff + size > m_shmidSize[page] ) { char *xx=NULL;*xx=0; }
		// get first byte
		int shmid = m_shmids[page];
		// assume we already have it loaded in
		char *mem = s_mem;
		// . is this the page we currently have loaded?
		// . the shmdt() and shmat() seems to take about 2 MICROSECONDS
		//   on avg to execute here. about 3x faster than the
		//   writeToCache() above.
		if ( shmid != s_shmid ) {
			// time it
			//int64_t start = gettimeofdayInMilliseconds();
			// free current first so shmat has some room?
			if ( s_mem && shmdt ( s_mem ) == -1 ) {
				log("disk: shmdt: %s",mstrerror(errno));
				char *xx=NULL;*xx=0;
			}
			// load it in if not
			mem = (char *) shmat ( shmid , NULL, SHM_R|SHM_W );
			// if this happens at startup, try calling shmat
			// when we init this page cache above...
			if ( mem == (char *)-1 ) {
				log("disk: shmat: %s",mstrerror(errno));
				char *xx=NULL;*xx=0; 
			}
			// store it
			s_mem   = mem;
			s_shmid = shmid;
			// time it
			//int64_t took = gettimeofdayInMilliseconds() -start;
			//if ( took > 1 ) 
			//	logf(LOG_DEBUG,"disk: took %"INT64" ms to read "
			//	     "to shm page cache shmid=%"INT32".",took,
			//	     (int32_t)shmid);
		}
		// store it in outBuf
		gbmemcpy ( outBuf , mem + poff , size );
		return;
	}
#endif

	if ( m_useRAMDisk ) {
		int32_t numBytesRead = pread( m_ramfd, outBuf, size, 
					   bigOff + smallOff );
		if ( numBytesRead != size ){
			char *xx=NULL; *xx=0;
		}
		return;
	}

	*/
	// the old fashioned way
	char *p = getMemPtrFromMemOff ( memOff );
	gbmemcpy(outBuf, p + pageOffset, bytesToCopy );
}

// lastly, we need some way to "force" a merge at around midnight when traffic
// is minimal, or when there are 3 or more indexdb files that are less than
// 80% in the indexdb disk page cache. because that means we are starting to
// do a lot of disk seeks. 
// checks if indexdb needs merge
/*
bool DiskPageCache::needsMerge( ){
	if ( !m_useRAMDisk ) return false;
	int32_t numVfds = 0;
	for ( int32_t i = 0; i < MAX_NUM_VFDS2; i++ ){
		if ( !m_memOffFromDiskPage[i] ) continue;
		// check to see if a file is less than 80% in the indexdb
		// disk page cache
		int32_t numOffsUsed = 0;
		for ( int32_t j = 0; j < m_maxPagesInFile[i]; j++ ){
			if ( m_memOffFromDiskPage[i][j] >= 0 )
				numOffsUsed++;
		}
		if ( (numOffsUsed * 100)/m_maxPagesInFile[i] < 80 )
			numVfds++;
	}
	if ( numVfds >= 3 )
		return true;
	return false;
}
*/

// 'ipcs -m' will show shared mem in linux
void freeAllSharedMem ( int32_t max ) {

	// free shared mem whose pid no longer exists
	//struct shmid_ds buf;
	//shmctl ( 0 , SHM_STAT , &buf );
	//int shmctl(int shmid, int cmd, struct shmid_ds *buf);

	/*
#ifdef GBUSESHM
	// types.h uses key_t type that shmget uses
	// try to nuke it all
	for ( int32_t i = 0 ; i < max ; i++ ) {
		int shmid = i;
		int32_t status = shmctl ( shmid , IPC_RMID , NULL);
		if ( status == -1 ) {
			//if ( errno != EINVAL )
			//	log("db: shctlt %"INT32": %s",(int32_t)shmid,mstrerror(errno));
		}
		else
			log("db: Removed shmid %"INT32"",i);
	}
#endif
	*/

}

// types.h uses key_t type that shmget uses
#undef key_t
