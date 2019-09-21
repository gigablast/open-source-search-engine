#include "gb-include.h"

#include "Msg5.h"
#include "RdbBase.h"
#include "Rdb.h"
//#include "Indexdb.h"
#include "Stats.h"
//#include "RdbCache.h"
#include "Threads.h"
#include "Msg0.h"
#include "PingServer.h"
//#include "Indexdb.h"  // g_indexdb.getTruncationLimit()
//#include "CollectionRec.h"

//#define GBSANITYCHECK

//#define _TESTNEWALGO_ 1

static void gotListWrapper ( void *state ) ;

int32_t g_numCorrupt = 0;

Msg5::Msg5() {
	m_waitingForList = false;
	//m_waitingForMerge = false;
	m_numListPtrs = 0;
	m_mergeLists = true;
	reset();
}

Msg5::~Msg5() {
	reset();
}

// frees m_treeList
void Msg5::reset() {
	if ( m_waitingForList ) { // || m_waitingForMerge ) {
		log("disk: Trying to reset a class waiting for a reply.");
		// might being doing an urgent exit (mainShutdown(1)) or
		// g_process.shutdown(), so do not core here
		//char *xx = NULL; *xx = 0; 
	}
	m_treeList.freeList();
	//m_tfndbList.freeList();
	m_msg3.reset();
	m_prevCount = 0;
	//m_prevKey.setMin();
	KEYMIN(m_prevKey,MAX_KEY_BYTES);// m_ks); m_ks is invalid
	// free lists if m_mergeLists was false
	for ( int32_t i = 0 ; ! m_mergeLists && i < m_numListPtrs ; i++ )
		m_listPtrs[i]->freeList();
	m_numListPtrs = 0;
	// and the tree list
	m_treeList.freeList();
}

/*
//key_t makeCacheKey ( key_t startKey     ,
//void  makeCacheKey ( key_t startKey     ,
//		     key_t endKey       ,
void  makeCacheKey ( char *startKey     ,
		     char *endKey       ,
		     bool  includeTree  ,
		     int32_t  minRecSizes  ,
		     int32_t  startFileNum ,
		     //int32_t  numFiles     ) {
		     int32_t  numFiles     ,
		     char *cacheKeyPtr  ,
		     char  ks           ) { // keySize
	//key_t cacheKey;
	//cacheKey = startKey ;
	//cacheKey = hash96 (       endKey       , cacheKey );
	//cacheKey = hash96 ( (int32_t)includeTree  , cacheKey );
	//cacheKey = hash96 ( (int32_t)minRecSizes  , cacheKey );
	//cacheKey = hash96 ( (int32_t)startFileNum , cacheKey );
	//cacheKey = hash96 ( (int32_t)numFiles     , cacheKey );
	if ( ks == 12 ) {
		key_t cacheKey = *(key_t *)startKey;
		cacheKey = hash96  ( *(key_t *)endKey   , cacheKey );
		cacheKey = hash96  ( (int32_t)includeTree  , cacheKey );
		cacheKey = hash96  ( (int32_t)minRecSizes  , cacheKey );
		cacheKey = hash96  ( (int32_t)startFileNum , cacheKey );
		cacheKey = hash96  ( (int32_t)numFiles     , cacheKey );
		*(key_t *)cacheKeyPtr = cacheKey;
	}
	else {
		key128_t cacheKey = *(key128_t *)startKey;
		cacheKey = hash128 ( *(key128_t *)endKey, cacheKey );
		cacheKey = hash128 ( (int32_t)includeTree  , cacheKey );
		cacheKey = hash128 ( (int32_t)minRecSizes  , cacheKey );
		cacheKey = hash128 ( (int32_t)startFileNum , cacheKey );
		cacheKey = hash128 ( (int32_t)numFiles     , cacheKey );
		*(key128_t *)cacheKeyPtr = cacheKey;
	}
	//return cacheKey;
}
*/

#include "Tfndb.h"
//#include "Checksumdb.h"

//HashTableX g_waitingTable;

