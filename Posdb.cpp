#include "Posdb.h"

#include "gb-include.h"

#include "Indexdb.h"
#include "Url.h"
#include "Clusterdb.h"
//#include "Checksumdb.h"
#include "Threads.h"
#include "Posdb.h"
#include "Rebalance.h"

// a global class extern'd in .h file
Posdb g_posdb;

// for rebuilding posdb
Posdb g_posdb2;

// resets rdb
void Posdb::reset() { 
	m_rdb.reset();
}

bool Posdb::init ( ) {
	// sanity check
	key144_t k;
	int64_t termId = 123456789LL;
	int64_t docId = 34567292222LL;
	int32_t dist = MAXWORDPOS-1;//54415;
	int32_t densityRank = 10;
	int32_t diversityRank = MAXDIVERSITYRANK-1;//11;
	int32_t wordSpamRank = MAXWORDSPAMRANK-1;//12;
	int32_t siteRank = 13;
	int32_t hashGroup = 1;
	int32_t langId = 59;
	int32_t multiplier = 13;
	char shardedByTermId = 1;
	char isSynonym = 1;
	g_posdb.makeKey ( &k ,
			  termId ,
			  docId,
			  dist,
			  densityRank , // 0-15
			  diversityRank,
			  wordSpamRank,
			  siteRank,
			  hashGroup ,
			  langId,
			  multiplier,
			  isSynonym , // syn?
			  false , // delkey?
			  shardedByTermId );
	// test it out
	if ( g_posdb.getTermId ( &k ) != termId ) { char *xx=NULL;*xx=0; }
	//int64_t d2 = g_posdb.getDocId(&k);
	if ( g_posdb.getDocId (&k ) != docId ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getHashGroup ( &k ) !=hashGroup) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getWordPos ( &k ) !=  dist ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getDensityRank (&k)!=densityRank){ char *xx=NULL;*xx=0; }
	if ( g_posdb.getDiversityRank(&k)!=diversityRank){char *xx=NULL;*xx=0;}
	if ( g_posdb.getWordSpamRank(&k)!=wordSpamRank){ char *xx=NULL;*xx=0; }
	if ( g_posdb.getSiteRank (&k) != siteRank ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getLangId ( &k ) != langId ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getMultiplier ( &k ) !=multiplier){char *xx=NULL;*xx=0; }
	if ( g_posdb.getIsSynonym ( &k ) != isSynonym) { char *xx=NULL;*xx=0; }
	if ( g_posdb.isShardedByTermId(&k)!=shardedByTermId){char *xx=NULL;*xx=0; }
	// more tests
	setDocIdBits ( &k, docId );
	setMultiplierBits ( &k, multiplier );
	setSiteRankBits ( &k, siteRank );
	setLangIdBits ( &k, langId );
	// test it out
	if ( g_posdb.getTermId ( &k ) != termId ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getDocId (&k ) != docId ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getWordPos ( &k ) !=  dist ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getDensityRank (&k)!=densityRank){ char *xx=NULL;*xx=0; }
	if ( g_posdb.getDiversityRank(&k)!=diversityRank){char *xx=NULL;*xx=0;}
	if ( g_posdb.getWordSpamRank(&k)!=wordSpamRank){ char *xx=NULL;*xx=0; }
	if ( g_posdb.getSiteRank (&k) != siteRank ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getHashGroup ( &k ) !=hashGroup) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getLangId ( &k ) != langId ) { char *xx=NULL;*xx=0; }
	if ( g_posdb.getMultiplier ( &k ) !=multiplier){char *xx=NULL;*xx=0; }
	if ( g_posdb.getIsSynonym ( &k ) != isSynonym) { char *xx=NULL;*xx=0; }

	setFacetVal32 ( &k,45678 );
	if ( getFacetVal32 ( &k ) != 45678 ) { char *xx=NULL;*xx=0;}

	/*
	// more tests
	key144_t sk;
	key144_t ek;
	g_posdb.makeStartKey(&sk,termId);
	g_posdb.makeEndKey  (&ek,termId);

	RdbList list;
	list.set(NULL,0,NULL,0,0,true,true,18);
	key144_t ka;
	ka.n2 = 0x1234567890987654ULL;
	ka.n1 = 0x5566778899aabbccULL;
	ka.n0 = (uint16_t)0xbaf1;
	list.addRecord ( (char *)&ka,0,NULL,true );
	key144_t kb;
	kb.n2 = 0x1234567890987654ULL;
	kb.n1 = 0x5566778899aabbccULL;
	kb.n0 = (uint16_t)0xeef1;
	list.addRecord ( (char *)&kb,0,NULL,true );

	char *p = list.m_list;
	char *pend = p + list.m_listSize;
	for ( ; p < pend ; p++ )
		log("db: %02"INT32") 0x%02"XINT32"",p-list.m_list,
		    (int32_t)(*(unsigned char *)p));
	list.resetListPtr();
	list.checkList_r(false,true,RDB_POSDB);
	char *xx=NULL;*xx=0;
	*/

	int64_t  maxTreeMem = 350000000; // 350MB
	// make it lower now for debugging
	//maxTreeMem = 5000000;
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (12 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	int32_t nodeSize      = (sizeof(key144_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = maxTreeMem  / nodeSize ;

	//int32_t pageSize = GB_INDEXDB_PAGE_SIZE;
	// we now use a disk page cache as opposed to the
	// old rec cache. i am trying to do away with the Rdb::m_cache rec
	// cache in favor of cleverly used disk page caches, because
	// the rec caches are not real-time and get stale. 
	//int32_t pcmem    = 30000000; // 30MB
	// make sure at least 30MB
	//if ( pcmem < 30000000 ) pcmem = 30000000;
	// keep this low if we are the tmp cluster, 30MB
	//if ( g_hostdb.m_useTmpCluster && pcmem > 30000000 ) pcmem = 30000000;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	//if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// save more mem!!! allow os to cache it i guess...
	// let's go back to using it
	//pcmem = 0;
	// disable for now... for rebuild
	//pcmem = 0;
	// . init the page cache
	// . MDW: "minimize disk seeks" not working otherwise i'd enable it!
	// if ( ! m_pc.init ( "posdb",
	// 		   RDB_POSDB,
	// 		   pcmem    ,
	// 		   pageSize ))
	// 	return log("db: Posdb init failed.");

	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want posdb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	if ( !m_rdb.init ( g_hostdb.m_dir              ,
			   "posdb"                   ,
			   true                        , // dedup same keys?
			   0                           , // fixed data size
			   // -1 means look in 
			   // CollectionRec::m_posdbMinFilesToMerge
			   -1,//g_conf.m_posdbMinFilesToMerge ,  // 6...
			   maxTreeMem , // g_conf.m_posdbMaxTreeMem  ,
			   maxTreeNodes                ,
			   // now we balance so Sync.cpp can ordered huge lists
			   true                        , // balance tree?
			   0 , // g_conf.m_posdbMaxCacheMem ,
			   0 , // maxCacheNodes 	       ,
			   true                        , // use half keys?
			   false                       , // g_conf.m_posdbSav
			   // newer systems have tons of ram to use
			   // for their disk page cache. it is slower than
			   // ours but the new engine has much slower things
			   NULL,//&m_pc                       ,
			   false , // istitledb?
			   false , // preloaddiskpagecache?
			   sizeof(key144_t)
			   ) )
		return false;

	return true;
	// validate posdb
	//return verify();
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Posdb::init2 ( int32_t treeMem ) {
	//if ( ! setGroupIdTable () ) return false;
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (12 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	int32_t nodeSize     = (sizeof(key144_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = treeMem  / nodeSize ;
	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want posdb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "posdbRebuild"            ,
			    true                        , // dedup same keys?
			    0                           , // fixed data size
			    // change back to 200!!
			    //2                         , // min files to merge
			    //230                       , // min files to merge
			    1000                        , // min files to merge
			    treeMem                     ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0                           , // MaxCacheMem ,
			    0                           , // maxCacheNodes
			    true                        , // use half keys?
			    false                       , // posdbSaveCache
			    NULL                        , // s_pc
			    false ,
			    false ,
			    sizeof(key144_t) ) )
		return false;
	return true;
}


bool Posdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addRdbBase1 ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// do a deep verify to figure out which files are corrupt
	//deepVerify ( coll );
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}

bool Posdb::verify ( char *coll ) {
	return true;
	log ( LOG_DEBUG, "db: Verifying Posdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key144_t startKey;
	key144_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_POSDB   ,
			      cr->m_collnum      ,
			      &list         ,
			      &startKey      ,
			      &endKey        ,
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

	int32_t count = 0;
	int32_t got   = 0;
	bool printedKey = false;
	bool printedZeroKey = false;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key144_t k;
		list.getCurrentKey(&k);
		// skip negative keys
		if ( (k.n0 & 0x01) == 0x00 ) continue;
		count++;
		//uint32_t groupId = k.n1 & g_hostdb.m_groupMask;
		//uint32_t groupId = getGroupId ( RDB_POSDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_POSDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
		else if ( !printedKey ) {
			log ( "db: Found bad key in list (only printing once): "
			      "%"XINT64" %"XINT64" %"XINT32"", k.n2, k.n1 ,(int32_t)k.n0);
			printedKey = true;
		}
		if ( k.n1 == 0 && k.n0 == 0 ) {
			if ( !printedZeroKey ) {
				log ( "db: Found Zero key in list, passing. "
				      "(only printing once)." );
				printedZeroKey = true;
			}
			if ( shardNum != getMyShardNum() )
				got++;
		}
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %"INT32" records in posdb, only %"INT32" belong "
		     "to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
				    "right "
				    "data in the right directory? "
				    "Exiting.");
		log ( "db: Exiting due to Posdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_DEBUG, "db: Posdb passed verification successfully for %"INT32" "
			"recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}


// make just the 6 byte key
void Posdb::makeKey48 ( char              *vkp            ,
			int32_t               wordPos        ,
			char               densityRank    ,
			char               diversityRank  ,
			char               wordSpamRank   ,
			char               hashGroup      ,
			char               langId         ,
			bool               isSynonym      ,
			bool               isDelKey       ) {

	uint32_t kk = wordPos;// = (uint32_t *)(vkp + 2 );
	//*kp = wordPos;
	// GGGG bits
	kk <<= 4;
	kk |= hashGroup;
	// ssss bits (wordspamrank)
	kk <<= 4;
	kk |= wordSpamRank;
	// vvvv bits
	kk <<= 4;
	kk |= diversityRank;
	// FF bits (is syn etc.)
	kk <<= 2;
	if ( isSynonym ) kk |= 0x01;
	// store it
	*(uint32_t *)(vkp + 2) = kk;
	// ppppp density rank bits, etc.
	vkp[1] = ((unsigned char)densityRank) << 3;
	// positive key bit and compression bits.
	vkp[0] = 0x01 | 0x04;
	// one maverick langid bit, the 6th bit
	// TODO: do we need this???
	if ( langId & 0x20 ) vkp[0] |= 0x08;
}


// . see Posdb.h for format of the 12 byte key
// . TODO: substitute var ptrs if you want extra speed
void Posdb::makeKey ( void              *vkp            ,
		      int64_t          termId         ,
		      uint64_t docId          , 
		      int32_t               wordPos        ,
		      char               densityRank    ,
		      char               diversityRank  ,
		      char               wordSpamRank   ,
		      char               siteRank       ,
		      char               hashGroup      ,
		      char               langId         ,
		      int32_t               multiplier     ,
		      bool               isSynonym      ,
		      bool               isDelKey       ,
		      bool shardedByTermId ) {

	// sanity
	if ( siteRank      > MAXSITERANK      ) { char *xx=NULL;*xx=0; }
	if ( wordSpamRank  > MAXWORDSPAMRANK  ) { char *xx=NULL;*xx=0; }
	if ( densityRank   > MAXDENSITYRANK   ) { char *xx=NULL;*xx=0; }
	if ( diversityRank > MAXDIVERSITYRANK ) { char *xx=NULL;*xx=0; }
	if ( langId        > MAXLANGID        ) { char *xx=NULL;*xx=0; }
	if ( hashGroup     > MAXHASHGROUP     ) { char *xx=NULL;*xx=0; }
	if ( wordPos       > MAXWORDPOS       ) { char *xx=NULL;*xx=0; }
	if ( multiplier    > MAXMULTIPLIER    ) { char *xx=NULL;*xx=0; }

	key144_t *kp = (key144_t *)vkp;

	// make sure we mask out the hi bits we do not use first
	termId = termId & TERMID_MASK;
	kp->n2 = termId;
	// then 16 bits of docid
	kp->n2 <<= 16;
	kp->n2 |= docId >> (38-16); // 22

	// rest of docid (22 bits)
	kp->n1 = docId & (0x3fffff);
	// a zero bit for aiding b-stepping alignment issues
	kp->n1 <<= 1;
	kp->n1 |= 0x00;
	// 4 site rank bits
	kp->n1 <<= 4;
	kp->n1 |= siteRank;
	// 4 langid bits
	kp->n1 <<= 5;
	kp->n1 |= (langId & 0x1f);
	// the word position, 18 bits
	kp->n1 <<= 18;
	kp->n1 |= wordPos;
	// the hash group, 4 bits
	kp->n1 <<= 4;
	kp->n1 |= hashGroup;
	// the word span rank, 4 bits
	kp->n1 <<= 4;
	kp->n1 |= wordSpamRank;
	// the diversity rank, 4 bits
	kp->n1 <<= 4;
	kp->n1 |= diversityRank;
	// word form bits, F-bits. right now just use 1 bit
	kp->n1 <<= 2;
	if ( isSynonym ) kp->n1 |= 0x01;

	// density rank, 5 bits
	kp->n0 = densityRank;
	// is in outlink text? reserved
	kp->n0 <<= 1;
	// a 1 bit for aiding b-stepping
	kp->n0 <<= 1;
	kp->n0 |= 0x01;
	// multiplier bits, 5 bits
	kp->n0 <<= 5;
	kp->n0 |= multiplier;
	// one maverick langid bit, the 6th bit
	kp->n0 <<= 1;
	if ( langId & 0x20 ) kp->n0 |= 0x01;
	// compression bits, 2 of 'em
	kp->n0 <<= 2;
	// delbit
	kp->n0 <<= 1;
	if ( ! isDelKey ) kp->n0 |= 0x01;

	if ( shardedByTermId ) setShardedByTermIdBit ( kp );

	// get the one we lost
	// char *kstr = KEYSTR ( kp , sizeof(POSDBKEY) );
	// if (!strcmp(kstr,"0x0ca3417544e400000000000032b96bf8aa01"))
	// 	log("got lost key");
}

RdbCache g_termFreqCache;
static bool s_cacheInit = false;

// . accesses RdbMap to estimate size of the indexList for this termId
// . returns an UPPER BOUND
// . because this is over POSDB now and not indexdb, a document is counted
//   once for every occurence of term "termId" it has... :{
int64_t Posdb::getTermFreq ( collnum_t collnum, int64_t termId ) {

	// establish the list boundary keys
	key144_t startKey ;
	key144_t endKey   ;
	makeStartKey ( &startKey, termId );
	makeEndKey   ( &endKey  , termId );


	// doint qa test?
	bool qaTest = false;
	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	if ( cr && strcmp(cr->m_coll,"qatest123") == 0 )
		qaTest = true;

	// if so, use the exact size
	if ( qaTest ) {
		Msg5 msg5;
		RdbList list;
		g_threads.disableThreads();
		msg5.getList ( RDB_POSDB   ,
			       collnum      ,
			      &list         ,
			       &startKey      ,
			       &endKey        ,
			      64000000      , // minRecSizes   ,
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
			       NULL        , // msg5b ptr
			       true          );
		// re-enable threads
		g_threads.enableThreads();
		//int64_t numBytes = list.getListSize();
		// see how many diff docids we have... easier to debug this
		// loop over entries in list
		int64_t docId = 0;
		int64_t count = 0;
		for ( list.resetListPtr() ; ! list.isExhausted() ;
		      list.skipCurrentRecord() ) {
			key144_t k; list.getCurrentKey(&k);
			// is it a delete?
			if ( (k.n0 & 0x01) == 0x00 ) continue;
			int64_t d = g_posdb.getDocId(&k);
			if ( d == docId ) continue;
			docId = d;
			count++;
		}		
		// convert to # keys, approx. just an estimate since
		// some keys are compressed...
		// none except first key are full size. they are all just
		// 12 bytes etc.
		int64_t numKeys = count;
		if ( numKeys < 0 ) numKeys = 0;
		// and assume each shard has about the same #
		numKeys *= g_hostdb.m_numShards;
		return numKeys;
	}
 
	

	//collnum_t collnum = g_collectiondb.getCollnum ( coll );

	if ( ! s_cacheInit ) {
		int32_t maxMem = 20000000; // 20MB
		maxMem = 5000000; // 5MB now... save mem
		int32_t maxNodes = maxMem / 17; // 8+8+1
		if( ! g_termFreqCache.init ( maxMem   , // maxmem 20MB
					     8        , // fixed data size
					     false    , // supportlists?
					     maxNodes ,
					     false    , // use half keys?
					     "tfcache", // dbname
					     false    , // load from disk?
					     8        , // cache key size
					     0          // data key size
					     ))
			log("posdb: failed to init termfreqcache: %s",
			    mstrerror(g_errno));
		// ignore errors
		g_errno = 0;
		s_cacheInit = true;
	}

	// . check cache for super speed
	// . TODO: make key incorporate collection
	// . colnum is 0 for now
	int64_t val = g_termFreqCache.getLongLong2 ( collnum ,
						       termId  , // key
						       500   , // maxage secs
						       true    );// promote?



	// -1 means not found in cache. if found, return it though.
	// do not return even if found if we are doing a qa test.
	if ( val >= 0 && ! qaTest ) {
		//log("posdb: got %"INT64" in cache",val);
		return val;
	}

	// . ask rdb for an upper bound on this list size
	// . but actually, it will be somewhat of an estimate 'cuz of RdbTree
	key144_t maxKey;
	//int64_t maxRecs;
	// . don't count more than these many in the map
	// . that's our old truncation limit, the new stuff isn't as dense
	//int32_t oldTrunc = 100000;
	// turn this off for this
	int64_t oldTrunc = -1;
	// get maxKey for only the top "oldTruncLimit" docids because when
	// we increase the trunc limit we screw up our extrapolation! BIG TIME!
	int64_t maxRecs = m_rdb.getListSize(collnum,
					    (char *)&startKey,
					    (char *)&endKey,
					    (char *)&maxKey,
					    oldTrunc );


	int64_t numBytes = 0;

	// get the # more slowly but exact for qa tests so it agrees
	// with the results of the last time we ran it
	// if ( qaTest )
	// 	// TODO: just get the actual list and count unique docids
	// 	// with a blocking msg5...
	// 	numBytes += m_rdb.m_buckets.getListSizeExact(collnum,
	// 						(char *)&startKey,
	// 						(char *)&endKey);
	// else
	numBytes += m_rdb.m_buckets.getListSize(collnum,
						(char *)&startKey,
						(char *)&endKey,
						NULL,NULL);



	// convert from size in bytes to # of recs
	maxRecs += numBytes / sizeof(POSDBKEY);

	// RdbList list;
	// makeStartKey ( &startKey, termId );
	// makeEndKey   ( &endKey  , termId );
	// int numNeg = 0;
	// int numPos = 0;
	// m_rdb.m_buckets.getList ( collnum ,
	// 			  (char *)&startKey,
	// 			  (char *)&endKey,
	// 			  -1 , // minrecsizes
	// 			  &list,
	// 			  &numPos,
	// 			  &numNeg,
	// 			  true );
	// if ( numPos*18 != numBytes ) {
	// 	char *xx=NULL;*xx=0; }

	

	// and assume each shard has about the same #
	maxRecs *= g_hostdb.m_numShards;

	// over all splits!
	//maxRecs *= g_hostdb.m_numShards;
	// . assume about 8 bytes per key on average for posdb.
	// . because of compression we got 12 and 6 byte keys in here typically
	//   for a single termid
	//maxRecs /= 8;

	// log it
	//log("posdb: approx=%"INT64" exact=%"INT64"",maxRecs,numBytes);

	// now cache it. it sets g_errno to zero.
	g_termFreqCache.addLongLong2 ( collnum, termId, maxRecs );
	// return it
	return maxRecs;
}

//////////////////
//
// THE NEW INTERSECTION LOGIC
//
//////////////////

#include "gb-include.h"

#include "Stats.h"
#include <math.h>
#include "Conf.h"
#include "Mem.h"        // getHighestLitBitValue()
#include "TopTree.h"
#include "sort.h"
#include "RdbBase.h"
#include "Msg39.h"
//#include "CollectionRec.h"
#include "SearchInput.h"
#include "Timedb.h"

// global var
TopTree *g_topTree;

PosdbTable::PosdbTable() { 
	// top docid info
	m_q             = NULL;
	m_r             = NULL;
	reset();
}

PosdbTable::~PosdbTable() { 
	reset(); 
}

void PosdbTable::reset() {
	// we can't reset this because we don't recall allocTopTree()
	// again when computing search results in docid ranges.
	//m_hasFacetTerm = false;
	// has init() been called?
	m_initialized          = false;
	m_estimatedTotalHits   = -1;
	m_errno                   = 0;
	freeMem();
	// does not free the mem of this safebuf, only resets length
	m_docIdVoteBuf.reset();
	m_filtered = 0;
	m_qiBuf.reset();
	// assume no-op
	m_t1 = 0LL;
	m_whiteListTable.reset();
	m_addedSites = false;
}

// realloc to save mem if we're rat
void PosdbTable::freeMem ( ) {
}

// . returns false on error and sets g_errno
// . NOTE: termFreqs is just referenced by us, not copied
// . sets m_startKeys, m_endKeys and m_minNumRecs for each termId
// . TODO: ensure that m_termFreqs[] are all UPPER BOUNDS on the actual #!!
//         we should be able to get an upper bound estimate from the b-tree
//         quickly using Msg36!
// . we now support multiple plus signs before the query term
// . lists[] and termFreqs[] must be 1-1 with q->m_qterms[]
void PosdbTable::init ( Query     *q               , 
			char       debug         , 
			void      *logstate        ,
			TopTree   *topTree         ,
			collnum_t collnum , // char      *coll            , 
			Msg2 *msg2 ,
			//IndexList *lists           ,
			//int32_t       numLists        ,
			Msg39Request *r            ) {
	// sanity check -- watch out for double calls
	if ( m_initialized ) { char *xx= NULL; *xx =0; }
	// clear everything
	reset();
	// we are now
	m_initialized = true;
	// set debug flag
	m_debug = debug;
	// this mean to do it too!
	if ( g_conf.m_logDebugQuery ) m_debug = 1;//true;
	// we should save the lists!
	//m_lists    = msg2->m_lists;//lists;
	//m_numLists = q->m_numTerms;
	m_msg2 = msg2;
	// sanity
	if ( m_msg2 && ! m_msg2->m_query ) { char *xx=NULL;*xx=0; }
	// save this
	m_collnum = r->m_collnum;
	// save the request
	m_r = r;

	// save this
	//m_coll = coll;
	// get the rec for it
        CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
        if ( ! cr ) { char *xx=NULL;*xx=0; }
	// set this now
	//m_collnum = cr->m_collnum;

	// save it
	m_topTree = topTree;
	// a ptr for debugging i guess
	g_topTree = topTree;
	// remember the query class, it has all the info about the termIds
	m_q = q;
	m_nqt = q->getNumTerms();
	// for debug msgs
	m_logstate = logstate;

	m_realMaxTop = r->m_realMaxTop;
	if ( m_realMaxTop > MAX_TOP ) m_realMaxTop = MAX_TOP;

	m_siteRankMultiplier = SITERANKMULTIPLIER;
	if ( m_q->m_isBoolean ) m_siteRankMultiplier = 0.0;

	// seo.cpp supplies a NULL msg2 because it already sets
	// QueryTerm::m_posdbListPtrs
	if ( ! msg2 ) return;
	// sanity
	if ( msg2->getNumLists() != m_q->getNumTerms() ) {char *xx=NULL;*xx=0;}
	// copy the list ptrs to the QueryTerm::m_posdbListPtr
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) 
		m_q->m_qterms[i].m_posdbListPtr = msg2->getList(i);
	// we always use it now
	if ( ! topTree ) {char *xx=NULL;*xx=0;}
}

// this is separate from allocTopTree() function below because we must
// call it for each iteration in Msg39::doDocIdSplitLoop() which is used
// to avoid reading huge termlists into memory. it breaks the huge lists
// up by smaller docid ranges and gets the search results for each docid
// range separately.
bool PosdbTable::allocWhiteListTable ( ) {
	//
	// the whitetable is for the docids in the whitelist. we have
	// to only show results whose docid is in the whitetable, which
	// is from the "&sites=abc.com+xyz.com..." custom search site list
	// provided by the user.
	//
	if ( m_r->size_whiteList <= 1 ) m_useWhiteTable = false; // inclds \0
	else 		                m_useWhiteTable = true;
	RdbList *whiteLists = m_msg2->m_whiteLists;
	int32_t nw = m_msg2->m_w;
	int32_t sum = 0;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		RdbList *list = &whiteLists[i];
		if ( list->isEmpty() ) continue;
		// assume 12 bytes for all keys but first which is 18
		int32_t size = list->getListSize();
		sum += size / 12 + 1;
	}
	if ( sum ) {
		// making this sum * 3 does not show a speedup... hmmm...
		int32_t numSlots = sum * 2;
		// keep it restricted to 5 byte keys so we do not have to
		// extract the docid, we can just hash the ptr to those
		// 5 bytes (which includes 1 siterank bit as the lowbit,
		// but should be ok since it should be set the same in
		// all termlists that have that docid)
		if ( ! m_whiteListTable.set(5,0,numSlots,NULL,0,false,
					    0,"wtall"))
			return false;
		// try to speed up. wow, this slowed it down about 4x!!
		//m_whiteListTable.m_maskKeyOffset = 1;
		//
		////////////
		//
		// this seems to make it like 20x faster... 1444ms vs 27000ms:
		//
		////////////
		//
		m_whiteListTable.m_useKeyMagic = true;
	}
	return true;
}


bool PosdbTable::allocTopTree ( ) {
	int64_t nn1 = m_r->m_docsToGet;
	int64_t nn2 = 0;
	// just add all up in case doing boolean OR or something
	for ( int32_t k = 0 ; k < m_msg2->m_numLists;k++){//getNumLists();k++){
		// count
		RdbList *list = m_msg2->getList(k);
		// skip if null
		if ( ! list ) continue;
		// skip if list is empty, too
		if ( list->isEmpty() ) continue;
		// show if debug
		if ( m_debug )
			log("toptree: adding listsize %"INT32" to nn2",
			    list->m_listSize);
		// tally. each new docid in this termlist will compress
		// the 6 byte termid out, so reduce by 6.
		nn2 += list->m_listSize / ( sizeof(POSDBKEY) -6 );
	}

	// if doing docid range phases where we compute the winning docids
	// for a range of docids to save memory, then we need to amp this up
	if ( m_r->m_numDocIdSplits > 1 ) {
		// if 1 split has only 1 docid the other splits
		// might have 10 then this doesn't work, so make it
		// a min of 100.
		if ( nn2 < 100 ) nn2 = 100;		
		// how many docid range splits are we doing?
		nn2 *= m_r->m_numDocIdSplits;
		// just in case one split is not as big
		nn2 *= 2;

		// boost this guy too since we compare it to nn2
		if ( nn1 < 100 ) nn1 = 100;
		nn1 *= m_r->m_numDocIdSplits;
		nn1 *= 2;
	}
		
	// do not go OOM just because client asked for 10B results and we
	// only have like 100 results.
	int64_t nn = nn2;
	if ( nn1 < nn2 ) nn = nn1;

	

	// . do not alloc space for anything if all termlists are empty
	// . before, even if nn was 0, top tree would alloc a bunch of nodes
	//   and we don't want to do that now to save mem and so 
	//   Msg39 can check 
	//   if ( m_posdbTable.m_topTree->m_numNodes == 0 )
	//   to see if it should
	//   advance to the next docid range or not.
	if ( nn == 0 )
		return true;

	// always at least 100 i guess. why? it messes up the
	// m_scoreInfoBuf capacity and it cores
	//if ( nn < 100 ) nn = 100;
	// but 30 is ok since m_scoreInfo buf uses 32
	if ( nn < 30 ) nn = 30;


	if ( m_r->m_doSiteClustering ) nn *= 2;

        // limit to this regardless!
        //CollectionRec *cr = g_collectiondb.getRec ( m_coll );
        //if ( ! cr ) return false;

	// limit to 2B docids i guess
	if ( nn > 2000000000 ) nn = 2000000000;

	if ( m_debug )
		log("toptree: toptree: initializing %"INT64" nodes",nn);

	if ( nn < m_r->m_docsToGet && m_debug )
		log("query: warning only getting up to %"INT64" docids "
		    "even though %"INT32" requested because termlist "
		    "sizes are so small!! splits=%"INT32""
		    , nn
		    , m_r->m_docsToGet 
		    , (int32_t)m_r->m_numDocIdSplits
		    );

	// keep it sane
	if ( nn > m_r->m_docsToGet * 2 && nn > 60 )
		nn = m_r->m_docsToGet * 2;

	// this actually sets the # of nodes to MORE than nn!!!
	if ( ! m_topTree->setNumNodes(nn,m_r->m_doSiteClustering)) {
		log("toptree: toptree: error allocating nodes: %s",
		    mstrerror(g_errno));
		return false;
	}
	// let's use nn*4 to try to get as many score as possible, although
	// it may still not work!
	int32_t xx = nn;//m_r->m_docsToGet ;
	// try to fix a core of growing this table in a thread when xx == 1
	if ( xx < 32 ) xx = 32;
	//if ( m_r->m_doSiteClustering ) xx *= 4;
	m_maxScores = xx;
	// for seeing if a docid is in toptree. niceness=0.
	//if ( ! m_docIdTable.set(8,0,xx*4,NULL,0,false,0,"dotb") )
	//	return false;

	if ( m_r->m_getDocIdScoringInfo ) {

		m_scoreInfoBuf.setLabel ("scinfobuf" );

		// . for holding the scoring info
		// . add 1 for the \0 safeMemcpy() likes to put at the end so 
		//   it will not realloc on us
		if ( ! m_scoreInfoBuf.reserve ( xx * sizeof(DocIdScore) +100) )
			return false;
		// likewise how many query term pair scores should we get?
		int32_t numTerms = m_q->m_numTerms;
		// limit
		if ( numTerms > 10 ) numTerms = 10;
		// the pairs. divide by 2 since (x,y) is same as (y,x)
		int32_t numPairs = (numTerms * numTerms) / 2;
		// then for each pair assume no more than MAX_TOP reps, usually
		// it's just 1, but be on the safe side
		numPairs *= m_realMaxTop;//MAX_TOP;
		// now that is how many pairs per docid and can be 500! but we
		// still have to multiply by how many docids we want to 
		// compute. so this could easily get into the megabytes, most 
		// of the time we will not need nearly that much however.
		numPairs *= xx;

		m_pairScoreBuf.setLabel ( "pairbuf" );
		m_singleScoreBuf.setLabel ("snglbuf" );

		// but alloc it just in case
		if ( ! m_pairScoreBuf.reserve (numPairs * sizeof(PairScore) ) )
			return false;
		// and for singles
		int32_t numSingles = numTerms * m_realMaxTop * xx; // MAX_TOP *xx;
		if ( !m_singleScoreBuf.reserve(numSingles*sizeof(SingleScore)))
			return false;
	}

	//bool useNewAlgo = false;
	// set the m_qiBuf, alloc it etc.
	//if ( m_r->m_useNewAlgo && ! setQueryTermInfo () )
	//	return false;

	/*
	  when we bring back fast intersections we can bring this back
	  when doAlternativeAlgo is true again
	// merge buf
	int64_t total = 0LL;
	for ( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
		// count
		RdbList *list = m_msg2->getList(k);
		// skip if null
		if ( ! list ) continue;
		// skip if list is empty, too
		if ( list->isEmpty() ) continue;
		// tally
		total += list->m_listSize;
	}
	if ( ! m_mergeBuf.reserve ( total + 12 ) ) return false;
	*/

	m_hasFacetTerm = false;
	//
	// allocate space for QueryTerm::m_facetHashList and QueryTerm::m_dt
	//
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		QueryTerm *qt = &m_q->m_qterms[i];
		// skip if not facet
		if ( qt->m_fieldCode != FIELD_GBFACETSTR &&
		     qt->m_fieldCode != FIELD_GBFACETINT &&
		     qt->m_fieldCode != FIELD_GBFACETFLOAT )
			continue;
		// how big?
		int64_t total = m_msg2->m_lists[i].getListSize();
		// skip if empty. no we could be doing a split that is
		// empty but other splits are full
		if ( total == 0 && m_r->m_numDocIdSplits <= 1 ) {
			log("query: empty facets for term #%i",i);
			continue;
		}
		// need this
		QueryWord *qw = qt->m_qword;
		// we got facet terms
		m_hasFacetTerm = true;
		// assume list is a unique site for section hash dup
		int32_t maxRecs = total / 6 + 1;
		// slot
		int32_t slots = maxRecs * 4;

		// if user provided a comma separated range like
		// gbfacetfloat:price,0-10.0,10.0-30,30-100,100-1000
		// then that is the max # of slots
		if ( slots > qw->m_numFacetRanges ) slots=qw->m_numFacetRanges;

		// min of at least 20 otherwise m_dt re-allocs in thread and
		// causes a core!
		if ( slots  < 32 ) slots = 32;

		// if we already initialized table for previous docid range phase
		// do not clear it out!
		if ( qt->m_facetHashTable.getNumSlots() > 0 ) continue;

		// limit this bitch to 10 million otherwise this gets huge!
		// like over 28 million i've seen and it goes oom
		if ( slots > 2000000 ) {
			log("posdb: limiting FACET list to 2M docids "
			    "for query term %s",qt->m_term);
			slots = 20000000;
		}

		log("query: using %i slots for query term #%i",slots,i);

		// . each site hash is 4 bytes
		// . we have to store each unique val in here for transmitting
		//   back to msg3a so it can merge them and compute the final
		//   stats. it really makes no sense for a shard to compute
		//   stats. it has to be done at the aggregator node.
		if ( ! qt->m_facetHashTable.set ( 4,sizeof(FacetEntry),
						  slots,NULL,0,false,
						  0,"qfht" 
						  // use magic b/c keys seems
						  // pretty similar in the 
						  // lower bits sometimes
						  , true
						  ) )
			return false;
		// make it nongrowable because we'll be in a thread
		qt->m_facetHashTable.setNonGrow();
	}

	// m_stackBuf
	int32_t   nqt = m_q->m_numTerms;
	int32_t need  = 0;
	need += 4 * nqt;
	need += 4 * nqt;
	need += 4 * nqt;
	need += 4 * nqt;
	need += sizeof(float ) * nqt;
	need += sizeof(char *) * nqt;
	need += sizeof(char *) * nqt;
	need += sizeof(char *) * nqt;
	need += sizeof(char *) * nqt;
	need += sizeof(char *) * nqt;
	need += sizeof(char  ) * nqt;
	need += sizeof(float ) * nqt * nqt; // square matrix
	m_stackBuf.setLabel("stkbuf1");
	if ( ! m_stackBuf.reserve( need ) )
		return false;

	return true;
}

PosdbTable *g_this = NULL;

static bool  s_init = false;
static float s_diversityWeights [MAXDIVERSITYRANK+1];
static float s_densityWeights   [MAXDENSITYRANK+1];
static float s_wordSpamWeights  [MAXWORDSPAMRANK+1]; // wordspam
// siterank of inlinker for link text:
static float s_linkerWeights    [MAXWORDSPAMRANK+1]; 
static float s_hashGroupWeights [HASHGROUP_END];
static char  s_isCompatible     [HASHGROUP_END][HASHGROUP_END];
static char  s_inBody           [HASHGROUP_END];

// initialize the weights table
void initWeights ( ) {
	if ( s_init ) return;
	s_init = true;
	float sum = 0.15;
	for ( int32_t i = 0 ; i <= MAXDIVERSITYRANK ; i++ ) {
		//s_diversityWeights[i] = sum;
		// disable for now
		s_diversityWeights[i] = 1.0; // sum
		sum *= 1.135; // so a rank of 15 implies a weight of 1.0
	}
	// density rank to weight
	//sum = 0.0;
	sum = 0.35;
	for ( int32_t i = 0 ; i <= MAXDENSITYRANK ; i++ ) {
		//sum += 1.0/(MAXDENSITYRANK+1);
		if ( sum > 1.0 ) sum = 1.0;
		s_densityWeights[i] = sum;
		// used wolframalpha.com to get this
		// entered '.15 * X^31 = 1.00'
		sum *= 1.03445; // so a rank of 31 implies a weight of 1.0
	}
	// . word spam rank to weight
	// . make sure if word spam is 0 that the weight is not 0!
	for ( int32_t i = 0 ; i <= MAXWORDSPAMRANK ; i++ )
		s_wordSpamWeights[i] = (float)(i+1) / (MAXWORDSPAMRANK+1);

	// site rank of inlinker
	// to be on the same level as multiplying the final score
	// by the siterank+1 we should make this a sqrt() type thing
	// since we square it so that single term scores are on the same
	// level as term pair scores
	for ( int32_t i = 0 ; i <= MAXWORDSPAMRANK ; i++ )
		s_linkerWeights[i] = sqrt(1.0 + i);
	
	// if two hashgroups are comaptible they can be paired
	for ( int32_t i = 0 ; i < HASHGROUP_END ; i++ ) {
		// set this
		s_inBody[i] = 0;
		// is it body?
		if ( i == HASHGROUP_BODY    ||
		     i == HASHGROUP_HEADING ||
		     i == HASHGROUP_INLIST  ||
		     i == HASHGROUP_INMENU   )
			s_inBody[i] = 1;
		for ( int32_t j = 0 ; j < HASHGROUP_END ; j++ ) {
			// assume not
			s_isCompatible[i][j] = 0;
			// or both in body (and not title)
			bool inBody1 = true;
			if ( i != HASHGROUP_BODY &&
			     i != HASHGROUP_HEADING && 
			     i != HASHGROUP_INLIST &&
			     //i != HASHGROUP_INURL &&
			     i != HASHGROUP_INMENU )
				inBody1 = false;
			bool inBody2 = true;
			if ( j != HASHGROUP_BODY &&
			     j != HASHGROUP_HEADING && 
			     j != HASHGROUP_INLIST &&
			     //j != HASHGROUP_INURL &&
			     j != HASHGROUP_INMENU )
				inBody2 = false;
			// no body allowed now!
			if ( inBody1 || inBody2 ) continue;
			//if ( ! inBody ) continue;
			// now neither can be in the body, because we handle
			// those cases in the new sliding window algo.
			// if one term is only in the link text and the other
			// term is only in the title, ... what then? i guess
			// allow those here, but they will be penalized
			// some with the fixed distance of like 64 units or
			// something...
			s_isCompatible[i][j] = 1;
			// if either is in the body then do not allow now
			// and handle in the sliding window algo
			//s_isCompatible[i][j] = 1;
		}
	}

	s_hashGroupWeights[HASHGROUP_BODY              ] = 1.0;
	s_hashGroupWeights[HASHGROUP_TITLE             ] = 8.0;
	s_hashGroupWeights[HASHGROUP_HEADING           ] = 1.5;//3.0
	s_hashGroupWeights[HASHGROUP_INLIST            ] = 0.3;
	// fix toyota cressida photos GALLERY by making this 0.1!
	s_hashGroupWeights[HASHGROUP_INMETATAG         ] = 0.1;//1.5;//2.0;
	s_hashGroupWeights[HASHGROUP_INLINKTEXT        ] = 16.0;
	s_hashGroupWeights[HASHGROUP_INTAG             ] = 1.0;
	// ignore neighborhoods for now kinda
	s_hashGroupWeights[HASHGROUP_NEIGHBORHOOD      ] = 0.0; // 2.0;
	s_hashGroupWeights[HASHGROUP_INTERNALINLINKTEXT] = 4.0;
	s_hashGroupWeights[HASHGROUP_INURL             ] = 1.0;
	s_hashGroupWeights[HASHGROUP_INMENU            ] = 0.2;
}


float getHashGroupWeight ( unsigned char hg ) {
	if ( ! s_init ) initWeights();
	return s_hashGroupWeights[hg];
}

float getDiversityWeight ( unsigned char diversityRank ) {
	if ( ! s_init ) initWeights();
	return s_diversityWeights[diversityRank];
}

float getDensityWeight ( unsigned char densityRank ) {
	if ( ! s_init ) initWeights();
	return s_densityWeights[densityRank];
}

float getWordSpamWeight ( unsigned char wordSpamRank ) {
	if ( ! s_init ) initWeights();
	return s_wordSpamWeights[wordSpamRank];
}

float getLinkerWeight ( unsigned char wordSpamRank ) {
	if ( ! s_init ) initWeights();
	return s_linkerWeights[wordSpamRank];
}

float getTermFreqWeight ( int64_t termFreq , int64_t numDocsInColl ) {
	// do not include top 6 bytes at top of list that are termid
	//float fw = listSize - 6;
	// sanity
	//if ( fw < 0 ) fw = 0;
	// estimate # of docs that have this term. the problem is
	// that posdb keys can be 18, 12 or 6 bytes!
	//fw /= 11.0;
	// adjust this so its per split!
	//int32_t nd = numDocsInColl / g_hostdb.m_numShards;
	float fw = termFreq;
	// what chunk are we of entire collection?
	//if ( nd ) fw /= nd;
	if ( numDocsInColl ) fw /= numDocsInColl;
	// limit
	if ( fw > .50 ) fw = .50;
	// make a percent
	//fw *= 100.0;
	// . map <=  1% to a weight of 1.00
	// . map <=  2% to a weight of 0.99
	// . map <=  3% to a weight of 0.98
	// . map <= 50% to a weight of 0.50
	//return 1.00 - fw;
	// . invert since we use the MIN algorithm for scoring!
	// . so the more important terms need to score much higher now
	//   to avoid being in the min scoring term pair
	return .50 + fw;
}

/*
bool printDiversityWeightTable ( SafeBuf &sb , bool isXml ) {

	if ( ! isXml )
		sb.safePrintf("<table border=1>"
			      "<tr><td colspan=2>"
			      "<center>diversityRank to Weight</center>"
			      "</td></tr>"
			      "<tr><td>diversityRank</td>"
			      "<td>weight</td></tr>"
			      );

}
*/

// TODO: truncate titles at index time?

// kinda like getTermPairScore, but uses the word positions currently
// pointed to by ptrs[i] and does not scan the word position lists.
// also tries to sub-out each term with the title or linktext wordpos term
// pointed to  by "bestPos[i]"
void PosdbTable::evalSlidingWindow ( char **ptrs , 
				     int32_t   nr , 
				     char **bestPos ,
				     float *scoreMatrix ,
				     int32_t   advancedTermNum ) {

	char *wpi;
	char *wpj;
	float wikiWeight;
	char *maxp1 = NULL;
	char *maxp2;
	//bool fixedDistance;
	//char *winners1[MAX_QUERY_TERMS*MAX_QUERY_TERMS];
	//char *winners2[MAX_QUERY_TERMS*MAX_QUERY_TERMS];
	//float scores  [MAX_QUERY_TERMS*MAX_QUERY_TERMS];
	float minTermPairScoreInWindow = 999999999.0;

	// TODO: only do this loop on the (i,j) pairs where i or j
	// is the term whose position got advanced in the sliding window.
	// advancedTermNum is -1 on the very first sliding window so we
	// establish our max scores into the scoreMatrix.
	int32_t maxi = nr;
	//if ( advancedTermNum >= 0 ) maxi = advancedTermNum + 1;

	for ( int32_t i = 0 ; i < maxi ; i++ ) {

		// skip if to the left of a pipe operator
		if ( m_bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) )
			continue;

		//if ( ptrs[i] ) wpi = ptrs[i];
		// if term does not occur in body, sub-in the best term
		// from the title/linktext/etc.
		//else           wpi = bestPos[i];

		wpi = ptrs[i];

		// only evaluate pairs that have the advanced term in them
		// to save time.
		int32_t j = i + 1;
		int32_t maxj = nr;
		//if ( advancedTermNum >= 0 && i != advancedTermNum ) {
		//	j = advancedTermNum;
		//	maxj = j+1;
		//}

	// loop over other terms
	for ( ; j < maxj ; j++ ) {

		// skip if to the left of a pipe operator
		if ( m_bflags[j] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) )
			continue;

		// TODO: use a cache using wpi/wpj as the key. 
		//if ( ptrs[j] ) wpj = ptrs[j];
		// if term does not occur in body, sub-in the best term
		// from the title/linktext/etc.
		//else wpj = bestPos[j];

		wpj = ptrs[j];

		// in same wikipedia phrase?
		if ( m_wikiPhraseIds[j] == m_wikiPhraseIds[i] &&
		     // zero means not in a phrase
		     m_wikiPhraseIds[j] ) {
			// try to get dist that matches qdist exactly
			m_qdist = m_qpos[j] - m_qpos[i];
			// wiki weight
			wikiWeight = WIKI_WEIGHT; // .50;
		}
		else {
			// basically try to get query words as close
			// together as possible
			m_qdist = 2;
			// fix 'what is an unsecured loan' to get the
			// exact phrase with higher score
			//m_qdist = m_qpos[j] - m_qpos[i];
			// wiki weight
			wikiWeight = 1.0;
		}

		// this will be -1 if wpi or wpj is NULL
		float max = getTermPairScoreForWindow ( i,j,wpi, wpj, 0 );

		// try sub-ing in the best title occurence or best
		// inlink text occurence. cuz if the term is in the title
		// but these two terms are really far apart, we should
		// get a better score
		float score = getTermPairScoreForWindow ( i,j,bestPos[i], 
							  wpj ,
							  FIXED_DISTANCE );
		if ( score > max ) {
			maxp1 = bestPos[i];
			maxp2 = wpj;
			max   = score;
			//fixedDistance = true;
		}
		else {
			maxp1 = wpi;
			maxp2 = wpj;
			//fixedDistance = false;
		}

		// a double pair sub should be covered in the 
		// getTermPairScoreForNonBody() function
		score = getTermPairScoreForWindow ( i,j,bestPos[i], 
						    bestPos[j] ,
						    FIXED_DISTANCE );
		if ( score > max ) {
			maxp1 = bestPos[i];
			maxp2 = bestPos[j];
			max   = score;
			//fixedDistance = true;
		}

		score = getTermPairScoreForWindow ( i,j,wpi , 
						    bestPos[j] ,
						    FIXED_DISTANCE );
		if ( score > max ) {
			maxp1 = wpi;
			maxp2 = bestPos[j];
			max   = score;
			//fixedDistance = true;
		}

		// wikipedia phrase weight
		if ( wikiWeight != 1.0 ) max *= wikiWeight;

		// term freqweight here
		max *= m_freqWeights[i] * m_freqWeights[j];

		// use score from scoreMatrix if bigger
		if ( scoreMatrix[m_nqt*i+j] > max ) {
			max = scoreMatrix[m_nqt*i+j];
			//if ( m_ds ) {
			//	winners1[i*MAX_QUERY_TERMS+j] = NULL;
			//	winners2[i*MAX_QUERY_TERMS+j] = NULL;
			//}
		}
		// if we end up selecting this window we will want to know
		// the term pair scoring information, but only if we
		// did not take the score from the scoreMatrix, which only
		// contains non-body term pairs.
		//else if ( m_ds ) {
		//	winners1[i*MAX_QUERY_TERMS+j] = maxp1;
		//	winners2[i*MAX_QUERY_TERMS+j] = maxp2;
		//	scores  [i*MAX_QUERY_TERMS+j] = max;
		//}


		// in same quoted phrase?
		if ( m_quotedStartIds[j] >= 0 &&
		     m_quotedStartIds[j] == m_quotedStartIds[i] ) {
			// no subouts allowed i guess
			if ( ! wpi ) {
				max = -1.0;
			}
			else if ( ! wpj ) {
				max = -1.0;
			}
			else {
				int32_t qdist = m_qpos[j] - m_qpos[i];
				int32_t p1 = g_posdb.getWordPos ( wpi );
				int32_t p2 = g_posdb.getWordPos ( wpj );
				int32_t  dist = p2 - p1;
				// must be in right order!
				if ( dist < 0 ) {
					max = -1.0;
					//log("ddd0: i=%"INT32" j=%"INT32" "
					//    "dist=%"INT32" qdist=%"INT32"",
					//    i,j,dist,qdist);
				}
				// allow for a discrepancy of 1 unit in case 
				// there is a comma? i think we add an extra 
				// unit
				else if ( dist > qdist && dist - qdist > 1 ) {
					max = -1.0;
					//log("ddd1: i=%"INT32" j=%"INT32" "
					//    "dist=%"INT32" qdist=%"INT32"",
					//    i,j,dist,qdist);
				}
				else if ( dist < qdist && qdist - dist > 1 ) {
					max = -1.0;
					//log("ddd2: i=%"INT32" j=%"INT32" "
					//    "dist=%"INT32" qdist=%"INT32"",
					//    i,j,dist,qdist);
				}
				//else {
				//	log("ddd3: i=%"INT32" j=%"INT32" "
				//	    "dist=%"INT32" qdist=%"INT32"",
				//	    i,j,dist,qdist);
				//}
			}
		}


		// now we want the sliding window with the largest min
		// term pair score!
		if ( max < minTermPairScoreInWindow ) 
			minTermPairScoreInWindow = max;
	}
	}

	if ( minTermPairScoreInWindow <= m_bestWindowScore ) return;

	m_bestWindowScore = minTermPairScoreInWindow;

	// record term positions in winning window
	for ( int32_t i = 0 ; i < maxi ; i++ )
		m_windowTermPtrs[i] = ptrs[i];	
	

	/*
	if ( ! m_ds ) return;

	for ( int32_t i = 0   ; i < nr ; i++ ) {
	for ( int32_t j = i+1 ; j < nr ; j++ ) {
		m_finalWinners1[i*MAX_QUERY_TERMS+j] = 
			winners1[i*MAX_QUERY_TERMS+j];
		m_finalWinners2[i*MAX_QUERY_TERMS+j] = 
			winners2[i*MAX_QUERY_TERMS+j];
		m_finalScores  [i*MAX_QUERY_TERMS+j] = 
			scores  [i*MAX_QUERY_TERMS+j];
		// sanity
		//if ( winners1[i*MAX_QUERY_TERMS+j])
		//unsigned char hg1;
		//hg1=g_posdb.getHashGroup(winners1[i*MAX_QUERY_TERMS+j]
		//if ( winners2[i*MAX_QUERY_TERMS+j])
		//unsigned char hg2;
		//hg2=g_posdb.getHashGroup(winners2[i*MAX_QUERY_TERMS+j]
		//log("winner %"INT32" x %"INT32" 0x%"XINT32" 0x%"XINT32"",i,j,
		//    (int32_t)winners1[i*MAX_QUERY_TERMS+j],
		//    (int32_t)winners1[i*MAX_QUERY_TERMS+j]);
	}
	}
	*/
}

