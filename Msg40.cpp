#ifndef _BUZZLOGIC_
#include "gb-include.h"
#endif

#include "Msg40.h"
#include "Stats.h"        // for timing and graphing time to get all summaries
#include "CollectionRec.h"
#include "Collectiondb.h"
//#include "TitleRec.h"      // containsAdultWords ()
#include "LanguageIdentifier.h"
#include "sort.h"
#include "matches.h"
#include "XmlDoc.h" // computeSimilarity()
//#include "Facebook.h" // msgfb
#include "Speller.h"
#include "Wiki.h"

// increasing this doesn't seem to improve performance any on a single
// node cluster....
#define MAX_OUTSTANDING_MSG20S 50

//static void handleRequest40              ( UdpSlot *slot , long netnice );
//static void gotExternalReplyWrapper      ( void *state , void *state2 ) ;
static void gotCacheReplyWrapper         ( void *state );
static void gotDocIdsWrapper             ( void *state );
static bool gotSummaryWrapper            ( void *state );
//static void didTaskWrapper               ( void *state );
//static void gotResults2                  ( void *state );

// here's the GIGABIT knobs:

// sample radius in chars around each query term    : 600  (line  212)
// max sample size, all excerpts, per document      : 100k (line  213)
// map from distance to query term in words to score:      (line  855)
// map from popularity to score weight              :      (lines 950 et al)
// the comments above are way out of date (aac, Jan 2008)
// 
// QPOP multiplier params
#define QPOP_ZONE_0          10
#define QPOP_ZONE_1          30
#define QPOP_ZONE_2          80
#define QPOP_ZONE_3          100
#define QPOP_ZONE_4          300
#define QPOP_MULT_0          10
#define QPOP_MULT_1          8
#define QPOP_MULT_2          6
#define QPOP_MULT_3          4
#define QPOP_MULT_4          2
// QTR scoring params
#define MAX_SCORE_MULTIPLIER 3000  // orig: 3000
#define ALT_MAX_SCORE        12000 // orig: 12000
#define ALT_START_SCORE      1000
#define QTR_ZONE_0           4
#define QTR_ZONE_1           8
#define QTR_ZONE_2           12
#define QTR_ZONE_3           20
#define QTR_BONUS_0          1000
#define QTR_BONUS_1          800
#define QTR_BONUS_2          500
#define QTR_BONUS_3          200
#define QTR_BONUS_CW         1
#define MULTIPLE_HIT_BOOST   1000 // orig: 1000
// gigabit phrase scoring params
//#define SPARSE_MARK          0.34
//#define SPARSE_PENALTY       1000
#define FWC_PENALTY          500   // penalty for begining with common word
#define POP_ZONE_0           10 // 0.00001
#define POP_ZONE_1           30 //0.0001
#define POP_ZONE_2           80 // 0.001
#define POP_ZONE_3           300 // 0.01
#define POP_BOOST_0          4.0
#define POP_BOOST_1          3.0
#define POP_BOOST_2          2.0
#define POP_BOOST_3          1.0
#define POP_BOOST_4          0.1



bool isSubDom(char *s , long len);

Msg40::Msg40() {
	m_buf           = NULL;
	m_buf2          = NULL;
	m_cachedResults = false;
	m_msg20         = NULL;
	m_numMsg20s     = 0;
	m_msg20StartBuf = NULL;
	m_numToFree     = 0;
	//m_numGigabitInfos = 0;
}

void Msg40::resetBuf2 ( ) {
	// remember num to free in reset() function
	char *p = m_msg20StartBuf;
	// msg20 destructors
	for ( long i = 0 ; i < m_numToFree ; i++ ) {
		// skip if empty
		//if ( ! m_msg20[i] ) continue;
		// call destructor
		//m_msg20[i]->destructor();
		// cast it
		Msg20 *m = (Msg20 *)p;
		// free its stuff
		m->destructor();
		// advance
		p += sizeof(Msg20);
	}
	// now free the msg20 ptrs and buffer space
	if ( m_buf2 ) mfree ( m_buf2 , m_bufMaxSize2 , "Msg40b" );
	m_buf2 = NULL;
}

Msg40::~Msg40() {
	if ( m_buf  ) mfree ( m_buf  , m_bufMaxSize  , "Msg40" );
	m_buf  = NULL;
	resetBuf2();
}

bool Msg40::registerHandler ( ) {
        // . register ourselves with the udp server
        // . it calls our callback when it receives a msg of type 0x40
	//if ( ! g_udpServer.registerHandler ( 0x40, handleRequest40 )) 
	//	return false;
        return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . uses Msg3a to get docIds
// . uses many msg20s to get title/summary/url/docLen for each docId
bool Msg40::getResults ( SearchInput *si      ,
			 bool         forward ,
			 void        *state   ,
			 void   (* callback) ( void *state ) ) {
	// warning
	if ( ! si->m_coll2 ) log(LOG_LOGIC,"net: NULL collection. msg40.");

	m_lastProcessedi = -1;

	m_si             = si;
	m_state          = state;
	m_callback       = callback;
	m_msg3aRecallCnt = 0;
	// we haven't allocated any Msg20s yet
	m_numMsg20s      = 0;
	// reset our error keeper
	m_errno = 0;
	// we need this info for caching as well
	//m_numGigabitInfos = 0;

 	m_postQueryRerank.set1( this, si );

	// get the collection rec
	CollectionRec *cr =g_collectiondb.getRec(m_si->m_coll2,
						 m_si->m_collLen2);
	// g_errno should be set if not found
	if ( ! cr ) { g_errno = ENOCOLLREC; return true; }
	// what is our max docids ceiling?
	//m_maxDocIdsToCompute = cr->m_maxDocIdsToCompute;
	// topic similarity cutoff
	m_topicSimilarCutoff = cr->m_topicSimilarCutoffDefault ;

	m_gigabitBuf.reset();
	m_factBuf.reset();

	// reset this for family filter
	m_queryCensored = false;
	m_filterStats[CR_DIRTY]	= 0;  //m_numCensored = 0;
	// . compute the min number of results to scan
	// . it is the 3rd number in a topicGroupPtr string
	// . the minimum number of docids to get for topic-clustering purposes
	//m_docsToScanForTopics = 30;
	//m_docsToScanForTopics = cr->m_docsToScanForTopics;
	m_docsToScanForTopics = 0;
	// we usually only have one TopicGroup but there can be multiple
	// ones. each TopicGroup can derive its gigabits/topics from a
	// different source, like the meta keywords tags only, for instance.
	// This support was originally put in for a client.
	for ( long i = 0 ; i < m_si->m_numTopicGroups ; i++ ) {
		long x = m_si->m_topicGroups[i].m_docsToScanForTopics ;
		if ( x > m_docsToScanForTopics ) m_docsToScanForTopics = x;
	}
	// . but only for first page!
	// . no! the second page of results may not match with the first
	//   if they have different result increments
	//if ( m_firstResultNum > 0 ) m_docsToScanForTopics = 0;

	// . reset these
	// . Next X Results links? yes or no?
	m_moreToCome = false;
	// set this to zero -- assume not in cache
	m_cachedTime = 0;
	// assume we are not taken from the serp cache
	m_cachedResults  = false;

	// bail now if 0 requested!
	if ( m_si->m_docsWanted == 0 ) return true;

	// . do this now in case results were cached.
	// . set SearchInput class instance, m_si
	// . has all the input that we need to get the search results just
	//   the way the caller wants them
	//m_msg1a.setSearchInput(m_si);

	// how many docids do we need to get?
	long get = m_si->m_docsWanted + m_si->m_firstResultNum ;
	// we get one extra for so we can set m_moreToFollow so we know
	// if more docids can be gotten (i.e. show a "Next 10" link)
	get++;
	// make sure we get more than requested for various other tasks, like
	// this one here is for gigabit generation. it likes to have 30 docids
	// typically to generate gigabits from.
	// NOTE: pqr needs gigabits for all pages
	if ( /*m_si->m_firstResultNum == 0 && */get < m_docsToScanForTopics ) 
		get = m_docsToScanForTopics;
	// for alden's reranking. often this is 50!
	if ( get < m_si->m_docsToScanForReranking  ) 
		get = m_si->m_docsToScanForReranking;
	// for zak's reference pages
        if ( get < m_si->m_refs_numToGenerate ) get=m_si->m_refs_numToGenerate;
	// limit to this ceiling though for peformance reasons
	//if ( get > m_maxDocIdsToCompute ) get = m_maxDocIdsToCompute;
	// ok, need some sane limit though to prevent malloc from 
	// trying to get 7800003 docids and going ENOMEM
	if ( get > MAXDOCIDSTOCOMPUTE ) get = MAXDOCIDSTOCOMPUTE;
	// this is how many visible results we need, after filtering/clustering
	m_docsToGetVisible = get;
	// if site clustering is on, get more than we should in anticipation 
	// that some docIds will be clustered.
	// MDW: we no longer do this here for full splits because Msg39 does
	// clustering on its end now!
	//if ( m_si->m_doSiteClustering ) get = (get*150LL)/100LL;
	//if ( m_si->m_doSiteClustering && ! g_conf.m_fullSplit ) 
	//	get = (get*150LL)/100LL;
	// ip clustering is not really used now i don't think (MDW)
	//if ( m_si->m_doIpClustering       ) get = (get*150LL)/100LL;
	// . get a little more since this usually doesn't remove many docIds
	// . deduping is now done in Msg40.cpp once the summaries are gotten
	if ( m_si->m_doDupContentRemoval ) get = (get*120LL)/100LL;
	// . get 30% more for what reason? i dunno, just cuz...
	// . well, for "missing query terms" filtering... errors (not founds)
	get = (get*130LL)/100LL;
	// get at least 50 since we need a good sample that explicitly has all 
	// query terms in order to calculate reliable affinities
	//if ( get < MIN_AFFINITY_SAMPLE ) get = MIN_AFFINITY_SAMPLE;
	// now apply the multiplier. before it was not getting applied and
	// we were constantly doing Msg3a recalls. you can set this multiplier
	// dynamically in the "search controls"
	// MDW: don't apply if in a full split though, i don't see why...
	// MDW: just ignore this now, ppl will just mis-set it and serisouly
	//      screw things up...
	//if ( cr->m_numDocsMultiplier > 1.0 && ! g_conf.m_fullSplit ) 
	//	get = (long) ((float)get * cr->m_numDocsMultiplier);
	// limit to this ceiling though for peformance reasons
	//if ( get > m_maxDocIdsToCompute ) get = m_maxDocIdsToCompute;
	// . ALWAYS get at least this many
	// . this allows Msg3a to allow higher scoring docids in tier #1 to
	//   outrank lower-scoring docids in tier #0, even if such docids have
	//   all the query terms explicitly. and we can guarantee consistency
	//   as long as we only allow for this outranking within the first
	//   MIN_DOCS_TO_GET docids.
	if ( get < MIN_DOCS_TO_GET ) get = MIN_DOCS_TO_GET;
	// this is how many docids to get total, assuming that some will be
	// filtered out for being dups, etc. and that we will have at least
	// m_docsToGetVisible leftover that are unfiltered and visible. so
	// we tell each msg39 split to get more docids than we actually want
	// in anticipation some will be filtered out in this class.
	m_docsToGet = get;

	// debug msg
	if ( m_si->m_debug ) 
		logf(LOG_DEBUG,"query: msg40 mapped %li wanted to %li to get",
		     m_docsToGetVisible,m_docsToGet );

	// let's try using msg 0xfd like Proxy.cpp uses to forward an http
	// request! then we just need specify the ip of the proxy and we
	// do not need hosts2.conf!
	if ( forward ) { char *xx=NULL;*xx=0; }

	// . forward to another *collection* and/or *cluster* if we should
	// . this is used by Msg41 for importing results from another cluster
	/*
	if ( forward ) {
		// serialize input
		long  requestSize;
		// CAUTION: m_docsToGet can be different on remote host!!!
		char *request = m_si->serializeForMsg40 ( &requestSize );
		if ( ! request ) return true;
		// . set timeout based on docids requested!
		// . the more docs requested the longer it will take to get
		// . use 50ms per docid requested
		long timeout = (50 * m_docsToGet) / 1000;
		// always wait at least 20 seconds
		if ( timeout < 20 ) timeout = 20;
		// . forward to another cluster
		// . use the advanced composite query to make the key
		unsigned long h = hash32 ( m_si->m_qbuf1 );
		// get groupId from docId, if positive
		long          groupNum = h % g_hostdb2.m_numGroups;
		unsigned long groupId  = g_hostdb2.getGroupId ( groupNum );
		if ( ! m_mcast.send ( request         , 
				      requestSize     , 
				      0x40            , // msgType 0x40
				      false           , // mcast own m_request?
				      groupId         , //sendtogroup(groupKey)
				      false           , // send to whole group?
				      h               , // key for host in grp
				      this            , // state data
				      NULL            , // state data
				      gotExternalReplyWrapper ,
				      timeout         , // to re-send to twin
				      m_si->m_niceness, // niceness        ,
				      false           , // real time udp?
				      -1              , // first hostid
				      NULL            , // m_reply         ,
				      0               , // m_replyMaxSize  ,
				      false           , // free reply buf?
				      false           , // disk load balancing?
				      -1              , // max cache age
				      0               , // cacheKey
				      0               , // bogus rdbId
				      -1              , // minRecSizes(-1=ukwn)
				      true            , // sendToSelf
				      false           , // retry forever
				      &g_hostdb2      )) {
			m_mcast.reset();
			return true;
		}
		// always blocks
		return false; // gotExternalReply();
	}
	*/

	// time the cache lookup
	if ( g_conf.m_logTimingQuery || m_si->m_debug ) 
		m_startTime = gettimeofdayInMilliseconds();

	// use cache?
	bool useCache = m_si->m_rcache;

	// turn it off for now until we cache the scoring tables
	log("db: cache is disabled until we cache scoring tables");
	useCache = false;

	// . try setting from cache first
	// . cacher --> "do we READ from cache?"
	if ( useCache ) {
		// make the key based on query and other input parms in msg40
		key_t key = m_si->makeKey ( );
		// this should point to the cached rec, if any
		m_cachePtr = NULL;
		m_cacheSize = 0;
		// this returns false if blocked, true otherwise
		if ( ! m_msg17.getFromCache ( SEARCHRESULTS_CACHEID,
					      key ,
					      &m_cachePtr,
					      &m_cacheSize,
					      m_si->m_coll2,
					      this , 
					      gotCacheReplyWrapper ,
					      m_si->m_niceness ,
					      1 ) )
			return false;
		// reset g_errno, we're just a cache
		g_errno = 0;
		return gotCacheReply();
	}

	// keep going
	return prepareToGetDocIds ( );
}

/*
void gotExternalReplyWrapper ( void *state , void *state2 ) {
	Msg40 *THIS = (Msg40 *)state;
	if ( ! THIS->gotExternalReply() ) return;
	THIS->m_callback ( THIS->m_state );
}

bool Msg40::gotExternalReply ( ) {
	if ( g_errno ) {
		log("query: Trying to forward to another cluster "
		    "had error: %s.",mstrerror(g_errno));
		return true;
	}
	// grab the reply from the multicast class
	bool freeit;
	long bufSize , bufMaxSize;
	char *buf = m_mcast.getBestReply ( &bufSize , &bufMaxSize , &freeit );
	relabel( buf, bufMaxSize, "Msg40-mcastGBR" );
	// sanity check
	if ( freeit ) {
		log(LOG_LOGIC,"query: msg40: gotReply: Bad engineer.");
		char *xx = NULL; *xx = 0;
	}
	if ( bufSize != bufMaxSize ) {
		log(LOG_LOGIC,"query: msg40: fix me.");
		char *xx = NULL; *xx = 0;
	}
	// set ourselves from it
	deserialize ( buf , bufSize );
	return true;
}
*/
		
// msg17 calls this after it gets a reply
void gotCacheReplyWrapper ( void *state ) {
	Msg40 *THIS = (Msg40 *)state;
	// reset g_errno, we're just a cache
	g_errno = 0;
	// handle the reply
	if ( ! THIS->gotCacheReply() ) return;
	// otherwise, call callback
	THIS->m_callback ( THIS->m_state );
}

bool Msg40::gotCacheReply ( ) {
	// if not found, get the result the hard way
	if ( ! m_msg17.wasFound() ) return prepareToGetDocIds ( );
	// otherwise, get the deserialized stuff
	long nb = deserialize(m_cachePtr, m_cacheSize);
	if ( nb <= 0 ) {
		log ("query: Deserialization of cached search results "
		     "page failed." );
		// free m_buf!
		if ( m_buf ) 
			mfree ( m_buf , m_bufMaxSize , "deserializeMsg40");
		// get results the hard way!
		return prepareToGetDocIds ( );
	}
	// log the time it took for cache lookup
	if ( g_conf.m_logTimingQuery ) {
		long long now  = gettimeofdayInMilliseconds();
		long long took = now - m_startTime;
		log(LOG_TIMING,
		    "query: [%lu] found in cache. "
		    "lookup took %lli ms.",(long)this,took);
	}
	m_cachedTime = m_msg17.getCachedTime();
	m_cachedResults = true;
	// if it was found, we return true, m_cachedTime should be set
	return true;
}

bool Msg40::prepareToGetDocIds ( ) {

	// log the time it took for cache lookup
	if ( g_conf.m_logTimingQuery || m_si->m_debug ) {
		long long now  = gettimeofdayInMilliseconds();
		long long took = now - m_startTime;
		logf(LOG_TIMING,"query: [%lu] Not found in cache. "
		     "Lookup took %lli ms.",(long)this,took);
		m_startTime = now;
		logf(LOG_TIMING,"query: msg40: [%lu] Getting up to %li "
		     "(docToGet=%li) docids", (long)this,
		     m_docsToGetVisible,  m_docsToGet);
	}

	//if ( m_si->m_compoundListMaxSize <= 0 )
	//	log("query: Compound list max size is %li. That is bad. You "
	//	    "will not get back some search results for UOR queries.",
	//	    m_si->m_compoundListMaxSize );

	// . if query has dirty words and family filter is on, set
	//   number of results to 0, and set the m_queryClen flag to true
	// . m_qbuf1 should be the advanced/composite query
	if ( m_si->m_familyFilter && 
	     getDirtyPoints ( m_si->m_sbuf1.getBufStart() , 
			      m_si->m_sbuf1.length() , 
			      0 ) ) {
		// make sure the m_numDocIds gets set to 0
		m_msg3a.reset();
		m_queryCensored = true;
		return true;
	}

	return getDocIds( false );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg40::getDocIds ( bool recall ) {

	// we modified m_rcache above to be true if we should read from cache
	long maxAge = 0 ;
	if ( m_si->m_rcache ) maxAge = g_conf.m_indexdbMaxIndexListAge;

	// reset it
	m_r.reset();

	m_r.ptr_coll                    = m_si->m_coll2;
	m_r.size_coll                   = m_si->m_collLen2+1;
	m_r.m_maxAge                    = maxAge;
	m_r.m_addToCache                = m_si->m_wcache;
	m_r.m_docsToGet                 = m_docsToGet;
	m_r.m_niceness                  = m_si->m_niceness;
	m_r.m_debug                     = m_si->m_debug          ;
	m_r.m_getDocIdScoringInfo       = m_si->m_getDocIdScoringInfo;
	m_r.m_doSiteClustering          = m_si->m_doSiteClustering    ;
	m_r.m_useMinAlgo                = m_si->m_useMinAlgo;
	m_r.m_useNewAlgo                = m_si->m_useNewAlgo;
	m_r.m_doMaxScoreAlgo            = m_si->m_doMaxScoreAlgo;
	m_r.m_fastIntersection          = m_si->m_fastIntersection;
	m_r.m_doIpClustering            = m_si->m_doIpClustering      ;
	m_r.m_doDupContentRemoval       = m_si->m_doDupContentRemoval ;
	//m_r.m_restrictIndexdbForQuery   = m_si->m_restrictIndexdbForQuery ;
	m_r.m_queryExpansion            = m_si->m_queryExpansion; 
	m_r.m_compoundListMaxSize       = m_si->m_compoundListMaxSize ;
	m_r.m_boolFlag                  = m_si->m_boolFlag            ;
	m_r.m_familyFilter              = m_si->m_familyFilter        ;
	m_r.m_language                  = (unsigned char)m_si->m_queryLang;
	m_r.ptr_query                   = m_si->m_q->m_orig;
	m_r.size_query                  = m_si->m_q->m_origLen+1;
	m_r.m_timeout                   = -1; // auto-determine based on #terms
	// make sure query term counts match in msg39
	m_r.m_maxQueryTerms             = m_si->m_maxQueryTerms; 
	m_r.m_realMaxTop                = m_si->m_realMaxTop;

	// . get the docIds
	// . this sets m_msg3a.m_clusterLevels[] for us
	if ( ! m_msg3a.getDocIds ( &m_r,  m_si->m_q, this , gotDocIdsWrapper))
		return false;
	// call again w/o parameters now
	return gotDocIds ( );
}	

// . uses parameters assigned to local member vars above
// . returns false if blocked, true otherwise
// . sets g_errno on error
void gotDocIdsWrapper ( void *state ) {
	Msg40 *THIS = (Msg40 *) state;
	// if this blocked, it returns false
	//if ( ! checkTurnOffRAT ( state ) ) return;
	// return if this blocked
	if ( ! THIS->gotDocIds() ) return;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
}

// . return false if blocked, true otherwise
// . sets g_errno on error
bool Msg40::gotDocIds ( ) {
	// log the time it took for cache lookup
	long long now  = gettimeofdayInMilliseconds();

	if ( g_conf.m_logTimingQuery || m_si->m_debug||g_conf.m_logDebugQuery){
		long long took = now - m_startTime;
		logf(LOG_DEBUG,"query: msg40: [%lu] Got %li docids in %lli ms",
		     (long)this,m_msg3a.getNumDocIds(),took);
		logf(LOG_DEBUG,"query: msg40: [%lu] Getting up to %li "
		     "summaries", (long)this,m_docsToGetVisible);
	}

	// save any covered up error
	if ( ! m_errno && m_msg3a.m_errno ) m_errno = m_msg3a.m_errno;
	//sanity check.  we might not have allocated due to out of memory
	if ( g_errno ) { m_errno = g_errno; return true; }
	
	// . ok, do the actual clustering
	// . sets m_clusterLevels[]
	// . sets m_clusterLevels[i] to CR_OK if docid #i is not clustered
	// . sets m_clusterLevels[i] to CR_OK if not doing any clustering or
	//   filtering, and then returns right away
	// . allow up to "2" docids per hostname if site clustering is on
	// . returns false and sets g_errno on error
	// . this should get rid of cluster levels of CR_GOT_REC
	// . if we had g_conf.m_fullSplit then Msg3a should have these
	//   already set...
	/*
	if ( ! g_conf.m_fullSplit && 
	     m_si->m_doSiteClustering &&
	     ! setClusterLevels ( m_msg3a.m_clusterRecs    ,
				  m_msg3a.m_docIds         ,
				  m_msg3a.m_numDocIds      ,
				  2                        ,
				  m_si->m_doSiteClustering ,
				  m_si->m_familyFilter     ,
				  m_si->m_language         ,
				  // list of blacklisted site ids
				  NULL                     ,
				  m_si->m_debug            ,
				  m_msg3a.m_clusterLevels  )) {
		m_errno = g_errno;
		return true;
	}
	*/

	// DEBUG HACK -- make most clustered!
	//for ( long i = 1 ; i < m_msg3a.m_numDocIds ; i++ )
	//	m_msg3a.m_clusterLevels[i] = CR_CLUSTERED;

	// time this
	m_startTime = gettimeofdayInMilliseconds();

	// we haven't got any Msg20 responses as of yet or sent any requests
	m_numRequests  =  0;
	m_numReplies   =  0;
	//m_maxiLaunched = -1;

	// we have received m_numGood contiguous Msg20 replies!
	//m_numContiguous     = 0;
	//m_visibleContiguous = 0;

	// . do not uncluster more than 5 docids! it slows things down.
	// . kind of a HACK until we do it right
	m_unclusterCount = 5;
	
	// assume we do not have enough visible docids
	//m_gotEnough = false;

	if ( ! m_urlTable.set ( m_msg3a.m_numDocIds * 2 ) ) {
		m_errno = g_errno;
		log("query: Failed to allocate memory for url deduping. "
		    "Not deduping search results.");
		return true;
	}

	// if only getting docids, skip summaries,topics, and references
	//	if ( m_si->m_docIdsOnly ) return launchMsg20s ( false );
	if ( m_si->m_docIdsOnly ) return true;

	// . alloc buf to hold all m_msg20[i] ptrs and the Msg20s they point to
	// . returns false and sets g_errno/m_errno on error
	// . salvage any Msg20s that we can if we are being re-called
	if ( ! reallocMsg20Buf() ) return true;

	// these are just like for passing to Msg39 above
	//long maxAge = 0;
	//if ( m_si->m_rcache ) maxAge = g_conf.m_titledbMaxCacheAge;

	// . launch a bunch of task that depend on the docids we got
	// . gigabits, reference pages and dmoz topics
	// . keep track of how many are out
	m_tasksRemaining = 0;

	// debug msg
	if ( m_si->m_debug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: [%lu] Getting topics/gigabits, "
		     "reference pages and dir pages.",(unsigned long)this);

	// . do not bother getting topics if we are passed first page
	// . AWL NOTE: pqr needs topics on all pages
	//if ( m_si->m_firstResultNum > 0 ) return launchMsg20s ( false );

	// do not bother getting topics if we will be re-called below so we
	// will be here again!
	//if ( numVisible       < m_docsToGet && // are we short?
	//     m_msg3a.m_tier+1 < MAX_TIERS   && // do we have a tier to go to?
	//     ! m_msg3a.m_isDiskExhausted      )// SOME more data on disk?
	//	return launchMsg20s ( false );

	// . TODO: do this LAST, after we get all summaries and do PQR
	// . TODO: give it all our Msg20s so it can just use those!!!
	// . get the topics
	// . returns true right away if m_docsToScanForTopics is <= 0
// 	if (!m_msg24.generateTopics ( m_si->m_coll                ,
// 				      m_si->m_collLen             ,
// 				      m_msg3a.m_q->m_orig         ,// query
// 				      m_msg3a.m_q->m_origLen      ,// query
// 				      m_msg3a.m_docIds            ,
// 				      m_msg3a.m_clusterLevels     ,
// 				      m_msg3a.m_numDocIds         ,
// 				      m_si->m_topicGroups         ,
// 				      m_si->m_numTopicGroups      ,
// 				      maxAge                      ,
// 				      m_si->m_wcache              ,//addToCache
// 				      m_si->m_returnDocIdCount    ,
// 				      m_si->m_returnDocIds        ,
// 				      m_si->m_returnPops          ,
// 				      this                        ,
// 				      didTaskWrapper              ,
// 				      m_si->m_niceness            ))
// 		m_tasksRemaining++;

	//generate reference and related pages.
// 	if ( ! m_msg1a.generateReferences(m_si,(void*)this,didTaskWrapper) )
// 		m_tasksRemaining++;

	return launchMsg20s ( false );
}

// . returns false and sets g_errno/m_errno on error
// . makes m_msg3a.m_numDocIds ptrs to Msg20s. 
// . does not allocate a Msg20 in the buffer if the m_msg3a.m_clusterLevels[i]
//   is something other than CR_OK
bool Msg40::reallocMsg20Buf ( ) {

	// if the user only requested docids, we have no summaries
	if ( m_si->m_docIdsOnly ) return true;

	// . allocate m_buf2 to hold all our Msg20 pointers and Msg20 classes
	// . how much mem do we need?
	// . need space for the msg20 ptrs
	long need = m_msg3a.m_numDocIds * sizeof(Msg20 *);
	// need space for the classes themselves, only if "visible" though
	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) 
		if ( m_msg3a.m_clusterLevels[i] == CR_OK ) 
			need += sizeof(Msg20);

	// MDW: try to preserve the old Msg20s if we are being re-called
	if ( m_buf2 ) {
		// use these 3 vars for mismatch stat reporting
		//long      mismatches = 0;
		//long long mismatch1  = 0LL;
		//long long mismatch2  = 0LL;
		// make new buf
		char *newBuf = (char *)mmalloc(need,"Msg40d");
		// return false if it fails
		if ( ! newBuf ) { m_errno = g_errno; return false; }
		// fill it up
		char *p = newBuf;
		// point to our new array of Msg20 ptrs
		Msg20 **tmp = (Msg20 **)p;
		// skip over pointer array
		p += m_msg3a.m_numDocIds * sizeof(Msg20 *);
		// record start to set to m_msg20StartBuf
		char *pstart = p;
		// and count for m_numToFree
		long pcount = 0;
		// fill in the actual Msg20s from the old buffer
		for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
			// assume empty, because clustered, filtered, etc.
			tmp[i] = NULL;
			// if clustered, keep it as a NULL ptr
			if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
			// point it to its memory
			tmp[i] = (Msg20 *)p;
			// point to the next Msg20
			p += sizeof(Msg20);
			// init it
			tmp[i]->constructor();
			// count it
			pcount++;
			// skip it if it is a new docid, we do not have a Msg20
			// for it from the previous tier. IF it is from
			// the current tier, THEN it is new.
			//if ( m_msg3a.m_tiers[i] == m_msg3a.m_tier ) continue;
			// see if we can find this docid from the old list!
			long k = 0;
			for ( ; k < m_numMsg20s ; k++ ) {
				// skip if NULL
				if ( ! m_msg20[k] ) continue;
				// if it never gave us a reply then skip it
				if ( ! m_msg20[k]->m_gotReply ) continue;
				//or if it had an error
				if ( m_msg20[k]->m_errno ) continue;
				// skip if no match
				if ( m_msg3a    .m_docIds[i] !=
				     m_msg20[k]->m_r->m_docId )//getDocId() )
					continue;
				// we got a match, grab its Msg20
				break;
			}
			// . skip if we could not match it... strange...
			// . no, because it may have been in the prev tier,
			//   from a split, but it was not in msg3a's final 
			//   merged list made in Msg3a::mergeLists(), but now 
			//   it is in there, with the previous tier, because
			//   we asked for more docids from msg3a.
			// . NO! why did we go to the next tier unnecessarily
			//   THEN? no again, because we did a msg3a recall
			//   and asked for more docids which required us
			//   going to the next tier, even though some (but
			//   not enough) docids remained in the previous tier.
			if ( k >= m_numMsg20s ) {
				/*
				logf(LOG_DEBUG,"query: msg40: could not match "
				     "docid %lli (max=%li) "
				     "to msg20. newBitScore=0x%hhx q=%s",
				     m_msg3a.m_docIds[i],
				     (char)m_msg3a.m_bitScores[i],
				     m_msg3a.m_q->m_orig);
				*/
				continue;
			}
			// it is from an older tier but never got the msg20 
			// for it? what happened? it got unclustered??
			if ( ! m_msg20[k] ) continue;
			/*
			// . make sure they match!
			// . they may get mismatched after the recall because
			//   a new doc gets added to the index!!!
			// . also, if the re-call gets the termlist from a
			//   different twin and gets back different docids
			//   or scores, it will change this too!!
			if ( tmp[i]->m_docId >= 0 &&
			     tmp[i]->m_docId != m_msg3a.m_docIds[i] ) {
				// it should be rare!!!
				mismatches++;
				if ( ! mismatch1 ) 
					mismatch1 = tmp[i]->m_docId;
				if ( ! mismatch2 ) 
					mismatch2 = m_msg3a.m_docIds[i];
				continue;
				//logf(LOG_DEBUG,"query: msg40: docid mismatch"
				//   " at #%li. olddocid=%lli newdocid=%lli",i,
				//   tmp[i]->m_docId,m_msg3a.m_docIds[i]);
				// core for testing on gb1d only!!!
				//char *xx = NULL; *xx = 0; 
			}
			*/
			// . otherwise copy the memory if available
			// . if m_msg20[i]->m_docId is set this will save us
			//   repeating a summary lookup
			tmp[i]->copyFrom ( m_msg20[k] );
		}
		// sanity check
		if ( p - (char *)tmp != need ) { char *xx = NULL; *xx = 0; }

		resetBuf2();
		// destroy all the old msg20s, this was mem leaking
		//for ( long i = 0 ; i < m_numMsg20s ; i++ ) {
		//	// assume empty, because clustered, filtered, etc.
		//	if ( ! m_msg20[i] ) continue;
		//	// call its destructor
		//	m_msg20[i]->destructor();
		//}

		// the new buf2 stuff
		m_numToFree     = pcount;
		m_msg20StartBuf = pstart;

		// re-assign the msg20 ptr to the ptrs
		m_msg20 = tmp;
		// update new count
		m_numMsg20s = m_msg3a.m_numDocIds;
		// free old buf
		//mfree ( m_buf2 , m_bufMaxSize2 , "Msg40c");
		// assign to new mem
		m_buf2        = newBuf;
		m_bufMaxSize2 = need;
		// note it since this is inefficient for now
		// . crap this is messing up m_nextMerged ptr!!
		//log("query: msg40: rellocated msg20 buffer");
		// show mismatch stats
		//if ( mismatches )
		//	logf(LOG_DEBUG,"query: msg40: docid %lli mismatched "
		//	     "%lli. Total of %li mismathes. q=%s",
		//	     mismatch1,mismatch2,mismatches,
		//	     m_msg3a.m_q->m_orig );
		// all done
		return true;
	}

	// do the alloc
	m_buf2        = NULL;
	m_bufMaxSize2 = need;
	if ( need ) m_buf2 = (char *)mmalloc ( need ,"Msg40msg20");
	if ( need && ! m_buf2 ) { m_errno = g_errno; return false; }
	// point to the mem
	char *p = m_buf2;
	// point to the array, then make p point to the Msg20 buffer space
	m_msg20 = (Msg20 **)p; p += m_msg3a.m_numDocIds * sizeof(Msg20 *);
	// start free here
	m_msg20StartBuf = p;
	// set the m_msg20[] array to use this memory, m_buf20
	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// assume empty
		m_msg20[i] = NULL;
		// if clustered, do a NULL ptr
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// point it to its memory
		m_msg20[i] = (Msg20 *)p;
		// call its constructor
		m_msg20[i]->constructor();
		// point to the next Msg20
		p += sizeof(Msg20);
		// remember num to free in reset() function
		m_numToFree++;
	}
	// remember how many we got in here in case we have to realloc above
	m_numMsg20s = m_msg3a.m_numDocIds;

	return true;
}