// . return false if blocked, true otherwise
// . set g_errno on error
// . fills "list" with the requested list
// . we want at least "minRecSizes" bytes of records, but not much more
// . we want all our records to have keys in the [startKey,endKey] range
// . final merged list should try to have a size of at least "minRecSizes"
// . if may fall int16_t if not enough records were in [startKey,endKey] range
// . endKey of list will be set so that all records from startKey to that
//   endKey are in the list
// . a minRecSizes of 0x7fffffff means virtual inifinty, but it also has 
//   another special meaning. it tells msg5 to tell RdbTree's getList() to 
//   pre-allocate the list size by counting the recs ahead of time.
bool Msg5::getList ( char     rdbId         ,
		     collnum_t collnum ,
		     RdbList *list          ,
		     //key_t    startKey      , 
		     //key_t    endKey        , 
		     void    *startKey      , 
		     void    *endKey        , 
		     int32_t     minRecSizes   , // requested scan size(-1 none)
		     bool     includeTree   ,
		     bool     addToCache    ,
		     int32_t     maxCacheAge   , // in secs for cache lookup
		     int32_t     startFileNum  , // first file to scan
		     int32_t     numFiles      , // rel. to startFileNum,-1 all
		     void    *state         , // for callback
		     void   (* callback ) ( void    *state ,
					    RdbList *list  ,
					    Msg5    *msg5  ) ,
		     int32_t     niceness      ,
		     bool     doErrorCorrection ,
		     //key_t   *cacheKeyPtr   , // NULL if none
		     char    *cacheKeyPtr   , // NULL if none
		     int32_t     retryNum      ,
		     int32_t     maxRetries    ,
		     bool     compensateForMerge ,
		     int64_t syncPoint ,
		     class Msg5 *msg5b   ,
		     bool        isRealMerge ,
		     bool        allowPageCache ,
		     // if this is false we only check the page cache
		     bool        hitDisk ,
		     bool        mergeLists ) {
	// make sure we are not being re-used prematurely
	if ( m_waitingForList ) {
		log("disk: Trying to reset a class waiting for a reply.");
		char *xx = NULL; *xx = 0; 
	}
	if ( collnum < 0 ) {
		log("msg5: called with bad collnum=%"INT32"",(int32_t)collnum);
		g_errno = ENOCOLLREC;
		return true;
	}
	// sanity check. we no longer have record caches!
	// now we do again for posdb gbdocid:xxx| restricted queries
	//if ( addToCache || maxCacheAge ) {char *xx=NULL;*xx=0; }
	// assume no error
	g_errno = 0;
	// sanity
	if ( ! list && mergeLists ) { char *xx=NULL;*xx=0; }
	// warning
	if ( collnum < 0 ) log(LOG_LOGIC,"net: bad collection. msg5.");
	// MUST have this
	//if ( rdbId == RDB_TITLEDB && ! msg5b ) {
	//	log(LOG_LOGIC,"net: No msg5b supplied. 1.");
	//	char *xx = NULL; *xx = 0;
	//}
	// . reset the provided list
	// . this will not free any mem it may have alloc'd but it will set
	//   m_listSize to 0 so list->isEmpty() will return true
	if ( list ) list->reset();
	// key size set
	m_ks = getKeySizeFromRdbId(rdbId);
	// . complain if endKey < startKey
	// . no because IndexReadInfo does this to prevent us from reading
	//   a list
	//if ( startKey > endKey ) return true;
	if ( KEYCMP((char *)startKey,(char *)endKey,m_ks)>0 ) return true;
	// log("Msg5::readList: startKey > endKey warning"); 
	// we no longer allow negative minRecSizes
	if ( minRecSizes < 0 ) {
		if ( g_conf.m_logDebugDb )
		      log(LOG_LOGIC,"net: msg5: MinRecSizes < 0, using 2GB.");
		minRecSizes = 0x7fffffff;
		//char *xx = NULL; *xx = 0;
	}
	// ensure startKey last bit clear, endKey last bit set
	//if ( (startKey.n0 & 0x01) == 0x01 ) 
	if ( !KEYNEG((char *)startKey) )
		log(LOG_REMIND,"net: msg5: StartKey lastbit set."); 
	// fix endkey
	//if ( (endKey.n0   & 0x01) == 0x00 ) {
	if ( KEYNEG((char *)endKey) ) {
		log(LOG_REMIND,"net: msg5: EndKey lastbit clear. Fixing.");
		//endKey.n0 |= 0x01;
		*((char *)endKey) |= 0x01;
	}
	QUICKPOLL(niceness);

	// debug msg
	//log("doing msg5 niceness=%"INT32"",niceness);
	//if ( niceness == 1 ) 
	//	log("hey!");
	// timing debug
	//m_startTime = gettimeofdayInMilliseconds();
	// remember stuff
	m_rdbId         = rdbId;
	m_collnum          = collnum;

	// why was this here? it was messing up the statsdb ("graph") link
	// in the admin panel.
	//CollectionRec *ttt = g_collectiondb.getRec ( m_collnum );
	//if ( ! ttt ) {
	//	g_errno = ENOCOLLREC;
	//	return true;
	//}

	m_list          = list;
	//m_startKey      = startKey;
	//m_endKey        = endKey;
	KEYSET(m_startKey,(char *)startKey,m_ks);
	KEYSET(m_endKey,(char *)endKey,m_ks);
	m_minRecSizes   = minRecSizes;
	m_includeTree   = includeTree;
	m_addToCache    = addToCache;
	m_maxCacheAge   = maxCacheAge;
	m_startFileNum  = startFileNum;
	m_numFiles      = numFiles;
	m_state         = state;
	m_callback      = callback;
	m_calledCallback= 0;
	m_niceness      = niceness;
	m_maxRetries    = maxRetries;
	m_oldListSize   = 0;
	m_dupsRemoved   = 0;
	m_compensateForMerge = compensateForMerge;
	//m_syncPoint          = syncPoint;
	m_msg5b              = msg5b;
	m_isRealMerge        = isRealMerge;
	m_allowPageCache     = allowPageCache;
	m_hitDisk            = hitDisk;
	m_mergeLists         = mergeLists;

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;
	// point to cache
	//RdbCache *cache = base->m_rdb->getCache();
	// . these 2 vars are used for error correction
	// . doRemoteLookup is -2 if it's up to us to decide
	m_doErrorCorrection = doErrorCorrection;
	// these get changed both by cache and gotList()
	m_newMinRecSizes = minRecSizes;
	m_round          = 0;
	m_readAbsolutelyNothing = false;
	//m_fileStartKey   = startKey;
	KEYSET(m_fileStartKey,m_startKey,m_ks);

	QUICKPOLL(m_niceness);
	// get trunc limit by collection now, not just in g_conf
	m_indexdbTruncationLimit = 0x7fffffff;

#ifdef GBSANITYCHECK
	log("msg5: sk=%s", KEYSTR(m_startKey,m_ks));
	log("msg5: ek=%s", KEYSTR(m_endKey,m_ks));
#endif


	/*
	if ( rdbId == RDB_INDEXDB ) {
		CollectionRec *cr = g_collectiondb.getRec ( m_coll );
		if ( ! cr ) { log("disk: Msg5 no coll rec."); return true; }
		//m_indexdbTruncationLimit = cr->m_indexdbTruncationLimit;
		// debug
		//log("trunc limit = %"INT32"",m_indexdbTruncationLimit);
		// Parms.cpp should never let this happen...
		if ( m_indexdbTruncationLimit < MIN_TRUNC ) {
			log("disk: trunc limit = %"INT32"",
			    m_indexdbTruncationLimit);
			char *xx = NULL; *xx = 0; 
		}
	}
	*/

	// debug msg and stuff
	//m_startKey.n1 = 1616550649;
	//m_startKey.n0 = (uint64_t)10489958987685363408LL;
	//m_includeTree = true;
	//m_minRecSizes = 10080000;
	//m_startFileNum = 0;
	//m_numFiles     = -1;

	// hack it down
	if ( numFiles > base->getNumFiles() ) 
		numFiles = base->getNumFiles();

	/*
	// if we're storing or reading from cache.. make the cache key now
	if ( m_maxCacheAge != 0 || m_addToCache ) {
		//if ( cacheKeyPtr ) m_cacheKey = *cacheKeyPtr;
		if ( cacheKeyPtr ) KEYSET(m_cacheKey,cacheKeyPtr,m_ks);
		//else m_cacheKey = makeCacheKey ( m_startKey     ,
		else makeCacheKey              ( m_startKey     ,
						 m_endKey       ,
						 m_includeTree  ,
						 m_minRecSizes  ,
						 m_startFileNum ,
						 //m_numFiles     );
						 m_numFiles     ,
						 m_cacheKey     ,
						 m_ks           );
	}
	*/
	//log("ck.n1=%"UINT32" ck.n0=%"UINT64"",m_cacheKey.n1,m_cacheKey.n0);
	//exit(-1);

	// . make sure we set base above so Msg0.cpp:268 doesn't freak out
	// . if startKey is > endKey list is empty
	//if ( m_startKey > m_endKey ) return true;
	if ( KEYCMP(m_startKey,m_endKey,m_ks)>0 ) return true;
	// same if minRecSizes is 0
	if ( m_minRecSizes == 0    ) return true;

	/*
	// bring back cache for posdb. but only if posdb is merging the lists
	// like when it does gbdocid:xxx| restriction queries in xmldoc.cpp's
	// seo pipe.
	if ( m_maxCacheAge && 
	     m_rdbId == RDB_POSDB && 
	     //m_mergeLists && 	     // this is obsolete me thinks
	     getListFromTermListCache ( coll,
					m_startKey ,
					m_endKey ,
					m_maxCacheAge,
					list ) )
		// list should now be set from the termlistcache!
		return true;

	// check to see if another request for the exact same termlist
	// is in progress. then wait for that if so.
	// so use logic like makeCacheKey() was using.
	int32_t conti = 0;
	m_waitingKey=hash64_cont(m_startKey,m_ks,0LL,&conti);
	m_waitingKey=hash64_cont(m_endKey,m_ks,m_waitingKey,&conti);
	m_waitingKey=hash64_cont((char *)&m_minRecSizes,4,m_waitingKey,&conti);
	m_waitingKey=hash64_cont((char*)&m_startFileNum,4,m_waitingKey,&conti);
	m_waitingKey=hash64_cont((char *)&m_numFiles,4,m_waitingKey,&conti);
	m_waitingKey=hash64_cont((char *)&m_includeTree,1,m_waitingKey,&conti);
	m_waitingKey^= ((uint64_t)rdbId) << (64-8);
	// init it?
	static bool s_waitInit = false;
	if ( ! s_waitInit ) {
		s_waitInit = true;
		if ( ! g_waitingTable.set(8,4,2048,NULL,0,
					  true,m_niceness,"m5wtbl"))
			log("msg5: failed to init waiting table");
		// ignore error
		g_errno = 0;
	}
	// wait for the reply to come in
	bool inTable = g_waitingTable.isInTable(&m_waitingKey);
	// always add it
	void *THIS = this;
	bool added = g_waitingTable.addKey(&m_waitingKey,&THIS);
	// wait for in-progress reply?
	if ( inTable ) {
		// log debug msg
		if ( added && rdbId == RDB_POSDB && m_addToCache ) {
			int64_t cks;
			cks = getTermListCacheKey(m_startKey,m_endKey);
			log("msg5: waiting for      rdbid=%"INT32" wkey=%"XINT64" "
			    "startkey=%s ckey=%"XINT64"",
			    (int32_t)rdbId,m_waitingKey,KEYSTR(m_startKey,m_ks),
			    cks);
		}
		// we blocked and are in the waiting table
		if ( added )
			return false;
		// otherwise, the addkey failed
		log("msg5: failed to add waitingkey: %s",mstrerror(g_errno));
		g_errno = 0;
	}
	// . otherwise log as well
	// . you should NEVER see a dup waiting key or cks because
	//   it should all be cached!!!!
	if ( rdbId == RDB_POSDB && m_addToCache ) {
		int64_t cks = getTermListCacheKey(m_startKey,m_endKey);
		log("msg5: hitting disk for rdbid=%"INT32" wkey=%"XINT64" "
		    "startkey=%s ckey=%"XINT64"",
		    (int32_t)rdbId,m_waitingKey,KEYSTR(m_startKey,m_ks),cks);
	}
	*/

	/*
	// use g_termListCache for single docid lookups
	int64_t singleDocId = 0LL;

	if ( rdbId == RDB_POSDB ) {
		int64_t d1 = g_posdb.getDocId(startKey);
		int64_t d2 = g_posdb.getDocId(endKey);
		if ( d1+1 == d2 ) singleDocId = d1;
	}

	// if in the termlist cache, send it back right away
	char *trec;
	int32_t trecSize;
	if ( singleDocId &&
	     getRecFromTermListCache(coll,
				     m_startKey,
				     m_endKey,
				     20*60, // 20 mins.  maxCacheAge,
				     &trec,
				     &trecSize) ) {
		// if in cache send it back!
		m_list->set ( trec                      , // list data
			      trecSize                  , // list data size
			      trec                      , // alloc 
			      trecSize                  , // alloc size
			      m_startKey                ,
			      m_endKey                  ,
			      0                         , // fixeddatasize
			      true                      , // own data?
			      true                      , // use half keys?
			      m_ks                      );
		// we got it without blocking
		return true;
	}

	// if not found in cache it might be a query we do not match like
	// 'track-track'
	if ( singleDocId ) {
		// just copy ptrs from this list into m_list
		m_list->set ( NULL                      , // list data
			      0                         , // list data size
			      NULL                      , // alloc 
			      0                         , // alloc size
			      m_startKey                ,
			      m_endKey                  ,
			      base->getFixedDataSize() ,
			      true                      , // own data?
			      base->useHalfKeys()       ,
			      m_ks                      );
		return true;
	}		
	*/

	// . check in cache first
	// . we now cache everything
	/*
	if ( m_maxCacheAge != 0 ) {
		log(LOG_DEBUG,"net: Checking cache for list. "
		    //"startKey.n1=%"UINT32" %"UINT64". cacheKey.n1=%"UINT32" %"UINT64".",
		    //m_startKey.n1,m_startKey.n0,
		    //m_cacheKey.n1,m_cacheKey.n0);
		    "startKey.n1=%"XINT64" %"XINT64". cacheKey.n1=%"XINT64" %"XINT64".",
		    KEY1(m_startKey,m_ks),KEY0(m_startKey),
		    KEY1(m_cacheKey,m_ks),KEY0(m_cacheKey));
		// is this list in the cache?
		if ( cache->getList ( base->m_collnum     ,
				      m_cacheKey    ,
				      m_startKey    ,
				      m_list        ,
				      true          ,  // do copy?
				      m_maxCacheAge ,
				      true          )) {// incCounts?
			// debug msg
			log(LOG_DEBUG,"net: msg5: Got cache hit for %s.", 
			    base->m_dbname );
			// sanity check
			//bool ok = m_list->checkList_r ( false , true );
			//if ( ! ok ) log("GETLIST had problem");
			// break out
			//if ( ! ok ) { char *xx = NULL; *xx = 0; }
			return true;
		}
	}
	*/

	// timing debug
	//log("Msg5:getting list startKey.n1=%"UINT32"",m_startKey.n1);
	// start the read loop - hopefully, will only loop once
	if ( readList ( ) ) return true;

	// tell Spider.cpp not to nuke us until we get back!!!
	m_waitingForList = true;
	// we blocked!!! must call m_callback
	return false;
}
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . reads from cache, tree and files
// . calls gotList() to do the merge if we need to
// . loops until m_minRecSizes is satisfied OR m_endKey is reached
bool Msg5::readList ( ) {
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;
 readMore:
	// . reset our tree list
	// . sets fixedDataSize here in case m_includeTree is false because
	//   we don't want merge to have incompatible lists
	m_treeList.reset();
	m_treeList.setFixedDataSize ( base->getFixedDataSize() );
	m_treeList.m_ks = m_ks;
	// reset Msg3 in case gotList() is called without calling 
	// Msg3::readList() first
	m_msg3.reset();

	// assume lists have no errors in them
	m_hadCorruption = false;

	// . restrict tree's endkey by calling msg3 now...
	// . this saves us from spending 1000ms to read 100k of negative 
	//   spiderdb recs from the tree only to have most of the for naught
	// . this call will ONLY set m_msg3.m_endKey
	// . but only do this if dealing with spiderdb really
	// . also now for tfndb, since we scan that in RdbDump.cpp to dedup
	//   the spiderdb list we are dumping to disk. it is really for any
	//   time when the endKey is unbounded, so check that now
	char *treeEndKey = m_endKey;
	bool compute = true;
	if ( ! m_includeTree           ) compute = false;
	// if endKey is "unbounded" then bound it...
	char max[MAX_KEY_BYTES]; KEYMAX(max,m_ks);
	if ( KEYCMP(m_endKey,max,m_ks) != 0 ) compute = false;
	// BUT don't bother if a small list, probably faster just to get it
	if ( m_newMinRecSizes < 1024   ) compute = false;
	// try to make merge read threads higher priority than
	// regular spider read threads
	int32_t niceness = m_niceness;
	if ( niceness > 0  ) niceness = 2;
	if ( m_isRealMerge ) niceness = 1;
	bool allowPageCache = true;
	// just in case cache is corrupted, do not use it for doing real
	// merges, also it would kick out good lists we have in there already
	if ( m_isRealMerge ) allowPageCache = false;
	if ( compute ) {
		m_msg3.readList  ( m_rdbId          ,
				   m_collnum        , 
				   m_fileStartKey   , // modified by gotList()
				   m_endKey         ,
				   m_newMinRecSizes , // modified by gotList()
				   m_startFileNum   ,
				   m_numFiles       ,
				   this             ,
				   gotListWrapper   ,
				   niceness         ,
				   0                , // retry num
				   m_maxRetries     , // -1=def
				   m_compensateForMerge ,
				   -1,//m_syncPoint          ,
				   true                 , // just get endKey?
				   allowPageCache     );
		if ( g_errno ) {
			log("db: Msg5: getting endKey: %s",mstrerrno(g_errno));
			return true;
		}
		treeEndKey = m_msg3.m_constrainKey;
	}

	QUICKPOLL((m_niceness));
	// . get the list from our tree
	// . set g_errno and return true on error
	// . it is crucial that we get tree list before spawning a thread
	//   because Msg22 will assume that if the TitleRec is in the tree
	//   now we'll get it, because we need to have the latest version
	//   of a particular document and this guarantees it. Otherwise, if
	//   the doc is not in the tree then tfndb must tell its file number.
	//   I just don't want to think its in the tree then have it get 
	//   dumped out right before we read it, then we end up getting the
	//   older version rather than the new one in the tree which tfndb
	//   does not know about until it is dumped out. so we could lose
	//   recs between the in-memory and on-disk phases this way.
	// . however, if we are getting a titlerec, we first read the tfndb 
	//   list from the tree then disk. if the merge replaces the tfndb rec
	//   we want with another while we are reading the tfndb list from
	//   disk, then the tfndb rec we got from the tree was overwritten!
	//   so then we'd read from the wrong title file number (tfn) and
	//   not find the rec because the merge just removed it. so keeping
	//   the tfndb recs truly in sync with the titledb recs requires
	//   some dancing. the simplest way is to just scan all titleRecs
	//   in the event of a disagreement... so turn on m_scanAllIfNotFound,
	//   which essentially disregards tfndb and searches all the titledb
	//   files for the titleRec.
	if ( m_includeTree ) {
		// get the mem tree for this rdb
		RdbTree *tree = base->m_rdb->getTree();
		// how many recs are deletes in this list?
		int32_t numNegativeRecs = 0;
		int32_t numPositiveRecs = 0;
		// set start time
		int64_t start ;
		if ( m_newMinRecSizes > 64 ) 
			start = gettimeofdayInMilliseconds();
		// save for later
		m_treeMinRecSizes = m_newMinRecSizes;
		// . returns false on error and sets g_errno
		// . endKey of m_treeList may be less than m_endKey
		char *structName = "tree";

		if(base->m_rdb->useTree()) {
			if ( ! tree->getList ( base->m_collnum    ,
					       m_fileStartKey       ,
					       treeEndKey           ,
					       m_newMinRecSizes     ,
					       &m_treeList          ,
					       &numPositiveRecs     , // # pos
					       &numNegativeRecs     , // # neg
					       base->useHalfKeys() ) ) 
				return true;
			// debug msg
		}
		else {
			RdbBuckets *buckets = &base->m_rdb->m_buckets;
			if ( ! buckets->getList ( base->m_collnum    ,
						  m_fileStartKey       ,
						  treeEndKey           ,
						  m_newMinRecSizes     ,
						  &m_treeList          ,
						  &numPositiveRecs     , 
						  &numNegativeRecs     ,
						  base->useHalfKeys() )) {
				return true;
			}
			structName = "buckets";
		}

		int64_t now  ;
		if ( m_newMinRecSizes > 64 ) {
			now  = gettimeofdayInMilliseconds();
			int64_t took = now - start ;
			if ( took > 9 )
				logf(LOG_INFO,"net: Got list from %s "
				     "in %"UINT64" ms. size=%"INT32" db=%s "
				     "niceness=%"INT32".",
				     structName, took,m_treeList.getListSize(),
				     base->m_dbname,m_niceness);
		}
		// if our recSize is fixed we can boost m_minRecSizes to
		// compensate for these deletes when we call m_msg3.readList()
		int32_t rs = base->getRecSize() ;
		// . use an avg. rec size for variable-length records
		// . just use tree to estimate avg. rec size
		if ( rs == -1) {
			if(base->m_rdb->useTree()) {
				// how much space do all recs take up in the tree?
				int32_t totalSize = tree->getMemOccupiedForList();
				// how many recs in the tree
				int32_t numRecs   = tree->getNumUsedNodes();
				// get avg record size
				if ( numRecs > 0 ) rs = totalSize / numRecs; 
				// add 10% for deviations
				rs = (rs * 110) / 100;
				// what is the minimal record size?
				int32_t minrs     = sizeof(key_t) + 4; 
				// ensure a minimal record size
				if ( rs < minrs ) rs = minrs;
			}
			else {
				RdbBuckets *buckets = &base->m_rdb->m_buckets;
				
				rs = buckets->getNumKeys() / 
					buckets->getMemOccupied();
				int32_t minrs = buckets->getRecSize() + 4; 
				// ensure a minimal record size
				if ( rs < minrs ) rs = minrs;
			}
		}
		// . TODO: get avg recSize in this rdb (avgRecSize*numNeg..)
		// . don't do this if we're not merging because it makes
		//   it harder to compute the # of bytes to read used to
		//   pre-allocate a reply buf for Msg0 when !m_doMerge
		// . we set endKey for spiderdb when reading from tree above
		//   based on the current minRecSizes so do not mess with it
		//   in that case.
		if ( m_rdbId != RDB_SPIDERDB ) {
			//m_newMinRecSizes += rs * numNegativeRecs;
			int32_t nn = m_newMinRecSizes + rs * numNegativeRecs;
			if ( rs > 0 && nn < m_newMinRecSizes ) nn = 0x7fffffff;
			m_newMinRecSizes = nn;
		}
		// . if m_endKey = m_startKey + 1 and our list has a rec
		//   then no need to check the disk, it was in the tree
		// . it could be a negative or positive record
		// . tree can contain both negative/positive recs for the key
		//   so we should do the final merge in gotList()
		// . that can happen because we don't do an annihilation
		//   because the positive key may be being dumped out to disk
		//   but it really wasn't and we get stuck with it
		//key_t kk = m_startKey ;
		//kk += (uint32_t)1;
		//if ( m_endKey == kk && ! m_treeList.isEmpty() ) {
		char kk[MAX_KEY_BYTES];
		KEYSET(kk,m_startKey,m_ks);
		KEYADD(kk,1,m_ks);
		// no no no.... gotList() might be returning false because
		// it's doing a threaded call to merge_r to take out 
		// the negative recs i guess...
		if ( KEYCMP(m_endKey,kk,m_ks)==0 && ! m_treeList.isEmpty() ) {
			return gotList(); } // return true; }
	}
	// if we don't use the tree then at least set the key bounds cuz we
	// pick the min endKey between diskList and treeList below
	else m_treeList.set ( m_fileStartKey , m_endKey );

	// . if we're reading indexlists from 2 or more sources then some 
	//   will probably be compressed from 12 byte keys to 6 byte keys
	// . it is typically only about 1% when files are small,
	//   and smaller than that when a file is large
	// . but just to be save reading an extra 2% won't hurt too much
	if ( base->useHalfKeys() ) {
		int32_t numSources = m_numFiles;
		if ( numSources == -1 ) 
			numSources = base->getNumFiles();
		// if tree is empty, don't count it
		if ( m_includeTree && ! m_treeList.isEmpty() ) numSources++;
		// . if we don't do a merge then we return the list directly
		//   (see condition where m_numListPtrs == 1 below)
		//   from Msg3 (or tree) and we must hit minRecSizes as
		//   close as possible for Msg3's call to constrain() so
		//   we don't overflow the UdpSlot's TMPBUFSIZE buffer
		// . if we just arbitrarily boost m_newMinRecSizes then
		//   the single list we get back from Msg3 will not have
		//   been constrained with m_minRecSizes, but constrained
		//   with m_newMinRecSizes (x2%) and be too big for our UdpSlot
		if ( numSources >= 2 ) {
			int64_t newmin = (int64_t)m_newMinRecSizes ;
			newmin = (newmin * 50LL) / 49LL ;
			// watch out for wrap around
			if ( (int32_t)newmin < m_newMinRecSizes ) 
				m_newMinRecSizes = 0x7fffffff;
			else    m_newMinRecSizes = (int32_t)newmin;
		}
	}

	// limit to 20MB so we don't go OOM!
	if ( m_newMinRecSizes > 2 * m_minRecSizes &&
	     m_newMinRecSizes > 20000000 )
		m_newMinRecSizes = 20000000;
	     

	QUICKPOLL((m_niceness));
	// debug msg
	//log("msg5 calling msg3 for %"INT32" bytes (msg5=%"UINT32")",
	//    m_newMinRecSizes,(int32_t)this);

	// . it's pointless to fetch data from disk passed treeList's endKey
	// . he only differs from m_endKey if his listSize is at least 
	//   newMinRecSizes
	//key_t diskEndKey = m_treeList.getEndKey();
	char *diskEndKey = m_treeList.getEndKey();
	// sanity check
	if ( m_treeList.m_ks != m_ks ) { char *xx = NULL; *xx = 0; }

	// we are waiting for the list
	//m_waitingForList = true;

	// clear just in case
	g_errno = 0;
	// . now get from disk
	// . use the cache-modified constraints to reduce reading time
	// . return false if it blocked
	// . if compensateForMerge is true then m_startFileNum/m_numFiles
	//   will be appropriately mapped around the merge
	if ( ! m_msg3.readList  ( m_rdbId          ,
				  m_collnum        , 
				  m_fileStartKey   , // modified by gotList()
				  diskEndKey       ,
				  m_newMinRecSizes , // modified by gotList()
				  m_startFileNum   ,
				  m_numFiles       ,
				  this             ,
				  gotListWrapper   ,
				  niceness         ,
				  0                , // retry num
				  m_maxRetries     , // max retries (-1=def)
				  m_compensateForMerge ,
				  -1,//m_syncPoint          ,
				  false                ,
				  m_allowPageCache     ,
				  m_hitDisk            ))
		return false;
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . updates m_newMinRecSizes
	// . updates m_fileStartKey to the endKey of m_list + 1
	if ( ! gotList () ) return false;
	// bail on error from gotList() or Msg3::readList()
	if ( g_errno ) return true;
	// we may need to re-call getList
	if ( needsRecall() ) goto readMore;
	// we did not block
	return true;
}