char *getHashGroupString ( unsigned char hg ) {
	if ( hg == HASHGROUP_BODY ) return "body";
	if ( hg == HASHGROUP_TITLE ) return "title";
	if ( hg == HASHGROUP_HEADING ) return "header";
	if ( hg == HASHGROUP_INLIST ) return "in list";
	if ( hg == HASHGROUP_INMETATAG ) return "meta tag";
	//if ( hg == HASHGROUP_INLINKTEXT ) return "offsite inlink text";
	if ( hg == HASHGROUP_INLINKTEXT ) return "inlink text";
	if ( hg == HASHGROUP_INTAG ) return "tag";
	if ( hg == HASHGROUP_NEIGHBORHOOD ) return "neighborhood";
	if ( hg == HASHGROUP_INTERNALINLINKTEXT) return "onsite inlink text";
	if ( hg == HASHGROUP_INURL ) return "in url";
	if ( hg == HASHGROUP_INMENU ) return "in menu";
	return "unknown!";
	char *xx=NULL;*xx=0; 
	return NULL;
}


///////////////
//
// the SECOND intersect routine to avoid merging rdblists
//
////////////////

/*
// . these lists[] are 1-1 with q->m_qterms
void PosdbTable::intersectLists9_r ( ) {

	//int32_t numGroups = m_msg2->getNumListGroups();
	// fill up listGroup[]
	//RdbList **listGroup    [MAX_QUERY_TERMS];
	//int32_t      numSubLists  [MAX_QUERY_TERMS];
	//for ( int32_t i = 0 ; i < numGroups ; i++ ) {
	//	listGroup[i] = m_msg2->getListGroup      (i);
	//	numSubLists [i] = m_msg2->getNumListsInGroup(i);
	//}

	// if we are just a sitehash:xxxxx list and m_getSectionStats is
	// true then assume the list is one of hacked posdb keys where
	// the wordposition bits and others are really a 32-bit site hash
	// and we have to see how many different docids and sites have
	// this term. and we compare to our site hash, 
	// m_r->m_sectionSiteHash32 to determine if the posdb key is
	// onsite or offsite. then XmlDoc::printRainbowSections()
	// can print out how many page/sites duplicate your section's content.
	if ( m_r->m_getSectionStats ) {
		// reset
		m_sectionStats.m_onSiteDocIds  = 0;
		m_sectionStats.m_offSiteDocIds = 0;
		m_dt.clear();
		// scan the posdb keys
		//for ( int32_t i = 0 ; i < m_msg2->getNumListsInGroup(0); i++) {
		// get the sublist
		RdbList *list = m_msg2->getList(0);//Group(0)[i];
		char *p    =     list->getList    ();
		char *pend = p + list->getListSize();
		// test
		//int64_t final = 5663137686803656554LL;
		//final &= TERMID_MASK;
		//if ( p<pend && g_posdb.getTermId(p) == final )
		//	log("boo");
		// scan it
		for ( ; p < pend ; ) {
			// . first key is the full size
			// . uses the w,G,s,v and F bits to hold this
			int32_t sh32 = g_posdb.getSectionSentHash32 ( p );
			//int64_t d = g_posdb.getDocId(p);
			//int32_t rs = list->getRecSize(p);
			// this will not update listptrlo, watch out!
			p += list->getRecSize ( p );
			// onsite or off?
			if ( sh32 == m_r->m_siteHash32 ) 
				m_sectionStats.m_onSiteDocIds++;
			else            
				m_sectionStats.m_offSiteDocIds++;
			// unique site count
			if ( m_dt.isInTable ( &sh32 ) ) continue;
			// count it
			m_sectionStats.m_numUniqueSites++;
			// only once
			m_dt.addKey ( &sh32 );
			// log it
			//log("usite: %08"XINT32" %"INT64" rs=%"INT32"",sh32,d,rs);
			// stop if too much so we do not try to 
			// re-alloc in a thread!
			if ( m_dt.m_numSlotsUsed >= 1000000 ) break;
		}
		// and return the list of merging
		int32_t *s    = (int32_t *)m_sentHashList.getBufStart();
		int32_t *send = (int32_t *)m_sentHashList.getBufEnd();
		//if ( m_sectionStats.m_numUniqueSites == 17 ) { 
		//	log("q=%s",m_r->ptr_query);
		//	log("hey");
		//	//char *xx = NULL;*xx=0; 
		//}
		//if(!strcmp(m_r->ptr_query,"gbsectionhash:3335323672699668766"
		//	log("boo");
		int32_t *orig = s;
		for ( int32_t i = 0 ; i < m_dt.m_numSlots ; i++ ) {
			if ( ! m_dt.m_flags[i] ) continue;
			*s++ = *(int32_t *)m_dt.getKeyFromSlot(i);
			if ( s >= send ) break;
		}
		m_sentHashList.setLength((char *)s-(char *)orig);
		return;
	}



	initWeights();

	g_this = this;

	// clear, set to ECORRUPTDATA below
	m_errno = 0;

	// assume no-op
	m_t1 = 0LL;

	// set start time
	int64_t t1 = gettimeofdayInMilliseconds();

	//char *modListPtrs  [MAX_QUERY_TERMS];
	//int32_t  modListSizes [MAX_QUERY_TERMS];

	//for ( int32_t i = 0 ; i < m_numSubLists ; i++ ) 
	//	m_lists[i].checkList_r(false,false,RDB_POSDB);


	// . now swap the top 12 bytes of each list
	// . this gives a contiguous list of 6-byte score/docid entries
	//   because the first record is always 12 bytes and the rest are
	//   6 bytes (with the half bit on) due to our termid compression
	// . this makes the lists much much easier to work with, but we have
	//   to remember to swap back when done!
	//for ( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
	for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
		// count
		int64_t total = 0LL;
		// loop over each list in this group
		//for ( int32_t i = 0 ; i < m_msg2->getNumListsInGroup(k); i++ ) {
		// get the list
		//RdbList *list = m_msg2->getListGroup(k)[i];
		//RdbList *list = m_msg2->getList(k);
		RdbList *list = m_q->m_qterms[k].m_posdbListPtr;
		// skip if null
		if ( ! list ) continue;
		// skip if list is empty, too
		if ( list->isEmpty() ) continue;
		// tally
		total += list->m_listSize;
		// point to start
		char *p = list->m_list;
		// remember to swap back when done!!
		char ttt[10];
		gbmemcpy ( ttt   , p       , 12 );
		gbmemcpy ( p     , p + 12 , 6   );
		gbmemcpy ( p + 6 , ttt     , 12 );
		// point to the low "hks" bytes now
		p += 6;
		// turn half bit on
		*p |= 0x02;
		// MANGLE the list
		list->m_listSize -= 6;
		list->m_list      = p;
		// print total list sizes
		if ( ! m_debug ) continue;
		log("query: termlist #%"INT32" totalSize=%"INT64"",k,total);
	}


	// set the required groups of lists
	RdbList *requiredGroup  [ MAX_QUERY_TERMS ][ MAX_SUBLISTS ];
	// the bigram set # of each sublist
	//char     bigramSet      [ MAX_QUERY_TERMS ][ MAX_SUBLISTS ];
	// flags to indicate if bigram list should be scored higher
	char     bigramFlags    [ MAX_QUERY_TERMS ][ MAX_SUBLISTS ];
	// how many required lists total? should be <= numGroups.
	int32_t nrg = 0;

	// how many lists per required group?
	int32_t  numRequiredSubLists [ MAX_QUERY_TERMS ];
	float termFreqWeights     [ MAX_QUERY_TERMS ];

	int32_t  qtermNums        [ MAX_QUERY_TERMS ];
	int32_t  qpos             [ MAX_QUERY_TERMS ];
	int32_t  wikiPhraseIds    [ MAX_QUERY_TERMS ];

	RdbList *list = NULL;

	// these should be 1-1 with query terms

	//for ( int32_t i = 0 ; i < m_msg2->getNumListGroups() ; i++ ) {
	//for ( int32_t i = 0 ; i < m_msg2->getNumLists() ; i++ ) {
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		QueryTerm *qt = &m_q->m_qterms[i];
		if ( ! qt->m_isRequired ) continue;
		// set this stff
		QueryWord     *qw =   qt->m_qword;
		int32_t wordNum = qw - &m_q->m_qwords[0];
		qtermNums        [ nrg ] = i;
		qpos             [ nrg ] = wordNum;
		wikiPhraseIds    [ nrg ] = qw->m_wikiPhraseId;
		// count
		int32_t nn = 0;
		// also add in bigram lists
		int32_t left  = qt->m_leftPhraseTermNum;
		int32_t right = qt->m_rightPhraseTermNum;
		// terms
		QueryTerm *leftTerm  = qt->m_leftPhraseTerm;
		QueryTerm *rightTerm = qt->m_rightPhraseTerm;
		bool leftAlreadyAdded = false;
		bool rightAlreadyAdded = false;
		//int64_t totalTermFreq = 0;
		//int64_t *tfreqs = (int64_t *)m_r->ptr_termFreqs;
		//
		// add the non-bigram list AFTER the
		// bigrams, which we like to do when we PREFER the bigram
		// terms because they are scored higher, specifically, as
		// in the case of being half stop wikipedia phrases like
		// "the tigers" for the query 'the tigers' we want to give
		// a slight bonus, 1.20x, for having that bigram since its
		// in wikipedia
		//

		//
		// add left bigram lists. BACKWARDS.
		//
		if ( left>=0 && leftTerm && leftTerm->m_isWikiHalfStopBigram ){
			// assume added
			leftAlreadyAdded = true;
			// get list
			//list = m_msg2->getList(left);
			list = m_q->m_qterms[left].m_posdbListPtr;
			// add list ptr into our required group
			requiredGroup[nrg][nn] = list;
			// left bigram is #2
			//bigramSet[nrg][nn] = 2;
			// special flags
			bigramFlags[nrg][nn] = BF_HALFSTOPWIKIBIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) bigramFlags[nrg][nn] |= BF_PIPED;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			for ( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != leftTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				requiredGroup[nrg][nn] = list;
				bigramFlags[nrg][nn] = BF_HALFSTOPWIKIBIGRAM;
				bigramFlags[nrg][nn] |= BF_SYNONYM;
				if (qt->m_piped)bigramFlags[nrg][nn]|=BF_PIPED;
				if ( list && list->m_listSize ) nn++;
			}

		}
		//
		// then the right bigram if also in a wiki half stop bigram
		//
		if ( right>=0 &&rightTerm &&rightTerm->m_isWikiHalfStopBigram){
			// assume added
			rightAlreadyAdded = true;
			// get list
			//list = m_msg2->getList(right);
			list = m_q->m_qterms[right].m_posdbListPtr;
			// add list ptr into our required group
			requiredGroup[nrg][nn] = list;
			// right bigram is #3
			//bigramSet[nrg][nn] = 3;
			// special flags
			bigramFlags[nrg][nn] = BF_HALFSTOPWIKIBIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) bigramFlags[nrg][nn] |= BF_PIPED;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			//for(int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != rightTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				requiredGroup[nrg][nn] = list;
				bigramFlags[nrg][nn] = BF_HALFSTOPWIKIBIGRAM;
				bigramFlags[nrg][nn] |= BF_SYNONYM;
				if (qt->m_piped)bigramFlags[nrg][nn]|=BF_PIPED;
				if ( list && list->m_listSize ) nn++;
			}

		}
		//
		// then the non-bigram termlist
		//
		// add to it. add backwards since we give precedence to
		// the first list and we want that to be the NEWEST list!
		//list = m_msg2->getList(i);
		list = m_q->m_qterms[i].m_posdbListPtr;
		// add list ptr into our required group
		requiredGroup[nrg][nn] = list;
		// how many in there?
		//int32_t count = m_msg2->getNumListsInGroup(left);
		// base term is #1
		//bigramSet[nrg][nn] = 1;
		// special flags
		bigramFlags[nrg][nn] = 0;
		// before a pipe operator?
		if ( qt->m_piped ) bigramFlags[nrg][nn] |= BF_PIPED;
		// is it a negative term?
		if ( qt->m_termSign == '-') bigramFlags[nrg][nn]|=BF_NEGATIVE; 
		// only really add if useful
		if ( list && list->m_listSize ) nn++;
		// 
		// add left bigram now if not added above
		//
		if ( left>=0 && ! leftAlreadyAdded ) {
			// get list
			//list = m_msg2->getList(left);
			list = m_q->m_qterms[left].m_posdbListPtr;
			// add list ptr into our required group
			requiredGroup[nrg][nn] = list;
			// left bigram is #2
			//bigramSet[nrg][nn] = 2;
			// special flags
			bigramFlags[nrg][nn] = 0;
			// before a pipe operator?
			if ( qt->m_piped ) bigramFlags[nrg][nn] |= BF_PIPED;
			// call it a synonym i guess
			bigramFlags[nrg][nn] |= BF_BIGRAM;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			//for( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != leftTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				requiredGroup[nrg][nn] = list;
				bigramFlags[nrg][nn] = BF_SYNONYM;
				if (qt->m_piped)bigramFlags[nrg][nn]|=BF_PIPED;
				if ( list && list->m_listSize ) nn++;
			}

		}
		// 
		// add right bigram now if not added above
		//
		if ( right>=0 && ! rightAlreadyAdded ) {
			// get list
			//list = m_msg2->getList(right);
			list = m_q->m_qterms[right].m_posdbListPtr;
			// add list ptr into our required group
			requiredGroup[nrg][nn] = list;
			// right bigram is #3
			//bigramSet[nrg][nn] = 3;
			// special flags
			bigramFlags[nrg][nn] = 0;
			// call it a synonym i guess
			bigramFlags[nrg][nn] |= BF_BIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) bigramFlags[nrg][nn] |= BF_PIPED;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			//for( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != rightTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				requiredGroup[nrg][nn] = list;
				bigramFlags[nrg][nn] = BF_SYNONYM;
				if (qt->m_piped)bigramFlags[nrg][nn]|=BF_PIPED;
				if ( list && list->m_listSize ) nn++;
			}

		}

		//
		// ADD SYNONYM TERMS
		//
		//for ( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			QueryTerm *qt2 = &m_q->m_qterms[k];
			QueryTerm *st = qt2->m_synonymOf;
			// skip if not a synonym of this term
			if ( st != qt ) continue;
			// its a synonym, add it!
			//list = m_msg2->getList(k);
			list = m_q->m_qterms[k].m_posdbListPtr;
			// add list ptr into our required group
			requiredGroup[nrg][nn] = list;
			// special flags
			bigramFlags[nrg][nn] = BF_SYNONYM;
			// before a pipe operator?
			if ( qt->m_piped ) bigramFlags[nrg][nn] |= BF_PIPED;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;
		}


		// empty implies no results!!!
		if ( nn == 0 && qt->m_termSign != '-' ) {
			//log("query: MISSING REQUIRED TERM IN QUERY!");
			return;
		}
		// store # lists in required group
		numRequiredSubLists[nrg] = nn;
		// set the term freqs for this list group/set
		termFreqWeights[nrg] = ((float *)m_r->ptr_termFreqWeights)[i];
		// crazy?
		if ( nn >= MAX_SUBLISTS ) {
			log("query: too many sublists. %"INT32" >= %"INT32"",
			    nn,(int32_t)MAX_SUBLISTS);
			return;
			char *xx=NULL; *xx=0; 
		}
		// count # required groups
		nrg++;
	}

	if ( nrg == 0 ) {
		log("query: NO REQUIRED TERMS IN QUERY1!");
		return;
	}

	// get the smallest termlist
	int64_t minListSize = 0;
	int32_t mini = -1;
	//int64_t freqs   [MAX_QUERY_TERMS];
	//float freqWeights [MAX_QUERY_TERMS];
	// hopefully no more than 100 sublists per term
	char *listEnds  [ MAX_QUERY_TERMS ][ MAX_SUBLISTS ];
	// set ptrs now i guess
	for ( int32_t i = 0 ; i < nrg ; i++ ) {
		// compute total sizes
		int64_t total = 0LL;
		// do not consider for first termlist if negative
		if ( bigramFlags[i][0] & BF_NEGATIVE ) continue;
		// add to it
		for ( int32_t q = 0 ; q < numRequiredSubLists[i] ; q++ ) {
			// add list ptr into our required group
			RdbList *list = requiredGroup[i][q];
			// set end ptr
			listEnds[i][q] = list->m_list + list->m_listSize;
			// get it
			int64_t listSize = list->getListSize();
			// add it up
			total += listSize;
		}
		// get min
		if ( total < minListSize || mini == -1 ) {
			minListSize = total;
			mini = i;
		}
		// do not include the termid 6 bytes at the top
		//float fw=getTermFreqWeight ( termFreqs[i], m_numDocsInColl );
		//float fw  = 100.0 - (size / 1000000);
		//if ( fw < 1.0 ) fw = 1.0;
		//freqWeights[i] = fw;
		// a guess!
		//freqs[i] = total;
		//freqs[i] = termFreqs[i];
	}

	// for evalSlidingWindow() function
	m_freqWeights = termFreqWeights;
	//m_freqs       = termFreqs;//freqs;
	m_qtermNums   = qtermNums;

	// if smallest required list is empty, 0 results
	if ( minListSize == 0 ) return;

	//
	// alternative algo vars
	//
	char *mptr = NULL;
	char *lastMptr = NULL;
	char *mend = NULL;
	char *mergedList    [ MAX_QUERY_TERMS ];
	char *mergedListEnd [ MAX_QUERY_TERMS ];
	char  mergedFlags   [ MAX_QUERY_TERMS ];
	bool doAlternativeAlgo = false;
	if ( minListSize > 10000000 ) doAlternativeAlgo = true;
	// turn it off for now since its way slower for
	// 'how to make a lockpick set'
	doAlternativeAlgo = false;
	// -1 means auto, otherwise we can force on or off...
	// but its default in Parms.cpp is -1
	if ( m_r->m_fastIntersection == 0 ) doAlternativeAlgo = false;
	if ( m_r->m_fastIntersection == 1 ) doAlternativeAlgo = true;
	if ( nrg <= 1 ) doAlternativeAlgo = false;
	// force on for testing
	//doAlternativeAlgo = true;
	if ( doAlternativeAlgo ) {
		mptr = m_mergeBuf.getBuf();
		mend = m_mergeBuf.getBufEnd();
	}

	//
	//
	// MERGE SYNONYM/BIGRAM LISTS FOR ALTERNATIVE INTERSECTION LOOP
	//
	//
	// time it
	int64_t startTime = 0LL;
	if ( doAlternativeAlgo ) startTime = gettimeofdayInMilliseconds();
	// . only do this if smallest list is big!
	// . if smallest list is small it's faster to do the binary jumping
	//   approach. like consider the 'url:xxx.com +the' query.
	// . loop over each group and merge them
	for ( int32_t j = 0 ; doAlternativeAlgo && j < nrg ; j++ ) {
		// int16_t cut
		int32_t numLists = numRequiredSubLists[j];
		// if only one list, skip it
		if ( numLists == 1 ) {
			mergedList   [j] = requiredGroup[j][0]->m_list;
			mergedListEnd[j] = requiredGroup[j][0]->m_listEnd;
			mergedFlags  [j] = bigramFlags  [j][0];
			continue;
		}
		// need to declare these
		char *nwp     [100];
		char *nwp12   [100];
		char *nwpEnd  [100];
		char  nwpFlags[100];
		if ( numLists >= 100 ) { char *xx=NULL;*xx=0; }
		int32_t nsub = 0;
		// set the list ptrs to merge
		for ( int32_t k = 0 ; k < numLists ; k++ ) {
			nwp     [nsub] = requiredGroup[j][k]->m_list;
			nwp12   [nsub] = requiredGroup[j][k]->m_list;
			nwpEnd  [nsub] = listEnds     [j][k];
			nwpFlags[nsub] = bigramFlags  [j][k];
			if ( ! nwp[nsub] ) continue;
			if ( nwp[nsub] == nwpEnd[nsub] ) continue;
			nsub++;
		}
		// get the min key and add into the list buf
		mergedList[j] = mptr;
		int64_t prevDocId = -1LL;

	mergeMore2:
		int32_t mink = -1;
		char minks = -1;
		for ( int32_t k = 0 ; k < nsub ; k++ ) {
			// skip if list is exhausted
			if ( ! nwp[k] ) continue;
			char ks = g_posdb.getKeySize(nwp[k]);
			// auto winner?
			if ( mink == -1 ) {
				mink  = k;
				minks = ks;
				continue;
			}
			
			// top 6 bytes (i.e. docid)
			if ( *(uint32_t *)(nwp12[k   ]+2+6) >
			     *(uint32_t *)(nwp12[mink]+2+6) )
				continue;
			if ( *(uint32_t  *)(nwp12[k   ]+2+6) <
			     *(uint32_t  *)(nwp12[mink]+2+6) )
				goto gotWinner;
			if ( *(uint16_t *)(nwp12[k   ]+6) >
			     *(uint16_t *)(nwp12[mink]+6) )
				continue;
			if ( *(uint16_t *)(nwp12[k   ]+6) <
			     *(uint16_t *)(nwp12[mink]+6) )
				goto gotWinner;
			// lower 6 bytes
			if ( *(uint32_t *)(nwp[k   ]+2) >
			     *(uint32_t *)(nwp[mink]+2) )
				continue;
			if ( *(uint32_t *)(nwp[k   ]+2) <
			     *(uint32_t *)(nwp[mink]+2) )
				goto gotWinner;
			if ( *(uint16_t *)(nwp[k   ]) >
			     *(uint16_t *)(nwp[mink]) )
				continue;
			if ( *(uint16_t *)(nwp[k   ]) <
			     *(uint16_t *)(nwp[mink]) )
				goto gotWinner;
			// otherwise, forget it! perfect tie...
			continue;
			// a new min...
		gotWinner:
			mink  = k;
			minks = ks;
		}
		// all exhausted? merge next set of lists then for term #j
		if ( mink == -1 ) {
			mergedListEnd[j] = mptr;
			mergedFlags  [j] = bigramFlags[j][0] ;
			mergedFlags  [j] &= (BF_NEGATIVE|BF_PIPED);
			continue;
		}
		// get docid
		int64_t docId = g_posdb.getDocId(nwp12[mink]);
		// if this key matches our previously stored docid, then
		// it's 6 bytes, otherwise 12
		if ( docId != prevDocId ) {
			// TODO: make this not use memset
			//gbmemcpy ( mptr , nwp[mink] , 12 );
			*(int64_t *)mptr = *(int64_t *)nwp[mink];
			*(int32_t *)(mptr+8) = *(int32_t *)(nwp[mink]+8);
			// set the synbit so we know if its a synonym of term
			if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) 
				mptr[2] |= 0x02;
			// make sure its 12 bytes! it might have been
			// the first key for the termid, and 18 bytes.
			mptr[0] &= 0xf9;
			mptr[0] |= 0x02;
			lastMptr = mptr;
			mptr += 12;
			// update this
			prevDocId = docId;
		}
		else {
			// if matches last key word position, do not add!
			// we should add the bigram first if more important
			// since it should be added before the actual term
			// above in the sublist array. so if they are
			// wikihalfstop bigrams they will be added first,
			// otherwise, they are added after the regular term.
			// should fix double scoring bug for 'cheat codes'
			// query!
			// TODO: fix this for fast intersection algo!!!
			// it does not order the nwp lists that way...
			if ( lastMptr[4] == nwp[mink][4] &&
			     lastMptr[5] == nwp[mink][5] &&
			     (lastMptr[3] & 0xc0) == (nwp[mink][3] & 0xc0) )
				goto skipOver2;
			*(int32_t  *) mptr    = *(int32_t  *) nwp[mink];
			*(int16_t *)(mptr+4) = *(int16_t *)(nwp[mink]+4);
			// set the synbit so we know if its a synonym of term
			if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) 
				mptr[2] |= 0x02;
			// if it was the first key of its list it may not
			// have its bit set for being 6 bytes now! so turn
			// on the 2 compression bits
			mptr[0] |= 0x06;
			// save it
			lastMptr = mptr;
			mptr += 6;
		}
	skipOver2:
		// advace the cursor over the key we used.
		nwp[mink] += minks; // g_posdb.getKeySize(nwp[mink]);
		// now continue to advance this list's ptr until it points
		// to a docid in our previous list, if any
		// exhausted? then set to NULL
		if ( nwp[mink] >= nwpEnd[mink] ) {
			nwp[mink] = NULL;
			goto mergeMore2;
		}
		// get new keysize
		char ks = g_posdb.getKeySize(nwp[mink]);
		// only advance this to a 12 byte key
		if ( ks == 12 ) nwp12[mink] = nwp[mink];
		// merge some more now
		goto mergeMore2;
	}
	// breach?
	if ( doAlternativeAlgo ) {
		// sanity check
		if ( mptr > mend ) { char *xx=NULL;*xx=0; }
		int64_t endTime = gettimeofdayInMilliseconds();
		int64_t took = endTime - startTime;
		log("posdb: synlist merge took %"INT64" ms", took);
	}
	//
	//
	// END MERGE SYNONYM/BIGRAM LISTS FOR ALTERNATIVE INTERSECTION LOOP
	//
	//



	bool secondPass = false;
	DocIdScore dcs;
	DocIdScore *pdcs;
	int32_t minx =0;
	bool allNull;
	int32_t minPos =0;

	//////////
	//
	// MAIN INTERSECTION LOGIC
	//
	/////////

	int64_t lastDocId = 0LL;
	int32_t lastLen = 0;
	char siteRank =0;
	char docLang =0;
	float score;
	float minScore;
	float minPairScore;
	float minSingleScore;
	int64_t docId;

	char *ptrs  [MAX_QUERY_TERMS];
	char *ends  [MAX_QUERY_TERMS];
	char  bflags[MAX_QUERY_TERMS];

	m_bflags = bflags;

	int32_t qdist;
	float wts;
	float pss;
	float scoreMatrix[MAX_QUERY_TERMS*MAX_QUERY_TERMS];
	char *bestPos[MAX_QUERY_TERMS];
	float maxNonBodyScore;
	//int32_t  nr2;

	//char *winnerStack1[MAX_QUERY_TERMS * MAX_QUERY_TERMS];
	//char *winnerStack2[MAX_QUERY_TERMS * MAX_QUERY_TERMS];
	//float  scoreStack [MAX_QUERY_TERMS * MAX_QUERY_TERMS];
	char *winnerStack[MAX_QUERY_TERMS];

	int64_t prevDocId = 0LL;
	// scan the posdb keys in the smallest list
	int32_t minddd =0;
	char *saved;
	int64_t dtmp;
	// raised from 200 to 300,000 for 'da da da' query
	char mbuf[300000];
	char *mptrEnd = mbuf + 299000;
	//char *mptr;
	//char *lastMptr = NULL;
	// assume no more than 100 sublists in a group
	char *dp[MAX_SUBLISTS];

	// save these for alternative loop algo
	char *origList[100];
	for ( int32_t i = 0 ; doAlternativeAlgo && i < nrg ; i++ ) 
		// reset mergedlist ptrs
		origList[i] = mergedList[i];


 secondPassLoop:

	// reset these
	prevDocId = 0LL;
	minddd =0;
	lastMptr = NULL;

	// initialize dp[] to point to each sublist in group #mini
	for ( int32_t i = 0 ; i < numRequiredSubLists[mini] ; i++ ) 
		dp[i] = requiredGroup[mini][i]->m_list;

	// and for alternative loop as well
	// how many terms must we have?
	int32_t needToMatch = 0;
	for ( int32_t i = 0 ; doAlternativeAlgo && i < nrg ; i++ ) {
		// reset mergedlist ptrs
		mergedList[i] = origList[i];
		if ( mergedFlags[i] & BF_NEGATIVE ) continue;
		needToMatch++;
	}



 docIdLoop:

	//
	//
	// ALTERNATIVE INTERSECTION LOOP
	//
	//
	// do not use the binary stepping, just straight up merge intersection.
	// hopefully faster when smallest termlist is quite big.
	//
	// get current docid
	if ( doAlternativeAlgo ) {
	altIntersectionLoop:
		// reset match count, we match term in "mini" list
		int32_t match ;
		// get current docid
		int64_t currentDocId = g_posdb.getDocId(mergedList[mini]);
		// if second pass, must be in the hashtable
		if ( secondPass && ! m_docIdTable.isInTable(&currentDocId) ) {
			match = 0;
			goto skipMinList;
		}
		// otherwise, we got one match, the term in the min list
		match = 1;
		// increment the other lists so their docid >= this
		for ( int32_t i = 0 ; i < nrg ; i++ ) {
			// skip if mini
			if ( i == mini ) continue;
			// advance until >= current docid
			for (; ; ) {
				// stop if >=
				if ( g_posdb.getDocId(mergedList[i]) >=
				     currentDocId )
					break;
				// get key size
				char mks = g_posdb.getKeySize(mergedList[i]);
				// loop back here for 6 byte keys
			littleLoop:
				// advance it
				mergedList[i] += mks;
				// stop if done
				if ( mergedList[i] >= mergedListEnd[i] )
					goto done;
				// if next key is 6 bytes, skip it, since
				// it has no docid!!
				mks = g_posdb.getKeySize(mergedList[i]);
				// 6 bytes?
				if ( mks == 6 ) goto littleLoop;
			}
			// equals docid?
			if ( g_posdb.getDocId(mergedList[i]) == currentDocId ){
				// negative term? very bad!
				if ( mergedFlags[i] & BF_NEGATIVE ) match=-100;
				else match++;
			}
		}
		// got a match? then set up the ptrs for jumping down below
		for ( int32_t i = 0 ; match == needToMatch && i < nrg ; i++ ) {
			ptrs   [i] = mergedList   [i];
			bflags [i] = mergedFlags  [i];
			ends   [i] = mergedListEnd[i];
			docId = currentDocId;
			// debug
			//log("posdb: matched d=%"INT64"",currentDocId);
		}
	skipMinList:
		// advance mini's docid
		mergedList[mini] += 12;
		// skip following 6 byte keys
		for ( ; ; ) {
			// stop on breach
			if ( mergedList[mini] >= mergedListEnd[mini] )
				break;
			// stop if 12 byte key
			char mks = g_posdb.getKeySize(mergedList[mini]);
			// stop if 12
			if ( mks == 12 ) break;
			// it's 6 byte, skip it
			mergedList[mini] += 6;
		}
		// do the jump after we've incremented mini's list ptr
		if ( match == needToMatch )
			goto jumpDownHere;
		// all done?
		if ( mergedList[mini] >= mergedListEnd[mini] ) goto done;
		// otherwise try the next docid in mini's list
		goto altIntersectionLoop;
	}
	//
	//
	// END ALTERNATIVE INTERSECTION LOOP
	//
	//

	// merge bigram list with the regular list. that way 'streetlight'
	// can sub in for the word 'street' for the query 'street light facts'
	// or we could just assume there are TWO lists for every term here
	// and keep a ptr1 and ptr2 for the two lists.


	// assume all sublists exhausted for this query term
	docId = MAX_DOCID;

	// find the next docid from all the sublists
	for ( int32_t i = 0 ; i < numRequiredSubLists[mini] ; i++ ) {
		// get the next docid
		if ( dp[i] >= listEnds[mini][i] ) continue;
		// get docid from this sublist
		dtmp = g_posdb.getDocId(dp[i]);
		// take the rec from the last list in the event
		// the docid is equal, because that is the most recent list
		// according to Msg5::gotList2() function.
		// for 'cheat' termlist for 'cheat codes' query we need
		// to take 'cheat' for that docid from the tree list, not
		// an older list, so this MUST be >=, not >
		if ( dtmp >= docId ) continue;
		//
		// no negative keys allowed!!
		//
		docId  = dtmp;
		minddd = i;
	}

	// sanity
	if ( minddd >= MAX_SUBLISTS ) { char *xx=NULL;*xx=0; }

	// all sublists exhausted for the smallest required termlist?
	if ( docId == MAX_DOCID ) goto done;

	// save it
	saved = dp[minddd];

	//if ( docId == 20622118994LL )
	//	log("hey");

	// advance to next docid in this sublist if we are a positive key
	dp[minddd] = g_posdb.getNextDocIdSublist ( dp[minddd]         ,
						   listEnds[mini][minddd] );

	// if same as last docid, just advance and be done with it
	if ( docId == prevDocId ) 
		goto docIdLoop;

	// udpate this
	prevDocId = docId;


	// . second pass? for printing out transparency info
	// . docidtable should be pre-allocated in allocTopTree() function
	if ( secondPass && ! m_docIdTable.isInTable ( &docId ) )
		goto docIdLoop;

	// debug
	//if ( docId != 238314041956LL )
	//	goto docIdLoop;

	// debug
	//if ( docId == 20622118994LL ) 
	//	log("hey");


	//
	// PERFORMANCE HACK:
	//
	// ON-DEMAND MINI MERGES.
	//
	// we got a docid that has all the query terms, so merge
	// each term's sublists into a single list just for this docid.
	//
	// all posdb keys for this docid should fit in here, the merge buf:
	// currently it is 10k
	mptr = mbuf;
	// merge each list set
	for ( int32_t j = 0 ; j < nrg ; j++ ) {
		// num sublists to merge
		int32_t nsub = 0;
		char *nwp   [100];
		char *nwpEnd[100];
		char  nwpFlags[100];
		// scan the sublists
		for ( int32_t k = 0 ; k < numRequiredSubLists[j] ; k++ ) {
			// . get sublist ptr
			// . get docid sublist of list #k for query term #j
			nwp[nsub] = getWordPosList ( docId , 
					      requiredGroup[j][k]->m_list,
					      requiredGroup[j][k]->m_listSize);
			// this is null if this list does not have this docid
			if ( ! nwp[nsub] ) continue;
			// save i guess
			nwpEnd[nsub] = listEnds[j][k];
			// and this
			nwpFlags[nsub] = bigramFlags[j][k];
			// print out
			if ( ! m_debug ) continue;
			char *xx = nwp[nsub];
			char *xxend = nwpEnd[nsub];
			char ks;
			for ( ; xx < xxend ; xx += ks ) {
				if ( xx>nwp[nsub]&&g_posdb.getKeySize(xx) != 6)
					break;
				ks = g_posdb.getKeySize(xx);
				char hgx = g_posdb.getHashGroup(xx);
				int32_t pos = g_posdb.getWordPos(xx);
				logf(LOG_DEBUG,
				     "posdb: premerge k=%"INT32" j=%"INT32",nsub=%"INT32" "
				    "hg=%s pos=%"INT32"",k,j,nsub,
				    getHashGroupString(hgx),pos);
			}	

			// this docid has this term, yay
			nsub++;
		}
		// if we have a negative term, that is a show stopper
		if ( bigramFlags[j][0] & BF_NEGATIVE ) {
			// if its empty, that's good!
			if ( nsub == 0 ) {
				// need this
				ptrs  [j] = NULL;
				ends  [j] = NULL;
				bflags[j] = BF_NEGATIVE;//nwpFlags[0];
				continue;
			}
			// otherwise, forget it!
			goto docIdLoop;
		}
		// . does this docid have term # j?
		// . if not there then go to next docid
		if ( nsub == 0 ) goto docIdLoop;
		// if only one list, no need to merge
		// UNLESS it's a synonym list then we gotta set the
		// synbits on it, below!!!
		if ( nsub == 1 && !(nwpFlags[0] & BF_SYNONYM)) {
			ptrs  [j] = nwp   [0];
			ends  [j] = nwpEnd[0];
			bflags[j] = nwpFlags[0];
			continue;
		}
		// init merge
		ptrs  [j] = mptr;
		// just use the flags from first term i guess
		bflags[j] = nwpFlags[0];
		bool isFirstKey = true;
		// . ok, merge the lists into a list in mbuf
		// . get the min of each list
	mergeMore:
		int32_t mink = -1;
		for ( int32_t k = 0 ; k < nsub ; k++ ) {
			// skip if list is exhausted
			if ( ! nwp[k] ) continue;
			// auto winner?
			if ( mink == -1 ) {
				mink = k;
				continue;
			}
			if ( *(uint32_t *)(nwp[k   ]+2) >
			     *(uint32_t *)(nwp[mink]+2) )
				continue;
			if ( *(uint32_t  *)(nwp[k   ]+2) ==
			     *(uint32_t  *)(nwp[mink]+2) &&
			     *(uint16_t *)(nwp[k   ]) >=
			     *(uint16_t *)(nwp[mink]) )
				continue;
			// a new min...
			mink = k;
		}
		// all exhausted? merge next set of lists then for term #j
		if ( mink == -1 ) {
			ends[j] = mptr;
			continue;
		}
		// get keysize
		//char ks = g_posdb.getKeySize(nwp[mink]);
		// if the first key in our merged list store the docid crap
		if ( isFirstKey ) {
			gbmemcpy ( mptr , nwp[mink] , 12 );
			// sanity check! make sure these not being used...
			//if ( mptr[2] & 0x03 ) { char *xx=NULL;*xx=0; }
			// wipe out its syn bits and re-use our way
			mptr[2] &= 0xfc;
			// set the synbit so we know if its a synonym of term
			if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) 
				mptr[2] |= 0x02;
			// wiki half stop bigram? so for the query
			// 'time enough for love' the phrase term "enough for"
			// is a half stopword wiki bigram, because it is in
			// a phrase in wikipedia ("time enough for love") and
			// one of the two words in the phrase term is a 
			// stop word. therefore we give it more weight than
			// just 'enough' by itself.
			if ( nwpFlags[mink] & BF_HALFSTOPWIKIBIGRAM )
				mptr[2] |= 0x01;
			// make sure its 12 bytes! it might have been
			// the first key for the termid, and 18 bytes.
			mptr[0] &= 0xf9;
			mptr[0] |= 0x02;
			// show hg
			//char hgx = g_posdb.getHashGroup(mptr);
			//int32_t pos = g_posdb.getWordPos(mptr);
			//log("j=%"INT32" mink=%"INT32" hgx=%"INT32" pos=%"INT32"",j,mink,hgx,pos);
			lastMptr = mptr;
			mptr += 12;
			isFirstKey = false;
		}
		else {
			// if matches last key word position, do not add!
			// we should add the bigram first if more important
			// since it should be added before the actual term
			// above in the sublist array. so if they are
			// wikihalfstop bigrams they will be added first,
			// otherwise, they are added after the regular term.
			// should fix double scoring bug for 'cheat codes'
			// query!
			if ( lastMptr[4] == nwp[mink][4] &&
			     lastMptr[5] == nwp[mink][5] &&
			     (lastMptr[3] & 0xc0) == (nwp[mink][3] & 0xc0) )
				goto skipOver;
			*(int32_t  *) mptr    = *(int32_t  *) nwp[mink];
			*(int16_t *)(mptr+4) = *(int16_t *)(nwp[mink]+4);
			// wipe out its syn bits and re-use our way
			mptr[2] &= 0xfc;
			// set the synbit so we know if its a synonym of term
			if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) 
				mptr[2] |= 0x02;
			if ( nwpFlags[mink] & BF_HALFSTOPWIKIBIGRAM )
				mptr[2] |= 0x01;
			// if it was the first key of its list it may not
			// have its bit set for being 6 bytes now! so turn
			// on the 2 compression bits
			mptr[0] |= 0x06;
			// show hg
			//char hgx = g_posdb.getHashGroup(mptr);
			//int32_t pos = g_posdb.getWordPos(mptr);
			//log("j=%"INT32" mink=%"INT32" hgx=%"INT32" pos=%"INT32"",j,mink,hgx,pos);
			//if ( pos == 8949 ) { // 73779 ) {
			//	char *xx=NULL;*xx=0; }
			// save it
			lastMptr = mptr;
			mptr += 6;
		}
	skipOver:
		// advace the cursor over the key we used.
		if ( (nwp[mink][0]) & 0x04 ) nwp[mink] += 6;
		else                         nwp[mink] += 12;
		//nwp[mink] += ks; // g_posdb.getKeySize(nwp[mink]);
		// exhausted?
		if ( nwp[mink] >= nwpEnd[mink] ) 
			nwp[mink] = NULL;
		// or hit a different docid
		else if ( g_posdb.getKeySize(nwp[mink]) != 6 )
			nwp[mink] = NULL;
		// avoid breach of core below now
		if ( mptr < mptrEnd ) goto mergeMore;
	}

	// breach?
	if ( mptr > mbuf + 300000 ) { char *xx=NULL;*xx=0; }

 jumpDownHere:

	// clear the counts on this DocIdScore class for this new docid
	pdcs = NULL;
	if ( secondPass ) {
		dcs.reset();
		pdcs = &dcs;
	}

	//
	//
	// NON-BODY TERM PAIR SCORING LOOP
	//
	//
	for ( int32_t i = 0   ; i < nrg ; i++ ) {

	// skip if not part of score
	if ( bflags[i] & (BF_PIPED|BF_NEGATIVE) ) continue;

	for ( int32_t j = i+1 ; j < nrg ; j++ ) {
		// skip if not part of score
		if ( bflags[j] & (BF_PIPED|BF_NEGATIVE) ) continue;
		// but if they are in the same wikipedia phrase
		// then try to keep their positions as in the query.
		// so for 'time enough for love' ideally we want
		// 'time' to be 6 units apart from 'love'
		if ( wikiPhraseIds[j] == wikiPhraseIds[i] &&
		     // zero means not in a phrase
		     wikiPhraseIds[j] ) {
			qdist = qpos[j] - qpos[i];
			// wiki weight
			wts = (float)WIKI_WEIGHT; // .50;
		}
		else {
			// basically try to get query words as close
			// together as possible
			qdist = 2;
			// this should help fix
			// 'what is an unsecured loan' so we are more likely
			// to get the page that has that exact phrase in it.
			// yes, but hurts how to make a lock pick set.
			//qdist = qpos[j] - qpos[i];
			// wiki weight
			wts = 1.0;
		}
		pss = 0.0;
		//
		// get score for term pair from non-body occuring terms
		//
		if ( ptrs[i] && ptrs[j] )
			getTermPairScoreForNonBody(i,
						   j,
						   ptrs[i],
						   ptrs[j],
						   ends[i],
						   ends[j],
						   qdist ,
						   &pss);
		// it's -1 if one term is in the body/header/menu/etc.
		if ( pss < 0 ) {
			scoreMatrix[i*MAX_QUERY_TERMS+j] = -1.00;
			wts = -1.0;
		}
		else {
			wts *= pss;
			wts *= m_freqWeights[i];//sfw[i];
			wts *= m_freqWeights[j];//sfw[j];
			// store in matrix for "sub out" algo below
			// when doing sliding window
			scoreMatrix[i*MAX_QUERY_TERMS+j] = wts;
			// if terms is a special wiki half stop bigram
			//if ( bflags[i] == 1 ) wts *= WIKI_BIGRAM_WEIGHT;
			//if ( bflags[j] == 1 ) wts *= WIKI_BIGRAM_WEIGHT;
			//if ( ts < minScore ) minScore = ts;
		}
	}
	}

	//
	//
	// SINGLE TERM SCORE LOOP
	//
	//
	maxNonBodyScore = -2.0;
	minSingleScore = 999999999.0;
	// . now add single word scores
	// . having inlink text from siterank 15 of max 
	//   diversity/density will result in the largest score, 
	//   but we add them all up...
	// . this should be highly negative if singles[i] has a '-' 
	//   termsign...
	for ( int32_t i = 0 ; i < nrg ; i++ ) {
		float sts;
		// skip if to the left of a pipe operator
		if ( bflags[i] & (BF_PIPED|BF_NEGATIVE) ) continue;
		// sometimes there is no wordpos subtermlist for this docid
		// because it just has the bigram, like "streetlight" and not
		// the word "light" by itself for the query 'street light'
		//if ( ptrs[i] ) {
		// assume all word positions are in body
		//bestPos[i] = NULL;
		// . this scans all word positions for this term
		// . sets "bestPos" to point to the winning word 
		//   position which does NOT occur in the body
		// . adds up MAX_TOP top scores and returns that sum
		// . pdcs is NULL if not secondPass
		sts = getSingleTermScore (i,ptrs[i],ends[i],pdcs,
					  &bestPos[i]);
		// sanity check
		if ( bestPos[i] &&
		     s_inBody[g_posdb.getHashGroup(bestPos[i])] ) {
			char *xx=NULL;*xx=0; }
		//sts /= 3.0;
		if ( sts < minSingleScore ) minSingleScore = sts;
	}

	//
	// . multiplier from siterank i guess
	// . ptrs[0] list can be null if it does not have 'street' but
	//   has 'streetlight' for the query 'street light'
	//
	if ( ptrs[0] ) {
		siteRank = g_posdb.getSiteRank ( ptrs[0] );
		docLang  = g_posdb.getLangId   ( ptrs[0] );
	}
	else {
		for ( int32_t k = 1 ; k < nrg ; k++ ) {
			if ( ! ptrs[k] ) continue;
			siteRank = g_posdb.getSiteRank ( ptrs[k] );
			docLang  = g_posdb.getLangId   ( ptrs[k] );
			break;
		}
	}
	
	//
	// parms for sliding window algorithm
	//
	m_qpos          = qpos;
	m_wikiPhraseIds = wikiPhraseIds;
	//if ( secondPass ) m_ds = &dcs;
	//else              m_ds = NULL;
	//m_sfw = sfw;
	m_bestWindowScore = -2.0;

	//
	//
	// BEGIN SLIDING WINDOW ALGO
	//
	//

	//m_finalWinners1 = winnerStack1;
	//m_finalWinners2 = winnerStack2;
	//m_finalScores   = scoreStack;
	m_windowTermPtrs = winnerStack;

	// . now scan the terms that are in the body in a sliding window
	// . compute the term pair score on just the terms in that
	//   sliding window. that way, the term for a word like 'dog'
	//   keeps the same word position when it is paired up with the
	//   other terms.
	// . compute the score the same way getTermPairScore() works so
	//   we are on the same playing field
	// . sub-out each term with its best scoring occurence in the title
	//   or link text or meta tag, etc. but it gets a distance penalty
	//   of like 100 units or so.
	// . if term does not occur in the body, the sub-out approach should
	//   fix that.
	// . keep a matrix of the best scores between two terms from the
	//   above double nested loop algo. and replace that pair if we
	//   got a better score in the sliding window.

	// use special ptrs for the windows so we do not mangle ptrs[]
	// array because we use that below!
	char *xpos[MAX_QUERY_TERMS];
	for ( int32_t i = 0 ; i < nrg ; i++ ) xpos[i] = ptrs[i];

	allNull = true;
	//
	// init each list ptr to the first wordpos rec in the body
	// and if no such rec, make it NULL
	//
	for ( int32_t i = 0 ; i < nrg ; i++ ) {
		// skip if to the left of a pipe operator
		if ( bflags[i] & (BF_PIPED|BF_NEGATIVE) ) continue;
		// skip wordposition until it in the body
		while ( xpos[i] &&!s_inBody[g_posdb.getHashGroup(xpos[i])]) {
			// advance
			if (   (xpos[i][0] & 0x04) ) xpos[i] += 6;
			else                         xpos[i] += 12;
			// NULLify list if no more for this docid
			if (xpos[i] < ends[i] && (xpos[i][0] & 0x04)) continue;
			// ok, no more! null means empty list
			xpos[i] = NULL;
			// must be in title or something else then
			if ( ! bestPos[i] ) { char *xx=NULL;*xx=0; }
		}
		// if all xpos are NULL, then no terms are in body...
		if ( xpos[i] ) allNull = false;
	}

	// if no terms in body, no need to do sliding window
	if ( allNull ) goto doneSliding;

	minx = -1;

 slideMore:

	// . now all xpos are in the body
	// . calc the window score
	// . if window score beats m_bestWindowScore we store the
	//   term xpos that define this window in m_windowTermPtrs[] array
	// . will try to sub in s_bestPos[i] if better, but will fix 
	//   distance to FIXED_DISTANCE
	// . "minx" is who just got advanced, this saves time because we
	//   do not have to re-compute the scores of term pairs that consist
	//   of two terms that did not advance in the sliding window
	// . "scoreMatrix" hold the highest scoring non-body term pair
	//   for sub-bing out the term pair in the body with
	// . sets m_bestWindowScore if this window score beats it
	// . does sub-outs with the non-body pairs and also the singles i guess
	// . uses "bestPos[x]" to get best non-body scoring term for sub-outs
	evalSlidingWindow ( xpos , 
			    nrg , 
			    bestPos , 
			    scoreMatrix , 
			    minx );


 advanceMin:
	// now find the min word pos still in body
	minx = -1;
	for ( int32_t x = 0 ; x < nrg ; x++ ) {
		// skip if to the left of a pipe operator
		if ( bflags[x] & (BF_PIPED|BF_NEGATIVE) ) continue;
		if ( ! xpos[x] ) continue;
		if ( xpos[x] && minx == -1 ) {
			minx = x;
			//minRec = xpos[x];
			minPos = g_posdb.getWordPos(xpos[x]);
			continue;
		}
		if ( g_posdb.getWordPos(xpos[x]) >= minPos ) 
			continue;
		minx = x;
		//minRec = xpos[x];
		minPos = g_posdb.getWordPos(xpos[x]);
	}
	// sanity
	if ( minx < 0 ) { char *xx=NULL;*xx=0; }

 advanceAgain:
	// now advance that to slide our window
	if (   (xpos[minx][0] & 0x04) ) xpos[minx] +=  6;
	else                            xpos[minx] += 12;
	// NULLify list if no more for this docid
	if ( xpos[minx] >= ends[minx] || ! (xpos[minx][0] & 0x04) ) {
		// exhausted list now
		xpos[minx] = NULL;
		// are all null now?
		int32_t k; 
		for ( k = 0 ; k < nrg ; k++ ) {
			// skip if to the left of a pipe operator
			if ( bflags[k] & (BF_PIPED|BF_NEGATIVE) ) continue;
			if ( xpos[k] ) break;
		}
		// all lists are now exhausted
		if ( k >= nrg ) goto doneSliding;
		// ok, now recompute the next min and advance him
		goto advanceMin;
	}
	// if it left the body then advance some more i guess?
	if ( ! s_inBody[g_posdb.getHashGroup(xpos[minx])] ) 
		goto advanceAgain;

	// do more!
	goto slideMore;


	//
	//
	// END SLIDING WINDOW ALGO
	//
	//

 doneSliding:

	minPairScore = -1.0;

	// for debug
	m_docId = docId;

	//
	//
	// BEGIN ZAK'S ALGO, BUT RESTRICT ALL BODY TERMS TO SLIDING WINDOW
	//
	//
	// (similar to NON-BODY TERM PAIR SCORING LOOP above)
	//
	for ( int32_t i = 0   ; i < nrg ; i++ ) {

	// skip if to the left of a pipe operator
	if ( bflags[i] & (BF_PIPED|BF_NEGATIVE) ) continue;

	for ( int32_t j = i+1 ; j < nrg ; j++ ) {

		// skip if to the left of a pipe operator
		if ( bflags[j] & (BF_PIPED|BF_NEGATIVE) ) continue;

		//
		// get score for term pair from non-body occuring terms
		//
		if ( ! ptrs[i] ) continue;
		if ( ! ptrs[j] ) continue;
		// . this will skip over body terms that are not 
		//   in the winning window defined by m_windowTermPtrs[]
		//   that we set in evalSlidingWindow()
		// . returns the best score for this term
		float score = getTermPairScoreForAny (i,
						      j,
						      ptrs[i],
						      ptrs[j],
						      ends[i],
						      ends[j],
						      pdcs );
		// get min of all term pair scores
		if ( score >= minPairScore && minPairScore >= 0.0 ) continue;
		// got a new min
		minPairScore = score;
	}
	}
	//
	//
	// END ZAK'S ALGO
	//
	//


	minScore = 999999999.0;
			
	// get a min score from all the term pairs
	if ( minPairScore < minScore && minPairScore >= 0.0 )
		minScore = minPairScore;

	// if we only had one query term
	if ( minSingleScore < minScore )
		minScore = minSingleScore;

	// try dividing it by 3! (or multiply by .33333 faster)
	score = minScore * (((float)siteRank)*SITERANKMULTIPLIER+1.0);

	// . not foreign language? give a huge boost
	// . use "qlang" parm to set the language. i.e. "&qlang=fr"
	if ( m_r->m_language == 0 || 
	     docLang == 0 ||
	     m_r->m_language == docLang)
		score *= SAMELANGMULT;

	// if doing the second pass for printint out transparency info
	// then do not mess with top tree
	if ( ! secondPass ) {
		// add to top tree then!
		int32_t tn = m_topTree->getEmptyNode();
		TopNode *t  = &m_topTree->m_nodes[tn];
		// set the score and docid ptr
		t->m_score = score;
		t->m_docId = docId;
		// . this will not add if tree is full and it is less than the 
		//   m_lowNode in score
		// . if it does get added to a full tree, lowNode will be 
		//   removed
		m_topTree->addNode ( t, tn);
		goto docIdLoop;
	}

	//////////
	//
	// ok, add the scoring info since we are on the second pass and this
	// docid is in the top tree
	//
	//////////

	dcs.m_siteRank   = siteRank;
	dcs.m_finalScore = score;
	dcs.m_docId      = docId;
	dcs.m_numRequiredTerms = nrg;
	// ensure enough room we can't allocate in a thread!
	if ( m_scoreInfoBuf.getAvail() < (int32_t)sizeof(DocIdScore)+1) { 
		char *xx=NULL;*xx=0; }
	// if same as last docid, overwrite it since we have a higher
	// siterank or langid i guess
	if ( docId == lastDocId ) 
		m_scoreInfoBuf.m_length = lastLen;
	// save that
	lastLen = m_scoreInfoBuf.m_length;
	// copy into the safebuf for holding the scoring info
	m_scoreInfoBuf.safeMemcpy ( (char *)&dcs, sizeof(DocIdScore) );
	// save it
	lastDocId = docId;
	
	// try to fix dup docid problem! it was hitting the
	// overflow check right above here... strange!!!
	//m_docIdTable.removeKey ( &docId );

	// advance to next docid
	//p = pend;
	// if not of end list loop back up
	//if ( p < listEnd ) goto bigloop;
	goto docIdLoop;

 done:
	// now repeat the above loop, but with m_dt hashtable
	// non-NULL and include all the docids in the toptree, and
	// for each of those docids store the transparency info in english
	// into the safebuf "transBuf".
	if ( ! secondPass && m_r->m_getDocIdScoringInfo ) {
		// only do one second pass
		secondPass = true;
		int32_t count = 0;
		// clear it in case still set from previous docid range split
		// logic, which i added to prevent oom conditions
		m_docIdTable.clear();
		// stock m_docIdTable
		for ( int32_t ti = m_topTree->getHighNode() ; 
		      ti >= 0 ; ti = m_topTree->getPrev(ti) ) {
			// get the guy
			TopNode *t = &m_topTree->m_nodes[ti];
			// limit to max!
			if ( count++ >= m_maxScores ) break;
			// now 
			m_docIdTable.addKey(&t->m_docId);
		}
		goto secondPassLoop;
	}

	// get time now
	int64_t now = gettimeofdayInMilliseconds();
	// store the addLists time
	m_addListsTime = now - t1;
	m_t1 = t1;
	m_t2 = now;
}
*/

