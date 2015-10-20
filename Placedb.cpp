#include "gb-include.h"

#include "Placedb.h"
#include "Threads.h"

// a global class extern'd in .h file
Placedb g_placedb;
Placedb g_placedb2;

// reset rdb
void Placedb::reset() { m_rdb.reset(); }

bool Placedb::init ( ) {
	// . what's max # of tree nodes?
	// . key+left+right+parents+balance = 12+4+4+4+1 = 25
	// . 25 bytes per key
	// . now we have an addition sizeof(collnum_t) bytes per node
	// . assume about 100 chars for each place in the data
	int32_t nodeSize      = 25 + sizeof(collnum_t) + 100;
	int32_t maxTreeMem    = 30000000;
	//int32_t maxTreeNodes  = g_conf.m_placedbMaxTreeMem  / nodeSize ;
	int32_t maxTreeNodes  = maxTreeMem / nodeSize ;

	// . use 30MB for disk page cache. 
	// . Events.cpp should be doing biased distribution to twins
	//   to maximize effective use of this disk page cache
	int32_t pcmem = 30000000;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// turn off for now
	pcmem = 0;

	// . each cahched list is just one key in the tree...
	// . 25(treeoverhead) + 24(cacheoverhead) = 49
	//int32_t maxCacheNodes = g_conf.m_placedbMaxCacheMem / 49;
	// we now use a page cache
	// if (!m_pc.init("placedb",RDB_PLACEDB,pcmem,GB_INDEXDB_PAGE_SIZE ) )
	// 	return log("db: Placedb page cache init failed.");
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir,
			    "placedb"       ,
			    true          , // dedup
			    -1            , // fixedDataSize -- variable data!
			    2             , // g_conf.m_placedbMinFilesToMerge
			    30000000      , // g_conf.m_placedbMaxTreeMem ,
			    maxTreeNodes  ,
			    true          , // balance tree?
			    0             , // g_conf.m_placedbMaxCacheMem 
			    0             , // maxCacheNodes              
			    false         , // half keys?
			    false         , // g_conf.m_placedbSaveCache 
			    NULL,//&m_pc         ,
			    false         , // is titledb?
			    false         , // preload page cache?
			    16            , // keysize
			    false         ))// this does not apply to us
		return false;
	return true;
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Placedb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . key+left+right+parents+balance = 12+4+4+4+1 = 25
	// . 25 bytes per key
	// . now we have an addition sizeof(collnum_t) bytes per node
	// . assume 100 bytes per place in the data
	int32_t nodeSize      = 25 + sizeof(collnum_t) + 100 ;
	int32_t maxTreeNodes  = treeMem  / nodeSize ;
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir,
			    "placedbRebuild",
			    true          , // dedup
			    -1            , // fixedDataSize -- variable
			    2             , // g_conf.m_placedbMinFilesToMerge 
			    treeMem       ,
			    maxTreeNodes  ,
			    true          , // balance tree?
			    0             , // g_conf.m_placedbMaxCacheMem   
			    0             , // maxCacheNodes                
			    false         , // half keys?
			    false         , // g_conf.m_placedbSaveCache   
			    NULL          , // &m_pc         
			    false         , // is titledb?
			    false         , // preload page cache? 
			    16            , // keysize
			    false         ) ) // bias page cache?
		return false;
	return true;
}
/*
bool Placedb::addColl ( char *coll, bool doVerify ) {
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
bool Placedb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Placedb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	startKey.setMin();
	key_t endKey;
	endKey.setMax();
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_PLACEDB     ,
			      cr->m_collnum ,
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
			      true          ,
			      false         )) { // allow page cache?
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	bool printedKey = false;
	bool printedZeroKey = false;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		// verify the group
		uint32_t shardNum = getShardNum ( RDB_PLACEDB , (char *)&k );
		if ( shardNum == getMyShardNum() )
			got++;
		else if ( !printedKey ) {
			log ("db: Found bad key in list (only printing once): "
			      "%"XINT32" %"XINT64"", k.n1, k.n0 );
			printedKey = true;
		}
		if ( k.n1 == 0 && k.n0 == 0 ) {
			if ( !printedZeroKey ) {
				log ( "db: Found Zero key in list, passing. "
				      "(only printing once)." );
				printedZeroKey = true;
			}
			// pass if we didn't match above
			if ( shardNum != getMyShardNum() )
				got++;
		}
	}
	if ( got != count ) {
		log("db: Out of first %"INT32" records in placedb, only %"INT32" passed "
		     "verification.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		g_threads.enableThreads();
		// if only one let it slide, i saw this happen on gb1 cluster
		if ( got - count >= -1 && got - count <= 1 )
			return true;
		log ( "db: Exiting due to Placedb inconsistency." );
		return g_conf.m_bypassValidation;
	}

	log ( LOG_INFO, "db: Placedb passed verification successfully for %"INT32" "
			"recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}