/*
void didTaskWrapper ( void* state ) {
	Msg40 *THIS = (Msg40 *) state;
	// one less task
	THIS->m_tasksRemaining--;
	// this returns false if blocked
	if ( ! THIS->launchMsg20s ( false ) ) return;
	// we are done, call the callback
	THIS->m_callback ( THIS->m_state );
}
*/

bool Msg40::launchMsg20s ( bool recalled ) {

	// these are just like for passing to Msg39 above
	long maxAge = 0 ;
	//if ( m_si->m_rcache ) maxAge = g_conf.m_titledbMaxCacheAge;
	// may it somewhat jive with the search results caching, otherwise
	// it will tell me a search result was indexed like 3 days ago
	// when it was just indexed 10 minutes ago because the 
	// titledbMaxCacheAge was set way too high
	if ( m_si->m_rcache ) maxAge = g_conf.m_searchResultsMaxCacheAge;

	/*
	// "need" = how many more msg20 replies do we need to get back to
	// get the required number of search results?
	long sample        = 0;
	long good          = 0;
	long gaps          = 0;
	long goodAfterGaps = 0;
	// loop up to the last msg20 request we actually launched
	for ( long i = 0 ; i <= m_maxiLaunched ; i++ ) {
		// if Msg51 had initially clustered (CR_CLUSTERED) this away 
		// we never actually gave it a msg20 ptr, so it is NULL. it
		// m_msg3a.m_clusterLevel[i] != CR_OK ever.
		if ( ! m_msg20[i] ) continue;
		// do not count if reply not received yet. it is a gap.
		if ( ! m_msg20[i]->m_gotReply ) { gaps++; continue; }
		// ok, we had launched it and got a reply for it, it is 
		// therefore in our "sample", used to make the visibility ratio
		sample++;
		// . skip if not "good" (visible)
		// . if msg20 has error, sets cluster level to CR_ERROR_SUMMARY
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// count as good. it is visible.
		if ( gaps ) goodAfterGaps++;
		else        good++;
	}
	// how many MORE docs to we need to get? subtract what was desired from
	// what we already have that is visible as long as it is before any gap
	long need = m_docsToGetVisible - good ;
	// if we fill in the gaps, we get "goodAfterGaps" more visible results
	if ( need >= gaps ) {
		// so no need to get these then
		need -= goodAfterGaps ;
		// but watch out for flooding!
		if ( need < gaps ) need = gaps;
	}
	// how many total good?
	long allGood = good + goodAfterGaps;
	// get the visiblity ratio from the replies we did get back
	float ratio ;
	if ( allGood > 0 ) ratio = (float)sample / (float)allGood;
	else               ratio = (float)sample / 1.0        ;
	// give a 5% boost
	ratio *= 1.05;
	// assume some of what we "need" will be invisible, make up for that
	if ( sample > 0 ) need = (long)((float)need * ratio);
	// . restrict "need" to no more than 50 at a time
	// . we are using it for a "max outstanding" msg20s
	// . do not overflow the udpservers
	if ( need > 50 ) need = 50;

	if ( m_si->m_debug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: msg40: can launch %li more msg20s. "
		     "%li out. %li completed. %li visible. %li gaps. "
		     "%li contiguous. %li toGet. ",
		     need,m_numRequests-m_numReplies,sample,allGood,gaps,
		     m_numContiguous,m_docsToGet);
	*/

	long bigSampleRadius = 0;
	long bigSampleMaxLen = 0;
	// NOTE: pqr needs gigabits for all pages
	if(m_docsToScanForTopics > 0 /*&& m_si->m_firstResultNum == 0*/) {
		bigSampleRadius = 300;
		//bigSampleMaxLen = m_si->m_topicGroups[0].m_topicSampleSize;
		bigSampleMaxLen = 2000;
	}
	// . launch a msg20 getSummary() for each docid
	// . m_numContiguous should preceed any gap, see below
	for ( long i = m_lastProcessedi+1 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// if the user only requested docids, do not get the summaries
		if ( m_si->m_docIdsOnly ) break;
		// if we have enough visible then no need to launch more!
		//if ( m_gotEnough ) break;
		// limit it to this dynamic limit so we can check to 
		// see if we got enough visible each time we get one back!
		// this prevents us from having to do like 50 msg20 lookups
		// at a time.
		//if ( m_numRequests-m_numReplies >= need ) break;
		// hard limit
		if ( m_numRequests-m_numReplies >=MAX_OUTSTANDING_MSG20S)break;
		// do not double count!
		//if ( i <= m_lastProcessedi ) continue;
		// do not repeat for this i
		m_lastProcessedi = i;
		// start up a Msg20 to get the summary
		Msg20 *m = m_msg20[i];
		// if msg20 ptr null that means the cluster level is not CR_OK
		if ( ! m ) {
			m_numRequests++;
			m_numReplies++;
			continue;
		}
		// . did we already TRY to get the summary for this docid?
		// . we might be re-called from the refilter: below
		// . if already did it, skip it
		// . Msg20::getSummary() sets m_docId, first thing
		if ( m_msg3a.m_docIds[i] == m->getRequestDocId() ) {
			m_numRequests++;
			m_numReplies++;
			continue;
		}
		// get the LinkInfo class to set from it
		//LinkInfo* li = NULL;
		//if ( m_si->m_refs_numToGenerate != 0 &&
		//     m_si->m_refs_docsToScan    >  0    ) 
		//	li = m_msg1a.getLinkInfo(i);
		// ENGINEER NOTE -- use a single call to msg20 and use flags..
		//if ( m_si->m_rp_useResultsAsReferences ) {
		//	if (!m_tmpMsg20[n].
		//	    getSummary(m_msg3a.getQuery(),
		// assume no error
		g_errno = 0;
		// debug msg
		if ( m_si->m_debug || g_conf.m_logDebugQuery )
			logf(LOG_DEBUG,"query: msg40: [%lu] Getting "
			     "summary #%li for docId=%lli",
			     (unsigned long)this,i,m_msg3a.m_docIds[i]);
		// launch it
		m_numRequests++;
		// keep for-loops shorter with this
		//if ( i > m_maxiLaunched ) m_maxiLaunched = i;
		
		// get the collection rec
		CollectionRec *cr =g_collectiondb.
			getRec(m_si->m_coll2,m_si->m_collLen2);

		// set the summary request then get it!
		Msg20Request req;
		Query *q = m_si->m_q;
		req.ptr_qbuf             = q->getQuery();
		req.size_qbuf            = q->getQueryLen()+1;
		req.m_langId             = m_si->m_queryLang;

		// set highlight query
		if ( m_si->m_highlightQuery &&
		     m_si->m_highlightQuery[0] ) {
			req.ptr_hqbuf = m_si->m_highlightQuery;
			req.size_hqbuf = gbstrlen(req.ptr_hqbuf)+1;
		}

		long q3size = m_si->m_sbuf3.length()+1;
		if ( q3size == 1 ) q3size = 0;
		//req.ptr_q2buf             = m_si->m_sbuf3.getBufStart();
		//req.size_q2buf            = q3size;
		
		req.m_isAdmin             = m_si->m_isAdmin;

		//req.m_rulesetFilter      = m_si->m_ruleset;

		req.m_getTitleRec         = m_si->m_getTitleRec;

		//req.m_isSuperTurk       = m_si->m_isSuperTurk;


		req.m_highlightQueryTerms = m_si->m_doQueryHighlighting;
		req.m_highlightDates      = m_si->m_doDateHighlighting;

		req.ptr_coll             = m_si->m_coll2;
		req.size_coll            = m_si->m_collLen2+1;
		req.m_isDebug            = (bool)m_si->m_debug;
		if ( m_si->m_displayMetasLen > 0 ) {
			req.ptr_displayMetas     = m_si->m_displayMetas;
			req.size_displayMetas    = m_si->m_displayMetasLen+1;
		}
		req.m_docId              = m_msg3a.m_docIds[i];
		req.m_numSummaryLines    = m_si->m_numLinesInSummary;
		req.m_maxCacheAge        = maxAge;
		req.m_wcache             = m_si->m_wcache; // addToCache
		req.m_state              = this;
		req.m_callback           = gotSummaryWrapper;
		req.m_niceness           = m_si->m_niceness;
		req.m_summaryMode        = m_si->m_summaryMode;
		// need to see if it is banned, etc.
		//req.m_checkSitedb        = 1;
		// 0 means not, 1 means is (should never be 2 at this point)
		req.m_boolFlag           = m_si->m_boolFlag;
		req.m_allowPunctInPhrase = m_si->m_allowPunctInPhrase;
		req.m_showBanned         = m_si->m_showBanned;
		//req.m_excludeLinkText    = m_si->m_excludeLinkText ;
		//req.m_excludeMetaText    = m_si->m_excludeMetaText ;
		req.m_includeCachedCopy  = m_si->m_includeCachedCopy;//bigsmpl
		req.m_considerTitlesFromBody = m_si->m_considerTitlesFromBody;
		if ( cr->m_considerTitlesFromBody )
			req.m_considerTitlesFromBody = true;
		req.m_expected           = true;
		req.m_getSummaryVector   = true;
		req.m_bigSampleRadius    = bigSampleRadius;
		req.m_bigSampleMaxLen    = bigSampleMaxLen;
		req.m_titleMaxLen        = 256;
		req.m_titleMaxLen = cr->m_titleMaxLen;
		if(m_si->m_isAdmin && m_si->m_xml == 0) 
			req.m_getGigabitVector   = true;
		else    req.m_getGigabitVector   = false;
		req.m_flags              = 0;
		if ( m_postQueryRerank.isEnabled() ) {
			req.m_flags |= REQ20FLAG1_PQRENABLED;
			if (m_si->m_pqr_demFactLocSummary > 0)
				req.m_flags |= REQ20FLAG1_PQRLOCENABLED;
		}
		if ( m_si->m_pqr_demFactCommonInlinks > 0.0 )
			//req.m_getInlinks = true;
			req.m_getLinkInfo = true;
		// . buzz likes to do the &inlinks=1 parm to get inlinks
		// . use "&inlinks=1" for realtime inlink info, use 
		//   "&inlinks=2" to just get it from the title rec, which is 
		//   more stale, but does not take extra time or resources
		// . we "default" to the realtime stuff... i.e. since buzz
		//   is already using "&inlinks=1"
		if ( m_si->m_displayInlinks == 1 ) 
			req.m_computeLinkInfo = true;
		if ( m_si->m_displayInlinks == 2 ) 
			//req.m_getInlinks    = true;
			req.m_getLinkInfo     = true;
		if ( m_si->m_displayInlinks == 3 ) 
			req.m_computeLinkInfo = true;
		if ( m_si->m_displayInlinks == 4 ) 
			req.m_computeLinkInfo = true;
		if ( m_si->m_displayOutlinks )
			req.m_getOutlinks     = true;

		// buzz still wants the SitePop, computed fresh from Msg25,
		// even if they do not say "&inlinks=4" ... but they do
		// seem to specify getsitepops, so use that too
		if ( m_si->m_getSitePops )
			req.m_computeLinkInfo = true;

		if (m_si->m_queryMatchOffsets)
			req.m_getMatches = true;

		if ( ! m->getSummary ( &req ) ) continue;
		// got reply
		m_numReplies++;
		// . otherwise we got summary without blocking
		// . deal with an error
		if ( ! g_errno ) continue;
		// log it
		log("query: Had error getting summary: %s.",
		    mstrerror(g_errno));
		// record g_errno
		if ( ! m_errno ) m_errno = g_errno;
		// reset g_errno
		g_errno   = 0;
	}
	// return false if still waiting on replies
	if ( m_numReplies < m_numRequests ) return false;
	// do not re-call gotSummary() to avoid a possible recursive stack
	// explosion. this is only true if we are being called from 
	// gotSummary() already, so do not call it again!!
	if ( recalled ) return true;
	// if we got nothing, that's it
	if ( m_msg3a.m_numDocIds <= 0 ) return true;
	// . i guess crash here for now
	// . seems like we can call reallocMsg20Buf() and the first 50
	//   can already be set, so we drop down to here... so don't core
	logf(LOG_DEBUG,"query: Had all msg20s already.");
	// . otherwise, we got everyone, so go right to the merge routine
	// . returns false if not all replies have been received 
	// . returns true if done
	// . sets g_errno on error
	return gotSummary ( );
}