float PosdbTable::getSingleTermScore ( int32_t i,
				       char *wpi , 
				       char *endi ,
				       DocIdScore *pdcs,
				       //SingleScore *ss ,
				       char **bestPos ) {

	float nonBodyMax = -1.0;
	//char *maxp;
	bool first = true;
	int32_t minx;
	float bestScores[MAX_TOP];
	char *bestwpi   [MAX_TOP];
	char  bestmhg   [MAX_TOP];
	int32_t numTop = 0;

	// assume no terms!
	*bestPos = NULL;

	// empty list? no terms!
	if ( ! wpi ) goto done;

	float score;
	unsigned char hg;
	unsigned char mhg;
	unsigned char dens;
	unsigned char wspam;
	unsigned char div;
	int32_t bro;

 loop:
	score = 100.0;
	// good diversity?
	div = g_posdb.getDiversityRank ( wpi );
	score *= s_diversityWeights[div];
	score *= s_diversityWeights[div];

	// hash group? title? body? heading? etc.
	hg = g_posdb.getHashGroup ( wpi );
	mhg = hg;
	if ( s_inBody[mhg] ) mhg = HASHGROUP_BODY;
	score *= s_hashGroupWeights[hg];
	score *= s_hashGroupWeights[hg];

	// good density?
	dens = g_posdb.getDensityRank ( wpi );
	score *= s_densityWeights[dens];
	score *= s_densityWeights[dens];

	// to make more compatible with pair scores divide by distance of 2
	//score /= 2.0;

	// word spam?
	wspam = g_posdb.getWordSpamRank ( wpi );
	// word spam weight update
	if ( hg == HASHGROUP_INLINKTEXT ) {
		score *= s_linkerWeights  [wspam];
		score *= s_linkerWeights  [wspam];
	}
	else {
		score *= s_wordSpamWeights[wspam];
		score *= s_wordSpamWeights[wspam];
	}

	// synonym
	if ( g_posdb.getIsSynonym(wpi) ) {
		score *= SYNONYM_WEIGHT;
		score *= SYNONYM_WEIGHT;
	}


	// do not allow duplicate hashgroups!
	bro = -1;
	for ( int32_t k = 0 ; k < numTop ; k++ ) {
		if ( bestmhg[k] == mhg && hg !=HASHGROUP_INLINKTEXT ){
			bro = k;
			break;
		}
	}
	if ( bro >= 0 ) {
		if ( score > bestScores[bro] ) {
			bestScores[bro] = score;
			bestwpi   [bro] = wpi;
			bestmhg   [bro] = mhg;
		}
	}
	// best?
	else if ( numTop < m_realMaxTop ) { // MAX_TOP ) {
		bestScores[numTop] = score;
		bestwpi   [numTop] = wpi;
		bestmhg   [numTop] = mhg;
		numTop++;
	}
	else if ( score > bestScores[minx] ) {
		bestScores[minx] = score;
		bestwpi   [minx] = wpi;
		bestmhg   [minx] = mhg;
	}

	// set "minx" to the lowest score out of the top scores
	if ( numTop >= m_realMaxTop ) { // MAX_TOP ) {
		minx = 0;
		for ( int32_t k = 1 ; k < m_realMaxTop; k++ ){//MAX_TOP ; k++ ) {
			if ( bestScores[k] > bestScores[minx] ) continue;
			minx = k;
		}
	}

	// for evalSlidingWindow() sub-out algo, i guess we need this?
	if ( score > nonBodyMax && ! s_inBody[hg] ) {
		nonBodyMax = score;
		*bestPos = wpi;
	}

	// first key is 12 bytes
	if ( first ) { wpi += 6; first = false; }
	// advance
	wpi += 6;
	// exhausted?
	if ( wpi < endi && g_posdb.getKeySize(wpi) == 6 ) goto loop;

 done:

	// add up the top scores
	float sum = 0.0;
	for ( int32_t k = 0 ; k < numTop ; k++ ) {
		// if it is something like "enough for" in a wikipedia
		// phrase like "time enough for love" give it a boost!
		// now we set a special bit in the keys since we do a mini 
		// merge, we do the same thing for the syn bits
		if ( g_posdb.getIsHalfStopWikiBigram(bestwpi[k]) )
			sum += (bestScores[k] * 
				WIKI_BIGRAM_WEIGHT * 
				WIKI_BIGRAM_WEIGHT);
		// otherwise just add it up
		else
			sum += bestScores[k];
	}

	// wiki weight
	//sum *= ts;

	sum *= m_freqWeights[i];
	sum *= m_freqWeights[i];

	// int16_tcut
	//char *maxp = bestwpi[k];

	// if terms is a special wiki half stop bigram
	//if ( m_bflags[i] & BF_HALFSTOPWIKIBIGRAM ) {
	//	sum *= WIKI_BIGRAM_WEIGHT;
	//	sum *= WIKI_BIGRAM_WEIGHT;
	//}

	// empty list?
	//if ( ! wpi ) sum = -2.0;

	//
	// end the loop. return now if not collecting scoring info.
	//
	if ( ! pdcs ) return sum;
	// none? wtf?
	if ( numTop <= 0 ) return sum;
	// point into buf
	SingleScore *sx = (SingleScore *)m_singleScoreBuf.getBuf();
	int32_t need = sizeof(SingleScore) * numTop;
	// point to that
	if ( pdcs->m_singlesOffset < 0 )
		pdcs->m_singlesOffset = m_singleScoreBuf.length();
	// reset this i guess
	pdcs->m_singleScores = NULL;
	// sanity
	if ( m_singleScoreBuf.getAvail() < need ) { 
		static bool s_first = true;
		if ( s_first ) log("posdb: CRITICAL single buf overflow");
		s_first = false;
		return sum;
		//char *xx=NULL;*xx=0; }
	}
	// increase buf ptr over this then
	m_singleScoreBuf.incrementLength(need);

	// set each of the top scoring terms individiually
	for ( int32_t k = 0 ; k < numTop ; k++ , sx++ ) {
		// udpate count
		pdcs->m_numSingles++;
		char *maxp = bestwpi[k];
		sx->m_isSynonym      = g_posdb.getIsSynonym(maxp);
		sx->m_isHalfStopWikiBigram = 
			g_posdb.getIsHalfStopWikiBigram(maxp);
		//sx->m_isSynonym = (m_bflags[i] & BF_SYNONYM) ;
		sx->m_diversityRank  = g_posdb.getDiversityRank(maxp);
		sx->m_wordSpamRank   = g_posdb.getWordSpamRank(maxp);
		sx->m_hashGroup      = g_posdb.getHashGroup(maxp);
		sx->m_wordPos        = g_posdb.getWordPos(maxp);
		sx->m_densityRank = g_posdb.getDensityRank(maxp);
		float score = bestScores[k];
		//score *= ts;
		score *= m_freqWeights[i];
		score *= m_freqWeights[i];
		// if terms is a special wiki half stop bigram
		if ( sx->m_isHalfStopWikiBigram ) {
			score *= WIKI_BIGRAM_WEIGHT;
			score *= WIKI_BIGRAM_WEIGHT;
		}
		sx->m_finalScore = score;
		sx->m_tfWeight = m_freqWeights[i];
		sx->m_qtermNum = m_qtermNums[i];
		//int64_t *termFreqs = (int64_t *)m_r->ptr_termFreqs;
		//sx->m_termFreq = termFreqs[sx->m_qtermNum];
		sx->m_bflags   = m_bflags[i];
	}

	return sum;
}

