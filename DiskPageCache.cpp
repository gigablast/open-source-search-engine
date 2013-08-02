#undef _XOPEN_SOURCE // needed for pread and pwrite
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "DiskPageCache.h"
#include "RdbMap.h"    // GB_PAGE_SIZE
#include "Indexdb.h"

// types.h uses key_t type that shmget uses
#undef key_t

#include <sys/ipc.h>  // shmget()
#include <sys/shm.h>  // shmget()

#define OFF_SIZE 0
#define OFF_SKIP 4
#define OFF_PREV 8
#define OFF_NEXT 12
#define OFF_PTR  16

#define oldshort long

DiskPageCache::DiskPageCache () {
	m_numPageSets = 0;
	// sometimes db may pass an unitialized DiskPageCache to a BigFile
	// so make sure when BigFile::close calls DiskPageCache::rmVfd() our
	// m_memOff vector is all NULLed out, otherwise it will core
	memset ( m_memOff , 0 , 4 * MAX_NUM_VFDS2 );
	m_availMemOff = NULL;
	//m_isOverriden = false;
	reset();
}

DiskPageCache::~DiskPageCache() {
	reset();
}

static char *s_mem = NULL;
static int   s_shmid = -1;

void DiskPageCache::reset() {
	if ( m_numPageSets > 0 ) 
		log("db: resetting page cache for %s",m_dbname);
	for ( long i = 0 ; i < m_numPageSets ; i++ ) {
		mfree ( m_pageSet[i], m_pageSetSize[i], "DiskPageCache");
		m_pageSet    [i] = NULL;
		m_pageSetSize[i] = 0;
	}
	// free all the m_memOffs[] arrays
	// free the map that maps this files pages on disk to pages/offs in mem
	for ( long i = 0 ; i < MAX_NUM_VFDS2 ; i++ ) {
		if ( ! m_memOff [ i ] ) continue;
		long size = m_maxPagesInFile[i] * sizeof(long);
		mfree ( m_memOff [ i ] , size , "DiskPageCache" );
		m_memOff [ i ] = NULL;
	}
	// and these
	if ( m_availMemOff ) {
		long size = m_maxAvailMemOffs * sizeof(long);
		mfree ( m_availMemOff , size , "DiskPageCache" );
	}
	// free current one, if exists
	if ( s_shmid >= 0 && s_mem ) {
		if ( shmdt ( s_mem ) == -1 )
			log("disk: shmdt: reset: %s",mstrerror(errno));
		s_mem   = NULL;
		s_shmid = -1;
	}
	// mark shared mem for destruction
	for ( long i = 0 ; m_useSHM && i < m_numShmids ; i++ ) {
		int shmid = m_shmids[i];
		if ( shmctl ( shmid , IPC_RMID , NULL) == -1 )
			log("db: shmctlt shmid=%li: %s",
			    (long)shmid,mstrerror(errno));
		else
			log("db: shmctl freed shmid=%li",(long)shmid);
	}
	m_numPageSets     = 0;
	m_nextMemOff      = 0;
	m_upperMemOff     = 0;
	m_maxMemOff       = 0;
	m_memAlloced      = 0;
	m_availMemOff     = NULL;
	m_numAvailMemOffs = 0;
	m_headOff         = -1;
	m_tailOff         = -1;
	m_enabled         = true;
	m_nexti           = 0;
	m_ramfd = -1;
	m_useRAMDisk = false;
	m_useSHM = false;
}