bool gotSummaryWrapper ( void *state ) {
	Msg40 *THIS  = (Msg40 *)state;
	// inc it here
	THIS->m_numReplies++;
	// it returns false if we're still awaiting replies
	if ( ! THIS->gotSummary ( ) ) return false;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
	return true;
}

// . returns false if not all replies have been received (or timed/erroredout)
// . returns true if done (or an error finished us)
// . sets g_errno on error
bool Msg40::gotSummary ( ) {
	// now m_linkInfo[i] (for some i, i dunno which) is filled
	if ( m_si->m_debug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: msg40: [%lu] Got summary. "
		     "Total got=#%li.",
		     (unsigned long)this,m_numReplies);

	// come back up here if we have to get more docids from Msg3a and
	// it gives us more right away without blocking, then we need to
	// re-filter them!
	// refilter:

	// did we have a problem getting this summary?
	if ( g_errno ) {
		// save it
		m_errno = g_errno;
		// log it
		if ( g_errno != EMISSINGQUERYTERMS )
			log("query: msg40: Got error getting summary: %s.",
			    mstrerror(g_errno));
		// reset g_errno
		g_errno = 0;
	}

	// . ok, now i wait for everybody.
	// . TODO: evaluate if this hurts us
	if ( m_numReplies < m_numRequests )
		return false;


	// do we need to launch another batch of summary requests?
	if ( m_numRequests < m_msg3a.m_numDocIds ) {
		// . if we can launch another, do it
		// . say "true" here so it does not call us, gotSummary() and 
		//   do a recursive stack explosion
		// . this returns false if still waiting on more to come back
		if ( ! launchMsg20s ( true ) ) return false; 
		// maybe some were cached?
		//goto refilter;
		// it returned true, so m_numRequests == m_numReplies and
		// we don't need to launch any more! but that does NOT
		// make sense because m_numContiguous < m_msg3a.m_numDocIds
		char *xx=NULL; *xx=0;
	}


	// save this before we increment m_numContiguous
	//long oldNumContiguous = m_numContiguous;

	// . before launching more msg20s, first see if we got enough now
	// . the first "m_numContiguous" of the m_msg20[] are valid!
	// . save this for launchMsg20s() to look at at so it will not keep
	//   launching just to keep m_maxOutstanding satisfied. otherwise, if
	//   msg20[0] is really really slow, we end up getting back *way* more
	//   summaries than we probably need!
	// . this also let's us know how many of the m_msg3a.m_docIds[] and
	//   m_msg20[]s we can look at at this point to determine how many of
	//   the docids are actually "visible" (unclustered)
	// . if enough are already visible we set m_gotEnough to true to
	//   prevent more msg20s being launched. but we must wait for all that
	//   have launched to come back.
	// . visibleContiguous = of the contiguous guys, how many are good, 
	//   i.e. visible/unclustered?
	/*
	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// stop as soon as we hit a gap breaking our contiguity... 
		if ( m_msg20[i] && ! m_msg20[i]->m_gotReply ) break;
		// count every docid
		m_numContiguous++;
		// count if it is visible
		if (m_msg3a.m_clusterLevels[i] == CR_OK) m_visibleContiguous++;
	}
	*/

	// if there's no way we have enough, keep going
        /*
	if ( m_visibleContiguous<m_si->m_docsWanted+m_si->m_firstResultNum+1 &&
	     // there have to be more docids to get summaries for...
	     m_numContiguous < m_msg3a.m_numDocIds ) {
		// . if we can launch another, do it
		// . say "true" here so it does not call us, gotSummary() and 
		//   do a recursive stack explosion
		// . this returns false if still waiting on more to come back
		if ( ! launchMsg20s ( true ) ) return false;
		// maybe some were cached?
		goto refilter;
	}
        */

	// MDW: put this back once we figure out how to prevent so many
	//      wasted summary lookups
	// how many msg20s have we got back but filtered out?
	// long filtered = m_numContiguous - m_visibleContinguous;
	// we don't want to over-launch msg20s if we end up getting what
	// we wanted without any disappearing because of clustering, etc.
	// BUT if we UNDER the hard count, launch more
	//if ( m_numContiguous == oldNumContiguous && ! m_gotEnough ) {
	//	// launch more msg20 requests
	//	if ( ! launchMsg20s ( true ) ) return false;
	//	// maybe some were cached?
	//	//goto refilter;
	//	// it returned true, so m_numRequests == m_numReplies and
	//	// we don't need to launch any more! but that does NOT
	//	// make sense because the reply we just got did not increase
	//	// m_numContiguous, meaning there is a gap we are waiting on.
	//	char *xx=NULL; *xx=0;
	//}

        // this logic here makes us get the msg20s in chunks of 50, so the
	// 51st msg20 request will have to wait for the first 50 replies to
	// arrive before it can even be launched! that seriously slows us down,
	// because we often have a summary that takes 200ms to get... but most
	// take like 10-20ms or so. MDW: comment out later again
	//if( (m_numContiguous == oldNumContiguous) &&
	//    (m_numReplies    <  m_numRequests   )    ) 
	//	return false;

	// so we have to set this to zero 0 i guess
	//oldNumContiguous = 0;
	// and this
	//m_numContiguous = m_numReplies;


	long long startTime = gettimeofdayInMilliseconds();
	long long took;

	// shortcut
	Query *q = m_msg3a.m_q;
        
	//log(LOG_DEBUG, "query: msg40: deduping from %ld to %ld", 
	//oldNumContiguous, m_numContiguous);

	// count how many are visible!
	//long visible = 0;

	// loop over each clusterLevel and set it
	for ( long i = 0 ; i < m_numReplies ; i++ ) {
		// get current cluster level
		char *level = &m_msg3a.m_clusterLevels[i];
		// sanity check -- this is a transistional value msg3a should 
		// set it to something else!
		if ( *level == CR_GOT_REC         ) { char *xx=NULL; *xx=0; }
		if ( *level == CR_ERROR_CLUSTERDB ) { char *xx=NULL; *xx=0; }
		// skip if already "bad"
		if ( *level != CR_OK ) continue;
		// if the user only requested docids, we have no summaries
		if ( m_si->m_docIdsOnly ) break;
		// convenient var
		Msg20 *m = m_msg20[i];
		// get the Msg20 reply
		Msg20Reply *mr = m->m_r;
		// if no reply, all hosts must have been dead i guess so
		// filter out this guy
		if ( ! mr && ! m->m_errno ) {
			logf(LOG_DEBUG,"query: msg 20 reply was null.");
			m->m_errno = ENOHOSTS;
		}
		// if any msg20 has m_errno set, then set ours so at least the
		// xml feed will know there was a problem even though it may 
		// have gotten search results.
		// the BIG HACK is done in Msg20. Msg20::m_errno is set to 
		// something like EMISSINGQUERYTERMS if the document really
		// doesn't match the query, maybe because of indexdb corruption
		if ( m->m_errno ) {
			if ( m_si->m_debug || g_conf.m_logDebugQuery )
			logf( LOG_DEBUG, "query: result %li (docid=%lli) had "
			     "an error (%s) and will not be shown.", i,
			      m_msg3a.m_docIds[i],  mstrerror(m->m_errno));
			*level = CR_ERROR_SUMMARY;
			//m_visibleContiguous--; 
			// update our m_errno while here
			if ( ! m_errno ) m_errno = m->m_errno;
			continue;
		}
		// a special case
		if ( mr && mr->m_errno == CR_RULESET_FILTERED ) {
			*level = CR_RULESET_FILTERED;
			//m_visibleContiguous--;
			continue;
		}
		// this seems to be set too!
		if ( mr && mr->m_errno == EDOCFILTERED ) {
			*level = CR_RULESET_FILTERED;
			//m_visibleContiguous--;
			continue;
		}
		if ( ! m_si->m_showBanned && mr->m_isBanned ) {
			if ( m_si->m_debug || g_conf.m_logDebugQuery )
			logf ( LOG_DEBUG, "query: result %li (docid=%lli) is "
			       "banned and will not be shown.", i, 
			       m_msg3a.m_docIds[i] );
			*level = CR_BANNED_URL;
                        //m_visibleContiguous--;
			continue;
		}
		// filter out urls with <![CDATA in them
		if ( strstr(mr->ptr_ubuf, "<![CDATA[") ) {
			*level = CR_BAD_URL;
                        //m_visibleContiguous--;
			continue;
		}
		// also filter urls with ]]> in them
		if ( strstr(mr->ptr_ubuf, "]]>") ) {
			*level = CR_BAD_URL;
                        //m_visibleContiguous--;
			continue;
		}
		if( ! mr->m_hasAllQueryTerms ) {
			if ( m_si->m_debug || g_conf.m_logDebugQuery )
			logf( LOG_DEBUG, "query: result %li (docid=%lli) is "
			      "missing query terms and will not be"
			      " shown.", i, m_msg3a.m_docIds[i] );
			*level = CR_MISSING_TERMS;
                        //m_visibleContiguous--;
			// uncluster any docids below this one
			if ( m_unclusterCount-- > 0 ) uncluster ( i );
			continue;
		}
		//visible++;
	}

	// . assume no dups removed
	// . we print "click here to show ommitted results" if this is true
	m_removedDupContent = false;

	// what is the deduping threshhold? 0 means do not do deuping
	long dedupPercent = 0;
	if ( m_si->m_doDupContentRemoval && m_si->m_percentSimilarSummary )
		dedupPercent = m_si->m_percentSimilarSummary;
	// if the user only requested docids, we have no summaries
	if ( m_si->m_docIdsOnly ) dedupPercent = 0;

	// filter out duplicate/similar summaries
	for ( long i = 0 ; dedupPercent && i < m_numReplies ; i++ ) {
		// skip if already invisible
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// start with the first docid we have not yet checked!
		//long m = oldNumContiguous;
		// get it
		Msg20Reply *mri = m_msg20[i]->m_r;
		// never let it be i
		//if ( m <= i ) m = i + 1;
		// see if any result lower-scoring than #i is a dup of #i
		for( long m = i+1 ; m < m_numReplies ; m++ ) {
			// get current cluster level
			char *level = &m_msg3a.m_clusterLevels[m];
			// skip if already invisible
			if ( *level != CR_OK ) continue;
			// get it
			Msg20Reply *mrm = m_msg20[m]->m_r;
			// use gigabit vector to do topic clustering, etc.
			long *vi = (long *)mri->ptr_vbuf;
			long *vm = (long *)mrm->ptr_vbuf;
			//char  s  = g_clusterdb.
			//	getSampleSimilarity (vi,vm,VECTOR_REC_SIZE );
			float s ;
			s = computeSimilarity(vi,vm,NULL,NULL,NULL,
					      m_si->m_niceness);
			// skip if not similar
			if ( (long)s < dedupPercent ) continue;
			// otherwise mark it as a summary dup
			if ( m_si->m_debug || g_conf.m_logDebugQuery )
				logf( LOG_DEBUG, "query: result #%ld "
				      "(docid=%lli) is %.02f%% similar-"
				      "summary of #%li (docid=%lld)", 
				      m, m_msg3a.m_docIds[m] , 
				      s, i, m_msg3a.m_docIds[i] );
			*level = CR_DUP_SUMMARY;
                        //m_visibleContiguous--;
			m_removedDupContent = true;
			// uncluster the next clustered docid from this 
			// hostname below "m"
			if ( m_unclusterCount-- > 0 ) uncluster ( m );
		}
	}



        //
        // BEGIN URL NORMALIZE AND COMPARE
        // 
        
        // . ONLY DEDUP URL if it explicitly enabled AND we are not performing
        //   a site: or suburl: query.
        if(m_si->m_dedupURL && 
	   !q->m_hasPositiveSiteField && 
	   !q->m_hasSubUrlField) { 
		for(long i = 0 ; i < m_msg3a.m_numDocIds ; i++) {
                        // skip if already invisible
                        if(m_msg3a.m_clusterLevels[i] != CR_OK) continue;
			// get it
			Msg20Reply *mr = m_msg20[i]->m_r;
                        // hash the URL all in lower case to catch wiki dups
			char *url  = mr-> ptr_ubuf;
			long  ulen = mr->size_ubuf - 1;
			
			// since the redirect url is a more accurate 
			// representation of the conent do that if it exists.
			if ( mr->ptr_rubuf ) {
				url  = mr-> ptr_rubuf;
				ulen = mr->size_rubuf - 1;
			}

                        // fix for directories, sometimes they are indexed 
                        // without a trailing slash, so let's normalize to 
                        // this standard.
			if(url[ulen-1] == '/')
				ulen--;
			Url u;
                        u.set(url,ulen);
                        url   = u.getHost();

                        if(u.getPathLen() > 1) {
                                // . remove sub-domain to fix conflicts with
                                //   sites having www,us,en,fr,de,uk,etc AND 
                                //   it redirects to the same page.
                                char *host = u.getHost();
                                char *mdom = u.getMidDomain();
                                if(mdom && host) {
                                        long  hlen = mdom - host;
                                        if (isSubDom(host, hlen-1))
                                                url = mdom;
                                }
                        }

                        // adjust url string length
                        ulen -= url - u.getUrl();

			uint64_t h = hash64Lower_a(url, ulen);
                        long slot = m_urlTable.getSlot(h);
                        // if there is no slot,this url doesn't exist => add it
                        if(slot == -1) {
                                m_urlTable.addKey(h,mr->m_docId);
                        }
                        else {
                                // If there was a slot, denote with the 
                                // cluster level URL already exited previously
                                char *level = &m_msg3a.m_clusterLevels[i];
                                if(m_si->m_debug || g_conf.m_logDebugQuery)
                                        logf(LOG_DEBUG, "query: result #%ld "
                                                        "(docid=%lli) is the "
                                                        "same URL as "
                                                        "(docid=%lld)", 
                                                        i,m_msg3a.m_docIds[i], 
                                                        m_urlTable.
					     getValueFromSlot(slot));
                                *level = CR_DUP_URL;
                                //m_visibleContiguous--;
                                m_removedDupContent = true;
                        }
                }
        }

        //
        // END URL NORMALIZE AND COMPARE
        // 

	// how many docids are visible? (unfiltered)
	//long visible = m_filterStats[CR_OK];

	// count how many are visible!
	long visible = 0;
	// loop over each clusterLevel and set it
	for ( long i = 0 ; i < m_numReplies ; i++ ) {
		// get current cluster level
		char *level = &m_msg3a.m_clusterLevels[i];
		// on CR_OK
		if ( *level == CR_OK ) visible++;
	}

	// do we got enough search results now?
	//if ( visible >= m_docsWanted ) 
	//	m_gotEnough = true;

	// show time
	took = gettimeofdayInMilliseconds() - startTime;
	if ( took > 3 )
		log(LOG_INFO,"query: Took %lli ms to do clustering and dup "
		    "removal.",took);

	// do we have enough visible at this point?
	//if ( m_visibleContiguous >= m_docsToGetVisible ) m_gotEnough = true;


	// . wait for all the replies to come in
	// . no more should be launched in launchedMsg20s() since we set 
	//   m_gotEnough to true
	// . MDW: i added "m_gotEnough &&" to this line...
	//if ( m_gotEnough && m_numReplies < m_numRequests ) return false;

	// . let's wait for the tasks to complete before even trying to launch
	//   more than the first MAX_OUTSTANDING msg20s
	// . the msg3a re-call will end up re-doing our tasks as well! so we
	//   have to make sure they complete at this point
	if ( m_tasksRemaining > 0 ) return false;

	// debug
	bool debug = (m_si->m_debug || g_conf.m_logDebugQuery);
	for ( long i = 0 ; debug && i < m_msg3a.m_numDocIds ; i++ ) {
		//unsigned long sh;
		//sh = g_titledb.getHostHash(*(key_t*)m_msg20[i]->m_vectorRec);
		long cn = (long)m_msg3a.m_clusterLevels[i];
		if ( cn < 0 || cn >= CR_END ) { char *xx=NULL;*xx=0; }
		char *s = g_crStrings[cn];
		if ( ! s ) { char *xx=NULL;*xx=0; }
		logf(LOG_DEBUG, "query: msg40 final hit #%li) d=%llu "
		     "cl=%li (%s)", 
		     i,m_msg3a.m_docIds[i],(long)m_msg3a.m_clusterLevels[i],s);
	}
	if ( debug )
		logf (LOG_DEBUG,"query: msg40: firstResult=%li, "
		      "totalDocIds=%ld, resultsWanted=%ld "
		      "visible=%li toGet=%li recallCnt=%li",
		      m_si->m_firstResultNum, m_msg3a.m_numDocIds ,
		      m_docsToGetVisible, visible,
		      //m_numContiguous, 
		      m_docsToGet , m_msg3aRecallCnt);

	// if we do not have enough visible, try to get more
	if ( visible < m_docsToGetVisible && m_msg3a.m_moreDocIdsAvail ) {
		// can it cover us?
		long need = m_msg3a.m_docsToGet + 20;
		// note it
		log("msg40: too many summaries invisible. getting more "
		    "docids from msg3a merge and getting summaries. "
		    "%li are visible, need %li. "
		    "%li to %li. "
		    "numReplies=%li numRequests=%li",
		    visible, m_docsToGetVisible,
		    m_msg3a.m_docsToGet, need,
		    m_numReplies, m_numRequests);
		// get more
		//m_docsToGet = need;
		// merge more
		m_msg3a.m_docsToGet = need;
		m_msg3a.mergeLists();
		// rellaoc the msg20 array
		if ( ! reallocMsg20Buf() ) return true;
		// reset this before launch
		m_numReplies  = 0;
		m_numRequests = 0;
		// reprocess all!
		m_lastProcessedi = -1;
		// now launch!
		if ( ! launchMsg20s ( true ) ) return false; 
		// all done, call callback
		return true;
	}

	     /*
	// if we do not have enough visible, try to get more
	if ( visible < m_docsToGet &&
	     // if we got some docids yet to get, from any tier...
	     //m_msg3a.m_moreDocIdsAvail &&
	     // do not recall until all done with the msg20s
	     //m_numContiguous >= m_msg3a.m_numDocIds &&
	     // and we had to have gotten all requested of use but just lost 
	     // some docids due to clustering/filtering
	     m_msg3a.m_numDocIds >= m_docsToGet &&
	     // . only recall 3 times at most
	     // . this also prevents potential stack explosion since we
	     //   re-call getDocIds() below!
	     m_msg3aRecallCnt < 3 &&
	     // do not recall if doing rerank
	     //m_si->m_rerankRuleset < 0 &&
	     // do not recall if we got the max to compute
	     m_msg3a.m_numDocIds < m_maxDocIdsToCompute ) {
		// get the visibility ratio
		float ratio ;
		if ( m_visibleContiguous < 2 ) 
			ratio = m_msg3a.m_numDocIds / 1;
		else    
			ratio = m_msg3a.m_numDocIds / m_visibleContiguous;
		// always boost by at least 50% more for good measure
		ratio *= 1.5;
		// keep stats on it
		g_stats.m_msg3aRecallCnt++;
		m_msg3aRecallCnt++;
		// . re-call msg3a and ask for more docids because some of them
		//   are invisible/filtered and we need more
		// . MDW: can we make Msg3a just re-do its merge if it can,
		//        rather than re-call Msg39 again? (TODO)
		// . apply the ratio, to get more docids
		long get = (long)((float)m_docsToGet * ratio);
		// do not breach the limit
		if ( get > m_maxDocIdsToCompute ) get = m_maxDocIdsToCompute;
		// . if different, recall msg3a
		// . if we are then we can start from msg3a.MergedocIds
		if ( get > m_docsToGet ) {
			// debug msg
			//if ( g_conf.m_logDebugQuery || m_si->m_debug )
			logf(LOG_DEBUG,"query: msg40: recalling msg3a "
			     "merge oldactual=%li newactual=%li",
			     m_docsToGet,get);
			// ok, we got a new number to get now
			m_docsToGet = get;
			// let's do it all from the top!
			return getDocIds ( true ) ;
			// NOTE: we no longer do msg3a re-calls for simplicity
			// so all re-calling is done from right here only
			// MDW: hack it in msg3a too
			//m_msg3a.m_docsToGet = get;
			// . true = recalled?
			// . this will re-merge the lists with a higher
			//   m_docsToGet and hopefully squeeze more docids out
			// . this will block (return false) if it has to 
			//   re-call the Msg39s to get more docids by calling
			//   Msg3a::fetchLists().
			// . if it blocks it will eventually call our
			//   gotDocIdsWrapper() callback
			//if ( ! m_msg3a.mergeLists ( true ) ) return false;
			// hey, we got some more docids out of the merge, 
			// so check them out
			//goto refilter;
		}
	}
	*/


	/*
	// how many msg20::getSummary() calls did we do unnecessarily?
	long vcnt = 0;
	for ( long i = 0 ; i <= m_maxiLaunched ; i++ ) {
		// skip if never launched and should have... a gap...
		if ( m_msg20[i] && ! m_msg20[i]->m_gotReply ) continue;
		// get cluster level
		char level = m_msg3a.m_clusterLevels[i];
		// sanity check
		if ( level < 0 || level >= CR_END ) { char *xx=NULL; *xx =0; }
		// add it up
		g_stats.m_filterStats[(long)level]++;
		// skip if NOT visible
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// count if visible
		vcnt++;
		// skip if not wasted
		if ( vcnt <= m_docsToGetVisible ) continue;
		// a special g_stat, means msg20 call was not necessary
		g_stats.m_filterStats[CR_WASTED]++;
		// discount from visible
		g_stats.m_filterStats[CR_OK]--;
	}
	*/

	// get time now
	long long now = gettimeofdayInMilliseconds();
	// . add the stat for how long to get all the summaries
	// . use purple for tie to get all summaries
	// . THIS INCLUDES Msg3a/Msg39 RECALLS!!!
	// . can we subtract that?
	g_stats.addStat_r ( 0           , 
			    m_startTime , 
			    now         ,
			    //"get_all_summaries",
			    0x008220ff  );
	// timestamp log
	if ( g_conf.m_logTimingQuery || m_si->m_debug )
		logf(LOG_DEBUG,"query: msg40: [%li] Got %li summaries in "
		    "%lli ms",
		     (long)this ,
		     visible, // m_visibleContiguous,
		     now - m_startTime );


	//long maxAge = 0;
	//if ( m_si->m_rcache ) maxAge = g_conf.m_titledbMaxCacheAge;




	/////////////
	//
	//
	// prepare query term extra info for gigabits
	//
	////////////

	//QueryTerm *qterms[MAX_QUERY_TERMS];
	//long nqt = 0;
	//Query *q = m_si->m_q;
	// english? TEST!
	unsigned char lang = m_si->m_queryLang;
	if ( lang == 0 ) { char *xx=NULL;*xx=0; }
	// we gotta use query TERMS not words, because the query may be
	// 'cd rom' and the phrase term will be 'cdrom' which is a good one
	// to use for gigabits! plus we got synonyms now!
	for ( long i = 0 ; i < q->m_numTerms ; i++ ) {
		// shortcut
		QueryTerm *qt = &q->m_qterms[i];
		// assume ignored
		qt->m_popWeight = 0;
		qt->m_hash64d   = 0;
		// skip if ignored query stop word etc.
		if ( qt->m_ignored && qt->m_ignored != IGNORE_QUOTED )continue;
		// get the word or phrase
		char *s    = qt->m_term;
		long  slen = qt->m_termLen;
		// use this special hash for looking up popularity in pop dict
		// i think it is just like hash64 but ignores spaces so we
		// can hash 'cd rom' as "cdrom". but i think we do this
		// now, so use m_termId as see...
		unsigned long long qh = hash64d(s, slen);
		//long long qh = qt->m_termId;
		long qpop;
		qpop = g_speller.getPhrasePopularity(s, qh, true,lang);
		long qpopWeight;
		if       ( qpop < QPOP_ZONE_0 ) qpopWeight = QPOP_MULT_0;
		else if  ( qpop < QPOP_ZONE_1 ) qpopWeight = QPOP_MULT_1;
		else if  ( qpop < QPOP_ZONE_2 ) qpopWeight = QPOP_MULT_2;
		else if  ( qpop < QPOP_ZONE_3 ) qpopWeight = QPOP_MULT_3;
		else if  ( qpop < QPOP_ZONE_4 ) qpopWeight = QPOP_MULT_4;
		else                            qpopWeight = 1;
		// remember them in the query term
		qt->m_hash64d   = qh;
		qt->m_popWeight = qpopWeight;
		// store that queryterm ptrs into our array
		//qterms[nqt] = qt;
		//nqt++;
		// debug it
		if ( ! m_si->m_debugGigabits ) continue;
		SafeBuf msg;
		msg.safePrintf("gbits: qpop=%li qweight=%li "
			       "queryterm=",
			       qpop,qpopWeight);
		msg.safeMemcpy(qt->m_term,qt->m_termLen);
		msg.pushChar('\0');
		logf(LOG_DEBUG,"%s",msg.getBufStart());
	}



	/////////////
	//
	// make gigabits
	//
	/////////////
	if ( m_docsToScanForTopics > 0 ) {
		// time it
		long long stt = gettimeofdayInMilliseconds();
		// get the fist one, just use that for now
		TopicGroup *tg = &m_si->m_topicGroups[0];


		// . this will not block
		// . this code is in XmlDoc.cpp
		// . samples are from XmlDoc::getSampleForGigabits(), generated
		//   for each titlerec in the search results
		// . SHIT! lets go back to the old code since this was
		//   the new approach and didn't support single lower-case
		//   words, like "parchment" for the 'Magna Carta' query.
		//   or 'copies' for the 'Magna Carta' query, etc. which
		//   i think are very interesting, especially when displayed
		//   in sentences
		// . set m_gigabitInfos[] to be the gigabits
		if ( ! computeGigabits( tg ) ) {
			// note it
			log("gbits: general error: %s",mstrerror(g_errno));
			// g_errno should be set on error here!
			return true;
		}

		// now make the fast facts from the gigabits and the
		// samples. these are sentences containing the query and
		// a gigabit.
		if ( ! computeFastFacts ( ) ) {
			// note it
			log("gbits: general error: %s",mstrerror(g_errno));
			// g_errno should be set on error here!
			return true;
		}
		

		/*
		long ng;
		ng = intersectGigabits ( //m_msg3a.m_q->m_orig       ,
					//m_msg3a.m_q->m_origLen    ,
					m_msg20                     ,
					m_msg3a.m_numDocIds,//m_numContiguous
					//m_msg3a.getClusterLevels(),
					//m_si->m_topicGroups       ,
					//m_si->m_numTopicGroups    ,
					m_si->m_langHint            ,
					tg->m_maxTopics             ,
					tg->m_docsToScanForTopics   ,
					tg->m_minDocCount           ,
					m_gigabitInfos              ,
					m_si->m_niceness            );
		*/
		// ng is -1 on error, g_errno should be set
		//if ( ng == -1 ) return true;
		// otherwise, it is legit!
		//m_numGigabitInfos = ng;
		// sanity check
		//if ( ng > 50 ) { char *xx=NULL;*xx=0; }
		// time it
		long long took = gettimeofdayInMilliseconds() - stt;
		if ( took > 5 )
			logf(LOG_DEBUG,"query: make gigabits took %lli ms",
			     took);
	}


	// run post query reranks for this query
	long wanted = m_si->m_docsWanted + m_si->m_firstResultNum + 1;

	if ( m_postQueryRerank.isEnabled() && 
	    m_postQueryRerank.set2(wanted)){
		if (      ! m_postQueryRerank.preRerank  () ) {
			log("query: PostQueryRerank::"
			    "preRerank() failed.");
			m_postQueryRerank.rerankFailed();
		}
		else if ( ! m_postQueryRerank.rerank     () ) {
			log("query: PostQueryRerank::"
			    "rerank() failed.");
			m_postQueryRerank.rerankFailed();
		}
		else if ( ! m_postQueryRerank.postRerank () ) {
			log("query: PostQueryRerank::"
			    "postRerank() failed.");
			m_postQueryRerank.rerankFailed();
		}
	}

	// set m_moreToCome, if true, we print a "Next 10" link
	m_moreToCome = (visible > //m_visibleContiguous > 
			m_si->m_docsWanted+m_si->m_firstResultNum);
	if ( m_si->m_debug || g_conf.m_logDebugQuery ) 
		logf ( LOG_DEBUG, "query: msg40: more? %d",   m_moreToCome );

	// alloc m_buf, which should be NULL
	if ( m_buf ) { char *xx = NULL; *xx = 0; }

	// . we need to collapse m_msg3a.m_docIds[], etc. into m_docIds[] etc
	//   to be just the docids we wanted.
	// . at this point we should merge in all docids from all Msg40s from
	//   different clusters, etc.
	// . now alloc space for "docsWanted" m_docIds[], m_scores[], 
	//   m_bitScores[], m_clusterLevels[] and m_newMsg20[]

	//
	// HACK TIME
	//

	// . bury filtered/clustered docids from m_msg3a.m_docIds[]
	// . also remove result no in the request window specified by &s=X&n=Y
	//   where "s" is m_si->m_firstResultNum (which starts at 0) and "n" 
	//   is the number of results requested, m_si->m_docsWanted
	// . this is a bit of a hack (MDW)
	long c = 0;
	long v = 0;
	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// assume we got a valid docid
		bool skip = false;
		// must ahve a cluster level of CR_OK (visible)
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) skip = true;
		// v is the visible count
		else if ( v++ < m_si->m_firstResultNum )
			skip = true;
		// . if skipping a valid msg20, give it a chance to destruct
		// . no longer do this because CR_SUMMARY_MERGED needs to keep
		//   the msg20 reply around so PageResults.cpp can merge the 
		//   event  descriptions
		//if ( skip && m_msg20[i] ) m_msg20[i]->destructor();
		// if skipping continue
		if ( skip ) continue;
		// we got a winner, save it
		m_msg3a.m_docIds        [c] = m_msg3a.m_docIds        [i];
		m_msg3a.m_scores        [c] = m_msg3a.m_scores        [i];
		m_msg3a.m_clusterLevels [c] = m_msg3a.m_clusterLevels [i];
		m_msg20                 [c] = m_msg20                 [i];
		m_msg3a.m_scoreInfos    [c] = m_msg3a.m_scoreInfos    [i];
		long need = m_si->m_docsWanted;
		// if done, bail
		if ( ++c >= need ) break;
	}
	// reset the # of docids we got to how many we kept!
	m_msg3a.m_numDocIds = c;

	// debug
	for ( long i = 0 ; debug && i < m_msg3a.m_numDocIds ; i++ )
		logf(LOG_DEBUG, "query: msg40 clipped hit #%li) d=%llu "
		     "cl=%li (%s)", 
		     i,m_msg3a.m_docIds[i],(long)m_msg3a.m_clusterLevels[i],
		     g_crStrings[(long)m_msg3a.m_clusterLevels[i]]);

	//
	// END HACK
	// 

	// . uc = use cache?
	// . store in cache now if we need to
	bool uc = false;
	if ( m_si->m_useCache   ) uc = true;
	if ( m_si->m_wcache     ) uc = true;
	// . do not store if there was an error
	// . no, allow errors in cache since we often have lots of 
	//   docid not founds and what not, due to index corruption and
	//   being out of sync with titledb
	if ( m_errno            &&
	     // forgive "Record not found" errors, they are quite common
	     m_errno != ENOTFOUND &&
	     m_errno != EMISSINGQUERYTERMS ) {
		logf(LOG_DEBUG,"query: not storing in cache: %s",
		     mstrerror(m_errno));
		uc = false;
	}
	if ( m_si->m_docIdsOnly ) uc = false;
	// all done if not storing in cache
	if ( ! uc ) return true;

	// debug
	if ( m_si->m_debug )
		logf(LOG_DEBUG,"query: [%lu] Storing output in cache.",
		     (unsigned long)this);
	// store in this buffer
	char tmpBuf [ 64 * 1024 ];
	// use that
	char *p = tmpBuf;
	// how much room?
	long tmpSize = getStoredSize();
	// unless too small
	if ( tmpSize > 64*1024 ) 
		p = (char *)mmalloc(tmpSize,"Msg40Cache");
	if ( ! p ) {
		// this is just for cachinig, not critical... ignore errors
		g_errno = 0;
		logf ( LOG_INFO ,
		       "query: Size of cached search results page (and "
		       "all associated data) is %li bytes. Max is %i. "
		       "Page not cached.", tmpSize, 32*1024 );
		return true;
	}
	// serialize into tmp
	long nb = serialize ( p , tmpSize );
	// it must fit exactly
	if ( nb != tmpSize || nb == 0 ) {
		g_errno = EBADENGINEER;
		log (LOG_LOGIC,
		     "query: Size of cached search results page (%li) "
		     "does not match what it should be. (%li)",
		     nb, tmpSize );
		return true;
	}

	if ( ! m_r.m_getDocIdScoringInfo ) {
		// make key based on the hash of certain vars in SearchInput
		key_t k = m_si->makeKey();
		// cache it
		m_msg17.storeInCache ( SEARCHRESULTS_CACHEID ,
				       k                     ,
				       p                     , // rec
				       tmpSize               , // recSize
				       m_si->m_coll2         ,
				       m_si->m_niceness      ,
				       3                     ); //timeout=3secs
	}

	// free it, cache will copy it into its ring buffer
	if ( p != tmpBuf ) mfree ( p , tmpSize , "Msg40Cache" );
	// ignore errors
	g_errno = 0;
 	return true;
}

