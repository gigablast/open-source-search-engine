// TODO: if the first 20 or so do NOT have the same hostname, then stop
// and set all clusterRecs to CR_OK

#include "gb-include.h"

#include "Msg51.h"
#include "Clusterdb.h"
//#include "CollectionRec.h"
#include "Stats.h"
#include "HashTableT.h"
#include "HashTableX.h"

// . these must be 1-1 with the enums above
// . used for titling the counts of g_stats.m_filterStats[]
char *g_crStrings[] = {
	"cluster rec not found"  ,  // 0
	"uninitialized"          ,
	"got clusterdb record"   ,
	"has adult bit"          ,
	"has wrong language"     ,
	"clustered"              ,
	"malformed url"          ,
	"banned url"             ,
	"missing query terms"    ,
	"summary error"          ,
	"duplicate"              ,
	"dup event summary"      ,
	"duplicate topic"        ,
	"clusterdb error (subcount of visible)" ,
        "duplicate url",

	// these are for buzzlogic (buzz)
	//"below min date"         ,
	//"above max date"         ,
	//"below min inlinks"      ,
	//"above max inlinks"      ,

	"wasted summary lookup"  ,
	"visible"                ,

	"blacklisted"            ,
	"ruleset filtered"       ,

	"end -- do not use"      
};
	
RdbCache s_clusterdbQuickCache;
static bool     s_cacheInit = false;

static void gotClusterRecWrapper51 ( void *state );

Msg51::Msg51 ( ) {
	m_clusterRecs     = NULL;
	m_clusterRecsSize = 0;
	m_clusterLevels   = NULL;
}

Msg51::~Msg51 ( ) {
	reset();
}

void Msg51::reset ( ) {
	// only free this if we allocated it
	if ( m_clusterRecsSize && m_clusterRecs )
		mfree ( m_clusterRecs , m_clusterRecsSize , "Msg51" );
	m_clusterRecsSize = 0;
	m_clusterRecs     = NULL;
	m_clusterLevels   = NULL;
}


// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg51::getClusterRecs ( int64_t     *docIds                   ,
			     char          *clusterLevels            ,
			     key_t         *clusterRecs              ,
			     int32_t           numDocIds                ,
			     //char          *coll                     ,
			     collnum_t collnum ,
			     int32_t           maxCacheAge              ,
			     bool           addToCache               ,
			     void          *state                    ,
			     void        (* callback)( void *state ) ,
			     // blacklisted sites
			     int32_t           niceness                 ,
			     // output
			     bool           isDebug                  ) {
	// reset this msg
	reset();
	// warning
	if ( collnum < 0 ) log(LOG_LOGIC,"net: NULL collection. msg51.");
	// get the collection rec
	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	// return true on error, g_errno should already be set
	if ( ! cr ) {
		log("db: msg51. Collection rec null for collnum %"INT32".", 
		    (int32_t)collnum);
		g_errno = EBADENGINEER;
		char *xx=NULL; *xx=0;
		return true;
	}
	// keep a pointer for the caller
	m_maxCacheAge   = maxCacheAge;
	m_addToCache    = addToCache;
	m_state         = state;
	m_callback      = callback;
	//m_coll          = coll;
	//m_collLen       = gbstrlen(coll);
	m_collnum = collnum;
	// these are storage for the requester
	m_docIds        = docIds;
	m_clusterLevels = clusterLevels;
	m_clusterRecs   = clusterRecs;
	m_numDocIds     = numDocIds;
	m_isDebug       = isDebug;

	// bail if none to do
	if ( m_numDocIds <= 0 ) return true;

	// . we do like 15 sends at a time
	// . we are often called multiple times have list of docids
	//   is grown, so don't redo the ones we've already done
	m_nexti      = 0;
	// for i/o mostly
	m_niceness   = niceness;
	m_errno      = 0;
	// caching info
	m_maxCacheAge = maxCacheAge;
	m_addToCache  = addToCache;

	// alloc cluster rec buf if none given
	m_clusterRecs = clusterRecs;

	// reset these
	m_numRequests = 0;
	m_numReplies  = 0;
	// clear these
	for ( int32_t i = 0 ; i < MSG51_MAX_REQUESTS ; i++ )
		m_msg0[i].m_inUse = false;
	// . do gathering
	// . returns false if blocked, true otherwise
	// . send up to MSG51_MAX_REQUESTS requests at the same time
	return sendRequests ( -1 );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error (and m_errno)
// . k is a hint of which msg0 to use
// . if k is -1 we do a complete scan to find available m_msg0[x]
bool Msg51::sendRequests ( int32_t k ) {

 sendLoop:

	// bail if none left, return false if still waiting
	if ( m_numRequests - m_numReplies >= MSG51_MAX_REQUESTS ) return false;

	bool isDone = false;
	if ( m_nexti >= m_numDocIds ) isDone = true;

	// any requests left to send?
	if ( isDone ) {
		// we are still waiting on replies, so we blocked...
		if ( m_numRequests > m_numReplies ) return false;
		// we are done!
		return true;
	}

	// sanity check
	if ( m_clusterLevels[m_nexti] <  0      ||
	     m_clusterLevels[m_nexti] >= CR_END   ) {
		char *xx = NULL; *xx = 0; }

	// skip if we already got the rec for this guy!
	if ( m_clusterLevels[m_nexti] != CR_UNINIT ) {
		m_nexti++;
		goto sendLoop;
	}

	// . check our quick local cache to see if we got it
	// . use a max age of 1 hour
	// . this cache is primarly meant to avoid repetetive lookups
	//   when going to the next tier in Msg3a and re-requesting cluster
	//   recs for the same docids we did a second ago
	RdbCache *c = &s_clusterdbQuickCache;
	if ( ! s_cacheInit ) c = NULL;
	int32_t      crecSize;
	// key_t     crec;
	char     *crecPtr = NULL;
	key_t     ckey = (key_t)m_docIds[m_nexti];
	bool found = false;
	if ( c )
		found = c->getRecord ( m_collnum    ,
				       ckey      , // cache key
				       &crecPtr  , // pointer to it
				       &crecSize ,
				       false     , // do copy?
				       3600      , // max age in secs
				       true      , // inc counts?
				       NULL      );// cachedTime
	if ( found ) {
		// sanity check
		if ( crecSize != sizeof(key_t) ) { char *xx = NULL; *xx = 0; }
		m_clusterRecs[m_nexti] = *(key_t *)crecPtr;
		// it is no longer CR_UNINIT, we got the rec now
		m_clusterLevels[m_nexti] = CR_GOT_REC;
		// debug msg
		//logf(LOG_DEBUG,"query: msg51 getRec k.n0=%"UINT64" rec.n0=%"UINT64"",
		//     ckey.n0,m_clusterRecs[m_nexti].n0);
		m_nexti++;
		goto sendLoop;
	}

	// . do not hog all the udpserver's slots!
	// . must have at least one outstanding reply so we can process
	//   his reply and come back here...
	if ( g_udpServer.getNumUsedSlots() > 1000 &&
	     m_numRequests > m_numReplies ) return false;

	// find empty slot
	int32_t slot ;

	// ignore bogus hints
	if ( k >= MSG51_MAX_REQUESTS ) k = -1;

	// if hint was provided use that
	if ( k >= 0 && ! m_msg0[k].m_inUse ) slot = k;
	// otherwise, do a scan for the empty slot
	else {
		for ( slot = 0 ; slot < MSG51_MAX_REQUESTS ; slot++ )
			// break out if available
			if ( ! m_msg0[slot].m_inUse ) break;
	}

	// sanity check -- must have one!!
	if ( slot >= MSG51_MAX_REQUESTS ) { char *xx = NULL ; *xx=0 ; }

	// send it, returns false if blocked, true otherwise
	sendRequest ( slot );

	// update any hint to make our loop more efficient
	if ( k >= 0 ) k++;
	
	goto sendLoop;
}

// . send using m_msg0s[i] class
bool Msg51::sendRequest ( int32_t    i ) {
	// what is the docid?
	int64_t  d;
	// point to where we want the last 64 bits of the cluster rec
	// to be store, "dataPtr"
	void    *dataPtr = NULL;

	// save it
	int32_t ci = m_nexti;
	// store where the cluster rec will go
	dataPtr = (void *)(PTRTYPE)ci;
	// what's the docid?
	d = m_docIds[m_nexti];
	// advance so we do not do this docid again 
	m_nexti++;

	// use a hack to store this
	m_msg0[i].m_parent  = this;
	m_msg0[i].m_slot51  = i;
	m_msg0[i].m_dataPtr = dataPtr;
	m_msg0[i].m_inUse   = true;
	// count it
	m_numRequests++;
	// lookup in clusterdb, need a start and endkey
	key_t startKey = g_clusterdb.makeFirstClusterRecKey ( d );
	key_t endKey   = g_clusterdb.makeLastClusterRecKey  ( d );
	
	// bias clusterdb lookups (from Msg22.cpp)
	int32_t           numTwins     = g_hostdb.getNumHostsPerShard();
	int64_t      sectionWidth = (DOCID_MASK/(int64_t)numTwins) + 1;
	int32_t           hostNum      = (d & DOCID_MASK) / sectionWidth;
	int32_t           numHosts     = g_hostdb.getNumHostsPerShard();
	uint32_t  shardNum     = getShardNum(RDB_CLUSTERDB,&startKey);
	Host          *hosts        = g_hostdb.getShard ( shardNum );
	if ( hostNum >= numHosts ) { char *xx = NULL; *xx = 0; }
	int32_t firstHostId = hosts [ hostNum ].m_hostId ;

	// if we are doing a full split, keep it local, going across the net
	// is too slow!
	//if ( g_conf.m_fullSplit ) firstHostId = -1;
	firstHostId = -1;
	
	// . send the request for the cluster rec, use Msg0
	// . returns false and sets g_errno on error
	// . otherwise, it blocks and returns true
	bool s = m_msg0[i].getList ( -1            , // hostid
				     -1            , // ip
				     -1            , // port 
				     m_maxCacheAge ,
				     m_addToCache  ,
				     RDB_CLUSTERDB ,
				     m_collnum        ,
				     &m_lists[i]   ,
				     (char *)&startKey      ,
				     (char *)&endKey        ,
				     36            , // minRecSizes 
				     &m_msg0[i]    , // state
				     gotClusterRecWrapper51  ,
				     m_niceness    ,
				     true        , // doErrorCorrection
				     true        , // includeTree
				     true        , // doMerge?
				     firstHostId ,
				     0           , // startFileNum
				     -1          , // numFiles
				     30          , // timeout
				     -1          , // syncPoint
				     false       , // preferLocalReads
				     &m_msg5[i]  , // use for local reads
				     NULL        , // msg5b
				     false       , // isRealMerge?
				     true        , // allow page cache?
				     false       , // force local indexdb?
				     false       , // noSplit?
				     -1          );// forceParitySplit

	// loop for more if blocked, slot #i is used, block it
	//if ( ! s ) { i++; continue; }
	if ( ! s ) { 
		// only wanted this for faster disk page cache hitting so make
		// sure it is not "double used" by another msg0
		//m_msg0[i].m_msg5 = NULL; 
		return false; 
	}
	// otherwise, process the response
	gotClusterRec ( &m_msg0[i] );
	return true;
}

void gotClusterRecWrapper51 ( void *state ) {//, RdbList *rdblist ) {
	Msg0 *msg0 = (Msg0 *)state;
	// extract our class form him -- a hack
	Msg51 *THIS = (Msg51 *)msg0->m_parent;
	// sanity check
	if ( &THIS->m_msg0[msg0->m_slot51] != msg0 ) {
		char *xx = NULL; *xx =0; }
	// process it
	THIS->gotClusterRec ( msg0 ) ;
	// get slot number for re-send on this slot
	int32_t    k = msg0->m_slot51;
	// . if not all done, launch the next one
	// . this returns false if blocks, true otherwise
	if ( ! THIS->sendRequests ( k ) ) return;
	// we don't need to go on if we're not doing deduping
	THIS->m_callback ( THIS->m_state );
	return;
}

// . sets m_errno to g_errno if not already set
void Msg51::gotClusterRec ( Msg0 *msg0 ) { //, RdbList *list ) {

	// count it
	m_numReplies++;

	// free up
	msg0->m_inUse = false;

	RdbList *list = msg0->m_list;

	// update m_errno if we had an error
	if ( ! m_errno ) m_errno = g_errno;

	if ( g_errno ) 
		// print error
		log(LOG_DEBUG,
		    "query: Had error getting cluster info got docId=d: "
		    "%s.",mstrerror(g_errno));

	// get docid
	//key_t *startKey = (key_t *)msg0->m_startKey;
	//int64_t docId = g_clusterdb.getDocId ( *startKey );

	// this doubles as a ptr to a cluster rec
	int32_t    ci = (int32_t   )(PTRTYPE)msg0->m_dataPtr;
	// get docid
	int64_t docId = m_docIds[ci];
	// assume error!
	m_clusterLevels[ci] = CR_ERROR_CLUSTERDB;

	// bail on error
	if ( g_errno || list->getListSize() < 12 ) {
		//log(LOG_DEBUG,
		//    "build: clusterdb rec for d=%"INT64" dptr=%"UINT32" "
		//     "not found. where is it?", docId, (int32_t)ci);
		g_errno = 0;
		return;
	}

	// . steal rec from this multicast
	// . point to cluster rec, a int32_t   
	key_t *rec = &m_clusterRecs[ci];

	// store the cluster rec itself
	*rec = *(key_t *)(list->m_list);
	// debug note
	log(LOG_DEBUG,
	    "build: had clusterdb SUCCESS for d=%"INT64" dptr=%"UINT32" "
	    "rec.n1=%"XINT32",%016"XINT64" sitehash26=0x%"XINT32".", (int64_t)docId, (int32_t)ci,
	    rec->n1,rec->n0,
	    g_clusterdb.getSiteHash26((char *)rec));

	// check for docid mismatch
	int64_t docId2 = g_clusterdb.getDocId ( rec );
	if ( docId != docId2 ) {
		logf(LOG_DEBUG,"query: docid mismatch in clusterdb.");
		return;
	}

	// it is legit, set to CR_OK
	m_clusterLevels[ci] = CR_OK;

	// int16_tcut
	RdbCache *c = &s_clusterdbQuickCache;
	
	// . init the quick cache
	// . use 100k
	if ( ! s_cacheInit && 
	     c->init ( 200*1024      ,  // maxMem
		       sizeof(key_t) ,  // fixedDataSize (clusterdb rec)
		       false         ,  // support lists
		       10000         ,  // max recs
		       false         ,  // use half keys?
		       "clusterdbQuickCache" ,
		       false         ,  // load from disk?
		       sizeof(key_t) ,  // cache key size
		       sizeof(key_t) )) // cache key size
		// only init once if successful
		s_cacheInit = true;

	// debug msg
	//logf(LOG_DEBUG,"query: msg51 addRec k.n0=%"UINT64" rec.n0=%"UINT64"",docId,
	//     rec->n0);

	// . add the record to our quick cache as a int64_t
	// . ignore any error
	if ( s_cacheInit )
		c->addRecord ( m_collnum        ,
			       (key_t)docId  , // docid is key
			       (char *)rec   ,
			       sizeof(key_t) , // recSize
			       0             );// timestamp

	// clear it in case the cache set it, we don't care
	g_errno = 0;
}

// . cluster the docids based on the clusterRecs
// . returns false and sets g_errno on error
// . if maxDocIdsPerHostname is -1 do not do hostname clsutering
bool setClusterLevels ( key_t     *clusterRecs          ,
			int64_t *docIds               ,
			int32_t       numRecs              ,
			int32_t       maxDocIdsPerHostname ,
			bool       doHostnameClustering ,
			bool       familyFilter         ,
			char       langFilter           ,
			bool       isDebug              ,
			// output to clusterLevels[]
			char    *clusterLevels        ) {
	
	if ( numRecs <= 0 ) return true;

	// skip if not clustering on anything
	//if ( ! doHostnameClustering && ! familyFilter && langFilter <= 0 ) {
	//	memset ( clusterLevels, CR_OK, numRecs );
	//	return true;
	//}

	// how many negative site hashes do we have?
	// count how many docids we got, they are a cgi value, so represented
	// in ascii separated by +'s. i.e. "12345+435322+3439333333"
	//HashTableT <int64_t,char> sht;
	//if ( ! hashFromString ( &sht , noSiteIds ) ) return false;
	//bool checkNegative = ( sht.getNumSlotsUsed() > 0 );

	HashTableX ctab;
	// init to 2*numRecs for speed. use 0 for niceness!
	if ( ! ctab.set ( 8 , 4 , numRecs * 2,NULL,0,false,0,"clustertab" ) )
		return false;

	// time it
	u_int64_t startTime = gettimeofdayInMilliseconds();

	// init loop counter vars
	int32_t           i     = -1;
	int32_t           count = 0;
	uint32_t  score = 0;
	char          *crec ;
	int64_t      h  ;
	char          *level ;
	bool           fakeIt ;
loop:
	// advance "i", but if done break out
	if ( ++i >= numRecs ) goto done;

	crec = (char *)&clusterRecs[i];
	// . set this cluster level
	// . right now will be CR_ERROR_CLUSTERDB or CR_OK...
	level = &clusterLevels[i];

	// sanity check
	if ( *level == CR_UNINIT ) { char *xx = NULL; *xx = 0; }
	// and the adult bit, for cleaning the results
	if ( familyFilter && g_clusterdb.hasAdultContent ( crec ) ) {
		*level = CR_DIRTY; goto loop; }
	// and the lang filter
	if ( langFilter > 0 && g_clusterdb.getLanguage( crec )!= langFilter ) {
		*level = CR_BAD_LANG; goto loop; }
	// if error looking up in clusterdb, use a 8 bit domainhash from docid
	if ( *level == CR_ERROR_CLUSTERDB ) fakeIt = true;
	else                                fakeIt = false;
	// assume ok, show it, it is visible
	*level = CR_OK;
	// site hash comes next
	if ( ! doHostnameClustering ) goto loop;

	// . get the site hash
	// . these are only 32 bits!
	if ( fakeIt ) h = g_titledb.getDomHash8FromDocId(docIds[i]);
	else          h = g_clusterdb.getSiteHash26 ( crec );
	// inc this count!
	if ( fakeIt ) g_stats.m_filterStats[CR_ERROR_CLUSTERDB]++;
	// if it matches a siteid on our black list
	//if ( checkNegative && sht.getSlot((int64_t)h) > 0 ) {
	//	*level = CR_BLACKLISTED_SITE; goto loop; }
	// look it up
	score = ctab.getScore ( &h ) ;
	// if still visible, just continue
	if ( score < (uint32_t)maxDocIdsPerHostname ) {
		if ( ! ctab.addTerm(&h))
			return false;
		goto loop;
	}
	// otherwise, no lonegr visible
	*level = CR_CLUSTERED;
	// get another
	goto loop;

 done:

	// debug
	for ( int32_t i = 0 ; i < numRecs && isDebug ; i++ ) {
		crec = (char *)&clusterRecs[i];
		uint32_t siteHash26=g_clusterdb.getSiteHash26(crec);
		logf(LOG_DEBUG,"query: msg51: hit #%"INT32") sitehash26=%"UINT32" "
		     "rec.n0=%"XINT64" docid=%"INT64" cl=%"INT32" (%s)",
		     (int32_t)count++,
		     (int32_t)siteHash26,
		     clusterRecs[i].n0,
		     (int64_t)docIds[i],
		     (int32_t)clusterLevels[i],
		     g_crStrings[(int32_t)clusterLevels[i]] );
	}


	//log(LOG_DEBUG,"build: numVisible=%"INT32" numClustered=%"INT32" numErrors=%"INT32"",
	//    *numVisible,*numClustered,*numErrors);
	// show time
	uint64_t took = gettimeofdayInMilliseconds() - startTime;
	if ( took <= 3 ) return true;
	log(LOG_INFO,"build: Took %"INT64" ms to do clustering.",took);

	// we are all done
	return true;
}
