#include "gb-include.h"

#include "Revdb.h"
#include "Threads.h"

Revdb g_revdb;
Revdb g_revdb2;

// reset rdb
void Revdb::reset() { m_rdb.reset(); }

// init our rdb
bool Revdb::init ( ) {

	int64_t maxTreeMem = 200000000;
	// . what's max # of tree nodes?
	// . assume avg RevRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	int32_t maxTreeNodes  = maxTreeMem  / (1*1024);

	// each entry in the cache is usually just a single record, no lists
	int32_t maxCacheNodes = 0;//g_conf.m_revdbMaxCacheMem / (10*1024);

	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "revdb"                   ,
			    true                        , // dedup same keys?
			    -1                          , // fixed record size
			    // this should not really be changed...
			    2                         , // min files to merge
			    maxTreeMem,//g_conf.m_revdbMaxTreeMem  ,
			    maxTreeNodes                ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0                           , // cache mem
			    maxCacheNodes               ,
			    false                       ,// half keys?
			    false                       ,// g_conf.m_revdbSav
			    NULL                        , // page cache ptr
			    false                       ) )// is titledb?
		return false;
	return true;
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Revdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg RevRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	int32_t maxTreeNodes  = treeMem / (1*1024);
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "revdbRebuild"            ,
			    true                        , // dedup same keys?
			    -1                          , // fixed record size
			    240                         , // MinFilesToMerge
			    treeMem                     ,
			    maxTreeNodes                ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0                           , // MaxCacheMem ,
			    0                           , // maxCacheNodes
			    false                       , // half keys?
			    false                       , // revdbSaveCache
			    NULL                        , // page cache ptr
			    false                        ) )// is titledb?
		return false;
	return true;
}
/*
bool Revdb::addColl ( char *coll, bool doVerify ) {
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
*/
bool Revdb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Revdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);

	if ( ! msg5.getList ( RDB_REVDB   ,
			      cr->m_collnum       ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      1024*1024     , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        ,
			      false         )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		//uint32_t groupId = getGroupId ( RDB_REVDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_REVDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %"INT32" records in revdb, "
		     "only %"INT32" belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( count > 10 && got == 0 ) 
			log("db: Are you sure you have the right "
				   "data in the right directory? "
				   "Exiting.");
		log ( "db: Exiting due to Revdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}

	log ( LOG_INFO, "db: Revdb passed verification successfully for %"INT32""
			" recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

// . make the key of a RevRec from a docId
// . remember to set the low bit so it's not a delete
// . hi bits are set in the key
key_t Revdb::makeKey ( int64_t docId, bool isDel ){
	key_t key ;
	key.n1 = 0;
	// shift up for delbit
	key.n0 = ((uint64_t)docId) << 1;
	// final del bit
	if ( ! isDel ) key.n0 |= 0x01;
	return key;
};

int64_t Revdb::getDocId ( key_t *k ) {
	return (k->n0 >> 1);
}