// m_msg3a.m_docIds[m] was filtered because it was a dup or something so we
// must "uncluster" the *next* docid from the same hostname that is clustered
void Msg40::uncluster ( long m ) {

	// skip for now
	return;

	key_t     crec1 = m_msg3a.m_clusterRecs[m];
	long long sh1   = g_clusterdb.getSiteHash26 ( (char *)&crec1 );

	for ( long k = 0 ; k < m_msg3a.m_numDocIds ; k++ ) {
		// skip docid #k if not from same hostname
		key_t     crec2 = m_msg3a.m_clusterRecs[k];
		long long sh2   = g_clusterdb.getSiteHash26 ( (char *)&crec2 );
		if ( sh2 != sh1 ) continue;
		// skip if not OK or CLUSTERED
		if ( m_msg3a.m_clusterLevels[k] != CR_CLUSTERED ) continue;
		// UNHIDE IT
		m_msg3a.m_clusterLevels[k] = CR_OK;
		// we must UN-dedup anything after us because now that we are
		// no longer clustered, we could dedup a result below us,
		// which deduped another result, which is now no longer deduped
		// because its deduped was this unclustered results dup! ;)
		for ( long i = k+1 ; i < m_msg3a.m_numDocIds ; i++ ) {
			// get current cluster level
			char *level = &m_msg3a.m_clusterLevels[i];
			// reset dupped guys, they will be re-done if needed!
			if ( *level == CR_DUP_SUMMARY ) *level = CR_OK;
			if ( *level == CR_DUP_TOPIC   ) *level = CR_OK;
		}
		// . reset this so it gets re-computed
		// . we are placing a gap at m_msg20[k] since
		//   m_msg20[k].m_gotReply = false
		//m_numContiguous     = 0; 
		//m_visibleContiguous = 0;
		// debug note
		logf(LOG_DEBUG,"query: msg40: unclustering docid #%li %lli. "
		     "(unclusterCount=%li)",
		     k,m_msg3a.m_docIds[k],m_unclusterCount);
		// . steal the msg20!
		// . sanity check -- should have been NULL!
		if (   m_msg20[k] ) { char *xx=NULL; *xx=0; }
		// sanity check
		if ( ! m_msg20[m] ) { char *xx=NULL; *xx=0; }
		// sanity check
		if ( k == m ) { char *xx=NULL; *xx=0; }
		// for every one guy marked as a dup, we uncluster FIVE
		//if ( ++count >= 5 ) break;
		// grab it
		m_msg20[k] = m_msg20[m];
		// reset it, m_gotReply should be false now
		m_msg20[k]->reset();
		// the dup guy has a NULL ptr now
		m_msg20[m] = NULL;
		// . only have to unhide one at a time
		// . one is a dup, so no more than one will
		//   become UNhidden
		break;
	}
}

long Msg40::getStoredSize ( ) {
	// moreToCome=1
	long size = 1;
	// msg3a
	size += m_msg3a.getStoredSize();
	// add each summary
	for ( long i = 0 ; i < m_msg3a.m_numDocIds; i++ ) {
		// do not store the big samples if we're not storing cached 
		// copy. if "includeCachedCopy" is true then the page itself 
		// will be the summary.
		//if ( ! m_si->m_includeCachedCopy )
		//	m_msg20[i]->clearBigSample();
		// getting rid of this makes it take up less room
		m_msg20[i]->clearLinks();
		m_msg20[i]->clearVectors();
		// if not visisble, do not store!
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// otherwise, store it
		size += m_msg20[i]->getStoredSize();
	}
	// . the related topics, only those whose score is >= m_minTopicScore
	// . nah, just re-instersect from the msg20 replies again! its quick
	//size += m_msg24.getStoredSize ( );
	//size += m_msg1a.getStoredSize ( );
	// cache msg2b if we have it
	size += m_msg2b.getStoredSize();

	return size;
}