// . advace two ptrs at the same time so it's just a linear scan
// . TODO: add all up, then basically taking a weight of the top 6 or so...
void PosdbTable::getTermPairScoreForNonBody ( int32_t i, int32_t j,
					      char *wpi , char *wpj , 
					      char *endi, char *endj,
					      int32_t qdist ,
					      float *retMax ) {

	int32_t p1 = g_posdb.getWordPos ( wpi );
	int32_t p2 = g_posdb.getWordPos ( wpj );

	// fix for bigram algorithm
	//if ( p1 == p2 ) p2 = p1 + 2;

	unsigned char hg1 = g_posdb.getHashGroup ( wpi );
	unsigned char hg2 = g_posdb.getHashGroup ( wpj );

	unsigned char wsr1 = g_posdb.getWordSpamRank(wpi);
	unsigned char wsr2 = g_posdb.getWordSpamRank(wpj);

	float spamw1 ;
	float spamw2 ;
	if ( hg1 == HASHGROUP_INLINKTEXT ) spamw1 = s_linkerWeights[wsr1];
	else                               spamw1 = s_wordSpamWeights[wsr1];

	if ( hg2 == HASHGROUP_INLINKTEXT ) spamw2 = s_linkerWeights[wsr2];
	else                               spamw2 = s_wordSpamWeights[wsr2];

	char *maxp1;
	char *maxp2;

	// density weight
	//float denw ;
	//if ( hg1 == HASHGROUP_BODY ) denw = 1.0;
	float denw1 = s_densityWeights[g_posdb.getDensityRank(wpi)];
	float denw2 = s_densityWeights[g_posdb.getDensityRank(wpj)];

	bool firsti = true;
	bool firstj = true;

	// if m_msg2 is NULL that means we are being called from seo.cpp
	// which gives us 6-byte keys only since we are restricted to just
	// one particular docid
	if ( ! m_msg2 ) {
		firsti = false;
		firstj = false;
	}

	float score;
	float max = -1.0;
	int32_t  dist;
	bool  fixedDistance;

 loop:

	if ( p1 <= p2 ) {
		// . skip the pair if they are in different hashgroups
		// . we no longer allow either to be in the body in this
		//   algo because we handle those cases in the sliding window
		//   algo!
		if ( ! s_isCompatible[hg1][hg2] ) goto skip1;
		// git distance
		dist = p2 - p1;
		// if zero, make sure its 2. this happens when the same bigram
		// is used by both terms. i.e. street uses the bigram 
		// 'street light' and so does 'light'. so the wordpositions
		// are exactly the same!
		if ( dist < 2 ) dist = 2;
		// fix distance if in different non-body hashgroups
		if ( dist > 50 ) {
			dist = FIXED_DISTANCE;
			fixedDistance = true;
		}
		else {
			fixedDistance = false;
		}
		// if both are link text and > 50 units apart that means
		// they are from different link texts
		//if ( hg1 == HASHGROUP_INLINKTEXT && dist > 50 ) goto skip1;
		// subtract from the dist the terms are apart in the query
		if ( dist >= qdist ) dist =  dist - qdist;
		//else               dist = qdist -  dist;
		// compute score based on that junk
		//score = (MAXWORDPOS+1) - dist;
		// good diversity? uneeded for pair algo
		//score *= s_diversityWeights[div1];
		//score *= s_diversityWeights[div2];
		// good density?
		score = 100 * denw1 * denw2;
		// hashgroup modifier
		score *= s_hashGroupWeights[hg1];
		score *= s_hashGroupWeights[hg2];
		// if synonym or alternate word form
		if ( g_posdb.getIsSynonym(wpi) ) score *= SYNONYM_WEIGHT;
		if ( g_posdb.getIsSynonym(wpj) ) score *= SYNONYM_WEIGHT;
		//if (m_bflags[i] & BF_SYNONYM) score *= SYNONYM_WEIGHT;
		//if (m_bflags[j] & BF_SYNONYM) score *= SYNONYM_WEIGHT;

		// word spam weights
		score *= spamw1 * spamw2;
		// huge title? do not allow 11th+ word to be weighted high
		//if ( hg1 == HASHGROUP_TITLE && dist > 20 ) 
		//	score /= s_hashGroupWeights[hg1];
		// mod by distance
		score /= (dist + 1.0);
		// log it for debug
		if ( ! m_msg2 && m_r->m_seoDebug >= 2 )
		log("seo: "
		    "gottermpairscore=%.05f "
		    "term1=%"INT32" "
		    "term2=%"INT32" "
		    "wpos1=%"INT32" "
		    "wpos2=%"INT32" "
		    "hg1=%s "
		    "hg2=%s "
		    "dr1=%"INT32" "
		    "dr2=%"INT32" "
		    ,score
		    ,i
		    ,j
		    ,p1
		    ,p2
		    ,getHashGroupString(hg1)
		    ,getHashGroupString(hg2)
		    ,(int32_t)g_posdb.getDensityRank(wpi)
		    ,(int32_t)g_posdb.getDensityRank(wpj)
		    );
		// tmp hack
		//score *= (dist+1.0);
		// best?
		if ( score > max ) {
			max = score;
			maxp1 = wpi;
			maxp2 = wpj;
		}
	skip1:
		// first key is 12 bytes
		if ( firsti ) { wpi += 6; firsti = false; }
		// advance
		wpi += 6;
		// end of list?
		if ( wpi >= endi ) goto done;
		// exhausted?
		if ( g_posdb.getKeySize ( wpi ) != 6 ) goto done;
		// update. include G-bits?
		p1 = g_posdb.getWordPos ( wpi );
		// hash group update
		hg1 = g_posdb.getHashGroup ( wpi );
		// update density weight in case hash group changed
		denw1 = s_densityWeights[g_posdb.getDensityRank(wpi)];
		// word spam weight update
		if ( hg1 == HASHGROUP_INLINKTEXT )
			spamw1=s_linkerWeights[g_posdb.getWordSpamRank(wpi)];
		else
			spamw1=s_wordSpamWeights[g_posdb.getWordSpamRank(wpi)];
		goto loop;
	}
	else {
		// . skip the pair if they are in different hashgroups
		// . we no longer allow either to be in the body in this
		//   algo because we handle those cases in the sliding window
		//   algo!
		if ( ! s_isCompatible[hg1][hg2] ) goto skip2;
		// get distance
		dist = p1 - p2;
		// if zero, make sure its 2. this happens when the same bigram
		// is used by both terms. i.e. street uses the bigram 
		// 'street light' and so does 'light'. so the wordpositions
		// are exactly the same!
		if ( dist < 2 ) dist = 2;
		// fix distance if in different non-body hashgroups
		if ( dist > 50 ) {
			dist = FIXED_DISTANCE;
			fixedDistance = true;
		}
		else {
			fixedDistance = false;
		}
		// if both are link text and > 50 units apart that means
		// they are from different link texts
		//if ( hg1 == HASHGROUP_INLINKTEXT && dist > 50 ) goto skip2;
		// subtract from the dist the terms are apart in the query
		if ( dist >= qdist ) {
			dist =  dist - qdist;
			// add 1 for being out of order
			dist += qdist - 1;
		}
		else {
			//dist =  dist - qdist;
			// add 1 for being out of order
			dist += 1; // qdist - 1;
		}

		// compute score based on that junk
		//score = (MAXWORDPOS+1) - dist;
		// good diversity? uneeded for pair algo
		//score *= s_diversityWeights[div1];
		//score *= s_diversityWeights[div2];
		// good density?
		score = 100 * denw1 * denw2;
		// hashgroup modifier
		score *= s_hashGroupWeights[hg1];
		score *= s_hashGroupWeights[hg2];
		// if synonym or alternate word form
		if ( g_posdb.getIsSynonym(wpi) ) score *= SYNONYM_WEIGHT;
		if ( g_posdb.getIsSynonym(wpj) ) score *= SYNONYM_WEIGHT;
		//if ( m_bflags[i] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
		//if ( m_bflags[j] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
		// word spam weights
		score *= spamw1 * spamw2;
		// huge title? do not allow 11th+ word to be weighted high
		//if ( hg1 == HASHGROUP_TITLE && dist > 20 ) 
		//	score /= s_hashGroupWeights[hg1];
		// mod by distance
		score /= (dist + 1.0);
		// tmp hack
		//score *= (dist+1.0);
		// best?
		if ( score > max ) {
			max = score;
			maxp1 = wpi;
			maxp2 = wpj;
		}
	skip2:
		// first key is 12 bytes
		if ( firstj ) { wpj += 6; firstj = false; }
		// advance
		wpj += 6;
		// end of list?
		if ( wpj >= endj ) goto done;
		// exhausted?
		if ( g_posdb.getKeySize ( wpj ) != 6 ) goto done;
		// update
		p2 = g_posdb.getWordPos ( wpj );
		// hash group update
		hg2 = g_posdb.getHashGroup ( wpj );
		// update density weight in case hash group changed
		denw2 = s_densityWeights[g_posdb.getDensityRank(wpj)];
		// word spam weight update
		if ( hg2 == HASHGROUP_INLINKTEXT )
			spamw2=s_linkerWeights[g_posdb.getWordSpamRank(wpj)];
		else
			spamw2=s_wordSpamWeights[g_posdb.getWordSpamRank(wpj)];
		goto loop;
	}

 done:

	//if ( max < *retMax ) return;
	//if ( max > 0 ) 
	//	log("posdb: ret score = %f",max);
	*retMax = max;
}

float PosdbTable::getTermPairScoreForWindow ( int32_t i,
					      int32_t j,
					      char *wpi , 
					      char *wpj ,
					      int32_t fixedDistance ) {

	if ( ! wpi ) return -1.00;
	if ( ! wpj ) return -1.00;

	int32_t p1 = g_posdb.getWordPos ( wpi );
	int32_t p2 = g_posdb.getWordPos ( wpj );
	unsigned char hg1 = g_posdb.getHashGroup ( wpi );
	unsigned char hg2 = g_posdb.getHashGroup ( wpj );
	unsigned char wsr1 = g_posdb.getWordSpamRank(wpi);
	unsigned char wsr2 = g_posdb.getWordSpamRank(wpj);
	float spamw1;
	float spamw2;
	float denw1;
	float denw2;
	float dist;
	float score;
	if ( hg1 ==HASHGROUP_INLINKTEXT)spamw1=s_linkerWeights[wsr1];
	else                            spamw1=s_wordSpamWeights[wsr1];
	if ( hg2 ==HASHGROUP_INLINKTEXT)spamw2=s_linkerWeights[wsr2];
	else                            spamw2=s_wordSpamWeights[wsr2];
	denw1 = s_densityWeights[g_posdb.getDensityRank(wpi)];
	denw2 = s_densityWeights[g_posdb.getDensityRank(wpj)];
	// set this
	if ( fixedDistance != 0 ) {
		dist = fixedDistance;
	}
	else {
		// do the math now
		if ( p2 < p1 ) dist = p1 - p2;
		else           dist = p2 - p1;
		// if zero, make sure its 2. this happens when the same bigram
		// is used by both terms. i.e. street uses the bigram 
		// 'street light' and so does 'light'. so the wordpositions
		// are exactly the same!
		if ( dist < 2 ) dist = 2;
		// subtract from the dist the terms are apart in the query
		if ( dist >= m_qdist ) dist =  dist - m_qdist;
		// out of order? penalize by 1 unit
		if ( p2 < p1 ) dist += 1;
	}
	// TODO: use left and right diversity if no matching query term
	// is on the left or right
	//score *= s_diversityWeights[div1];
	//score *= s_diversityWeights[div2];
	// good density?
	score = 100 * denw1 * denw2;
	// wikipedia phrase weight
	//score *= ts;
	// hashgroup modifier
	score *= s_hashGroupWeights[hg1];
	score *= s_hashGroupWeights[hg2];
	// if synonym or alternate word form
	if ( g_posdb.getIsSynonym(wpi) ) score *= SYNONYM_WEIGHT;
	if ( g_posdb.getIsSynonym(wpj) ) score *= SYNONYM_WEIGHT;
	//if ( m_bflags[i] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
	//if ( m_bflags[j] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
	// word spam weights
	score *= spamw1 * spamw2;
	// mod by distance
	score /= (dist + 1.0);
	// tmp hack
	//score *= (dist+1.0);
	return score;
}


// . advance two ptrs at the same time so it's just a linear scan
// . TODO: add all up, then basically taking a weight of the top 6 or so...
// . skip body terms not in the sliding window as defined by m_windowTermPtrs[]
float PosdbTable::getTermPairScoreForAny ( int32_t i, int32_t j,
					  char *wpi , char *wpj , 
					  char *endi, char *endj,
					   DocIdScore *pdcs ) {

	// wiki phrase weight?
	float wts;

	int32_t qdist;

	// but if they are in the same wikipedia phrase
	// then try to keep their positions as in the query.
	// so for 'time enough for love' ideally we want
	// 'time' to be 6 units apart from 'love'
	if ( m_wikiPhraseIds[j] == m_wikiPhraseIds[i] &&
	     // zero means not in a phrase
	     m_wikiPhraseIds[j] ) {
		qdist = m_qpos[j] - m_qpos[i];
		// wiki weight
		wts = (float)WIKI_WEIGHT; // .50;
	}
	else {
		// basically try to get query words as close
		// together as possible
		qdist = 2;
		// this should help fix
		// 'what is an unsecured loan' so we are more likely
		// to get the page that has that exact phrase in it.
		// yes, but hurts how to make a lock pick set.
		//qdist = qpos[j] - qpos[i];
		// wiki weight
		wts = 1.0;
	}

	bool inSameQuotedPhrase = false;
	if ( m_quotedStartIds[i] == m_quotedStartIds[j] &&
	     m_quotedStartIds[i] >= 0 )
		inSameQuotedPhrase = true;

	// oops.. this was not counting non-space punct for 2 units 
	// instead of 1
	if ( inSameQuotedPhrase ) 
		qdist = m_qpos[j] - m_qpos[i];		


	int32_t p1 = g_posdb.getWordPos ( wpi );
	int32_t p2 = g_posdb.getWordPos ( wpj );

	// fix for bigram algorithm
	//if ( p1 == p2 ) p2 = p1 + 2;

	unsigned char hg1 = g_posdb.getHashGroup ( wpi );
	unsigned char hg2 = g_posdb.getHashGroup ( wpj );

	// reduce to either HASHGROUP_BODY/TITLE/INLINK/META
	unsigned char mhg1 = hg1;
	unsigned char mhg2 = hg2;
	if ( s_inBody[mhg1] ) mhg1 = HASHGROUP_BODY;
	if ( s_inBody[mhg2] ) mhg2 = HASHGROUP_BODY;

	unsigned char wsr1 = g_posdb.getWordSpamRank(wpi);
	unsigned char wsr2 = g_posdb.getWordSpamRank(wpj);

	float spamw1 ;
	float spamw2 ;
	if ( hg1 == HASHGROUP_INLINKTEXT ) spamw1 = s_linkerWeights[wsr1];
	else                               spamw1 = s_wordSpamWeights[wsr1];

	if ( hg2 == HASHGROUP_INLINKTEXT ) spamw2 = s_linkerWeights[wsr2];
	else                               spamw2 = s_wordSpamWeights[wsr2];

	// density weight
	//float denw ;
	//if ( hg1 == HASHGROUP_BODY ) denw = 1.0;
	float denw1 = s_densityWeights[g_posdb.getDensityRank(wpi)];
	float denw2 = s_densityWeights[g_posdb.getDensityRank(wpj)];

	bool firsti = true;
	bool firstj = true;

	// if m_msg2 is NULL that means we are being called from seo.cpp
	// which gives us 6-byte keys only since we are restricted to just
	// one particular docid. not any more, i think we use
	// QueryTerm::m_posdbListPtr and make the first key 12 bytes.
	//if ( ! m_msg2 ) {
	//	firsti = false;
	//	firstj = false;
	//}

	float score;
	int32_t  minx = -1;
	float bestScores[MAX_TOP];
	char *bestwpi   [MAX_TOP];
	char *bestwpj   [MAX_TOP];
	char  bestmhg1  [MAX_TOP];
	char  bestmhg2  [MAX_TOP];
	char  bestFixed [MAX_TOP];
	int32_t  numTop = 0;
	int32_t  dist;
	bool  fixedDistance;
	int32_t  bro;
	char  syn1;
	char  syn2;

 loop:

	// pos = 19536
	//log("hg1=%"INT32" hg2=%"INT32" pos1=%"INT32" pos2=%"INT32"",
	//    (int32_t)hg1,(int32_t)hg2,(int32_t)p1,(int32_t)p2);

	// . if p1/p2 is in body and not in window, skip
	// . this is how we restrict all body terms to the winning
	//   sliding window
	if ( s_inBody[hg1] && wpi != m_windowTermPtrs[i] ) 
		goto skip1;
	if ( s_inBody[hg2] && wpj != m_windowTermPtrs[j] ) 
		goto skip2;

	// make this strictly < now and not <= because in the event
	// of bigram terms, where p1==p2 we'd like to advance p2/wj to
	// point to the non-syn single term in order to get a better score
	// to fix the 'search engine' query on gigablast.com
	if ( p1 <= p2 ) {
		// git distance
		dist = p2 - p1;

		// if in the same quoted phrase, order is bad!
		if ( inSameQuotedPhrase ) {
			// debug
			//log("dddx: i=%"INT32" j=%"INT32" dist=%"INT32" qdist=%"INT32" posi=%"INT32" "
			//    "posj=%"INT32"",
			//    i,j,dist,qdist,p1,p2);
			// TODO: allow for off by 1
			// if it has punct in it then dist will be 3, 
			// just a space or similar then dist should be 2.
			if ( dist > qdist && dist - qdist >= 2 ) 
				goto skip1;
			if ( dist < qdist && qdist - dist >= 2 ) 
				goto skip1;
		}

		// are either synonyms
		syn1 = g_posdb.getIsSynonym(wpi);
		syn2 = g_posdb.getIsSynonym(wpj);
		// if zero, make sure its 2. this happens when the same bigram
		// is used by both terms. i.e. street uses the bigram 
		// 'street light' and so does 'light'. so the wordpositions
		// are exactly the same!
		if ( dist < 2 ) {
			dist = 2;
		}
		// fix distance if in different non-body hashgroups
		if ( dist < 50 ) {
			fixedDistance = false;
		}
		// body vs title, linktext vs title, linktext vs body
		else if ( mhg1 != mhg2 ) {
			dist = FIXED_DISTANCE;
			fixedDistance = true;
		}
		// link text to other link text
		else if ( mhg1 == HASHGROUP_INLINKTEXT ) {
			dist = FIXED_DISTANCE;
			fixedDistance = true;
		}
		// if both are link text and > 50 units apart that means
		// they are from different link texts
		//if ( hg1 == HASHGROUP_INLINKTEXT && dist > 50 ) goto skip1;
		// subtract from the dist the terms are apart in the query
		if ( dist >= qdist ) dist =  dist - qdist;
		//else               dist = qdist -  dist;
		// compute score based on that junk
		//score = (MAXWORDPOS+1) - dist;
		// good diversity? uneeded for pair algo
		//score *= s_diversityWeights[div1];
		//score *= s_diversityWeights[div2];
		// good density?
		score = 100 * denw1 * denw2;
		// hashgroup modifier
		score *= s_hashGroupWeights[hg1];
		score *= s_hashGroupWeights[hg2];
		// if synonym or alternate word form
		if ( syn1 ) score *= SYNONYM_WEIGHT;
		if ( syn2 ) score *= SYNONYM_WEIGHT;
		// the new logic
		if ( g_posdb.getIsHalfStopWikiBigram(wpi) ) 
			score *= WIKI_BIGRAM_WEIGHT;
		if ( g_posdb.getIsHalfStopWikiBigram(wpj) ) 
			score *= WIKI_BIGRAM_WEIGHT;
		//if ( m_bflags[i] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
		//if ( m_bflags[j] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
		// word spam weights
		score *= spamw1 * spamw2;
		// huge title? do not allow 11th+ word to be weighted high
		//if ( hg1 == HASHGROUP_TITLE && dist > 20 ) 
		//	score /= s_hashGroupWeights[hg1];
		// mod by distance
		score /= (dist + 1.0);
		// tmp hack
		//score *= (dist+1.0);

		// log it for debug
		if ( ! m_msg2 && m_debug >= 2 )
		log("seo: "
		    "gottermpairscore2=%.010f "
		    "term1=%"INT32" "
		    "term2=%"INT32" "
		    "wpos1=%"INT32" "
		    "wpos2=%"INT32" "
		    "dist=%"INT32" "
		    "qdist=%"INT32" "
		    "syn1=%"INT32" "
		    "syn2=%"INT32" "
		    "hg1=%s "
		    "hg2=%s "
		    "dr1=%"INT32" "
		    "dr2=%"INT32" "
		    "wts=%f "
		    "tfw1=%f "
		    "tfw2=%f "
		    ,score * wts * m_freqWeights[i] * m_freqWeights[j]
		    ,i
		    ,j
		    ,p1
		    ,p2
		    ,dist
		    ,qdist
		    ,(int32_t)syn1
		    ,(int32_t)syn2
		    ,getHashGroupString(hg1)
		    ,getHashGroupString(hg2)
		    ,(int32_t)g_posdb.getDensityRank(wpi)
		    ,(int32_t)g_posdb.getDensityRank(wpj)
		    ,wts
		    ,m_freqWeights[i]
		    ,m_freqWeights[j]
		    );

		// if our hg1/hg2 hashgroup pairing already exists
		// in the bestScores array we have to beat it and then
		// we have to replace that. we can only have one per,
		// except for linktext!
		//if ( m_docId == 52648678438LL )
		//	log("hey");
		bro = -1;
		for ( int32_t k = 0 ; k < numTop ; k++ ) {
			if ( bestmhg1[k]==mhg1 && hg1 !=HASHGROUP_INLINKTEXT ){
				bro = k;
				break;
			}
			if ( bestmhg2[k]==mhg2 && hg2 !=HASHGROUP_INLINKTEXT ){
				bro = k;
				break;
			}
		}
		if ( bro >= 0 ) {
			if ( score > bestScores[bro] ) {
				bestScores[bro] = score;
				bestwpi   [bro] = wpi;
				bestwpj   [bro] = wpj;
				bestmhg1  [bro] = mhg1;
				bestmhg2  [bro] = mhg2;
				bestFixed [bro] = fixedDistance;
			}
		}
		// best?
		else if ( numTop < m_realMaxTop ) { // MAX_TOP ) {
			bestScores[numTop] = score;
			bestwpi   [numTop] = wpi;
			bestwpj   [numTop] = wpj;
			bestmhg1  [numTop] = mhg1;
			bestmhg2  [numTop] = mhg2;
			bestFixed [numTop] = fixedDistance;
			numTop++;
		}
		else if ( score > bestScores[minx] ) {
			bestScores[minx] = score;
			bestwpi   [minx] = wpi;
			bestwpj   [minx] = wpj;
			bestmhg1  [minx] = mhg1;
			bestmhg2  [minx] = mhg2;
			bestFixed [minx] = fixedDistance;
		}
		
		// set "minx" to the lowest score out of the top scores
		if ( numTop >= m_realMaxTop ) { // MAX_TOP ) {
			minx = 0;
			for ( int32_t k = 1 ; k < m_realMaxTop;k++){//MAX_TOP;k++
				if (bestScores[k]>bestScores[minx] ) continue;
				minx = k;
			}
		}

		
	skip1:
		// first key is 12 bytes
		if ( firsti ) { wpi += 6; firsti = false; }
		// advance
		wpi += 6;
		// end of list?
		if ( wpi >= endi ) goto done;
		// exhausted?
		if ( g_posdb.getKeySize ( wpi ) != 6 ) {
			// sometimes there is posdb index corruption and
			// we have a 12 byte key with the same docid but
			// different siterank or langid because it was
			// not deleted right!
			if ( (uint64_t)g_posdb.getDocId(wpi) != 
			     m_docId ) {
				char *xx=NULL;*xx=0;
				goto done;
			}
			// re-set this i guess
			firsti = true;
		}
		// update. include G-bits?
		p1 = g_posdb.getWordPos ( wpi );
		// hash group update
		hg1 = g_posdb.getHashGroup ( wpi );
		// the "modified" hash group
		mhg1 = hg1;
		if ( s_inBody[mhg1] ) mhg1 = HASHGROUP_BODY;
		// update density weight in case hash group changed
		denw1 = s_densityWeights[g_posdb.getDensityRank(wpi)];
		// word spam weight update
		if ( hg1 == HASHGROUP_INLINKTEXT )
			spamw1=s_linkerWeights[g_posdb.getWordSpamRank(wpi)];
		else
			spamw1=s_wordSpamWeights[g_posdb.getWordSpamRank(wpi)];
		goto loop;
	}
	else {
		// get distance
		dist = p1 - p2;

		// if in the same quoted phrase, order is bad!
		if ( inSameQuotedPhrase ) {
			// debug
			//log("dddy: i=%"INT32" j=%"INT32" dist=%"INT32" qdist=%"INT32" posi=%"INT32" "
			//    "posj=%"INT32"",
			//    i,j,dist,qdist,p1,p2);
			goto skip2;
		}

		// if zero, make sure its 2. this happens when the same bigram
		// is used by both terms. i.e. street uses the bigram 
		// 'street light' and so does 'light'. so the wordpositions
		// are exactly the same!
		if ( dist < 2 ) dist = 2;
		// fix distance if in different non-body hashgroups
		if ( dist < 50 ) {
			fixedDistance = false;
		}
		// body vs title, linktext vs title, linktext vs body
		else if ( mhg1 != mhg2 ) {
			dist = FIXED_DISTANCE;
			fixedDistance = true;
		}
		// link text to other link text
		else if ( mhg1 == HASHGROUP_INLINKTEXT ) {
			dist = FIXED_DISTANCE;
			fixedDistance = true;
		}
		// if both are link text and > 50 units apart that means
		// they are from different link texts
		//if ( hg1 == HASHGROUP_INLINKTEXT && dist > 50 ) goto skip2;
		// subtract from the dist the terms are apart in the query
		if ( dist >= qdist ) {
			dist =  dist - qdist;
			// add 1 for being out of order
			dist += qdist - 1;
		}
		else {
			//dist =  dist - qdist;
			// add 1 for being out of order
			dist += 1; // qdist - 1;
		}

		// compute score based on that junk
		//score = (MAXWORDPOS+1) - dist;
		// good diversity? uneeded for pair algo
		//score *= s_diversityWeights[div1];
		//score *= s_diversityWeights[div2];
		// good density?
		score = 100 * denw1 * denw2;
		// hashgroup modifier
		score *= s_hashGroupWeights[hg1];
		score *= s_hashGroupWeights[hg2];
		// if synonym or alternate word form
		if ( g_posdb.getIsSynonym(wpi) ) score *= SYNONYM_WEIGHT;
		if ( g_posdb.getIsSynonym(wpj) ) score *= SYNONYM_WEIGHT;
		//if ( m_bflags[i] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
		//if ( m_bflags[j] & BF_SYNONYM ) score *= SYNONYM_WEIGHT;
		// word spam weights
		score *= spamw1 * spamw2;
		// huge title? do not allow 11th+ word to be weighted high
		//if ( hg1 == HASHGROUP_TITLE && dist > 20 ) 
		//	score /= s_hashGroupWeights[hg1];
		// mod by distance
		score /= (dist + 1.0);
		// tmp hack
		//score *= (dist+1.0);

		// log it for debug
		if ( ! m_msg2 && m_debug >= 2 )
		log("seo: "
		    "gottermpairscore3=%.010f "
		    "term1=%"INT32" "
		    "term2=%"INT32" "
		    "wpos1=%"INT32" "
		    "wpos2=%"INT32" "
		    "dist=%"INT32" "
		    "qdist=%"INT32" "
		    "syn1=%"INT32" "
		    "syn2=%"INT32" "
		    "hg1=%s "
		    "hg2=%s "
		    "dr1=%"INT32" "
		    "dr2=%"INT32" "
		    "wts=%f "
		    "tfw1=%f "
		    "tfw2=%f "
		    ,score * wts * m_freqWeights[i] * m_freqWeights[j]
		    ,i
		    ,j
		    ,p1
		    ,p2
		    ,dist
		    ,qdist
		    ,(int32_t)g_posdb.getIsSynonym(wpi)
		    ,(int32_t)g_posdb.getIsSynonym(wpj)
		    ,getHashGroupString(hg1)
		    ,getHashGroupString(hg2)
		    ,(int32_t)g_posdb.getDensityRank(wpi)
		    ,(int32_t)g_posdb.getDensityRank(wpj)
		    ,wts
		    ,m_freqWeights[i]
		    ,m_freqWeights[j]
		    );

		// if our hg1/hg2 hashgroup pairing already exists
		// in the bestScores array we have to beat it and then
		// we have to replace that. we can only have one per,
		// except for linktext!
		//if ( m_docId == 52648678438LL )
		//	log("hey");
		bro = -1;
		for ( int32_t k = 0 ; k < numTop ; k++ ) {
			if ( bestmhg1[k]==mhg1 && hg1 !=HASHGROUP_INLINKTEXT ){
				bro = k;
				break;
			}
			if ( bestmhg2[k]==mhg2 && hg2 !=HASHGROUP_INLINKTEXT ){
				bro = k;
				break;
			}
		}
		if ( bro >= 0 ) {
			if ( score > bestScores[bro] ) {
				bestScores[bro] = score;
				bestwpi   [bro] = wpi;
				bestwpj   [bro] = wpj;
				bestmhg1  [bro] = mhg1;
				bestmhg2  [bro] = mhg2;
				bestFixed [bro] = fixedDistance;
			}
		}
		// best?
		else if ( numTop < m_realMaxTop ) { // MAX_TOP ) {
			bestScores[numTop] = score;
			bestwpi   [numTop] = wpi;
			bestwpj   [numTop] = wpj;
			bestmhg1  [numTop] = mhg1;
			bestmhg2  [numTop] = mhg2;
			bestFixed [numTop] = fixedDistance;
			numTop++;
		}
		else if ( score > bestScores[minx] ) {
			bestScores[minx] = score;
			bestwpi   [minx] = wpi;
			bestwpj   [minx] = wpj;
			bestmhg1  [minx] = mhg1;
			bestmhg2  [minx] = mhg2;
			bestFixed [minx] = fixedDistance;
		}
		
		// set "minx" to the lowest score out of the top scores
		if ( numTop >= m_realMaxTop ) { // MAX_TOP ) {
			minx = 0;
			for ( int32_t k = 1 ; k < m_realMaxTop;k++){//MAX_TOP;k++
				if (bestScores[k]>bestScores[minx] ) continue;
				minx = k;
			}
		}

	skip2:
		// first key is 12 bytes
		if ( firstj ) { wpj += 6; firstj = false; }
		// advance
		wpj += 6;
		// end of list?
		if ( wpj >= endj ) goto done;
		// exhausted?
		if ( g_posdb.getKeySize ( wpj ) != 6 ) {
			// sometimes there is posdb index corruption and
			// we have a 12 byte key with the same docid but
			// different siterank or langid because it was
			// not deleted right!
			if ( (uint64_t)g_posdb.getDocId(wpj) != 
			     m_docId ) {
				char *xx=NULL;*xx=0;
				goto done;
			}
			// re-set this i guess
			firstj = true;
		}
		// update
		p2 = g_posdb.getWordPos ( wpj );
		// hash group update
		hg2 = g_posdb.getHashGroup ( wpj );
		// the "modified" hash group
		mhg2 = hg2;
		if ( s_inBody[mhg2] ) mhg2 = HASHGROUP_BODY;
		// update density weight in case hash group changed
		denw2 = s_densityWeights[g_posdb.getDensityRank(wpj)];
		// word spam weight update
		if ( hg2 == HASHGROUP_INLINKTEXT )
			spamw2=s_linkerWeights[g_posdb.getWordSpamRank(wpj)];
		else
			spamw2=s_wordSpamWeights[g_posdb.getWordSpamRank(wpj)];
		goto loop;
	}

 done:

	// add up the top scores
	float sum = 0.0;
	for ( int32_t k = 0 ; k < numTop ; k++ )
		sum += bestScores[k];

	if ( m_debug >= 2 ) {
		for ( int32_t k = 0 ; k < numTop ; k++ )
			log("posdb: best score #%"INT32" = %f",k,bestScores[k]);
		log("posdb: best score sum = %f",sum);
	}

	// wiki phrase weight
	sum *= wts;

	// mod by freq weight
	sum *= m_freqWeights[i];
	sum *= m_freqWeights[j];

	if ( m_debug >= 2 )
		log("posdb: best score final = %f",sum);

	// wiki bigram weight
	// i don't think this works this way any more!
	//if ( m_bflags[i] & BF_HALFSTOPWIKIBIGRAM ) sum *= WIKI_BIGRAM_WEIGHT;
	//if ( m_bflags[j] & BF_HALFSTOPWIKIBIGRAM ) sum *= WIKI_BIGRAM_WEIGHT;

	//
	// end the loop. return now if not collecting scoring info.
	//
	if ( ! pdcs ) return sum;
	// none? wtf?
	if ( numTop <= 0 ) return sum;

	//
	// now store the PairScores into the m_pairScoreBuf for this 
	// top docid.
	//

	// point into buf
	PairScore *px = (PairScore *)m_pairScoreBuf.getBuf();
	int32_t need = sizeof(PairScore) * numTop;
	// point to that
	if ( pdcs->m_pairsOffset < 0 )
		pdcs->m_pairsOffset = m_pairScoreBuf.length();
	// reset this i guess
	pdcs->m_pairScores = NULL;
	// sanity
	if ( m_pairScoreBuf.getAvail() < need ) { 
		// m_pairScores will be NULL
		static bool s_first = true;
		if ( s_first ) log("posdb: CRITICAL pair buf overflow");
		s_first = false;
		return sum;
	}
	// increase buf ptr over this then
	m_pairScoreBuf.incrementLength(need);

	//if ( m_debug )
	//	log("posdb: DOCID=%"INT64" BESTSCORE=%f",m_docId,sum);

	// set each of the top scoring terms individiually
	for ( int32_t k = 0 ; k < numTop ; k++ , px++ ) {
		pdcs->m_numPairs++;
		char *maxp1 = bestwpi[k];
		char *maxp2 = bestwpj[k];
		float score = bestScores[k];
		bool fixedDist = bestFixed[k];
		score *= wts;
		score *= m_freqWeights[i];
		score *= m_freqWeights[j];
		// we have to encode these bits into the mini merge now
		if ( g_posdb.getIsHalfStopWikiBigram(maxp1) )
			score *= WIKI_BIGRAM_WEIGHT;
		if ( g_posdb.getIsHalfStopWikiBigram(maxp2) )
			score *= WIKI_BIGRAM_WEIGHT;
		//if ( m_bflags[i] & BF_HALFSTOPWIKIBIGRAM ) 
		//if ( m_bflags[j] & BF_HALFSTOPWIKIBIGRAM ) 
		// wiki phrase weight
		px->m_finalScore     = score;
		px->m_wordPos1       = g_posdb.getWordPos(maxp1);
		px->m_wordPos2       = g_posdb.getWordPos(maxp2);
		char syn1 = g_posdb.getIsSynonym(maxp1);
		char syn2 = g_posdb.getIsSynonym(maxp2);
		px->m_isSynonym1     = syn1;
		px->m_isSynonym2     = syn2;
		px->m_isHalfStopWikiBigram1 = 
			g_posdb.getIsHalfStopWikiBigram(maxp1);
		px->m_isHalfStopWikiBigram2 = 
			g_posdb.getIsHalfStopWikiBigram(maxp2);
		//px->m_isSynonym1 = ( m_bflags[i] & BF_SYNONYM );
		//px->m_isSynonym2 = ( m_bflags[j] & BF_SYNONYM );
		px->m_diversityRank1 = g_posdb.getDiversityRank(maxp1);
		px->m_diversityRank2 = g_posdb.getDiversityRank(maxp2);
		px->m_wordSpamRank1  = g_posdb.getWordSpamRank(maxp1);
		px->m_wordSpamRank2  = g_posdb.getWordSpamRank(maxp2);
		px->m_hashGroup1     = g_posdb.getHashGroup(maxp1);
		px->m_hashGroup2     = g_posdb.getHashGroup(maxp2);
		px->m_qdist          = qdist;
		// bigram algorithm fix
		//if ( px->m_wordPos1 == px->m_wordPos2 )
		//	px->m_wordPos2 += 2;
		px->m_densityRank1   = g_posdb.getDensityRank(maxp1);
		px->m_densityRank2   = g_posdb.getDensityRank(maxp2);
		px->m_fixedDistance  = fixedDist;
		px->m_qtermNum1      = m_qtermNums[i];
		px->m_qtermNum2      = m_qtermNums[j];
		//int64_t *termFreqs = (int64_t *)m_r->ptr_termFreqs;
		//px->m_termFreq1      = termFreqs[px->m_qtermNum1];
		//px->m_termFreq2      = termFreqs[px->m_qtermNum2];
		px->m_tfWeight1      = m_freqWeights[i];//sfw[i];
		px->m_tfWeight2      = m_freqWeights[j];//sfw[j];
		px->m_bflags1        = m_bflags[i];
		px->m_bflags2        = m_bflags[j];
		// flag it as in same wiki phrase
		if ( wts == (float)WIKI_WEIGHT ) px->m_inSameWikiPhrase =true;
		else                             px->m_inSameWikiPhrase =false;
		// only log for debug if it is one result
		if ( m_debug < 2 ) continue;
		// log each one for debug
		log("posdb: result #%"INT32" "
		    "i=%"INT32" "
		    "j=%"INT32" "
		    "termNum0=%"INT32" "
		    "termNum1=%"INT32" "
		    "finalscore=%f "
		    "tfw0=%f "
		    "tfw1=%f "
		    "fixeddist=%"INT32" " // bool
		    "wts=%f "
		    "bflags0=%"INT32" "
		    "bflags1=%"INT32" "
		    "syn0=%"INT32" "
		    "syn1=%"INT32" "
		    "div0=%"INT32" "
		    "div1=%"INT32" "
		    "wspam0=%"INT32" "
		    "wspam1=%"INT32" "
		    "hgrp0=%s "
		    "hgrp1=%s "
		    "qdist=%"INT32" "
		    "wpos0=%"INT32" "
		    "wpos1=%"INT32" "
		    "dens0=%"INT32" "
		    "dens1=%"INT32" "
		    ,k
		    ,i
		    ,j
		    ,px->m_qtermNum1
		    ,px->m_qtermNum2
		    ,score
		    ,m_freqWeights[i]
		    ,m_freqWeights[j]
		    ,(int32_t)bestFixed[k]
		    ,wts
		    , (int32_t)m_bflags[i]
		    , (int32_t)m_bflags[j]
		    , (int32_t)px->m_isSynonym1
		    , (int32_t)px->m_isSynonym2
		    , (int32_t)px->m_diversityRank1
		    , (int32_t)px->m_diversityRank2
		    , (int32_t)px->m_wordSpamRank1
		    , (int32_t)px->m_wordSpamRank2
		    , getHashGroupString(px->m_hashGroup1)
		    , getHashGroupString(px->m_hashGroup2)
		    , (int32_t)px->m_qdist
		    , (int32_t)px->m_wordPos1
		    , (int32_t)px->m_wordPos2
		    , (int32_t)px->m_densityRank1
		    , (int32_t)px->m_densityRank2
		    );
	}

	// do the same but for second bests! so seo.cpp's top term pairs
	// algo can do a term insertion, and if that hurts the best pair
	// the 2nd best might cover for it. ideally, we'd have all the term
	// pairs for this algo, but i think we'll have to get by on just this.

	return sum;
}