/*
// we were in the g_waitingTable waiting for a list to be read that was already
// in progress when our request came in.
void Msg5::copyAndSendBackList ( RdbList *listSrc ) {
	// this returns false and sets g_errno on error
	if ( ! m_list->copyList(listSrc) )
		log("msg5: copylist failed: %s",mstrerror(g_errno));
	// set it now
	m_calledCallback = 1;
	// when completely done call the callback
	m_callback ( m_state , m_list , this );
}
*/

bool Msg5::needsRecall ( ) {
	bool logIt;
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase ( m_rdbId , m_collnum );
	// if collection was deleted from under us, base will be NULL
	if ( ! base && ! g_errno ) {
		log("msg5: base lost for rdbid=%"INT32" collnum %"INT32"",
		    (int32_t)m_rdbId,(int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return false;
	}
	// sanity check
	if ( ! base && ! g_errno ) { char *xx=NULL;*xx=0; }
	// . return true if we're done reading
	// . sometimes we'll need to read more because Msg3 will int16_ten the
	//   endKey to better meat m_minRecSizes but because of 
	//   positive/negative record annihilation on variable-length
	//   records it won't read enough
	if ( g_errno                         ) goto done;
	if ( m_newMinRecSizes    <= 0        ) goto done;
	if ( ! m_mergeLists ) goto done;
	// limit to just doledb for now in case it results in data loss
	if(m_readAbsolutelyNothing&&
	   (m_rdbId==RDB_DOLEDB||m_rdbId==RDB_SPIDERDB ) ) 
		goto done;
	// seems to be ok, let's open it up to fix this bug where we try
	// to read too many bytes a small titledb and it does an infinite loop
	if ( m_readAbsolutelyNothing ) {
		log("rdb: read absolutely nothing more for dbname=%s on cn=%"INT32"",
		    base->m_dbname,(int32_t)m_collnum);
		goto done;
	}
	//if ( m_list->getEndKey() >= m_endKey ) goto done;
	if ( KEYCMP(m_list->getEndKey(),m_endKey,m_ks)>=0 ) goto done;

	// if this is NOT the first round and the sum of all our list sizes
	// did not increase, then we hit the end...
	//
	// i think this is sometimes stopping us int16_t. we need to verify
	// that each list (from tree and files) is exhausted... which
	// the statement above does... !!! it was causing us to miss urls
	// in doledb and think a collection was done spidering. i think
	// maybe because the startkey of each list would change since we
	// merge them and accumulate into m_list. a better way would be
	// to make sure doledb or any rdb dumps and tight merges when
	// we start having a lot of key annihilations.
	/*
	if ( m_round >= 1 && m_totalSize == m_lastTotalSize ) {
		log("msg5: increasing minrecsizes did nothing. assuming done. "
		    "db=%s (newsize=%"INT32" origsize=%"INT32" total "
		    "got %"INT32" totalListSizes=%"INT32" sk=%s) "
		    "cn=%"INT32" this=0x%"XINT32" round=%"INT32".", 
		    base->m_dbname , 
		    m_newMinRecSizes,
		    m_minRecSizes, 
		    m_list->m_listSize,
		    m_totalSize,
		    KEYSTR(m_startKey,m_ks),
		    (int32_t)m_collnum,(int32_t)this, m_round );
		goto done;
	}
	*/
	// ok, make sure if we go another round at least one list gains!
	m_lastTotalSize = m_totalSize;

	/*
	// sanity check
	if ( m_indexdbTruncationLimit < MIN_TRUNC ) {
		log("disk: trunc limit2 = %"INT32"", m_indexdbTruncationLimit);
		char *xx = NULL; *xx = 0; 
	}
	// if we are limited by truncation then we are done
	if ( base->useHalfKeys() && 
	     base->m_rdb != g_tfndb.getRdb() &&
	     //m_prevCount >= g_indexdb.getTruncationLimit() &&
	     m_prevCount >= m_indexdbTruncationLimit && 
	     g_indexdb.getTermId(*(key_t *)m_startKey) == 
	     g_indexdb.getTermId(*(key_t *)m_endKey) )
		goto done;
	*/
	// debug msg
	//if ( g_conf.m_timingDebugEnabled )
	// this is kinda important. we have to know if we are abusing
	// the disk... we should really keep stats on this...
	logIt = true;
	// seems to be very common for doledb, so don't log unless extreme
	//if ( m_rdbId == RDB_DOLEDB && m_round < 15 ) logIt = false;
	if ( m_round > 100 && (m_round % 1000) != 0 ) logIt = false;
	// seems very common when doing rebalancing then merging to have
	// to do at least one round of re-reading, so note that
	if ( m_round == 0 ) logIt = false;
	// so common for doledb because of key annihilations
	if ( m_rdbId == RDB_DOLEDB && m_round < 10 ) logIt = false;
	if ( logIt )
		log("db: Reading %"INT32" again from %s (need %"INT32" total "
		     "got %"INT32" totalListSizes=%"INT32" sk=%s) "
		     "cn=%"INT32" this=0x%"PTRFMT" round=%"INT32".", 
		     m_newMinRecSizes , base->m_dbname , m_minRecSizes, 
		     m_list->m_listSize,
		     m_totalSize,
		     KEYSTR(m_startKey,m_ks),
		     (int32_t)m_collnum,(PTRTYPE)this, m_round );
	m_round++;
	// record how many screw ups we had so we know if it hurts performance
	base->m_rdb->didReSeek ( );

	// try to read more from disk
	return true;
 done:
	// . reset merged list ptr
	// . merge_r() never rests the list ptr, m_listPtr
	if ( m_list ) m_list->resetListPtr();

	/*
	// bring back termlist caching for posdb for gbdocid:xxxx| queries
	// in the seo pipeline in xmldoc.cpp because we need to do like
	// 1M of those things ultra fast!
	if ( m_addToCache && ! g_errno && m_rdbId == RDB_POSDB )
		addToTermListCache(m_coll,m_startKey,m_endKey,m_list);

	// send back replies to others that are waiting
 callbackLoop:
	int32_t slot = g_waitingTable.getSlot(&m_waitingKey); 
	// valid?
	if ( slot >= 0 ) {
		// get it
		Msg5 *THIS = *(Msg5 **)g_waitingTable.getValueFromSlot(slot);
		// sanity. must have same start keys!
		if ( KEYCMP(THIS->m_startKey,m_startKey,m_ks) != 0 ) {
			char *xx=NULL;*xx=0; }
		// do not call this for the original request
		if ( THIS != this ) THIS->copyAndSendBackList ( m_list );
		// delete it
		g_waitingTable.deleteSlot(slot);
		// another one
		goto callbackLoop;
	}
	*/

	/*
	// add finalized list to cache if we should
	if ( m_addToCache && ! g_errno ) {
		// point to cache
		RdbCache *cache = base->m_rdb->getCache();
		// sanity check
		//bool ok = m_list->checkList_r ( false , true );
		// break out
		//if ( ! ok ) { char *xx = NULL; *xx = 0; }
		//if ( ! ok ) log("ADDLIST had problem");
		// add it if its ok
		//if ( ok ) m_cache->addList ( m_cacheKey, m_list ) ;
		cache->addList ( base->m_collnum , m_cacheKey, m_list ) ;
		// ignore errors
		g_errno = 0;
	}
	*/
	// return false cuz we don't need a recall
	return false;
}

void gotListWrapper ( void *state ) {
	Msg5 *THIS = (Msg5 *) state;
	// . this sets g_errno on error
	// . this will merge cache/tree and disk lists into m_list
	// . it will update m_newMinRecSizes
	// . it will also update m_fileStartKey to the endKey of m_list + 1
	// . returns false if it blocks
	if ( ! THIS->gotList ( ) ) return;
	// . throw it back into the loop if necessary
	// . only returns true if COMPLETELY done
	if ( THIS->needsRecall() && ! THIS->readList() ) return;
	// sanity check
	if ( THIS->m_calledCallback ) { char *xx=NULL;*xx=0; }
	// set it now
	THIS->m_calledCallback = 1;
	// we are no longer waiting for the list
	THIS->m_waitingForList = false;
	// when completely done call the callback
	THIS->m_callback ( THIS->m_state , THIS->m_list , THIS );
}

static void  threadDoneWrapper   ( void *state , ThreadEntry *t ) ;
static void *mergeListsWrapper_r ( void *state , ThreadEntry *t ) ;
//static void  gotListWrapper2     ( void *state , RdbList *list , Msg5 *msg5);

#define TFNDBMINRECSIZES (256*1024)

// . this is the NEW gotList() !!! mdw
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg5::gotList ( ) {

	// we are no longer waiting for the list
	//m_waitingForList = false;

	// debug msg
	//log("msg5 got lists from msg3 (msg5=%"UINT32")",(int32_t)this);
	// return if g_errno is set
	if ( g_errno && g_errno != ECORRUPTDATA ) return true;

	// if reading from titledb, read corresponding tfndb list so
	// we can remove overwritten titleRecs
	if ( m_rdbId != RDB_TITLEDB ) return gotList2();

	// if not merging files on disk, skip this stuff
	if ( ! m_isRealMerge ) return gotList2();

	// this is now obsolete!
	return gotList2();
}
/*

	// The Tfndb Tfn Bug Fix. We've had this bug for over a year. Now we 
	// need to load the corresponding tfndb list with every titledb list 
	// so we can remove titleRecs that are from files whose tfn does not 
	// match the ones in the tfndblist for that docid. 

	// This will remove titlerecs that would only get removed in a tight
	// merge. But even more importantly, fixes the problem that when
	// merging to older titledb files, the tfndb rec of a titlerec in
	// that merge gets re-added to tfndb, and override the newer one in
	// tfndb that corresponds to a newer titledb file that contains the
	// document.

	// We load the tfndb list after the titledb list because if a titlerec
	// got added to the tree just after we loaded the titledb list, then
	// 

	// MUST have this
	if ( ! m_msg5b ) {
		log(LOG_LOGIC,"net: No msg5b supplied.");
		char *xx = NULL; *xx = 0;
	}
	m_time1 = gettimeofdayInMilliseconds();
	int64_t docId1  =g_titledb.getDocIdFromKey((key_t *)m_fileStartKey);
	int64_t docId2  =g_titledb.getDocIdFromKey((key_t *)m_msg3.m_endKey);
	key_t     startKey = g_tfndb.makeMinKey ( docId1 ) ;
	key_t     endKey   = g_tfndb.makeMaxKey ( docId2 ) ;

	QUICKPOLL((m_niceness));
	//endKey.setMax();
	// the tfndb list is often already in the page cache, so this is fast
	if ( ! m_msg5b->getList ( RDB_TFNDB   ,
				  m_coll      ,
				  &m_tfndbList,
				  startKey    ,
				  endKey      ,
				  TFNDBMINRECSIZES , // minRecSizes 
				  true        , // includeTree
				  false       , // addToCache
				  0           , // maxCacheAge
				  0           , // startFileNum
				  -1          , // numFiles
				  this        , // state
				  gotListWrapper2, // callback
				  m_niceness  ,
				  false       , // do error correction
				  NULL        , // cacheKeyPtr
				  0           , // retryNum
				  m_maxRetries, // was 5 -- maxRetries
				  true        , // compensateForMerge
				  -1LL        , // syncpoint
				  NULL        , // msg5b
				  false       , // isRealMerge
				  m_allowPageCache ))
		return false;
	return gotList2();
}

void gotListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) {
	Msg5 *THIS = (Msg5 *)state;
	if ( ! THIS->gotList2() ) return;
	// sanity check
	if ( THIS->m_calledCallback ) { char *xx=NULL;*xx=0; }
	// set it now
	THIS->m_calledCallback = 2;
	// call the original callback
	THIS->m_callback ( THIS->m_state , THIS->m_list , THIS );
}
*/

// . this is the NEW gotList() !!! mdw
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg5::gotList2 ( ) {
	// reset this
	m_startTime = 0LL;
	// return if g_errno is set
	if ( g_errno && g_errno != ECORRUPTDATA ) return true;
	// put all the lists in an array of list ptrs
	int32_t n = 0;
	// all the disk lists
	for ( int32_t i = 0 ; n < MAX_RDB_FILES && i<m_msg3.getNumLists(); i++ ) {
		// . skip list if empty
		// . was this causing problems?
		if ( ! m_isRealMerge ) {
			RdbList *list = m_msg3.getList(i);
			if ( list->isEmpty() ) continue;
		}
		// . remember the tfn, the secondary id
		// . the tfn is used by tfndb to map a docid to a titledb
		//   file. each tfndb record has the tfn. each titledb file
		//   has a tfn (secondary id, aka id2)
		//if ( m_rdbId == RDB_TITLEDB ) m_tfns [n] = m_msg3.getTfn(i); 
		m_listPtrs [ n++ ] = m_msg3.getList(i);
	}
	QUICKPOLL(m_niceness);

	// sanity check.
	if ( m_msg3.getNumLists() > MAX_RDB_FILES ) 
		log(LOG_LOGIC,"db: Msg3 had more than %"INT32" lists.",
		    (int32_t)MAX_RDB_FILES);

	// . get smallest endKey from all the lists
	// . all lists from Msg3 should have the same endKey, but
	//   m_treeList::m_endKey may differ
	// . m_treeList::m_endKey should ALWAYS be >= that of the files
	// . constrain m_treeList to the startKey/endKey of the files
	//m_minEndKey = m_endKey;
	KEYSET(m_minEndKey,m_endKey,m_ks);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		//if ( m_listPtrs[i]->getEndKey() < m_minEndKey ) 
		//	m_minEndKey = m_listPtrs[i]->getEndKey();
		// sanity check
		//if ( KEYNEG(m_listPtrs[i]->getEndKey()) ) {
		//	char *xx=NULL;*xx=0; }
		if ( KEYCMP(m_listPtrs[i]->getEndKey(),m_minEndKey,m_ks)<0 ) {
			KEYSET(m_minEndKey,m_listPtrs[i]->getEndKey(),m_ks);
			// crap, if list is all negative keys, then the
			// end key seems negative too! however in this
			// case RdbScan::m_endKey seems positive so
			// maybe we got a negative endkey in constrain?
			//if (! (m_minEndKey[0] & 0x01) )
			//	log("msg5: list had bad endkey");
		}
	}
	// sanity check
	//if ( KEYNEG( m_minEndKey) ) {char *xx=NULL;*xx=0; }
	/*
	// if we got a tfndblist, constrain the title rec lists to its 
	// transformed endkey. we only read in up to 500k of tfndb list so if 
	// merging two really small titledb files we could potentially be 
	// reading in a much bigger tfndb list.
	if ( m_rdbId == RDB_TITLEDB && m_isRealMerge && ! g_errno ) {
		int64_t time2 = gettimeofdayInMilliseconds();
		int64_t diff  =  time2 - m_time1 ;
		log(LOG_DEBUG,"db: Read tfndblist in %"INT64" ms "
		    "(size=%"INT32").",diff,m_tfndbList.m_listSize);
		// cut it down to m_msg3.m_endKey because that's what we used 
		// to constrain this tfndb list read
		//if ( m_msg3.m_constrainKey < m_minEndKey ) {
		//	log(LOG_DEBUG,"db: Constraining to tfndb Msg3 "
		//	    "m_endKey.");
		//	m_minEndKey = m_msg3.m_constrainKey ;
		//}
		// only mess with m_minEndKey if our list was NOT limited
		// by it. if we were not limited by it, our endKey should
		// really be virtual inifinite. because the most our endKey
		// will ever be is g_tfndb.makeMaxKey ( docIdMAX ) as can
		// be seen above.
		if ( m_tfndbList.m_listSize >= TFNDBMINRECSIZES ) {
			// constrain titledb lists to tfndb's endkey if 
			// it's smaller
			//key_t     ekey  = m_tfndbList.getEndKey();
			char     *ekey  = m_tfndbList.getEndKey();
			int64_t docid = g_tfndb.getDocId ( (key_t *)ekey );
			if ( docid >  0 ) docid = docid - 1;
			//key_t nkey = g_titledb.makeLastKey ( docid );
			char nkey[MAX_KEY_BYTES];
			key_t trk = g_titledb.makeLastKey ( docid );
			KEYSET ( nkey , (char *)&trk , m_ks );
			// sanity check
			//if ( g_titledb.getKeySize() != m_ks ) {
			//	char *xx = NULL; *xx = 0; }
			// only do constrain if docid is not 0
			//if ( docid > 0 && nkey < m_minEndKey ) {
			if ( docid > 0 && KEYCMP(nkey,m_minEndKey,m_ks)<0 ) {
				log(LOG_DEBUG,"db: Tfndb had min key: "
				    //"0x%"XINT64"",nkey.n0);
				    "0x%"XINT64"",KEY0(nkey));
				//m_minEndKey = nkey;
				KEYSET(m_minEndKey,nkey,m_ks);
			}
		}
	}
	*/

	QUICKPOLL(m_niceness);
	// . is treeList included?
	// . constrain treelist for the merge
	// . if used, m_listPtrs [ m_numListPtrs - 1 ] MUST equal &m_treeList
	//   since newer lists are listed last so their records override older
	if ( m_includeTree && ! m_treeList.isEmpty() ) {
		// only constrain if we are NOT the sole list because the
		// constrain routine sets our endKey to virtual infinity it
		// seems like and that makes SpiderCache think that spiderdb
		// is exhausted when it is only in the tree. so i added the
		// if ( n > 0 ) condition here.
		if ( n > 0 ) {
			char k[MAX_KEY_BYTES];
			m_treeList.getCurrentKey(k);
			m_treeList.constrain ( m_startKey  , 
					       m_minEndKey ,
					       -1          , // min rec sizes
					       0           , // hint offset
					       //m_treeList.getCurrentKey() ,
					       k,
					       "tree" ,
					       m_niceness );
		}
		//if ( m_rdbId == RDB_TITLEDB ) m_tfns [n] = 255;
		m_listPtrs [ n++ ] = &m_treeList;
	}
	
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;

	// if not enough lists, use a dummy list to trigger merge so tfndb
	// filter happens and we have a chance to weed out old titleRecs
	if ( m_rdbId == RDB_TITLEDB && m_numFiles != 1 && n == 1 &&
	     m_isRealMerge ) {
		//log(LOG_LOGIC,"db: Adding dummy list.");
		//m_tfns [n] = 255;
		m_dummy.set ( NULL                      , // list data
			      0                         , // list data size
			      NULL                      , // alloc 
			      0                         , // alloc size
			      m_startKey                ,
			      m_minEndKey                  ,
			      base->getFixedDataSize() ,
			      true                      , // own data?
			      base->useHalfKeys()       ,
			      m_ks                      );
		m_listPtrs [ n++ ] = &m_dummy;
	}

	// bitch
	if ( n >= MAX_RDB_FILES ) 
		log(LOG_LOGIC,"net: msg5: Too many lists (%"INT32" | %"INT32").",
			   m_msg3.getNumLists() , n);

	// store # of lists here for use by the call to merge_r()
	m_numListPtrs = n;
	// count the sizes
	m_totalSize = 0;
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ )
		m_totalSize += m_listPtrs[i]->getListSize();

	QUICKPOLL(m_niceness);
	// . but don't breach minRecSizes
	// . this totalSize is just to see if we should spawn a thread, really
	//if ( totalSize > m_minRecSizes ) m_totalSize = m_minRecSizes;

#ifdef GBSANITYCHECK
	// who uses this now?
	//log("Msg5:: who is merging?????");
	// timing debug
	//    m_startKey.n1,
	//    gettimeofdayInMilliseconds()-m_startTime , 
	//    m_diskList.getListSize());
	// ensure both lists are legit
	// there may be negative keys in the tree
	// diskList may now also have negative recs since Msg3 no longer 
	// removes them for fears of delayed positive keys not finding their
	// negative key because it was merged out by RdbMerge
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ )
		m_listPtrs[i]->checkList_r ( false , true );