// . serialize ourselves for the cache
// . returns bytes written
// . returns -1 and sets g_errno on error
long Msg40::serialize ( char *buf , long bufLen ) {
	// set the ptr stuff
	char *p    = buf;
	char *pend = buf + bufLen;

	// miscellaneous
	*p++ = m_moreToCome;

	// msg3a:
	// m_numDocIds[]
	// m_docIds[]
	// m_scores[]
	// m_clusterLevels[]
	// m_totalHits (estimated)
	long nb = m_msg3a.serialize ( p , pend );
	// return -1 on error
	if ( nb < 0 ) return -1;
	// otherwise, inc over it
	p += nb;

	// . then summary excerpts, keep them word aligned...
	// . TODO: make sure empty Msg20s are very little space!
	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// sanity check
		if ( m_msg3a.m_clusterLevels[i] == CR_OK && ! m_msg20[i] ) {
			char *xx = NULL; *xx = 0; }
		// if null skip it
		if ( ! m_msg20[i] ) continue;
		// do not store the big samples if we're not storing cached 
		// copy. if "includeCachedCopy" is true then the page itself 
		// will be the summary.
		//if ( ! m_si->m_includeCachedCopy )
		//	m_msg20[i]->clearBigSample();
		// getting rid of this makes it take up less room
		m_msg20[i]->clearLinks();
		m_msg20[i]->clearVectors();
		// if not visisble, do not store!
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// return -1 on error, g_errno should be set
		long nb = m_msg20[i]->serialize ( p , pend - p ) ;
		// count it
		if ( m_r.m_debug )
			log("query: msg40 serialize msg20size=%li",nb);
		//if ( m_r.m_debug ) {
		//	long mcount = 0;
		//	Msg20Reply *mr = m_msg20[i]->m_r;
		//	for ( long *mm = &mr->size_tbuf ; 
		//	      mm <= &mr->size_templateVector ; 
		//	      mm++ ) {
		//		log("query: msg20 #%li = %li",
		//		    mcount,*mm);
		//		mcount++;
		//	}
		//}
		if ( nb == -1 ) return -1;
		p += nb;
	}

	// nah, just re-instersect from the msg20 replies again! its quick
	//long x = m_msg24.serialize ( p , pend - p );
	//if ( x == -1 ) return -1;
	//p += x;

	//long y = m_msg1a.serialize (p, pend - p);
	//if ( y == -1 ) return -1;
	//p += y;

	long z = m_msg2b.serialize (p, pend - p);
	if ( z == -1 ) return -1;
	p += z;

	if ( m_r.m_debug )
		log("query: msg40 serialize nd=%li "
		    "msg3asize=%li ",m_msg3a.m_numDocIds,nb);

	// return bytes stored
	return p - buf;
}

// . deserialize ourselves for the cache
// . returns bytes written
// . returns -1 and sets g_errno on error
long Msg40::deserialize ( char *buf , long bufSize ) {

	// we OWN the buffer
	m_buf        = buf;
	m_bufMaxSize = bufSize;

	// set the ptr stuff
	char *p    = buf;
	char *pend = buf + bufSize;

	// miscellaneous
	m_moreToCome      = *p++;

	// msg3a:
	// m_numDocIds
	// m_docIds[]
	// m_scores[]
	// m_clusterLevels[]
	// m_totalHits (estimated)
	long nb = m_msg3a.deserialize ( p , pend );
	// return -1 on error
	if ( nb < 0 ) return -1;
	// otherwise, inc over it
	p += nb;

	// . alloc buf to hold all m_msg20[i] ptrs and the Msg20s they point to
	// . return -1 if this failed! it will set g_errno/m_errno already
	if ( ! reallocMsg20Buf() ) return -1;

	// MDW: then summary excerpts, keep them word aligned...
	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// if flag is 0 that means a NULL msg20
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// return -1 on error, g_errno should be set
		long x = m_msg20[i]->deserialize ( p , pend - p ) ;
		if ( x == -1 ) return -1;
		p += x;
	}

	// msg2b
	long z = m_msg2b.deserialize ( p , pend - p );
	if ( z == -1 ) return -1;
	p += z;

	// return bytes read
	return p - buf;
}


static char      *s_subDoms[] = {
        // Common Language sub-domains
        "en" ,
        "fr" ,
        "es" ,
        "ru" ,
        "zz" ,
        "ja" ,
        "tw" ,
        "cn" ,
        "ko" ,
        "de" ,
        "nl" ,
        "it" ,
        "fi" ,
        "sv" ,
        "no" ,
        "pt" ,
        "vi" ,
        "ar" ,
        "he" ,
        "id" ,
        "el" ,
        "th" ,
        "hi" ,
        "bn" ,
        "pl" ,
        "tl" ,
        // Common Country sub-domains
        "us" ,
        "uk" ,
        // Common web sub-domains
        "www" };
static HashTable  s_subDomTable;
static bool       s_subDomInitialized = false;
static bool initSubDomTable(HashTable *table, char *words[], long size ){
	// set up the hash table
	if ( ! table->set ( size * 2 ) ) 
		return log(LOG_INIT,"build: Could not init sub-domain "
			   "table." );
	// now add in all the stop words
	long n = (long)size/ sizeof(char *); 
	for ( long i = 0 ; i < n ; i++ ) {
		char      *sw    = words[i];
		long       swlen = gbstrlen ( sw );
                long h = hash32Lower_a(sw, swlen);
                long slot = table->getSlot(h);
                // if there is no slot, this url doesn't exist => add it
                if(slot == -1)
                        table->addKey(h,0);
                else 
                        log(LOG_INIT,"build: Sub-domain table has duplicates");
	}
	return true;
}

bool isSubDom(char *s , long len) {
	if ( ! s_subDomInitialized ) {
		s_subDomInitialized = 
			initSubDomTable(&s_subDomTable, s_subDoms, 
				      sizeof(s_subDoms));
		if (!s_subDomInitialized) return false;
	} 

	// get from table
        long h = hash32Lower_a(s, len);
        if(s_subDomTable.getSlot(h) == -1)
                return false;
	return true;
}		




//////////////////////////////////
//
// COMPUTE GIGABITS!!!
//
//////////////////////////////////


bool hashSample ( Query *q, 
		  HashTableX *master, 
		  TopicGroup *tg ,
		  SafeBuf *vecBuf,
		  Msg20 *thisMsg20 ,
		  HashTableX *repeatTable ,
		  bool debugGigabits ) ;


static int gigabitCmp ( const void *a, const void *b ) {
	Gigabit *ga = *(Gigabit **)a;
	Gigabit *gb = *(Gigabit **)b;
	// put termlen =0 at end, that means it was nuked
	//if ( ga->m_termLen == 0 && gb->m_termLen >  0 ) return 1; // swap
	//if ( ga->m_termLen  > 0 && gb->m_termLen == 0 ) return -1;
	float sa = ga->m_gbscore * ga->m_numPages;
	float sb = gb->m_gbscore * gb->m_numPages;
	// "King John" on 6 pages should be "John" on 12!
	sa *= ga->m_numWords;
	sb *= gb->m_numWords;
	// punish if only on one page
	if ( ga->m_numPages <= 1 ) sa /= 4.0;
	if ( gb->m_numPages <= 1 ) sb /= 4.0;
	if ( sa < sb ) return  1; // swap!
	if ( sa > sb ) return -1;
	if ( ga->m_numPages < gb->m_numPages ) return  1; // swap
	if ( ga->m_numPages > gb->m_numPages ) return -1;
	if ( ga->m_termLen > gb->m_termLen ) return  1; // swap
	if ( ga->m_termLen < gb->m_termLen ) return -1;
	return 0;
}
	

//#define MAXPOP 10000
#define MAXPOP 32000

//
// . set m_gigabitInfos[] array and return # of them we set
// . returns -1 with g_errno set on error
// . fills m_gigabitPtrs safebuf with ptrs to the Gigabit class
//   and the ptrs are sorted by m_gbscore
//

bool Msg40::computeGigabits( TopicGroup *tg ) {

	//long long start = gettimeofdayInMilliseconds();

	long niceness = 0;

	Query *q = m_si->m_q;

	// for every sample estimate the number of words so we know how big
	// to make our repeat hash table
	long maxWords = 0;

	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// skip if not visible
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// get it
		Msg20* thisMsg20 = m_msg20[i];
		// must be there! wtf?
		if ( ! thisMsg20 ) { char *xx=NULL;*xx=0; }
		// make sure the summary is not in a foreign language (aac)
		//if (thisMsg20) {
		//    unsigned char sLang;
		//    sLang = thisMsg20->m_r->m_summaryLanguage;
		//    if (language != langUnknown && sLang != language) 
		//          continue;   
		//};
		// . get the sample as provided by XmlDoc::getMsg20Reply() 
		//   calling XmlDoc::getGigabitSample() for each docid in
		//   the search results
		// . the sample is a bunch of text snippets surrounding the
		//   query terms in the doc in the search results
		Msg20Reply *reply = thisMsg20->getReply();
		char *sample = reply->ptr_gigabitSample;
		long  slen   = reply->size_gigabitSample;
		// but if doing metas, get the display content as the sample
		//char  *next = thisMsg20->getDisplayBuf();
		//if ( tg->m_meta[0] && next )
		//	sample = thisMsg20->getNextDisplayBuf(&slen,&next);
		// set parser vars
		char *p    = sample;
		char *pend = sample + slen;
		long sampleWords = 0;
		//long numExcerpts = 0;
		while ( p < pend ) {
			// buffer is \0 separated text snippets
			long plen = gbstrlen       (p);
			sampleWords += countWords( p,plen);
			// advance to next exerpt
			p += plen + 1;
			//if ( debug ) numExcerpts++;
		};
		if (maxWords + sampleWords > 0x08000000) {
			log("gbits: too many words in samples. "
			    "Discarding the remaining samples "
			    "(maxWords=%li)", maxWords);
			char *xx=NULL;*xx=0;
		}
		// the thing we are counting!!!!
		maxWords += sampleWords;
	}

	//
	// hash table for repeated fragment detection
	//
	// make it big enough so there are gaps, so chains are not too long
	long  minBuckets = (long)(maxWords * 1.5);
	if(minBuckets < 512) minBuckets = 512;
	long  numSlots   = 2 * getHighestLitBitValue ( minBuckets ) ;
	// return -1 with g_errno set on error
	HashTableX repeatTable;
	if ( ! repeatTable.set(8,4,numSlots,NULL , 0, false,niceness,"gbbux"))
		return false;

	//
	// only allow one gigabit sample per ip?
	//
	HashTableX iptable;
	if ( tg->m_ipRestrict ) {
		long ns = m_msg3a.m_numDocIds * 4;
		if ( ! iptable.set(4,0,ns,NULL,0,false,niceness,"gbit") )
			return false;
	}

	//
	// space for all vectors for deduping samples that are 80% similar
	//
	SafeBuf vecBuf;
	long  vneed   = m_msg3a.m_numDocIds * SAMPLE_VECTOR_SIZE;
	if ( tg->m_dedupSamplePercent >= 0 && ! vecBuf.reserve ( vneed ) ) 
		return false;


	//
	//
	// . the master hash table for scoring gigabits
	// . each slot is a class "Gigabit"
	//
	//
	HashTableX master;
	long bs = sizeof(Gigabit);
	// key is a 64-bit wordid hash from Words.cpp
	if ( ! master.set ( 8 , bs , 20000,NULL,0,false,niceness,"mgbt") )
		return false;



	//
	// now combine all the pronouns and pronoun phrases into one big hash 
	// table and collect the top 10 topics
	//
	QUICKPOLL(niceness);
	long numDocsProcessed = 0;

	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// skip if not visible
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// get it
		Msg20* thisMsg20 = m_msg20[i];
		// must be there! wtf?
		if ( ! thisMsg20 ) { char *xx=NULL;*xx=0; }
		// make sure the summary is not in a foreign language (aac)
		//if (thisMsg20) {
		//    unsigned char sLang;
		//    sLang = thisMsg20->m_r->m_summaryLanguage;
		//    if(language!=langUnknown && sLang != language) continue; 
		//};
		Msg20Reply *reply = thisMsg20->getReply();
		// skip if from an ip we already did
		if ( tg->m_ipRestrict ) {
			long ipd = ipdom ( reply->m_firstIp );
			// zero is invalid!
			if ( ! ipd ) continue;
			//log("url=%s",thisMsg20->getUrl()); 
			if ( iptable.isInTable(&ipd) ) {
				//log("dup=%s",thisMsg20->getUrl()); 
				continue; 
			}
			// now we also check domain
			Url uu;
			uu.set ( reply->ptr_ubuf, reply->size_ubuf-1);
			// "mid dom" is the "ibm" part of ibm.com or ibm.de
			char *dom  = uu.getMidDomain();
			long  dlen = uu.getMidDomainLen();
			if ( dom && dlen > 0 ) {
				long  h = hash32 ( dom , dlen );
				if ( iptable.isInTable(&h) ) continue; 
				iptable.addKey (&h);
			}
			// add ip
			iptable.addKey ( &ipd );
		}
		// continue; // mdw
		// count it
		numDocsProcessed++;
		// . hash it into the master table
		// . this may alloc st->m_mem, so be sure to free below
		hashSample ( q,
			     &master, 
			     tg ,
			     &vecBuf,
			     thisMsg20,
			     &repeatTable,
			     m_si->m_debugGigabits);
		// ignore errors
		g_errno = 0;
	}

	// debug msg
	/*
	for ( long i = 0 ; i < nt ; i++ ) {
		long score = master->getScoreFromTermNum(i) ;
		if ( ! score ) continue;
		char *ptr  = master->getTermPtr(i) ;
		long len   = master->getTermLen(i);
		char ff[1024];
		if ( len > 1020 ) len = 1020;
		memcpy ( ff , ptr , len );
		ff[len] = '\0';
		// we can have html entities in here now
		//if ( ! is_alnum(ff[0]) ) { char *xx = NULL; *xx = 0; }
		log("%08li %s",score,ff);
	}
	*/

	// how many do we need?
	//long need = tg->m_maxTopics ;

	SafeBuf gigabitPtrBuf;
	long need = master.m_numSlotsUsed * 4;
	if ( ! gigabitPtrBuf.reserve ( need ) ) return false;

	//long  minScore = 0x7fffffff;
	//long  minj = -1;
	long  i ;

	for ( i = 0 ; i < master.m_numSlots ; i++ ) {
		// skip if empty
		if ( master.isEmpty(i) ) continue;
		// get it
		Gigabit *gb = (Gigabit *)master.getValueFromSlot(i);
		// skip term #i from "table" if it has 0 score
		//long score = master.m_scores[i]; // getScoreFromTermNum(i) ;
		//if ( ! score ) continue;

		// skip if 0 score i guess
		//if ( ! gb->m_qrt ) continue;

		// . make it higher the more popular a term is
		// . these are based on a MAXPOP of 10000
		//long mdc = (long)((((double)numDocsProcessed * 3.0 * 
		//		    (double)(gb->m_gbpop&0x7fffffff))+0.5)/
		//		  MAXPOP);
		//if ( mdc < tg->m_minDocCount ) mdc = tg->m_minDocCount;

		// skip if does not meet the min doc count
		if ( gb->m_numPages < tg->m_minDocCount ) continue;

		// set the min of all in our list
		//if ( score < minScore ) { minScore = score; minj = np; }
		// i've seen this become NULL at line 753 on gb1 below for
		// /search?code=mammaXbG&uip=12.41.126.39&n=15&raw=8&q=
		//  manhattan,+ny 
		// so let's try it again and try to find out why maybe
		if ( gb->m_termLen <= 0 ) {
			char *orig = "";
			if ( q ) orig = q->m_orig;
			log (LOG_LOGIC,"query: Got 0 length gigabit. q=%s",
			     orig);
			continue;
		}
		// recalc the score
		//double frac1 = ((MAXPOP-(pops[i]&0x7fffffff))*100.0)/MAXPOP;
		//double frac2 = ((double)count * 100.0) / (double)sampled;
		//score = (long)((frac1 * frac2) / 100.0);
		// we got a winner
		gigabitPtrBuf.pushLong((long)gb);
	}

	//
	//
	// sort the gigabit ptrs
	//
	//
	Gigabit **ptrs = (Gigabit **)gigabitPtrBuf.getBufStart();
	long numPtrs = gigabitPtrBuf.length() / 4;
	gbqsort ( ptrs , numPtrs , sizeof(Gigabit *) , gigabitCmp , 0 );

	// we are done if not deduping
	if ( ! tg->m_dedup ) goto skipdedup;

	// . scan the gigabits
	// . now remove similar terms from the gigabits
	for ( long i = 0 ; i < numPtrs ; i++ ) {
		// get it
		Gigabit *gi = ptrs[i];
		// skip if nuked already
		if ( gi->m_termLen == 0 ) continue;
		// scan down to this score, but not below
		//long minScore = scores[i] - 25;
		// if we get replaced by a longer guy, remember him
		//long replacerj = -1;
		// . a longer term than encapsulates us can eliminate us
		// . or, if we're the longer, we eliminate the shorter
		for ( long j = i + 1 ; j < numPtrs ; j++ ) {
			// get it
			Gigabit *gj = ptrs[j];
			// skip if nuked already
			if ( gj->m_termLen == 0 ) continue;
			// wtf?
			if ( gj->m_termId64 == gi->m_termId64 ) {
				char *xx=NULL; *xx=0; }
			// if page count not the same let it coexist
			if ( gi->m_numPages != gj->m_numPages ) 
				continue;
			// if we are the shorter, nuke the longer guy
			// that contains us because we have a higher score
			// since ptrs are sorted by score then length.
			if ( gi->m_termLen < gj->m_termLen ) {
				// just null term the longer
				char c1 = gi->m_term[gi->m_termLen];
				gi->m_term[gi->m_termLen] = '\0';
				char c2 = gj->m_term[gj->m_termLen];
				gj->m_term[gj->m_termLen] = '\0';
				// if shorter is contained
				char *s;
				s = gb_strcasestr (gj->m_term, gi->m_term);
				// un-null term longer
				gi->m_term[gi->m_termLen] = c1;
				gj->m_term[gj->m_termLen] = c2;
				// even if he's longer, if his score is too
				// low then he cannot nuke us
				// MDW: try doing page count!
				//if ( scores[j] < minScore ) continue;
				// if we were NOT contained by someone below...
				if ( ! s ) continue;

				// just punish, and resort by score later.
				// TODO: ensure cannot go negative!
				//gj->m_numPages -= gi->m_numPages;
				
				// he's gotta be on all of our pages, too
				//if ( ! onSamePages(i,j,slots,heads,pages) )
				//	continue;

				// debug it
				if ( m_si->m_debugGigabits ) {
					SafeBuf msg;
					msg.safePrintf("gbits: gigabit \"");
					msg.safeMemcpy(gi->m_term,
						       gi->m_termLen);
					msg.safePrintf("\"[%.0f] *NUKES0* \"",
						       gi->m_gbscore);
					msg.safeMemcpy(gj->m_term,
						       gj->m_termLen);
					msg.safePrintf("\"[%.0f]",
						       gj->m_gbscore);
					logf(LOG_DEBUG,"%s",msg.getBufStart());
				}

				// shorter gets our score (we need to sort)
				// not yet! let him finish, then replace him!!
				//replacerj = j;
				gj->m_termLen = 0;
				// see if we can nuke other guys at least
				//continue;
			}
			
			else {
				// just null term the longer
				char c1 = gi->m_term[gi->m_termLen];
				gi->m_term[gi->m_termLen] = '\0';
				char c2 = gj->m_term[gj->m_termLen];
				gj->m_term[gj->m_termLen] = '\0';
				// . otherwise, we are the longer
				// . we can nuke any shorter below us, all
				//   scores
				char *s;
				s = gb_strcasestr ( gi->m_term,gj->m_term );
				// un-null term
				gi->m_term[gi->m_termLen] = c1;
				gj->m_term[gj->m_termLen] = c2;
				// keep going if no match
				if ( ! s ) continue;

				// just punish, and resort by score later.
				// TODO: ensure cannot go negative!
				//gj->m_numPages -= gi->m_numPages;

				// debug it
				if ( m_si->m_debugGigabits ) {
					SafeBuf msg;
					msg.safePrintf("gbits: gigabit \"");
					msg.safeMemcpy(gi->m_term,
						       gi->m_termLen);
					msg.safePrintf("\"[%.0f] *NUKES1* \"",
						       gi->m_gbscore);
					msg.safeMemcpy(gj->m_term,
						       gj->m_termLen);
					msg.safePrintf("\"[%.0f]",
						       gj->m_gbscore);
					logf(LOG_DEBUG,"%s",msg.getBufStart());
				}

				// remove him if we contain him
				gj->m_termLen = 0;
			}
		}

		/*
		// if we got replaced by a longer guy, he replaces us
		// and takes our score
		if ( replacerj >= 0 ) {
			// gigabit #i is now gigabit #j
			Gigabit *gj = ptrs[replacerj];

			// debug it
			SafeBuf msg;
			msg.safePrintf("msg40: replacing gigabit \"");
			msg.safeMemcpy(gi->m_term,gi->m_termLen);
			msg.safePrintf("\"[%.0f] *WITH2* \"",gi->m_gbscore);
			msg.safeMemcpy(gj->m_term,gj->m_termLen);
			msg.safePrintf("\"[%.0f]",gj->m_gbscore);
			logf(LOG_DEBUG,msg.getBufStart());

			// make us longer then!
			gi->m_termLen = gj->m_termLen;
			// and nuke him
			gj->m_termLen = 0;

		}
		*/
	}

	// remove common phrases
	for ( long i = 0 ; i < numPtrs ; i++ ) {
		// get it
		Gigabit *gi = ptrs[i];
		// skip if nuked already
		if ( gi->m_termLen == 0 ) continue;
		// shortcut
		char *s = gi->m_term;
		long slen = gi->m_termLen;
		// compare
		if (!strncasecmp(s, "all rights reserved",slen) ||
		    !strncasecmp(s, "rights reserved"    ,slen) ||
		    !strncasecmp(s, "in addition"        ,slen) ||
		    !strncasecmp(s, "for example"        ,slen) ||
		    !strncasecmp(s, "in order"           ,slen) ||
		    !strncasecmp(s, "in fact"            ,slen) ||
		    !strncasecmp(s, "in general"         ,slen) ||
		    !strncasecmp(s, "contact us"         ,slen) ||
		    !strncasecmp(s, "at the same time"   ,slen) ||
		    !strncasecmp(s, "http"               ,slen) ||
		    !strncasecmp(s, "html"               ,slen) ||
		    !strncasecmp(s, "s "                 ,slen) ||
		    !strncasecmp(s, "for more information",slen))
			gi->m_termLen = 0;
	}

	// now after longer topics replaced the shorter topics which they
	// contained, remove the longer topics if they have too many words
	// remove common phrases
	for ( long i = 0 ; i < numPtrs ; i++ ) {
		// get it
		Gigabit *gi = ptrs[i];
		// skip if nuked already
		if ( gi->m_termLen == 0 ) continue;
		// set the words to this gigabit
		char *s = gi->m_term;
		long  slen = gi->m_termLen;
		Words w;
		w.setx ( s , slen , 0 );
		long nw = w.getNumWords();
		// . does it have comma? or other punct besides an apostrophe?
		// . we allow gigabit phrases to incorporate a long stretch
		//   of punct... only before the LAST word in the phrase,
		//   that way our overlap removal still works well.
		bool hasPunct = false;
		for ( long k = 0 ; k < slen ; k++ ) {
			if ( ! is_punct_a(s[k]) ) continue;
			// apostrophe is ok as long as alnum follows
			if ( s[k] == '\'' &&
			     is_alnum_a(s[k+1]) ) continue;
			// . period ok, as long as space or alnum follows
			// . if space follows, then an alnum must follow that
			// . same goes for colon
			QUICKPOLL(niceness);

			// . for now, until we get abbreviations working,
			//   alnum must follow period
			if ( (s[k] == '.' || s[k] == ':' ) &&
			     ( is_alnum_a(s[k+1])  ||
			       // accept single intial before the period, too
			       (s[k+1] ==' ' && is_alnum_a(s[k+2]) 
				&& k>=2 && s[k-2]==' ')))
				continue;
			// comma is ok if surrounded by digits
			if ( (s[k] == ',' &&
			      is_digit(s[k-1]) &&
			      is_digit(s[k+1])   )) continue;
			// percent is ok
			if ( s[k] == '%' ) continue;
			if ( s[k] == '&' ) continue;
			if ( s[k] == '@' ) continue;
			if ( s[k] == '-' ) continue;
			//if ( s[k] == '(' ) continue;
			//if ( s[k] == ')' ) continue;
			hasPunct = true;
			break;
		}
		// keep it if words are under limit
		// and has no commas
		if ( nw <= 2 * tg->m_maxWordsPerTopic -1 && ! hasPunct ) 
			continue;
		// remove it!!!
		gi->m_termLen = 0;
	}

	// resort!! put termLen = 0 at end!
	//gbqsort ( ptrs , numPtrs , sizeof(Gigabit *) , gigabitCmp , 0 );


	// fucking, done, just use the ptrs!!!
	//m_gigabitPtrsValid = true;
	// return ptr to the safebuf
	//return &m_gigabitPtrs;

 skipdedup:

	long stored = 0;
	// now top winning copy winning gigabits into safebuf
	for ( long i = 0 ; i < numPtrs ; i++ ) {
		// get it
		Gigabit *gi = ptrs[i];
		// skip if nuked already
		if ( gi->m_termLen == 0 ) continue;
		// store it
		m_gigabitBuf.safeMemcpy ( gi , sizeof(Gigabit) );
		// stop at 100 i guess
		if ( ++stored >= 100 ) break;
	}

	return true;
}