//
//
// TRY TO SPEED IT UP!!!
//
//


// returns false and sets g_errno on error
bool PosdbTable::setQueryTermInfo ( ) {

	// alloc space. assume max
	//int32_t qneed = sizeof(QueryTermInfo) * m_msg2->getNumLists();
	int32_t qneed = sizeof(QueryTermInfo) * m_q->m_numTerms;
	if ( ! m_qiBuf.reserve(qneed,"qibuf") ) return false; // label it too!
	// point to those
	QueryTermInfo *qip = (QueryTermInfo *)m_qiBuf.getBufStart();

	RdbList *list = NULL;

	int32_t nrg = 0;

	// assume not sorting by a numeric termlist
	m_sortByTermNum = -1;
	m_sortByTermNumInt = -1;

	// now we have score ranges for gbmin:price:1.99 etc.
	m_minScoreTermNum = -1;
	m_maxScoreTermNum = -1;

	// for gbminint:count:99 etc.
	m_minScoreTermNumInt = -1;
	m_maxScoreTermNumInt = -1;

	m_hasMaxSerpScore = false;
	if ( m_r->m_minSerpDocId )
		m_hasMaxSerpScore = true;

	//for ( int32_t i = 0 ; i < m_msg2->getNumLists() ; i++ ) {
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		QueryTerm *qt = &m_q->m_qterms[i];

		// default this to off
		qt->m_queryTermInfoNum = -1;

		if ( ! qt->m_isRequired ) continue;
		// set this stff
		QueryWord     *qw =   qt->m_qword;
		//int32_t wordNum = qw - &m_q->m_qwords[0];
		// get one
		QueryTermInfo *qti = &qip[nrg];
		// and set it
		qti->m_qt            = qt;
		qti->m_qtermNum      = i;
		// and vice versa
		qt->m_queryTermInfoNum = nrg;
		// now we count the total # of docs that have a facet
		// for doing tf/idf type things
		//qti->m_numDocsThatHaveFacet = 0;
		// this is not good enough, we need to count 
		// non-whitespace punct as 2 units not 1 unit
		// otherwise qdist gets thrown off and our phrasing fails.
		// so use QueryTerm::m_qpos just for this.
		//qti->m_qpos          = wordNum;
		qti->m_qpos          = qw->m_posNum;
		qti->m_wikiPhraseId  = qw->m_wikiPhraseId;
		qti->m_quotedStartId = qw->m_quoteStart;
		// is it gbsortby:?
		if ( qt->m_fieldCode == FIELD_GBSORTBYFLOAT ||
		     qt->m_fieldCode == FIELD_GBREVSORTBYFLOAT ) {
			m_sortByTermNum = i;
			m_sortByTermInfoNum = nrg;
		}

		if ( qt->m_fieldCode == FIELD_GBSORTBYINT ||
		     qt->m_fieldCode == FIELD_GBREVSORTBYINT ) {
			m_sortByTermNumInt = i;
			m_sortByTermInfoNumInt = nrg;
			// tell topTree to use int scores
			m_topTree->m_useIntScores = true;
		}

		// is it gbmin:price:1.99?
		if ( qt->m_fieldCode == FIELD_GBNUMBERMIN ) {
			m_minScoreTermNum = i;
			m_minScoreVal = qt->m_qword->m_float;
		}
		if ( qt->m_fieldCode == FIELD_GBNUMBERMAX ) {
			m_maxScoreTermNum = i;
			m_maxScoreVal = qt->m_qword->m_float;
		}
		if ( qt->m_fieldCode == FIELD_GBNUMBERMININT ) {
			m_minScoreTermNumInt = i;
			m_minScoreValInt = qt->m_qword->m_int;
		}
		if ( qt->m_fieldCode == FIELD_GBNUMBERMAXINT ) {
			m_maxScoreTermNumInt = i;
			m_maxScoreValInt = qt->m_qword->m_int;
		}
		// count
		int32_t nn = 0;
		// also add in bigram lists
		int32_t left  = qt->m_leftPhraseTermNum;
		int32_t right = qt->m_rightPhraseTermNum;
		// terms
		QueryTerm *leftTerm  = qt->m_leftPhraseTerm;
		QueryTerm *rightTerm = qt->m_rightPhraseTerm;
		bool leftAlreadyAdded = false;
		bool rightAlreadyAdded = false;
		//int64_t totalTermFreq = 0;
		//int64_t *tfreqs = (int64_t *)m_r->ptr_termFreqs;
		//
		// add the non-bigram list AFTER the
		// bigrams, which we like to do when we PREFER the bigram
		// terms because they are scored higher, specifically, as
		// in the case of being half stop wikipedia phrases like
		// "the tigers" for the query 'the tigers' we want to give
		// a slight bonus, 1.20x, for having that bigram since its
		// in wikipedia
		//

		//
		// add left bigram lists. BACKWARDS.
		//
		if ( left>=0 && leftTerm && leftTerm->m_isWikiHalfStopBigram ){
			// assume added
			leftAlreadyAdded = true;
			// get list
			//list = m_msg2->getList(left);
			list = m_q->m_qterms[left].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subLists[nn] = list;
			// left bigram is #2
			//bigramSet[nrg][nn] = 2;
			// special flags
			qti->m_bigramFlags[nn] = BF_HALFSTOPWIKIBIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_bigramFlags[nn] |= BF_PIPED;
			// add list of member terms as well
			//qti->m_qtermList[nn] = &m_q->m_qterms[left];
			m_q->m_qterms[left].m_bitNum = nrg;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			//for ( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != leftTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subLists[nn] = list;
				qti->m_bigramFlags[nn] = BF_HALFSTOPWIKIBIGRAM;
				qti->m_bigramFlags[nn] |= BF_SYNONYM;
				if (qt->m_piped)
					qti->m_bigramFlags[nn]|=BF_PIPED;
				// add list of member terms as well
				//qti->m_qtermList[nn] = bt;
				bt->m_bitNum = nrg;
				if ( list && list->m_listSize ) nn++;
			}

		}
		//
		// then the right bigram if also in a wiki half stop bigram
		//
		if ( right>=0 &&rightTerm &&rightTerm->m_isWikiHalfStopBigram){
			// assume added
			rightAlreadyAdded = true;
			// get list
			//list = m_msg2->getList(right);
			list = m_q->m_qterms[right].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subLists[nn] = list;
			// right bigram is #3
			//bigramSet[nrg][nn] = 3;
			// special flags
			qti->m_bigramFlags[nn] = BF_HALFSTOPWIKIBIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_bigramFlags[nn] |= BF_PIPED;
			// add list of member terms as well
			//qti->m_qtermList[nn] = &m_q->m_qterms[right];
			m_q->m_qterms[right].m_bitNum = nrg;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			//for (int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != rightTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subLists[nn] = list;
				qti->m_bigramFlags[nn] = BF_HALFSTOPWIKIBIGRAM;
				qti->m_bigramFlags[nn] |= BF_SYNONYM;
				if (qt->m_piped)
					qti->m_bigramFlags[nn]|=BF_PIPED;
				// add list of member terms as well
				//qti->m_qtermList[nn] = bt;
				bt->m_bitNum = nrg;
				if ( list && list->m_listSize ) nn++;
			}

		}
		//
		// then the non-bigram termlist
		//
		// add to it. add backwards since we give precedence to
		// the first list and we want that to be the NEWEST list!
		//list = m_msg2->getList(i);
		list = m_q->m_qterms[i].m_posdbListPtr;
		// add list ptr into our required group
		qti->m_subLists[nn] = list;
		// how many in there?
		//int32_t count = m_msg2->getNumListsInGroup(left);
		// base term is #1
		//bigramSet[nrg][nn] = 1;
		// special flags
		qti->m_bigramFlags[nn] = 0;
		// before a pipe operator?
		if ( qt->m_piped ) qti->m_bigramFlags[nn] |= BF_PIPED;
		// is it a negative term?
		if ( qt->m_termSign=='-')qti->m_bigramFlags[nn]|=BF_NEGATIVE; 

		// numeric posdb termlist flags. instead of word position
		// they have a float stored there for sorting etc.
		if (qt->m_fieldCode == FIELD_GBSORTBYFLOAT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBREVSORTBYFLOAT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBNUMBERMIN )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBNUMBERMAX )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBNUMBEREQUALFLOAT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;

		if (qt->m_fieldCode == FIELD_GBSORTBYINT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBREVSORTBYINT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBNUMBERMININT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBNUMBERMAXINT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		if (qt->m_fieldCode == FIELD_GBNUMBEREQUALINT )
			qti->m_bigramFlags[nn]|=BF_NUMBER;
		//if (qt->m_fieldCode == FIELD_GBFIELDMATCH )
		//	qti->m_bigramFlags[nn]|=BF_NUMBER;


		if (qt->m_fieldCode == FIELD_GBFACETSTR )
			qti->m_bigramFlags[nn]|=BF_FACET;
		if (qt->m_fieldCode == FIELD_GBFACETINT )
			qti->m_bigramFlags[nn]|=BF_FACET;
		if (qt->m_fieldCode == FIELD_GBFACETFLOAT )
			qti->m_bigramFlags[nn]|=BF_FACET;

		// add list of member terms
		//qti->m_qtermList[nn] = qt;
		qt->m_bitNum = nrg;

		// only really add if useful
		// no, because when inserting NEW (related) terms that are
		// not currently in the document, this list may initially
		// be empty.
		if ( list && list->m_listSize ) nn++;
		// 
		// add left bigram now if not added above
		//
		if ( left>=0 && ! leftAlreadyAdded ) {
			// get list
			//list = m_msg2->getList(left);
			list = m_q->m_qterms[left].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subLists[nn] = list;
			// left bigram is #2
			//bigramSet[nrg][nn] = 2;
			// special flags
			qti->m_bigramFlags[nn] = 0;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_bigramFlags[nn] |= BF_PIPED;
			// call it a synonym i guess
			qti->m_bigramFlags[nn] |= BF_BIGRAM;
			// add list of member terms
			//qti->m_qtermList[nn] = &m_q->m_qterms[left];
			m_q->m_qterms[left].m_bitNum = nrg;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			//for( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != leftTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subLists[nn] = list;
				qti->m_bigramFlags[nn] = BF_SYNONYM;
				if (qt->m_piped)
					qti->m_bigramFlags[nn]|=BF_PIPED;
				// add list of member terms
				//qti->m_qtermList[nn] = bt;
				bt->m_bitNum = nrg;
				if ( list && list->m_listSize ) nn++;
			}

		}
		// 
		// add right bigram now if not added above
		//
		if ( right>=0 && ! rightAlreadyAdded ) {
			// get list
			//list = m_msg2->getList(right);
			list = m_q->m_qterms[right].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subLists[nn] = list;
			// right bigram is #3
			//bigramSet[nrg][nn] = 3;
			// special flags
			qti->m_bigramFlags[nn] = 0;
			// call it a synonym i guess
			qti->m_bigramFlags[nn] |= BF_BIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_bigramFlags[nn] |= BF_PIPED;
			// add list of query terms too that are in this group
			//qti->m_qtermList[nn] = &m_q->m_qterms[right];
			m_q->m_qterms[right].m_bitNum = nrg;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			//for (int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != rightTerm ) continue;
				//list = m_msg2->getList(k);
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subLists[nn] = list;
				qti->m_bigramFlags[nn] = BF_SYNONYM;
				if (qt->m_piped)
					qti->m_bigramFlags[nn]|=BF_PIPED;
				// add list of member terms
				//qti->m_qtermList[nn] = bt;
				bt->m_bitNum = nrg;
				if ( list && list->m_listSize ) nn++;
			}

		}

		//
		// ADD SYNONYM TERMS
		//
		//for ( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			QueryTerm *qt2 = &m_q->m_qterms[k];
			QueryTerm *st = qt2->m_synonymOf;
			// skip if not a synonym of this term
			if ( st != qt ) continue;
			// its a synonym, add it!
			//list = m_msg2->getList(k);
			list = m_q->m_qterms[k].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subLists[nn] = list;
			// special flags
			qti->m_bigramFlags[nn] = BF_SYNONYM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_bigramFlags[nn] |= BF_PIPED;
			// add list of member terms as well
			//qti->m_qtermList[nn] = qt2;
			// set bitnum here i guess
			qt2->m_bitNum = nrg;
			// only really add if useful
			if ( list && list->m_listSize ) nn++;
		}


		// empty implies no results!!!
		//if ( nn == 0 && qt->m_termSign != '-' ) {
		//	//log("query: MISSING REQUIRED TERM IN QUERY!");
		//	return;
		//}

		// store # lists in required group. nn might be zero!
		qti->m_numSubLists = nn;
		// set the term freqs for this list group/set
		qti->m_termFreqWeight =((float *)m_r->ptr_termFreqWeights)[i];
		// crazy?
		if ( nn >= MAX_SUBLISTS ) {
			log("query: too many sublists. %"INT32" >= %"INT32"",
			    nn,(int32_t)MAX_SUBLISTS);
			return false;
			char *xx=NULL; *xx=0; 
		}
		
		// compute m_totalSubListsSize
		qti->m_totalSubListsSize = 0LL;
		for ( int32_t q = 0 ; q < qti->m_numSubLists ; q++ ) {
			// add list ptr into our required group
			RdbList *list = qti->m_subLists[q];
			// set end ptr
			//qti->m_subListEnds[q]=list->m_list +list->m_listSize;
			// get it
			int64_t listSize = list->getListSize();
			// add it up
			qti->m_totalSubListsSize += listSize;
		}
		
		// count # required groups
		nrg++;
	}

	//
	// now set QueryTerm::m_bitNum for use by Expression::isTruth()
	// in Query.cpp for boolean queries, so we can get the bit vector
	// of a docid that is 1-1 with the queryterminfos and see which
	// query words in the boolean expression it contains.
	// used by matchesBoolQuery() which we call below.
	//
	/*
	for ( int32_t i = 0 ; i < nrg ; i++ ) {
		// get one
		QueryTermInfo *qti = &qip[i];
		// how many query terms are in this group?
		for ( int32_t j = 0 ; j < qti->m_numSubLists ; j++ ) {
			// get the query term
			QueryTerm *qt = qti->m_qtermList[j];
			// set the bit num member
			qt->m_bitNum = i;
		}
	}
	*/


	//
	// get the query term with the least data in posdb including syns
	//
	m_minListSize = 0;
	m_minListi    = -1;
	int64_t grand = 0LL;
	// hopefully no more than 100 sublists per term
	//char *listEnds  [ MAX_QUERY_TERMS ][ MAX_SUBLISTS ];
	// set ptrs now i guess
	for ( int32_t i = 0 ; i < nrg ; i++ ) {
		// compute total sizes
		int64_t total = 0LL;
		// get it
		QueryTermInfo *qti = &qip[i];
		// do not consider for first termlist if negative
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;
		// add to it
		total = qti->m_totalSubListsSize;
		// add up this now
		grand += total;
		// get min
		if ( total < m_minListSize || m_minListi == -1 ) {
			m_minListSize = total;
			m_minListi    = i;
		}
	}

	// bad! ringbuf[] was not designed for this nonsense!
	if ( m_minListi >= 255 ) { char *xx=NULL;*xx=0; }
	
	// set this for caller to use to loop over the queryterminfos
	m_numQueryTermInfos = nrg;

	// . m_minListSize is set in setQueryTermInfo()
	// . how many docids do we have at most in the intersection?
	// . all keys are of same termid, so they are 12 or 6 bytes compressed
	// . assume 12 if each is a different docid
	int32_t maxDocIds = m_minListSize / 12;
	// store all interesected docids in here for new algo plus 1 byte vote
	int32_t need = maxDocIds * 6;

	// they could all be OR'd together!
	if ( m_q->m_isBoolean ) need = grand;

	// so we can always cast a int64_t from a ptr in there
	// for setting m_docId when m_booleanQuery is true below
	need += 8;

	// get max # of docids we got in an intersection from all the lists
	if ( ! m_docIdVoteBuf.reserve ( need,"divbuf" ) ) return false;

	// i'm feeling if a boolean query put this in there too, the
	// hashtable that maps each docid to its boolean bit vector
	// where each bit stands for an operand so we can quickly evaluate
	// the bit vector in a truth table.
	// CRAP, can't use min list size because it might be behind a
	// NOT operator!!! then we core trying to realloc m_bt in a thread
	// below when trying to grow it. they could all be OR'd together
	// so alloc the most!
	int32_t maxSlots = (grand/12) * 2;
	// try to speed up. this doesn't *seem* to matter, so i took out:
	//maxSlots *= 2;
	// get total operands we used
	//int32_t numOperands = m_q->m_numWords;//Operands;
	// a quoted phrase counts as a single operand
	// . QueryTerm::m_bitNum <== m_numQueryTermInfos
	// . each queryTermInfo class corresponds to one bit in our bit vec
	// . essentially each queryTermInfo is a query term, but it has
	//   all the synonym and word forms for that query, etc.
	m_vecSize = m_numQueryTermInfos / 8;//numOperands / 8 ;
	// allow an extra byte for remainders
	if ( m_numQueryTermInfos % 8 ) m_vecSize++;
	// now preallocate the hashtable. 0 niceness.
	if ( m_q->m_isBoolean &&  // true = useKeyMagic
	     ! m_bt.set (8,m_vecSize,maxSlots,NULL,0,false,0,"booltbl",true))
		return false;
	// . m_ct maps a boolean "bit vector" to a true/false value
	// . each "bit" in the "bit vector" indicates if docid has that 
	//   particular query term
	if ( m_q->m_isBoolean && // true = useKeyMagic
	     ! m_ct.set (8,1,maxSlots,NULL,0,false,0,
			 "booltbl",true))
		return false;

	return true;
}

void PosdbTable::rmDocIdVotes ( QueryTermInfo *qti ) {
	// int16_tcut
	char *bufStart = m_docIdVoteBuf.getBufStart();

	register char *dp = NULL;
	register char *dpEnd;
	register char *recPtr     ;
	char          *subListEnd ;

	// just scan each sublist vs. the docid list
	for ( int32_t i = 0 ; i < qti->m_numSubLists  ; i++ ) {
		// get that sublist
		recPtr     = qti->m_subLists[i]->getList();
		subListEnd = qti->m_subLists[i]->getListEnd();
		// reset docid list ptrs
		dp    =      m_docIdVoteBuf.getBufStart();
		dpEnd = dp + m_docIdVoteBuf.length();
		// loop it
	subLoop:
		// scan for his docids and inc the vote
		for ( ; dp < dpEnd ; dp += 6 ) {
			// if current docid in docid list is >= the docid
			// in the sublist, stop. docid in list is 6 bytes and
			// recPtr must be pointing to a 12 byte posdb rec.
			if ( *(uint32_t *)(dp+1) >
			     *(uint32_t *)(recPtr+8) ) 
				break;
			// less than? keep going
			if ( *(uint32_t *)(dp+1) <
			     *(uint32_t *)(recPtr+8) ) 
				continue;
			// top 4 bytes are equal. check lower single byte then.
			if ( *(unsigned char *)(dp) >
			     (*(unsigned char *)(recPtr+7) & 0xfc ) )
				break;
			if ( *(unsigned char *)(dp) <
			     (*(unsigned char *)(recPtr+7) & 0xfc ) )
				continue;
			// . equal! mark it as nuked!
			dp[5] = -1;//listGroupNum;
			// skip it
			dp += 6;
			// advance recPtr now
			break;
		}
		// if we've exhausted this docid list go to next sublist
		if ( dp >= dpEnd ) continue;
		// skip that docid record in our termlist. it MUST have been
		// 12 bytes, a docid heading record.
		recPtr += 12;
		// skip any following keys that are 6 bytes, that means they
		// share the same docid
		for ( ; recPtr < subListEnd && ((*recPtr)&0x04); recPtr += 6 );
		// if we have more posdb recs in this sublist, then keep
		// adding our docid votes into the docid list
		if ( recPtr < subListEnd ) goto subLoop;
		// otherwise, advance to next sublist
	}

	// now remove docids with a 0xff vote, they are nuked
	dp    =      m_docIdVoteBuf.getBufStart();
	dpEnd = dp + m_docIdVoteBuf.length();
	register char *dst   = dp;
	for ( ; dp < dpEnd ; dp += 6 ) {
		// do not re-copy it if it was in this negative termlist
		if ( dp[5] == -1 ) continue;
		// copy it over. might be the same address!
		*(int32_t  *) dst    = *(int32_t *)  dp;
		*(int16_t *)(dst+4) = *(int16_t *)(dp+4);
		dst += 6;
	}
	// shrink the buffer size now
	m_docIdVoteBuf.setLength ( dst - bufStart );
	return;

}
// for boolean queries containing terms like gbmin:offerprice:190
inline bool isInRange( char *p , QueryTerm *qt ) {

	// return false if outside of range
	if ( qt->m_fieldCode == FIELD_GBNUMBERMIN ) {
		float score2 = g_posdb.getFloat ( p );
		return ( score2 >= qt->m_qword->m_float );
	}

	if ( qt->m_fieldCode == FIELD_GBNUMBERMAX ) {
		float score2 = g_posdb.getFloat ( p );
		return ( score2 <= qt->m_qword->m_float );
	}

	if ( qt->m_fieldCode == FIELD_GBNUMBEREQUALFLOAT ) {
		float score2 = g_posdb.getFloat ( p );
		return ( score2 == qt->m_qword->m_float );
	}

	if ( qt->m_fieldCode == FIELD_GBNUMBERMININT ) {
		int32_t score2 = g_posdb.getInt ( p );
		return ( score2 >= qt->m_qword->m_int );
	}

	if ( qt->m_fieldCode == FIELD_GBNUMBERMAXINT ) {
		int32_t score2 = g_posdb.getInt ( p );
		return ( score2 <= qt->m_qword->m_int );
	}

	if ( qt->m_fieldCode == FIELD_GBNUMBEREQUALINT ) {
		int32_t score2 = g_posdb.getInt ( p );
		return ( score2 == qt->m_qword->m_int );
	}

	// if ( qt->m_fieldCode == FIELD_GBFIELDMATCH ) {
	// 	int32_t score2 = g_posdb.getInt ( p );
	// 	return ( score2 == qt->m_qword->m_int );
	// }

	// how did this happen?
	char *xx=NULL;*xx=0;
	return true;
}
		
inline bool isInRange2 ( char *recPtr , char *subListEnd, QueryTerm *qt ) {
	// if we got a range term see if in range.
	if ( isInRange(recPtr,qt) ) return true;
	recPtr += 12;
	for(;recPtr<subListEnd&&((*recPtr)&0x04);recPtr +=6) {
		if ( isInRange(recPtr,qt) ) return true;
	}
	return false;
}

// for a facet
int64_t PosdbTable::countUniqueDocids( QueryTermInfo *qti ) {

	QueryTerm *qt = qti->m_qt;
	HashTableX *ft = &qt->m_facetHashTable;

	// get that sublist. facets should only have one sublist since
	// they have no synonyms.
	char *start = qti->m_subLists[0]->getList();
	register char *recPtr     = start;
	register char *subListEnd = qti->m_subLists[0]->getListEnd();
	int64_t count = 0;
 loop:
	if ( recPtr >= subListEnd ) {
		if ( m_debug )
			log(LOG_DEBUG,"posdb: term list size of %"
			    INT32" has %"INT64" unique docids"
			    , (int32_t)(subListEnd-start),count);
		return count;
	}

	// this is a facet term so get the value bits. they can represent
	// a float32, int32 or stringhash32
	int32_t val32 = g_posdb.getFacetVal32 ( recPtr );
	// now just accumulate in our hash table of vals
	FacetEntry *fe = (FacetEntry *)ft->getValue(&val32);
	// inc the TOTAL val count
	if ( fe ) fe->m_outsideSearchResultsCount++;

	// Increment ptr to the next record
        int32_t recSize = qti->m_subLists[0]->getRecSize(recPtr);
        recPtr += recSize;

        // Records that are 6 bytes share the same doc id, so only increment
        // 'count' if it refers to a record with a new (unique) docId
        if (recSize > 6) count++;
	goto loop;
}

// . add a QueryTermInfo for a term (synonym lists,etc) to the docid vote buf
//   "m_docIdVoteBuf"
// . this is how we intersect all the docids to end up with the winners
void PosdbTable::addDocIdVotes ( QueryTermInfo *qti , int32_t   listGroupNum) {

	// sanity check, we store this in a single byte below for voting
	if ( listGroupNum >= 256 ) { char *xx=NULL;*xx=0; }

	// int16_tcut
	char *bufStart = m_docIdVoteBuf.getBufStart();

	register char *dp = NULL;
	register char *dpEnd;
	register char *recPtr     ;
	char          *subListEnd ;

	// range terms tend to disappear if the docid's value falls outside
	// of the specified range... gbmin:offerprice:190
	bool isRangeTerm = false;
	QueryTerm *qt = qti->m_qt;
	if ( qt->m_fieldCode == FIELD_GBNUMBERMIN ) 
		isRangeTerm = true;
	if ( qt->m_fieldCode == FIELD_GBNUMBERMAX ) 
		isRangeTerm = true;
	if ( qt->m_fieldCode == FIELD_GBNUMBEREQUALFLOAT )
		isRangeTerm = true;
	if ( qt->m_fieldCode == FIELD_GBNUMBERMININT ) 
		isRangeTerm = true;
	if ( qt->m_fieldCode == FIELD_GBNUMBERMAXINT ) 
		isRangeTerm = true;
	if ( qt->m_fieldCode == FIELD_GBNUMBEREQUALINT ) 
		isRangeTerm = true;
	// if ( qt->m_fieldCode == FIELD_GBFIELDMATCH )
	// 	isRangeTerm = true;

	// . just scan each sublist vs. the docid list
	// . a sublist is a termlist for a particular query term, for instance
	//   the query term "jump" will have sublists for "jump" "jumps"
	//   "jumping" "jumped" and maybe even "jumpy", so that could be
	//   5 sublists, and their QueryTermInfo::m_qtermNum should be the
	//   same for all 5.
	// . IFF listGroupNum > 0, we handle first listgroup below, because
	//   the first listGroup is not intersecting, just adding to
	//   the docid vote buf. that is, if the query is "jump car" we
	//   just add all the docids for "jump" and then intersect with the
	//   docids for "car".
	for ( int32_t i = 0 ; i < qti->m_numSubLists && listGroupNum > 0; i++){
		// get that sublist
		recPtr     = qti->m_subLists[i]->getList();
		subListEnd = qti->m_subLists[i]->getListEnd();
		// reset docid list ptrs
		dp    =      m_docIdVoteBuf.getBufStart();
		dpEnd = dp + m_docIdVoteBuf.length();
		// loop it
	subLoop:
		// scan for his docids and inc the vote
		for ( ; dp < dpEnd ; dp += 6 ) {
			// if current docid in docid list is >= the docid
			// in the sublist, stop. docid in list is 6 bytes and
			// recPtr must be pointing to a 12 byte posdb rec.
			if ( *(uint32_t *)(dp+1) >
			     *(uint32_t *)(recPtr+8) ) 
				break;
			// less than? keep going
			if ( *(uint32_t *)(dp+1) <
			     *(uint32_t *)(recPtr+8) ) 
				continue;
			// top 4 bytes are equal. check lower single byte then.
			if ( *(unsigned char *)(dp) >
			     (*(unsigned char *)(recPtr+7) & 0xfc ) )
				break;
			if ( *(unsigned char *)(dp) <
			     (*(unsigned char *)(recPtr+7) & 0xfc ) )
				continue;

			// if we are a range term, does this subtermlist
			// for this docid meet the min/max requirements
			// of the range term, i.e. gbmin:offprice:190.
			// if it doesn't then do not add this docid to the
			// docidVoteBuf, "dp"
			if ( isRangeTerm && ! isInRange2(recPtr,subListEnd,qt))
				break;

			// . equal! record our vote!
			// . we start at zero for the
			//   first termlist, and go to 1, etc.
			dp[5] = listGroupNum;
			// skip it
			dp += 6;

			// advance recPtr now
			break;
		}

		// if we've exhausted this docid list go to next sublist
		// since this docid is NOT in the current/ongoing intersection
		// of the docids for each queryterm
		if ( dp >= dpEnd ) continue;

		// skip that docid record in our termlist. it MUST have been
		// 12 bytes, a docid heading record.
		recPtr += 12;

		// skip any following keys that are 6 bytes, that means they
		// share the same docid
		for ( ; recPtr < subListEnd && ((*recPtr)&0x04); recPtr += 6 );
		// if we have more posdb recs in this sublist, then keep
		// adding our docid votes into the docid list
		if ( recPtr < subListEnd ) goto subLoop;
		// otherwise, advance to next sublist
	}

	// . all done if not the first group of sublists
	// . shrink the docid list then
	if ( listGroupNum > 0 ) {
		// ok, shrink the docidbuf by removing docids with not enough 
		// votes which means they are missing a query term
		dp    =      m_docIdVoteBuf.getBufStart();
		dpEnd = dp + m_docIdVoteBuf.length();
		register char *dst   = dp;
		for ( ; dp < dpEnd ; dp += 6 ) {
			// skip if it has enough votes to be in search 
			// results so far
			if ( dp[5] != listGroupNum ) continue;
			// copy it over. might be the same address!
			*(int32_t  *) dst    = *(int32_t *)  dp;
			*(int16_t *)(dst+4) = *(int16_t *)(dp+4);
			dst += 6;
		}
		// shrink the buffer size now
		m_docIdVoteBuf.setLength ( dst - bufStart );
		return;
	}

	//
	// OTHERWISE add the first sublist's docids into the docid buf!!!!
	//

	// cursors
	char *cursor[MAX_SUBLISTS];
	char *cursorEnd[MAX_SUBLISTS];
	for ( int32_t i = 0 ; i < qti->m_numSubLists ; i++ ) {
		// get that sublist
		cursor    [i] = qti->m_subLists[i]->getList();
		cursorEnd [i] = qti->m_subLists[i]->getListEnd();
	}

	// reset docid list ptrs
	dp = m_docIdVoteBuf.getBufStart();
	char *minRecPtr;
	char *lastMinRecPtr = NULL;
	int32_t mini = -1;

 getMin:

	// reset this
	minRecPtr = NULL;

	// just scan each sublist vs. the docid list
	for ( int32_t i = 0 ; i < qti->m_numSubLists ; i++ ) {
		// skip if exhausted
		if ( ! cursor[i] ) continue;
		// shortcut
		recPtr = cursor[i];
		// get the min docid
		if ( ! minRecPtr ) {
			minRecPtr = recPtr;
			mini = i;
			continue;
		}
		// compare!
		if ( *(uint32_t *)(recPtr   +8) >
		     *(uint32_t *)(minRecPtr+8) )
			continue;
		// a new min
		if ( *(uint32_t *)(recPtr   +8) <
		     *(uint32_t *)(minRecPtr+8) ) {
			minRecPtr = recPtr;
			mini = i;
			continue;
		}
		// check lowest byte
		if ( (*(unsigned char *)(recPtr   +7) & 0xfc ) >
		     (*(unsigned char *)(minRecPtr+7) & 0xfc ) )
			continue;
		// a new min
		if ( (*(unsigned char *)(recPtr   +7) & 0xfc ) <
		     (*(unsigned char *)(minRecPtr+7) & 0xfc ) ) {
			minRecPtr = recPtr;
			mini = i;
			continue;
		}
	}

	// if no min then all lists exhausted!
	if ( ! minRecPtr ) {
		// update length
		m_docIdVoteBuf.setLength ( dp - bufStart );
		// all done!
		return;
	}

	bool inRange;

	// if we are a range term, does this subtermlist
	// for this docid meet the min/max requirements
	// of the range term, i.e. gbmin:offprice:190.
	// if it doesn't then do not add this docid to the
	// docidVoteBuf, "dp"
	if ( isRangeTerm ) {
		// a new docid i guess
		inRange = false;
		// no longer in range
		if ( isInRange2(cursor[mini],cursorEnd[mini],qt))
			inRange = true;
	}
		

	// advance that guy over that docid
	cursor[mini] += 12;
	// 6 byte keys follow?
	for ( ; ; ) {
		// end of list?
		if ( cursor[mini] >= cursorEnd[mini] ) {
			// use NULL to indicate list is exhausted
			cursor[mini] = NULL;
			break;
		}
		// if we hit a new 12 byte key for a new docid, stop
		if ( ! ( cursor[mini][0] & 0x04 ) ) break;

		// check range again
		if (isRangeTerm && isInRange2(cursor[mini],cursorEnd[mini],qt))
			inRange = true;

		// otherwise, skip this 6 byte key
		cursor[mini] += 6;
	}

	// is it a docid dup?
	if(lastMinRecPtr &&
	   *(uint32_t *)(lastMinRecPtr+8)==
	   *(uint32_t *)(minRecPtr+8)&&
	   (*(unsigned char *)(lastMinRecPtr+7)&0xfc)==
	   (*(unsigned char *)(minRecPtr+7)&0xfc))
		goto getMin;

	// . do not store the docid if not in the whitelist
	// . FIX: two lower bits, what are they? at minRecPtrs[7].
	// . well the lowest bit is the siterank upper bit and the
	//   other bit is always 0. we should be ok with just using
	//   the 6 bytes of the docid ptr as is though since the siterank
	//   should be the same for the site: terms we indexed for the same
	//   docid!!
	if ( m_useWhiteTable && ! m_whiteListTable.isInTable(minRecPtr+7) )
		goto getMin;
		
	if ( isRangeTerm && ! inRange )
		goto getMin;

	// only update this if we add the docid... that way there can be
	// a winning "inRange" term in another sublist and the docid will
	// get added.
	lastMinRecPtr = minRecPtr;

	// store our docid. actually it contains two lower bits not
	// part of the docid, so we'll have to shift and mask to get
	// the actual docid!
	// docid is only 5 bytes for now
	*(int32_t  *)(dp+1) = *(int32_t  *)(minRecPtr+8);
	// the single lower byte
	dp[0] = minRecPtr[7] & 0xfc;
	// 0 vote count
	dp[5] = 0;

	/*
	// debug
	int64_t dd = g_posdb.getDocId(minRecPtr);
	log("posdb: adding docid %"INT64"", dd);
	// test
	uint64_t actualDocId;
	actualDocId = *(uint32_t *)(dp+1);
	actualDocId <<= 8;
	actualDocId |= (unsigned char)dp[0];
	actualDocId >>= 2;
	if (  dd != actualDocId ) { char *xx=NULL;*xx=0; }
	*/

	// advance
	dp += 6;
	// get the next min from all the termlists
	goto getMin;
}