#endif

	// if not doing merge, we're all done!
	if ( ! m_mergeLists ) 
		return doneMerging();

	// . if no lists we're done
	// . if we were a recall, then list may not be empty
	if ( m_numListPtrs == 0 && m_list->isEmpty() ) {
		// just copy ptrs from this list into m_list
		m_list->set ( NULL                      , // list data
			      0                         , // list data size
			      NULL                      , // alloc 
			      0                         , // alloc size
			      m_startKey                ,
			      m_endKey                  ,
			      base->getFixedDataSize() ,
			      true                      , // own data?
			      base->useHalfKeys()       ,
			      m_ks                      );
		// . add m_list to our cache if we should
		// . this returns false if blocked, true otherwise
		// . sets g_errno on error
		// . only blocks if calls msg0 to patch a corrupted list
		// . it will handle calling callback if that happens
		return doneMerging();
	}

	if ( m_numListPtrs == 0 ) m_readAbsolutelyNothing = true;

	// if all lists from msg3 were 0... tree still might have something
	if ( m_totalSize == 0 && m_treeList.isEmpty() ) 
		m_readAbsolutelyNothing = true;

	// if msg3 had corruption in a list which was detected in contrain_r()
	if ( g_errno == ECORRUPTDATA ) {
		// if we only had one list, we were not doing a merge
		// so return g_errno to the requested so he tries from the
		// twin
		if ( m_numListPtrs == 1 ) return true;
		// assume nothing is wrong
		g_errno = 0;
		// if m_doErrorCorrection is true, repairLists_r() should fix
	}
	QUICKPOLL((m_niceness));

	// . should we remove negative recs from final merged list?
	// . if we're reading from root and tmp merge file of root
	// . should we keep this out of the thread in case a file created?
	int32_t fn = 0;
	if ( base->m_numFiles > 0 ) fn = base->m_fileIds[m_startFileNum];
	if ( fn == 0 || fn == 1 ) m_removeNegRecs = true;
	else                      m_removeNegRecs = false;

	// . if we only have one list, just use it
	// . Msg3 should have called constrain() on it so it's m_list so
	//   m_listEnd and m_listSize all fit m_startKey/m_endKey/m_minRecSizes
	//   to a tee
	// . if it's a tree list it already fits to a tee
	// . same with cache list?? better be...
	// . if we're only reading one list it should always be empty right?
	// . i was getting negative keys in my RDB_DOLEDB list which caused
	//   Spider.cpp to core, so i add the "! m_removeNegRecs" constraint
	//   here... 
	// . TODO: add some code to just filter out the negative recs
	//   super quick just for this purpose
	// . crap, rather than do that just deal with the negative recs 
	//   in the caller code... in this case Spider.cpp::gotDoledbList2()
	if ( m_numListPtrs == 1 && m_list->isEmpty() &&//&&!m_removeNegRecs 
	     // just do this logic for doledb now, it was causing us to
	     // return search results whose keys were negative indexdb keys.
	     // or later we can just write some code to remove the neg
	     // recs from the single list!
	     ( m_rdbId == RDB_LINKDB || m_rdbId == RDB_DOLEDB ||
	       // this speeds up our queryloop querylog parsing in
	       // seo.cpp quite a bit
	       (m_rdbId == RDB_POSDB && m_numFiles==1) ) ) {
		// log any problems
		if ( ! m_listPtrs[0]->m_ownData ) {
			log(LOG_LOGIC,"db: Msg5: list does not own data.");
			goto skip;
		}
		// . bitch if not empty
		// . NO! might be our second time around if we had key
		//   annihilations between file #0 and the tree, and now
		//   we only have 1 non-empty list ptr, either from the tree
		//   or from the file
		//if ( ! m_list->isEmpty() ) 
		//	log("Msg5::gotList: why is it not empty? size=%"INT32"",
		//	    m_list->getListSize() );
		// just copy ptrs from this list into m_list
		m_list->set ( m_listPtrs[0]->getList          () ,
			      m_listPtrs[0]->getListSize      () ,
			      m_listPtrs[0]->getAlloc         () ,
			      m_listPtrs[0]->getAllocSize     () ,
			      m_listPtrs[0]->getStartKey      () ,
			      m_listPtrs[0]->getEndKey        () ,
			      m_listPtrs[0]->getFixedDataSize () ,
			      true                               , // own data?
			      m_listPtrs[0]->useHalfKeys      () ,
			      m_ks                               );
		// ensure we don't free it when we loop on freeLists() below
		m_listPtrs[0]->setOwnData ( false );
		// gotta set this too!
		if ( m_listPtrs[0]->m_lastKeyIsValid )
			m_list->setLastKey ( m_listPtrs[0]->m_lastKey );
		// . remove titleRecs that shouldn't be there
		// . if the tfn of the file we read the titlerec from does not
		//   match the one in m_tfndbList, then remove it
		// . but if we're not merging lists, why remove it?
		//if ( m_rdbId == RDB_TITLEDB && m_msg3.m_numFileNums > 1 )
		//	stripTitleRecs ( m_list , m_tfns[0] , m_tfndbList );
		// . add m_list to our cache if we should
		// . this returns false if blocked, true otherwise
		// . sets g_errno on error
		// . only blocks if calls msg0 to patch a corrupted list
		// . it will handle calling callback if that happens
		return doneMerging();
	}

 skip:
	// time the perparation and merge
	m_startTime = gettimeofdayInMilliseconds();

	// . merge the lists 
	// . the startKey of the finalList is m_startKey, the first time
	// . but after that, we're adding diskLists, so us m_fileStartKey
	// . we're called multiple times for the same look-up in case of
	//   delete records in a variable rec-length db cause some recs in our
	//   disk lookups to be wiped out, thereby falling below minRecSizes
	// . this will set g_errno and return false on error (ENOMEM,...)
	// . older list goes first so newer list can override
	// . remove all negative-keyed recs since Msg5 is a high level msg call

	// . prepare for the merge, grows the buffer
	// . this returns false and sets g_errno on error
	// . should not affect the current list in m_list, only build on top
	if ( ! m_list->prepareForMerge ( m_listPtrs    , 
					 m_numListPtrs , 
					 m_minRecSizes ) ) {
		log("net: Had error preparing to merge lists from %s: %s",
		    base->m_dbname,mstrerror(g_errno));
		return true;
	}		
	QUICKPOLL((m_niceness));