void hashExcerpt ( Query *q , 
		   // local gigabits table
		   HashTableX *tt , 
		   // the excerpt
		   Words &ww,
		   TopicGroup *tg , 
		   HashTableX *repeatTable ,
		   Msg20 *thisMsg20 ,
		   bool debugGigabits );



// . returns false and sets g_errno on error
// . here's the tricky part
bool hashSample ( Query *q, 
		  HashTableX *master, 
		  TopicGroup *tg ,
		  SafeBuf *vecBuf,
		  Msg20 *thisMsg20 ,
		  HashTableX *repeatTable ,
		  bool debugGigabits ) {

	// numTerms must be less than this
	//if ( q && q->m_numTerms > MAX_QUERY_TERMS ) 
	//	return log("gbits: Too many query terms for "
	//		   "topic generation.");

	Msg20Reply *reply = thisMsg20->getReply();
	// get the ith big sample
	char *bigSampleBuf = reply->ptr_gigabitSample;
	long  bigSampleLen = reply->size_gigabitSample;
	// but if doing metas, get the display content
	//char  *next = thisMsg20->getDisplayBuf();
	// but if doing metas, get the display content
	//if ( tg->m_meta[0] && next)
	//	bigSampleBuf=thisMsg20->getNextDisplayBuf(&bigSampleLen,&next);
	// skip if empty
	if ( bigSampleLen<=0 || ! bigSampleBuf ) return true;
	// the docid
	long long docId = reply->m_docId;

	//long long start = gettimeofdayInMilliseconds();

	//
	// termtable. for hashing all excerpts in a sample
	//
	HashTableX localGigabitTable;
	long bs = sizeof(Gigabit);
	if ( ! localGigabitTable.set(8,bs,20000,NULL,0,false, 0,"gbtrmtbl") ) {
		log("gbits: Had error allocating a table for topic "
		    "generation: %s.",mstrerror(g_errno));
		return true;
	}

	//---> word next to both query terms should not be between by word just
	//next to one....
	//---> weight by query popularity too!

	//log("******** hashing doc *********");


	HashTableX simTable;
	char tmpBuf[41000];
	simTable.set(4,0,8192,tmpBuf,41000,false,0,"simtbl");

	// store our elements into here
	//char vstack[10000];
	//long vneed = nw * 8;
	//SafeBuf vbuf(vstack,10000);
	//if ( ! vbuf.reserve ( vneed ) ) return true;
	SafeBuf vbuf;
	// TODO: make this better
	if ( ! vbuf.reserve ( 10000 * 8 ) ) return true;

	//
	// NOTE: now we have only a sample and excerpts are separated
	// with |'s
	//


	// hash each excerpt
	char *p    = bigSampleBuf;
	// most samples are under 5k, i've seend a 32k sample take 11ms!
	char *pend = p + bigSampleLen;
	while ( p < pend ) {
		// debug
		//log("docId=%lli EXCERPT=%s",docId,p);
		long plen = gbstrlen(p);
		// parse into words
		Words ww;
		ww.setx ( p, plen, 0);// niceness
		// advance to next excerpt
		p += plen + 1;
		// p is only non-NULL if we are doing it the old way
		hashExcerpt ( q, 
			      &localGigabitTable, 
			      ww,
			      tg,
			      repeatTable , 
			      thisMsg20 ,
			      debugGigabits );
		// skip if not deduping
		if ( tg->m_dedupSamplePercent <= 0 ) continue;
		// make a vector out of words
		long long *wids = ww.getWordIds();
		long nw = ww.getNumWords();
		for ( long i = 0 ; i < nw ; i++ ) {
			// make it this
			unsigned long widu = (unsigned long long)(wids[i]);
			// donot allow this! zero is a vector terminator
			if ( widu == 0 ) widu = 1;
			// skip if already added to vector
			if ( simTable.isInTable(&widu) ) continue;
			// store that as a vector component
			if ( ! vbuf.pushLong(widu) ) return false;
			// make sure we do not dedup
			if ( ! simTable.addKey(&widu) ) return false;
		}
	}

	// sort 32-bit word ids from whole sample. niceness = 0
	vbuf.sortLongs(0); 
	// make sure under (128-4) bytes...
	vbuf.truncLen(((long)SAMPLE_VECTOR_SIZE) - 4);
	// make last long a 0
	vbuf.pushLong(0);

	// . compute the fingerprint/similarirtyVector from this table
	//   the same way we do for documents for deduping them at query time
	// . or we could just wait for our dedup algo to kick in... (mdw)
	//   then comment this stuff out ...
	if ( tg->m_dedupSamplePercent > 0 ) {
		// store it there
		//SafeBuf sampleVec;
		//getSampleVector ( bigSample , bigSampleLen , &sampleVec );
		// point to it
		char *v1 = vbuf.getBufStart();
		// get # stored so far
		long numVecs = vecBuf->length() / (long)SAMPLE_VECTOR_SIZE;
		char *v2 = vecBuf->getBufStart();
		// see if our vector is too similar
		for ( long i = 0 ; i < numVecs ; i++ ) {
			char ss;
			ss = g_clusterdb.getSampleSimilarity(v1,v2, 
							  SAMPLE_VECTOR_SIZE);
			v2 += SAMPLE_VECTOR_SIZE;
			// return true if too similar to another sample we did
			if ( ss >= tg->m_dedupSamplePercent ) { // 80 ) {
				localGigabitTable.reset();
				log(LOG_DEBUG,"gbits: removed dup sample.");
				return true;
			}
		}
		// add our vector to the array
		vecBuf->safeMemcpy(v1,(long)SAMPLE_VECTOR_SIZE);
	}

	//log("TOOK %lli ms plen=%li",gettimeofdayInMilliseconds()-start,
	//    bufLen);

	//log("have %li terms in termtable. adding to master.",
	//     tt.getNumTermsUsed());


	// . now hash the entries of this table, tt, into the master
	// . the master contains entries from all the other tables
	long nt = localGigabitTable.getNumSlots();
	//long pop = 0 ;
	for ( long i = 0 ; i < nt ; i++ ) {
		// skip if empty
		if ( localGigabitTable.isEmpty(i) ) continue;
		// get it
		Gigabit *gc = (Gigabit *)localGigabitTable.getDataFromSlot(i);
		// this should be indented
		if ( ! gc->m_gbscore ) continue;//tt.m_scores[i] ) continue;
		//long ii = (long)tt.getTermPtr(i);
		// then divide by that
		//long score =gc->m_scoreFromTermNum;//tt.getScoreFromTermNum(i
		// watch out for 0
		//if ( score <= 0 ) continue;
		// get termid
		long long termId64 = *(long long *)localGigabitTable.getKey(i);
		// . get the bucket
		// . may be or may not be full (score is 0 if empty)
		//long n = master->getTermNum ( tt.getTermId(i) );
		Gigabit *mg = (Gigabit *)master->getValue(&termId64);
		// skip if 0, i've seen this happen before
		//if ( tt.getTermId(i) == 0 ) continue;
		//if ( returnPops ) pop = tt.m_pops[i];
		// set hi bit of "pop" if in unicode
		//if ( isUnicode ) pop |= 0x80000000;
		//else             pop &= 0x7fffffff;
		//pop &= 0x7fffffff;
		Gigabit gbit;
		Gigabit *pg;
		// if already there... inc the score i guess
		if ( mg ) {
			// if already seen it for this docid skip?
			if ( mg->m_lastDocId == docId ) continue;
			// first time for this docid
			mg->m_numPages++;
			mg->m_gbscore += gc->m_gbscore;
			mg->m_lastDocId = docId;
			pg = mg;
		}
		else {
			// . add term to master table
			gbit.m_term     = gc->m_term;
			gbit.m_termLen  = gc->m_termLen;
			gbit.m_numPages = 1;
			gbit.m_gbscore  = gc->m_gbscore;
			gbit.m_lastDocId = docId;
			gbit.m_termId64  = termId64;
			gbit.m_minPop    = gc->m_minPop;
			gbit.m_numWords  = gc->m_numWords;
			// zero out
			memset ( gbit.m_wordIds , 0 , MAX_GIGABIT_WORDS*8);
			// sanity
			if ( gc->m_numWords > MAX_GIGABIT_WORDS ) { 
				char*xx=NULL;*xx=0;}
			memcpy((char *)gbit.m_wordIds,
			       (char *)gc->m_wordIds,
			       gc->m_numWords * 8 );
			if ( ! master->addKey ( &termId64, &gbit ) )
				return false;
			pg = &gbit;
		}
		// debug msg
		//if ( ! g_conf.m_logDebugQuery ) continue;
		if ( ! debugGigabits ) continue;
		char *ww    = pg->m_term;
		long  wwlen = pg->m_termLen;
		char c      = ww[wwlen];
		ww[wwlen]='\0';
		logf(LOG_DEBUG,"gbits: master "
		     "termId=%020llu "
		     "d=%018lli "
		     "score=%7.1f "
		     "cumscore=%7.1f "
		     "pages=%li "
		     "len=%02li term=%s",
		     termId64,
		     docId,
		     gc->m_gbscore, // this time score
		     pg->m_gbscore, // cumulative score
		     pg->m_numPages,
		     wwlen,
		     ww);
		ww[wwlen]=c;
	}

	//log("master has %li terms",master.getNumTermsUsed());
	// clear any error
	if ( g_errno ) {
		log("gbits: Had error getting topic candidates from "
		    "document: "
		    "%s.",mstrerror(g_errno));
		g_errno = 0;
	}
	//mfree ( buf , bufMaxLen , "Msg24" );
	return true;
}

class WordInfo {
public:
	// popularity
	long m_wpop;
	// is query term?
	bool m_isQueryTerm;
	// is common word? (do not let frags end in these words)
	bool m_isCommonWord;
	// the raw QTR scores (aac)
	float m_proxScore;//qtr;
	// a hash for looking up in the popularity dictionary
	//long long dwid64;
	// . from 0 to 100. 100 means not repeated.
	// . set in setRepeatScores() function
	char m_repeatScore;
};

void setRepeatScores ( Words *ww , 
		       WordInfo *wis, 
		       HashTableX *repeatTable ) ;