void PosdbTable::shrinkSubLists ( QueryTermInfo *qti ) {

	// reset count of new sublists
	qti->m_numNewSubLists = 0;

	// scan each sublist vs. the docid list
	for ( int32_t i = 0 ; i < qti->m_numSubLists ; i++ ) {

		// get that sublist
		register char *recPtr     = qti->m_subLists[i]->getList();
		register char *subListEnd = qti->m_subLists[i]->getListEnd();
		// reset docid list ptrs
		register char *dp    =      m_docIdVoteBuf.getBufStart();
		register char *dpEnd = dp + m_docIdVoteBuf.length();

		// re-copy into the same buffer!
		char *dst = recPtr;
		// save it
		char *savedDst = dst;


	subLoop:
		// scan the docid list for the current docid in this termlist
		for ( ; ; dp += 6 ) {
			// no docids in list? no need to skip any more recPtrs!
			if ( dp >= dpEnd ) goto doneWithSubList;
			// if current docid in docid list is >= the docid
			// in the sublist, stop. docid in list is 6 bytes and
			// recPtr must be pointing to a 12 byte posdb rec.
			if ( *(uint32_t *)(dp+1) > 
			     *(uint32_t *)(recPtr+8) )
				break;
			// try to catch up docid if it is behind
			if ( *(uint32_t *)(dp+1) < 
			     *(uint32_t *)(recPtr+8) )
				continue;
			// check lower byte if equal
			if ( *(unsigned char *)(dp) >
			     (*(unsigned char *)(recPtr+7) & 0xfc ) )
				break;
			if ( *(unsigned char *)(dp) <
			     (*(unsigned char *)(recPtr+7) & 0xfc ) )
				continue;
			// copy over the 12 byte key
			*(int64_t *)dst = *(int64_t *)recPtr;
			*(int32_t *)(dst+8) = *(int32_t *)(recPtr+8);
			// skip that 
			dst    += 12;
			recPtr += 12;
			// copy over any 6 bytes keys following
			for ( ; ; ) {
				if ( recPtr >= subListEnd ) 
					// give up on this exhausted term list!
					goto doneWithSubList;
				// next docid willbe next 12 bytekey
				if ( ! ( recPtr[0] & 0x04 ) ) break;
				// otherwise it's 6 bytes
				*(int32_t *)dst = *(int32_t *)recPtr;
				*(int16_t *)(dst+4) = *(int16_t *)(recPtr+4);
				dst += 6;
				recPtr += 6;
			}
			// continue the docid loop for this new recPtr
			continue;
		}

		// skip that docid record in our termlist. it MUST have been
		// 12 bytes, a docid heading record.
		recPtr += 12;
		// skip any following keys that are 6 bytes, that means they
		// share the same docid
		for ( ; ;  ) {
			// list exhausted?
			if ( recPtr >= subListEnd ) goto doneWithSubList;
			// stop if next key is 12 bytes, that is a new docid
			if ( ! (recPtr[0] & 0x04) ) break;
			// skip it
			recPtr += 6;
		}

		// process the next rec ptr now
		goto subLoop;

	doneWithSubList:

		// set sublist end
		int32_t x = qti->m_numNewSubLists;
		qti->m_newSubListSize  [x] = dst - savedDst;
		qti->m_newSubListStart [x] = savedDst;
		qti->m_newSubListEnd   [x] = dst;
		qti->m_cursor          [x] = savedDst;
		qti->m_savedCursor     [x] = savedDst;
		if ( qti->m_newSubListSize [x] ) qti->m_numNewSubLists++;
	}
}

//static int32_t s_sss = 0;
Query *g_q;
// . compare the output of this to intersectLists9_r()
// . hopefully this will be easier to understand and faster
// . IDEAS:
//   we could also note that if a term was not in the title or
//   inlink text it could never beat the 10th score.
void PosdbTable::intersectLists10_r ( ) {

	m_finalScore = 0.0;

	//log("seo: intersecting query %s",m_q->m_orig);

	bool seoHack = false;
	if ( ! m_msg2 ) seoHack = true;

	// if we are just a sitehash:xxxxx list and m_getSectionStats is
	// true then assume the list is one of hacked posdb keys where
	// the wordposition bits and others are really a 32-bit site hash
	// and we have to see how many different docids and sites have
	// this term. and we compare to our site hash, 
	// m_r->m_sectionSiteHash32 to determine if the posdb key is
	// onsite or offsite. then XmlDoc::printRainbowSections()
	// can print out how many page/sites duplicate your section's content.
	
	// MDW: TODO: for the facet terms just compile the stats and do not
	// send to intersecting. they are ignored for those purposes. send
	// the hashtable back so msg3a can integrate the stats. keep in mind
	// we have multiple docid ranges sometimes for one query!!!!

	/*

	  MDW: take this out. now treat as a normal termlist but
	  do not use for scoring. so it is kinda like gbmin: gbmax:
	  query operators but it will just add the facet values to
	  QueryTerm::m_facetHashList for transmission back to the aggregator
	  node. however, it is only for docids in the final result set!
	
	if ( m_r->m_getFacetStats ) {
		// reset
		m_facetStats.m_totalMatches = 0;
		m_facetStats.m_totalEntries = 0;
		m_dt.clear();
		// scan the posdb keys
		//for ( int32_t i = 0 ; i < m_msg2->getNumListsInGroup(0); i++) {
		// get the sublist
		RdbList *list = m_msg2->getList(0);//Group(0)[i];
		char *p    =     list->getList    ();
		char *pend = p + list->getListSize();
		// test
		//int64_t final = 5663137686803656554LL;
		//final &= TERMID_MASK;
		//if ( p<pend && g_posdb.getTermId(p) == final )
		//	log("boo");
		// scan it
		for ( ; p < pend ; ) {
			// . first key is the full size
			// . uses the w,G,s,v and F bits to hold this
			// . this is no longer necessarily sitehash, but
			//   can be any val, like now FacetStats is using
			//   it for the innerHtml sentence content hash32
			int32_t sh32 = g_posdb.getFacetVal32 ( p );
			//int64_t d = g_posdb.getDocId(p);
			//int32_t rs = list->getRecSize(p);
			// this will not update listptrlo, watch out!
			p += list->getRecSize ( p );
			// does this xpath from another docid have the
			// same inner html as us?
			if ( sh32 == m_r->m_myFacetVal32 ) // m_siteHash32 ) 
				m_facetStats.m_totalMatches++;
			// always this
			m_facetStats.m_totalEntries++;
			// unique site count
			if ( m_dt.isInTable ( &sh32 ) ) continue;
			// count it
			m_facetStats.m_numUniqueVals++;
			// only once
			m_dt.addKey ( &sh32 );
			// log it
			//log("usite: %08"XINT32" %"INT64" rs=%"INT32"",sh32,d,rs);
			// stop if too much so we do not try to 
			// re-alloc in a thread!
			if ( m_dt.m_numSlotsUsed >= 1000000 ) break;
		}
		// and return the list of merging
		int32_t *s    = (int32_t *)m_facetHashList.getBufStart();
		int32_t *send = (int32_t *)m_facetHashList.getBufEnd();
		//if ( m_facetStats.m_numUniqueSites == 17 ) { 
		//	log("q=%s",m_r->ptr_query);
		//	log("hey");
		//	//char *xx = NULL;*xx=0; 
		//}
		//if(!strcmp(m_r->ptr_query,"gbsectionhash:3335323672699668766"
		//	log("boo");
		int32_t *orig = s;
		for ( int32_t i = 0 ; i < m_dt.m_numSlots ; i++ ) {
			if ( ! m_dt.m_flags[i] ) continue;
			*s++ = *(int32_t *)m_dt.getKeyFromSlot(i);
			if ( s >= send ) break;
		}
		m_facetHashList.setLength((char *)s-(char *)orig);
		return;
	}
	*/

	//
	// hash the docids in the whitelist termlists into a hashtable.
	// every docid in the search results must be in there. the
	// whitelist termlists are from a provided "&sites=abc.com+xyz.com+.."
	// cgi parm. the user only wants search results returned from the
	// specified subdomains. there can be up to MAX_WHITELISTS (500)
	// sites right now. this hash table must have been pre-allocated
	// in Posdb::allocTopTree() above since we might be in a thread.
	//
	RdbList *whiteLists = NULL;
	int32_t nw = 0;
	if ( m_msg2 ) {
		whiteLists = m_msg2->m_whiteLists;
		nw = m_msg2->m_w;
	}
	for ( int32_t i = 0 ; ! m_addedSites && i < nw ; i++ ) {
		RdbList *list = &whiteLists[i];
		if ( list->isEmpty() ) continue;
		// sanity test
		int64_t d1 = g_posdb.getDocId(list->getList());
		if ( d1 > m_msg2->m_docIdEnd ) { 
			log("posdb: d1=%"INT64" > %"INT64"",
			    d1,m_msg2->m_docIdEnd);
			//char *xx=NULL;*xx=0; 
		}
		if ( d1 < m_msg2->m_docIdStart ) { 
			log("posdb: d1=%"INT64" < %"INT64"",
			    d1,m_msg2->m_docIdStart);
			//char *xx=NULL;*xx=0; 
		}
		// first key is always 18 bytes cuz it has the termid
		// scan recs in the list
		for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
			char *rec = list->getCurrentRec();
			// point to the 5 bytes of docid
			m_whiteListTable.addKey ( rec + 7 );
		}
	}


	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		QueryTerm *qt = &m_q->m_qterms[i];
		if ( qt->m_fieldCode != FIELD_GBFACETSTR &&
		     qt->m_fieldCode != FIELD_GBFACETINT &&
		     qt->m_fieldCode != FIELD_GBFACETFLOAT )
			continue;

		QueryWord *qw = qt->m_qword;

		//
		// if first time init facet hash table so empty facet ranges
		// will still print out but with 0 documents
		//

		// skip if we already initialized it from a previous docid range phase
		// so we do not reset the data between phases!!!
		HashTableX *ft = &qt->m_facetHashTable;

		if ( ft->m_numSlotsUsed > 0 ) continue;

		log("posdb: init %i facet ranges for query term #%i",
		    (int)qw->m_numFacetRanges,(int)i);

		// prevent core
		if ( ft->m_numSlots == 0 ) {
			log("posdb: facet table for term #%i is uninit",i);
			continue;
		}

		for ( int32_t k = 0 ; k < qw->m_numFacetRanges ; k ++ ) {
			FacetEntry *fe;
			if ( qw->m_fieldCode == FIELD_GBFACETFLOAT ) {
				float val32 = qw->m_facetRangeFloatA[k];
				fe=(FacetEntry *)ft->getValue(&val32);
				FacetEntry ff;
				memset ( &ff , 0 , sizeof(FacetEntry) );
				// ff.m_count = 0;
				// ff.m_docId = m_docId;
				ft->addKey(&val32,&ff);
				continue;
			}
			else {
				// and for int facets
				int32_t val32 = qw->m_facetRangeIntA[k];
				fe=(FacetEntry *)ft->getValue(&val32);
				FacetEntry ff;
				memset ( &ff , 0 , sizeof(FacetEntry) );
				// ff.m_count = 0;
				// ff.m_docId = m_docId;
				ft->addKey(&val32,&ff);
				continue;
			}
		}
		//
		// end init of facet hash table
		//
	}



	m_addedSites = true;



	initWeights();

	g_this = this;

	// clear, set to ECORRUPTDATA below
	m_errno = 0;

	// assume no-op
	m_t1 = 0LL;

	// set start time
	int64_t t1 = gettimeofdayInMilliseconds();

	int64_t lastTime = t1;

	// assume we return early
	m_addListsTime = 0;

	//for ( int32_t i = 0 ; i < m_numSubLists ; i++ ) 
	//	m_lists[i].checkList_r(false,false,RDB_POSDB);


	// . now swap the top 12 bytes of each list
	// . this gives a contiguous list of 6-byte score/docid entries
	//   because the first record is always 12 bytes and the rest are
	//   6 bytes (with the half bit on) due to our termid compression
	// . this makes the lists much much easier to work with, but we have
	//   to remember to swap back when done!
	//for ( int32_t k = 0 ; k < m_msg2->getNumLists() ; k++ ) {
	// now we only do this if m_msg2 is valid, because we do this
	// ahead of time in seo.cpp which sets msg2 to NULL. so skip in that
	// case.
	for ( int32_t k = 0 ; ! seoHack && k < m_q->m_numTerms ; k++ ) {
		// count
		int64_t total = 0LL;
		// loop over each list in this group
		//for ( int32_t i = 0 ; i < m_msg2->getNumListsInGroup(k); i++ ) {
		// get the list
		//RdbList *list = m_msg2->getListGroup(k)[i];
		//RdbList *list = m_msg2->getList(k);
		RdbList *list = m_q->m_qterms[k].m_posdbListPtr;
		// skip if null
		if ( ! list ) continue;
		// skip if list is empty, too
		if ( list->isEmpty() ) continue;
		// tally
		total += list->m_listSize;
		// point to start
		char *p = list->m_list;
		// remember to swap back when done!!
		char ttt[12];
		gbmemcpy ( ttt   , p       , 12 );
		gbmemcpy ( p     , p + 12 , 6   );
		gbmemcpy ( p + 6 , ttt     , 12 );
		// point to the low "hks" bytes now
		p += 6;
		// turn half bit on. first key is now 12 bytes!!
		*p |= 0x02;
		// MANGLE the list
		list->m_listSize -= 6;
		list->m_list      = p;
		// print total list sizes
		if ( ! m_debug ) continue;
		log("query: termlist #%"INT32" totalSize=%"INT64"",k,total);
	}

	//static int32_t s_special = 0;
	//if ( s_special == 2836 )
	//	log("hey");

	// point to our array of query term infos set in setQueryTermInfos()
	QueryTermInfo *qip = (QueryTermInfo *)m_qiBuf.getBufStart();

	// if a query term is for a facet (ie gbfacetstr:gbtagsite)
	// then count how many unique docids are in it. we were trying to 
	// do this in addDocIdVotes() but it wasn't in the right place i guess.
	// for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
	// 	QueryTermInfo *qti = &qip[i];
	// 	QueryTerm *qt = qti->m_qt;
	// 	bool isFacetTerm = false;
	// 	if ( qt->m_fieldCode == FIELD_GBFACETSTR ) isFacetTerm = true;
	// 	if ( qt->m_fieldCode == FIELD_GBFACETINT ) isFacetTerm = true;
	// 	if ( qt->m_fieldCode == FIELD_GBFACETFLOAT ) isFacetTerm =true;
	// 	if ( ! isFacetTerm ) continue;
	// 	qt->m_numDocsThatHaveFacet += countUniqueDocids ( qti );
	// }


	// setQueryTermInfos() should have set how many we have
	if ( m_numQueryTermInfos == 0 ) {
		log("query: NO REQUIRED TERMS IN QUERY2!");
		return;
	}

	// . if smallest required list is empty, 0 results
	// . also set in setQueryTermInfo
	if ( m_minListSize == 0 && ! m_q->m_isBoolean ) return;

	/*
	for ( int32_t k = 0 ; seoHack && k < m_q->m_numTerms ; k++ ) {
		// count
		int64_t total = 0LL;
		RdbList *list = m_q->m_qterms[k].m_posdbListPtr;
		// skip if null
		if ( ! list ) continue;
		// skip if list is empty, too
		if ( list->isEmpty() ) continue;
		// test it
		char *p = list->m_list;
		// you must provide termlists that start with 12 byte key
		// for the seo hack! this is for scoring individual docids
		if ( g_posdb.getKeySize(p) != 12 ) { char *xx=NULL;*xx=0;}
	}
	*/

	int64_t now;
	int64_t took;
	int32_t phase = 1;

	int32_t listGroupNum = 0;


	// if all non-negative query terms are in the same wikiphrase then
	// we can apply the WIKI_WEIGHT in getMaxPossibleScore() which
	// should help us speed things up!
	m_allInSameWikiPhrase = true;
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo *qti = &qip[i];
		// skip if negative query term
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;
		// skip if numeric field like gbsortby:price gbmin:price:1.23
		if ( qti->m_bigramFlags[0] & BF_NUMBER ) continue;
		if ( qti->m_bigramFlags[0] & BF_FACET  ) continue;
		// set it
		if ( qti->m_wikiPhraseId == 1 ) continue;
		// stop
		m_allInSameWikiPhrase = false;
		break;
	}
	
	// if doing a special hack for seo.cpp and just computing the score
	// for one docid...
	// we need this i guess because we have to do the minimerges
	// to merge synlists together otherwise 'advance+search' query
	// fails to find results even though gigablast.com has
	// 'advanced search' on the page because advanced is a syn of advance.
	//if ( ! m_msg2 ) goto seoHackSkip;


	// for boolean queries we scan every docid in all termlists,
	// then we see what query terms it has, and make a bit vector for it.
	// then use a hashtable to map that bit vector to a true or false
	// as to whether we should include it in the results or not.
	// we use Query::getBitScore(qvec_t ebits) to evaluate a docid's
	// query term explicit term bit vector.
	if ( m_q->m_isBoolean ) {
		// keeping the docids sorted is the challenge here...
		makeDocIdVoteBufForBoolQuery_r();
		goto skip3;
	}

	// . create "m_docIdVoteBuf" filled with just the docids from the
	//   smallest group of sublists 
	// . m_minListi is the queryterminfo that had the smallest total
	//   sublist sizes of m_minListSize. this was set in 
	//   setQueryTermInfos()
	// . if all these sublist termlists were 50MB i'd day 10-25ms to
	//   add their docid votes.
	addDocIdVotes ( &qip[m_minListi] , listGroupNum );

	/*
	// test docid buf
	char *xdp = m_docIdVoteBuf.getBufStart();
	char *xdpEnd = xdp + m_docIdVoteBuf.length();
	for ( ; xdp < xdpEnd ; xdp += 6 ) {
		uint64_t actualDocId;
		actualDocId = *(uint32_t *)(xdp+1);
		actualDocId <<= 8;
		actualDocId |= (unsigned char)xdp[0];
		actualDocId >>= 2;
		log("posdb: intact docid %"INT64"",actualDocId);
	}
	*/

	// now repeat the docid scan for successive lists but only
	// inc the docid count for docids we match. docids that are 
	// in m_docIdVoteBuf but not in sublist group #i will be removed
	// from m_docIdVoteBuf. worst case scenario with termlists limited
	// to 30MB will be about 10MB of docid vote buf, but it should
	// shrink quite a bit every time we call addDocIdVotes() with a 
	// new group of sublists for a query term. but scanning 10MB should
	// be pretty fast since gk0 does like 4GB/s of main memory reads.
	// i would think scanning and docid voting for 200MB of termlists 
	// should take like 50-100ms
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// skip if we did it above
		if ( i == m_minListi ) continue;
		// get it
		QueryTermInfo *qti = &qip[i];
		// do not consider for adding if negative ('my house -home')
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;
		// inc this
		listGroupNum++;
		// if it hits 256 then wrap back down to 1
		if ( listGroupNum >= 256 ) listGroupNum = 1;
		// add it
		addDocIdVotes ( qti , listGroupNum );
	}


	// remove the negative query term's docids from our docid vote buf
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// skip if we did it above
		if ( i == m_minListi ) continue;
		// get it
		QueryTermInfo *qti = &qip[i];
		// do not consider for adding if negative ('my house -home')
		if ( ! (qti->m_bigramFlags[0] & BF_NEGATIVE) ) continue;
		// add it
		rmDocIdVotes ( qti );
	}

	/*
	// test docid buf
	xdp = m_docIdVoteBuf.getBufStart();
	xdpEnd = xdp + m_docIdVoteBuf.length();
	for ( ; xdp < xdpEnd ; xdp += 6 ) {
		uint64_t actualDocId;
		actualDocId = *(uint32_t *)(xdp+1);
		actualDocId <<= 8;
		actualDocId |= (unsigned char)xdp[0];
		actualDocId >>= 2;
		log("posdb: intact docid %"INT64"",actualDocId);
	}
	*/

 skip3:

	if ( m_debug ) {
		now = gettimeofdayInMilliseconds();
		took = now - lastTime;
		log("posdb: new algo phase %"INT32" took %"INT64" ms", phase,took);
		lastTime = now;
		phase++;
	}

	/*
	// NOW REMOVED DOCIDS from m_docIdBuf if in a negative termlist
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// do not consider for first termlist if negative
		if ( ! ( bigramFlags[i][0] & BF_NEGATIVE ) ) continue;
		// remove docid votes for all docids in this
		removeDocIdVotes ( requiredGroup       [i] , 
				   listEnds            [i] , 
				   //numRequiredSubLists [i] );
				   // only do exact matches not synonyms!
				   1 );
	}
	*/

	//
	// NOW FILTER EVERY SUBLIST to just the docids in m_docIdVoteBuf.
	// Filter in place so it is destructive. i would think 
	// that doing a filter on 200MB of termlists wouldn't be more than
	// 50-100ms since we can read 4GB/s from main memory.
	//
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo *qti = &qip[i];
		// do not consider for adding if negative ('my house -home')
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;
		// remove docids from each termlist that are not in
		// m_docIdVoteBuf (the intersection)
		shrinkSubLists ( qti );
	}

	if ( m_debug ) {
		now = gettimeofdayInMilliseconds();
		took = now - lastTime;
		log("posdb: new algo phase %"INT32" took %"INT64" ms", phase,took);
		lastTime = now;
		phase++;
	}

	//seoHackSkip:

	//
	// TRANSFORM QueryTermInfo::m_* vars into old style arrays
	//
	// int32_t  wikiPhraseIds  [MAX_QUERY_TERMS];
	// int32_t  quotedStartIds[MAX_QUERY_TERMS];
	// int32_t  qpos           [MAX_QUERY_TERMS];
	// int32_t  qtermNums      [MAX_QUERY_TERMS];
	// float freqWeights    [MAX_QUERY_TERMS];
	// now dynamically allocate to avoid stack smashing
	char     *pp  = m_stackBuf.getBufStart();
	int32_t   nqt = m_q->m_numTerms;
	int32_t  *wikiPhraseIds  = (int32_t *)pp; pp += 4 * nqt;
	int32_t  *quotedStartIds = (int32_t *)pp; pp += 4 * nqt;
	int32_t  *qpos           = (int32_t *)pp; pp += 4 * nqt;
	int32_t  *qtermNums      = (int32_t *)pp; pp += 4 * nqt;
	float    *freqWeights    = (float   *)pp; pp += sizeof(float) * nqt;
	char    **miniMergedList = (char   **)pp; pp += sizeof(char *) * nqt;
	char    **miniMergedEnd  = (char   **)pp; pp += sizeof(char *) * nqt;
	char    **bestPos        = (char   **)pp; pp += sizeof(char *) * nqt;
	char    **winnerStack    = (char   **)pp; pp += sizeof(char *) * nqt;
	char    **xpos           = (char   **)pp; pp += sizeof(char *) * nqt;
	char     *bflags         = (char    *)pp; pp += sizeof(char) * nqt;
	float    *scoreMatrix    = (float   *)pp; pp += sizeof(float) *nqt*nqt;
	if ( pp > m_stackBuf.getBufEnd() ) {char *xx=NULL;*xx=0; }

	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo *qti = &qip[i];
		// set it
		wikiPhraseIds   [i] = qti->m_wikiPhraseId;
		quotedStartIds [i] = qti->m_quotedStartId;
		// query term position
		qpos         [i] = qti->m_qpos;
		qtermNums    [i] = qti->m_qtermNum;
		freqWeights  [i] = qti->m_termFreqWeight;
	}


	// for evalSlidingWindow() function
	//m_freqWeights = (float *)m_r->ptr_termFreqWeights;
	m_freqWeights = freqWeights;
	m_qtermNums   = qtermNums;

	//////////
	//
	// OLD MAIN INTERSECTION LOGIC
	//
	/////////

	bool secondPass = false;
	DocIdScore dcs;
	DocIdScore *pdcs = NULL;
	int32_t minx =0;
	bool allNull;
	int32_t minPos =0;

	uint64_t lastDocId = 0LL;
	int32_t lastLen = 0;
	char siteRank =0;
	char docLang =0;
	float score;
	int32_t intScore;
	float minScore;
	float minPairScore;
	float minSingleScore;
	//int64_t docId;
	m_bflags = bflags;
	int32_t qdist;
	float wts;
	float pss;
	float maxNonBodyScore;
	// new vars for removing supplanted docid score infos and
	// corresponding pair and single score infos
	char *sx;
	char *sxEnd;
	int32_t pairOffset;
	int32_t pairSize;
	int32_t singleOffset;
	int32_t singleSize;
	// scan the posdb keys in the smallest list
	// raised from 200 to 300,000 for 'da da da' query
	char mbuf[300000];
	char *mptrEnd = mbuf + 299000;
	char *mptr;
	char *docIdPtr;
	char *docIdEnd = m_docIdVoteBuf.getBufStart()+m_docIdVoteBuf.length();
	float minWinningScore = -1.0;
	char *nwp     [MAX_SUBLISTS];
	char *nwpEnd  [MAX_SUBLISTS];
	char  nwpFlags[MAX_SUBLISTS];
	char *lastMptr = NULL;
	int32_t topCursor = -9;
	int32_t numProcessed = 0;