bool DiskPageCache::init ( const char *dbname ,
			   char rdbId,
			   long maxMem  ,
			   long pageSize,
			   bool useRAMDisk,
			   bool minimizeDiskSeeks ) {
			//   long maxMem ,
			//   void (*getPages2)(DiskPageCache*, long, char*,
			//		     long, long long, long*,
			//		     long long*),
			//   void (*addPages2)(DiskPageCache*, long, char*,
			//	   	     long, long long),
			//   long (*getVfd2)(DiskPageCache*, long long),
			//   void (*rmVfd2)(DiskPageCache*, long) ) {
	reset();

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

	bool useSHM = false;
	// a quick hacky thing, force them to use shared mem instead of ram dsk
	if ( useRAMDisk ) { 
		useRAMDisk = false;
		useSHM     = true;
	}
	// not for tmp cluster
	if ( g_hostdb.m_useTmpCluster ) useSHM = false;
	// it is off by default because it leaks easily (if u Ctrl+C the process)
	if ( ! g_conf.m_useSHM ) useSHM = false;
	// right now shared mem only supports a single page size because
	// we use s_mem/s_shmid, and if we have a small page size which
	// we free, then shmat() may get ENOMEM when trying to get the larger
	// of the two page sizes
	if ( useSHM && pageSize != GB_INDEXDB_PAGE_SIZE) {char *xx=NULL;*xx=0;}
	// don't use it until we figure out how to stop the memory from being
	// counted as being the process's memory space. i think we can make
	// shmat() use the same mem address each time...
	if ( useSHM ) {
		log("disk: shared mem currently not supported. Turn off "
		    "in gb.conf <useSharedMem>");
		char *xx=NULL;*xx=0;
	}
	// save it;
	m_useSHM = useSHM;
	// clear it
	m_numShmids = 0;
	// set this
	//m_maxAllocSize = 33554432;
	// the shared mem page size is a little more than the disk page size
	m_spageSize = pageSize + HEADERSIZE;
	// . this is /proc/sys/kernel/shmmax DIVIDED BY 2 on titan and gk0 now
	// . which is the max to get per call to shmat()
	// . making this smaller did not seem to have much effect on speed
	long max = 33554432/2;
	// make sure it is "pageSize" aligned so we don't split pages
	m_maxAllocSize = (max / m_spageSize) * m_spageSize;
	// set it up
	if ( m_useSHM ) {
		// we can only use like 30MB shared mem pieces
		long need = maxMem;
	shmloop:
		// how much to alloc now?
		long alloc = need;
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
		log("db: allocated %li bytes shmid=%li",alloc,(long)shmid);
		// add it to our list
		m_shmids    [ m_numShmids ] = shmid;
		m_shmidSize [ m_numShmids ] = alloc;
		m_numShmids++;
		// count it
		g_mem.m_sharedUsed += alloc;
		// log it for now
		//logf(LOG_DEBUG,"db: new shmid id is %li, size=%li",
		//     (long)shmid,(long)alloc);
		// subtract it
		need -= alloc;
		// get more
		if ( need > 0 ) goto shmloop;
	}

	// a malloc tag, must be LESS THAN 16 bytes including the NULL
	char *p = m_memTag;
	memcpy  ( p , "pgcache-" , 8 ); p += 8;
	if ( dbname ) strncpy ( p , dbname    , 8 ); 
	// so we know what db we are caching for
	m_dbname = p;
	p += 8;
	*p++ = '\0';
	// sanity check, we store bytes used as a short at top of page
	//if ( m_pageSize > 0x7fff ) { char *xx = NULL; *xx = 0; }
	// . do not use more than this much memory for caching
	// . it may go over by like 2% for header information
	m_maxMemOff = maxMem ;
	// set m_pageSetSize. use this now instead of m_maxPageSetSize #define
	long phsize = pageSize + HEADERSIZE;
	m_maxPageSetSize = (((128*1024*1024)/phsize)*phsize);
	m_pageSize     = pageSize;

	m_minimizeDiskSeeks = minimizeDiskSeeks;

	// we need to keep a count memory of files being cached
	if ( m_minimizeDiskSeeks )
		m_memFree = m_maxMemOff;

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

	// for now only indexdb will use the ramdisk
	if ( strcmp ( dbname, "indexdb" ) == 0 && useRAMDisk ){
		if ( !initRAMDisk( dbname, maxMem ) )
			return log ( "db: failed to init RAM disk" );
	}

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
bool DiskPageCache::initRAMDisk( const char *dbname, long maxMem ){
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

// . this returns true iff the entire read was copied into
//   "buf" from the page cache
// . it will move the used pages to the head of the linked list
// . if *buf is NULL we allocate here
void DiskPageCache::getPages   ( long       vfd         ,
				 char     **buf         ,
				 long       numBytes    ,
				 long long  offset      ,
				 long      *newNumBytes ,
				 long long *newOffset   ,
				 char     **allocBuf    ,
				 long      *allocSize   ,
				 long       allocOff    ) {
	// check for override function
	//if ( m_isOverriden ) {
	//	//log ( LOG_INFO, "cache: Get Pages [%li] [%li][%lli]",
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
	*newOffset   = offset;
	*newNumBytes = numBytes;

	// return if no pages allowed in page cache
	if ( m_maxMemOff == 0 ) return;
	// or disabled
	if ( ! m_enabled ) return;
	// disabled at the master controls?
	if ( m_switch && ! *m_switch ) return;

	// or if minimizeDiskSeeks did not accept the vfd
	if ( m_minimizeDiskSeeks && vfd < 0 )
		return;

	// or if no pages in this vfd
	if ( !m_memOff[vfd] ) 
		return;

	// debug point
	//if ( offset == 16386 && numBytes == 16386 ) 
	//	log("hey");

	// what is the page range?
	long sp = offset / m_pageSize ;
	long ep = (offset + (numBytes-1)) / m_pageSize ;

	// . sanity check
	// . we establish the maxPagesInFile when BigFile::open is called
	//   by RdbDump. Rdb.cpp calls m_dump.set with a maxFileSize based on
	//   the mem occupied by the RdbTree. BUT, recs can be added to the tree
	//   WHILE we are dumping, so we end up with a bigger file, and this
	//   disk page cache is not prepared for it!
	if ( ep >= m_maxPagesInFile[vfd] ) { 
		// happens because rdbdump did not get a high enough 
		// maxfilesize so we did not make enough pages! we endedup
		// dumping more than what was end the tree because stuff was
		// added to the tree while dumping!
		log("db: pagecache: Caught get breach. "
		    "ep=%li max=%li", ep,m_maxPagesInFile[vfd] );
		return;
		//char *xx = NULL; *xx = 0; 
	}

	char *bufPtr = *buf;
	char *bufEnd = *buf + numBytes;

	// our offset into first page on disk
	oldshort start1 = offset - sp * m_pageSize;
	// this is for second while loop
	oldshort start2 = 0;
	if ( ep == sp ) start2 = start1;

	// store start pages
	while ( sp <= ep ) {
		// the page offset in memory
		long poff = m_memOff[vfd][sp];
		// get a ptr to it
		//char *s = getMemPtrFromOff ( poff );
		// break if we do not have page in memory
		//if ( ! s ) break;
		if ( poff < 0 ) break;
		// first 2 bytes of page is how many bytes are used in page
		oldshort size = 0;
		readFromCache( &size, poff, OFF_SIZE, sizeof(oldshort));
		//oldshort size = *(oldshort *)(s+OFF_SIZE);
		// second set of 2 bytes is offset of data from page boundary
		oldshort skip = 0;
		readFromCache( &skip, poff, OFF_SKIP, sizeof(oldshort));
		//oldshort skip = *(oldshort *)(s+OFF_SKIP);
		// debug msg
		//log("getPage: pageNum=%li page[0]=%hhx size=%li skip=%li",
		//    sp,s[HEADERSIZE],(long)size,(long)skip);
		// if this page data starts AFTER our offset, it is no good
		if ( skip > start1 ) break;
		// adjust size by our page offset, we won't necessarily be
		// starting our read at "skip"
		size -= (start1 - skip);
		// if size is 0 or less all cached data was below our offset
		if ( size <= 0 ) break;
		// . promote this page in the linked list
		// . bytes 8-16 of each page in memory houses the
		//   next and prev ptrs to pages in memory
		promotePage ( poff , false );
		// allocate the read buffer if we need to
		if ( ! *buf ) {
			// allocate enough room for allocOff, too
			long need = numBytes + allocOff;
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
		readFromCache(bufPtr, poff, HEADERSIZE + start1 , size);
		//memcpy ( bufPtr , s + HEADERSIZE + start1 , size );
		bufPtr       += size;
		*newOffset   += size;
		*newNumBytes -= size;
		// return if we got it all
		if ( bufPtr >= bufEnd ) { m_hits += 1; return; }
		// otherwise, advance to next page
		sp++;
		// and our page relative offset is zero now, iff ep > sp
		if ( sp <= ep ) start1 = 0;
		// if the cached page ended before the physical page, break out
		// because we don't want any holes
		readFromCache( &size, poff, OFF_SIZE, sizeof(oldshort));
		if ( skip + size < m_pageSize ) break;
		//if ( skip + *(oldshort *)(s+OFF_SIZE) < m_pageSize ) break;
	}

	// now store from tail down
	/*
	while ( ep > sp ) {
		// the page offset in memory
		long poff = m_memOff[vfd][ep];
		// get a ptr to it
		char *s = getMemPtrFromOff ( poff );
		// break if we do not have page in memory
		if ( ! s ) break;
		// first 2 bytes of page is how many bytes are used
		oldshort size = *(oldshort *)s;
		// second set of 2 bytes is offset from boundary
		oldshort skip = *(oldshort *)(s+OFF_SKIP);
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
		memcpy ( bufEnd - size , s + HEADERSIZE + start2 , size );
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
void DiskPageCache::addPages ( long vfd,
			       char *buf,
			       long numBytes,
			       long long offset ,
			       long niceness ){
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
	if ( m_maxMemOff == 0 ) return;
	// or disabled
	if ( ! m_enabled ) return;
	// disabled at the master controls?
	if ( m_switch && ! *m_switch ) return;
	// sometimes the file got unlinked on us
	if ( ! m_memOff[vfd] ) return;
	// what is the page range?
	long long sp = offset / m_pageSize ;
	// point to it
	char *bufPtr = buf;
	char *bufEnd = buf + numBytes;
	// . do not add first page unless right on the boundary
	// . how much did we exceed the boundary by?
	oldshort skip = offset - sp * m_pageSize ;
	long  size = m_pageSize - skip;
	// now add the remaining pages
	while ( bufPtr < bufEnd ) {
		// breathe
		QUICKPOLL(niceness);
		// ensure "size" is not too big
		if ( bufPtr + size > bufEnd ) size = bufEnd - bufPtr;
		// add the page to memory
		addPage ( vfd , sp , bufPtr , size , skip );
		// advance
		bufPtr += size;
		sp++;
		size    = m_pageSize;
		skip    = 0;
	}
}

char *DiskPageCache::getMemPtrFromOff ( long off ) {
	if ( off < 0 ) return NULL; // NULL means not in DiskPageCache
	// get set number
	long sn = off / m_maxPageSetSize ;
	// get offset from within the chunk of memory (within the set)
	//long poff = off & (m_maxPageSetSize-1);
	long poff = off % (m_maxPageSetSize);
	// . sanity check
	// . offset must be multiple of m_pageSize+HEADERSIZE, no cuz we skip
	//   ahead X bytes of a page set boundary...
	//long off2 = off - sn * m_maxPageSetSize;
	//if ( off2 != 0 && (off2% (m_pageSize+HEADERSIZE)) != 0) {
	//	char *xx = NULL; *xx = 0; }
	// if we are not in the first page set, advance by one chunk
	// because the first page is often mapped to by a truncated poff from
	// the previous page set
	//if ( sn > 0 && poff == 0 ) poff += m_pageSize + HEADER_SIZE;
	// if it would breech our PAGE_SET, up it
	if ( poff + m_pageSize + HEADERSIZE > m_maxPageSetSize ) {poff=0; sn++;}
	// sanity check
	if ( sn >= m_numPageSets ) { char *xx = NULL; *xx = 0; }
	// return the proper ptr
	return &m_pageSet[sn][poff];
}

// skip is offset of "page" into physical page
void DiskPageCache::addPage(long vfd,long pageNum,char *page,long size,
			    oldshort skip){
	// . if pageNum is beyond the file size
	// . see the explanation for this same error msg above
	if ( pageNum >= m_maxPagesInFile[vfd] ) { 
		// this has happened during a merge before!! (at startup)
		//log(LOG_LOGIC,"db: pagecache: addPage: Bad engineer. "
		// happens because rdbdump did not get a high enough 
		// maxfilesize so we did not make enough pages! we endedup
		// dumping more than what was end the tree because stuff was
		// added to the tree while dumping!
		log("db: pagecache: Caught add breach. "
		    "pageNum=%li max=%li db=%s", 
		    pageNum,m_maxPagesInFile[vfd],m_dbname);
		return;
	}

	// debug msg
	//log("addPage: pageNum=%li page[0]=%hhx size=%li skip=%li",
	//    pageNum,page[0],size,(long)skip);
		
	long poff = m_memOff [ vfd ] [ pageNum ] ;
	// p will be NULL if page does not have any data in memory yet
	//char *p = getMemPtrFromOff ( poff );
	// if page already exists in cache and needs data on the boundaries
	// we may be able to supply it
	if ( poff >= 0 ) {
		// debug msg
		//log("ENHANCING off=%li",poff);
		enhancePage ( poff , page , size , skip );
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
		//log("RECYCLING off=%li",poff);
	}
	// can we grab a page from memory without having to grow?
	else if ( m_nextMemOff + m_pageSize + HEADERSIZE < m_upperMemOff ) {
		poff = m_nextMemOff;
		m_nextMemOff += m_pageSize + HEADERSIZE;
		// debug msg
		//log("CLAIMING off=%li",poff);
	}
	// . we now grow everything at start
	// . otherwise, try to grow the page cache by 200k
	//else if ( m_nextMemOff + m_pageSize + HEADERSIZE < m_maxMemOff ) {
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
		poff = m_tailOff;
		//char *p = getMemPtrFromOff ( poff );
		excisePage ( poff );
		// . the file no longer owns him
		// . this is a long ptr to &m_bufOffs[vfd][pageNum]
		// . if that vfd no longer exists it should have added all its
		//   pages to m_avail list
		//long tmp = -1;
		long *memOffPtr = NULL;
		readFromCache(&memOffPtr, poff, OFF_PTR, sizeof(long*));
		*memOffPtr = -1;
		//m_cacheBuf.writeToCache(poff, OFF_PTR, &tmp, sizeof(long));
		// testing
		//m_cacheBuf.readFromCache ( &tmp, poff+OFF_PTR, sizeof(long) );
		//if ( tmp != -1 ){
		//char *xx=NULL; *xx=0;}
		//**(long **)(p+OFF_PTR) = -1;
		// debug msg
		//log("KICKINGTAIL off=%li",poff);
	}
	// sanity check
	if ( poff < 0 ) { char *xx = NULL; *xx = 0; }
	// get ptr to the page in memory from the memory offset
	//p = getMemPtrFromOff ( poff );
	// store the size as first 2 bytes
	writeToCache(poff, OFF_SIZE, &size, sizeof(oldshort));
	// oldshort tmp = 0;
	// m_cacheBuf.readFromCache ( &tmp, poff, OFF_SIZE, sizeof(long) );
	// if ( tmp != size ){
	//  char *xx=NULL; *xx=0;}
	//*(oldshort *)(p+OFF_SIZE) = size; 
	writeToCache( poff, OFF_SKIP, &skip, sizeof(oldshort) );
	//*(oldshort *)(p+OFF_SKIP) = skip;
	// sanity check
	if ( size + skip > m_pageSize ) { char *xx = NULL; *xx = 0; }
	// store the link information in bytes 8-16
	promotePage ( poff , true/*isNew?*/ ); 
	// then store a ptr to m_memOff[vfd][pageNum] so we can set *ptr
	// to -1 if they page gets replaced by another
	
	long *memOffPtr = &m_memOff[ vfd ][ pageNum ];
	writeToCache( poff, OFF_PTR, &memOffPtr, sizeof(long*));
				 
	//*(long **)(p+OFF_PTR) = &m_memOff [ vfd ] [ pageNum ] ;
	// then the data from disk (skip over link info)
	writeToCache( poff, HEADERSIZE + skip, page, size);
	//memcpy ( p + HEADERSIZE + skip , page , size );
	// transform mem ptr to offset
	if ( !m_useRAMDisk && ! m_useSHM ) {
		long off = -1;
		char *p = getMemPtrFromOff ( poff );
		for ( long i = 0 ; i < m_numPageSets ; i++ ) {
			if ( p < m_pageSet[i] ) continue;
			if ( p > m_pageSet[i] + m_pageSetSize[i] ) 
				continue;
			off = p - m_pageSet[i] + i * m_maxPageSetSize ;
			break;
		}
		// update map
		m_memOff [ vfd ] [ pageNum ] = off;
		// sanity check
		if ( off != poff ) { char *xx=NULL; *xx=0; }
	}
	else
		m_memOff [ vfd ] [ pageNum ] = poff;
	// update the header of that page
	
	// we have added the page!
	if ( m_minimizeDiskSeeks )
		m_numPagesPresentOfFile[vfd]++;
}

// add data from "page" (we just read it from disk or wrote to disk) 
// into "p" page in memory
void DiskPageCache::enhancePage (long poff, char *page, long size, 
				 oldshort skip) {
	oldshort psize = 0;
	readFromCache( &psize, poff, OFF_SIZE, sizeof(oldshort));
	//oldshort psize = *(oldshort *)(p+OFF_SIZE);
	oldshort pskip = 0;
	readFromCache( &pskip, poff, OFF_SKIP, sizeof(oldshort));
	//oldshort pskip = *(oldshort *)(p+OFF_SKIP);
	// can we add to front of page?
	if ( skip < pskip ) {
		long diff = pskip - skip;
		// . we cored here because page[diff-1] was out of bounds. why?
		// . do not allow gap in between cached data, that is, we have 
		//   cached bytes at the end of the page, then we try to cache 
		//   some at the beginning, and it's not contiguous... we are
		//   not built for that... this can happen when dumping a file,
		//   if your first reads up to the file end (somewhere in the
		//   middle of the page) and your second read starts somewhere
		//   else.... mmmm... i dunno....
		if ( skip + size < pskip || diff > size ) { 
			log("db: Avoided cache gap in %s. diff=%li "
			    "size=%li pskip=%li skip=%li.",
			    m_dbname,diff,size,(long)pskip,(long)skip);
			return;
		}
		writeToCache(poff, HEADERSIZE + skip , page , diff);
		//memcpy ( p + HEADERSIZE + skip , page , diff );
		psize += diff;
		pskip -= diff;
		writeToCache(poff, OFF_SIZE, &psize, sizeof(oldshort));
		//*(oldshort *)(p+OFF_SIZE) = psize ;
		writeToCache(poff, OFF_SKIP, &pskip, sizeof(oldshort));
		//*(oldshort *)(p+OFF_SKIP) = pskip ;
	}
	// can we add to end of page?
	long pend = pskip + psize;
	long  end = skip  +  size;
	if ( end <= pend ) return;
	long diff = end - pend ;
	// if the read's starting point is beyond our ending point, bail,
	// we don't want any holes...
	if ( diff > size ) return;
	writeToCache(poff, HEADERSIZE + pend, page + size - diff, diff);
	//memcpy ( p + HEADERSIZE + pend , page + size - diff , diff );
	oldshort tmp = psize+diff;
	writeToCache(poff, OFF_SIZE, &tmp, sizeof(oldshort));
	//*(oldshort *)(p+OFF_SIZE) = (oldshort)psize + diff;
}

// the link information is bytes 8-16 of each page in mem (next/prev mem ptrs)
void DiskPageCache::promotePage ( long poff , bool isNew ) {
	if ( isNew ) {
	here:
		long tmp = -1;
		writeToCache(poff, OFF_PREV, &tmp, sizeof(long));
		// testing
		readFromCache ( &tmp, poff, OFF_PREV, sizeof(long) );
		if ( tmp != -1 ){
			char *xx=NULL; *xx=0;}
		//*(long *)(p + OFF_PREV) = -1       ;// our prev is -1 (none)
		writeToCache(poff, OFF_NEXT, &m_headOff, sizeof(long));
		//*(long *)(p+OFF_NEXT) = m_headOff;// our next is the old head
		// the old head's prev is us
		if ( m_headOff >= 0 ) {
			writeToCache(m_headOff, OFF_PREV, &poff, 
				     sizeof(long));
			//char *headPtr = getMemPtrFromOff ( m_headOff ) ;
			//*(long *)(headPtr + OFF_PREV) = poff;
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
void DiskPageCache::excisePage ( long poff ) {
	// get our neighbors, NULL if none
	long prev = 0;
	readFromCache(&prev, poff, OFF_PREV, sizeof(long));
	//long prev = *(long *)(p + OFF_PREV);
	long next = 0;
	readFromCache(&next, poff, OFF_NEXT, sizeof(long));
	//long next = *(long *)(p + OFF_NEXT);
	// if we were the head or tail, then pass it off to our neighbor
	if ( poff == m_headOff ) m_headOff = next;
	if ( poff == m_tailOff ) m_tailOff = prev;
	// our prev's next becomes our old next
	if ( prev >= 0 ) {
		//char *prevPtr = getMemPtrFromOff ( prev );
		writeToCache(prev, OFF_NEXT, &next, sizeof(long));
		//*(long *)(prevPtr + OFF_NEXT ) = next;
	}
	// our next's prev becomes our old prev
	if ( next >= 0 ) {
		//char *nextPtr = getMemPtrFromOff ( next );
		writeToCache(next, OFF_PREV, &prev, sizeof(long));
		//long *)(nextPtr + OFF_PREV ) = prev;
	}
}

// . grow/shrink m_memOff[] which maps vfd/page to a mem offset
// . returns false and sets g_errno on error
// . called by DiskPageCache::open()/close() respectively
// . fileSize is so we can alloc m_memOff[vfd] big enough for all pgs
long DiskPageCache::getVfd ( long long maxFileSize, bool vfdAllowed ) {
	// check for override function
	//if ( m_isOverriden ) {
	//	return m_getVfd2 ( this, maxFileSize );
	//}

	// for RAMDisks, do not cache disk
	// pages from the indexdb root file, nor, any indexdb file that is
	// larger than twice the "maxMemForRamDisk" value
	if ( m_useRAMDisk && maxFileSize > (m_maxMemOff * 2) ){
		log (LOG_INFO,"db: getvfd: cannot cache on RAMDisk files that "
		     "larger than twice the max mem value. fileSize=%li",
		     m_maxMemOff);
		return -1;
	}

	long  numPages = (maxFileSize / m_pageSize) + 1;

	// RESTRICT to only the first m_maxMemOff worth of files, 
	// starting with the SMALLEST file first. so if maxMemoff is 50MB, and
	// we have 5 files that are 10,20,30 & 40MB,
        // then we use 10MB for the first file, 20MB of the 2nd BUT only
        // 20MB for the 3rd file, and the 4th file does not get any page cache.
        // if doing "biased lookups" each file is virtually half the actual
        // size, and this allocates page cache appropriately.
	
	// don't to do a page cache for an indexdb0001.dat that is 100GB 
	// because we'd have to allocate too much mem for the m_memOff[] array
	// so for the parital file make sure its less than 1 GB
	if ( m_minimizeDiskSeeks && !vfdAllowed ){
		log (LOG_INFO,"db: getVfd: cannot cache because minimizing "
		     "disk seeks. numPages=%li", numPages);
		return -1;
	}
			
	// . pick a vfd for this BigFile to use
	// . start AFTER last pick in case BigFile closed, released its
	//   m_vfd, a read thread returned and called addPages() using that
	//   old m_vfd!!!!!!! TODO: can we fix this better?
	long i ;
	long count = MAX_NUM_VFDS2;
	for ( i = m_nexti ; count-- > 0 ; i++ ) {
		if ( i >= MAX_NUM_VFDS2 ) i = 0; // wrap
		if ( ! m_memOff [ i ] ) break;
	}
	// bail if none left
	if ( count == 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"db: pagecache: getvfd: no vfds remaining.");
		//char *xx = NULL; *xx = 0;
		return -1;
	}
	// . file size has to be below 2 gigs because m_memOff is only a long
	// . if we need to we could transform m_memOff into m_memPageNum
	//if ( maxFileSize > 0x7fffffffLL ) {
	//	g_errno = EBADENGINEER;
	//	log("DiskPageCache::getVfd: maxFileSize too big");
	//	return -1;
	//}
	// assign it
	long vfd = i;
	// start here next time
	m_nexti = i + 1;
	// say which cache it is
	

	// alloc the map space for this file
	long  need     = numPages * sizeof(long) ;
	long *buf      = (long *)mmalloc ( need , m_memTag );
	if ( ! buf ) {
		log("db: Failed to allocate %li bytes for page cache "
		    "structures for caching pages for vfd %li. "
		    "MaxfileSize=%lli. Not enough memory.",need,i,maxFileSize);
		return -1;
	}
	m_memOff         [ vfd ] = buf;
	m_maxPagesInFile [ vfd ] = numPages;

	// keep a tab on the number of pages we can store of the file
	if ( m_minimizeDiskSeeks ){
		m_numPagesPresentOfFile[vfd] = 0;
		if ( m_memFree > numPages * ( HEADERSIZE + m_pageSize ) )
			m_maxPagesPerFile[vfd] = numPages;
		else 
			m_maxPagesPerFile[vfd] = m_memFree / ( m_pageSize + 
							       HEADERSIZE );
	}
			
	// add it in
	m_memAlloced += need;
	// debug msg
	//log("%s adding %li",m_dbname,need);
	// no pages are in memory yet, so set offsets to -1
	for ( i = 0 ; i < numPages ; i++ ) m_memOff [ vfd ] [ i ] = -1;

	// if minimizing disk seeks then calculate the memory used
	if ( m_minimizeDiskSeeks ){
		m_memFree -= maxFileSize;
		// if the file is bigger than the mem only partially store it
		if ( m_memFree < 0 )
			m_memFree = 0;
	}
	// debug msg
	//log("ALLOCINGFILE pages=%li",numPages);
	return vfd;
}

// when a file loses its vfd this is called
void DiskPageCache::rmVfd  ( long vfd ) {
	// check for override function
	//if ( m_isOverriden ) {
	//	m_rmVfd2 ( this, vfd );
	//	return;
	//}
	// ensure validity
	if ( vfd < 0 ) return;

	// if 0 bytes are allocated for disk cache, just skip this junk
	if ( m_maxMemOff <= 0 ) return;

	// this vfd may have already been nuked by call to unlink!
	if ( ! m_memOff [ vfd ] ) return;
	// add valid offsets used by vfd into m_availMemOff
	for ( long i = 0 ; i < m_maxPagesInFile [ vfd ] ; i++ ) {
		long off = m_memOff [ vfd ] [ i ];
		if ( off < 0 ) continue;
		// sanity check
		if ( m_numAvailMemOffs > m_maxAvailMemOffs ) {
			char *xx = NULL; *xx = 0; }
		// debug msg
		//log("MAKING off=%li available. na=%li",
		// off,m_numAvailMemOffs+1);
		// store it in list of available memory offsets so some other
		// file can use it
		m_availMemOff [ m_numAvailMemOffs++ ] = off;
		// remove that page from linked list, too
		//char *p = getMemPtrFromOff ( off );
		excisePage ( off );
	}
	// free the map that maps this files pages on disk to pages/offs in mem
	long size = m_maxPagesInFile[vfd] * sizeof(long);
	mfree ( m_memOff [ vfd ] , size , "DiskPageCache" );
	m_memOff [ vfd ] = NULL;
	// debug msg
	//log("%s rmVfd: vfd=%li down %li",m_dbname,vfd,size);
	m_memAlloced -= size;
	if ( m_minimizeDiskSeeks ){
		m_memFree += m_maxPagesPerFile[vfd] * m_pageSize;
		m_maxPagesPerFile[vfd] = 0;
		m_numPagesPresentOfFile[vfd] = 0;
	}
}

// use "mem" bytes of memory for the cache
bool DiskPageCache::growCache ( long mem ) {
	// debug msg
	//log("GROWING PAGE CACHE from %li to %li bytes", m_upperMemOff, mem );
	// don't exceed the max
	if ( mem > m_maxMemOff ) mem = m_maxMemOff; 
	// bail if we wouldn't be growing
	if ( mem <= m_upperMemOff ) return true;
	// how many pages? round up.
	long npages = mem/(m_pageSize+HEADERSIZE) + 1;

	// . we need one "available" slot for each page in the cache
	// . this is a list of memory offsets that are available
	long oldSize = m_maxAvailMemOffs * sizeof(long) ;
	long newSize = npages            * sizeof(long) ;
	long *a = (long *) mrealloc(m_availMemOff,oldSize,newSize,m_memTag);
	if ( ! a ) return log("db: Failed to regrow page cache from %li to "
			      "%li bytes. Not enough memory.",oldSize,newSize);
	m_availMemOff     = a;
	m_maxAvailMemOffs = npages;
	m_memAlloced += (newSize - oldSize);
	// debug msg
	//log("%s growCache: up %li",m_dbname,(newSize - oldSize));

	// how much more mem do we need to alloc?
	long need = mem - m_upperMemOff ;
	// how big is our last page set?
	long size = 0;
	char *ptr = NULL;
	long    i = 0;
	if ( m_numPageSets > 0 ) {
		// since we allocate everything at init this shouldn't happen
		char *xx=NULL; *xx=0;
		i    = m_numPageSets - 1;
		ptr  = m_pageSet     [ i ];
		size = m_pageSetSize [ i ];
	}
	// realloc him
	long extra = m_maxPageSetSize - size ;
	if ( extra > need ) extra = need;
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

	char *s = (char *)mrealloc ( ptr , size , size + extra, 
				     m_memTag);
	if ( ! s ) return log("db: Failed to allocate %li bytes more "
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
	//log("%s growCache2: up %li",m_dbname,extra);
	// if we do not need more, we are done
	if ( need == 0 ) return true;
	// otherwise, alloc new page sets until we hit it
	for ( i++ ; i < MAX_PAGE_SETS && need > 0 ; i++ ) {
		long size = need;
		if ( size > m_maxPageSetSize ) size = m_maxPageSetSize;
		need -= size;
		m_pageSet[i] = (char *) mmalloc ( size , m_memTag );
		if ( ! m_pageSet[i] ) break;
		m_pageSetSize[i]  = size;
		m_memAlloced     += size;
		m_upperMemOff    += size;
		m_numPageSets++;
		// debug msg
		//log("%s growCache3: up %li",m_dbname,size);
	}
	// update upper bound
	if ( need == 0 ) return true;
	return log(LOG_LOGIC,"db: pagecache: Bad engineer. Weird problem.");
}

long DiskPageCache::getMemUsed ( ) {
	return m_nextMemOff - m_numAvailMemOffs * (m_pageSize+HEADERSIZE);
}

#include "BigFile.h"
#include "Threads.h"

bool DiskPageCache::verify ( BigFile *f ) {
	long vfd = f->getVfd();
	// ensure validity
	if ( vfd < 0 ) return true;
	// this vfd may have already been nuked by call to unlink!
	if ( ! m_memOff [ vfd ] ) return true;
	// debug msg
	//log("VERIFYING PAGECACHE vfd=%li fn=%s",vfd,f->getFilename());
	// read into here
	char buf [ 32 * 1024 ];//GB_PAGE_SIZE ]; //m_pageSize ];
	// ensure threads disabled
	bool on = ! g_threads.areThreadsDisabled();
	if ( on ) g_threads.disableThreads();
	// disable ourselves
	disableCache();
	// add valid offsets used by vfd into m_availMemOff
	for ( long i = 0 ; i < m_maxPagesInFile [ vfd ] ; i++ ) {
		long off = m_memOff [ vfd ] [ i ];
		if ( off < 0 ) continue;
		//char *p = getMemPtrFromOff ( off );
		oldshort size = 0;
		readFromCache(&size, off, OFF_SIZE, sizeof(oldshort));
		//oldshort size = *(oldshort *)(p+OFF_SIZE);
		oldshort skip = 0;
		readFromCache(&skip, off, OFF_SKIP, sizeof(oldshort));
		if ( size > 32 * 1024 ){
			char *xx=NULL; *xx=0; }
		//oldshort skip = *(oldshort *)(p+OFF_SKIP);
		FileState fstate;
		if ( ! f->read ( buf           ,
				 size          ,
				 ((long long)i * (long long)m_pageSize) + 
				                 (long long)skip ,
				 &fstate       ,
				 NULL          ,  // state
				 NULL          ,  // callback
				 0             )){// niceness
			// core if it did not complete
			char *xx = NULL; *xx = 0; }
		// compare to what we have in mem
		log("checking page # %li size=%li skip=%li", i, size, skip);
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
void DiskPageCache::writeToCache( long bigOff, long smallOff,  void *inBuf, 
				  long size ){

	if ( m_useSHM ) {
		// what page are we on?
		long page = ( bigOff + smallOff ) / m_maxAllocSize;
		// offset within that page
		long poff = ( bigOff + smallOff ) % m_maxAllocSize;
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
			//long long start = gettimeofdayInMicroseconds();
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
			//long long took = gettimeofdayInMicroseconds() -start;
			//if ( took > 1 ) 
			//	logf(LOG_DEBUG,"disk: took %lli us to write "
			//	     "to shm page cache shmid=%li.",took,
			//	     (long)shmid);
		}
		// store it into the cache
		memcpy ( mem + poff , inBuf , size );
		return;
	}

	if ( m_useRAMDisk ){
		long numBytesWritten = pwrite( m_ramfd, inBuf, size, 
					       bigOff + smallOff );
		if ( numBytesWritten != size ){
			char *xx=NULL; *xx=0;
		}
		return;
	}

	char *p = getMemPtrFromOff ( bigOff );
	memcpy(p + smallOff, inBuf, size);
}

void DiskPageCache::readFromCache( void *outBuf, long bigOff, long smallOff,
				   long size ){
	if ( m_useSHM ) {
		// what page are we on?
		long page = ( bigOff + smallOff ) / m_maxAllocSize;
		// offset within that page
		long poff = ( bigOff + smallOff ) % m_maxAllocSize;
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
			//long long start = gettimeofdayInMilliseconds();
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
			//long long took = gettimeofdayInMilliseconds() -start;
			//if ( took > 1 ) 
			//	logf(LOG_DEBUG,"disk: took %lli ms to read "
			//	     "to shm page cache shmid=%li.",took,
			//	     (long)shmid);
		}
		// store it in outBuf
		memcpy ( outBuf , mem + poff , size );
		return;
	}

	if ( m_useRAMDisk ) {
		long numBytesRead = pread( m_ramfd, outBuf, size, 
					   bigOff + smallOff );
		if ( numBytesRead != size ){
			char *xx=NULL; *xx=0;
		}
		return;
	}

	// the old fashioned way
	char *p = getMemPtrFromOff ( bigOff );
	memcpy(outBuf, p + smallOff, size);
}

// lastly, we need some way to "force" a merge at around midnight when traffic
// is minimal, or when there are 3 or more indexdb files that are less than
// 80% in the indexdb disk page cache. because that means we are starting to
// do a lot of disk seeks. 
// checks if indexdb needs merge
/*
bool DiskPageCache::needsMerge( ){
	if ( !m_useRAMDisk ) return false;
	long numVfds = 0;
	for ( long i = 0; i < MAX_NUM_VFDS2; i++ ){
		if ( !m_memOff[i] ) continue;
		// check to see if a file is less than 80% in the indexdb
		// disk page cache
		long numOffsUsed = 0;
		for ( long j = 0; j < m_maxPagesInFile[i]; j++ ){
			if ( m_memOff[i][j] >= 0 )
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
void freeAllSharedMem ( long max ) {

	// free shared mem whose pid no longer exists
	//struct shmid_ds buf;
	//shmctl ( 0 , SHM_STAT , &buf );
	//int shmctl(int shmid, int cmd, struct shmid_ds *buf);

	// types.h uses key_t type that shmget uses
	// try to nuke it all
	for ( long i = 0 ; i < max ; i++ ) {
		int shmid = i;
		long status = shmctl ( shmid , IPC_RMID , NULL);
		if ( status == -1 ) {
			//if ( errno != EINVAL )
			//	log("db: shctlt %li: %s",(long)shmid,mstrerror(errno));
		}
		else
			log("db: Removed shmid %li",i);
	}
}

// types.h uses key_t type that shmget uses
#undef key_t