void hashExcerpt ( Query *q , 
		   HashTableX *localGigabitTable , 
		   Words &words,
		   TopicGroup *tg , 
		   HashTableX *repeatTable ,
		   Msg20 *thisMsg20 , 
		   bool debugGigabits ) {
	// . bring it out
	// . allow one more word per gigabit, then remove gigabits that
	//   are that length. this fixes the problem of having the same
	//   sentence repeated in different documents, which are fairly 
	//   different as a whole, but have the same repeated sentence or
	//   paragraph.
	// . by only adding one, if the next word is a common word then
	//   we would fail to make a larger gigabit, that's why i added
	//   the maxjend code below this.
	long maxWordsPerPhrase  = tg->m_maxWordsPerTopic ;
	if ( tg->m_topicRemoveOverlaps ) maxWordsPerPhrase += 2;
	//char enforceQueryRadius = ! tg->m_meta[0];
	char delimeter          = tg->m_delimeter; // 0 means none (default)
	//char idf                = tg->m_useIdfForTopics;
	// or if no query, no query radius
	//if ( ! q || q->getNumNonFieldedSingletonTerms() == 0 )
	//	enforceQueryRadius = false;
	// . now all the data is in buf/bufLen
	// . parse it up into Words
	// . now XmlDoc::getGigabitVector() calls us and it already has the
	//   Words pased up, so it will use a NULL buf
	long nw = words.getNumWords();
	// don't breech our arrays man
	//if ( nw > 10000 ) nw = 10000;

	Msg20Reply *reply = thisMsg20->getReply();
	unsigned char lang = reply->m_language;

	//
	//
	// additional info for each word
	//
	//
	SafeBuf wibuf;
	long need = nw * sizeof(WordInfo);
	if ( ! wibuf.reserve ( need ) ) {
		log("gigabits: could not allocate local buffer "
		    "(%li bytes required)", need);
		return;
	}
	WordInfo *wis = (WordInfo *)wibuf.getBufStart();

	//
	//
	// . where does each query term occur in the doc?
	// . record each query term's word position into the PosInfo array
	//
	//
	class PosInfo {
	public:
		long m_pos[1000];
		long m_posLen;
		long m_posPtr;
	};
	SafeBuf posBuf;
	long need2 = MAX_QUERY_TERMS * sizeof(PosInfo);
	if ( ! posBuf.reserve ( need2 ) ) {
		log("gigabits: could not allocate 2 local buffer "
		    "(%li bytes required)", need2);
		return;
	}
	PosInfo *pis = (PosInfo *)posBuf.getBufStart();
	for (long i = 0; i < q->m_numTerms ; i++) {
		pis[i].m_posLen = 0; 
		pis[i].m_posPtr = 0; 
	}



	// start parsing at word #0 in the excerpt
	long i  = 0;
	// skip punct at beginning of excerpt
	if ( i < nw && words.isPunct(i) ) i++;

	// this is aac's thing...
	if ( i < nw ) wis[i].m_proxScore = 0.0;

	// . now we keep a hash table to zero out repeated fragments
	// . it uses a sliding window of 5 words
	// . it stores the hash of those 5 words in the hash table
	// . if sees how many 5-word matches it gets in a row
	// . the more matches it gets, the more it demotes the word scores
	//   in that span of 5 words
	// . these are stored in the weights class
	// . a repeatScore of 0 means to demote it out completely, 100 means
	//   it is not repeated at all
	// . multiply the final gigabit score by the repeatScore/100.
	// . this function sets WordInfo::m_repeatScore
	// . each word in the excerpt is 1-1 with the WordInfos
	setRepeatScores ( &words , wis, repeatTable );

	// record the positions of all query words
	char **wp   = words.m_words;
	long  *wlen = words.m_wordLens;
	long long *wids = words.getWordIds();

	// loop over the words in our EXCERPT
	for ( ; i < nw ; i++ ) {
		// get associated WordInfo class
		WordInfo *wi = &wis[i];
		// aac's thing
	        wi->m_proxScore = 0.0;
		// skip if not indexable
 		if ( ! wids[i] ) continue;
		// skip if repeated too much according to setRepeatScores()
		if ( wi->m_repeatScore <= 20 ) continue;
		// reset popularity
		//if   ( idf ) wi->m_wpop = -1;
		// assume all same if not using idf
		//else         wi->m_wpop =  1; 
		// assume this word is not in the query
		wi->m_isQueryTerm = 0;
		// reset
		wi->m_wpop = -1;
		// debug point
		//if ( strncmp( wp[i],"This",4) == 0 )
		//	log("hey");
		// store the id
		//wi->m_dwid64 = hash64d(wp[i], wlen[i] );
		// . is it a common word?
		// . it is if it is just one letter
		// . what about X-windows coming up for a 'windows' query?
		//   or e-mail coming up for a query?
		// . METALINCS likes to have 1 digit topics
		if ( wlen[i] <= 1 && is_lower_a(wp[i][0]) ) 
			wi->m_isCommonWord = 1;
		// 2004 is common here but if it makes it in, don't remove it
		// in the top topics list... no. loses 'atari 2600' then!
		//else if ( is_digit(ww.getWord(i)[0]) ) 
		//	icw[i] = 1;
		//#ifndef _METALINCS_
		else wi->m_isCommonWord = isCommonWord ( wids[i] );
		//#else
		// always allow gigabits that start with numbers for metalincs
		//else if ( ! is_digit(wp[i][0])) 
		//	wi->m_isCommonWord = isCommonWord ( (long)wids[i] );
		//else                            
		//	wi->m_isCommonWord = 0;
		//#endif
		// debug msg
		/*
		char *s    = ww.getWord(i);
		long  slen = ww.getWordLen(i);
		char  c    = s[slen];
		s[slen]='\0';
		log("icw=%li %s",icw[i],s);
		s[slen]=c;
		*/
		// is it a query term? if so, record its word # in "pos" arry
		long nt = q->m_numTerms;
		for ( long j = 0 ; j < nt ; j++ ) {
			// get query term #j
			QueryTerm *qt = &q->m_qterms[j];
			// does word #i match query word id #j? skip if not.
			if ( wids[i] != qt->m_hash64d ) continue;
			// get vector for query word #j
			PosInfo *pi = &pis[j];
			// skip if already have 1000 occurences of this term
			if ( pi->m_posLen >= 1000  ) continue;
			// add this query term # into our m_pos vector
			pi->m_pos[pi->m_posLen] = i;
			pi->m_posLen++;
			// mark this word so if a phrase only has
			// all query terms we do not hash it
			wi->m_isQueryTerm = 1;
			break;
		}
	}

	//
	//
	// done scanning words in excerpt
	//
	//

	// max score -- ONE max scoring hits per doc
	//long maxScore = nqi * MAX_SCORE_MULTIPLIER;
	// this happens when generating the gigabit vector for a single doc
	// so don't hamper it to such a small ceiling
	//if ( nqi == 0 ) maxScore = ALT_MAX_SCORE;

	// reset cursor to word #0 in excerpt again
	i = 0;
	// skip initial punct and spaces
	if ( i < nw && words.isPunct(i) ) i++;
	// score each word based on distance to query terms
	//float score;

	//
	//
	// loop through all the words again and set WordInfo::m_proxScore
	// and WordInfo::m_wpop
	//
	//
	for ( ; i < nw ; i++ ) {
		// debug point
		//if ( strncasecmp( wp[i],"Microsoft",9) == 0 )
		//	log("hey");
		// do we have pre-supplied words and scores from XmlDoc.cpp?
		//if ( wids ) {
		// skip if not indexable
		if ( ! wids[i] ) continue;
		// shorcut
		WordInfo *wi = &wis[i];
		// skip if in a repeat chunk of doc
		if ( wi->m_repeatScore <= 20 ) continue;
		// protect against misspelled html entities (aac) 
		if ( (wp[i][-1] == '&' && is_alnum_a(wp[i][0])) ||
		     (wp[i][0] == '&' && is_alnum_a(wp[i][1]))  )	
			continue;
		// no more one or two letter gigabits (aac)
		if ( wlen[i] < 3 && (! is_digit(wp[i][0])) ) continue;
		//continue; //mdw
		// if we had a delimeter, previous word must have it
		// or be the first punct word
		if ( delimeter && i >= 2 && ! words.hasChar(i-1,delimeter) ) 
			continue;
		// skip if a query term, it's ineligible
		//if ( ww.getWordLen(i) == 0 ) continue;
		// if query is NULL, assume we are restricting to meta tags
		// and query is not necessary
		//if   ( enforceQueryRadius ) score = 0;
		//else                        score = ALT_START_SCORE;
		long j ;

		// number of matches
		long nm = 0; 

		// how close is the word to the query terms? base
		// the proxScore on that.
		float proxScore = 0.0;

		// loop over the # of matchable words in the query
		for ( j = 0 ; j < q->m_numTerms ; j++ ) {
			// get the vector that has what word #'s in the
			// excerpt that query word #j matches
			PosInfo *pe = &pis[j];
			// skip query word #j if does not match ANY words
			// in this excerpt...
			if ( pe->m_posLen <= 0 ) continue;
			// get the jth quer term we match then
			QueryTerm *qt = &q->m_qterms[j];
			// zero for this term
			float score = 0.0;
			// get distance in words
			//long d1 = i - pos[ 1000 * j + posPtr[j] ] ;
			// . posPtr is like a cursor into our m_pos array
			//   that has the word #'s that this query word
			//   matches in the excerpt
			// . "d1" is distance in words from word #i to
			//   the next closest query term
			long d1 = i - pe->m_pos[pe->m_posPtr];
			// if word #i is BEFORE this matching word in the
			// excerpt, flip the sign
			if ( d1 < 0   ) d1 = d1 * -1;
			//
			// if the matching word is the last that occurence
			// of that word...
			//
			if ( pe->m_posPtr + 1 >= pe->m_posLen ) {
				// if too far apart, go to next query term
				if (d1 >= QTR_ZONE_3) continue;
				
				if ( wi->m_isQueryTerm ||
				     wi->m_isCommonWord ||
				     wlen[i] <= 3) {
				    // common word, query terms, short words
				    // are all second class citizens when it
				    // comes to scoring: they get a small
				    // bonus, to ensure that they are
				    // considered in the next stage, but do not
				    // benefit from QPOP and multiple hit
				    // bonuses (aac)
					//score = QTR_BONUS_CW;
					//proxScore += score;
				    continue; 
				};
				// QTR_ZONE_0 is the tighest radius
				if (d1 < QTR_ZONE_0) 
				    score = QTR_BONUS_0;
				else if (d1 < QTR_ZONE_1) 
				    score = QTR_BONUS_1;
				else if (d1 < QTR_ZONE_2) 
				    score = QTR_BONUS_2;
				else                         
				    score = QTR_BONUS_3;
				// increment the # of matches
				nm++;
				// multiplier based on query word popularity
				score *= qt->m_popWeight;//qpops[j];
				proxScore += score;
				continue;
			}
			//
			// look at the following match
			//
			//long d2 = pos[ 1000 * j + posPtr[j] + 1 ] - i ;
			// look at the next occurence of query term #j
			// in the excerpt and get dist from us to it
			long d2 = pe->m_pos[pe->m_posPtr + 1] - i;
			// make it positive
			if ( d2 < 0  ) d2 = d2 * -1;
			// if we are closer to the current matching word
			// then set score for that...
			if ( d1 <= d2 ) {
				// if      ( d1 >=20 ) continue;
				// if      ( d1 <  4 ) score += 1000;
				// else if ( d1 <  8 ) score += 800;
				// else if ( d1 < 12 ) score += 500;
				// else                score += 200;
				// nm++;
				// score *= qpops[j];
				// continue;
				if (d1 >= QTR_ZONE_3) continue;
				if ( wi->m_isQueryTerm || 
				     wi->m_isCommonWord || 
				    wlen[i] <= 3) {
				    // common word, query terms, short words
				    // are all second class citizens when it
				    // comes to scoring: they get a small
				    // bonus, to ensure that they are
				    // considered in the next stage, but do not
				    // benefit from QPOP and multiple hit
				    // bonuses (aac)
					//score = QTR_BONUS_CW;
				    continue; 
				};
				if (d1 < QTR_ZONE_0) 
				    score = QTR_BONUS_0;
				else if (d1 < QTR_ZONE_1) 
				    score = QTR_BONUS_1;
				else if (d1 < QTR_ZONE_2) 
				    score = QTR_BONUS_2;
				else                         
				    score = QTR_BONUS_3;
				nm++;
				score *= qt->m_popWeight;//qpops[j];
				proxScore += score;
				continue;
			}

			//
			//
			// otherwise, we are closer to the next occurence!!
			// be sure to ince its posPtr cursor then
			//
			//

			// i think it is safe to increment this here now
			// because we are closer to the word position
			// m_pos[m_posPtr+1] then m_pos[m_posPtr].
			pe->m_posPtr++;


			// if radius is too big... no score increase
			if (d2 >= QTR_ZONE_3) 
				continue; 
			
			if ( wi->m_isQueryTerm || 
			     wi->m_isCommonWord || 
			     wlen[i] <= 3) {
			    // common word, query terms, short words
			    // are all second class citizens when it
			    // comes to scoring: they get a small
			    // bonus, to ensure that they are
			    // considered in the next stage, but do not
			    // benefit from QPOP and multiple hit
			    // bonuses (aac)
				//score = QTR_BONUS_CW;
				//proxScore += score;
			    continue; 
			}

			// give out some score bonuses
			if (d2 < QTR_ZONE_0) score = QTR_BONUS_0;
			else if (d2 < QTR_ZONE_1) score = QTR_BONUS_1;
			else if (d2 < QTR_ZONE_2) score = QTR_BONUS_2;
			else                      score = QTR_BONUS_3;

			// and match count.. why this?
			nm++;

			// multiply by query term pop weight that we are
			// closest too
			score *= qt->m_popWeight;//qpops[j];
			proxScore += score;
		}

		// skip if too far away from all query terms
		if ( proxScore <= 0 ) continue;

		// give a boost for multiple hits 
		// the more terms in range, the bigger the boost...
		if ( nm > 1 ) {
			//log("nm=%li",nm);
			// hmmm...  try to rely on more pages mentioning it!
			//score += MULTIPLE_HIT_BOOST * nm;
		};

		// . save the raw QTR score (aac)
		// . this is based on how close the word is to all query
		//   terms...
		wi->m_proxScore = proxScore;

		// no longer count closeness to query terms for score,
		// just use # times topic is in doc(s) and popularity
		//score = 1000;

		// set pop if it is -1
		if ( wi->m_wpop == -1 ) { // pops[i] == -1 ) {
			wi->m_wpop = g_speller.
				getPhrasePopularity( wp[i],wids[i], true,lang);
		       // decrease popularity by half if 
		       // capitalized so Jack does not have 
		       // same pop as "jack"
		       if ( is_upper_a (wp[i][0]) ) wi->m_wpop >>= 1;
		       if ( wi->m_wpop == 0 ) wi->m_wpop = 1;
		}

		// log that
		if ( ! debugGigabits ) continue;
		SafeBuf msg;
		msg.safePrintf("gbits: wordpos=%3li "
			       "repeatscore=%3li "
			       "wordproxscore=%6.1f word=",
			       i,
			       (long)wi->m_repeatScore,
			       proxScore);
		msg.safeMemcpy(wp[i],wlen[i]);
		msg.pushChar(0);
		logf(LOG_DEBUG,"%s",msg.getBufStart());
		
	}


	//long mm = 0;
	// reset word ptr again
	i = 0;
	// skip initial punct again
	if ( i < nw && words.isPunct(i) ) i++;

	long wikiEnd = -1;

	//
	//
	// scan words again and add GIGABITS to term table "localGigabitTable"
	//
	//

	for ( ; i < nw ; i++ ) {
		// shortcut
		WordInfo *wi = &wis[i];
		// must start with a QTR-scoring word (aac)
		if ( wi->m_proxScore <= 0.0 ) continue;

		// do not split a phrase like "next generation" to just
		// get the gigabit "generation" by itself.
		// should also fix "search engine" from being split into
		// "search" and "engine"
		if ( i <= wikiEnd ) continue;

		//if ( strncmp(words.m_words[i],"point",5) == 0 )
		//	log("hey");

		// in a wikipedia title?
		long numWiki = g_wiki.getNumWordsInWikiPhrase ( i,&words );
		wikiEnd = i + numWiki;

		// point to the string of the word
		char *ww = wp[i];
		long  wwlen = wlen[i];
		//long  ss;
		//float ss;
		if ( wi->m_isCommonWord ) {
			// . skip this and all phrases if we're "to"
			// . avoid "to use..." "to do..." "to make..." annoying
			// . "to" has score 1, "and" has score 2, "of" is 3,
			// . "the" is 4, "this" is 5
			if ( wi->m_isCommonWord <= 5 ) continue;
			// cannot start with any common word,unless capitalized
			if ( is_lower_a(wp[i][0]) ) continue;
		}
		// if a hyphen is immediately before us, we cannot start
		// a phrase... fu-ture, preven-tion
		if ( i > 0 && wp[i][-1]=='-' ) continue;
		// same for colon
		if ( i > 0 && wp[i][-1]==':' ) continue;
		// . if a "'s " is before us, we cannot start either
		// . "valentine's day cards"
		if ( i >= 3 && 
		     wp[i][-3]=='\'' && 
		     wp[i][-2  ]=='s' &&
		     is_wspace_a(wp[i][-1]) ) continue;
		// or if our first char is a digit and a "digit," is before us
		// because we don't want to break numbers with commas in them
		if ( is_digit(wp[i][0]) && i >= 2 && wp[i][-1]==',' && 
		     is_digit(wp[i][-2]) ) continue;
		// set initial popularity
		//float gigabitPop = 1.0;
		long minPop = 0x7fffffff;
		//if ( wi->m_wpop > 0) pop = ((float) wi->m_wpop) / MAXPOP;
		//else pop = 1.0 / MAXPOP;

		//
		//
		// set initial score and bonus resuming from above for loop
		//
		//
		//float wordProxSum = 0;//wi->m_proxScore;
		float wordProxMax = 0;

		float bonus = 0;
		unsigned long long  ph64 = 0;//wids[i]; // hash value
		// if first letter is upper case, double the score
		//if ( is_upper_a (ww.getWord(i)[0]) ) score <<= 1;

		// . loop through all phrases that start with this word
		// . up to 6 real words per phrase
		// . 'j' counts our 'words' which counts a $ of puncts as word
		long jend    = i + maxWordsPerPhrase * 2; // 12;
		long maxjend = jend ;
		if ( tg->m_topicRemoveOverlaps ) maxjend += 8;
		if ( jend    > nw ) jend    = nw;
		if ( maxjend > nw ) maxjend = nw;

		long count = 0;
		long nqc   = 0; // # common/query words in our phrase
		long nhw   = 0; // # of "hot words" (contribute to score)

		//if ( wlen[i] == 8 && strncmp(wp[i],"Practice",8) == 0 )
		//	log("hey");

		long jWikiEnd = -1;
		
		for ( long j = i ; j < jend ; j++ ) {
			// skip if not indexable
			if ( ! wids[j] ) continue;
			// . do not split a wiki title
			//if ( j < wikiEnd-1 ) continue;

			// . j starts at i, so we can pick up the wikiphrase
			//   from i
			// . so if "search" is i and is @ 146 and "engine" @ 
			//   148 then jWikiEnd will be 148, and j needs to be 
			//   able to end on that so use wikiEnd-1
			if ( j < jWikiEnd - 1) continue;
			long njw = g_wiki.getNumWordsInWikiPhrase ( j,&words );
			jWikiEnd = j + njw;

			// get word info
			WordInfo *wj = &wis[j];
			// skip if in a repeated fragment
			if ( wj->m_repeatScore <= 20 ) continue;
			// no ending in ing on capitalized
			if ( wlen[j] > 3 &&
			     wp[j][wlen[j]-1   ]=='g' &&
			     wp[j][wlen[j]-2  ]=='n' &&
			     wp[j][wlen[j]-3]=='i' &&
			     is_lower_a(wp[j][0]) )
				continue;
			if (j == i) {
			    if ( wj->m_isCommonWord ||  wlen[j] < 3) 
				    bonus -= FWC_PENALTY;  
			    // if word is 4 letters or more and ends in ed, do
			    // not allow to be its own gigabit
			    if ( wlen[j] > 3 &&
				 wp[j][wlen[j]-1 ]=='d' &&
				 wp[j][wlen[j]-2]=='e' )
				    continue;
			    // no more "com" gigabits, please! (aac)
			    if ( wlen[j] == 3 &&
				 wp[j][0       ]=='c' &&
				 wp[j][1 ]=='o' &&
				 wp[j][2]=='m') continue;
			};
			// let's generalize even more! do not allow common
			// single words as gigabits, with 250+ pop
			//if ( pop > 100 && j == i && is_lower(wp[j][0]) ) 
			//continue;
			// the above assumes a MAX_POP of 10k (sanity check)
			//if ( MAXPOP != 10000 ) { char *xx = NULL; *xx = 0; }
			// are we passed the first word in the phrase?
			if ( j > i ) {
				// advance phrase length
				wwlen += wlen[j-1] + wlen[j];
				// . cut phrase short if too much punct between
				//   the current word, j, and the last one, j-2
				// . but allow for abbreviations or initials
				//   of single letters, like 'harry s. truman'.
				//   we do not want to break before 's.'
				// . because the phrase "s. doesn't stand for 
				//   anything." was unable to form. we only
				//   got "s." and "doesn't stand for anything."
				//   as possible gigabit candidates.
				//if ( wlen[j-1] > 1 ) {
				//	if ( wlen[j-1]    != 2   ) break;
				//	if ( wp  [j-1][0] != '.' ) break;
				//	if ( wlen[j-2]    >  1   ) break;
				//}
				// . we now allow most punct since it is 
				//   filtered out above w/ hasPunct variable
				// . this a little more than doubles the 
				//   processing overhead going from 1 to 3
				// . going from 1 to 2 we see that we take 60ms
				//   instead of 50ms *when removing overlaps*
				// . at 1 we take about 48/45ms, not much
				//   different when removing overlaps
				// . increasing this totally wipes out our
				//   overlap problem, but it is very expensive,
				//   so now i just halt after jumping one big
				//   string of punct below, and filter out 
				//   those gigabits above with hasPunct.
				// . i'd really like to NOT have this here
				//   becase we get much better gigabits, but
				//   we need it as a speed saver...
				if (wlen[j-1]>tg->m_topicMaxPunctLen) break;
				// no phrasing across commas, etc.
				/*
				if ( wlen[j-1] == 2 ) {
					// only allow "  " or ": " or ". "
					if ( wp[j-1][1]!=' ' ) break;
					if ( wp[j-1][0]!=' ' &&
					     wp[j-1][0]!=':' &&
					     wp[j-1][0]!='\'' && // beatles'
					     // allow commas here, but we
					     // remove any gigabits with commas
					     // because we just use them to
					     // cancel out bad gigabits.
					     wp[j-1][0]!=',' &&
					     wp[j-1][0]!='.'  ) break;
					// . TODO: add in sgt. col. so that
					//   stuff can be in a gigabit
					// . only allow ". " if prev word was 
					//   abbreviation.
					if ( wp[j-1][0]=='.' &&
					     j >= 2 &&
					     wlen[j-2] > 3) break; // != 1
				}
				*/
				// or if we just skipped the delimeter,
				// we are not allowed to phrase across that
				// if one was provided
				if ( delimeter &&words.hasChar(j-1,delimeter)) 
					break;
				// make sure we could phrase across last word
				//if ( wlen[j-1] > 1 && 
				//   bits.getPunctuationBits(wp[j-1],wlen[j-1])
				//   == 0 ) break;
			}

			//
			// accumulate the phrase's hash AND pop
			//
			ph64 = hash64 ( ph64 , wids[j] );

			// set pop if it is -1
			if ( wj->m_wpop == -1 ) { // pops[i] == -1 ) {
				wj->m_wpop = g_speller.
				getPhrasePopularity( wp[j],wids[j], true,lang);
				// decrease popularity by half if 
				// capitalized so Jack does not have 
				// same pop as "jack"
				if ( is_upper_a (wp[j][0]) ) wj->m_wpop >>= 1;
				if ( wj->m_wpop == 0 ) wj->m_wpop = 1;
			}

			// adjust popularity
			//gigabitPop = (gigabitPop* wj->m_wpop)/MAXPOP;
			// watch our for overflow
			//if ( gigabitPop <= 0 ) gigabitPop = 1.0/MAXPOP;
			if ( wj->m_wpop < minPop )
				minPop = wj->m_wpop;
			// get lowest of scores
			//if(scores && scores[j] > mm )	mm = scores[j];


			// accumulate wordproxscores
			//wordProxSum += wj->m_proxScore;
			if ( wj->m_proxScore > wordProxMax )
				wordProxMax = wj->m_proxScore;



			// keep track of words
			count++;
			if ( wj->m_isQueryTerm || wj->m_isCommonWord ) {
			    nqc++; // increment number of query/commoners
			}
			// do not count 1.0 cuz those are the query terms!
			else if ( wj->m_proxScore > 1.0) {
				// increment "hot word" counter
				nhw++; 
			};
			// keep phrasing until next punct word is delimeter
			// or the end
			if ( delimeter ) {
				// if we end on a punct word, then hash
				// our phrase, otherwise, only hash it if
				// the next word has the delimeter
				if ( j+2<jend &&!words.hasChar(j+1,delimeter))
					continue;
			}
			// otherwise, ensure phrase is not ALL query terms
			else {
				// if phrase is all commoners  & query skip it
				if ( nqc == count ) {
					// debug
					//char saveChar = ww[wwlen];
					//ww[wwlen] = '\0';
					//log("gbits: "
					//"phrase is all QT or CW; skipping" 
					//" phrase %s", ww);
					//ww[wwlen] = saveChar;
					continue;
				};
			}
			// . skip if we're common, pair across common words
			// . BUT it is common to end a meta from tag in ".com"
			//   so we should not count that one as common
			if ( wj->m_isCommonWord ) {
				// allow for more words only for purposes
				// of fixing the ABCD and BCDE overlap bug 
				// without having to raise jend for all cases
				if ( jend < maxjend ) jend++; 
				continue;
			}
			// do not stop if - . or @ follows us right b4 alnum
			if ( j+1 < nw && is_alnum_a(wp[j+1][1]) ) {
			if ( wp[j+1][0]=='-' ) continue;
			if ( wp[j+1][0]=='.' ) continue;
			if ( wp[j+1][0]=='\'') continue;
			if ( wp[j+1][0]=='@' ) continue;
			// . do not split phrases between capitalized words
			// . this should fix the Costa Rica, Costa Blah bug
			// . it may decrease score of Belkin for query 
			//   'Belkin Omni Cube' but that's ok because if 
			//   Belkin is important it will be used independently.
			if ( is_upper_a(wp[j][0]) &&
			     j + 2 < nw &&
			     wp[j+1][0]==' ' &&
			     is_upper_a(wp[j+2][0]) &&
			     wlen[j+1] == 1 &&
			     tg->m_maxWordsPerTopic > 1 ) 
				continue;
			}
			// do not mix caps
			if ( is_upper_a(wp[i][0]) != is_upper_a(wp[j][0]) )
			     continue;
			// . do not stop on a single capital letter
			// . so we don't stop on "George W->" (george w. bush)
			// . i added the " && j > i" so METALINCS can have
			//   single digit gigabits
			if ( wlen[j] == 1 && j > i ) continue;
			// . do not split after Mr. or St. or Ms. or Mt. ...
			// . fixes 'st. valentines day'
			if ( wlen[j] == 2 && is_upper_a(wp[j][0]) &&
			     wp[j][2]=='.' ) continue;
			// sgt. or col.
			if ( wlen[j] == 3 && wp[j][3]=='.' ){
				if ( to_lower_a(wp[j][0       ])=='s' &&
				     to_lower_a(wp[j][1 ])=='g' &&
				     to_lower_a(wp[j][2])=='t' ) continue;
				if ( to_lower_a(wp[j][0       ])=='c' &&
				     to_lower_a(wp[j][1 ])=='o' &&
				     to_lower_a(wp[j][2])=='l' ) continue;
				if ( to_lower_a(wp[j][0       ])=='m' &&
				     to_lower_a(wp[j][1 ])=='r' &&
				     to_lower_a(wp[j][2])=='s' ) continue;
			}
			// . do not split commas in numbers
			// . like 1,000,000,000
			if ( j >= 2 && 
			              wp[j][-1 ]==',' && 
			     is_digit(wp[j][-2])     &&
			     wp[j][wlen[j]]==',' && 
			     is_digit(wp[j][wlen[j]+1]))
				continue;
			/*
			if       ( pop < 1  ) ;
			else if  ( pop < 2  ) ss = (score * 90) / 100;
			else if  ( pop < 5  ) ss = (score * 85) / 100;
			else if  ( pop < 10 ) ss = (score * 80) / 100;
			else if  ( pop < 20 ) ss = (score * 75) / 100;
			else if  ( pop < 30 ) ss = (score * 70) / 100;
			else if  ( pop < 40 ) ss = (score * 65) / 100;
			else if  ( pop < 50 ) ss = (score * 60) / 100;
			else                  ss = (score * 40) / 100;
			*/
			//if ( tt->getScoreFromTermId((long long)h) > 0 )
			//	continue;
			// debug msg
			//char c     = ww[wwlen];
			//ww[wwlen]='\0';
			//fprintf(stderr,"tid=%lu score=%li pop=%li len=%li "
			// "repeat=%li term=%s\n",h,ss,pop,wwlen,
			//	repeatScores[i],ww);
			//ww[wwlen]=c;
			// include any ending or starting ( or )
			if ( i > 0 && ww[-1] == '(' ) { 
				// ensure we got a ')' somwhere before adding (
				for ( long r = 0 ; r <= wwlen ; r++ )
					if ( ww[r]==')' ) {
						ww--; wwlen++; break; }
			}
			if ( i < nw && ww[wwlen] == ')' ) { 
				// we need a '(' somewhere before adding the )
				for ( long r = 0 ; r <= wwlen ; r++ )
					if ( ww[r]=='(' ) {
						wwlen++; break; }
			}
			// now remove ('s if begin AND end in them
			if ( ww[0] == '(' && ww[wwlen-1] == ')' ) {
				ww++; wwlen -= 2; }

			// base his score on this
			float wordScore = wj->m_proxScore;

			// now double score if capitalized, we need more
			// proper nouns for topic clustering to work better,
			// but it doesn't count if start of a sentence, so 
			// there must be some alnum word right before it.
			if ( is_upper_a(ww[0]) && 
			     wwlen>=2 && 
			     j >= 2 && // do not breach!
			     is_alnum_a(ww[-2])) 
				wordScore *= 2; // <<= 1; // 1;
			// adjust the gigabit score using the new scores array
			//if ( scores && mm != NORM_WORD_SCORE ) 
			//	ss = (ss * mm) / NORM_WORD_SCORE;
			// adjust the gigabit score using the new scores array
			//if ( scores && mm != NORM_WORD_SCORE )
			//	ss = (ss * mm) / NORM_WORD_SCORE;
			// only count the highest scoring guy once per page
			//long tn = tt->getTermNum((long long)h);
			//maxScore = ss;
			//if ( tn >= 0 ) {
			//	long sc = tt->getScoreFromTermNum(tn);
			//	if ( sc > maxScore ) maxScore = sc;
			//}
			// . add it
			// . now store the popularity, too, so we can display
			//   it for the winning gigabits
			//if ( ! tt->addTerm ((long long)h,ss,maxScore,false,
			//		    ww,wwlen,tn,NULL,pop) ) 
			// . weight score by pop
			// . lets try weighting more popular phrases more!
			//ss = score;
			// i guess average the > 0 prox scores
			//if ( nhw > 0) wordScore /= nhw;

			// i think a common word penalty is this bonus?
			// it is accumulate, so we can add it down here
			//wordProxSum += bonus;

			// penalty if not enough hot words
			//if ( nhw < 3 )
			//	wordScore -= 100;

			// accumulate proxScores of each word
			// involved in the gigbit, including the
			// FIRST word!
			//wordScoreSum += wordScore;

			float boost;
			if      (minPop < POP_ZONE_0) boost = POP_BOOST_0;
			else if (minPop < POP_ZONE_1) boost = POP_BOOST_1;
			else if (minPop < POP_ZONE_2) boost = POP_BOOST_2;
			else if (minPop < POP_ZONE_3) boost = POP_BOOST_3;
			else                          boost = POP_BOOST_4;
			// apply the boost
			//float popModScore = wordProxSum * boost;
			float popModScore = wordProxMax * boost;
			if ( popModScore <= 0 ) popModScore = 1;

			// average among the words with positive prox scores
			//if ( nhw > 0 ) popModScore /= nhw;

			// store it
			//long ipop = (long)(pop * MAXPOP);

			//
			// ADD A GIGABIT CANDIDATE
			//
			Gigabit gc;
			gc.m_term    = ww;
			gc.m_termLen = wwlen;
			gc.m_gbscore = popModScore;
			gc.m_minPop = minPop;

			// how many words in the gigabit?
			long ngw = (j - i)/2 + 1;
			gc.m_numWords = ngw;

			// breach check. go to next gigabit beginning word?
			if ( ngw > MAX_GIGABIT_WORDS ) break;

			// record each word!
			long wcount = 0;
			for ( long k = i ; k <= j ; k++ ) {
				if ( ! wids[k] ) continue;
				gc.m_wordIds[wcount] = wids[k];
				wcount++;
				if ( wcount >= MAX_GIGABIT_WORDS ) break;
				gc.m_wordIds[wcount] = 0LL;
			}
			

			if ( ! localGigabitTable->addKey ( &ph64 , &gc ) ) {
				log("gbits: No memory to grow table.");
				return;
			}

			// debug it
			if ( debugGigabits ) {
				SafeBuf msg;
				msg.safePrintf("gbits: adding gigabit "
					       "d=%018llu "
					       "termId=%020llu "
					       "popModScore=%7.1f "
					       //"wordProxSum=%7.1f "
					       "wordProxMax=%7.1f "
					       "nhw=%2li "
					       "minWordPopBoost=%2.1f "
					       "minWordPop=%5li "
					       "term=\"",
					       reply->m_docId,
					       ph64,
					       popModScore,
					       wordProxMax,
					       nhw,
					       boost,
					       minPop);
				msg.safeMemcpy(gc.m_term,gc.m_termLen);
				msg.safePrintf("\"");
				logf(LOG_DEBUG,"%s",msg.getBufStart());
			}


			// stop after indexing a word after a long string of
			// punct, this is the overlap bug fix without taking
			// a performance hit. hasPunct above will remove it.
			if ( j > i && wlen[j-1] > 2 ) break;
		}
	}
	// report error
	if ( g_errno )
		log("gbits: Had error getting topic candidates from "
		    "document: %s.",mstrerror(g_errno));
	// clear any error
	g_errno = 0;
}

