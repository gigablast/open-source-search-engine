#include "gb-include.h"

#include "Titledb.h"
#include "Threads.h"

Titledb g_titledb;
Titledb g_titledb2;

// reset rdb
void Titledb::reset() { m_rdb.reset(); }

// init our rdb
bool Titledb::init ( ) {

	// key sanity tests
	long long uh48  = 0x1234567887654321LL & 0x0000ffffffffffffLL;
	long long docId = 123456789;
	key_t k = makeKey(docId,uh48,false);
	if ( getDocId(&k) != docId ) { char *xx=NULL;*xx=0;}
	if ( getUrlHash48(&k) != uh48 ) { char *xx=NULL;*xx=0;}

	char *url = "http://.ezinemark.com/long-island-child-custody-attorneys-new-york-visitation-lawyers-melville-legal-custody-law-firm-45f00bbed18.html";
	Url uu;
	uu.set(url);
	char *d1 = uu.getDomain();
	long  dlen1 = uu.getDomainLen();
	long dlen2 = 0;
	char *d2 = getDomFast ( url , &dlen2 );
	if ( dlen1 != dlen2 ) { char *xx=NULL;*xx=0; }
	// another one
	url = "http://ok/";
	uu.set(url);
	d1 = uu.getDomain();
	dlen1 = uu.getDomainLen();
	dlen2 = 0;
	d2 = getDomFast ( url , &dlen2 );
	if ( dlen1 != dlen2 ) { char *xx=NULL;*xx=0; }


	long long maxMem = 200000000; // 200MB

	// . what's max # of tree nodes?
	// . assume avg TitleRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	long maxTreeNodes  = maxMem  / (1*1024);

	// . we now use a disk page cache for titledb as opposed to the
	//   old rec cache. i am trying to do away with the Rdb::m_cache rec
	//   cache in favor of cleverly used disk page caches, because
	//   the rec caches are not real-time and get stale.
	// . just hard-code 30MB for now
	long pcmem    = 30000000; // = g_conf.m_titledbMaxDiskPageCacheMem;
	// fuck that we need all the mem!
	pcmem = 0;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	long pageSize = GB_INDEXDB_PAGE_SIZE;
	// init the page cache
	if ( ! m_pc.init ( "titledb",
			   RDB_TITLEDB,
			   pcmem    ,
			   pageSize ) )
		return log("db: Titledb init failed.");

	// each entry in the cache is usually just a single record, no lists
	//long maxCacheNodes = g_conf.m_titledbMaxCacheMem / (10*1024);
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "titledb"                   ,
			    true                        , // dedup same keys?
			    -1                          , // fixed record size
			    //g_hostdb.m_groupMask          ,
			    //g_hostdb.m_groupId            ,
			    //g_conf.m_titledbMinFilesToMerge , 
			    // this should not really be changed...
			    -1,//3,//230  minfilestomerge mintomerge
			    maxMem, // g_conf.m_titledbMaxTreeMem  ,
			    maxTreeNodes                ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    // turn off cache for now because the page cache
			    // is just as fast and does not get out of date
			    // so bad??
			    //0                         ,
			    0,//g_conf.m_titledbMaxCacheMem ,
			    0,//maxCacheNodes               ,
			    false                       ,// half keys?
			    false                       ,// g_conf.m_titledbSav
			    &m_pc                       , // page cache ptr
			    true                        ) )// is titledb?
		return false;
	return true;
	// validate
	//return verify ( );
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Titledb::init2 ( long treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg TitleRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	long maxTreeNodes  = treeMem / (1*1024);
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "titledbRebuild"            ,
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
			    false                       , // titledbSaveCache
			    NULL                        , // page cache ptr
			    true                        ) )// is titledb?
		return false;
	return true;
	// validate
	//return verify ( );
}

bool Titledb::addColl ( char *coll, bool doVerify ) {
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

bool Titledb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Titledb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//long minRecSizes = 64000;

	if ( ! msg5.getList ( RDB_TITLEDB   ,
			      coll          ,
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

	long count = 0;
	long got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		unsigned long groupId = getGroupId ( RDB_TITLEDB , &k );
		if ( groupId == g_hostdb.m_groupId ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %li records in titledb, "
		     "only %li belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( count > 10 && got == 0 ) 
			log("db: Are you sure you have the right "
				   "data in the right directory? "
				   "Exiting.");
		// repeat with log
		for ( list.resetListPtr() ; ! list.isExhausted() ;
		      list.skipCurrentRecord() ) {
			key_t k = list.getCurrentKey();
			unsigned long groupId = getGroupId ( RDB_TITLEDB , &k);
			long groupNum = g_hostdb.getGroupNum(groupId);
			log("db: docid=%lli group=%li",
			    getDocId(&k),groupNum);
		}

		log ( "db: Exiting due to Titledb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}

	log ( LOG_INFO, "db: Titledb passed verification successfully for %li"
			" recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

// . get a 38 bit docid
// . lower 38 bits are set
// . we put a domain hash in the docId to ease site clustering
// . returns false and sets errno on error
/*
unsigned long long Titledb::getProbableDocId ( char *url ) {
	// just hash the whole collection/url
	long long docId ;
	docId = 0; // hash64  ( coll , collLen );
	docId = hash64b ( url , docId );
	// top 8 bits of docId is always hash of the ip
	//	if ( ! hasIp() ) { errno = EURLHASNOIP; return false; }
	// now hash the HOSTNAME of the url as lower middle bits in docId
	//unsigned char h = hash8 ( url->getDomain() , url->getDomainLen() );
	// . now we store h in the middle 8 bits of the docId
	// . if we stored in top 8 bits all titleRecs from the same hostname
	//   would be clumped together
	// . if we stored in lower 8 bits the chaining might change our
	//   lower 8 bits making the docid seems like from a different site
	// . 00000000 00000000 00000000 00dddddd
	// . dddddddd dddddddd hhhhhhhh dddddddd
	//long long h2 = (h << 8);
	// . clear all but lower 38 bits, then clear 8 bits for h2
	docId &= DOCID_MASK; 
	//docId |= h2;
	// or in the hostname hash as the low 8 bits
	//	*docId |= h;
	return docId;
}
*/

bool Titledb::isLocal ( long long docId ) {
	// shift it up (64 minus 38) bits so we can mask it
	//key_t key = makeTitleRecKey ( docId , false /*isDelKey?*/ );
	// mask upper bits of the top 4 bytes
	return ( getGroupIdFromDocId ( docId ) == g_hostdb.m_groupId ) ;
}

// . make the key of a TitleRec from a docId
// . remember to set the low bit so it's not a delete
// . hi bits are set in the key
key_t Titledb::makeKey ( long long docId, long long uh48, bool isDel ){
	key_t key ;
	// top bits are the docid so generic getGroupId() works!
	key.n1 = (unsigned long)(docId >> 6); // (NUMDOCIDBITS-32));

	long long n0 = (unsigned long long)(docId&0x3f);
	// sanity check
	if ( uh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }
	// make room for uh48
	n0 <<= 48;
	n0 |= uh48;
	// 9 bits reserved
	n0 <<= 9;
	// final del bit
	n0 <<= 1;
	if ( ! isDel ) n0 |= 0x01;
	// store it
	key.n0 = n0;
	return key;
};
