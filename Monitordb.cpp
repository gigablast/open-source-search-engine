#include "Monitordb.h"
#include "Threads.h"

Monitordb g_monitordb;

void Monitordb::reset() {
	m_rdb.reset();
}

bool Monitordb::init ( ) {
	// we use the same disk page size as indexdb (for rdbmap.cpp)
	long pageSize = GB_INDEXDB_PAGE_SIZE;
	// set this for debugging
	//long long maxTreeMem = 1000000;
	long long maxTreeMem = 10000000; // 10MB
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = sizeof(key96_t)+4 +4+4+4+4
	// . 32 bytes per record when in the tree
	long maxTreeNodes = maxTreeMem /(sizeof(key96_t)+16);
	// disk page cache mem, 100MB on gk0 now
	long pcmem = 0; // g_conf.m_monitordbMaxDiskPageCacheMem;
	// keep this low if we are the tmp cluster
	//if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// TODO: would be nice to just do page caching on the satellite files;
	//       look into "minimizeDiskSeeks" at some point...
	if ( ! m_pc.init ( "monitordb" ,
			   RDB_MONITORDB,
			   pcmem    ,
			   pageSize ,
			   true     ,  // use shared mem?
			   false    )) // minimizeDiskSeeks?
		return log("db: Monitordb init failed.");
	// init the rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "monitordb" ,
			    true     , // dedup
			    0        , // fixeddatasize is 0 since no data
			    4,//g_conf.m_monitordbMinFilesToMerge ,
			    // fix this to 15 and rely on the page cache of
			    // just the satellite files and the daily merge to
			    // keep things fast.
			    //15       , 
			    maxTreeMem ,
			    maxTreeNodes ,
			    true     , //isTreeBalanced
			    0        , // cache mem 
			    0        , // cache nodes
			    false, // true     , // use half keys
			    false    , // load cache from disk
			    &m_pc    ,
			    false    , // false
			    false    , // preload page cache
			    sizeof(key96_t) ,
			    true             ); // bias page cache? (true!)
}

bool Monitordb::addColl ( char *coll, bool doVerify ) {
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

bool Monitordb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Monitordb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key224_t startKey;
	key224_t endKey;
	startKey.setMin();
	endKey.setMax();
	long minRecSizes = 64000;
	
	if ( ! msg5.getList ( RDB_MONITORDB   ,
			      coll          ,
			      &list         ,
			      (char*)&startKey      ,
			      (char*)&endKey        ,
			      minRecSizes   ,
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
		key224_t k;
		list.getCurrentKey((char*)&k);
		count++;
		uint32_t groupId = getGroupId ( RDB_MONITORDB , &k );
		if ( groupId == g_hostdb.m_groupId ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %li records in Monitordb , "
		     "only %li belong to our group.",count,got);

		/*
		// repeat with log
		for ( list.resetListPtr() ; ! list.isExhausted() ;
		      list.skipCurrentRecord() ) {

			key224_t k;
			list.getCurrentKey((char*)&k);
			uint32_t groupId = getGroupId ( RDB_MONITORDB , &k );
			long groupNum = g_hostdb.getGroupNum(groupId);
			unsigned long sh32 ;
			sh32 = g_monitordb.getLinkeeSiteHash32_uk(&k);
			uint16_t sh16 = sh32 >> 19;
			log("db: sh16=0x%lx group=%li",
			    (long)sh16,groupNum);
		}
		*/


		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
				    "right "
				    "data in the right directory? "
				    "Exiting.");
		log ( "db: Exiting due to inconsistency.");
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_INFO, "db: Monitordb passed verification successfully for "
	      "%li recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