#define RINGBUFSIZE 4096
//#define RINGBUFSIZE 1024
	unsigned char ringBuf[RINGBUFSIZE+10];
	// for overflow conditions in loops below
	ringBuf[RINGBUFSIZE+0] = 0xff;
	ringBuf[RINGBUFSIZE+1] = 0xff;
	ringBuf[RINGBUFSIZE+2] = 0xff;
	ringBuf[RINGBUFSIZE+3] = 0xff;
	//int32_t bestDist[MAX_QUERY_TERMS];
	//int32_t dist;
	//int32_t prevPos = -1;
	unsigned char qt;
	QueryTermInfo *qtx;
	uint32_t wx;
	int32_t fail0 = 0;
	int32_t pass0 = 0;
	int32_t fail = 0;
	int32_t pass = 0;
	int32_t ourFirstPos = -1;

	//char          *cursors        [MAX_SUBLISTS*MAX_QUERY_TERMS];
	//char          *savedCursors   [MAX_SUBLISTS*MAX_QUERY_TERMS];
	//QueryTermInfo *cursorTermInfos[MAX_SUBLISTS*MAX_QUERY_TERMS];
	//int32_t           numCursors = 0;

	// populate the cursors for each sublist

	int32_t nnn = m_numQueryTermInfos;
	if ( ! m_r->m_doMaxScoreAlgo ) nnn = 0;

	// do not do it if we got a gbsortby: field
	if ( m_sortByTermNum >= 0 ) nnn = 0;
	if ( m_sortByTermNumInt >= 0 ) nnn = 0;

	/*
	// skip all this if getting score of just one docid on special
	// posdb termlists that are 6-byte only keys
	// no, because we have to merge syn lists!!!!
	for ( int32_t j = 0 ; ! m_msg2 && j < m_numQueryTermInfos ; j++ ) {
		// get the query term info
		QueryTermInfo *qti = &qip[j];
		// just use the flags from first term i guess
		// NO! this loses the wikihalfstopbigram bit! so we 
		// gotta add that in for the key i guess the same way 
		// we add in the syn bits below!!!!!
		bflags [j] = qti->m_bigramFlags[0];
		// if we have a negative term, skip it
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE )
			// if its empty, that's good!
			continue;
		// get query term #
		int32_t qtn = qti->m_qtermNum;
		// get list
		RdbList *list = m_q->m_qterms[qtn].m_posdbListPtr;
		// skip if list empty
		if ( ! list ) {
			miniMergedList[j] = NULL;
			miniMergedEnd [j] = NULL;
			continue;
		}
		// . because we are special, we only have one 
		//   "sublist", the posdb termlist for this docid, so
		//   there's no need to merge a bunch of lists
		// . assign that. first key is 12 bytes, the others 
		//   are 6
		miniMergedList[j] = list->m_list;
		miniMergedEnd [j] = list->m_listEnd;
		// shit, if it's a synonym, we gotta set the syn bits
		if ( !(nwpFlags[0] & BF_SYNONYM) &&
		     !(nwpFlags[0] & BF_HALFSTOPWIKIBIGRAM) )
			continue;
		// ok, set that 0x02 bit on each key
		char *p = list->m_list;
		char *pend = list->m_listEnd;
		bool firstKey = true;
		for ( ; p < pend ;  ) {
			// set the synbit so we know its a syn of term
			if ( bflags[j] & (BF_BIGRAM|BF_SYNONYM) )
				p[2] |= 0x02;
			// set wiki half stop bigram bit
			if ( bflags[j] & (BF_HALFSTOPWIKIBIGRAM) )
				p[2] |= 0x01;
			// skip to next key
			p += 6;
			if ( ! firstKey ) continue;
			p += 6;
			firstKey = false;
		}
	}
	*/

	// ok, lists should be set now
	if ( ! m_msg2 ) 
		goto seoHackSkip2;
		


 secondPassLoop:

	// reset docid to start!
	docIdPtr = m_docIdVoteBuf.getBufStart();

	// reset QueryTermInfo::m_cursor[] for second pass
	for ( int32_t i = 0 ; secondPass && i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo *qti = &qip[i];
		// skip negative termlists
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;
		// do each sublist
		for ( int32_t j = 0 ; j < qti->m_numNewSubLists ; j++ ) {
			qti->m_cursor      [j] = qti->m_newSubListStart[j];
			qti->m_savedCursor [j] = qti->m_newSubListStart[j];
		}
	}

	//
	// the main loop for looping over each docid
	//
 docIdLoop:

	// is this right?
	if ( docIdPtr >= docIdEnd ) goto done;

	// assume all sublists exhausted for this query term
	//docId = *(int64_t *)docIdPtr;

	// advance for next loop iteration
	//docIdPtr += 6;

	// docid ptr points to 5 bytes of docid shifted up 2
	/*
	int64_t tmpDocId;
	tmpDocId = *(uint32_t *)(docIdPtr+1);
	tmpDocId <<= 8;
	tmpDocId |= (unsigned char)docIdPtr[0];
	tmpDocId >>= 2;
	if ( tmpDocId == 236271972369LL )
		log("hey");
	*/

	// . second pass? for printing out transparency info
	// . skip if not a winner
	if ( secondPass ) {
		// did we get enough score info?
		if ( numProcessed >= m_r->m_docsToGet ) goto done;
		// loop back up here if the docid is from a previous range
	nextNode:
		// this mean top tree empty basically
		if ( topCursor == -1 ) goto done;
		// get the #1 docid/score node #
		if ( topCursor == -9 ) {
			// if our query had a quoted phrase, might have had no
			// docids in the top tree! getHighNode() can't handle
			// that so handle it here
			if ( m_topTree->m_numUsedNodes == 0 ) goto done;
			// otherwise, initialize topCursor
			topCursor = m_topTree->getHighNode();
		}
		// get current node
		TopNode *tn = m_topTree->getNode ( topCursor );
		// advance
		topCursor = m_topTree->getPrev ( topCursor );
		// count how many so we do not exceed requested #
		numProcessed++;
		// int16_tcut
		m_docId = tn->m_docId;
		// skip if not in our range! the top tree now holds
		// all the winners from previous docid ranges. msg39
		// now does the search result in docid range chunks to avoid
		// OOM conditions.
		if ( m_r->m_minDocId != -1 &&
		     m_r->m_maxDocId != -1 &&
		     ( m_docId < (uint64_t)m_r->m_minDocId || 
		       m_docId >= (uint64_t)m_r->m_maxDocId ) ) 
			goto nextNode;
		// set query termlists in all sublists
		for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
			// get it
			QueryTermInfo *qti = &qip[i];
			// do not advance negative termlist cursor
			if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;
			// do each sublist
			for ( int32_t j = 0 ; j < qti->m_numNewSubLists ; j++ ) {
				// get termlist for that docid
				char *xlist    = qti->m_newSubListStart[j];
				char *xlistEnd = qti->m_newSubListEnd[j];
				char *xp = getWordPosList ( m_docId ,
							    xlist ,
							    xlistEnd - xlist);
				/*
				// try this hack
				char *px = xlist;
				for ( ; ; ) {
					if ( px >= xlistEnd ) {px=NULL;break;}
					if ( px[0] & 0x04 ) { px+=6; continue;}
					int64_t dx = g_posdb.getDocId(px);
					if ( dx == (int64_t)m_docId ) break;
					px += 12;
				}
				// sanity check
				if ( px != xp ) { char *xx=NULL;*xx=0; }
				*/
				// not there? xlist will be NULL
				qti->m_savedCursor[j] = xp;
				// if not there make cursor NULL as well
				if ( ! xp ) {
					qti->m_cursor[j] = NULL;
					continue;
				}
				// skip over docid list
				xp += 12;
				for ( ; ; ) {
					// do not breach sublist
					if ( xp >= xlistEnd ) break;
					// break if 12 byte key: another docid!
					if ( !(xp[0] & 0x04) ) break;
					// skip over 6 byte key
					xp += 6;
				}
				// point to docid sublist end
				qti->m_cursor[j] = xp;
			}
		}
		// skip the pre-advance logic below
		goto skipPreAdvance;
	}

	// . pre-advance each termlist's cursor to skip to next docid
	// . set QueryTermInfo::m_cursor and m_savedCursor of each termlist
	//   so we are ready for a quick skip over this docid
	// . TODO: use just a single array of termlist ptrs perhaps,
	//   then we can remove them when they go NULL.  and we'd save a little
	//   time not having a nested loop.
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo *qti = &qip[i];
		// do not advance negative termlist cursor
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;
		// do each sublist
		for ( int32_t j = 0 ; j < qti->m_numNewSubLists ; j++ ) {
			// int16_tcuts
			register char *xc    = qti->m_cursor[j];
			register char *xcEnd = qti->m_newSubListEnd[j];
			// exhausted? (we can't make cursor NULL because
			// getMaxPossibleScore() needs the last ptr)
			// must match docid
			if ( xc >= xcEnd ||
			     *(int32_t *)(xc+8) != *(int32_t *)(docIdPtr+1) ||
			     (*(char *)(xc+7)&0xfc) != 
			     (*(char *)(docIdPtr)&0xfc) ) {
				// flag it as not having the docid
				qti->m_savedCursor[j] = NULL;
				// skip this sublist if does not have our docid
				continue;
			}
			// sanity. must be 12 byte key
			//if ( (*xc & 0x06) != 0x02 ) {
			//	char *xx=NULL;*xx=0;}
			// save it
			qti->m_savedCursor[j] = xc;
			// get new docid
			//log("new docid %"INT64"",g_posdb.getDocId(xc) );
			// advance the cursors. skip our 12
			xc += 12;
			// then skip any following 6 byte keys because they
			// share the same docid
			for ( ;  ; xc += 6 ) {
				// end of whole termlist?
				if ( xc >= xcEnd ) break;
				// sanity. no 18 byte keys allowed
				if ( (*xc & 0x06) == 0x00 ) {
					// i've seen this triggered on gk28.
					// a dump of posdb for the termlist
					// for 'post' had corruption in it,
					// yet its twin, gk92 did not. the
					// corruption could have occurred
					// anywhere from nov 2012 to may 2013,
					// and the posdb file was never
					// re-merged! must have been blatant
					// disk malfunction?
					log("posdb: encountered corrupt "
					    "posdb list. bailing.");
					return;
					//char *xx=NULL;*xx=0;
				}
				// the next docid? it will be a 12 byte key.
				if ( ! (*xc & 0x04) ) break;
			}
			// assign to next docid word position list
			qti->m_cursor[j] = xc;
		}
	}

	if ( m_q->m_isBoolean ) {
		//minScore = 1.0;
		// we can't jump over setting of miniMergeList. do that.
		goto boolJump1;
	}

	// or if term is gbsectionhash:
	//minScore = 1.0;
	//goto boolJump1;

	// TODO: consider skipping this pre-filter if it sucks, as it does
	// for 'time enough for love'. it might save time!

	// . if there's no way we can break into the winner's circle, give up!
	// . this computes an upper bound for each query term
	for ( int32_t i = 0 ; i < nnn ; i++ ) { // m_numQueryTermInfos ; i++ ) {
		// skip negative termlists.
		// now also skip gbfacet: operator terms
		if ( qip[i].m_bigramFlags[0]&(BF_NEGATIVE|BF_FACET) ) continue;
		// an upper bound on the score we could get
		float maxScore = getMaxPossibleScore ( &qip[i], 0, 0, NULL );
		// -1 means it has inlink text so do not apply this constraint
		// to this docid because it is too difficult because we
		// sum up the inlink text
		if ( maxScore == -1.0 ) {
			continue;
		}
		// if any one of these terms have a max score below the
		// worst score of the 10th result, then it can not win.
		if ( maxScore <= minWinningScore && ! secondPass ) {
			docIdPtr += 6;
			fail0++;
			goto docIdLoop;
		}
	}

	pass0++;

	if ( m_sortByTermNum >= 0 ) goto skipScoringFilter;
	if ( m_sortByTermNumInt >= 0 ) goto skipScoringFilter;

	// we can't filter out a docid based on a low score if we are
	// computing facet stats... because they are based on ALL the results
	// of a query.
	if ( m_hasFacetTerm ) goto skipScoringFilter;

	// test why we are slow
	//if ( (s_sss++ % 8) != 0 ) { docIdPtr += 6; fail0++; goto docIdLoop;}

	// TODO: consider skipping this pre-filter if it sucks, as it does
	// for 'search engine'. it might save time!

	// reset ring buf. make all slots 0xff. should be 1000 cycles or so.
	memset ( ringBuf, 0xff, RINGBUFSIZE );

	// now to speed up 'time enough for love' query which does not
	// have many super high scoring guys on top we need a more restrictive
	// filter than getMaxPossibleScore() so let's pick one query term,
	// the one with the int16_test termlist, and see how close it gets to
	// each of the other query terms. then score each of those pairs.
	// so quickly record the word positions of each query term into
	// a ring buffer of 4096 slots where each slot contains the
	// query term # plus 1.
	qtx = &qip[m_minListi];
	// populate ring buf just for this query term
	for ( int32_t k = 0 ; k < qtx->m_numNewSubLists ; k++ ) {
		// scan that sublist and add word positions
		char *sub = qtx->m_savedCursor [k];
		// skip sublist if it's cursor is exhausted
		if ( ! sub ) continue;
		char *end = qtx->m_cursor      [k];
		// add first key
		//int32_t wx = g_posdb.getWordPos(sub);
		wx = (*((uint32_t *)(sub+3))) >> 6;
		// mod with 4096
		wx &= (RINGBUFSIZE-1);
		// store it. 0 is legit.
		ringBuf[wx] = m_minListi;
		// set this
		ourFirstPos = wx;
		// skip first key
		sub += 12;
		// then 6 byte keys
		for ( ; sub < end ; sub += 6 ) {
			// get word position
			//wx = g_posdb.getWordPos(sub);
			wx = (*((uint32_t *)(sub+3))) >> 6;
			// mod with 4096
			wx &= (RINGBUFSIZE-1);
			// store it. 0 is legit.
			ringBuf[wx] = m_minListi;
		}
	}
	// now get query term closest to query term # m_minListi which
	// is the query term # with the int16_test termlist
	// get closest term to m_minListi and the distance
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// skip the man
		if ( i == m_minListi ) continue;
		// get the query term info
		QueryTermInfo *qti = &qip[i];
		// if we have a negative term, skip it
		if ( qti->m_bigramFlags[0] & (BF_NEGATIVE|BF_FACET) )
			// if its empty, that's good!
			continue;
		// store all his word positions into ring buffer AS WELL
		for ( int32_t k = 0 ; k < qti->m_numNewSubLists ; k++ ) {
			// scan that sublist and add word positions
			char *sub = qti->m_savedCursor [k];
			// skip sublist if it's cursor is exhausted
			if ( ! sub ) continue;
			char *end = qti->m_cursor      [k];
			// add first key
			//int32_t wx = g_posdb.getWordPos(sub);
			wx = (*((uint32_t *)(sub+3))) >> 6;
			// mod with 4096
			wx &= (RINGBUFSIZE-1);
			// store it. 0 is legit.
			ringBuf[wx] = i;
			// skip first key
			sub += 12;
			// then 6 byte keys
			for ( ; sub < end ; sub += 6 ) {
				// get word position
				//wx = g_posdb.getWordPos(sub);
				wx = (*((uint32_t *)(sub+3))) >> 6;
				// mod with 4096
				wx &= (RINGBUFSIZE-1);
				// store it. 0 is legit.
				ringBuf[wx] = i;
			}
		}
		// reset
		int32_t ourLastPos = -1;
		int32_t hisLastPos = -1;
		int32_t bestDist = 0x7fffffff;
		// how far is this guy from the man?
		for ( int32_t x = 0 ; x < (int32_t)RINGBUFSIZE ; ) {
			// skip next 4 slots if all empty. fast?
			if (*(uint32_t *)(ringBuf+x)==0xffffffff) {
				x+=4;continue;}
			// skip if nobody
			if ( ringBuf[x] == 0xff ) { x++; continue; }
			// get query term #
			qt = ringBuf[x];
			// if it's the man
			if ( qt == m_minListi ) {
				// record
				hisLastPos = x;
				// skip if we are not there yet
				if ( ourLastPos == -1 ) { x++; continue; }
				// try distance fix
				if ( x - ourLastPos < bestDist )
					bestDist = x - ourLastPos;
			}
			// if us
			else if ( qt == i ) {
				// record
				ourLastPos = x;
				// skip if he's not recorded yet
				if ( hisLastPos == -1 ) { x++; continue; }
				// update
				ourLastPos = x;
				// check dist
				if ( x - hisLastPos < bestDist )
					bestDist = x - hisLastPos;
			}
			x++;
			continue;
		}
		// compare last occurence of query term #x with our first occ.
		// since this is a RING buffer
		int32_t wrapDist = ourFirstPos + ((int32_t)RINGBUFSIZE-hisLastPos);
		if ( wrapDist < bestDist ) bestDist = wrapDist;
		// query distance
		qdist = qpos[m_minListi] - qpos[i];
		// compute it
		float maxScore2 = getMaxPossibleScore(&qip[i],
						      bestDist,
						      qdist,
						      &qip[m_minListi]);
		// -1 means it has inlink text so do not apply this constraint
		// to this docid because it is too difficult because we
		// sum up the inlink text
		if ( maxScore2 == -1.0 ) continue;
		// if any one of these terms have a max score below the
		// worst score of the 10th result, then it can not win.
		if ( maxScore2 <= minWinningScore && ! secondPass ) {
			docIdPtr += 6;
			fail++;
			goto docIdLoop;
		}
	}

 skipScoringFilter:

	pass++;

 skipPreAdvance:

 boolJump1:

	if ( m_q->m_isBoolean ) {
		//minScore = 1.0;
		// this is somewhat wasteful since it is set below again
		m_docId = *(uint32_t *)(docIdPtr+1);
		m_docId <<= 8;
		m_docId |= (unsigned char)docIdPtr[0];
		m_docId >>= 2;
		// add one point for each term matched in the bool query
		// this is really just for when the terms are from different
		// fields. if we have unfielded boolean terms we should
		// do proximity matching.
		int32_t slot = m_bt.getSlot ( &m_docId );
		if ( slot >= 0 ) {
			uint8_t *bv = (uint8_t *)m_bt.getValueFromSlot(slot);
			// then a score based on the # of terms that matched
			int16_t bitsOn = getNumBitsOnX ( bv , m_vecSize );
			// but store in hashtable now
			minScore = (float)bitsOn;
		}
		else {
			minScore = 1.0;
		}
	}

	// we need to do this for seo hacks to merge the synonyms together
	// into one list
 seoHackSkip2:

	//log("seo: special=%"INT32"",s_special);
	//s_special++;
	//if ( m_q->m_numTerms >= 3 && 
	//     strncmp(m_q->m_qterms[1].m_term,"fine ",5)==0)
	//	log("seo: debug. (special=%"INT32") posdblistptr2size=%"INT32"",
	//	    special,m_q->m_qterms[2].m_posdbListPtr->m_listSize);

	//
	// PERFORMANCE HACK:
	//
	// ON-DEMAND MINI MERGES.

	// we got a docid that has all the query terms, so merge
	// each term's sublists into a single list just for this docid.

	// all posdb keys for this docid should fit in here, the 
	// mini merge buf:
	mptr = mbuf;

	// . merge each set of sublists
	// . like we merge a term's list with its two associated bigram
	//   lists, if there, the left bigram and right bigram list.
	// . and merge all the synonym lists for that term together as well.
	//   so if the term is 'run' we merge it with the lists for
	//   'running' 'ran' etc.
	for ( int32_t j = 0 ; j < m_numQueryTermInfos ; j++ ) {
		// get the query term info
		QueryTermInfo *qti = &qip[j];
		// just use the flags from first term i guess
		// NO! this loses the wikihalfstopbigram bit! so we gotta
		// add that in for the key i guess the same way we add in
		// the syn bits below!!!!!
		bflags [j] = qti->m_bigramFlags[0];
		// if we have a negative term, skip it
		if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) {
			// need to make this NULL for getSiteRank() call below
			miniMergedList[j] = NULL;
			// if its empty, that's good!
			continue;
		}
		// the merged list for term #j is here:
		miniMergedList [j] = mptr;
		bool isFirstKey = true;
		// populate the nwp[] arrays for merging
		int32_t nsub = 0;
		for ( int32_t k = 0 ; k < qti->m_numNewSubLists ; k++ ) {
			// NULL means does not have that docid
			if ( ! qti->m_savedCursor[k] ) continue;
			// getMaxPossibleScore() incremented m_cursor to
			// the next docid so use m_savedCursor.
			nwp      [nsub] = qti->m_savedCursor [k];
			// sanity
			//if ( g_posdb.getKeySize(nwp[nsub]) > 12 ) { 
			//	char *xx=NULL;*xx=0;}
			// if doing seohack then m_cursor was not advanced
			// so advance it here
			if ( m_msg2 ) nwpEnd [nsub] = qti->m_cursor [k];
			else          nwpEnd [nsub] = qti->m_newSubListEnd[k];
			nwpFlags [nsub] = qti->m_bigramFlags [k];
			nsub++;
		}
		/* // scan the sublists and print them out
		if ( 1==1 ) { //g_conf.m_logDebugSEOInserts ) {
			for ( int32_t k = 0 ; ! m_msg2 && k < nsub ; k++ ) {
				// skip if empty
				//if ( ! qti->m_savedCursor[k] ) continue;
				// print out
				char *xx = nwp[k];
				char *xxend = nwpEnd[k];
				if ( g_posdb.getKeySize(xx) != 12 ) {
					char *xx=NULL;*xx=0; }
				char ks;
				for ( ; xx < xxend ; xx += ks ) {
					if ( xx>nwp[k]&&g_posdb.getKeySize(xx)
					     != 6)
						break;
					ks = g_posdb.getKeySize(xx);
					char hgx = g_posdb.getHashGroup(xx);
					int32_t pos = g_posdb.getWordPos(xx);
					log("seo: term#=%"INT32",nsub=%"INT32" "
					    "hgx=%"INT32" pos=%"INT32"",
					    j,k,(int32_t)hgx,pos);
				}	
			}
		} */
		// debug point
		//if ( ! m_msg2 )
		//	log("poo");
		// if only one sublist had this docid, no need to merge
		// UNLESS it's a synonym list then we gotta set the
		// synbits on it, below!!! or a half stop wiki bigram like
		// the term "enough for" in the wiki phrase 
		// "time enough for love" because we wanna reward that more!
		// this halfstopwikibigram bit is set in the indivial keys
		// so we'd have to at least do a key cleansing, so we can't
		// do this shortcut right now... mdw oct 10 2015
		if ( nsub == 1 && 
		     // need it for gbfacet termlists though it seems
		     (nwpFlags[0] & (BF_FACET|BF_NUMBER)) &&		     
		     !(nwpFlags[0] & BF_SYNONYM) &&
		     !(nwpFlags[0] & BF_HALFSTOPWIKIBIGRAM) ) {
			miniMergedList [j] = nwp     [0];
			miniMergedEnd  [j] = nwpEnd  [0];
			bflags         [j] = nwpFlags[0];
			continue;
		}
		// . ok, merge the lists into a list in mbuf
		// . get the min of each list
	mergeMore:
		int32_t mink = -1;
		for ( int32_t k = 0 ; k < nsub ; k++ ) {
			// skip if list is exhausted
			if ( ! nwp[k] ) continue;
			// auto winner?
			if ( mink == -1 ) {
				mink = k;
				continue;
			}
			if ( *(uint32_t *)(nwp[k   ]+2) >
			     *(uint32_t *)(nwp[mink]+2) )
				continue;
			if ( *(uint32_t  *)(nwp[k   ]+2) ==
			     *(uint32_t  *)(nwp[mink]+2) &&
			     *(uint16_t *)(nwp[k   ]) >=
			     *(uint16_t *)(nwp[mink]) )
				continue;
			// a new min...
			mink = k;
		}
		// all exhausted? merge next set of sublists then for term #j
		if ( mink == -1 ) {
			miniMergedEnd[j] = mptr;
			continue;
		}
		// get keysize
		char ks = g_posdb.getKeySize(nwp[mink]);
		// sanity
		//if ( ks > 12 ) { char *xx=NULL;*xx=0; }
		//
		// HACK OF CONFUSION:
		//
		// skip it if its a query phrase term, like 
		// "searchengine" is for the 'search engine' query 
		// AND it has the synbit which means it was a bigram
		// in the doc (i.e. occurred as two separate terms)
		if ( (nwpFlags[mink] & BF_BIGRAM) &&
		     // this means it occurred as two separate terms
		     // or could be like bob and occurred as "bob's".
		     // see XmlDoc::hashWords3().
		     (nwp[mink][2] & 0x03) )
			goto skipOver;
		// if the first key in our merged list store the docid crap
		if ( isFirstKey ) {
			// store a 12 byte key in the merged list buffer
			gbmemcpy ( mptr , nwp[mink] , 12 );
			// sanity check! make sure these not being used...
			//if ( mptr[2] & 0x03 ) { char *xx=NULL;*xx=0; }
			// wipe out its syn bits and re-use our way
			mptr[2] &= 0xfc;
			// set the synbit so we know if its a synonym of term
			if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) 
				mptr[2] |= 0x02;
			// wiki half stop bigram? so for the query
			// 'time enough for love' the phrase term "enough for"
			// is a half stopword wiki bigram, because it is in
			// a phrase in wikipedia ("time enough for love") and
			// one of the two words in the phrase term is a 
			// stop word. therefore we give it more weight than
			// just 'enough' by itself.
			if ( nwpFlags[mink] & BF_HALFSTOPWIKIBIGRAM )
				mptr[2] |= 0x01;
			// make sure its 12 bytes! it might have been
			// the first key for the termid, and 18 bytes.
			mptr[0] &= 0xf9;
			mptr[0] |= 0x02;
			// show hg
			//char hgx = g_posdb.getHashGroup(mptr);
			//int32_t pos = g_posdb.getWordPos(mptr);
			//log("j1=%"INT32" mink=%"INT32" hgx=%"INT32" pos=%"INT32"",
			//    (int32_t)j,(int32_t)mink,(int32_t)hgx,(int32_t)pos);
			lastMptr = mptr;
			mptr += 12;
			isFirstKey = false;
		}
		else {
			// if matches last key word position, do not add!
			// we should add the bigram first if more important
			// since it should be added before the actual term
			// above in the sublist array. so if they are
			// wikihalfstop bigrams they will be added first,
			// otherwise, they are added after the regular term.
			// should fix double scoring bug for 'cheat codes'
			// query!
			if ( lastMptr[4] == nwp[mink][4] &&
			     lastMptr[5] == nwp[mink][5] &&
			     (lastMptr[3] & 0xc0) == (nwp[mink][3] & 0xc0) ){
				goto skipOver;
			}
			*(int32_t  *) mptr    = *(int32_t  *) nwp[mink];
			*(int16_t *)(mptr+4) = *(int16_t *)(nwp[mink]+4);
			// wipe out its syn bits and re-use our way
			mptr[2] &= 0xfc;
			// set the synbit so we know if its a synonym of term
			if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) 
				mptr[2] |= 0x02;
			if ( nwpFlags[mink] & BF_HALFSTOPWIKIBIGRAM )
				mptr[2] |= 0x01;
			// if it was the first key of its list it may not
			// have its bit set for being 6 bytes now! so turn
			// on the 2 compression bits
			mptr[0] |= 0x06;
			// show hg
			//char hgx = g_posdb.getHashGroup(mptr);
			//int32_t pos = g_posdb.getWordPos(mptr);
			//log("j2=%"INT32" mink=%"INT32" hgx=%"INT32" pos=%"INT32"",
			//    (int32_t)j,(int32_t)mink,(int32_t)hgx,(int32_t)pos);
			//if ( pos == 8949 ) { // 73779 ) {
			//	char *xx=NULL;*xx=0; }
			// save it
			lastMptr = mptr;
			mptr += 6;
		}
	skipOver:
		//log("skipping ks=%"INT32"",(int32_t)ks);
		// advance the cursor over the key we used.
		nwp[mink] += ks; // g_posdb.getKeySize(nwp[mink]);
		// exhausted?
		if ( nwp[mink] >= nwpEnd[mink] ) 
			nwp[mink] = NULL;
		// or hit a different docid
		else if ( g_posdb.getKeySize(nwp[mink]) != 6 )
			nwp[mink] = NULL;
		// avoid breach of core below now
		if ( mptr < mptrEnd ) goto mergeMore;
		// wrap it up here since done merging
		miniMergedEnd[j] = mptr;		
	}

	// breach?
	if ( mptr > mbuf + 300000 ) { char *xx=NULL;*xx=0; }

	// clear the counts on this DocIdScore class for this new docid
	pdcs = NULL;
	if ( secondPass ) {
		dcs.reset();
		pdcs = &dcs;
	}

	// log out what the min-scoring pair is if doing debug
	if ( ! m_msg2 && m_r->m_seoDebug ) { //g_conf.m_logDebugSEOInserts ) {
		dcs.reset();
		pdcs = &dcs;
	}


	// second pass already sets m_docId above
	if ( ! secondPass && m_msg2 ) {
		// docid ptr points to 5 bytes of docid shifted up 2
		m_docId = *(uint32_t *)(docIdPtr+1);
		m_docId <<= 8;
		m_docId |= (unsigned char)docIdPtr[0];
		m_docId >>= 2;
	}

	// seoHackSkip2:

	//
	// sanity check for all
	//
	for ( int32_t i = 0   ; i < m_numQueryTermInfos ; i++ ) {
		// skip if not part of score
		if ( bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_FACET) ) continue;
		// get list
		char *plist    = miniMergedList[i];
		char *plistEnd = miniMergedEnd[i];
		int32_t  psize    = plistEnd - plist;
		// test it. first key is 12 bytes.
		if ( psize && g_posdb.getKeySize(plist) != 12 ) {
			char *xx=NULL;*xx=0; }
		// next key is 6
		if ( psize > 12 && g_posdb.getKeySize(plist+12) != 6){
			char *xx=NULL;*xx=0; }
		// show it
		//if ( ! m_msg2 && m_r->m_seoDebug ) {
		//	log("seo: dumping mergedlist #%"INT32"",i);
		//	printTermList(i,plist,psize);
		//	log("seo: DONE dumping mergedlist #%"INT32"",i);
		//}
	}


	if ( m_q->m_isBoolean )
		goto boolJump2;

	//
	//
	// NON-BODY TERM PAIR SCORING LOOP
	//
	// . nested for loops to score the term pairs
	// . store best scores into the scoreMatrix so the sliding window
	//   algorithm can use them from there to do sub-outs
	//

	// scan over each query term (its synonyms are part of the
	// QueryTermInfo)
	for ( int32_t i = 0   ; i < m_numQueryTermInfos ; i++ ) {

	// skip if not part of score
	if ( bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) ) continue;

	// // get the query term info
	// QueryTermInfo *qtix = &qip[i];
	// QueryTerm     *qti  = &m_q->m_qterms[qtix->m_qtermNum];

	// and pair it with each other possible query term
	for ( int32_t j = i+1 ; j < m_numQueryTermInfos ; j++ ) {
		// skip if not part of score
		if ( bflags[j] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) ) 
			continue;

		// // skip if not in same field
		// QueryTermInfo *qtjy = &qip[j];
		// QueryTerm     *qtj  = &m_q->m_qterms[qtjy->m_qtermNum];

		// if ( qti->m_fieldCode != qtj->m_fieldCode )
		// 	continue;

		// but if they are in the same wikipedia phrase
		// then try to keep their positions as in the query.
		// so for 'time enough for love' ideally we want
		// 'time' to be 6 units apart from 'love'
		if ( wikiPhraseIds[j] == wikiPhraseIds[i] &&
		     // zero means not in a phrase
		     wikiPhraseIds[j] ) {
			// . the distance between the terms in the query
			// . ideally we'd like this distance to be reflected
			//   in the matched terms in the body
			qdist = qpos[j] - qpos[i];
			// wiki weight
			wts = (float)WIKI_WEIGHT; // .50;
		}
		else {
			// basically try to get query words as close
			// together as possible
			qdist = 2;
			// this should help fix
			// 'what is an unsecured loan' so we are more likely
			// to get the page that has that exact phrase in it.
			// yes, but hurts how to make a lock pick set.
			//qdist = qpos[j] - qpos[i];
			// wiki weight
			wts = 1.0;
		}
		pss = 0.0;
		//
		// get score for term pair from non-body occuring terms
		//
		if ( miniMergedList[i] && miniMergedList[j] )
			getTermPairScoreForNonBody(i,
						   j,
						   miniMergedList[i],
						   miniMergedList[j],
						   miniMergedEnd[i],
						   miniMergedEnd[j],
						   qdist ,
						   &pss);
		// it's -1 if one term is in the body/header/menu/etc.
		if ( pss < 0 ) {
			scoreMatrix[i*nqt+j] = -1.00;
			wts = -1.0;
		}
		else {
			wts *= pss;
			wts *= m_freqWeights[i];//sfw[i];
			wts *= m_freqWeights[j];//sfw[j];
			// store in matrix for "sub out" algo below
			// when doing sliding window
			scoreMatrix[i*nqt+j] = wts;
			// if terms is a special wiki half stop bigram
			//if ( bflags[i] == 1 ) wts *= WIKI_BIGRAM_WEIGHT;
			//if ( bflags[j] == 1 ) wts *= WIKI_BIGRAM_WEIGHT;
			//if ( ts < minScore ) minScore = ts;
		}
	}
	}

	//
	//
	// SINGLE TERM SCORE LOOP
	//
	//
	maxNonBodyScore = -2.0;
	minSingleScore = 999999999.0;
	// . now add single word scores
	// . having inlink text from siterank 15 of max 
	//   diversity/density will result in the largest score, 
	//   but we add them all up...
	// . this should be highly negative if singles[i] has a '-' 
	//   termsign...
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		float sts;
		// skip if to the left of a pipe operator
		if ( bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) ) 
			continue;

		// skip if in a field. although should make exception
		// for title:
		//QueryTermInfo *qtix = &qip[i];
		//QueryTerm     *qti  = &m_q->m_qterms[qtix->m_qtermNum];
		//if ( qti->m_fieldCode )
		//	continue;

		// sometimes there is no wordpos subtermlist for this docid
		// because it just has the bigram, like "streetlight" and not
		// the word "light" by itself for the query 'street light'
		//if ( miniMergedList[i] ) {
		// assume all word positions are in body
		//bestPos[i] = NULL;
		// . this scans all word positions for this term
		// . this should ignore occurences in the body and only
		//   focus on inlink text, etc.
		// . sets "bestPos" to point to the winning word 
		//   position which does NOT occur in the body
		// . adds up MAX_TOP top scores and returns that sum
		// . pdcs is NULL if not secondPass
		sts = getSingleTermScore (i,
					  miniMergedList[i],
					  miniMergedEnd[i],
					  pdcs,
					  &bestPos[i]);
		// sanity check
		if ( bestPos[i] &&
		     s_inBody[g_posdb.getHashGroup(bestPos[i])] ) {
			char *xx=NULL;*xx=0; }
		//sts /= 3.0;
		if ( sts < minSingleScore ) minSingleScore = sts;
	}

	//
	// . multiplier from siterank i guess
	// . miniMergedList[0] list can be null if it does not have 'street' 
	//   but has 'streetlight' for the query 'street light'
	//
	if ( miniMergedList[0] && 
	     // siterank/langid is always 0 in facet/numeric 
	     // termlists so they sort by their number correctly
	     ! (qip[0].m_bigramFlags[0] & (BF_NUMBER|BF_FACET) ) ) {
		siteRank = g_posdb.getSiteRank ( miniMergedList[0] );
		docLang  = g_posdb.getLangId   ( miniMergedList[0] );
	}
	else {
		for ( int32_t k = 1 ; k < m_numQueryTermInfos ; k++ ) {
			if ( ! miniMergedList[k] ) continue;
			// siterank/langid is always 0 in facet/numeric 
			// termlists so they sort by their number correctly
			if ( qip[k].m_bigramFlags[0] & (BF_NUMBER|BF_FACET) )
				continue;
			siteRank = g_posdb.getSiteRank ( miniMergedList[k] );
			docLang  = g_posdb.getLangId   ( miniMergedList[k] );
			break;
		}
	}
	
	//
	// parms for sliding window algorithm
	//
	m_qpos          = qpos;
	m_wikiPhraseIds = wikiPhraseIds;
	m_quotedStartIds = quotedStartIds;
	//if ( secondPass ) m_ds = &dcs;
	//else              m_ds = NULL;
	m_bestWindowScore = -2.0;

	//
	//
	// BEGIN SLIDING WINDOW ALGO
	//
	//

	m_windowTermPtrs = winnerStack;

	// . now scan the terms that are in the body in a sliding window
	// . compute the term pair score on just the terms in that
	//   sliding window. that way, the term for a word like 'dog'
	//   keeps the same word position when it is paired up with the
	//   other terms.
	// . compute the score the same way getTermPairScore() works so
	//   we are on the same playing field
	// . sub-out each term with its best scoring occurence in the title
	//   or link text or meta tag, etc. but it gets a distance penalty
	//   of like 100 units or so.
	// . if term does not occur in the body, the sub-out approach should
	//   fix that.
	// . keep a matrix of the best scores between two terms from the
	//   above double nested loop algo. and replace that pair if we
	//   got a better score in the sliding window.

	// use special ptrs for the windows so we do not mangle 
	// miniMergedList[] array because we use that below!
	//char *xpos[MAX_QUERY_TERMS];
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) 
		xpos[i] = miniMergedList[i];

	allNull = true;
	//
	// init each list ptr to the first wordpos rec in the body
	// and if no such rec, make it NULL
	//
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// skip if to the left of a pipe operator
		if ( bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) ) 
			continue;
		// skip wordposition until it in the body
		while ( xpos[i] &&!s_inBody[g_posdb.getHashGroup(xpos[i])]) {
			// advance
			if ( ! (xpos[i][0] & 0x04) ) xpos[i] += 12;
			else                         xpos[i] +=  6;
			// NULLify list if no more for this docid
			if (xpos[i] < miniMergedEnd[i] && (xpos[i][0] & 0x04)) 
				continue;
			// ok, no more! null means empty list
			xpos[i] = NULL;
			// must be in title or something else then
			if ( ! bestPos[i] ) { char *xx=NULL;*xx=0; }
		}
		// if all xpos are NULL, then no terms are in body...
		if ( xpos[i] ) allNull = false;
	}

	// if no terms in body, no need to do sliding window
	if ( allNull ) goto doneSliding;

	minx = -1;

 slideMore:

	// . now all xpos are in the body
	// . calc the window score
	// . if window score beats m_bestWindowScore we store the
	//   term xpos that define this window in m_windowTermPtrs[] array
	// . will try to sub in s_bestPos[i] if better, but will fix 
	//   distance to FIXED_DISTANCE
	// . "minx" is who just got advanced, this saves time because we
	//   do not have to re-compute the scores of term pairs that consist
	//   of two terms that did not advance in the sliding window
	// . "scoreMatrix" hold the highest scoring non-body term pair
	//   for sub-bing out the term pair in the body with
	// . sets m_bestWindowScore if this window score beats it
	// . does sub-outs with the non-body pairs and also the singles i guess
	// . uses "bestPos[x]" to get best non-body scoring term for sub-outs
	evalSlidingWindow ( xpos , 
			    m_numQueryTermInfos,
			    bestPos , 
			    scoreMatrix , 
			    minx );


 advanceMin:
	// now find the min word pos still in body
	minx = -1;
	for ( int32_t x = 0 ; x < m_numQueryTermInfos ; x++ ) {
		// skip if to the left of a pipe operator
		// and numeric posdb termlists do not have word positions,
		// they store a float there.
		if ( bflags[x] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) ) 
			continue;
		if ( ! xpos[x] ) continue;
		if ( xpos[x] && minx == -1 ) {
			minx = x;
			//minRec = xpos[x];
			minPos = g_posdb.getWordPos(xpos[x]);
			continue;
		}
		if ( g_posdb.getWordPos(xpos[x]) >= minPos ) 
			continue;
		minx = x;
		//minRec = xpos[x];
		minPos = g_posdb.getWordPos(xpos[x]);
	}
	// sanity
	if ( minx < 0 ) { char *xx=NULL;*xx=0; }

 advanceAgain:
	// now advance that to slide our window
	if ( ! (xpos[minx][0] & 0x04) ) xpos[minx] += 12;
	else                            xpos[minx] +=  6;
	// NULLify list if no more for this docid
	if ( xpos[minx] >= miniMergedEnd[minx] || ! (xpos[minx][0] & 0x04) ) {
		// exhausted list now
		xpos[minx] = NULL;
		// are all null now?
		int32_t k; 
		for ( k = 0 ; k < m_numQueryTermInfos ; k++ ) {
			// skip if to the left of a pipe operator
			if(bflags[k]&(BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET))
				continue;
			if ( xpos[k] ) break;
		}
		// all lists are now exhausted
		if ( k >= m_numQueryTermInfos ) goto doneSliding;
		// ok, now recompute the next min and advance him
		goto advanceMin;
	}
	// if it left the body then advance some more i guess?
	if ( ! s_inBody[g_posdb.getHashGroup(xpos[minx])] ) 
		goto advanceAgain;

	// do more!
	goto slideMore;


	//
	//
	// END SLIDING WINDOW ALGO
	//
	//

 doneSliding:

	minPairScore = -1.0;

	// debug
	//log("posdb: eval docid %"INT64"",m_docId);

	//
	//
	// BEGIN ZAK'S ALGO, BUT RESTRICT ALL BODY TERMS TO SLIDING WINDOW
	//
	//
	// (similar to NON-BODY TERM PAIR SCORING LOOP above)
	//
	for ( int32_t i = 0   ; i < m_numQueryTermInfos ; i++ ) {

	// skip if to the left of a pipe operator
	if ( bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) ) continue;

	// get the query term info
	// QueryTermInfo *qtix = &qip[i];
	// QueryTerm     *qti  = &m_q->m_qterms[qtix->m_qtermNum];

	for ( int32_t j = i+1 ; j < m_numQueryTermInfos ; j++ ) {

		// skip if to the left of a pipe operator
		if ( bflags[j] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER|BF_FACET) ) 
			continue;

		// skip if not in same field
		// QueryTermInfo *qtjy = &qip[j];
		// QueryTerm     *qtj  = &m_q->m_qterms[qtjy->m_qtermNum];

		// if ( qti->m_fieldCode != qtj->m_fieldCode )
		// 	continue;

		//
		// get score for term pair from non-body occuring terms
		//
		if ( ! miniMergedList[i] ) continue;
		if ( ! miniMergedList[j] ) continue;
		// . this limits its scoring to the winning sliding window
		//   as far as the in-body terms are concerned
		// . it will do sub-outs using the score matrix
		// . this will skip over body terms that are not 
		//   in the winning window defined by m_windowTermPtrs[]
		//   that we set in evalSlidingWindow()
		// . returns the best score for this term
		float score = getTermPairScoreForAny (i,
						      j,
						      miniMergedList[i],
						      miniMergedList[j],
						      miniMergedEnd[i],
						      miniMergedEnd[j],
						      pdcs );
		// get min of all term pair scores
		if ( score >= minPairScore && minPairScore >= 0.0 ) continue;
		// got a new min
		minPairScore = score;
	}
	}
	//
	//
	// END ZAK'S ALGO
	//
	//

	m_preFinalScore = minPairScore;

	minScore = 999999999.0;
			
	// get a min score from all the term pairs
	if ( minPairScore < minScore && minPairScore >= 0.0 )
		minScore = minPairScore;

	// if we only had one query term
	if ( minSingleScore < minScore )
		minScore = minSingleScore;

	//score = -1.0;
	//log("score: minPairScore=%f",minPairScore);
	// fix "Recently I posted a question about how"

	// comment out for gbsectionhash: debug:
	if ( minScore <= 0.0 ) 
		goto advance;


 boolJump2:

	// try dividing it by 3! (or multiply by .33333 faster)
	score = minScore * (((float)siteRank)*m_siteRankMultiplier+1.0);

	// . not foreign language? give a huge boost
	// . use "qlang" parm to set the language. i.e. "&qlang=fr"
	if ( m_r->m_language == 0 || 
	     docLang == 0 ||
	     m_r->m_language == docLang)
		score *= (m_r->m_sameLangWeight);//SAMELANGMULT;

	// assume filtered out
	if ( ! secondPass ) m_filtered++;

	//
	// if we have a gbsortby:price term then score exclusively on that
	//
	if ( m_sortByTermNum >= 0 ) {
		// no term?
		if ( ! miniMergedList[m_sortByTermInfoNum] ) goto advance;
		score = g_posdb.getFloat (miniMergedList[m_sortByTermInfoNum]);
	}

	if ( m_sortByTermNumInt >= 0 ) {
		// no term?
		if ( ! miniMergedList[m_sortByTermInfoNumInt] ) goto advance;
	       intScore=g_posdb.getInt(miniMergedList[m_sortByTermInfoNumInt]);
		// do this so hasMaxSerpScore below works, although
		// because of roundoff errors we might lose a docid
		// through the cracks in the widget.
		//score = (float)intScore;
	}

	/*

	  // this logic now moved into isInRange2() when we fill up
	  // the docidVoteBuf. we won't add the docid if it fails one
	  // of these range terms. But if we are a boolean query then
	  // we handle it in makeDocIdVoteBufForBoolQuery_r() below.

	// skip docid if outside of range
	if ( m_minScoreTermNum >= 0 ) {
		// no term?
		if ( ! miniMergedList[m_minScoreTermNum] ) goto advance;
		float score2 ;
		score2= g_posdb.getFloat ( miniMergedList[m_minScoreTermNum] );
		if ( score2 < m_minScoreVal ) goto advance;
	}

	// skip docid if outside of range
	if ( m_maxScoreTermNum >= 0 ) {
		// no term?
		if ( ! miniMergedList[m_maxScoreTermNum] ) goto advance;
		float score2 ;
		score2= g_posdb.getFloat ( miniMergedList[m_maxScoreTermNum] );
		if ( score2 > m_maxScoreVal ) goto advance;
	}

	// skip docid if outside of range
	if ( m_minScoreTermNumInt >= 0 ) {
		// no term?
		if ( ! miniMergedList[m_minScoreTermNumInt] ) goto advance;
		int32_t score3;
		score3=g_posdb.getInt(miniMergedList[m_minScoreTermNumInt]);
		if ( score3 < m_minScoreValInt ) goto advance;
	}

	// skip docid if outside of range
	if ( m_maxScoreTermNumInt >= 0 ) {
		// no term?
		if ( ! miniMergedList[m_maxScoreTermNumInt] ) goto advance;
		int32_t score3 ;
		score3= g_posdb.getInt ( miniMergedList[m_maxScoreTermNumInt]);
		if ( score3 > m_maxScoreValInt ) goto advance;
	}
	*/

	// now we have a maxscore/maxdocid upper range so the widget
	// can append only new results to an older result set.
	if ( m_hasMaxSerpScore ) {
		// if dealing with an "int" score use the extra precision
		// of the double that m_maxSerpScore is!
		if ( m_sortByTermNumInt >= 0 ) {
			if ( intScore > (int32_t)m_r->m_maxSerpScore )
				goto advance;
			if ( intScore == (int32_t)m_r->m_maxSerpScore &&
			     (int64_t)m_docId <= m_r->m_minSerpDocId ) 
				goto advance;
		}
		else {
			if ( score > (float)m_r->m_maxSerpScore ) 
				goto advance;
			if ( score == m_r->m_maxSerpScore &&
			     (int64_t)m_docId <= m_r->m_minSerpDocId ) 
				goto advance;
		}
	}

	// we did not get filtered out
	if ( ! secondPass ) m_filtered--;

	//
	// even if docid did not have a score high enough to be in the
	// winner's list, still add its facet stats, it still is in the
	// search results, just not in the top X.
	//
	// TODO: use limiters like gbfacetmaxvalcount:gbsitehash,2 to limit
	// to just the top 2 results per site. like a generalized way of
	// doing site clustering. i thinkg this is called buckets.
	//
	// for our section stuff we do a query like 
	// gbfacet:<xpathsitehash> and the values is the innerhtml content hash
	// of that xpath/site so we won't have to do buckets for that.
	//
	if ( ! m_hasFacetTerm ) goto skipFacetCheck;

	// only do facet computations on the first pass so we have access
	// to all docids even if not in the winner tree
	if ( secondPass ) goto skipFacetCheck;

	// scan each facet termlist and update
	// QueryTerm::m_facetHashTable/m_dt
	for ( int32_t j = 0 ; j < m_q->m_numTerms ; j++ ) {
		QueryTerm *qt = &m_q->m_qterms[j];
		if ( qt->m_fieldCode != FIELD_GBFACETSTR &&
		     qt->m_fieldCode != FIELD_GBFACETINT &&
		     qt->m_fieldCode != FIELD_GBFACETFLOAT )
			continue;
		// get the queryterminfo class for this query term
		int qti = qt->m_queryTermInfoNum;
		// use that, because miniMergedLists are the synonym lists
		// merged from multiple terms.
		char *p2    = miniMergedList[qti];
		//char *pend = miniMergedEnd [i];
		//
		// just grab the first value i guess...
		//
		//int32_t val32 = g_posdb.getFacetVal32 ( p );
		// add it. count occurences of it per docid
		//qt->m_facetHashTable.addTerm32 ( &val32 );
		// it might have multiple sections that have
		// the same gbxpathsitehash...
		bool firstTime = true;
		//int32_t lastVal;


		// loop over entire termlist
		for ( ; ; ) {

		// do not breach sublist
		if ( p2 >= miniMergedEnd[qti] ) break;
		// break if 12 byte key: another docid!
		if ( ! firstTime && !(p2[0] & 0x04) ) break;

		// . first key is the full size
		// . uses the w,G,s,v and F bits to hold this
		// . this is no longer necessarily sitehash,but
		//   can be any val, like now SectionStats is 
		//   using it for the innerHtml sentence 
		//   content hash32
		int32_t val32 = g_posdb.getFacetVal32 ( p2 );

		// PREADVANCE "p"
		// to avoid dupage...
		//lastVal = val32;
		// skip over 6 or 12 byte key
		if ( firstTime ) p2 += 12;
		else             p2 += 6;
		firstTime = false;


		float *fp = (float *)&val32;

		QueryWord *qw = qt->m_qword;

		int32_t rangeVal32  = 0;

		FacetEntry ff;
		FacetEntry *fe = NULL;

		//
		// CONDENSE THE FACETS
		//
		// if the specified facet range is like
		// 'gbfacetfloat:price,0-1.0,1.0-10,10-50'
		// then we map this val to one of those range
		// buckets, i guess the first number in the
		// range, and do the counts on that! this is
		// the HISTOGRAM logic. that way if we have 1M results
		// and each page has its own price, we won't have 1M facets!
		bool found = false;
		for ( int32_t k = 0 ; k < qw->m_numFacetRanges ; k ++ ) {
			if ( qw->m_fieldCode == FIELD_GBFACETFLOAT ) {
				if ( *fp < qw->m_facetRangeFloatA[k])continue;
				if ( *fp >=qw->m_facetRangeFloatB[k])continue;
				rangeVal32 = *(int32_t *)
					(&qw->m_facetRangeFloatA[k]);
				found = true;
				break;
			}
			// otherwise it was like a 'gbfacetint:gbhopcount' qry
			if ( val32 <  qw->m_facetRangeIntA[k] ) continue;
			if ( val32 >= qw->m_facetRangeIntB[k] ) continue;
			rangeVal32 = qw->m_facetRangeIntA[k];
			found = true;
			break;
		}
				
		// bucket range voting?
		if ( qw->m_numFacetRanges > 0 && found ) {
			// get it
			HashTableX *ft = &qt->m_facetHashTable;
			fe=(FacetEntry *)ft->getValue(&rangeVal32);
			if ( ! fe ) { char *xx=NULL;*xx=0; }
			//fe->m_count++;
			//fe->m_docId = m_docId;
		}

		// don't allow the same docid to vote on the
		// same value twice!
		if ( qw->m_numFacetRanges <= 0 ) {
		//     ( val32 != lastVal || firstTime ) ) {
			// add it
			//qt->m_facetHashTable.addTerm32(&val32
			// get it
			HashTableX *ft = &qt->m_facetHashTable;
			fe=(FacetEntry *)ft->getValue(&val32);
			// debug 
			//log("facets: got entry for key=%"UINT32" "
			//d=%"UINT64"",  val32,m_docId);
			// if not there, init it... but NOT if doing ranges
			// because we already initialized the ranges above
			// so there is already one bucket for each range 
			// specified, and your
			if ( ! fe ) {
				// sanity check
				if ( qw->m_numFacetRanges > 0 ) { 
					char *xx=NULL;*xx=0; }
				memset ( &ff , 0 , sizeof(FacetEntry) );
				fe = &ff;
				int32_t slot;
				ft->addKey(&val32,fe,&slot);
				// now point to what we added since
				// we increment count below, add min/max, etc.
				fe = (FacetEntry *)ft->getValueFromSlot(slot);
			}
		}

		// not in a provided range? or val32==lastVal
		if ( ! fe ) continue;

		// only one vote per docid per facet entry
		if ( fe->m_docId == (int64_t)m_docId ) continue;

		fe->m_docId = m_docId;
		fe->m_count++;

		// first time? then init min/max and set sum to 0
		if ( fe->m_count == 1 ) {
			// set initial float stats
			if ( qw->m_fieldCode == FIELD_GBFACETFLOAT ) {
				*((double *)&fe->m_sum) = 0.0;
				*((float *)&fe->m_min) = *fp;
				*((float *)&fe->m_max) = *fp;
			}
			// and initial int stats
			else {
				fe->m_sum = 0;
				fe->m_min = val32;
				fe->m_max = val32;
			}
		}

		// handle float stats
		if ( qw->m_fieldCode == FIELD_GBFACETFLOAT ) {
			double sum = *((double *)&fe->m_sum);
			sum += *fp; //(double)
			*((double *)&fe->m_sum) = sum;
			if ( *fp < *((float *)&fe->m_min) )
				*((float *)&fe->m_min) = *fp;
			if ( *fp > *((float *)&fe->m_max) )
				*((float *)&fe->m_max) = *fp;
		}
		// and int stats
		else {
			fe->m_sum += val32;
			if ( val32 < fe->m_min )
				fe->m_min = val32;
			if ( val32 > fe->m_max )
				fe->m_max = val32;
		}
	

		}
	}

 skipFacetCheck:


	// if only one term like gbfacetstr:gbxpathsitehash123456
	// then do not bother adding to top tree
	if ( m_r->m_forSectionStats ) goto advance;


	// . seoDebug hack so we can set "dcs"
	// . we only come here if we actually made it into m_topTree
	if ( secondPass || m_r->m_seoDebug ) {
		dcs.m_siteRank   = siteRank;
		dcs.m_finalScore = score;
		// a double can capture an int without dropping any bits,
		// inlike a mere float
		if ( m_sortByTermNumInt >= 0 )
			dcs.m_finalScore = (double)intScore;
		dcs.m_docId      = m_docId;
		dcs.m_numRequiredTerms = m_numQueryTermInfos;
		dcs.m_docLang = docLang;
		// ensure enough room we can't allocate in a thread!
		if ( m_scoreInfoBuf.getAvail()<(int32_t)sizeof(DocIdScore)+1){
			goto advance;
			char *xx=NULL;*xx=0; }
		// if same as last docid, overwrite it since we have a higher
		// siterank or langid i guess
		if ( m_docId == lastDocId ) 
			m_scoreInfoBuf.m_length = lastLen;
		// save that
		int32_t len = m_scoreInfoBuf.m_length;
		// just in case?
		if ( m_r->m_seoDebug ) m_scoreInfoBuf.reset();
		// show it, 190255775595
		//log("posdb: storing score info for d=%"INT64"",m_docId);
		// copy into the safebuf for holding the scoring info
		m_scoreInfoBuf.safeMemcpy ( (char *)&dcs, sizeof(DocIdScore) );
		// save that
		lastLen = len;
		// save it
		lastDocId = m_docId;
		// try to fix dup docid problem! it was hitting the
		// overflow check right above here... strange!!!
		//m_docIdTable.removeKey ( &docId );

		/////////////////////////////
		//
		// . docid range split HACK...
		// . if we are doing docid range splitting, we process
		//   the search results separately in disjoint docid ranges.
		// . so because we still use the same m_scoreInfoBuf etc.
		//   for each split we process, we must remove info from
		//   a top docid of a previous split that got supplanted by
		//   a docid of this docid-range split, so we do not breach
		//   the allocated buffer size.
		// . so  find out which docid we replaced
		//   in the top tree, and replace his entry in scoreinfobuf
		//   as well!
		// . his info was already added to m_pairScoreBuf in the
		//   getTermPairScoreForAny() function
		//
		//////////////////////////////

		// the top tree remains persistent between docid ranges.
		// and so does the score buf. so we must replace scores
		// if the docid gets replaced by a better scoring docid
		// from a following docid range of different docids.
		// However, scanning the docid scor buffer each time is 
		// really slow, so we need to get the kicked out docid
		// from the top tree and use that to store its offset
		// into m_scoreInfoBuf so we can remove it.

		DocIdScore *si;

		// only kick out docids from the score buffer when there
		// is no room left...
		if ( m_scoreInfoBuf.getAvail() >= (int)sizeof(DocIdScore ) )
			goto advance;

		sx = m_scoreInfoBuf.getBufStart();
		sxEnd = sx + m_scoreInfoBuf.length();
		// if we have not supplanted anyone yet, be on our way
		for ( ; sx < sxEnd ; sx += sizeof(DocIdScore) ) {
			si = (DocIdScore *)sx;
			// if top tree no longer has this docid, we must
			// remove its associated scoring info so we do not
			// breach our scoring info bufs
			if ( ! m_topTree->hasDocId( si->m_docId ) ) break;
		}
		// might not be full yet
		if ( sx >= sxEnd ) goto advance;
		// must be there!
		if ( ! si ) { char *xx=NULL;*xx=0; }

		// note it because it is slow
		// this is only used if getting score info, which is
		// not default when getting an xml or json feed
		//log("query: kicking out docid %"INT64" from score buf",
		//    si->m_docId);

		// get his single and pair offsets
		pairOffset   = si->m_pairsOffset;
		pairSize     = si->m_numPairs * sizeof(PairScore);
		singleOffset = si->m_singlesOffset;
		singleSize   = si->m_numSingles * sizeof(SingleScore);
		// nuke him
		m_scoreInfoBuf  .removeChunk1 ( sx , sizeof(DocIdScore) );
		// and his related info
		m_pairScoreBuf  .removeChunk2 ( pairOffset   , pairSize   );
		m_singleScoreBuf.removeChunk2 ( singleOffset , singleSize );
		// adjust offsets of remaining single scores
		sx = m_scoreInfoBuf.getBufStart();
		for ( ; sx < sxEnd ; sx += sizeof(DocIdScore) ) {
			si = (DocIdScore *)sx;
			if ( si->m_pairsOffset > pairOffset )
				si->m_pairsOffset -= pairSize;
			if ( si->m_singlesOffset > singleOffset )
				si->m_singlesOffset -= singleSize;
		}
		
		// adjust this too!
		lastLen -= sizeof(DocIdScore);
	}
	// if doing the second pass for printint out transparency info
	// then do not mess with top tree
	else if ( m_msg2 ) { // ! secondPass ) {
		// add to top tree then!
		int32_t tn = m_topTree->getEmptyNode();
		TopNode *t  = &m_topTree->m_nodes[tn];
		// set the score and docid ptr
		t->m_score = score;
		t->m_docId = m_docId;
		// sanity
		// take this out i've seen this core here before, no idea
		// why, but why core?
		//if ( m_docId == 0 ) { char *xx=NULL;*xx=0; }
		// use an integer score like lastSpidered timestamp?
		if ( m_sortByTermNumInt >= 0 ) {
			t->m_intScore = intScore;
			t->m_score = 0.0;
			if ( ! m_topTree->m_useIntScores){char *xx=NULL;*xx=0;}
		}
		// . this will not add if tree is full and it is less than the 
		//   m_lowNode in score
		// . if it does get added to a full tree, lowNode will be 
		//   removed
		m_topTree->addNode ( t, tn);
		// top tree only holds enough docids to satisfy the
		// Msg39Request::m_docsToGet (m_r->m_docsToGet) request 
		// from the searcher. It basically stores m_docsToGet
		// into TopTree::m_docsWanted. TopTree::m_docsWanted is often 
		// double m_docsToGet to compensate for site clustering, and
		// it can be even more than that in order to ensure we get
		// enough domains represented in the search results.
		// See TopTree::addNode(). it will not add the "t" node if
		// its score is not high enough when the top tree is full.
		if ( m_topTree->m_numUsedNodes > m_topTree->m_docsWanted ) {
			// get the lowest scoring node
			int32_t lowNode = m_topTree->getLowNode();
			// and record its score in "minWinningScore"
			minWinningScore = m_topTree->m_nodes[lowNode].m_score;
		}
	}
	
 advance:

	if ( ! m_msg2 && m_r->m_seoDebug ) { //g_conf.m_logDebugSEOInserts ) {
		// fix it
		DocIdScore *nds = (DocIdScore *)m_scoreInfoBuf.getBufStart();
		// set this... this is done in msg3a normally, but if doing
		// seo shit we gotta do it here. since only running on one 
		// docid we can do this
		if ( nds->m_pairsOffset != 0 &&
		     nds->m_numPairs ) { char *xx=NULL;*xx=0; }
		if ( nds->m_singlesOffset != 0 &&
		     nds->m_numSingles ) { char *xx=NULL;*xx=0; }
		nds->m_pairScores = 
			(PairScore *)m_pairScoreBuf.getBufStart();
		nds->m_singleScores =
			(SingleScore *)m_singleScoreBuf.getBufStart();
	}

	if ( ! m_msg2 ) {
		// if doing this for seo.cpp
		m_finalScore = score;
		return;
	}

	// advance to next docid
	docIdPtr += 6;
	//p = pend;
	// if not of end list loop back up
	//if ( p < listEnd ) goto bigloop;
	goto docIdLoop;

 done:

	if ( m_debug ) {
		now = gettimeofdayInMilliseconds();
		took = now - lastTime;
		log("posdb: new algo phase %"INT32" took %"INT64" ms", phase,took);
		lastTime = now;
		phase++;
	}

	// now repeat the above loop, but with m_dt hashtable
	// non-NULL and include all the docids in the toptree, and
	// for each of those docids store the transparency info in english
	// into the safebuf "transBuf".
	if ( ! secondPass && m_r->m_getDocIdScoringInfo ) {
		// only do one second pass
		secondPass = true;
		// reset this for purposes above!
		//m_topTree->m_lastKickedOutDocId = -1LL;
		/*
		int32_t count = 0;
		// stock m_docIdTable
		for ( int32_t ti = m_topTree->getHighNode() ; 
		      ti >= 0 ; ti = m_topTree->getPrev(ti) ) {
			// get the guy
			TopNode *t = &m_topTree->m_nodes[ti];
			// limit to max!
			if ( count++ >= m_maxScores ) break;
			// now 
			m_docIdTable.addKey(&t->m_docId);
		}
		*/
		goto secondPassLoop;
	}

	if ( m_debug ) {
		log("posdb: # fail0 = %"INT32" ", fail0 );
		log("posdb: # pass0 = %"INT32" ", pass0 );

		log("posdb: # fail = %"INT32" ", fail );
		log("posdb: # pass = %"INT32" ", pass );
	}


	// if a query term is for a facet (ie gbfacetstr:gbtagsite)
	// then count how many unique docids are in it. we were trying to 
	// do this in addDocIdVotes() but it wasn't in the right place i guess.
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		QueryTermInfo *qti = &qip[i];
		QueryTerm *qt = qti->m_qt;
		bool isFacetTerm = false;
		if ( qt->m_fieldCode == FIELD_GBFACETSTR ) isFacetTerm = true;
		if ( qt->m_fieldCode == FIELD_GBFACETINT ) isFacetTerm = true;
		if ( qt->m_fieldCode == FIELD_GBFACETFLOAT ) isFacetTerm =true;
		if ( ! isFacetTerm ) continue;
		// this should also now use the facettable we built up
		// as we accumulated the facet counts above.
		qt->m_numDocsThatHaveFacet += countUniqueDocids ( qti );
	}



	// get time now
	now = gettimeofdayInMilliseconds();
	// store the addLists time
	m_addListsTime = now - t1;
	m_t1 = t1;
	m_t2 = now;
}


