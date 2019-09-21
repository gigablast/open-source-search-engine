#include "gb-include.h"

#include "Indexdb.h"
#include "Url.h"
#include "Clusterdb.h"
//#include "Checksumdb.h"
#include "Threads.h"

// a global class extern'd in .h file
Indexdb g_indexdb;

// for rebuilding indexdb
Indexdb g_indexdb2;

// resets rdb
void Indexdb::reset() { 
	m_rdb.reset();
//#ifdef SPLIT_INDEXDB
	//if ( m_groupIdTable ) {
	//if ( g_hostdb.m_indexSplits > 1 && m_groupIdTable ) {
	//	mfree(m_groupIdTable, m_groupIdTableSize, "Indexdb");
	//	m_groupIdTable = NULL;
	//	m_groupIdTableSize = 0;
	//}
//#endif
}

//#include "DiskPageCache.h"

/*
bool Indexdb::setGroupIdTable ( ) {
	// skip if not split
	if ( g_hostdb.m_indexSplits <= 1 ) return true;
	// . create the groupId table
	m_numGroups = g_hostdb.getNumGroups();
	//m_groupIdTableSize = m_numGroups*INDEXDB_SPLIT*sizeof(int32_t);
	m_groupIdTableSize = m_numGroups*g_hostdb.m_indexSplits*sizeof(int32_t);
	m_groupIdTable =(uint32_t*)mmalloc(m_groupIdTableSize, "Indexdb");
	if ( ! m_groupIdTable ) {
		g_errno = ENOMEM;
		log ( "Could not allocate %"INT32" bytes for groupIdTable",
		      m_groupIdTableSize );
		return false;
	}
	// . the groupId table with the lookup values
	m_groupIdShift = 32;
	int32_t x = m_numGroups;
	while ( x != 1 ) {
		x >>= 1;
		m_groupIdShift--;
	}
	for ( int32_t i = 0; i < m_numGroups; i++ ) {
		uint32_t groupId = g_hostdb.getGroupId(i);
		groupId >>= m_groupIdShift;
		if ( !g_conf.m_legacyIndexdbSplit ) {
			//for ( int32_t s = 0; s < INDEXDB_SPLIT; s++ ) {
			for ( int32_t s = 0; s < g_hostdb.m_indexSplits; s++ ) {
				int32_t g = i + s;
				while ( g >= m_numGroups ) g -= m_numGroups;
				//int32_t x = groupId + ((g % INDEXDB_SPLIT) *
				int32_t x = groupId + ((g%g_hostdb.m_indexSplits)*
						m_numGroups);
				m_groupIdTable[x] = g_hostdb.getGroupId(g);
			}
		}
		else {
			//for ( int32_t s = 0; s < INDEXDB_SPLIT; s++ ) {
			for ( int32_t s = 0; s < g_hostdb.m_indexSplits; s++ ) {
				int32_t g = i + s;
				while ( g >= m_numGroups ) g -= m_numGroups;
				m_groupIdTable[groupId+(m_numGroups*s)] =
					g_hostdb.getGroupId(g);
			}
		}
	}
	return true;
}
*/