#ifdef _TESTNEWALGO_
	// for testing useBigRootList
	if ( ! m_list2.prepareForMerge ( m_listPtrs    , 
					 m_numListPtrs , 
					 m_minRecSizes ) ) {
		log("net: Had error preparing to merge lists from %s: %s",
		    base->m_dbname,mstrerror(g_errno));
		return true;
	}		
#endif

	// . if size < 32k of don't bother with thread, should be < ~1 ms
	// . it seems to be about 1ms per 10k for tfndb recs
	// . it seems to core dump if we spawn a thread with totalSizes too low
	// . why???
	if ( m_totalSize < 32*1024 ) goto skipThread;

	// if we are an interruptible niceness 1, do not use a thread,
	// we can be interrupted by the alarm callback and serve niceness
	// 0 requests, that is probably better! although the resolution is
	// on like 10ms on those alarms... BUT if you use a smaller
	// mergeBufSize of like 100k, that might make it responsive enough!
	// allow it to do a thread again so we can take advantage of
	// multiple cores, or hyperthreads i guess because i am seeing
	// some missed quickpoll log msgs, i suppose because we did not
	// insert QUICKPOLL() statements in the RdbList::merge_r() code
	//if ( m_niceness >= 1 ) goto skipThread;

	// supder duper hack!
	//if ( m_rdbId == RDB_REVDB ) goto skipThread;

	// i'm not sure why we core in Msg5's call to RdbList::merge_r().
	// the list appears to be corrupt...
	//if ( m_rdbId == RDB_FACEBOOKDB ) goto skipThread;

	// skip it for now
	//goto skipThread;

	//m_waitingForMerge = true;

	// . if size is big, make a thread
	// . let's always make niceness 0 since it wasn't being very
	//   aggressive before
	if ( g_threads.call ( MERGE_THREAD        , // threadType
			      m_niceness          , // m_niceness        , 
			      this                , // state data for callback
			      threadDoneWrapper   ,
			      mergeListsWrapper_r ) ) 
		return false;

	//m_waitingForMerge = false;

	// thread creation failed
	if ( g_errno ) // g_conf.m_useThreads && ! g_threads.m_disabled )
		log(LOG_INFO,
		    "net: Failed to create thread to merge lists. Doing "
		    "blocking merge. (%s)",mstrerror(g_errno));
	// clear g_errno because it really isn't a problem, we just block
	g_errno = 0;
	// come here to skip the thread
 skipThread:
	// repair any corruption
	repairLists_r();
	// do it
	mergeLists_r ();
	// . add m_list to our cache if we should
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . only blocks if calls msg0 to patch a corrupted list
	// . it will handle calling callback if that happens
	return doneMerging();
}