// taken from Weights.cpp's set3() function
void setRepeatScores ( Words *words , 
		       WordInfo *wis, 
		       HashTableX *repeatTable ) {

	long nw = words->getNumWords();

	// if no words, nothing to do
	if ( nw == 0 ) return;

	//char      *ptr      = repeatTable;
	//long       numSlots = repeatTableNumSlots;
	//long long *hashes   = (long long *)ptr; ptr += numSlots * 8;
	//long      *vals     = (long      *)ptr; ptr += numSlots * 4;

	long long   ringWids [ 5 ];
	long        ringPos  [ 5 ];
	long        ringi = 0;
	long        count = 0;
	long long   h     = 0;

	//long numSlots = repeatTable->getNumSlots();

	// make the mask
	//unsigned long mask = numSlots - 1;

	// clear ring of hashes
	memset ( ringWids , 0 , 5 * sizeof(long long) );

	// for sanity check
	//long lastStart = -1;

	// count how many 5-word sequences we match in a row
	long matched    = 0;
	long matchStart = -1;

	// reset
	for ( long i = 0 ; i < nw ; i++ ) 
		wis[i].m_repeatScore = 100;


	// return until we fix the infinite loop bug
	//return;

	long long *wids = words->getWordIds();

	// . hash EVERY 5-word sequence in the document
	// . if we get a match look and see what sequences it matches
	// . we allow multiple instances of the same hash to be stored in
	//   the hash table, so keep checking for a matching hash until you
	//   chain to a 0 hash, indicating the chain ends
	// . check each matching hash to see if more than 5 words match
	// . get the max words that matched from all of the candidates
	// . demote the word and phrase weights based on the total/max
	//   number of words matching
	for ( long i = 0 ; i < nw ; i++ ) {
		// skip if not alnum word
		if ( ! wids[i] ) continue;
		// reset
		//repeatScores[i] = 100;
		// add new to the 5 word hash
		h ^= wids[i];
		// . remove old from 5 word hash before adding new...
		// . initial ring wids are 0, so should be benign at startup
		h ^= ringWids[ringi];
		// add to ring
		ringWids[ringi] = wids[i];
		// save our position
		ringPos[ringi] = i;
		// wrap the ring ptr if we need to, that is why we are a ring
		if ( ++ringi >= 5 ) ringi = 0;
		// this 5-word sequence starts with word # "start"
		long start = ringPos[ringi];
		// need at least 5 words in the ring buffer to do analysis
		if ( ++count < 5 ) continue;
		// sanity check
		//if ( start <= lastStart ) { char *xx = NULL; *xx = 0; }
		// look up in the hash table
		//long n = h & mask;
		// stop at new york times - debug
		/*
		if ( words->m_words[i][0] == 'A' &&
		     words->m_words[i][1] == 's' &&
		     words->m_words[i][2] == 'k' &&
		     words->m_words[i][3] == 'e' &&
		     words->m_words[i][4] == 'd' &&
		     words->m_words[i][5] == ' ' &&
		     words->m_words[i][6] == 'Q' &&
		     words->m_words[i][7] == 'u' ) 
			log("hey");
		*/
		//loop:
		// all done if empty
		if ( ! repeatTable->isInTable(&h) ) {//! hashes[n] ) {
			// add ourselves to the hash table now
			//hashes[n] = h;
			// this is where the 5-word sequence starts
			//vals  [n] = matchStart+1;
			long val = matchStart+1;
			repeatTable->addKey(&h,&val);
			// do not demote any words if less than 8 matched
			if ( matched < 3 ) { matched = 0; continue; }
			// reset
			matched = 0;
			// . how much we should we demote
			// . 10 matching words pretty much means 0 weights
			//float demote = 1.0 - ((matched-5)*.10);
			//if ( demote >= 1.0 ) continue;
			//if ( demote <  0.0 ) demote = 0.0;
			// demote the words involved
			for ( long j = matchStart ; j < i ; j++ ) 
				wis[j].m_repeatScore = 0;
			// get next word
			continue;
		}
		// save start of matching sequence for demote loop
		if ( matched == 0 ) matchStart = start;
		// inc the match count
		matched++;
	}
	// if we ended without nulling out some matches
	if ( matched < 3 ) return;
	for ( long j = matchStart ; j < nw ; j++ ) 
		wis[j].m_repeatScore = 0;

}

///////////////////
//
// FAST FACTS
//
// Sentences containing a gigabit and a lot or all of the query terms.
//
//
///////////////////

static int factCmp ( const void *a, const void *b ) {
	Fact *fa = *(Fact **)a;
	Fact *fb = *(Fact **)b;
	float sa = fa->m_maxGigabitModScore * fa->m_queryScore;
	float sb = fb->m_maxGigabitModScore * fb->m_queryScore;
	// punish if more than one gigabit! just try to get all
	// query terms and ONE gigabit to keep things more targetted.
	sa /= fa->m_numGigabits;
	sb /= fb->m_numGigabits;
	if ( sa < sb ) return  1; // swap!
	if ( sa > sb ) return -1;
	if ( fa->m_factLen > fb->m_factLen ) return  1; // swap
	if ( fa->m_factLen < fb->m_factLen ) return -1;
	// then based on docid
	if ( fa->m_docId > fb->m_docId ) return 1; // swap
	if ( fa->m_docId < fb->m_docId ) return -1;
	// if same docid, base on doc position
	if ( fa->m_fact > fb->m_fact ) return 1; // swap
	if ( fa->m_fact < fb->m_fact ) return -1;
	return 0;
}


// . now make the fast facts from the gigabits and the samples. 
// . these are sentences containing the query and a gigabit.
bool Msg40::computeFastFacts ( ) {

	// skip for now
	//return true;

	bool debugGigabits = m_si->m_debugGigabits;

	//
	// hash gigabits by first wordid and # words, and phrase hash
	//
	HashTableX gbitTable;
	char gbuf[30000];
	if ( ! gbitTable.set(8,4,1024,gbuf,30000,false,0,"gbtbl") )
		return false;
	long numGigabits = m_gigabitBuf.length()/sizeof(Gigabit);
	Gigabit *gigabits = (Gigabit *)m_gigabitBuf.getBufStart();
	for ( long i = 0 ; i < numGigabits ; i++ ) {
		// get the ith gigabit
		Gigabit *gi = &gigabits[i];
		// parse into words
		Words ww;
		ww.setx ( gi->m_term , gi->m_termLen , 0 );
		long long *wids = ww.getWordIds();
		if ( ! wids[0] ) { char *xx=NULL;*xx=0; }
		// . hash first word
		// . so gigabit has # words in it so we can do a slower
		//   compare function to make sure entire gigabit is matched
		//   in the code below
		if ( ! gbitTable.addKey ( &wids[0] , &gi ) ) return false;
	}

	//
	// hash the query terms we need to match into table as well
	//
	Query *q = m_si->m_q;
	HashTableX queryTable;
	char qbuf[10000];
	if ( ! queryTable.set(8,4,512,qbuf,10000,false,0,"qrttbl") )
		return false;
	for ( long i = 0 ; i < q->m_numTerms ; i++ ) {
		// shortcut
		QueryTerm *qt = &q->m_qterms[i];
		// skip if no weight!
		if ( qt->m_popWeight <= 0.0 ) continue;
		// use RAW termid
		if ( ! queryTable.addKey ( &qt->m_rawTermId, &qt ) ) 
			return false;
	}


	//
	// store Facts (sentences) into this safebuf
	//
	char ftmp[100000];
	SafeBuf factBuf(ftmp,100000);
	// scan docs in search results
	for ( long i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// skip if not visible
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// get it
		Msg20* thisMsg20 = m_msg20[i];
		// must be there! wtf?
		Msg20Reply *reply = thisMsg20->getReply();
		// get sample. sample uses \0 as delimeters between excerpts
		char *p    =     reply-> ptr_gigabitSample;
		char *pend = p + reply->size_gigabitSample; // includes \0
		// skip if empty
		if ( ! p ) continue;
		// scan excerpts in sample, \0 separated
		while ( p < pend ) {
			//
			// find the terminating " * " that delineates sections
			// and sentences in this excerpt.
			//
			// NOW we delineate sentences and headings with |'s
			//
			char *pstart = p;
			for ( ; *p ; p++ ) {
				if ( p[0] == '|' ) 
					break;
			}
			// mark that
			char *pend = p;
			// skip over delimeter if it was there so
			// pstart points to the next section on the next
			// iteration
			if ( *p ) p += 1;
			// otherwise, skip the \0
			else p++;
			// debug
			//log("docId=%lli EXCERPT=%s",docId,p);
			// . add facts that have the query and a gigabit
			// . set Fact::m_score based on gigabit it contains
			// . limit to complete sentences, surrounded by *'s
			//   i guess...
			if ( ! addFacts ( &queryTable,
					  &gbitTable,
					  pstart,
					  pend,
					  debugGigabits,
					  reply,
					  &factBuf ) )
				return false;
		}
	}


	//
	// now sort the Facts by scores
	//
	long numFacts = factBuf.getLength() / sizeof(Fact);
	Fact *facts = (Fact *)factBuf.getBufStart();
	SafeBuf ptrBuf;
	if ( ! ptrBuf.reserve( numFacts * 4 ) ) return false;
	for ( long i = 0 ; i < numFacts ; i++ ) {
		Fact *fi = &facts[i];
		ptrBuf.pushLong((long)fi);
	}
	Fact **ptrs = (Fact **)ptrBuf.getBufStart();
	gbqsort ( ptrs , numFacts , sizeof(Fact *) , factCmp , 0 );



	//
	// now dedup and set m_gigabitModScore to 0 if a dup fact!
	//
	long need = 0;
	for ( long i = 0 ; i < numFacts ; i++ ) {
		// get it
		Fact *fi = &facts[i];
		char *v1 = fi->m_dedupVector;
		long vsize = SAMPLE_VECTOR_SIZE;
		// compare its dedup vector to the facts before us
		long j; for ( j = 0 ; j < i ; j++ ) {
			// get it
			Fact *fj = &facts[j];
			char *v2 = fj->m_dedupVector;
			char ss = g_clusterdb.getSampleSimilarity(v1,v2,vsize);
			if ( ss < 80 ) continue;
			// damn, we're a dup sentence...
			fi->m_gigabitModScore = 0.0;
			fi->m_queryScore   = 0.0;
			break;
		}
		// otherwise we passed
		if ( j >= i ) need += sizeof(Fact);
	}

	//
	// now transcribe the non-dups over into permanent buf
	//
	if ( ! m_factBuf.reserve ( need ) ) return false;
	for ( long i = 0 ; i < numFacts ; i++ ) {
		// get it
		Fact *fi = &facts[i];
		if ( fi->m_gigabitModScore == 0.0 ) continue;
		// transcribe
		m_factBuf.safeMemcpy ( fi , sizeof(Fact) );
	}	


	return true;
}

bool Msg40::addFacts ( HashTableX *queryTable,
		       HashTableX *gbitTable ,
		       char *pstart,
		       char *pend,
		       bool debugGigabits ,
		       Msg20Reply *reply,
		       SafeBuf *factBuf ) {

	// parse into words. 0 niceness.
	Words ww;
	if ( ! ww.set11 ( pstart,pend , 0 ) ) return false;

	long nw = ww.getNumWords();
	long long *wids = ww.getWordIds();

	// initialize the sentence/fact we might add to factBuf if score>0
	Fact fact;
	fact.m_queryScore   = 0;
	fact.m_gigabitModScore = 0;
	fact.m_numGigabits  = 0;
	fact.m_printed      = 0;
	fact.m_numQTerms    = 0;
	fact.m_fact         = pstart;
	fact.m_factLen      = pend - pstart;
	fact.m_reply        = reply;
	fact.m_maxGigabitModScore = 0;

	// . sentences end in periods.
	// . all sections delimeted by **'s
	for ( long i = 0 ; i < nw ; i++ ) {
		// skip punct words in the sentence/section
		if ( ! wids[i] ) continue;
		// does it match a query term?
		QueryTerm **qtp = (QueryTerm **)queryTable->getValue(&wids[i]);
		// yes?
		if ( qtp ) {
			// get the query term it matches
			QueryTerm *qt = *qtp;
			// add points for matching it!
			fact.m_queryScore += qt->m_popWeight;
			fact.m_numQTerms++;
			// no need to add gigabit then
			continue;
		}
		// match a gigabit?
		Gigabit **gbp = (Gigabit **)gbitTable->getValue(&wids[i]);
		if ( gbp ) {
			// avoid overflow of ptrs!
			if ( fact.m_numGigabits >= MAX_GIGABIT_PTRS )
				continue;
			// get the gigabit it might match
			Gigabit *gb = *gbp;
			// see if matches all words in the gigabit
			long x = i + 2;
			long k;
			for ( k = 1 ; k < gb->m_numWords ; k++ ) {
				// get next word id in sent
				for ( ; x < nw && ! wids[x] ; x++ );
				// all done? no match then
				if ( x >= nw ) break;
				// ok check it
				if ( gb->m_wordIds[k] != wids[x]) break;
				// advance x too
				x++;
			}
			// it does NOT match the full gigabit! next word then.
			if ( k < gb->m_numWords ) goto nomatch;
			// . ok, it is a match
			// . multiply gigabit score by # pages it is on
			//   to get the modified gigabit score
			float gbModScore = gb->m_gbscore * gb->m_numPages;
			fact.m_gigabitModScore += gbModScore;
			if ( gbModScore > fact.m_maxGigabitModScore  )
				fact.m_maxGigabitModScore = gbModScore;
			fact.m_gigabitPtrs[fact.m_numGigabits] = gb;
			fact.m_numGigabits++;
			continue;
		}
	nomatch:
		// otherwise, it does not match a gigabit or query word
		continue;
	}

	// ok, skip if missing either a gigabit or query term
	if ( fact.m_gigabitModScore == 0 ) return true;
	if ( fact.m_queryScore      == 0 ) return true;


	//
	// make a vector out of words for deduping it!
	//
	HashTableX simTable;
	char sbuf[5000];
	simTable.set(4,0,256,sbuf,5000,false,0,"simtab3");
	char vtmp[5000];
	SafeBuf vbuf(vtmp,5000);
	for ( long j = 0 ; j < nw ; j++ ) {
		// make it this
		unsigned long widu;
		widu = (unsigned long long)(wids[j]);
		// dont allow this! zero is a vector terminator
		if ( widu == 0 ) widu = 1;
		// skip if already added to vector
		if ( simTable.isInTable(&widu) ) continue;
		// store that as a vector component
		if ( ! vbuf.pushLong(widu) ) return false;
		// make sure we do not dedup
		if ( ! simTable.addKey(&widu) ) return false;
	}
	// sort 32-bit word ids from excerpt. niceness = 0
	vbuf.sortLongs(0); 
	// make sure under (128-4) bytes...
	vbuf.truncLen(((long)SAMPLE_VECTOR_SIZE) - 4);
	// make last long a 0 so Clusterdb::getSimilarity() likes it
	vbuf.pushLong(0);
	// now store it in the Fact struct
	memcpy ( fact.m_dedupVector , vbuf.getBufStart(), vbuf.length() );


	// otherwise, add it
	if ( ! factBuf->safeMemcpy ( &fact , sizeof(Fact) ) ) return false;
	return true;
}
