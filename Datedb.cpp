#include "gb-include.h"

#include "Datedb.h"
#include "Url.h"
#include "Clusterdb.h"
//#include "Checksumdb.h"
#include "Threads.h"

// a global class extern'd in .h file
Datedb g_datedb;
Datedb g_datedb2;

// resets rdb
void Datedb::reset() { m_rdb.reset(); }

//#include "DiskPageCache.h"

bool Datedb::init ( ) {

	// sanity check
	long long termId = 0x00001915c8f5e747LL;
	long      date   = 0x0badf00d;
	char      score  = 123;
	long long docId =  247788576824LL;
	key128_t k = makeKey ( termId,date,score,docId,false);
	// parse it up to test
	if ( g_datedb.getDate  (&k) != date   ) { char *xx=NULL;*xx=0; }
	if ( g_indexdb.getScore((char *)&k) != score  ) { char *xx=NULL;*xx=0;}
	if ( g_datedb.getDocId (&k) != docId  ) { char *xx=NULL;*xx=0; }
	long long rt = g_datedb.getTermId(&k);
	if ( g_datedb.getTermId(&k) != termId ) { char *xx=NULL;*xx=0; }
	if ( rt                     != termId ) { char *xx=NULL;*xx=0; }
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (16 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	long nodeSize      = (sizeof(key128_t)+12+4) + sizeof(collnum_t);
	long maxTreeNodes = g_conf.m_datedbMaxTreeMem  / nodeSize ;
	// . assume the average cached list is about 600 bytes
	// . TODO: if we cache a lot of not founds (small lists), we won't have
	//   enough nodes!!
	long maxCacheNodes = g_conf.m_datedbMaxCacheMem / 600;


	long pageSize = GB_INDEXDB_PAGE_SIZE;
	// we now use a disk page cache as opposed to the
	// old rec cache. i am trying to do away with the Rdb::m_cache rec
	// cache in favor of cleverly used disk page caches, because
	// the rec caches are not real-time and get stale. 
	long pcmem    = g_conf.m_datedbMaxDiskPageCacheMem;
	// make sure at least 30MB
	//if ( pcmem < 30000000 ) pcmem = 30000000;
	// keep this low if we are the tmp cluster, 20MB
	if ( g_hostdb.m_useTmpCluster && pcmem > 20000000 ) pcmem = 20000000;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// . init the page cache
	// . MDW: "minimize disk seeks" not working otherwise i'd enable it!
	if ( ! m_pc.init ( "datedb",
			   RDB_DATEDB,
			   pcmem    ,
			   pageSize , 
			   true     ,  // use RAM disk?
			   false    )) // minimize disk seeks?
		return log("db: Datedb pc init failed.");

	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want datedb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	return m_rdb.init( g_hostdb.m_dir              ,
			   "datedb"                   ,
			   true                        , // dedup same keys?
			   0                           , // fixed data size
			   //g_hostdb.m_groupMask          ,
			   //g_hostdb.m_groupId            ,
			   g_conf.m_datedbMinFilesToMerge , 
			   g_conf.m_datedbMaxTreeMem  ,
			   maxTreeNodes                ,
			   // now we balance so Sync.cpp can ordered huge lists
			   true                        , // balance tree?
			   g_conf.m_datedbMaxCacheMem ,
			   maxCacheNodes 	       ,
			   true                        , // use half keys?
			   g_conf.m_datedbSaveCache   ,
			   &m_pc                      ,
			   false                      , // is titledb?
			   false                      , // preload dskpagecache
			   16                         ); // key size
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Datedb::init2 ( long treeMem ) {
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (16 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	long nodeSize     = (sizeof(key128_t)+12+4) + sizeof(collnum_t);
	long maxTreeNodes = treeMem / nodeSize ;
	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want datedb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	return m_rdb.init( g_hostdb.m_dir   ,
			   "datedbRebuild"  ,
			   true             , // dedup same keys?
			   0                , // fixed data size
			   200              , // MinFilesToMerge , 
			   treeMem          ,
			   maxTreeNodes     ,
			   true             , // balance tree?
			   0                , // MaxCacheMem ,
			   0                , // maxCacheNodes 
			   true             , // use half keys?
			   false            , // SaveCache   ,
			   NULL             , // s_pc
			   false            , // is titledb?
			   false            , // preload dskpagecache
			   16               );// key size
}

bool Datedb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}

bool Datedb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Datedb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//long minRecSizes = 64000;
	
	if ( ! msg5.getList ( RDB_DATEDB    ,
			      coll          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	long count = 0;
	long got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		//key128_t k = list.getCurrentKey();
		key128_t k;
		list.getCurrentKey ( (char*)&k );
		count++;
		unsigned long groupId = getGroupId ( RDB_DATEDB , &k );
		if ( groupId == g_hostdb.m_groupId ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %li records in datedb, only %li belong "
		     "to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Datedb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_INFO, "db: Datedb passed verification successfully for %li "
			"recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

// . see Datedb.h for format of the 12 byte key
// . TODO: substitute var ptrs if you want extra speed
key128_t Datedb::makeKey ( long long          termId   , 
			   unsigned long      date     ,
			   unsigned char      score    , 
			   unsigned long long docId    , 
			   bool               isDelKey ) {
	key128_t key ;
	char *kp = (char *)&key;
	char *tp = (char *)&termId;
	char *dp = (char *)&docId;
	// store termid
	*(short *)(kp+14) = *(short *)(tp+4);
	*(long  *)(kp+10) = *(long  *)(tp  );
	// store date, not complemented
	*(long  *)(kp+ 6) = ~date;
	// store the complement of the score
	kp[5] = ~score;
	// . store docid
	// . make room for del bit and half bit
	docId <<= 2;
	*(long *)(kp+1) = *(long *)(dp+1);
	kp[0] = dp[0];
	// turn off half bit
	kp[0] &= 0xfd;
	// turn on/off delbit
	if ( isDelKey ) kp[0] &= 0xfe;
	else            kp[0] |= 0x01;
	// key is complete
	return key;
}