bool Indexdb::init ( ) {
	// fake it for now
	return true;
	//if ( ! setGroupIdTable () ) return false;
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (12 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	int32_t nodeSize      = (sizeof(key_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = g_conf.m_indexdbMaxTreeMem  / nodeSize ;
	// . assume the average cached list is about 600 bytes
	// . TODO: if we cache a lot of not founds (small lists), we won't have
	//   enough nodes!!
	int32_t maxCacheNodes = g_conf.m_indexdbMaxCacheMem / 600;

	//int32_t pageSize = GB_INDEXDB_PAGE_SIZE;
	// we now use a disk page cache as opposed to the
	// old rec cache. i am trying to do away with the Rdb::m_cache rec
	// cache in favor of cleverly used disk page caches, because
	// the rec caches are not real-time and get stale. 
	//int32_t pcmem    = g_conf.m_indexdbMaxDiskPageCacheMem;

	//pcmem = 0;
	// make sure at least 30MB
	//if ( pcmem < 30000000 ) pcmem = 30000000;
	// keep this low if we are the tmp cluster, 30MB
	//if ( g_hostdb.m_useTmpCluster && pcmem > 30000000 ) pcmem = 30000000;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	//if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// . init the page cache
	// . MDW: "minimize disk seeks" not working otherwise i'd enable it!
	// if ( ! m_pc.init ( "indexdb",
	// 		   RDB_INDEXDB,
	// 		   pcmem    ,
	// 		   pageSize ))
	// 	return log("db: Indexdb init failed.");

	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want indexdb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	if ( !m_rdb.init ( g_hostdb.m_dir              ,
			   "indexdb"                   ,
			   true                        , // dedup same keys?
			   0                           , // fixed data size
			   g_conf.m_indexdbMinFilesToMerge , 
			   g_conf.m_indexdbMaxTreeMem  ,
			   maxTreeNodes                ,
			   // now we balance so Sync.cpp can ordered huge lists
			   true                        , // balance tree?
			   g_conf.m_indexdbMaxCacheMem ,
			   maxCacheNodes 	       ,
			   true                        , // use half keys?
			   false                       , // g_conf.m_indexdbSav
			   NULL))//&m_pc                       ) )
		return false;
	return true;
	// validate indexdb
	//return verify();
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Indexdb::init2 ( int32_t treeMem ) {
	//if ( ! setGroupIdTable () ) return false;
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (12 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	int32_t nodeSize     = (sizeof(key_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = treeMem  / nodeSize ;
	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want indexdb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "indexdbRebuild"            ,
			    true                        , // dedup same keys?
			    0                           , // fixed data size
			    200                         , // min files to merge
			    treeMem                     ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0                           , // MaxCacheMem ,
			    0                           , // maxCacheNodes
			    true                        , // use half keys?
			    false                       , // indexdbSaveCache
			    NULL                      ) ) // s_pc
		return false;
	return true;
}

/*
bool Indexdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// do a deep verify to figure out which files are corrupt
	deepVerify ( coll );
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}
*/

bool Indexdb::verify ( char *coll ) {
	return true;
	log ( LOG_INFO, "db: Verifying Indexdb for coll %s...", coll );
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
	
	if ( ! msg5.getList ( RDB_INDEXDB   ,
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
		key_t k = list.getCurrentKey();
		count++;
		//uint32_t groupId = k.n1 & g_hostdb.m_groupMask;
		//uint32_t groupId = getGroupId ( RDB_INDEXDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_INDEXDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
		else if ( !printedKey ) {
			log ( "db: Found bad key in list (only printing once): "
			      "%"XINT32" %"XINT64"", k.n1, k.n0 );
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
		log ("db: Out of first %"INT32" records in indexdb, only %"INT32" belong "
		     "to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
				    "right "
				    "data in the right directory? "
				    "Exiting.");
		log ( "db: Exiting due to Indexdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_INFO, "db: Indexdb passed verification successfully for %"INT32" "
			"recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

void Indexdb::deepVerify ( char *coll ) {
	log ( LOG_INFO, "db: Deep Verifying Indexdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	
	collnum_t collnum = g_collectiondb.getCollnum(coll);
	RdbBase *rdbBase = g_indexdb.m_rdb.getBase(collnum);
	int32_t numFiles = rdbBase->getNumFiles();
	int32_t currentFile = 0;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
deepLoop:
	// done after scanning all files
	if ( currentFile >= numFiles ) {
		g_threads.enableThreads();
		log ( LOG_INFO, "db: Finished deep verify for %"INT32" files.",
				numFiles );
		return;
	}
	// scan this file
	if ( ! msg5.getList ( RDB_INDEXDB   ,
			      cr->m_collnum ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      currentFile   , // startFileNum  ,
			      1             , // numFiles      ,
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
			      false         )) {
		g_threads.enableThreads();
		log("db: HEY! it did not block");
		return;
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		//uint32_t groupId = k.n1 & g_hostdb.m_groupMask;
		//uint32_t groupId = getGroupId ( RDB_INDEXDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_INDEXDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		BigFile *f = rdbBase->getFile(currentFile);
		log ("db: File %s: Out of first %"INT32" records in indexdb, "
		     "only %"INT32" belong to our group.",
		     f->getFilename(),count,got );
	}
	//else
	//	log ( LOG_INFO, "db: File %"INT32": Indexdb passed verification "
	//	      "successfully for %"INT32" recs.",currentFile,count );
	// next file
	currentFile++;
	goto deepLoop;
}

// . see Indexdb.h for format of the 12 byte key
// . TODO: substitute var ptrs if you want extra speed
key_t Indexdb::makeKey ( int64_t          termId   , 
			 unsigned char      score    , 
			 uint64_t docId    , 
			 bool               isDelKey ) {
	// make sure we mask out the hi bits we do not use first
	termId = termId & TERMID_MASK;
	key_t key ;
	char *kp = (char *)&key;
	char *tp = (char *)&termId;
	char *dp = (char *)&docId;
	// store termid
	*(int16_t *)(kp+10) = *(int16_t *)(tp+4);
	*(int32_t  *)(kp+ 6) = *(int32_t  *)(tp  );
	// store the complement of the score
	kp[5] = ~score;
	// . store docid
	// . make room for del bit and half bit
	docId <<= 2;
	*(int32_t *)(kp+1) = *(int32_t *)(dp+1);
	kp[0] = dp[0];
	// turn off half bit
	kp[0] &= 0xfd;
	// turn on/off delbit
	if ( isDelKey ) kp[0] &= 0xfe;
	else            kp[0] |= 0x01;
	// key is complete
	return key;
}

// . accesses RdbMap to estimate size of the indexList for this termId
// . returns an UPPER BOUND
int64_t Indexdb::getTermFreq ( collnum_t collnum , int64_t termId ) {
	// establish the list boundary keys
	key_t startKey = makeStartKey ( termId );
	key_t endKey   = makeEndKey   ( termId );
	// . ask rdb for an upper bound on this list size
	// . but actually, it will be somewhat of an estimate 'cuz of RdbTree
	key_t maxKey;
	// divide by 6 since indexdb's recs are 6 bytes each, except for first
	int64_t maxRecs;
	// . don't count more than these many in the map
	// . that's our old truncation limit, the new stuff isn't as dense
	int32_t oldTrunc = 100000;
	// get maxKey for only the top "oldTruncLimit" docids because when
	// we increase the trunc limit we screw up our extrapolation! BIG TIME!
	maxRecs=m_rdb.getListSize(collnum,startKey,endKey,&maxKey,oldTrunc )/6;
	// . TRUNCATION NOW OBSOLETE
	return maxRecs;
	
	// . is this termId truncated in this indexdb?
	// . truncationLimit of Indexdb is max # of records for one termId
	//if ( (int64_t)maxRecs < getTruncationLimit() ) return maxRecs;
	// . no, i like to raise truncation limit on the fly, so if we
	//   still have that line above then nothing would seem to be
	//   truncated, would it?
	// . so just, use a minimal truncation limit then
	if ( maxRecs < MIN_TRUNC ) return maxRecs;

	// this var is so we can adjust the # of recs lost due to truncation
	int64_t numRecs = maxRecs ;

	// . get last score we got
	// . if it is > 1 then we probably got the 1's truncated off
	unsigned char shy       = g_indexdb.getScore ( maxKey );
	int64_t     lastDocId = g_indexdb.getDocId ( maxKey );
	// . which page has first key with this score (shy)?
	// . modify maxKey
	key_t midKey = g_indexdb.makeKey   ( termId , shy , 0LL , true );
	// get # of recs that have this termId and score
	int32_t  lastChunk = m_rdb.getListSize(collnum,
					    midKey,endKey,&maxKey,oldTrunc)/ 6;
	// now interpolate number of uncounted docids for the score "shy"
	int32_t remaining = (((int64_t)lastChunk) * lastDocId) / 
		(int64_t)DOCID_MASK ;

	// add in remaining # of docids from the score "shy"
	numRecs += remaining;

	// log it
	log(LOG_DEBUG,"query: Adding %"INT32" (%"INT32") to score --> %"INT64".", 
	    remaining,lastChunk,numRecs);

	// . if we got a meta tag here, scores are MOSTLY the same
	//   and we should not interpolate based on score
	// . if we got a meta tag scores are usually 33 or more
	// . TODO: figure out a way to do this correctly
	if ( shy > 20 ) shy = 0;

	// debug msg
	//log("endKey.n0=%"XINT64" startKey.n0=%"XINT64"", endKey.n0 , startKey.n0 );
	//log("maxRecs=%"UINT64" maxKey.n0=%"XINT64" shy=%"INT32"",maxRecs,maxKey.n0,shy);

	// don't loop forever
	if ( shy == 0 ) shy = 1;
	// . if last score is > 1 then interpolate just based on the score
	// . a score of i has about 1.5 times the docids of a score of i+1
	// . so if max score (255) has N docs, then we got
	//   TOTAL = N + Nx + Nxx + Nxxx + ... ( where x = 1.5)
	// . therefore, if we lost the score of 1, we just multiply total
	//   docs for scores of 2 though 255 by 1.5 and add N, if N is small,
	//   which it is, don't bother adding it
	// . unfortunately, if we increase the trunc limit we'll often
	//   quickly get lower scoring docids in as porous filler so "shy" will
	//   equal 1 and we won't extrapolate, because we won't know that
	//   a bunch of other docids are really missing
	// . TODO: extrapolate based on last docid, too, not just score,
	//   that way we are way more continuous
	// . FIX: now we use g_conf.m_oldTruncationLimit
	while ( shy-- > 1 ) {
		// this is exponential
		numRecs = (numRecs * 1436LL /*1106*//*1500*/ ) / 1000LL ;
		// only account for truncation by docid for the first round
		//if ( numRecs == maxRecs ) {
		//	// make up for missed docids
		//	uint64_t d = g_indexdb.getDocId ( maxKey );
		//	toAdd = (toAdd * DOCID_MASK) / d;
		//}
		//numRecs += toAdd;
	}

	// log it
	log(LOG_DEBUG,"query: Interpolated tf to %"INT64".", numRecs );

	// debug msg
	//log("numRecs=%"UINT64"",numRecs);

	// . see PageRoot.cpp for explanation of this:
	// . so starting with Lars we'll use checksumdb
	//#ifdef _LARS_
	//int64_t trecs = g_checksumdb.getRdb()->getNumGlobalRecs();
	int64_t trecs = g_clusterdb.getRdb()->getNumGlobalRecs();
	//#else
	//int64_t trecs = g_clusterdb.getRdb()->getNumGlobalRecs() ;
	//#endif
	if ( numRecs > trecs ) numRecs = trecs;

	// TODO: watch out for explosions! (if all scores are the same...)
	if ( maxRecs > numRecs ) return maxRecs;
	return numRecs;
}

// keys are stored from lowest to highest
key_t Indexdb::makeStartKey ( int64_t termId ) {
	return makeKey ( termId , 255/*score*/ , 
			 0x0000000000000000LL/*docId*/ , true/*delKey?*/ );
}
key_t Indexdb::makeEndKey   ( int64_t termId ) {
	return makeKey ( termId , 0/*score*/ , 
			 0xffffffffffffffffLL/*docId*/ , false/*delKey?*/ );
}
