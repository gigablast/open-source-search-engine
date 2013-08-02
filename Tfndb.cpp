#include "gb-include.h"

#include "Tfndb.h"
#include "Threads.h"

// a global class extern'd in .h file
Tfndb g_tfndb;
Tfndb g_tfndb2;

// reset rdb
void Tfndb::reset() { m_rdb.reset(); }

bool Tfndb::init ( ) {

	// key sanity tests
	char      tfn   = 33;
	long long docId = 123456789;
	long long urlHash48  = 0x1234567887654321LL & 0x0000ffffffffffffLL;
	key_t k = makeKey(docId,urlHash48,tfn,false);
	if ( getDocId(&k) != docId ) { char *xx=NULL;*xx=0;}
	if ( getUrlHash48(&k) != urlHash48 ) { char *xx=NULL;*xx=0;}
	if ( getTfn  (&k) != tfn   ) { char *xx=NULL;*xx=0;}

	// . what's max # of tree nodes?
	// . key+left+right+parents+balance = 12+4+4+4+1 = 25
	// . 25 bytes per key
	// . now we have an addition sizeof(collnum_t) bytes per node
	long maxTreeMem    = 2000000; // 2MB
	long nodeSize      = 25 + sizeof(collnum_t);
	long maxTreeNodes  = maxTreeMem  / nodeSize ;

	long pcmem = g_conf.m_tfndbMaxDiskPageCacheMem ;
	// keep this low if we are the tmp cluster
	if ( g_hostdb.m_useTmpCluster && pcmem > 30000000 ) pcmem = 30000000;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	if ( g_hostdb.m_useTmpCluster ) pcmem = 0;

	// . each cahched list is just one key in the tree...
	// . 25(treeoverhead) + 24(cacheoverhead) = 49
	//long maxCacheNodes = g_conf.m_tfndbMaxCacheMem / 49;
	// we now use a page cache
	if ( ! m_pc.init ( "tfndb" , RDB_TFNDB , pcmem ,
			    GB_TFNDB_PAGE_SIZE ) )
		return log("db: Tfndb page cache init failed.");
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir,
			    "tfndb"       ,
			    true          , // dedup
			    0             , // fixedDataSize -- we're just keys
			    2             , //g_conf.m_tfndbMinFilesToMerge ,
			    maxTreeMem    ,
			    maxTreeNodes  ,
			    true          , // balance tree?
			    0             , // g_conf.m_tfndbMaxCacheMem     ,
			    0             , // maxCacheNodes                 ,
			    true          , // half keys?
			    false         , // g_conf.m_tfndbSaveCache       ,
			    &m_pc         ,
			    false         , // is titledb?
			    // no, not sense we do interlaced docid
			    // sections now because we sort by docid if score
			    // is the same!
			    false         , // preload page cache? yes!!
			    12            , // keysize
			    true          ) ) // bias page cache?
		return false;
	return true;
	// validate
	//return verify ( );
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Tfndb::init2 ( long treeMem ) {
	// . what's max # of tree nodes?
	// . key+left+right+parents+balance = 12+4+4+4+1 = 25
	// . 25 bytes per key
	// . now we have an addition sizeof(collnum_t) bytes per node
	long nodeSize      = 25 + sizeof(collnum_t);
	long maxTreeNodes  = treeMem  / nodeSize ;
	// we now use a page cache
	//if ( ! m_pc.init ( "tfndb" , g_conf.m_tfndbMaxDiskPageCacheMem ) )
	//	return log("db: Tfndb page cache init failed.");
	// note
	//logf(LOG_INIT, "db: -- Tfndb extended bits set to %li.",
	//     g_conf.m_tfndbExtBits );
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir,
			    "tfndbRebuild",
			    true          , // dedup
			    0             , // fixedDataSize -- we're just keys
			    30            , //g_conf.m_tfndbMinFilesToMerge ,
			    treeMem       ,
			    maxTreeNodes  ,
			    true          , // balance tree?
			    0             , // g_conf.m_tfndbMaxCacheMem     ,
			    0             , // maxCacheNodes                 ,
			    true          , // half keys?
			    false         , // g_conf.m_tfndbSaveCache       ,
			    NULL          , // &m_pc         ,
			    false         , // is titledb?
			    false         , // preload page cache? yes!!
			    12            , // keysize
			    true          ) ) // bias page cache?
		return false;
	return true;
}

bool Tfndb::addColl ( char *coll, bool doVerify ) {
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

bool Tfndb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Tfndb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	startKey.setMin();
	key_t endKey;
	endKey.setMax();
	//long minRecSizes = 64000;
	
	if ( ! msg5.getList ( RDB_TFNDB     ,
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
			      true          ,
			      false         )) { // allow page cache?
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	long count = 0;
	long got   = 0;
	bool printedKey = false;
	bool printedZeroKey = false;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		// verify the group
		uint32_t groupId = getGroupId ( RDB_TFNDB , (char *)&k );
		if ( groupId == g_hostdb.m_groupId )
			got++;
		else if ( !printedKey ) {
			log ( "db: Found bad key in list (only printing once): "
			      "%lx %llx", k.n1, k.n0 );
			printedKey = true;
		}
		if ( k.n1 == 0 && k.n0 == 0 ) {
			if ( !printedZeroKey ) {
				log ( "db: Found Zero key in list, passing. "
				      "(only printing once)." );
				printedZeroKey = true;
			}
			// pass if we didn't match above
			if ( groupId != g_hostdb.m_groupId )
				got++;
		}
	}
	if ( got != count ) {
		log ("db: Out of first %li records in tfndb, only %li passed "
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
		log ( "db: Exiting due to Tfndb inconsistency." );
		return g_conf.m_bypassValidation;
	}

	log ( LOG_INFO, "db: Tfndb passed verification successfully for %li "
			"recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

// see Tfndb.h for bitmap of this key
key_t Tfndb::makeKey (long long docId,long long uh48,long tfn,bool isDelete) {
	// sanity check
	if ( tfn > 255 || tfn < 0 ) { char *xx=NULL;*xx=0; }
	// init the key
	key_t k; 
	k.n1 = (unsigned long)(docId >> (NUMDOCIDBITS-32));
	// keep the lower 6 bits here
	k.n0 = (docId & 0x3f);
	// sanity check
	if ( uh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }
	// then uh48
	k.n0 <<= 48;
	k.n0 |= uh48;
	// then tfn
	k.n0 <<= 8;
	k.n0 |= tfn;
	// half bit is off, we're a full key
	k.n0 <<= 1;
	// room for del bit
	k.n0 <<= 1;
	// or that in (negative/del keys must rank BEFORE positive keys,
	// and since we sort from lowest to highest...)
	if ( ! isDelete ) k.n0 |= 0x01;
	return k;
}