// thread will run this first
void *mergeListsWrapper_r ( void *state , ThreadEntry *t ) {
	// we're in a thread now!
	Msg5 *THIS = (Msg5 *)state;
	// debug msg
	//log("Msg5::mergeListsWrapper: begining threaded merge!");
	// repair any corruption
	THIS->repairLists_r();
	// do the merge
	THIS->mergeLists_r();
	// now cleanUp wrapper will call it
	//pthread_exit ( NULL );
	// bogus return
	return NULL;
}

// . now we're done merging
// . when the thread is done we get control back here, in the main process
void threadDoneWrapper ( void *state , ThreadEntry *t ) {
	// we MAY be in a thread now
	Msg5 *THIS = (Msg5 *)state;
	// debug msg
	//log("msg3 back from merge thread (msg5=%"UINT32")",THIS->m_state);
	// . add m_list to our cache if we should
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . only blocks if calls msg0 to patch a corrupted list
	// . it will handle calling callback if that happens
	if ( ! THIS->doneMerging() ) return;
	// . throw it back into the loop if necessary
	// . only returns true if COMPLETELY done
	if ( THIS->needsRecall() && ! THIS->readList() ) return;
	// sanity check
	if ( THIS->m_calledCallback ) { char *xx=NULL;*xx=0; }
	// we are no longer waiting for the list
	THIS->m_waitingForList = false;
	// set it now
	THIS->m_calledCallback = 3;
	// when completely done call the callback
	THIS->m_callback ( THIS->m_state , THIS->m_list , THIS );
}

// check lists in the thread
void Msg5::repairLists_r ( ) {
	// assume none
	m_hadCorruption = false;
	// return if no need to
	if ( ! m_doErrorCorrection ) return;
	// or if msg3 already check them and they were ok
	if ( m_msg3.m_listsChecked ) return;
	// if msg3 said they were corrupt... this happens when the map
	// is generated over a bad data file and ends up writing the same key
	// on more than 500MB worth of data. so when we try to read a list
	// that has the startkey/endkey covering that key, the read size
	// is too big to ever happen...
	if ( m_msg3.m_hadCorruption ) m_hadCorruption = true;
	// time it
	//m_checkTime = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ ) {
		// . did it breech our minRecSizes?
		// . only check for indexdb, our keys are all size 12
		// . is this a mercenary problem?
		// . cored on 'twelfth night cake'
		// . no... this happens after merging the lists. if we had
		//   a bunch of negative recs we over read anticipating some
		//   recs will be deleted, so it isn't really necessary to
		//   bitch about this here..
		if ( g_conf.m_logDebugDb &&
		     m_rdbId == RDB_POSDB &&
		     m_listPtrs[i]->m_listSize > m_minRecSizes + 12 )
			// just log it for now, maybe force core later
			log(LOG_DEBUG,"db: Index list size is %"INT32" but "
			    "minRecSizes is %"INT32".",
			    m_listPtrs[i]->m_listSize ,
			    m_minRecSizes );
		// this took like 50ms (-O3) on lenny on a 4meg list
		bool status = m_listPtrs[i]->checkList_r(false,
		 // sleep on corruption if doing a sanity check (core dumps)
#ifdef GBSANITYCHECK							 
							 true
#else
		                                         false
#endif
							 );
		// if no errors, check the next list
		if ( status ) continue;
		// . show the culprit file
		// . logging the key ranges gives us an idea of how long
		//   it will take to patch the bad data
		int32_t nn = m_msg3.m_numFileNums;
		// TODO: fix this. can't call Collectiondb::getBase from
		// within a thread!
		RdbBase *base = getRdbBase ( m_rdbId , m_collnum );
		if ( i < nn && base ) {
			int32_t fn = m_msg3.m_fileNums[i];
			BigFile *bf = base->getFile ( fn );
			log("db: Corrupt filename is %s in collnum %"INT32"."
			    ,bf->getFilename()
			    ,(int32_t)m_collnum);
			//key_t sk = m_listPtrs[i]->getStartKey();
			//key_t ek = m_listPtrs[i]->getEndKey  ();
			//log("db: "
			//    "startKey.n1=%"XINT32" n0=%"XINT64" "
			//    "endKey.n1=%"XINT32" n0=%"XINT64"",
			//    sk.n1,sk.n0,ek.n1,ek.n0);
			char *sk = m_listPtrs[i]->getStartKey();
			char *ek = m_listPtrs[i]->getEndKey  ();
			log("db: "
			    "startKey=%s "
			    "endKey=%s ",
			    KEYSTR(sk,m_ks),KEYSTR(ek,m_ks));
		}
		// . remove the bad eggs from the list
		// . TODO: support non-fixed data sizes
		//if ( m_listPtrs[i]->getFixedDataSize() >= 0 )
		m_listPtrs[i]->removeBadData_r();
		//else
		//m_listPtrs[i]->reset();
		// otherwise we have a patchable error
		m_hadCorruption = true;
		// don't add a list with errors to cache, please
		m_addToCache = false;
	}
}