// . "bestDist" is closest distance to query term # m_minListi
// . set "bestDist" to 1 to ignore it
float PosdbTable::getMaxPossibleScore ( QueryTermInfo *qti , 
					int32_t bestDist ,
					int32_t qdist ,
					QueryTermInfo *qtm ) {

	// get max score of all sublists
	float bestHashGroupWeight = -1.0;
	unsigned char bestDensityRank;
	char siteRank = -1;
	char docLang;
	//char bestWordSpamRank ;
	unsigned char hgrp;
	bool hadHalfStopWikiBigram = false;
	// scan those sublists to set m_ptrs[] and to get the
	// max possible score of each one
	for ( int32_t j = 0 ; j < qti->m_numNewSubLists ; j++ ) {
		// scan backwards up to this
		char *start = qti->m_savedCursor[j] ;
		// skip if does not have our docid
		if ( ! start ) continue;
		// note it if any is a wiki bigram
		if ( qti->m_bigramFlags[0] & BF_HALFSTOPWIKIBIGRAM ) 
			hadHalfStopWikiBigram = true;
		// skip if entire sublist/termlist is exhausted
		if ( start >= qti->m_newSubListEnd[j] ) continue;
		// set this?
		if ( siteRank == -1 ) {
			siteRank = g_posdb.getSiteRank(start);
			docLang = g_posdb.getLangId(start);
		}
		// skip first key because it is 12 bytes, go right to the
		// 6 byte keys. we deal with it below.
		start += 12;
		// get cursor. this is actually pointing to the next docid
		char *dc = qti->m_cursor[j];
		// back up into our list
		dc -= 6;
		// reset this
		bool retried = false;
		// do not include the beginning 12 byte key in this loop!
		for ( ; dc >= start ; dc -= 6 ) {
			// loop back here for the 12 byte key
		retry:
			// get the best hash group
			hgrp = g_posdb.getHashGroup(dc);
			// if not body, do not apply this algo because
			// we end up adding term pairs from each hash group
			if ( hgrp == HASHGROUP_INLINKTEXT ) return -1.0;
			//if ( hgrp == HASHGROUP_TITLE      ) return -1.0;
			// loser?
			if ( s_hashGroupWeights[hgrp] < bestHashGroupWeight ) {
				// if in body, it's over for this termlist 
				// because this is the last hash group
				// we will encounter.
				if ( hgrp == HASHGROUP_BODY )
					goto nextTermList;
				// otherwise, keep chugging
				continue;
			}
			char dr = g_posdb.getDensityRank(dc);
			// a clean win?
			if ( s_hashGroupWeights[hgrp] > bestHashGroupWeight ) {
				// if the term was in an inlink we end
				// up summing those up so let's just return
				// -1 to indicate we had inlinktext so
				// we won't apply the constraint to this
				// docid for this term
				if ( hgrp == HASHGROUP_INLINKTEXT )
					return -1.0;
				bestHashGroupWeight = s_hashGroupWeights[hgrp];
				bestDensityRank = dr;
				continue;
			}
			// but worst density rank?
			if ( dr < bestDensityRank ) 
				continue;
			// better?
			if ( dr > bestDensityRank )
				bestDensityRank = dr;
			// another tie, oh well... just ignore it
		}
		// handle the beginning 12 byte key
		if ( ! retried ) {
			retried = true;
			dc = qti->m_savedCursor[j];
			goto retry;
		}

	nextTermList:
		continue;

	}

	// if nothing, then maybe all sublists were empty?
	if ( bestHashGroupWeight < 0 ) return 0.0;

	// assume perfect adjacency and that the other term is perfect
	float score = 100.0;

	score *= bestHashGroupWeight;
	score *= bestHashGroupWeight;
	// since adjacent, 2nd term in pair will be in same sentence
	// TODO: fix this for 'qtm' it might have a better density rank and
	//       better hashgroup weight, like being in title!
	score *= s_densityWeights[bestDensityRank];
	score *= s_densityWeights[bestDensityRank];
	// wiki bigram?
	if ( hadHalfStopWikiBigram ) {
		score *= WIKI_BIGRAM_WEIGHT;
		score *= WIKI_BIGRAM_WEIGHT;
	}
	//score *= perfectWordSpamWeight * perfectWordSpamWeight;
	score *= (((float)siteRank)*m_siteRankMultiplier+1.0);

	// language boost if same language (or no lang specified)
	if ( m_r->m_language == docLang ||
	     m_r->m_language == 0 || 
	     docLang == 0 )
		score *= m_r->m_sameLangWeight;//SAMELANGMULT;
	
	// assume the other term we pair with will be 1.0
	score *= qti->m_termFreqWeight;

	// the new logic to fix 'time enough for love' slowness
	if ( qdist ) {
		// no use it
		score *= qtm->m_termFreqWeight;
		// subtract qdist
		bestDist -= qdist;
		// assume in correct order
		if ( qdist < 0 ) qdist *= -1;
		// make it positive
		if ( bestDist < 0 ) bestDist *= -1;
		// avoid 0 division
		if ( bestDist > 1 ) score /= (float)bestDist;
	}

	// terms in same wikipedia phrase?
	//if ( wikiWeight != 1.0 ) 
	//	score *= WIKI_WEIGHT;

	// if query is 'time enough for love' it is kinda slow because
	// we were never multiplying by WIKI_WEIGHT, even though all
	// query terms were in the same wikipedia phrase. so see if this
	// speeds it up.
	if ( m_allInSameWikiPhrase )
		score *= WIKI_WEIGHT;
	
	return score;
}

void printTermList ( int32_t i, char *list, int32_t listSize ) {
	// first key is 12 bytes
	bool firstKey = true;
	char *px = list;//->m_list;
	char *pxend = px + listSize;//list->m_listSize;
	for ( ; px < pxend ; ) {
		int32_t wp = g_posdb.getWordPos(px);
		int32_t dr = g_posdb.getDensityRank(px);
		int32_t hg = g_posdb.getHashGroup(px);
		int32_t syn = g_posdb.getIsSynonym(px);
		log("seo: qterm#%"INT32" pos=%"INT32" dr=%"INT32" hg=%s syn=%"INT32""
		    , i
		    , wp
		    , dr
		    , getHashGroupString(hg)
		    , syn
		    );
		if ( firstKey && g_posdb.getKeySize(px)!=12) {
			char *xx=NULL;*xx=0; }
		else if ( ! firstKey&& g_posdb.getKeySize(px)!=6) {
			char *xx=NULL;*xx=0; }
		if ( firstKey ) px += 12;
		else            px += 6;
		firstKey = false;
	}
}

// sort in descending order
int dcmp6 ( const void *h1 , const void *h2 ) {
	if ( *(uint32_t *)((char *)h1+2) < 
	     *(uint32_t *)((char *)h2+2) )
		return -1;
	if ( *(uint32_t *)((char *)h1+2) > 
	     *(uint32_t *)((char *)h2+2) )
		return  1;
	if ( *(uint16_t *)((char *)h1) < 
	     *(uint16_t *)((char *)h2) )
		return -1;
	// they shouldn't be any dups in there...
	return 1;
}

// TODO: do this in docid range phases to save memory and be much faster
// since we could contain to the L1 cache for hashing
bool PosdbTable::makeDocIdVoteBufForBoolQuery_r ( ) {

	// . make a hashtable of all the docids from all the termlists
	// . the value slot will be the operand bit vector i guess
	// . the size of the vector needs one bit per query operand
	// . if the vector is only 1-2 bytes we can just evaluate each
	//   combination we encounter and store it into an array, otherwise,
	//   we can use a another hashtable in order to avoid re-evaluation
	//   on if it passes the boolean query.
	char bitVec[MAX_OVEC_SIZE];
	if ( m_vecSize > MAX_OVEC_SIZE ) m_vecSize = MAX_OVEC_SIZE;

	QueryTermInfo *qip = (QueryTermInfo *)m_qiBuf.getBufStart();

	// . scan each list of docids to a get a new docid, keep a dedup
	//   table to avoid re-processing the same docid.
	// . each posdb list we read corresponds to a query term,
	//   or a synonym of a query term, or bigram of a query term, etc.
	//   but we really want to know what operand, so we associate an
	//   operand bit with each query term, and each list can map to 
	//   the base query term so we can get the operand # from that.
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {

		// get it
		QueryTermInfo *qti = &qip[i];

		QueryTerm *qt = &m_q->m_qterms[qti->m_qtermNum];
		// get the query word
		//QueryWord *qw = qt->m_qword;

		// just use the word # now
		//int32_t opNum = qw->m_wordNum;//opNum;

		// if this query term # is a gbmin:offprice:190 type
		// of thing, then we may end up ignoring it based on the
		// score contained within!
		bool isRangeTerm = false;
		if ( qt->m_fieldCode == FIELD_GBNUMBERMIN ) 
			isRangeTerm = true;
		if ( qt->m_fieldCode == FIELD_GBNUMBERMAX ) 
			isRangeTerm = true;
		if ( qt->m_fieldCode == FIELD_GBNUMBEREQUALFLOAT ) 
			isRangeTerm = true;
		if ( qt->m_fieldCode == FIELD_GBNUMBERMININT ) 
			isRangeTerm = true;
		if ( qt->m_fieldCode == FIELD_GBNUMBERMAXINT ) 
			isRangeTerm = true;
		if ( qt->m_fieldCode == FIELD_GBNUMBEREQUALINT ) 
			isRangeTerm = true;
		// if ( qt->m_fieldCode == FIELD_GBFIELDMATCH )
		// 	isRangeTerm = true;

		// . make it consistent with Query::isTruth()
		// . m_bitNum is set above to the QueryTermInfo #
		int32_t bitNum = qt->m_bitNum;

		// do not consider for adding if negative ('my house -home')
		//if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;

		// set all to zeroes
		memset ( bitVec , 0 , m_vecSize );

		// set bitvec for this query term #
		int32_t byte = bitNum / 8;
		unsigned char mask = 1<<(bitNum % 8);
		bitVec[byte] |= mask;

		// each query term can have synonym lists etc. scan those.
		// this includes the original query termlist as well.
		for ( int32_t j = 0 ; j < qti->m_numSubLists ; j++ ) {

			// scan all docids in this list
			char *p = qti->m_subLists[j]->getList();
			char *pend = qti->m_subLists[j]->getListEnd();

			//int64_t lastDocId = 0LL;

			// scan the sub termlist #j
			for ( ; p < pend ; ) {
				// place holder
				int64_t docId = g_posdb.getDocId(p);

				// assume this docid is not in range if we
				// had a range term like gbmin:offerprice:190
				bool inRange = false;

				// sanity
				//if ( d < lastDocId ) { char *xx=NULL;*xx=0; }
				//lastDocId = d;

				// point to it
				//char *dp = p + 8;

				// check each posdb key for compliance
				// for gbmin:offprice:190 bool terms
				if ( isRangeTerm && isInRange(p,qt) )
					inRange = true;

				// this was the first key for this docid for 
				// this termid and possible the first key for 
				// this termid, so skip it, either 12 or 18 
				// bytes
				if ( (((char *)p)[0])&0x02 ) p += 12;
				// the first key for this termid?
				else p += 18;

				// then only 6 byte keys would follow from the
				// same docid, so skip those as well
			subloop:
				if( p<pend && (((char *)p)[0])&0x04){
					// check each posdb key for compliance
					// for gbmin:offprice:190 bool terms
					if ( isRangeTerm && isInRange(p,qt) )
						inRange = true;
					p += 6;
					goto subloop;
				}

				// if we had gbmin:offprice:190 and it
				// was not satisfied, then do not OR in this
				// bit in the bitvector for the docid
				if ( isRangeTerm && ! inRange )
					continue;

				// convert docid into hash key
				//int64_t docId = *(int64_t *)dp;
				// shift down 2 bits
				//docId >>= 2;
				// and mask
				//docId &= DOCID_MASK;
				// test it
				//int64_t docId = g_posdb.getDocId(dp-8);
				//if ( d2 != docId ) { char *xx=NULL;*xx=0; }
				// store this docid though. treat as int64_t
				// but we mask with keymask
				int32_t slot = m_bt.getSlot ( &docId );
				if ( slot < 0 ) {
					// we can't alloc in a thread, careful
					if ( ! m_bt.addKey(&docId,bitVec) ) {
						char *xx=NULL;*xx=0; }
					continue;
				}
				// or the bit in otherwise
				char *bv = (char *)m_bt.getValueFromSlot(slot);
				bv[byte] |= mask;
			}
		}
	}


	// debug info
	// int32_t nc = m_bt.getLongestString();
	// log("posdb: string of %"INT32" filled slots!",nc);

	char *dst = m_docIdVoteBuf.getBufStart();

	// . now our hash table is filled with all the docids
	// . evaluate each bit vector
	for ( int32_t i = 0 ; i < m_bt.m_numSlots ; i++ ) {
		// skip if empty
		if ( ! m_bt.m_flags[i] ) continue;
		// get the bit vector
		unsigned char *vec = (unsigned char *)m_bt.getValueFromSlot(i);
		// hash the vector
		int64_t h64 = 0LL;
		for ( int32_t k = 0 ; k < m_vecSize ; k++ )
		       h64^=g_hashtab[(unsigned char)vec[k]][(unsigned char)k];
		// check in hash table
		char *val = (char *)m_ct.getValue ( &h64 );

		// it passes, add the ocid
		if ( m_debug ) {
			int64_t docId =*(int64_t *)m_bt.getKeyFromSlot(i);
			log("query: eval d=%"UINT64" vec[0]=%"XINT32" h64=%"INT64"",
			    docId,(int32_t)vec[0],h64);
			//if ( docId == 47801316261LL )
			//	log("hy");
		}

		// add him to the good table
		if ( val && *val ) {
			// it passes, add the ocid
			int64_t docId =*(int64_t *)m_bt.getKeyFromSlot(i);
			// fix it up
			if ( m_debug ) {
				log("query: adding d=%"UINT64" bitVecSize=%"INT32" "
				    "bitvec[0]=0x%"XINT32" (TRUE)",
				    docId,m_vecSize,(int32_t)vec[0]);
			}
			// shift up
			docId <<= 2;
			// a 6 byte key means you pass
			gbmemcpy ( dst , &docId , 6 );
			dst += 6;
			continue;
		}
		// evaluate the vector
		char include = m_q->matchesBoolQuery ( (unsigned char *)vec ,
						        m_vecSize );
		if ( include ) {
			// it passes, add the ocid
			int64_t docId =*(int64_t *)m_bt.getKeyFromSlot(i);
			// fix it up
			if ( m_debug ) {
				log("query: adding d=%"UINT64" vec[0]=0x%"XINT32"",
				    docId,(int32_t)vec[0]);
			}
			// shift up
			docId <<= 2;
			// a 6 byte key means you pass
			gbmemcpy ( dst , &docId , 6 );
			// test it
			if ( m_debug ) {
				int64_t d2;
				d2 = *(uint32_t *)(dst+1);
				d2 <<= 8;
				d2 |= (unsigned char)dst[0];
				d2 >>= 2;
				docId >>= 2;
				if ( d2 != docId ) { char *xx=NULL;*xx=0; }
			}
			// end test
			dst += 6;
		}
		// store in hash table
		m_ct.addKey ( &h64  , &include );
	}

	// update SafeBuf::m_length
	m_docIdVoteBuf.setLength ( dst - m_docIdVoteBuf.getBufStart() );

	// now sort the docids. TODO: break makeDocIdVoteBufForBoolQuery_r()
	// up into docid ranges so we have like 1/100th the # of docids to 
	// sort. that should make this part a lot faster.
	// i.e. 1000*log(1000) > 1000*(10*log(10))) --> 3000 > 1000
	// i.e. it's faster to break it down into 1000 pieces
	// i.e. for log base 2 maybe it's like 10x faster...
	qsort ( m_docIdVoteBuf.getBufStart() ,
		m_docIdVoteBuf.length() / 6 ,
		6 ,
		dcmp6 );

	return true;
}

int Posdb::printList ( RdbList &list ) {
	bool justVerify = false;
	POSDBKEY lastKey;
	// loop over entries in list
	for ( list.resetListPtr() ; ! list.isExhausted() && ! justVerify ;
	      list.skipCurrentRecord() ) {
		key144_t k; list.getCurrentKey(&k);
		// compare to last
		char *err = "";
		if ( KEYCMP((char *)&k,(char *)&lastKey,sizeof(key144_t))<0 ) 
			err = " (out of order)";
		lastKey = k;
		// is it a delete?
		char *dd = "";
		if ( (k.n0 & 0x01) == 0x00 ) dd = " (delete)";
		int64_t d = g_posdb.getDocId(&k);
		uint8_t dh = g_titledb.getDomHash8FromDocId(d);
		char *rec = list.m_listPtr;
		int32_t recSize = 18;
		if ( rec[0] & 0x04 ) recSize = 6;
		else if ( rec[0] & 0x02 ) recSize = 12;
		// alignment bits check
		if ( recSize == 6  && !(rec[1] & 0x02) ) {
			int64_t nd1 = g_posdb.getDocId(rec+6);
			// seems like nd2 is it, so it really is 12 bytes but
			// does not have the alignment bit set...
			//int64_t nd2 = g_posdb.getDocId(rec+12);
			//int64_t nd3 = g_posdb.getDocId(rec+18);
			// what size is it really?
			// seems like 12 bytes
			//log("debug1: d=%"INT64" nd1=%"INT64" nd2=%"INT64" nd3=%"INT64"",
			//d,nd1,nd2,nd3);
			err = " (alignerror1)";
			if ( nd1 < d ) err = " (alignordererror1)";
			//char *xx=NULL;*xx=0;
		}
		if ( recSize == 12 && !(rec[1] & 0x02) )  {
			//int64_t nd1 = g_posdb.getDocId(rec+6);
			// seems like nd2 is it, so it really is 12 bytes but
			// does not have the alignment bit set...
			int64_t nd2 = g_posdb.getDocId(rec+12);
			//int64_t nd3 = g_posdb.getDocId(rec+18);
			// what size is it really?
			// seems like 12 bytes
			//log("debug1: d=%"INT64" nd1=%"INT64" nd2=%"INT64" nd3=%"INT64"",
			//d,nd1,nd2,nd3);
			//if ( nd2 < d ) { char *xx=NULL;*xx=0; }
			//char *xx=NULL;*xx=0;
			err = " (alignerror2)";
			if ( nd2 < d ) err = " (alignorderrror2)";
		}
		// if it 
		if ( recSize == 12 &&  (rec[7] & 0x02)) { 
			//int64_t nd1 = g_posdb.getDocId(rec+6);
			// seems like nd2 is it, so it really is 12 bytes but
			// does not have the alignment bit set...
			int64_t nd2 = g_posdb.getDocId(rec+12);
			//int64_t nd3 = g_posdb.getDocId(rec+18);
			// what size is it really?
			// seems like 12 bytes really as well!
			//log("debug2: d=%"INT64" nd1=%"INT64" nd2=%"INT64" nd3=%"INT64"",
			//d,nd1,nd2,nd3);
			//char *xx=NULL;*xx=0;
			err = " (alignerror3)";
			if ( nd2 < d ) err = " (alignordererror3)";
		}
		// if ( KEYCMP((char *)&k,(char *)&startKey,list.m_ks)<0 || 
		//      KEYCMP((char *)&k,ek2,list.m_ks)>0){
		// 	err = " (out of range)";
		// }
		//if ( err )
		//	printf("%s",err );
		//continue;
		//if ( ! magicBit && recSize == 6 ) { char *xx=NULL;*xx=0; }
		if ( 1==1 ) //termId < 0 )
			//fprintf(stderr,
			log(
			       "k=%s "
			       "tid=%015"UINT64" "
			       "docId=%012"INT64" "

			       "siterank=%02"INT32" "
			       "langid=%02"INT32" "
			       "pos=%06"INT32" "
			       "hgrp=%02"INT32" "
			       "spamrank=%02"INT32" "
			       "divrank=%02"INT32" "
			       "syn=%01"INT32" "
			       "densrank=%02"INT32" "
			       //"outlnktxt=%01"INT32" "
			       "mult=%02"INT32" "

			       "dh=0x%02"XINT32" "
			       "rs=%"INT32"" //recSize
			       "%s" // dd
			       "%s" // err
			       "\n" , 
			       KEYSTR(&k,sizeof(key144_t)),
			       (int64_t)g_posdb.getTermId(&k),
			       d , 
			       (int32_t)g_posdb.getSiteRank(&k),
			       (int32_t)g_posdb.getLangId(&k),
			       (int32_t)g_posdb.getWordPos(&k),
			       (int32_t)g_posdb.getHashGroup(&k),
			       (int32_t)g_posdb.getWordSpamRank(&k),
			       (int32_t)g_posdb.getDiversityRank(&k),
			       (int32_t)g_posdb.getIsSynonym(&k),
			       (int32_t)g_posdb.getDensityRank(&k),
			       //(int32_t)g_posdb.getIsOutlinkText(&k),
			       (int32_t)g_posdb.getMultiplier(&k),
			       
			       (int32_t)dh, 
			       recSize,
			       dd ,
			       err );
		else
			log(//fprintf(stderr,
			       "k=%s "
			       "tid=%015"UINT64" "
			       "docId=%012"INT64" "

			       "siterank=%02"INT32" "
			       "langid=%02"INT32" "
			       "pos=%06"INT32" "
			       "hgrp=%02"INT32" "
			       "spamrank=%02"INT32" "
			       "divrank=%02"INT32" "
			       "syn=%01"INT32" "
			       "densrank=%02"INT32" "
			       //"outlnktxt=%01"INT32" "
			       "mult=%02"INT32" "
			       //"senth32=0x%08"XINT32" "
			       "recSize=%"INT32" "
			       "dh=0x%02"XINT32"%s%s\n" , 
			       KEYSTR(&k,sizeof(key144_t)),
			       (int64_t)g_posdb.getTermId(&k),
			       d , 
			       (int32_t)g_posdb.getSiteRank(&k),
			       (int32_t)g_posdb.getLangId(&k),
			       (int32_t)g_posdb.getWordPos(&k),
			       (int32_t)g_posdb.getHashGroup(&k),
			       (int32_t)g_posdb.getWordSpamRank(&k),
			       (int32_t)g_posdb.getDiversityRank(&k),
			       (int32_t)g_posdb.getIsSynonym(&k),
			       (int32_t)g_posdb.getDensityRank(&k),
			       //(int32_t)g_posdb.getIsOutlinkText(&k),
			       (int32_t)g_posdb.getMultiplier(&k),
			       //(int32_t)g_posdb.getSectionSentHash32(&k),
			       recSize,
			       
			       (int32_t)dh, 
			       dd ,
			       err );
		continue;
	}

	// startKey = *(key144_t *)list.getLastKey();
	// startKey += (uint32_t) 1;
	// // watch out for wrap around
	// if ( startKey < *(key144_t *)list.getLastKey() ) return;
	// goto loop;
	return 1;
}