void Msg5::mergeLists_r ( ) {

	// . don't do any merge if this is true
	// . if our fetch of remote list fails, then we'll be called
	//   again with this set to false
	if ( m_hadCorruption ) return;

	// start the timer
	//int64_t startTime = gettimeofdayInMilliseconds();

	// . if the key of the last key of the previous list we read from
	//   is not below startKey, reset the truncation count to avoid errors
	// . if caller does the same read over and over again then
	//   we would do a truncation in error eventually
	// . use m_fileStartKey, not just m_startKey, since we may be doing
	//   a follow-up read
	//if ( m_prevKey >= m_fileStartKey ) m_prevCount = 0;
	if ( KEYCMP(m_prevKey,m_fileStartKey,m_ks)>=0 ) m_prevCount = 0;

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	//RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) {
	//	log("No collection found."); return; }

	/*
	if ( m_rdbId == RDB_POSDB ) {
		m_list->posdbMerge_r (  m_listPtrs      ,
					m_numListPtrs   ,
					m_startKey      ,
					m_minEndKey     ,
					m_minRecSizes   ,
					m_removeNegRecs ,
					m_prevKey       ,
					&m_prevCount    ,
					&m_dupsRemoved  ,
					base->m_rdb->m_rdbId ,
					&m_filtered     ,
					m_isRealMerge   , // do group mask?
					m_isRealMerge   , // is real merge?
					m_niceness      );

	}
	*/

	int32_t niceness = m_niceness;
	if ( niceness > 0  ) niceness = 2;
	if ( m_isRealMerge ) niceness = 1;

	// . all lists must be constrained because indexMerge_r does not check
	//   merged keys to see if they're in [startKey,endKey]
	// . RIGHT NOW indexdb is the only one that uses half keys!!
	// . indexMerge_r() will return false and set g_errno on error
	// . this is messing up linkdb!!
	/*
	if ( base->useHalfKeys() && 
	     m_rdbId != RDB_POSDB && 
	     m_rdbId != RDB2_POSDB2 && 
	     1 == 3 ) {
		// disable for now!
		char *xx=NULL;*xx=0; 
		// always assume to use it
		bool useBigRootList = true;
		// must include the first file always
		if ( m_startFileNum != 0 ) useBigRootList = false;
		// must be 12 bytes per key
		if ( m_ks != 12 ) useBigRootList = false;
		// just indexdb for now, tfndb has special merge rules
		if ( m_rdbId != RDB_POSDB && m_rdbId != RDB2_POSDB2 ) 
			useBigRootList = false;
		// do not do if merging files on disk
		if ( m_isRealMerge          ) useBigRootList = false;
		// turn off for now
		//useBigRootList = false;
		// do the merge
		m_list->indexMerge_r (  m_listPtrs      ,
					m_numListPtrs   ,
					m_startKey      ,
					m_minEndKey     ,
					m_minRecSizes   ,
					m_removeNegRecs ,
					m_prevKey       ,
					&m_prevCount    ,
					//g_indexdb.getTruncationLimit() ,
					m_indexdbTruncationLimit ,
					&m_dupsRemoved  ,
					//base->m_rdb == g_tfndb.getRdb() ,
					base->m_rdb->m_rdbId ,
					&m_filtered     ,
					m_isRealMerge   , // do group mask?
					m_isRealMerge   , // is real merge?
					useBigRootList  ,// useBigRootList?
					niceness      );
#ifdef _TESTNEWALGO_
		// for testing useBigRootList
		if ( useBigRootList ) {
			logf(LOG_DEBUG,"db: TRYING bit root list algo.");
			m_list2.indexMerge_r (  m_listPtrs      ,
						m_numListPtrs   ,
						m_startKey      ,
						m_minEndKey     ,
						m_minRecSizes   ,
						m_removeNegRecs ,
						m_prevKey       ,
						&m_prevCount    ,
						m_indexdbTruncationLimit ,
						&m_dupsRemoved  ,
						base->m_rdb->m_rdbId ,
						&m_filtered     ,
						m_isRealMerge   , //dogrpmask?
						m_isRealMerge   , 
						false           );//bigRootList
			// sanity check
			int32_t size1 = m_list->m_listSize;
			int32_t size2 = m_list2.m_listSize;
			char *list1 = (char *)m_list->m_list;
			char *list2 = (char *)m_list2.m_list;
			if ( size1 != size2 ||
			     memcmp ( list1 , list2 , size1 ) != 0 ) {
				log("db: Got bad list.");
				m_list->printList();
				m_list2.printList();
				//char *xx = NULL; *xx = 0;
			}
		}
#endif
	}
	*/

	//g_conf.m_useThreads = false;

	// . old Msg3 notes:
	// . otherwise, merge the lists together
	// . this may call growList() via RdbList::addRecord/Key() but it 
	//   shouldn't since we called RdbList::prepareForMerge() above
	// . we aren't allowed to do allocating in a thread!
	// . TODO: only merge the recs not cached, [m_fileStartKey, endKey]
	// . merge() might shrink m_endKey in diskList if m_minRecSizes
	//   contrained us OR it might decrement it by 1 if it's a negative key
	// .........................
	// . this MUST start at m_list->m_listPtr cuz this may not be the
	//   1st time we had to dive in to disk, due to negative rec
	//   annihilation
	// . old finalList.merge_r() Msg5 notes:
	// . use startKey of tree
	// . NOTE: tree may contains some un-annihilated key pairs because
	//   one of them was PROBABLY in the dump queue and we decided in
	//   Rdb::addRecord() NOT to do the annihilation, therefore it's good
	//   to do the merge to do the annihilation
	//else
	m_list->merge_r ( m_listPtrs      , 
			  m_numListPtrs   , 
			  m_startKey      , 
			  m_minEndKey     , 
			  m_minRecSizes   ,
			  m_removeNegRecs ,
			  //getIdFromRdb ( base->m_rdb ) ,
			  m_rdbId ,
			  &m_filtered     ,
			  NULL,//m_tfns          , // used for titledb
			  NULL,//&m_tfndbList    , // used for titledb
			  m_isRealMerge   ,
			  niceness      );
	
	// maintain this info for truncation purposes
	if ( m_list->isLastKeyValid() ) 
		//m_prevKey = m_list->getLastKey();
		KEYSET(m_prevKey,m_list->getLastKey(),m_ks);
	else {
		// . lastKey should be set and valid if list is not empty
		// . we need it for de-duping dup tfndb recs that fall on our
		//   read boundaries
		if ( m_list->m_listSize > 0 )
			log(LOG_LOGIC,"db: Msg5. Last key invalid.");
		m_prevCount = 0;
	}
}


// . this returns false if blocked, true otherwise
// . sets g_errno on error
// . only blocks if calls msg0 to patch a corrupted list
// . it will handle calling callback if that happens
// . this is called when all files are done reading in m_msg3
// . sets g_errno on error
// . problem: say maxRecSizes is 1200 (1000 keys)
// . there are 10000 keys in the [startKey,endKey] range
// . we read 1st 1000 recs from the tree and store in m_treeList
// . we read 1st 1000 recs from disk 
// . all recs in tree are negative and annihilate the 1000 recs from disk
// . we are left with an empty list
bool Msg5::doneMerging ( ) {

	//m_waitingForMerge = false;

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;

	// . if there was a merge error, bitch about it
	// . Thread class should propagate g_errno when it was set in a thread
	if ( g_errno ) {
		log("net: Had error merging lists from %s: %s.",
		    base->m_dbname,mstrerror(g_errno));
		return true;
	}

	// . was a list corrupted?
	// . if so, we did not even begin the merge yet
	// . try to get the list from a remote brother
	// . if that fails we have already removed the bad data, so begin
	//   our first merge
	if ( m_hadCorruption ) {
		// log it here, cuz logging in thread doesn't work too well
		log("net: Encountered a corrupt list in rdb=%s collnum=%"INT32"",
		    base->m_dbname,(int32_t)m_collnum);
		// remove error condition, we removed the bad data in thread
		
		m_hadCorruption = false;

// 		if(g_numCorrupt++ >= g_conf.m_maxCorruptLists &&
// 		   g_conf.m_maxCorruptLists > 0) {
		g_numCorrupt++;
		if(g_conf.m_maxCorruptLists > 0 &&
		   (g_numCorrupt % g_conf.m_maxCorruptLists) == 0) {
			char msgbuf[1024];
			Host *h = g_hostdb.getHost ( 0 );
			snprintf(msgbuf, 1024,
				 "%"INT32" corrupt lists. "
				 "cluster=%s "
				 "host=%"INT32"",
				 g_numCorrupt,
				 iptoa(h->m_ip),
				 g_hostdb.m_hostId);
			g_pingServer.sendEmail(NULL, msgbuf);
		}

		// try to get the list from remote host
		if ( ! getRemoteList() ) return false;
		// note that
		if ( ! g_errno ) {
			log("net: got remote list without blocking");
			char *xx=NULL;*xx=0;
		}
		// if it set g_errno, it could not get a remote list
		// so try to make due with what we have
		if ( g_errno ) {
			// log a msg, we actually already removed it in thread
			log("net: Removed corrupted data.");
			// clear error
			g_errno = 0;
			// . merge the modified lists again
			// . this is not in a thread
			// . it should not block
			mergeLists_r();
		}
	}

	if ( m_isRealMerge )
		log(LOG_DEBUG,"db: merged list is %"INT32" bytes long.",
		    m_list->m_listSize);

	// log it
	int64_t now ;
	// only time it if we actually did a merge, check m_startTime
	if ( m_startTime ) now = gettimeofdayInMilliseconds();
	else               now = 0;
	int64_t took = now - m_startTime ;
	if ( g_conf.m_logTimingNet ) {
		if ( took > 5 )
			log(LOG_INFO,
			    "net: Took %"UINT64" ms to do merge. %"INT32" lists merged "
			     "into one list of %"INT32" bytes.",
			     took , m_numListPtrs , m_list->getListSize() );
		//log("Msg5:: of that %"UINT64" ms was in checkList_r()s",
		//     m_checkTime );
	}


	// . add the stat
	// . use turquoise for time to merge the disk lists
	// . we should use another color rather than turquoise
	// . these clog up the graph, so only log if took more than 1 ms
	// . only time it if we actually did a merge, check m_startTime
	if ( took > 1 && m_startTime )
		g_stats.addStat_r ( m_minRecSizes ,
				    m_startTime , 
				    now ,
				    //"rdb_list_merge",
				    0x0000ffff );

	// . scan merged list for problems
	// . this caught an incorrectly set m_list->m_lastKey before
#ifdef GBSANITYCHECK
	m_list->checkList_r ( false , true , m_rdbId );
#endif

	// . all done if we did not merge the lists
	// . posdb doesn't need that so much for calling intersect9_r()
	if ( ! m_mergeLists )
		return true;

	// . TODO: call freeList() on each m_list[i] here rather than destructr
	// . free all lists we used
	// . some of these may be from Msg3, some from cache, some from tree
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ ) {
		m_listPtrs[i]->freeList();
		m_listPtrs[i] = NULL;
	}
	// and the tree list
	m_treeList.freeList();
	// . update our m_newMinRecSizes
	// . NOTE: this now ignores the negative records in the tree
	int64_t newListSize = m_list->getListSize();

	// scale proportionally based on how many got removed during the merge
	int64_t percent = 100LL;
	int64_t net = newListSize - m_oldListSize;
	// add 5% for inconsistencies
	if ( net > 0 ) percent =(((int64_t)m_newMinRecSizes*100LL)/net)+5LL;
	else           percent = 200;
	if ( percent <= 0 ) percent = 1;
	// set old list size in case we get called again
	m_oldListSize = newListSize;

	//int32_t delta = m_minRecSizes - (int32_t)newListSize;

	// how many recs do we have left to read?
	m_newMinRecSizes = m_minRecSizes - (int32_t)newListSize;
	
	// return now if we met our minRecSizes quota
	if ( m_newMinRecSizes <= 0 ) return true;

	// if we gained something this round then try to read the remainder
	//if ( net > 0 ) m_newMinRecSizes = delta;


	// otherwise, scale proportionately
	int32_t nn = ((int64_t)m_newMinRecSizes * percent ) / 100LL;
	if ( percent > 100 ) {
		if ( nn > m_newMinRecSizes ) m_newMinRecSizes = nn;
		else                         m_newMinRecSizes = 0x7fffffff;
	}
	else m_newMinRecSizes = nn;

	// . for every round we get call increase by 10 percent
	// . try to fix all those negative recs in the rebalance re-run
	m_newMinRecSizes *= (int32_t)(1.0 + (m_round * .10));

	// wrap around?
	if ( m_newMinRecSizes < 0 || m_newMinRecSizes > 1000000000 )
		m_newMinRecSizes = 1000000000;

	
	QUICKPOLL(m_niceness);
	// . don't exceed original min rec sizes by 5 i guess
	// . watch out for wrap
	//int32_t max = 5 * m_minRecSizes ;
	//if ( max < m_minRecSizes ) max = 0x7fffffff;
	//if ( m_newMinRecSizes > max && max > m_minRecSizes )
	//	m_newMinRecSizes = max;

	// keep this above a certain point because if we didn't make it now
	// we got negative records messing with us
	if ( m_rdbId != RDB_DOLEDB &&
	     m_newMinRecSizes < 128000 ) m_newMinRecSizes = 128000;
	// . update startKey in case we need to read more
	// . we'll need to read more if endKey < m_endKey && m_newMinRecSizes 
	//   is positive
	// . we read more from files AND from tree
	//m_fileStartKey  = m_list->getEndKey() ;
	//m_fileStartKey += (uint32_t)1;
	KEYSET(m_fileStartKey,m_list->getEndKey(),m_ks);
	KEYADD(m_fileStartKey,1,m_ks);
	return true;
}

void gotRemoteListWrapper( void *state );//, RdbList *list ) ;

int32_t g_isDumpingRdbFromMain = 0;

// . if we discover one of the lists we read from a file is corrupt we go here
// . uses Msg5 to try to get list remotely
bool Msg5::getRemoteList ( ) {

	// skip this part if doing a cmd line 'gb dump p main 0 -1 1' cmd or
	// similar to dump out a local rdb.
	if ( g_isDumpingRdbFromMain ) {
		g_errno = 1;
		return true;
	}

	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . get list from ALL files, not just m_startFileNum/m_numFiles
	//   since our files may not be the same
	// . if doRemotely parm is not supplied replying hostId is unspecified
	// get our twin host, or a redundant host in our group
	//Host *group = g_hostdb.getGroup ( g_hostdb.m_groupId );
	Host *group = g_hostdb.getMyShard();
	int32_t  n     = g_hostdb.getNumHostsPerShard();
	// . if we only have 1 host per group, data is unpatchable
	// . we should not have been called if this is the case!!
	if ( n == 1 ) {
		g_errno = EBADENGINEER;
		//log("Msg5::gotRemoteList: no twins. data unpatchable.");
		return true;
	}
	// tfndb is not shareable, since it has tfns
	if ( m_rdbId == RDB_TFNDB ) {
		g_errno = EBADENGINEER;
		log("net: Cannot patch tfndb data from twin because it is "
		    "not interchangable. Tfndb must be regenerated.");
		return true;
	}
	if ( m_rdbId == RDB_STATSDB ) {
		g_errno = EBADENGINEER;
		log("net: Cannot patch statsdb data from twin because it is "
		    "not interchangable.");
		return true;
	}
	// tell them about
	log("net: Getting remote list from twin instead.");
	// make a new Msg0 for getting remote list
	try { m_msg0 = new ( Msg0 ); }
	// g_errno should be set if this is NULL
	catch ( ... ) {
		g_errno = ENOMEM;
		log("net: Could not allocate memory to get from twin.");
		return true;
	}
	mnew ( m_msg0 , sizeof(Msg0) , "Msg5" );
	// select our twin
	int32_t i;
	for ( i = 0 ; i < n ; i++ ) 
		if ( group[i].m_hostId != g_hostdb.m_hostId ) break;
	Host *h = &group[i];
	// get our groupnum. the column #
	int32_t forceParitySplit = h->m_shardNum;//group;
	// translate base to an id, for the sake of m_msg0
	//char rdbId = getIdFromRdb ( base->m_rdb );
	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . get list from ALL files, not just m_startFileNum/m_numFiles
	//   since our files may not be the same
	// . if doRemotely parm is not supplied replying hostId is unspecified
	// . make minRecSizes as big as possible because it gets from ALL
	//   files and from tree!
	// . just make it 256k for now lest, msg0 bitch about it being too big
	//   if rdbId == RDB_INDEXDB passed the truncation limit
	// . wait forever for this host to reply... well, at least a day that
	//   way if he's dead we'll wait for him to come back up to save our
	//   data
	if ( ! m_msg0->getList ( h->m_hostId          ,
				 h->m_ip              ,
				 h->m_port            ,
				 0                    , // max cached age
				 false                , // add to cache?
				 m_rdbId              , // rdbId
				 m_collnum            ,
				 m_list               ,
				 m_startKey           ,
				 m_endKey             ,
				 m_minRecSizes        , // was 256k minRecSizes
				 this                 ,
				 gotRemoteListWrapper ,
				 m_niceness           ,
				 false                , // do error correction?
				 true                 , // include tree?
				 true                 , // do merge? (obsolete)
				 -1                   , // first hostid
				 0                    , // startFileNum
				 -1                   , // numFiles (-1=all)
				 60*60*24             , // timeout in secs
				 -1                   , // syncPoint
				 -1                   , // preferLocalReads
				 NULL                 , // msg5
				 NULL                 , // msg5b
				 m_isRealMerge        , // merging files?
//#ifdef SPLIT_INDEXDB
				 m_allowPageCache     , // allow page cache?
				 false                , // force local Indexdb
				 false                , // doIndexdbSplit
				 // "forceParitySplit" is a group # 
				 // (the groupId is a mask)
				 forceParitySplit     ))
//#else
//				 m_allowPageCache     ))// allow page cache?
//#endif
		return false;
	// this is strange
	log("msg5: call to msg0 did not block");
	// . if we did not block then call this directly
	// . return false if it blocks
	return gotRemoteList ( ) ;
}

void gotRemoteListWrapper( void *state ) { // , RdbList *list ) {
	Msg5 *THIS = (Msg5 *)state;
	// return if this blocks
	if ( ! THIS->gotRemoteList() ) return;
	// sanity check
	if ( THIS->m_calledCallback ) { char *xx=NULL;*xx=0; }
	// we are no longer waiting for the list
	THIS->m_waitingForList = false;
	// set it now
	THIS->m_calledCallback = 4;
	// if it doesn't block call the callback, g_errno may be set
	THIS->m_callback ( THIS->m_state , THIS->m_list , THIS );
}

// returns false if it blocks
bool Msg5::gotRemoteList ( ) {
	// free the Msg0
	mdelete ( m_msg0 , sizeof(Msg0) , "Msg5" );
	delete ( m_msg0 );
	// return true now if everything ok
	if ( ! g_errno ) {
		// . i modified checkList to set m_lastKey if it is not set
		// . we need it for the big merge for getting next key in
		//   RdbDump.cpp
		// . if it too is invalid, we are fucked
		if ( ! m_list->checkList_r ( false , false ) ) {
			log("net: Received bad list from twin.");
			g_errno = ECORRUPTDATA;
			goto badList;
		}
		// . success messages
		// . logging the key ranges gives us an idea of how long
		//   it will take to patch the bad data
		//key_t sk = m_list->getStartKey();
		//key_t ek = m_list->getEndKey  ();
		//log("net: Received good list from twin. Requested %"INT32" bytes "
		//    "and got %"INT32". "
		//    "startKey.n1=%"XINT32" n0=%"XINT64" "
		//    "endKey.n1=%"XINT32" n0=%"XINT64"",
		//    m_minRecSizes , m_list->getListSize() ,
		//    sk.n1,sk.n0,ek.n1,ek.n0);
		char *sk = m_list->getStartKey();
		char *ek = m_list->getEndKey  ();
		log("net: Received good list from twin. Requested %"INT32" bytes "
		    "and got %"INT32". "
		    "startKey=%s endKey=%s",
		    m_minRecSizes , m_list->getListSize() ,
		    KEYSTR(sk,m_ks),KEYSTR(ek,m_ks));
		// . HACK: fix it so end key is right
		// . TODO: fix this in Msg0::gotReply()
		// . if it is empty, then there must be nothing else left
		//   since the endKey was maxed in call to Msg0::getList()
		QUICKPOLL(m_niceness);
		if ( ! m_list->isEmpty() )
			m_list->setEndKey ( m_list->getLastKey() );
		//key_t k ;
		//k = m_list->getStartKey();
		char *k = m_list->getStartKey();
		log(LOG_DEBUG,
		    //"net: Received list skey.n1=%08"XINT32" skey.n0=%016"XINT64"." ,
		    //  k.n1 , k.n0 );
		    "net: Received list skey=%s." ,
		      KEYSTR(k,m_ks) );
		k = m_list->getEndKey();
		log(LOG_DEBUG,
		    //"net: Received list ekey.n1=%08"XINT32" ekey.n0=%016"XINT64"." ,
		    //  k.n1 , k.n0 );
		    "net: Received list ekey=%s",
		      KEYSTR(k,m_ks) );
		if ( ! m_list->isEmpty() ) {
			k = m_list->getLastKey();
			//log(LOG_DEBUG,"net: Received list Lkey.n1=%08"XINT32" "
			//      "Lkey.n0=%016"XINT64"" , k.n1 , k.n0 );
			log(LOG_DEBUG,"net: Received list Lkey=%s",
			    KEYSTR(k,m_ks) );
		}
		//log("Msg5::gotRemoteList: received list is good.");
		return true;
	}

 badList:
	// it points to a corrupted list from twin, so reset
	m_list->reset();
	// because we passed m_list to Msg0, it called m_list->reset()
	// which set our m_mergeMinListSize to -1, so we have to call
	// the prepareForMerge() again
	if ( ! m_list->prepareForMerge ( m_listPtrs    , 
					 m_numListPtrs , 
					 m_minRecSizes ) ) {
		log("net: Had error preparing for merge: %s.",
		    mstrerror(g_errno));
		return true;
	}		
	// . if g_errno is timed out we couldn't get a patch list
	// . turn off error correction and try again
	log("net: Had error getting remote list: %s.", mstrerror(g_errno) );
	log("net: Merging repaired lists.");
	// clear g_errno so RdbMerge doesn't freak out
	g_errno = 0;
	// . we have the lists ready to merge
	// . hadCorruption should be false at this point
	mergeLists_r();
	// process the result
	return doneMerging();
}
