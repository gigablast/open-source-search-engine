#ifndef _BUZZLOGIC_
#include "gb-include.h"
#endif

#include "Msg40.h"
#include "Stats.h"        // for timing and graphing time to get all summaries
//#include "CollectionRec.h"
#include "Collectiondb.h"
//#include "TitleRec.h"      // containsAdultWords ()
#include "LanguageIdentifier.h"
#include "sort.h"
#include "matches2.h"
#include "XmlDoc.h" // computeSimilarity()
//#include "Facebook.h" // msgfb
#include "Speller.h"
#include "Wiki.h"
#include "HttpServer.h"
#include "PageResults.h"

// increasing this doesn't seem to improve performance any on a single
// node cluster....
#define MAX_OUTSTANDING_MSG20S 200

bool printHttpMime ( class State0 *st ) ;

//static void handleRequest40              ( UdpSlot *slot , int32_t netnice );
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



bool isSubDom(char *s , int32_t len);

Msg40::Msg40() {
	m_calledFacets = false;
	m_doneWithLookup = false;
	m_socketHadError = 0;
	m_buf           = NULL;
	m_buf2          = NULL;
	m_cachedResults = false;
	m_msg20         = NULL;
	m_numMsg20s     = 0;
	m_msg20StartBuf = NULL;
	m_numToFree     = 0;
	// new stuff for streaming results:
	m_hadPrintError = false;
	m_numPrinted    = 0;
	m_printedHeader = false;
	m_printedTail   = false;
	m_sendsOut      = 0;
	m_sendsIn       = 0;
	m_printi        = 0;
	m_numDisplayed  = 0;
	m_numPrintedSoFar = 0;
	m_lastChunk     = false;
	m_didSummarySkip = false;
	m_omitCount      = 0;
	m_printCount = 0;
	//m_numGigabitInfos = 0;
	m_numCollsToSearch = 0;
	m_numMsg20sIn = 0;
	m_numMsg20sOut = 0;
}

#define MAX2 50

void Msg40::resetBuf2 ( ) {
	// remember num to free in reset() function
	char *p = m_msg20StartBuf;
	// msg20 destructors
	for ( int32_t i = 0 ; i < m_numToFree ; i++ ) {
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


	// make a safebuf of 50 of them if we haven't yet
	if ( m_unusedBuf.length() <= 0 ) return;
	Msg20 *ma = (Msg20 *)m_unusedBuf.getBufStart();
	for ( int32_t i = 0 ; i < (int32_t)MAX2 ; i++ ) ma[i].destructor();
}

Msg40::~Msg40() {
	// free tmp msg3as now
	for ( int32_t i = 0 ; i < m_numCollsToSearch ; i++ ) {
		if ( ! m_msg3aPtrs[i] ) continue;
		if ( m_msg3aPtrs[i] == &m_msg3a ) continue;
		mdelete ( m_msg3aPtrs[i] , sizeof(Msg3a), "tmsg3a");
		delete  ( m_msg3aPtrs[i] );
		m_msg3aPtrs[i] = NULL;
	}
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

	m_omitCount = 0;

	// warning
	//if ( ! si->m_coll2 ) log(LOG_LOGIC,"net: NULL collection. msg40.");
	if ( si->m_collnumBuf.length() < (int32_t)sizeof(collnum_t) )
		log(LOG_LOGIC,"net: NULL collection. msg40.");


	m_lastProcessedi = -1;
	m_didSummarySkip = false;

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

	m_lastHeartbeat = getTimeLocal();

	//just getfrom searchinput
	//....	m_catId = hr->getLong("catid",0);m_si->m_catId;

 	m_postQueryRerank.set1( this, si );

	// take search parms i guess from first collnum
	collnum_t *cp = (collnum_t *)m_si->m_collnumBuf.getBufStart();

	// get the collection rec
	CollectionRec *cr =g_collectiondb.getRec( cp[0] );

	// g_errno should be set if not found
	if ( ! cr ) { g_errno = ENOCOLLREC; return true; }

	// save that
	m_firstCollnum = cr->m_collnum;

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
	for ( int32_t i = 0 ; i < m_si->m_numTopicGroups ; i++ ) {
		int32_t x = m_si->m_topicGroups[i].m_docsToScanForTopics ;
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
	// crap then we don't stream anything if in streaming mode.
	if ( m_si->m_docsWanted == 0 ) {
		log("msg40: setting streamresults to false. n=0.");
		m_si->m_streamResults = false;
		return true;
	}

	// or if no query terms
	if ( m_si->m_q.m_numTerms <= 0 ) {
		log("msg40: setting streamresults to false. numTerms=0.");
		m_si->m_streamResults = false;
		return true;
	}

	// . do this now in case results were cached.
	// . set SearchInput class instance, m_si
	// . has all the input that we need to get the search results just
	//   the way the caller wants them
	//m_msg1a.setSearchInput(m_si);

	// how many docids do we need to get?
	int32_t get = m_si->m_docsWanted + m_si->m_firstResultNum ;
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
	//if ( get < m_si->m_docsToScanForReranking  ) 
	//	get = m_si->m_docsToScanForReranking;
	// for zak's reference pages
	// if(get<m_si->m_refs_numToGenerate ) get=m_si->m_refs_numToGenerate;
	// limit to this ceiling though for peformance reasons
	//if ( get > m_maxDocIdsToCompute ) get = m_maxDocIdsToCompute;
	// ok, need some sane limit though to prevent malloc from 
	// trying to get 7800003 docids and going ENOMEM
	if ( get > MAXDOCIDSTOCOMPUTE ) {
		log("msg40: asking for too many docids. reducing to %"INT32"",
		    (int32_t)MAXDOCIDSTOCOMPUTE);
		get = MAXDOCIDSTOCOMPUTE;
	}
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
	//get = (get*130LL)/100LL;
	// make it 10% because we are getting too many summaries some times
	// no, this is bad when not doing site clustering or dup removal
	// we need to skip directly to the 1000th result sometimes to show
	// those results and we do not want to lookup the first 1000
	// summaries, so we don't, and this makes us end up looking up 100
	// more summaries. well, leave this in, just limit the max out
	// for summaries below then to what we want to show.
	// crap, Msg40::gotSummary() has a m_numRequests < m_numDocIds
	// condition, so take this out...
	//get = (get*110LL)/100LL;

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
	//	get = (int32_t) ((float)get * cr->m_numDocsMultiplier);
	// limit to this ceiling though for peformance reasons
	//if ( get > m_maxDocIdsToCompute ) get = m_maxDocIdsToCompute;
	// . ALWAYS get at least this many
	// . this allows Msg3a to allow higher scoring docids in tier #1 to
	//   outrank lower-scoring docids in tier #0, even if such docids have
	//   all the query terms explicitly. and we can guarantee consistency
	//   as int32_t as we only allow for this outranking within the first
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
		logf(LOG_DEBUG,"query: msg40 mapped %"INT32" wanted to %"INT32" to get",
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
		int32_t  requestSize;
		// CAUTION: m_docsToGet can be different on remote host!!!
		char *request = m_si->serializeForMsg40 ( &requestSize );
		if ( ! request ) return true;
		// . set timeout based on docids requested!
		// . the more docs requested the longer it will take to get
		// . use 50ms per docid requested
		int32_t timeout = (50 * m_docsToGet) / 1000;
		// always wait at least 20 seconds
		if ( timeout < 20 ) timeout = 20;
		// . forward to another cluster
		// . use the advanced composite query to make the key
		uint32_t h = hash32 ( m_si->m_qbuf1 );
		// get groupId from docId, if positive
		int32_t          groupNum = h % g_hostdb2.m_numGroups;
		uint32_t groupId  = g_hostdb2.getGroupId ( groupNum );
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
	// if searching multiple collections do not cache for now
	if ( m_si->m_collnumBuf.length() > (int32_t)sizeof(collnum_t) ) 
		useCache=false;

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
					      // use first collection #
					      m_si->m_firstCollnum,
					      this , 
					      gotCacheReplyWrapper ,
					      m_si->m_niceness ,
					      1 ) )
			return false;
		// reset g_errno, we're just a cache
		g_errno = 0;
		bool status = gotCacheReply();

		if ( status && m_si->m_streamResults ) {
			log("msg40: setting streamresults to false. "
			    "was in cache.");
			m_si->m_streamResults = false;
		}

		return status;
	}

	// keep going
	bool status = prepareToGetDocIds ( );

	if ( status && m_si->m_streamResults ) {
		log("msg40: setting streamresults to false. "
		    "prepare did not block.");
		m_si->m_streamResults = false;
	}

	return status;
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
	int32_t bufSize , bufMaxSize;
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
	int32_t nb = deserialize(m_cachePtr, m_cacheSize);
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
		int64_t now  = gettimeofdayInMilliseconds();
		int64_t took = now - m_startTime;
		log(LOG_TIMING,
		    "query: [%"PTRFMT"] found in cache. "
		    "lookup took %"INT64" ms.",(PTRTYPE)this,took);
	}
	m_cachedTime = m_msg17.getCachedTime();
	m_cachedResults = true;
	// if it was found, we return true, m_cachedTime should be set
	return true;
}

bool Msg40::prepareToGetDocIds ( ) {

	// log the time it took for cache lookup
	if ( g_conf.m_logTimingQuery || m_si->m_debug ) {
		int64_t now  = gettimeofdayInMilliseconds();
		int64_t took = now - m_startTime;
		logf(LOG_TIMING,"query: [%"PTRFMT"] Not found in cache. "
		     "Lookup took %"INT64" ms.",(PTRTYPE)this,took);
		m_startTime = now;
		logf(LOG_TIMING,"query: msg40: [%"PTRFMT"] Getting up to %"INT32" "
		     "(docToGet=%"INT32") docids", (PTRTYPE)this,
		     m_docsToGetVisible,  m_docsToGet);
	}

	//if ( m_si->m_compoundListMaxSize <= 0 )
	//	log("query: Compound list max size is %"INT32". That is bad. You "
	//	    "will not get back some search results for UOR queries.",
	//	    m_si->m_compoundListMaxSize );

	// . if query has dirty words and family filter is on, set
	//   number of results to 0, and set the m_queryClen flag to true
	// . m_qbuf1 should be the advanced/composite query
	if ( m_si->m_familyFilter && 
	     getDirtyPoints ( m_si->m_sbuf1.getBufStart() , 
			      m_si->m_sbuf1.length() , 
			      0 ,
			      NULL ) ) {
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

	// . get the docIds
	// . this sets m_msg3a.m_clusterLevels[] for us
	//if(! m_msg3a.getDocIds ( &m_r,  m_si->m_q, this , gotDocIdsWrapper))
	//	return false;

	////
	//
	// NEW CODE FOR LAUNCHING one MSG3a per collnum to search a token
	//
	////
	m_num3aReplies = 0;
	m_num3aRequests = 0;

	// how many are we searching? usually just one.
	m_numCollsToSearch = m_si->m_collnumBuf.length() /sizeof(collnum_t);

	// make enough for ptrs
	int32_t need = sizeof(Msg3a *) * m_numCollsToSearch;
	if ( ! m_msg3aPtrBuf.reserve ( need ) ) return true;
	// cast the mem buffer
	m_msg3aPtrs = (Msg3a **)m_msg3aPtrBuf.getBufStart();

	// clear these out so we do not free them when destructing
	for ( int32_t i = 0 ; i < m_numCollsToSearch ;i++ )
		m_msg3aPtrs[i] = NULL;

	// use first guy in case only one coll we are searching, the std case
	if ( m_numCollsToSearch <= 1 )
		m_msg3aPtrs[0] = &m_msg3a;

	return federatedLoop();
}

bool Msg40::federatedLoop ( ) {

	// search the provided collnums (collections)
	collnum_t *cp = (collnum_t *)m_si->m_collnumBuf.getBufStart();

	// we modified m_rcache above to be true if we should read from cache
	int32_t maxAge = 0 ;
	if ( m_si->m_rcache ) maxAge = g_conf.m_indexdbMaxIndexListAge;


	// reset it
	Msg39Request mr;
	mr.reset();

	//m_r.ptr_coll                    = m_si->m_coll2;
	//m_r.size_coll                   = m_si->m_collLen2+1;
	mr.m_maxAge                    = maxAge;
	mr.m_addToCache                = m_si->m_wcache;
	mr.m_docsToGet                 = m_docsToGet;
	mr.m_maxFacets                 = m_si->m_maxFacets;
	mr.m_niceness                  = m_si->m_niceness;
	mr.m_debug                     = m_si->m_debug          ;
	mr.m_getDocIdScoringInfo       = m_si->m_getDocIdScoringInfo;
	mr.m_doSiteClustering          = m_si->m_doSiteClustering    ;
	mr.m_hideAllClustered          = m_si->m_hideAllClustered;
	mr.m_familyFilter              = m_si->m_familyFilter;
	//mr.m_useMinAlgo                = m_si->m_useMinAlgo;
	//mr.m_useNewAlgo                = m_si->m_useNewAlgo;
	mr.m_doMaxScoreAlgo            = m_si->m_doMaxScoreAlgo;
	//mr.m_fastIntersection          = m_si->m_fastIntersection;
	//mr.m_doIpClustering            = m_si->m_doIpClustering      ;
	mr.m_doDupContentRemoval       = m_si->m_doDupContentRemoval ;
	//mr.m_restrictIndexdbForQuery   = m_si->m_restrictIndexdbForQuery ;
	mr.m_queryExpansion            = m_si->m_queryExpansion; 
	//mr.m_compoundListMaxSize       = m_si->m_compoundListMaxSize ;
	mr.m_boolFlag                  = m_si->m_boolFlag            ;
	mr.m_familyFilter              = m_si->m_familyFilter        ;
	mr.m_language                  = (unsigned char)m_si->m_queryLangId;
	mr.ptr_query                   = m_si->m_q.m_orig;
	mr.size_query                  = m_si->m_q.m_origLen+1;
	//mr.ptr_whiteList               = m_si->m_whiteListBuf.getBufStart();
	//mr.size_whiteList              = m_si->m_whiteListBuf.length()+1;
	int32_t slen = 0; if ( m_si->m_sites ) slen=gbstrlen(m_si->m_sites)+1;
	mr.ptr_whiteList               = m_si->m_sites;
	mr.size_whiteList              = slen;
	mr.m_timeout                   = -1; // auto-determine based on #terms
	// make sure query term counts match in msg39
	//mr.m_maxQueryTerms             = m_si->m_maxQueryTerms; 
	mr.m_realMaxTop                = m_si->m_realMaxTop;

	mr.m_minSerpDocId              = m_si->m_minSerpDocId;
	mr.m_maxSerpScore              = m_si->m_maxSerpScore;
	mr.m_sameLangWeight            = m_si->m_sameLangWeight;

	//
	// how many docid splits should we do to avoid going OOM?
	//
	CollectionRec *cr = g_collectiondb.getRec(m_firstCollnum);
	RdbBase *base = NULL;
	if ( cr ) g_titledb.getRdb()->getBase(cr->m_collnum);
	int64_t numDocs = 0;
	if ( base ) numDocs = base->getNumTotalRecs();
	// for every 5M docids per host, lets split up the docid range
	// to avoid going OOM
	int32_t mult = numDocs / 5000000;
        if ( mult <= 0 ) mult = 1;
	// . do not do splits if caller is already specifying a docid range
	//   like for gbdocid: queries i guess.
	// . make sure m_msg2 is non-NULL, because if it is NULL we are
	//   evaluating a query for a single docid for seo tools
	//if ( m_r->m_minDocId == -1 ) { // && m_msg2 ) {
	int32_t nt = m_si->m_q.getNumTerms();
	int32_t numDocIdSplits = nt / 2; // ;/// 2;
	if ( numDocIdSplits <= 0 ) numDocIdSplits = 1;
	// and mult based on index size
	numDocIdSplits *= mult;
	// prevent going OOM for type:article AND html
	if ( numDocIdSplits < 5 ) numDocIdSplits = 5;
	//}

	if ( cr ) mr.m_maxQueryTerms = cr->m_maxQueryTerms; 
	else      mr.m_maxQueryTerms = 100;

	// special oom hack fix
	if ( cr && cr->m_isCustomCrawl && numDocIdSplits < 4 ) 
		numDocIdSplits = 4;

	// for testing
	//m_numDocIdSplits = 3;
	//if ( ! g_conf.m_doDocIdRangeSplitting )
	//	m_numDocIdSplits = 1;
	// limit to 10
	if ( numDocIdSplits > 15 ) 
		numDocIdSplits = 15;
	// store it in the reuquest now
	mr.m_numDocIdSplits = numDocIdSplits;

	int32_t maxOutMsg3as = 1;

	// create new ones if searching more than 1 coll
	for ( int32_t i = m_num3aRequests ; i < m_numCollsToSearch ; i++ ) {

		// do not have more than this many outstanding
		if ( m_num3aRequests - m_num3aReplies >= maxOutMsg3as )
			// wait for it to return before launching another
			return false;

		// get it
		Msg3a *mp = m_msg3aPtrs[i];
		// stop if only searching one collection
		if ( ! mp ) {
			try { mp = new ( Msg3a); }
			catch ( ... ) {
				g_errno = ENOMEM;
				return true;
			}
			mnew(mp,sizeof(Msg3a),"tm3ap");
		}
		// error?
		if ( ! mp ) {
			log("msg40: Msg40::getDocIds() had error: %s",
			    mstrerror(g_errno));
			return true;
		}
		// assign it
		m_msg3aPtrs[i] = mp;
		// assign the request for it
		gbmemcpy ( &mp->m_rrr , &mr , sizeof(Msg39Request) );
		// then customize it to just search this collnum
		mp->m_rrr.m_collnum = cp[i];

		// launch a search request
		m_num3aRequests++;
		// this returns false if it would block and will call callback
		// m_si is actually contained in State0 in PageResults.cpp
		// and Msg40::m_si points to that. so State0's destructor
		// should call SearchInput's destructor which calls
		// Query's destructor to destroy &m_si->m_q here when done.
		if(!mp->getDocIds(&mp->m_rrr,&m_si->m_q,this,gotDocIdsWrapper))
			continue;
		if ( g_errno && ! m_errno ) 
			m_errno = g_errno;
		m_num3aReplies++;
	}

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
	THIS->m_num3aReplies++;
	// try to launch more if there are more colls left to search
	if ( THIS->m_num3aRequests < THIS->m_numCollsToSearch ) {
		THIS->federatedLoop ( );
		return;
	}
	// return if this blocked
	if ( ! THIS->gotDocIds() ) return;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
}

// . return false if blocked, true otherwise
// . sets g_errno on error
bool Msg40::gotDocIds ( ) {

	// return now if still waiting for a msg3a reply to get in
	if ( m_num3aReplies < m_num3aRequests ) return false;


	// if searching over multiple collections let's merge their docids
	// into m_msg3a now before we go forward
	// this will set g_errno on error, like oom
	if ( ! mergeDocIdsIntoBaseMsg3a() )
		log("msg40: error: %s",mstrerror(g_errno));


	// log the time it took for cache lookup
	int64_t now  = gettimeofdayInMilliseconds();

	if ( g_conf.m_logTimingQuery || m_si->m_debug||g_conf.m_logDebugQuery){
		int64_t took = now - m_startTime;
		logf(LOG_DEBUG,"query: msg40: [%"PTRFMT"] Got %"INT32" docids in %"INT64" ms",
		     (PTRTYPE)this,m_msg3a.getNumDocIds(),took);
		logf(LOG_DEBUG,"query: msg40: [%"PTRFMT"] Getting up to %"INT32" "
		     "summaries", (PTRTYPE)this,m_docsToGetVisible);
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
	//for ( int32_t i = 1 ; i < m_msg3a.m_numDocIds ; i++ )
	//	m_msg3a.m_clusterLevels[i] = CR_CLUSTERED;

	// time this
	m_startTime = gettimeofdayInMilliseconds();

	// we haven't got any Msg20 responses as of yet or sent any requests
	m_numRequests  =  0;
	m_numReplies   =  0;
	//m_maxiLaunched = -1;

	// when returning search results in csv let's get the first 100
	// results and use those to determine the most common column headers
	// for the csv. any results past those that have new json fields we
	// will add a header for, but the column will not be labelled with
	// the header name unfortunately.
	m_needFirstReplies = 0;
	if ( m_si->m_format == FORMAT_CSV ) {
		m_needFirstReplies = m_msg3a.m_numDocIds;
		if ( m_needFirstReplies > 100 ) m_needFirstReplies = 100;
	}

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
	//int32_t maxAge = 0;
	//if ( m_si->m_rcache ) maxAge = g_conf.m_titledbMaxCacheAge;

	// . launch a bunch of task that depend on the docids we got
	// . gigabits, reference pages and dmoz topics
	// . keep track of how many are out
	m_tasksRemaining = 0;

	// debug msg
	if ( m_si->m_debug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: [%"PTRFMT"] Getting topics/gigabits, "
		     "reference pages and dir pages.",(PTRTYPE)this);

	// . do not bother getting topics if we are passed first page
	// . AWL NOTE: pqr needs topics on all pages
	//if ( m_si->m_firstResultNum > 0 ) return launchMsg20s ( false );

	// do not bother getting topics if we will be re-called below so we
	// will be here again!
	//if ( numVisible       < m_docsToGet && // are we int16_t?
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


	//
	// call Msg2b to generate directory
	//
	// why is this here? it does not depend on the docids. (mdw 9/25/13)
	// dissect it and fix it!!
	//
	//if ( m_si->m_catId && 
	//     ! m_msg2b.generateDirectory ( m_si->m_catId,
	//				   (void*)this,
	//				   didTaskWrapper ) )
	//	m_tasksRemaining++;


	return launchMsg20s ( false );
}

bool Msg40::mergeDocIdsIntoBaseMsg3a() {

	// only do this if we were searching multiple collections, otherwise
	// all the docids are already in m_msg3a
	if ( m_numCollsToSearch <= 1 ) return true;
	
	// free any mem in use
	m_msg3a.reset();

	// count total docids into "td"
	int32_t td = 0LL;
	for ( int32_t i = 0 ; i < m_numCollsToSearch ; i++ ) {
		Msg3a *mp = m_msg3aPtrs[i];
		td += mp->m_numDocIds;
		// reset cursor for list of docids from this collection
		mp->m_cursor = 0;
		// add up here too
		m_msg3a.m_numTotalEstimatedHits += mp->m_numTotalEstimatedHits;
	}

	// setup to to merge all msg3as into our one m_msg3a
	int32_t need = 0;
	need += td * 8;
	need += td * sizeof(double);
	need += td * sizeof(key_t);
	need += td * 1;
	need += td * sizeof(collnum_t);
	// make room for the merged docids
	m_msg3a.m_finalBuf =  (char *)mmalloc ( need , "finalBuf" );
	m_msg3a.m_finalBufSize = need;
	// return true with g_errno set
	if ( ! m_msg3a.m_finalBuf ) return true;
	// parse the memory up into arrays
	char *p = m_msg3a.m_finalBuf;
	m_msg3a.m_docIds        = (int64_t *)p; p += td * 8;
	m_msg3a.m_scores        = (double    *)p; p += td * sizeof(double);
	m_msg3a.m_clusterRecs   = (key_t     *)p; p += td * sizeof(key_t);
	m_msg3a.m_clusterLevels = (char      *)p; p += td * 1;
	m_msg3a.m_scoreInfos    = NULL;
	m_msg3a.m_collnums      = (collnum_t *)p; p += td * sizeof(collnum_t);
	if ( p - m_msg3a.m_finalBuf != need ) { char *xx=NULL;*xx=0; }

	m_msg3a.m_numDocIds = td;

	//
	// begin the collection merge
	//

	int32_t next = 0;

 loop:

	// get next biggest score
	double max  = -1000000000.0;
	Msg3a *maxmp = NULL;

	for ( int32_t i = 0 ; i < m_numCollsToSearch ; i++ ) {
		// int16_tcut
		Msg3a *mp = m_msg3aPtrs[i];
		// get cursor
		int32_t cursor = mp->m_cursor;
		// skip if exhausted
		if ( cursor >= mp->m_numDocIds ) continue;
		// get his next score 
		double score = mp->m_scores[ cursor ];
		if ( score <= max ) continue;
		// got a new winner
		max = score;
		maxmp = mp;
	}

	// store him
	if ( maxmp ) {
		m_msg3a.m_docIds  [next] = maxmp->m_docIds[maxmp->m_cursor];
		m_msg3a.m_scores  [next] = maxmp->m_scores[maxmp->m_cursor];
		m_msg3a.m_collnums[next] = maxmp->m_rrr.m_collnum;
		m_msg3a.m_clusterLevels[next] = CR_OK;
		maxmp->m_cursor++;
		next++;
		goto loop;
	}

	// free tmp msg3as now
	for ( int32_t i = 0 ; i < m_numCollsToSearch ; i++ ) {
		if ( m_msg3aPtrs[i] == &m_msg3a ) continue;
		mdelete ( m_msg3aPtrs[i] , sizeof(Msg3a), "tmsg3a");
		delete  ( m_msg3aPtrs[i] );
		m_msg3aPtrs[i] = NULL;
	}

	return true;
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
	int64_t need = m_msg3a.m_numDocIds * sizeof(Msg20 *);
	// need space for the classes themselves, only if "visible" though
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) 
		if ( m_msg3a.m_clusterLevels[i] == CR_OK ) 
			need += sizeof(Msg20);

	// MDW: try to preserve the old Msg20s if we are being re-called
	if ( m_buf2 ) {
		// we do not do recalls when streaming yet
		if ( m_si->m_streamResults ) { char *xx=NULL;*xx=0; }
		// use these 3 vars for mismatch stat reporting
		//int32_t      mismatches = 0;
		//int64_t mismatch1  = 0LL;
		//int64_t mismatch2  = 0LL;
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
		int32_t pcount = 0;
		// fill in the actual Msg20s from the old buffer
		for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
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
			// set this now
			tmp[i]->m_owningParent = (void *)this;
			tmp[i]->m_constructedId = 1;
			// count it
			pcount++;
			// skip it if it is a new docid, we do not have a Msg20
			// for it from the previous tier. IF it is from
			// the current tier, THEN it is new.
			//if ( m_msg3a.m_tiers[i] == m_msg3a.m_tier ) continue;
			// see if we can find this docid from the old list!
			int32_t k = 0;
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
				     "docid %"INT64" (max=%"INT32") "
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
				//   " at #%"INT32". olddocid=%"INT64" newdocid=%"INT64"",i,
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
		//for ( int32_t i = 0 ; i < m_numMsg20s ; i++ ) {
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
		//	logf(LOG_DEBUG,"query: msg40: docid %"INT64" mismatched "
		//	     "%"INT64". Total of %"INT32" mismathes. q=%s",
		//	     mismatch1,mismatch2,mismatches,
		//	     m_msg3a.m_q->m_orig );
		// all done
		return true;
	}

	m_numMsg20s = m_msg3a.m_numDocIds;

	// when streaming because we can have hundreds of thousands of
	// search results we recycle a few msg20s to save mem
	if ( m_si->m_streamResults ) {
		int32_t max = MAX_OUTSTANDING_MSG20S * 2;
		if ( m_msg3a.m_numDocIds < max ) max = m_msg3a.m_numDocIds;
		need = 0;
		need += max * sizeof(Msg20 *);
		need += max * sizeof(Msg20);
		m_numMsg20s = max;
	}

	m_buf2        = NULL;
	m_bufMaxSize2 = need;

	// if ( need > 2000000000 ) {
	// 	log("msg40: need too much mem=%"INT64,need);
	// 	m_errno = ENOMEM;
	// 	g_errno = ENOMEM;
	// 	return false; 
	// }

	// do the alloc
	if ( need ) m_buf2 = (char *)mmalloc ( need ,"Msg40msg20");
	if ( need && ! m_buf2 ) { m_errno = g_errno; return false; }
	// point to the mem
	char *p = m_buf2;
	// point to the array, then make p point to the Msg20 buffer space
	m_msg20 = (Msg20 **)p; 
	p += m_numMsg20s * sizeof(Msg20 *);
	// start free here
	m_msg20StartBuf = p;
	// set the m_msg20[] array to use this memory, m_buf20
	for ( int32_t i = 0 ; i < m_numMsg20s ; i++ ) {
		// assume empty
		m_msg20[i] = NULL;
		// if clustered, do a NULL ptr
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// point it to its memory
		m_msg20[i] = (Msg20 *)p;
		// call its constructor
		m_msg20[i]->constructor();
		// set this now
		m_msg20[i]->m_owningParent = (void *)this;
		m_msg20[i]->m_constructedId = 2;
		// point to the next Msg20
		p += sizeof(Msg20);
		// remember num to free in reset() function
		m_numToFree++;
	}
	// remember how many we got in here in case we have to realloc above
	//m_numMsg20s = m_msg3a.m_numDocIds;

	return true;
}

void didTaskWrapper ( void* state ) {
	Msg40 *THIS = (Msg40 *) state;
	// one less task
	THIS->m_tasksRemaining--;
	// this returns false if blocked
	if ( ! THIS->launchMsg20s ( false ) ) return;
	// we are done, call the callback
	THIS->m_callback ( THIS->m_state );
}

bool Msg40::launchMsg20s ( bool recalled ) {

	// don't launch any more if client browser closed socket
	if ( m_socketHadError ) { char *xx=NULL; *xx=0; }

	// these are just like for passing to Msg39 above
	int32_t maxAge = 0 ;
	//if ( m_si->m_rcache ) maxAge = g_conf.m_titledbMaxCacheAge;
	// may it somewhat jive with the search results caching, otherwise
	// it will tell me a search result was indexed like 3 days ago
	// when it was just indexed 10 minutes ago because the 
	// titledbMaxCacheAge was set way too high
	if ( m_si->m_rcache ) maxAge = g_conf.m_searchResultsMaxCacheAge;

	/*
	// "need" = how many more msg20 replies do we need to get back to
	// get the required number of search results?
	int32_t sample        = 0;
	int32_t good          = 0;
	int32_t gaps          = 0;
	int32_t goodAfterGaps = 0;
	// loop up to the last msg20 request we actually launched
	for ( int32_t i = 0 ; i <= m_maxiLaunched ; i++ ) {
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
	// what we already have that is visible as int32_t as it is before any gap
	int32_t need = m_docsToGetVisible - good ;
	// if we fill in the gaps, we get "goodAfterGaps" more visible results
	if ( need >= gaps ) {
		// so no need to get these then
		need -= goodAfterGaps ;
		// but watch out for flooding!
		if ( need < gaps ) need = gaps;
	}
	// how many total good?
	int32_t allGood = good + goodAfterGaps;
	// get the visiblity ratio from the replies we did get back
	float ratio ;
	if ( allGood > 0 ) ratio = (float)sample / (float)allGood;
	else               ratio = (float)sample / 1.0        ;
	// give a 5% boost
	ratio *= 1.05;
	// assume some of what we "need" will be invisible, make up for that
	if ( sample > 0 ) need = (int32_t)((float)need * ratio);
	// . restrict "need" to no more than 50 at a time
	// . we are using it for a "max outstanding" msg20s
	// . do not overflow the udpservers
	if ( need > 50 ) need = 50;

	if ( m_si->m_debug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: msg40: can launch %"INT32" more msg20s. "
		     "%"INT32" out. %"INT32" completed. %"INT32" visible. %"INT32" gaps. "
		     "%"INT32" contiguous. %"INT32" toGet. ",
		     need,m_numRequests-m_numReplies,sample,allGood,gaps,
		     m_numContiguous,m_docsToGet);
	*/

	int32_t bigSampleRadius = 0;
	int32_t bigSampleMaxLen = 0;
	// NOTE: pqr needs gigabits for all pages
	if(m_docsToScanForTopics > 0 /*&& m_si->m_firstResultNum == 0*/) {
		bigSampleRadius = 300;
		//bigSampleMaxLen = m_si->m_topicGroups[0].m_topicSampleSize;
		bigSampleMaxLen = 5000;
	}

	int32_t maxOut = (int32_t)MAX_OUTSTANDING_MSG20S;
	if ( g_udpServer.getNumUsedSlots() > 500 ) maxOut = 10;
	if ( g_udpServer.getNumUsedSlots() > 800 ) maxOut = 1;

	// if not deduping or site clustering or getting gigabits, then
	// just skip over docids for speed.
	// don't bother with summaries we do not need
	if ( m_si && 
	     ! m_si->m_doDupContentRemoval &&
	     ! m_si->m_doSiteClustering &&
	     // gigabits required the first X summaries to be computed
	     m_docsToScanForTopics <= 0 &&
	     m_lastProcessedi == -1 ) {
		// start getting summaries with the result # they want
		m_lastProcessedi = m_si->m_firstResultNum-1;
		// assume we printed the summaries before
		m_printi = m_si->m_firstResultNum;
		m_numDisplayed = m_si->m_firstResultNum;
		// fake this so Msg40::gotSummary() can let us finish
		// because it checks m_numRequests <  m_msg3a.m_numDocIds
		m_numRequests = m_si->m_firstResultNum;
		m_numReplies  = m_si->m_firstResultNum;
		m_didSummarySkip = true;
		log("query: skipping summary generation of first %"INT32" docs",
		    m_si->m_firstResultNum);
	}

	// if not doing deduping or site clustering, let's not get like
	// 100 summaries at a time when we only wanted 10 results
	// for performance reasons
	// if ( m_si && 
	//      ! m_si->m_doDupContentRemoval &&
	//      ! m_si->m_doSiteClustering &&
	//       maxOut > m_si->m_docsWanted ) 
	// 	maxOut = m_si->m_docsWanted;


	// . launch a msg20 getSummary() for each docid
	// . m_numContiguous should preceed any gap, see below
	for ( int32_t i = m_lastProcessedi+1 ; i < m_msg3a.m_numDocIds ;i++ ) {
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
		if ( m_numRequests-m_numReplies >= maxOut ) break;
		// do not launch another until m_printi comes back because
		// all summaries are bottlenecked on printing him out now.
		if ( m_si->m_streamResults &&
		     // must have at least one outstanding summary guy
		     // otherwise we can return true below and cause
		     // the stream to truncate results in gotSummary()
		     //m_numReplies < m_numRequests &&
		     i >= m_printi + MAX_OUTSTANDING_MSG20S - 1 )
			break;

		// if we have printed enough summaries then do not launch
		// any more, wait for them to come back in.
		/// this is causing problems because we have a bunch of
		// m_printi < m_msg3a.m_numDocIds checks that kinda expect
		// us to get all summaries for every docid. but when we
		// do federated search we can get a ton of docids.
		// if ( m_printi >= m_docsToGetVisible ) {
		// 	logf(LOG_DEBUG,"query: got %"INT32" >= %"INT32" "
		// 	     "summaries. done. "
		// 	     "waiting on remaining "
		// 	     "%"INT32" to return."
		// 	     , m_printi
		// 	     , m_docsToGetVisible
		// 	     , m_numRequests-m_numReplies);
		// 	// wait for all msg20 replies to come in
		// 	if ( m_numRequests != m_numReplies ) break;
		// 	// then let's hack fix this then so we can call
		// 	// printSearchResultsTail()
		// 	m_printi   = m_msg3a.m_numDocIds;
		// 	// set these to max so they do not launch another
		// 	// summary request, just in case, below
		// 	m_numRequests = m_msg3a.m_numDocIds;
		// 	m_numReplies  = m_msg3a.m_numDocIds;
		// 	break;
		// }

		// do not double count!
		//if ( i <= m_lastProcessedi ) continue;
		// do not repeat for this i
		m_lastProcessedi = i;


		// if we have printed enough summaries then do not launch
		// any more, wait for them to come back in.
		/// this is causing problems because we have a bunch of
		// m_printi < m_msg3a.m_numDocIds checks that kinda expect
		// us to get all summaries for every docid. but when we
		// do federated search we can get a ton of docids.
		// if ( m_printi >= m_docsToGetVisible ) {
		// 	logf(LOG_DEBUG,"query: got %"INT32" >= %"INT32" "
		// 	     "summaries. done. "
		// 	     "waiting on remaining "
		// 	     "%"INT32" to return."
		// 	     , m_printi
		// 	     , m_docsToGetVisible
		// 	     , m_numRequests-m_numReplies);
		// 	m_numRequests++;
		// 	m_numReplies++;
		// 	continue;
		// }


		// start up a Msg20 to get the summary
		Msg20 *m = NULL;
		if ( m_si->m_streamResults ) {
			// there can be hundreds of thousands of results
			// when streaming, so recycle a few msg20s to save mem
			m = getAvailMsg20();
			// mark it so we know which docid it goes with
			m->m_ii = i;
		}
		else
			m = m_msg20[i];

		// if to a dead host, skip it
		int64_t docId = m_msg3a.m_docIds[i];
		uint32_t shardNum = g_hostdb.getShardNumFromDocId ( docId );
		// get the collection rec
		CollectionRec *cr = g_collectiondb.getRec(m_firstCollnum);
		// if shard is dead then do not send to it if not crawlbot
		if ( g_hostdb.isShardDead ( shardNum ) &&
		     cr &&
		     // diffbot urls.csv downloads often encounter dead
		     // hosts that are not really dead, so wait for it
		     ! cr->m_isCustomCrawl &&
		     // this is causing us to truncate streamed results
		     // too early when we have false positives that a 
		     // host is dead because the server is locking up 
		     // periodically
		     ! m_si->m_streamResults ) {
			log("msg40: skipping summary "
			    "lookup #%"INT32" of "
			    "docid %"INT64" for dead shard #%"INT32""
			    , i
			    , docId
			    , shardNum );
			m_numRequests++;
			m_numReplies++;
			continue;
		}


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
			logf(LOG_DEBUG,"query: msg40: [%"PTRFMT"] Getting "
			     "summary #%"INT32" for docId=%"INT64"",
			     (PTRTYPE)this,i,m_msg3a.m_docIds[i]);
		// launch it
		m_numRequests++;
		// keep for-loops int16_ter with this
		//if ( i > m_maxiLaunched ) m_maxiLaunched = i;
		
		//getRec(m_si->m_coll2,m_si->m_collLen2);
		if ( ! cr ) {
			log("msg40: missing coll");
			g_errno = ENOCOLLREC;
			if ( m_numReplies < m_numRequests ) return false;
			return true;
		}


		// set the summary request then get it!
		Msg20Request req;
		Query *q = &m_si->m_q;
		req.ptr_qbuf             = q->getQuery();
		req.size_qbuf            = q->getQueryLen()+1;
		req.m_langId             = m_si->m_queryLangId;

		// set highlight query
		if ( m_si->m_highlightQuery &&
		     m_si->m_highlightQuery[0] ) {
			req.ptr_hqbuf = m_si->m_highlightQuery;
			req.size_hqbuf = gbstrlen(req.ptr_hqbuf)+1;
		}

		//int32_t q3size = m_si->m_sbuf3.length()+1;
		//if ( q3size == 1 ) q3size = 0;
		//req.ptr_q2buf             = m_si->m_sbuf3.getBufStart();
		//req.size_q2buf            = q3size;
		
		req.m_isMasterAdmin             = m_si->m_isMasterAdmin;

		//req.m_rulesetFilter      = m_si->m_ruleset;

		//req.m_getTitleRec         = m_si->m_getTitleRec;

		//req.m_isSuperTurk       = m_si->m_isSuperTurk;


		req.m_highlightQueryTerms = m_si->m_doQueryHighlighting;
		//req.m_highlightDates      = m_si->m_doDateHighlighting;

		//req.ptr_coll             = m_si->m_coll2;
		//req.size_coll            = m_si->m_collLen2+1;

		req.m_isDebug            = (bool)m_si->m_debug;

		if ( m_si->m_displayMetas && m_si->m_displayMetas[0] ) {
			int32_t dlen = gbstrlen(m_si->m_displayMetas);
			req.ptr_displayMetas     = m_si->m_displayMetas;
			req.size_displayMetas    = dlen+1;
		}

		req.m_docId              = m_msg3a.m_docIds[i];
		
		// if the msg3a was merged from other msg3as because we
		// were searching multiple collections...
		if ( m_msg3a.m_collnums )
			req.m_collnum = m_msg3a.m_collnums[i];
		// otherwise, just one collection
		else
			req.m_collnum = m_msg3a.m_rrr.m_collnum;

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
		req.m_getSectionVotingInfo   = m_si->m_getSectionVotingInfo;
		req.m_considerTitlesFromBody = m_si->m_considerTitlesFromBody;
		if ( cr->m_considerTitlesFromBody )
			req.m_considerTitlesFromBody = true;
		req.m_expected           = true;
		req.m_getSummaryVector   = true;
		req.m_bigSampleRadius    = bigSampleRadius;
		req.m_bigSampleMaxLen    = bigSampleMaxLen;
		//req.m_titleMaxLen        = 256;
		req.m_titleMaxLen = m_si->m_titleMaxLen; // cr->
		req.m_summaryMaxLen = cr->m_summaryMaxLen;

		// Line means excerpt 
		req.m_summaryMaxNumCharsPerLine = 
			m_si->m_summaryMaxNumCharsPerLine;

		// a special undocumented thing for getting <h1> tag
		req.m_getHeaderTag       = m_si->m_hr.getLong("geth1tag",0);
		//req.m_numSummaryLines = cr->m_summaryMaxNumLines;
		// let "ns" parm override
		req.m_numSummaryLines    = m_si->m_numLinesInSummary;

		if(m_si->m_isMasterAdmin && m_si->m_format == FORMAT_HTML )
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
		//if ( m_si->m_getSitePops )
		//	req.m_computeLinkInfo = true;

		if (m_si->m_queryMatchOffsets)
			req.m_getMatches = true;

		// it copies this using a serialize() function
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
	if ( recalled ) 
		return true;
	// if we got nothing, that's it
	if ( m_msg3a.m_numDocIds <= 0 ) {
		// but if in streaming mode we still have to stream the
		// empty results back
		if ( m_si->m_streamResults ) return gotSummary ( );
		// otherwise, we're done
		return true;
	}
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

Msg20 *Msg40::getAvailMsg20 ( ) {
	for ( int32_t i = 0 ; i < m_numMsg20s ; i++ ) {
		// m_inProgress is set to false right before it
		// calls Msg20::m_callback which is gotSummaryWrapper()
		// so we should be ok with this
		if ( m_msg20[i]->m_launched ) continue;
		return m_msg20[i];
	}
	// how can this happen???  THIS HAPPEND
	char *xx=NULL;*xx=0; 
	return NULL;
}

Msg20 *Msg40::getCompletedSummary ( int32_t ix ) {
	for ( int32_t i = 0 ; i < m_numMsg20s ; i++ ) {
		// it seems m_numMsg20s can be > m_numRequests when doing
		// a multi collection federated search somehow and this
		// can therefore be null
		if ( ! m_msg20[i] ) 
			continue;
		if ( m_msg20[i]->m_ii != ix ) continue;
		if ( m_msg20[i]->m_inProgress ) return NULL;
		return m_msg20[i];
	}
	return NULL;
}


bool gotSummaryWrapper ( void *state ) {
	Msg40 *THIS  = (Msg40 *)state;
	// inc it here
	THIS->m_numReplies++;
	// log every 1000 i guess
	if ( (THIS->m_numReplies % 1000) == 0 )
		log("msg40: got %"INT32" summaries out of %"INT32"",
		    THIS->m_numReplies,
		    THIS->m_msg3a.m_numDocIds);
	// it returns false if we're still awaiting replies
	if ( ! THIS->m_calledFacets && ! THIS->gotSummary ( ) ) return false;
	// lookup facets
	if ( THIS->m_si &&
	     ! THIS->m_si->m_streamResults &&
	     ! THIS->lookupFacets() ) 
		return false;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
	return true;
}

void doneSendingWrapper9 ( void *state , TcpSocket *sock ) {
	Msg40 *THIS = (Msg40 *)state;
	// the send completed, count it
	THIS->m_sendsIn++;
	// error?
	if ( THIS->m_sendsIn > THIS->m_sendsOut ) {
		log("msg40: sendsin > sendsout. bailing!!!");
		// try to prevent a core i haven't fixed right yet!!!
		// seems like a reply coming back after we've destroyed the
		// state!!!
		return;
	}
	// debug
	//g_errno = ETCPTIMEDOUT;
	// socket error? if client closes the socket midstream we get one.
	if ( g_errno ) {
		THIS->m_socketHadError = g_errno;
		log("msg40: streaming socket had error: %s",
		    mstrerror(g_errno));
		// i guess destroy the socket here so we don't get called again?

	}
	// clear it so we don't think it was a msg20 error below
	g_errno = 0;
	// try to send more... returns false if blocked on something
	if ( ! THIS->gotSummary() ) return;
	// all done!!!???
	THIS->m_callback ( THIS->m_state );
}

// . returns false if not all replies have been received (or timed/erroredout)
// . returns true if done (or an error finished us)
// . sets g_errno on error
bool Msg40::gotSummary ( ) {
	// now m_linkInfo[i] (for some i, i dunno which) is filled
	if ( m_si->m_debug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: msg40: [%"PTRFMT"] Got summary. "
		     "Total got=#%"INT32".",
		     (PTRTYPE)this,m_numReplies);

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

	// initialize dedup table if we haven't already
	if ( ! m_dedupTable.isInitialized() &&
	     ! m_dedupTable.set (4,0,64,NULL,0,false,m_si->m_niceness,"srdt") )
		log("query: error initializing dedup table: %s",
		    mstrerror(g_errno));

	State0 *st = (State0 *)m_state;

	// keep socket alive if not streaming. like downloading csv...
	// this fucks up HTTP replies by inserting a space before the "HTTP"
	// it does not render properly on the browser...
	/*
	int32_t now2 = getTimeLocal();
	if ( now2 - m_lastHeartbeat >= 10 && ! m_si->m_streamResults &&
	     // incase socket is closed and recycled for another connection
	     st->m_socket->m_numDestroys == st->m_numDestroys ) {
		m_lastHeartbeat = now2;
		int n = ::send ( st->m_socket->m_sd , " " , 1 , 0 );
		log("msg40: sent heartbeat of %"INT32" bytes on sd=%"INT32"",
		    (int32_t)n,(int32_t)st->m_socket->m_sd);
	}
	*/


	/*
	// sanity check
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// stop as soon as we hit a gap breaking our contiguity... 
		Msg20 *m = m_msg20[i];
		if ( ! m ) continue;
		Msg20Reply *mr = m->m_r;
		if ( ! mr ) continue;
		char *cc = mr->ptr_content;
		if ( ! cc ) continue;
		//if ( ! strstr(cc,"Modern Marketing KF400032MA") )  continue;
		//log("hey");
		//fprintf(stderr,"msg %"INT32" = %s\n",i,cc );
		if ( i == 48329 ) { char *xx=NULL;*xx=0; }
		mr->ptr_content = NULL;
	}
	*/


 doAgain:

	SafeBuf *sb = &st->m_sb;

	sb->reset();

	// this is in PageResults.cpp
	if ( m_si && m_si->m_streamResults && ! m_printedHeader ) {
		// only print header once
		m_printedHeader = true;
		printHttpMime ( st );
		printSearchResultsHeader ( st );
	}

	for ( ; m_si && m_si->m_streamResults&&m_printi<m_msg3a.m_numDocIds ;
	      m_printi++){

		// if we are waiting on our previous send to complete... wait..
		if ( m_sendsOut > m_sendsIn ) break;

		// get summary for result #m_printi
		Msg20 *m20 = getCompletedSummary ( m_printi );

		// if printing csv we need the first 100 results back
		// to get the most popular csv headers for to print that
		// as the first row in the csv output. if we print a
		// results with a column not in the header row then we
		// augment the headers then and there, although the header
		// row will be blank for the new column, we can put
		// the new header row at the end of the file i guess. this way
		// we can immediately start streaming back the csv.
		if ( m_needFirstReplies ) {
			// need at least this many replies to process
			if ( m_numReplies < m_needFirstReplies )
				break;
			// ensure we got the TOP needFirstReplies in order
			// of their display to ensure consistency
			int32_t k;
			for ( k = 0 ; k < m_needFirstReplies ; k++ ) {
				Msg20 *xx = getCompletedSummary(k);
				if ( ! xx ) break;
				if ( ! xx->m_r && 
				     // and it did not have an error fetching
				     // because m_r could be NULL and m_errno
				     // is set to something like Bad Cached
				     // Document
				     ! xx->m_errno ) 
					break;
			}
			// if not all have come back yet, wait longer...
			if ( k < m_needFirstReplies ) break;
			// now make the csv header and print it
			printCSVHeaderRow ( sb );
			// and no longer need to do this logic
			m_needFirstReplies = 0;
		}

		// otherwise, get the summary for result #m_printi
		//Msg20 *m20 = m_msg20[m_printi];

		//if ( ! m20 ) {
		//	log("msg40: m20 NULL #%"INT32"",m_printi);
		//	continue;
		//}

		// if result summary #i not yet in, wait...
		if ( ! m20 ) 
			break;

		// wait if no reply for it yet
		//if ( m20->m_inProgress )
		//	break;

		if ( m20->m_errno ) {
			log("msg40: sum #%"INT32" error: %s",
			    m_printi,mstrerror(m20->m_errno));
			// make it available to be reused
			m20->reset();
			continue;
		}

		// get the next reply we are waiting on to print results order
		Msg20Reply *mr = m20->m_r;
		if ( ! mr ) break;
		//if ( ! mr ) { char *xx=NULL;*xx=0; }

		// primitive deduping. for diffbot json exclude url's from the
		// XmlDoc::m_contentHash32.. it will be zero if invalid i guess
		if ( m_si && m_si->m_doDupContentRemoval && // &dr=1
		     mr->m_contentHash32 &&
		     // do not dedup CT_STATUS results, those are
		     // spider reply "documents" that indicate the last
		     // time a doc was spidered and the error code or success
		     // code
		     mr->m_contentType != CT_STATUS &&
		     m_dedupTable.isInTable ( &mr->m_contentHash32 ) ) {
			//if ( g_conf.m_logDebugQuery )
			log("msg40: dup sum #%"INT32" (%"UINT32")"
			    "(d=%"INT64")",m_printi,
			    mr->m_contentHash32,mr->m_docId);
			// make it available to be reused
			m20->reset();
			continue;
		}

		// static int32_t s_bs = 0;
		// if ( (s_bs++ % 5) != 0 ) {
		// 	log("msg40: FAKE dup sum #%"INT32" (%"UINT32")(d=%"INT64")",m_printi,
		// 	    mr->m_contentHash32,mr->m_docId);
		// 	// make it available to be reused
		// 	m20->reset();
		// 	continue;
		// }


		// return true with g_errno set on error
		if ( m_si && m_si->m_doDupContentRemoval && // &dr=1
		     mr->m_contentHash32 &&
		     // do not dedup CT_STATUS results, those are
		     // spider reply "documents" that indicate the last
		     // time a doc was spidered and the error code or success
		     // code
		     mr->m_contentType != CT_STATUS &&
		     ! m_dedupTable.addKey ( &mr->m_contentHash32 ) ) {
			m_hadPrintError = true;
			log("msg40: error adding to dedup table: %s",
			    mstrerror(g_errno));
		}

		// assume we show this to the user
		m_numDisplayed++;
		//log("msg40: numdisplayed=%"INT32"",m_numDisplayed);

		// do not print it if before the &s=X start position though
		if ( m_si && m_numDisplayed <= m_si->m_firstResultNum ){
			if ( m_printCount == 0 ) 
				log("msg40: hiding #%"INT32" (%"UINT32")"
				    "(d=%"INT64")",
				    m_printi,mr->m_contentHash32,mr->m_docId);
		        m_printCount++;
			if ( m_printCount == 100 ) m_printCount = 0;
			m20->reset();
			continue;
		}

		// . ok, we got it, so print it and stream it
		// . this might set m_hadPrintError to true
		printSearchResult9 ( m_printi , &m_numPrintedSoFar , mr );

		//m_numPrintedSoFar++;
		//log("msg40: printedsofar=%"INT32"",m_numPrintedSoFar);

		// now free the reply to save memory since we could be 
		// streaming back 1M+. we call reset below, no need for this.
		//m20->freeReply();

		// return it so getAvailMsg20() can use it again
		// this will set m_launched to false
		m20->reset();
	}

	// . set it to true on all but the last thing we send!
	// . after each chunk of data we send out, TcpServer::sendChunk
	//   will call our callback, doneSendingWrapper9 
	if ( m_si->m_streamResults && st->m_socket )
		st->m_socket->m_streamingMode = true;


	// if streaming results, and too many results were clustered or
	// deduped then try to get more by merging the docid lists that
	// we already have from the shards. if this still does not provide
	// enough docids then we will need to issue a new msg39 request to
	// each shard to get even more docids from each shard.
	if ( m_si && m_si->m_streamResults &&
	     // this is coring as well on multi collection federated searches
	     // so disable that for now too. it is because Msg3a::m_r is
	     // NULL.
	     m_numCollsToSearch == 1 &&	     
	     // must have no streamed chunk sends out
	     m_sendsOut == m_sendsIn &&
	     // if we did not ask for enough docids and they were mostly
	     // dups so they got deduped, then ask for more.
	     // m_numDisplayed includes results before the &s=X parm.
	     // and so does m_docsToGetVisiable, so we can compare them.
	     m_numDisplayed < m_docsToGetVisible && 
	     // wait for us to have exhausted the docids we have merged
	     m_printi >= m_msg3a.m_numDocIds &&
	     // wait for us to have available msg20s to get summaries
	     m_numReplies == m_numRequests &&
	     // this is true if we can get more docids from merging
	     // more of the termlists from the shards together.
	     // otherwise, we will have to ask each shard for a
	     // higher number of docids.
	     m_msg3a.m_moreDocIdsAvail &&
	     // do not do this if client closed connection
	     ! m_socketHadError ) { //&&
		// doesn't work on multi-coll just yet, it cores.
		// MAKE it.
		//m_numCollsToSearch == 1 ) {
		// can it cover us?
		int32_t need = m_msg3a.m_docsToGet + 20;
		// note it
		log("msg40: too many summaries deduped. "
		    "getting more "
		    "docids from msg3a merge and getting summaries. "
		    "%"INT32" are visible, need %"INT32". "
		    "changing docsToGet from %"INT32" to %"INT32". "
		    "numReplies=%"INT32" numRequests=%"INT32"",
		    m_numDisplayed,
		    m_docsToGetVisible,
		    m_msg3a.m_docsToGet, 
		    need,
		    m_numReplies, 
		    m_numRequests);
		// merge more docids from the shards' termlists
		m_msg3a.m_docsToGet = need;
		// sanity. the original msg39request must be there
		if ( ! m_msg3a.m_r ) { char *xx=NULL;*xx=0; }
		// this should increase m_msg3a.m_numDocIds
		m_msg3a.mergeLists();
	}

	// if we've printed everything out and we are streaming, now
	// get the facet text. when done this should print the tail
	// like we do below. lookupFacets() should scan the facet values
	// and each value should have a docid with it that we do the lookup
	// on. and store the text into m_facetTextBuf safebuf, and make
	// the facet table have the offset of it in that safebuf.
	if ( m_si && 
	     m_si->m_streamResults && 
	     m_printi >= m_msg3a.m_numDocIds )
		if ( ! lookupFacets () ) return false;


	// . wrap it up with Next 10 etc.
	// . this is in PageResults.cpp
	if ( m_si && 
	     m_si->m_streamResults && 
	     ! m_printedTail &&
	     m_printi >= m_msg3a.m_numDocIds ) {
		m_printedTail = true;
		printSearchResultsTail ( st );
		if ( m_sendsIn < m_sendsOut ) { char *xx=NULL;*xx=0; }
		if ( g_conf.m_logDebugTcp )
			log("tcp: disabling streamingMode now");
		// this will be our final send
		if ( st->m_socket ) st->m_socket->m_streamingMode = false;
	}


	TcpServer *tcp = &g_httpServer.m_tcp;

	//g_conf.m_logDebugTcp = 1;

	// do we still own this socket? i am thinking it got closed somewhere
	// and the socket descriptor was re-assigned to another socket
	// getting a diffbot reply from XmLDoc::getDiffbotReply()
	if ( st->m_socket && 
	     st->m_socket->m_startTime != st->m_socketStartTimeHack ) {
		log("msg40: lost control of socket. sd=%i. the socket "
		    "descriptor closed on us and got re-used by someone else.",
		    (int)st->m_socket->m_sd);
		// if there wasn't already an error like 'broken pipe' then
		// set it here so we stop getting summaries if streaming.
		if ( ! m_socketHadError ) m_socketHadError = EBADENGINEER;
		// make it NULL to avoid us from doing anything to it
		// since sommeone else is using it now.
		st->m_socket = NULL;
		//g_errno = EBADENGINEER;
	}


	// . transmit the chunk in sb if non-zero length
	// . steals the allocated buffer from sb and stores in the 
	//   TcpSocket::m_sendBuf, which it frees when socket is
	//   ultimately destroyed or we call sendChunk() again.
	// . when TcpServer is done transmitting, it does not close the
	//   socket but rather calls doneSendingWrapper() which can call
	//   this function again to send another chunk
	// . when we are truly done sending all the data, then we set lastChunk
	//   to true and TcpServer.cpp will destroy m_socket when done.
	//   no, actually we just set m_streamingMode to false i guess above
	if ( sb->length() &&
	     // did client browser close the socket on us midstream?
	     ! m_socketHadError &&
	     st->m_socket &&
	     ! tcp->sendChunk ( st->m_socket , 
				sb  ,
				this ,
				doneSendingWrapper9 ) )
		// if it blocked, inc this count. we'll only call m_callback 
		// above when m_sendsIn equals m_sendsOut... and 
		// m_numReplies == m_numRequests
		m_sendsOut++;


	// writing on closed socket?
	if ( g_errno ) {
		if ( ! m_socketHadError ) m_socketHadError = g_errno;
		log("msg40: got tcp error : %s",mstrerror(g_errno));
		// disown it here so we do not damage in case it gets 
		// reopened by someone else
		st->m_socket = NULL;
	}

	// do we need to launch another batch of summary requests?
	if ( m_numRequests < m_msg3a.m_numDocIds && ! m_socketHadError ) {
		// . if we can launch another, do it
		// . say "true" here so it does not call us, gotSummary() and 
		//   do a recursive stack explosion
		// . this returns false if still waiting on more to come back
		if ( ! launchMsg20s ( true ) ) return false; 
		// it won't launch now if we are bottlnecked waiting for
		// m_printi's summary to come in
		if ( m_si->m_streamResults ) {
			// it won't launch any if we printed out enough as well
			// and it printed "waiting on remaining 0 to return".
			// we shouldn't be waiting for more to come in b/c
			// we are in gotSummart() so one just came in 
			// freeing up a msg20 to launch another, so assume
			// this means we are basically done. and it
			// set m_numRequests=m_msg3a.m_numDocIds etc.
			//if ( m_numRequests == m_msg3a.m_numDocIds )
			//	goto printTail;
			// otherwise, keep chugging
			goto complete;
		}
		// maybe some were cached?
		//goto refilter;
		// it returned true, so m_numRequests == m_numReplies and
		// we don't need to launch any more! but that does NOT
		// make sense because m_numContiguous < m_msg3a.m_numDocIds
		// . i guess the launch can fail because of oom... and
		//   end up returning true here... seen it happen, and
		//   we had full requests/replies for m_msg3a.m_numDocIds
		log("msg40: got all replies i guess");
		goto doAgain;
		//char *xx=NULL; *xx=0;
	}

 complete:

	// . ok, now i wait for all msg20s (getsummary) to come back in.
	// . TODO: evaluate if this hurts us
	if ( m_numReplies < m_numRequests )
		return false;

	// if streaming results, we are done
	if ( m_si && m_si->m_streamResults ) {
		// unless waiting for last transmit to complete
		if ( m_sendsOut > m_sendsIn ) return false;
		// delete everything! no, doneSendingWrapper9 does...
		//mdelete(st, sizeof(State0), "msg40st0");
		//delete st;
		// otherwise, all done!
		log("msg40: did not send last search result summary. "
		    "this=0x%"PTRFMT" because had error: %s",(PTRTYPE)this,
		    mstrerror(m_socketHadError));
		return true;
	}


	// save this before we increment m_numContiguous
	//int32_t oldNumContiguous = m_numContiguous;

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
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
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
	// int32_t filtered = m_numContiguous - m_visibleContinguous;
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


	int64_t startTime = gettimeofdayInMilliseconds();
	int64_t took;

	// int16_tcut
	//Query *q = m_msg3a.m_q;
	Query *q = &m_si->m_q;
        
	//log(LOG_DEBUG, "query: msg40: deduping from %"INT32" to %"INT32"", 
	//oldNumContiguous, m_numContiguous);

	// count how many are visible!
	//int32_t visible = 0;

	// loop over each clusterLevel and set it
	for ( int32_t i = 0 ; i < m_numReplies ; i++ ) {
		// did we skip the first X summaries because we were
		// not deduping/siteclustering/gettingGigabits?
		if ( m_didSummarySkip && i < m_si->m_firstResultNum )
			continue;
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
			logf( LOG_DEBUG, "query: result %"INT32" (docid=%"INT64") had "
			     "an error (%s) and will not be shown.", i,
			      m_msg3a.m_docIds[i],  mstrerror(m->m_errno));
			// update our m_errno while here
			if ( ! m_errno ) m_errno = m->m_errno;
			if ( ! m_si->m_showErrors ) {
				*level = CR_ERROR_SUMMARY;
				//m_visibleContiguous--; 
				continue;
			}
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
		if ( ! m_si->m_showBanned && mr && mr->m_isBanned ) {
			if ( m_si->m_debug || g_conf.m_logDebugQuery )
			logf ( LOG_DEBUG, "query: result %"INT32" "
			       "(docid=%"INT64") is "
			       "banned and will not be shown.", i, 
			       m_msg3a.m_docIds[i] );
			*level = CR_BANNED_URL;
                        //m_visibleContiguous--;
			continue;
		}
		// corruptino?
		if ( mr && ! mr->ptr_ubuf ) {
			log("msg40: got corrupt msg20 reply for docid %"
			    INT64,mr->m_docId);
			*level = CR_BAD_URL;
			continue;
		}
		// filter out urls with <![CDATA in them
		if ( mr && strstr(mr->ptr_ubuf, "<![CDATA[") ) {
			*level = CR_BAD_URL;
                        //m_visibleContiguous--;
			continue;
		}
		// also filter urls with ]]> in them
		if ( mr && strstr(mr->ptr_ubuf, "]]>") ) {
			*level = CR_BAD_URL;
                        //m_visibleContiguous--;
			continue;
		}
		if( mr && ! mr->m_hasAllQueryTerms ) {
			if ( m_si->m_debug || g_conf.m_logDebugQuery )
			logf( LOG_DEBUG, "query: result %"INT32" "
			      "(docid=%"INT64") is "
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
	int32_t dedupPercent = 0;
	if ( m_si->m_doDupContentRemoval && m_si->m_percentSimilarSummary )
		dedupPercent = m_si->m_percentSimilarSummary;
	// icc=1 turns this off too i think
	if ( m_si->m_includeCachedCopy ) dedupPercent = 0;
	// if the user only requested docids, we have no summaries
	if ( m_si->m_docIdsOnly ) dedupPercent = 0;

	// filter out duplicate/similar summaries
	for ( int32_t i = 0 ; dedupPercent && i < m_numReplies ; i++ ) {
		// skip if already invisible
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// Skip if invalid
		if ( m_msg20[i]->m_errno ) continue;

		// start with the first docid we have not yet checked!
		//int32_t m = oldNumContiguous;
		// get it
		Msg20Reply *mri = m_msg20[i]->m_r;
		// do not dedup CT_STATUS results, those are
		// spider reply "documents" that indicate the last
		// time a doc was spidered and the error code or 
		// success code
		if ( mri->m_contentType == CT_STATUS ) continue;
		// never let it be i
		//if ( m <= i ) m = i + 1;
		// see if any result lower-scoring than #i is a dup of #i
		for( int32_t m = i+1 ; m < m_numReplies ; m++ ) {
			// get current cluster level
			char *level = &m_msg3a.m_clusterLevels[m];
			// skip if already invisible
			if ( *level != CR_OK ) continue;
			// get it
			if ( m_msg20[m]->m_errno ) continue;

			Msg20Reply *mrm = m_msg20[m]->m_r;
			// do not dedup CT_STATUS results, those are
			// spider reply "documents" that indicate the last
			// time a doc was spidered and the error code or 
			// success code
			if ( mrm->m_contentType == CT_STATUS ) continue;
			// use gigabit vector to do topic clustering, etc.
			int32_t *vi = (int32_t *)mri->ptr_vbuf;
			int32_t *vm = (int32_t *)mrm->ptr_vbuf;
			//char  s  = g_clusterdb.
			//	getSampleSimilarity (vi,vm,VECTOR_REC_SIZE );
			float s ;
			s = computeSimilarity(vi,vm,NULL,NULL,NULL,
					      m_si->m_niceness);
			// skip if not similar
			if ( (int32_t)s < dedupPercent ) continue;
			// otherwise mark it as a summary dup
			if ( m_si->m_debug || g_conf.m_logDebugQuery )
				logf( LOG_DEBUG, "query: result #%"INT32" "
				      "(docid=%"INT64") is %.02f%% similar-"
				      "summary of #%"INT32" (docid=%"INT64")", 
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
		for(int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++) {
                        // skip if already invisible
                        if(m_msg3a.m_clusterLevels[i] != CR_OK) continue;
			// get it
			Msg20Reply *mr = m_msg20[i]->m_r;
                        // hash the URL all in lower case to catch wiki dups
			char *url  = mr-> ptr_ubuf;
			int32_t  ulen = mr->size_ubuf - 1;
			
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
                                        int32_t  hlen = mdom - host;
                                        if (isSubDom(host, hlen-1))
                                                url = mdom;
                                }
                        }

                        // adjust url string length
                        ulen -= url - u.getUrl();

			uint64_t h = hash64Lower_a(url, ulen);
                        int32_t slot = m_urlTable.getSlot(h);
                        // if there is no slot,this url doesn't exist => add it
                        if(slot == -1) {
                                m_urlTable.addKey(h,mr->m_docId);
                        }
                        else {
                                // If there was a slot, denote with the 
                                // cluster level URL already exited previously
                                char *level = &m_msg3a.m_clusterLevels[i];
                                if(m_si->m_debug || g_conf.m_logDebugQuery)
                                        logf(LOG_DEBUG, "query: result #%"INT32" "
                                                        "(docid=%"INT64") is the "
                                                        "same URL as "
                                                        "(docid=%"INT64")", 
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
	//int32_t visible = m_filterStats[CR_OK];


	m_omitCount = 0;

	// count how many are visible!
	int32_t visible = 0;
	// loop over each clusterLevel and set it
	for ( int32_t i = 0 ; i < m_numReplies ; i++ ) {
		// get current cluster level
		char *level = &m_msg3a.m_clusterLevels[i];
		// on CR_OK
		if ( *level == CR_OK ) visible++;
		// otherwise count as ommitted
		else m_omitCount++;
	}

	// do we got enough search results now?
	//if ( visible >= m_docsWanted ) 
	//	m_gotEnough = true;

	// show time
	took = gettimeofdayInMilliseconds() - startTime;
	if ( took > 3 )
		log(LOG_INFO,"query: Took %"INT64" ms to do clustering and dup "
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
	for ( int32_t i = 0 ; debug && i < m_msg3a.m_numDocIds ; i++ ) {
		//uint32_t sh;
		//sh = g_titledb.getHostHash(*(key_t*)m_msg20[i]->m_vectorRec);
		int32_t cn = (int32_t)m_msg3a.m_clusterLevels[i];
		if ( cn < 0 || cn >= CR_END ) { char *xx=NULL;*xx=0; }
		char *s = g_crStrings[cn];
		if ( ! s ) { char *xx=NULL;*xx=0; }
		logf(LOG_DEBUG, "query: msg40 final hit #%"INT32") d=%"UINT64" "
		     "cl=%"INT32" (%s)", 
		     i,m_msg3a.m_docIds[i],(int32_t)m_msg3a.m_clusterLevels[i],s);
	}
	if ( debug )
		logf (LOG_DEBUG,"query: msg40: firstResult=%"INT32", "
		      "totalDocIds=%"INT32", resultsWanted=%"INT32" "
		      "visible=%"INT32" toGet=%"INT32" recallCnt=%"INT32"",
		      m_si->m_firstResultNum, m_msg3a.m_numDocIds ,
		      m_docsToGetVisible, visible,
		      //m_numContiguous, 
		      m_docsToGet , m_msg3aRecallCnt);

	// if we do not have enough visible, try to get more
	if ( visible < m_docsToGetVisible && m_msg3a.m_moreDocIdsAvail &&
	     // do not spin too long in this!
	     // TODO: fix this better somehow later
	     m_docsToGet <= 1000 &&
	     // doesn't work on multi-coll just yet, it cores
	     m_numCollsToSearch == 1 ) {
		// can it cover us?
		//int32_t need = m_msg3a.m_docsToGet + 20;
		int32_t need = m_docsToGet + 20;
		// increase by 25 percent as well
		need *= 1.25;
		// note it
		log("msg40: too many summaries invisible. getting more "
		    "docids from msg3a merge and getting summaries. "
		    "%"INT32" are visible, need %"INT32". "
		    "%"INT32" to %"INT32". "
		    "numReplies=%"INT32" numRequests=%"INT32"",
		    visible, m_docsToGetVisible,
		    m_msg3a.m_docsToGet, need,
		    m_numReplies, m_numRequests);
		// get more
		//m_docsToGet = need;

		// get more!
		//m_msg3a.m_docsToGet = need;
		m_docsToGet = need;
		// reset this before launch
		m_numReplies  = 0;
		m_numRequests = 0;
		// reprocess all!
		m_lastProcessedi = -1;
		// let's do it all from the top!
		return getDocIds ( true ) ;
		

		//m_msg3a.mergeLists();
		// rellaoc the msg20 array
		//if ( ! reallocMsg20Buf() ) return true;
		// reset this before launch
		//m_numReplies  = 0;
		//m_numRequests = 0;
		// reprocess all!
		//m_lastProcessedi = -1;
		// now launch!
		//if ( ! launchMsg20s ( true ) ) return false; 
		// all done, call callback
		//return true;
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
		int32_t get = (int32_t)((float)m_docsToGet * ratio);
		// do not breach the limit
		if ( get > m_maxDocIdsToCompute ) get = m_maxDocIdsToCompute;
		// . if different, recall msg3a
		// . if we are then we can start from msg3a.MergedocIds
		if ( get > m_docsToGet ) {
			// debug msg
			//if ( g_conf.m_logDebugQuery || m_si->m_debug )
			logf(LOG_DEBUG,"query: msg40: recalling msg3a "
			     "merge oldactual=%"INT32" newactual=%"INT32"",
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
	int32_t vcnt = 0;
	for ( int32_t i = 0 ; i <= m_maxiLaunched ; i++ ) {
		// skip if never launched and should have... a gap...
		if ( m_msg20[i] && ! m_msg20[i]->m_gotReply ) continue;
		// get cluster level
		char level = m_msg3a.m_clusterLevels[i];
		// sanity check
		if ( level < 0 || level >= CR_END ) { char *xx=NULL; *xx =0; }
		// add it up
		g_stats.m_filterStats[(int32_t)level]++;
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
	int64_t now = gettimeofdayInMilliseconds();
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
		logf(LOG_DEBUG,"query: msg40: [%"PTRFMT"] Got %"INT32" summaries in "
		    "%"INT64" ms",
		     (PTRTYPE)this ,
		     visible, // m_visibleContiguous,
		     now - m_startTime );


	//int32_t maxAge = 0;
	//if ( m_si->m_rcache ) maxAge = g_conf.m_titledbMaxCacheAge;




	/////////////
	//
	//
	// prepare query term extra info for gigabits
	//
	////////////

	//QueryTerm *qterms[MAX_QUERY_TERMS];
	//int32_t nqt = 0;
	//Query *q = m_si->m_q;
	// english? TEST!
	unsigned char lang = m_si->m_queryLangId;
	// just print warning i guess
	if ( lang == 0 ) { 
		log("query: queryLang is 0 for q=%s",q->m_orig);
		//char *xx=NULL;*xx=0; }
	}
	// we gotta use query TERMS not words, because the query may be
	// 'cd rom' and the phrase term will be 'cdrom' which is a good one
	// to use for gigabits! plus we got synonyms now!
	for ( int32_t i = 0 ; i < q->m_numTerms ; i++ ) {
		// int16_tcut
		QueryTerm *qt = &q->m_qterms[i];
		// assume ignored
		qt->m_popWeight = 0;
		qt->m_hash64d   = 0;
		// skip if ignored query stop word etc.
		if ( qt->m_ignored && qt->m_ignored != IGNORE_QUOTED )continue;
		// get the word or phrase
		char *s    = qt->m_term;
		int32_t  slen = qt->m_termLen;
		// use this special hash for looking up popularity in pop dict
		// i think it is just like hash64 but ignores spaces so we
		// can hash 'cd rom' as "cdrom". but i think we do this
		// now, so use m_termId as see...
		uint64_t qh = hash64d(s, slen);
		//int64_t qh = qt->m_termId;
		int32_t qpop;
		qpop = g_speller.getPhrasePopularity(s, qh, true,lang);
		int32_t qpopWeight;
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
		msg.safePrintf("gbits: qpop=%"INT32" qweight=%"INT32" "
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
		int64_t stt = gettimeofdayInMilliseconds();
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
		int32_t ng;
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
		int64_t took = gettimeofdayInMilliseconds() - stt;
		if ( took > 5 )
			logf(LOG_DEBUG,"query: make gigabits took %"INT64" ms",
			     took);
	}


	// take this out for now...
#ifdef GB_PQR
	// run post query reranks for this query
	int32_t wanted = m_si->m_docsWanted + m_si->m_firstResultNum + 1;
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
#endif

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
	int32_t c = 0;
	int32_t v = 0;
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
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
		if ( m_msg3a.m_scoreInfos )
			m_msg3a.m_scoreInfos [c] = m_msg3a.m_scoreInfos [i];
		int32_t need = m_si->m_docsWanted;
		// if done, bail
		if ( ++c >= need ) break;
	}
	// reset the # of docids we got to how many we kept!
	m_msg3a.m_numDocIds = c;

	// debug
	for ( int32_t i = 0 ; debug && i < m_msg3a.m_numDocIds ; i++ )
		logf(LOG_DEBUG, "query: msg40 clipped hit #%"INT32") d=%"UINT64" "
		     "cl=%"INT32" (%s)", 
		     i,m_msg3a.m_docIds[i],(int32_t)m_msg3a.m_clusterLevels[i],
		     g_crStrings[(int32_t)m_msg3a.m_clusterLevels[i]]);

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
		logf(LOG_DEBUG,"query: [%"PTRFMT"] Storing output in cache.",
		     (PTRTYPE)this);
	// store in this buffer
	char tmpBuf [ 64 * 1024 ];
	// use that
	char *p = tmpBuf;
	// how much room?
	int32_t tmpSize = getStoredSize();
	// unless too small
	if ( tmpSize > 64*1024 ) 
		p = (char *)mmalloc(tmpSize,"Msg40Cache");
	if ( ! p ) {
		// this is just for cachinig, not critical... ignore errors
		g_errno = 0;
		logf ( LOG_INFO ,
		       "query: Size of cached search results page (and "
		       "all associated data) is %"INT32" bytes. Max is %i. "
		       "Page not cached.", tmpSize, 32*1024 );
		return true;
	}
	// serialize into tmp
	int32_t nb = serialize ( p , tmpSize );
	// it must fit exactly
	if ( nb != tmpSize || nb == 0 ) {
		g_errno = EBADENGINEER;
		log (LOG_LOGIC,
		     "query: Size of cached search results page (%"INT32") "
		     "does not match what it should be. (%"INT32")",
		     nb, tmpSize );
		return true;
	}

	if ( ! m_msg3a.m_rrr.m_getDocIdScoringInfo ) {
		// make key based on the hash of certain vars in SearchInput
		key_t k = m_si->makeKey();
		// cache it
		m_msg17.storeInCache ( SEARCHRESULTS_CACHEID ,
				       k                     ,
				       p                     , // rec
				       tmpSize               , // recSize
				       m_firstCollnum ,//m_si->m_coll2
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
void Msg40::uncluster ( int32_t m ) {

	// skip for now
	return;

	key_t     crec1 = m_msg3a.m_clusterRecs[m];
	int64_t sh1   = g_clusterdb.getSiteHash26 ( (char *)&crec1 );

	for ( int32_t k = 0 ; k < m_msg3a.m_numDocIds ; k++ ) {
		// skip docid #k if not from same hostname
		key_t     crec2 = m_msg3a.m_clusterRecs[k];
		int64_t sh2   = g_clusterdb.getSiteHash26 ( (char *)&crec2 );
		if ( sh2 != sh1 ) continue;
		// skip if not OK or CLUSTERED
		if ( m_msg3a.m_clusterLevels[k] != CR_CLUSTERED ) continue;
		// UNHIDE IT
		m_msg3a.m_clusterLevels[k] = CR_OK;
		// we must UN-dedup anything after us because now that we are
		// no longer clustered, we could dedup a result below us,
		// which deduped another result, which is now no longer deduped
		// because its deduped was this unclustered results dup! ;)
		for ( int32_t i = k+1 ; i < m_msg3a.m_numDocIds ; i++ ) {
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
		logf(LOG_DEBUG,"query: msg40: unclustering docid #%"INT32" %"INT64". "
		     "(unclusterCount=%"INT32")",
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

int32_t Msg40::getStoredSize ( ) {
	// moreToCome=1
	int32_t size = 1;
	// msg3a
	size += m_msg3a.getStoredSize();
	// add each summary
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds; i++ ) {
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
	//size += m_msg2b.getStoredSize();

	return size;
}

// . serialize ourselves for the cache
// . returns bytes written
// . returns -1 and sets g_errno on error
int32_t Msg40::serialize ( char *buf , int32_t bufLen ) {
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
	int32_t nb = m_msg3a.serialize ( p , pend );
	// return -1 on error
	if ( nb < 0 ) return -1;
	// otherwise, inc over it
	p += nb;

	// . then summary excerpts, keep them word aligned...
	// . TODO: make sure empty Msg20s are very little space!
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
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
		int32_t nb = m_msg20[i]->serialize ( p , pend - p ) ;
		// count it
		if ( m_msg3a.m_rrr.m_debug )
			log("query: msg40 serialize msg20size=%"INT32"",nb);
		//if ( m_r.m_debug ) {
		//	int32_t mcount = 0;
		//	Msg20Reply *mr = m_msg20[i]->m_r;
		//	for ( int32_t *mm = &mr->size_tbuf ; 
		//	      mm <= &mr->size_templateVector ; 
		//	      mm++ ) {
		//		log("query: msg20 #%"INT32" = %"INT32"",
		//		    mcount,*mm);
		//		mcount++;
		//	}
		//}
		if ( nb == -1 ) return -1;
		p += nb;
	}

	// nah, just re-instersect from the msg20 replies again! its quick
	//int32_t x = m_msg24.serialize ( p , pend - p );
	//if ( x == -1 ) return -1;
	//p += x;

	//int32_t y = m_msg1a.serialize (p, pend - p);
	//if ( y == -1 ) return -1;
	//p += y;

	//int32_t z = m_msg2b.serialize (p, pend - p);
	//if ( z == -1 ) return -1;
	//p += z;

	if ( m_msg3a.m_rrr.m_debug )
		log("query: msg40 serialize nd=%"INT32" "
		    "msg3asize=%"INT32" ",m_msg3a.m_numDocIds,nb);

	// return bytes stored
	return p - buf;
}

// . deserialize ourselves for the cache
// . returns bytes written
// . returns -1 and sets g_errno on error
int32_t Msg40::deserialize ( char *buf , int32_t bufSize ) {

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
	int32_t nb = m_msg3a.deserialize ( p , pend );
	// return -1 on error
	if ( nb < 0 ) return -1;
	// otherwise, inc over it
	p += nb;

	// . alloc buf to hold all m_msg20[i] ptrs and the Msg20s they point to
	// . return -1 if this failed! it will set g_errno/m_errno already
	if ( ! reallocMsg20Buf() ) return -1;

	// MDW: then summary excerpts, keep them word aligned...
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// if flag is 0 that means a NULL msg20
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// return -1 on error, g_errno should be set
		int32_t x = m_msg20[i]->deserialize ( p , pend - p ) ;
		if ( x == -1 ) return -1;
		p += x;
	}

	// msg2b
	//int32_t z = m_msg2b.deserialize ( p , pend - p );
	//if ( z == -1 ) return -1;
	//p += z;

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
static bool initSubDomTable(HashTable *table, char *words[], int32_t size ){
	// set up the hash table
	if ( ! table->set ( size * 2 ) ) 
		return log(LOG_INIT,"build: Could not init sub-domain "
			   "table." );
	// now add in all the stop words
	int32_t n = (int32_t)size/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char      *sw    = words[i];
		int32_t       swlen = gbstrlen ( sw );
                int32_t h = hash32Lower_a(sw, swlen);
                int32_t slot = table->getSlot(h);
                // if there is no slot, this url doesn't exist => add it
                if(slot == -1)
                        table->addKey(h,0);
                else 
                        log(LOG_INIT,"build: Sub-domain table has duplicates");
	}
	return true;
}

bool isSubDom(char *s , int32_t len) {
	if ( ! s_subDomInitialized ) {
		s_subDomInitialized = 
			initSubDomTable(&s_subDomTable, s_subDoms, 
				      sizeof(s_subDoms));
		if (!s_subDomInitialized) return false;
	} 

	// get from table
        int32_t h = hash32Lower_a(s, len);
        if(s_subDomTable.getSlot(h) == -1)
                return false;
	return true;
}		




//////////////////////////////////
//
// COMPUTE GIGABITS!!!
//
//////////////////////////////////


bool hashGigabitSample ( Query *q, 
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

	// not if we skipped the first X summariest
	if ( m_didSummarySkip ) { char *xx=NULL;*xx=0; }

	//return true;

	//int64_t start = gettimeofdayInMilliseconds();

	int32_t niceness = 0;

	Query *q = &m_si->m_q;

	// for every sample estimate the number of words so we know how big
	// to make our repeat hash table
	int32_t maxWords = 0;

	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
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
		// if m_si->m_showErrors then reply can be NULL if the
		// titleRec was not found
		if ( ! reply ) continue;
		char *sample = reply->ptr_gigabitSample;
		int32_t  slen   = reply->size_gigabitSample;
		// but if doing metas, get the display content as the sample
		//char  *next = thisMsg20->getDisplayBuf();
		//if ( tg->m_meta[0] && next )
		//	sample = thisMsg20->getNextDisplayBuf(&slen,&next);
		// set parser vars
		char *p    = sample;
		char *pend = sample + slen;
		int32_t sampleWords = 0;
		//int32_t numExcerpts = 0;
		while ( p < pend ) {
			// buffer is \0 separated text snippets
			int32_t plen = gbstrlen       (p);
			sampleWords += countWords( p,plen);
			// advance to next exerpt
			p += plen + 1;
			//if ( debug ) numExcerpts++;
		};
		if (maxWords + sampleWords > 0x08000000) {
			log("gbits: too many words in samples. "
			    "Discarding the remaining samples "
			    "(maxWords=%"INT32")", maxWords);
			// return -1 with g_errno set on error
			g_errno = EBUFTOOSMALL;
			return -1;
			//char *xx=NULL;*xx=0;
		}
		// the thing we are counting!!!!
		maxWords += sampleWords;
	}

	//
	// hash table for repeated fragment detection
	//
	// make it big enough so there are gaps, so chains are not too long
	int32_t  minBuckets = (int32_t)(maxWords * 1.5);
	if(minBuckets < 512) minBuckets = 512;
	int32_t  numSlots   = 2 * getHighestLitBitValue ( minBuckets ) ;
	// return -1 with g_errno set on error
	HashTableX repeatTable;
	if ( ! repeatTable.set(8,4,numSlots,NULL , 0, false,niceness,"gbbux"))
		return false;

	//
	// only allow one gigabit sample per ip?
	//
	HashTableX iptable;
	if ( tg->m_ipRestrict ) {
		int32_t ns = m_msg3a.m_numDocIds * 4;
		if ( ! iptable.set(4,0,ns,NULL,0,false,niceness,"gbit") )
			return false;
	}

	//
	// space for all vectors for deduping samples that are 80% similar
	//
	SafeBuf vecBuf;
	int32_t  vneed   = m_msg3a.m_numDocIds * SAMPLE_VECTOR_SIZE;
	if ( tg->m_dedupSamplePercent >= 0 && ! vecBuf.reserve ( vneed ) ) 
		return false;


	//
	//
	// . the master hash table for scoring gigabits
	// . each slot is a class "Gigabit"
	//
	//
	HashTableX master;
	int32_t bs = sizeof(Gigabit);
	// key is a 64-bit wordid hash from Words.cpp
	if ( ! master.set ( 8 , bs , 20000,NULL,0,false,niceness,"mgbt") )
		return false;



	//
	// now combine all the pronouns and pronoun phrases into one big hash 
	// table and collect the top 10 topics
	//
	QUICKPOLL(niceness);
	int32_t numDocsProcessed = 0;

	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
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
			int32_t ipd = ipdom ( reply->m_firstIp );
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
			int32_t  dlen = uu.getMidDomainLen();
			if ( dom && dlen > 0 ) {
				int32_t  h = hash32 ( dom , dlen );
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
		hashGigabitSample ( q,
			     &master, 
			     tg ,
				    // vecbuf is an ongoing accumulation
				    // of wordid vectors from the samples
				    // we let into the master hash table.
			     &vecBuf,
			     thisMsg20,
			     &repeatTable,
			     m_si->m_debugGigabits);
		// ignore errors
		g_errno = 0;
	}

	// debug msg
	/*
	for ( int32_t i = 0 ; i < nt ; i++ ) {
		int32_t score = master->getScoreFromTermNum(i) ;
		if ( ! score ) continue;
		char *ptr  = master->getTermPtr(i) ;
		int32_t len   = master->getTermLen(i);
		char ff[1024];
		if ( len > 1020 ) len = 1020;
		gbmemcpy ( ff , ptr , len );
		ff[len] = '\0';
		// we can have html entities in here now
		//if ( ! is_alnum(ff[0]) ) { char *xx = NULL; *xx = 0; }
		log("%08"INT32" %s",score,ff);
	}
	*/

	// how many do we need?
	//int32_t need = tg->m_maxTopics ;

	SafeBuf gigabitPtrBuf;
	int32_t need = master.m_numSlotsUsed * sizeof(Gigabit *);
	if ( ! gigabitPtrBuf.reserve ( need ) ) return false;

	//int32_t  minScore = 0x7fffffff;
	//int32_t  minj = -1;
	int32_t  i ;

	for ( i = 0 ; i < master.m_numSlots ; i++ ) {
		// skip if empty
		if ( master.isEmpty(i) ) continue;
		// get it
		Gigabit *gb = (Gigabit *)master.getValueFromSlot(i);
		// skip term #i from "table" if it has 0 score
		//int32_t score = master.m_scores[i]; // getScoreFromTermNum(i) ;
		//if ( ! score ) continue;

		// skip if 0 score i guess
		//if ( ! gb->m_qrt ) continue;

		// . make it higher the more popular a term is
		// . these are based on a MAXPOP of 10000
		//int32_t mdc = (int32_t)((((double)numDocsProcessed * 3.0 * 
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
		//score = (int32_t)((frac1 * frac2) / 100.0);
		// we got a winner
		gigabitPtrBuf.pushPtr(gb);
	}

	//
	//
	// sort the gigabit ptrs
	//
	//
	Gigabit **ptrs = (Gigabit **)gigabitPtrBuf.getBufStart();
	int32_t numPtrs = gigabitPtrBuf.length() / sizeof(Gigabit *);
	gbqsort ( ptrs , numPtrs , sizeof(Gigabit *) , gigabitCmp , 0 );

	// we are done if not deduping
	if ( ! tg->m_dedup ) goto skipdedup;

	// . scan the gigabits
	// . now remove similar terms from the gigabits
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {
		// get it
		Gigabit *gi = ptrs[i];
		// skip if nuked already
		if ( gi->m_termLen == 0 ) continue;
		// scan down to this score, but not below
		//int32_t minScore = scores[i] - 25;
		// if we get replaced by a longer guy, remember him
		//int32_t replacerj = -1;
		// . a longer term than encapsulates us can eliminate us
		// . or, if we're the longer, we eliminate the int16_ter
		for ( int32_t j = i + 1 ; j < numPtrs ; j++ ) {
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
			// if we are the int16_ter, nuke the longer guy
			// that contains us because we have a higher score
			// since ptrs are sorted by score then length.
			if ( gi->m_termLen < gj->m_termLen ) {
				// just null term the longer
				char c1 = gi->m_term[gi->m_termLen];
				gi->m_term[gi->m_termLen] = '\0';
				char c2 = gj->m_term[gj->m_termLen];
				gj->m_term[gj->m_termLen] = '\0';
				// if int16_ter is contained
				char *s;
				s = gb_strcasestr (gj->m_term, gi->m_term);
				// un-null term longer
				gj->m_term[gj->m_termLen] = c2;
				gi->m_term[gi->m_termLen] = c1;
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

				// int16_ter gets our score (we need to sort)
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
				// . we can nuke any int16_ter below us, all
				//   scores
				char *s;
				s = gb_strcasestr ( gi->m_term,gj->m_term );
				// un-null term
				gj->m_term[gj->m_termLen] = c2;
				gi->m_term[gi->m_termLen] = c1;
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
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {
		// get it
		Gigabit *gi = ptrs[i];
		// skip if nuked already
		if ( gi->m_termLen == 0 ) continue;
		// int16_tcut
		char *s = gi->m_term;
		int32_t slen = gi->m_termLen;
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

	// now after longer topics replaced the int16_ter topics which they
	// contained, remove the longer topics if they have too many words
	// remove common phrases
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {
		// get it
		Gigabit *gi = ptrs[i];
		// skip if nuked already
		if ( gi->m_termLen == 0 ) continue;
		// set the words to this gigabit
		char *s = gi->m_term;
		int32_t  slen = gi->m_termLen;
		Words w;
		w.setx ( s , slen , 0 );
		int32_t nw = w.getNumWords();
		// . does it have comma? or other punct besides an apostrophe?
		// . we allow gigabit phrases to incorporate a int32_t stretch
		//   of punct... only before the LAST word in the phrase,
		//   that way our overlap removal still works well.
		bool hasPunct = false;
		for ( int32_t k = 0 ; k < slen ; k++ ) {
			if ( ! is_punct_a(s[k]) ) continue;
			// apostrophe is ok as int32_t as alnum follows
			if ( s[k] == '\'' &&
			     is_alnum_a(s[k+1]) ) continue;
			// . period ok, as int32_t as space or alnum follows
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

	int32_t stored = 0;
	// now top winning copy winning gigabits into safebuf
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {
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
// . this compates thisMsg20->getReply()->ptr_gigabitSample excerpts
//   to all from other docids and this docids that we have accumulated
//   because they are distinct enough.
bool hashGigabitSample ( Query *q, 
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
	// if m_si->m_showErrors is true then reply can be NULL
	// if titleRec was not found
	if ( ! reply ) return true;
	// get the ith big sample
	char *bigSampleBuf = reply->ptr_gigabitSample;
	int32_t  bigSampleLen = reply->size_gigabitSample;
	// but if doing metas, get the display content
	//char  *next = thisMsg20->getDisplayBuf();
	// but if doing metas, get the display content
	//if ( tg->m_meta[0] && next)
	//	bigSampleBuf=thisMsg20->getNextDisplayBuf(&bigSampleLen,&next);
	// skip if empty
	if ( bigSampleLen<=0 || ! bigSampleBuf ) return true;
	// the docid
	int64_t docId = reply->m_docId;

	//int64_t start = gettimeofdayInMilliseconds();

	//
	// termtable. for hashing all excerpts in a sample
	//
	HashTableX localGigabitTable;
	int32_t bs = sizeof(Gigabit);
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
	//int32_t vneed = nw * 8;
	//SafeBuf vbuf(vstack,10000);
	//if ( ! vbuf.reserve ( vneed ) ) return true;
	SafeBuf vbuf;
	// TODO: make this better
	if ( ! vbuf.reserve ( 10000 * 8 ) ) return true;

	//
	// NOTE: now we have only a sample and excerpts are separated
	// with |'s
	//


	// hash each \0 separated excerpt in bigSampleBuf
	char *p    = bigSampleBuf;
	// most samples are under 5k, i've seend a 32k sample take 11ms!
	char *pend = p + bigSampleLen;

	// compile all \0 terminated excerpts into a single vector for this
	// docid
	while ( p < pend ) {
		// debug
		//log("docId=%"INT64" EXCERPT=%s",docId,p);
		int32_t plen = gbstrlen(p);
		// parse into words
		Words ww;
		ww.setx ( p, plen, 0);// niceness
		// save it
		//log("gbits: getting sim for %s",p);
		// advance to next excerpt
		p += plen + 1;
		// p is only non-NULL if we are doing it the old way
		// 'tg' indicates where the gigabits came from, like the
		// body, or a particular meta tag.
		// 'repeatTable' is for counting the same word
		hashExcerpt ( q, 
			      &localGigabitTable, 
			      ww,
			      tg,
			      repeatTable , 
			      thisMsg20 ,
			      debugGigabits );
		// . skip if not deduping
		// . if a sample is too similar to another sample then we
		//   do not allow its gigabits to vote. its considered too
		//   spammy.
		if ( tg->m_dedupSamplePercent <= 0 ) continue;
		// make a vector out of words
		int64_t *wids = ww.getWordIds();
		int32_t nw = ww.getNumWords();
		// put all the words from this sample into simTable hash table
		// and just make vbuf a list of the unique wordIds from all
		// gigabit samples this docid provides.
		for ( int32_t i = 0 ; i < nw ; i++ ) {
			// convert word to a number
			uint32_t widu = (uint64_t)(wids[i]);
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
	vbuf.truncLen(((int32_t)SAMPLE_VECTOR_SIZE) - 4);
	// make last int32_t a 0
	vbuf.pushLong(0);
	// now vbuf is a fairly decent vector of words that represent
	// the gigabit sample for this docid. see if it is already
	// too similar to ones we've stored in "vecBuf" which has all the
	// saples from all the other docids that were considered 
	// mutually distinct enough.

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
		int32_t numVecs = vecBuf->length()/(int32_t)SAMPLE_VECTOR_SIZE;
		char *v2 = vecBuf->getBufStart();
		// see if our vector is too similar
		for ( int32_t i = 0 ; i < numVecs ; i++ ) {
			char ss;
			ss = g_clusterdb.getSampleSimilarity(v1,v2, 
							  SAMPLE_VECTOR_SIZE);
			v2 += SAMPLE_VECTOR_SIZE;
			// return true if too similar to another sample we did
			if ( ss >= tg->m_dedupSamplePercent ) { // 80 ) {
				localGigabitTable.reset();
				// log(LOG_DEBUG,"gbits: removed dup sample "
				//     "\"%s\" too similar to sample #%i"
				//     , bigSampleBuf
				//     , i
				//     );
				return true;
			}
		}
		// this docid sample as considered unique enough with respect
		// to the other samples from other docids, so add the
		// wordids to our list to dedup the next excerpts
		vecBuf->safeMemcpy(v1,(int32_t)SAMPLE_VECTOR_SIZE);

		// log(LOG_DEBUG,"gbits: adding unique sample #%i %s "
		//     ,numVecs,bigSampleBuf);

	}

	//log("TOOK %"INT64" ms plen=%"INT32"",gettimeofdayInMilliseconds()-start,
	//    bufLen);

	//log("have %"INT32" terms in termtable. adding to master.",
	//     tt.getNumTermsUsed());


	// . now hash the entries of this table, tt, into the master
	// . the master contains entries from all the other tables
	int32_t nt = localGigabitTable.getNumSlots();
	//int32_t pop = 0 ;
	for ( int32_t i = 0 ; i < nt ; i++ ) {
		// skip if empty
		if ( localGigabitTable.isEmpty(i) ) continue;
		// get it
		Gigabit *gc = (Gigabit *)localGigabitTable.getDataFromSlot(i);
		// this should be indented
		if ( ! gc->m_gbscore ) continue;//tt.m_scores[i] ) continue;
		//int32_t ii = (int32_t)tt.getTermPtr(i);
		// then divide by that
		//int32_t score =gc->m_scoreFromTermNum;//tt.getScoreFromTermNum(i
		// watch out for 0
		//if ( score <= 0 ) continue;
		// get termid
		int64_t termId64 = *(int64_t *)localGigabitTable.getKey(i);
		// . get the bucket
		// . may be or may not be full (score is 0 if empty)
		//int32_t n = master->getTermNum ( tt.getTermId(i) );
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
			gbmemcpy((char *)gbit.m_wordIds,
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
		int32_t  wwlen = pg->m_termLen;
		char c      = ww[wwlen];
		ww[wwlen]='\0';
		logf(LOG_DEBUG,"gbits: master "
		     "termId=%020"UINT64" "
		     "d=%018"INT64" "
		     "score=%7.1f "
		     "cumscore=%7.1f "
		     "pages=%"INT32" "
		     "len=%02"INT32" term=%s",
		     termId64,
		     docId,
		     gc->m_gbscore, // this time score
		     pg->m_gbscore, // cumulative score
		     pg->m_numPages,
		     wwlen,
		     ww);
		ww[wwlen]=c;
	}

	//log("master has %"INT32" terms",master.getNumTermsUsed());
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
	int32_t m_wpop;
	// is query term?
	bool m_isQueryTerm;
	// is common word? (do not let frags end in these words)
	bool m_isCommonWord;
	// the raw QTR scores (aac)
	float m_proxScore;//qtr;
	// a hash for looking up in the popularity dictionary
	//int64_t dwid64;
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
	int32_t maxWordsPerPhrase  = tg->m_maxWordsPerTopic ;
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
	int32_t nw = words.getNumWords();
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
	int32_t need = nw * sizeof(WordInfo);
	if ( ! wibuf.reserve ( need ) ) {
		log("gigabits: could not allocate local buffer "
		    "(%"INT32" bytes required)", need);
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
		int32_t m_pos[1000];
		int32_t m_posLen;
		int32_t m_posPtr;
	};
	SafeBuf posBuf;
	//int32_t need2 = MAX_QUERY_TERMS * sizeof(PosInfo);
	int32_t need2 = q->m_numTerms * sizeof(PosInfo);
	posBuf.setLabel("m40posbuf");
	if ( ! posBuf.reserve ( need2 ) ) {
		log("gigabits: could not allocate 2 local buffer "
		    "(%"INT32" bytes required)", need2);
		return;
	}
	PosInfo *pis = (PosInfo *)posBuf.getBufStart();
	for (int32_t i = 0; i < q->m_numTerms ; i++) {
		pis[i].m_posLen = 0; 
		pis[i].m_posPtr = 0; 
	}



	// start parsing at word #0 in the excerpt
	int32_t i  = 0;
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
	int32_t  *wlen = words.m_wordLens;
	int64_t *wids = words.getWordIds();

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
		//	wi->m_isCommonWord = isCommonWord ( (int32_t)wids[i] );
		//else                            
		//	wi->m_isCommonWord = 0;
		//#endif
		// debug msg
		/*
		char *s    = ww.getWord(i);
		int32_t  slen = ww.getWordLen(i);
		char  c    = s[slen];
		s[slen]='\0';
		log("icw=%"INT32" %s",icw[i],s);
		s[slen]=c;
		*/
		// is it a query term? if so, record its word # in "pos" arry
		int32_t nt = q->m_numTerms;
		for ( int32_t j = 0 ; j < nt ; j++ ) {
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
	//int32_t maxScore = nqi * MAX_SCORE_MULTIPLIER;
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
		int32_t j ;

		// number of matches
		int32_t nm = 0; 

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
			//int32_t d1 = i - pos[ 1000 * j + posPtr[j] ] ;
			// . posPtr is like a cursor into our m_pos array
			//   that has the word #'s that this query word
			//   matches in the excerpt
			// . "d1" is distance in words from word #i to
			//   the next closest query term
			int32_t d1 = i - pe->m_pos[pe->m_posPtr];
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
				    // common word, query terms, int16_t words
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
			//int32_t d2 = pos[ 1000 * j + posPtr[j] + 1 ] - i ;
			// look at the next occurence of query term #j
			// in the excerpt and get dist from us to it
			int32_t d2 = pe->m_pos[pe->m_posPtr + 1] - i;
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
				    // common word, query terms, int16_t words
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
			    // common word, query terms, int16_t words
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
			//log("nm=%"INT32"",nm);
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
		msg.safePrintf("gbits: wordpos=%3"INT32" "
			       "repeatscore=%3"INT32" "
			       "wordproxscore=%6.1f word=",
			       i,
			       (int32_t)wi->m_repeatScore,
			       proxScore);
		msg.safeMemcpy(wp[i],wlen[i]);
		msg.pushChar(0);
		logf(LOG_DEBUG,"%s",msg.getBufStart());
		
	}


	//int32_t mm = 0;
	// reset word ptr again
	i = 0;
	// skip initial punct again
	if ( i < nw && words.isPunct(i) ) i++;

	int32_t wikiEnd = -1;

	//
	//
	// scan words again and add GIGABITS to term table "localGigabitTable"
	//
	//

	for ( ; i < nw ; i++ ) {
		// int16_tcut
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
		int32_t numWiki = g_wiki.getNumWordsInWikiPhrase ( i,&words );
		wikiEnd = i + numWiki;

		// point to the string of the word
		char *ww = wp[i];
		int32_t  wwlen = wlen[i];
		//int32_t  ss;
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
		int32_t minPop = 0x7fffffff;
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
		uint64_t  ph64 = 0;//wids[i]; // hash value
		// if first letter is upper case, double the score
		//if ( is_upper_a (ww.getWord(i)[0]) ) score <<= 1;

		// . loop through all phrases that start with this word
		// . up to 6 real words per phrase
		// . 'j' counts our 'words' which counts a $ of puncts as word
		int32_t jend    = i + maxWordsPerPhrase * 2; // 12;
		int32_t maxjend = jend ;
		if ( tg->m_topicRemoveOverlaps ) maxjend += 8;
		if ( jend    > nw ) jend    = nw;
		if ( maxjend > nw ) maxjend = nw;

		int32_t count = 0;
		int32_t nqc   = 0; // # common/query words in our phrase
		int32_t nhw   = 0; // # of "hot words" (contribute to score)

		//if ( wlen[i] == 8 && strncmp(wp[i],"Practice",8) == 0 )
		//	log("hey");

		int32_t jWikiEnd = -1;
		
		for ( int32_t j = i ; j < jend ; j++ ) {
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
			int32_t njw = g_wiki.getNumWordsInWikiPhrase ( j,&words );
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
				// . cut phrase int16_t if too much punct between
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
			//if ( tt->getScoreFromTermId((int64_t)h) > 0 )
			//	continue;
			// debug msg
			//char c     = ww[wwlen];
			//ww[wwlen]='\0';
			//fprintf(stderr,"tid=%"UINT32" score=%"INT32" pop=%"INT32" len=%"INT32" "
			// "repeat=%"INT32" term=%s\n",h,ss,pop,wwlen,
			//	repeatScores[i],ww);
			//ww[wwlen]=c;
			// include any ending or starting ( or )
			if ( i > 0 && ww[-1] == '(' ) { 
				// ensure we got a ')' somwhere before adding (
				for ( int32_t r = 0 ; r <= wwlen ; r++ )
					if ( ww[r]==')' ) {
						ww--; wwlen++; break; }
			}
			if ( i < nw && ww[wwlen] == ')' ) { 
				// we need a '(' somewhere before adding the )
				for ( int32_t r = 0 ; r <= wwlen ; r++ )
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
			//int32_t tn = tt->getTermNum((int64_t)h);
			//maxScore = ss;
			//if ( tn >= 0 ) {
			//	int32_t sc = tt->getScoreFromTermNum(tn);
			//	if ( sc > maxScore ) maxScore = sc;
			//}
			// . add it
			// . now store the popularity, too, so we can display
			//   it for the winning gigabits
			//if ( ! tt->addTerm ((int64_t)h,ss,maxScore,false,
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
			//int32_t ipop = (int32_t)(pop * MAXPOP);

			//
			// ADD A GIGABIT CANDIDATE
			//
			Gigabit gc;
			gc.m_term    = ww;
			gc.m_termLen = wwlen;
			gc.m_gbscore = popModScore;
			gc.m_minPop = minPop;

			// how many words in the gigabit?
			int32_t ngw = (j - i)/2 + 1;
			gc.m_numWords = ngw;

			// breach check. go to next gigabit beginning word?
			if ( ngw > MAX_GIGABIT_WORDS ) break;

			// record each word!
			int32_t wcount = 0;
			for ( int32_t k = i ; k <= j ; k++ ) {
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
					       "d=%018"UINT64" "
					       "termId=%020"UINT64" "
					       "popModScore=%7.1f "
					       //"wordProxSum=%7.1f "
					       "wordProxMax=%7.1f "
					       "nhw=%2"INT32" "
					       "minWordPopBoost=%2.1f "
					       "minWordPop=%5"INT32" "
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


			// stop after indexing a word after a int32_t string of
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

	int32_t nw = words->getNumWords();

	// if no words, nothing to do
	if ( nw == 0 ) return;

	//char      *ptr      = repeatTable;
	//int32_t       numSlots = repeatTableNumSlots;
	//int64_t *hashes   = (int64_t *)ptr; ptr += numSlots * 8;
	//int32_t      *vals     = (int32_t      *)ptr; ptr += numSlots * 4;

	int64_t   ringWids [ 5 ];
	int32_t        ringPos  [ 5 ];
	int32_t        ringi = 0;
	int32_t        count = 0;
	int64_t   h     = 0;

	//int32_t numSlots = repeatTable->getNumSlots();

	// make the mask
	//uint32_t mask = numSlots - 1;

	// clear ring of hashes
	memset ( ringWids , 0 , 5 * sizeof(int64_t) );

	// for sanity check
	//int32_t lastStart = -1;

	// count how many 5-word sequences we match in a row
	int32_t matched    = 0;
	int32_t matchStart = -1;

	// reset
	for ( int32_t i = 0 ; i < nw ; i++ ) 
		wis[i].m_repeatScore = 100;


	// return until we fix the infinite loop bug
	//return;

	int64_t *wids = words->getWordIds();

	// . hash EVERY 5-word sequence in the document
	// . if we get a match look and see what sequences it matches
	// . we allow multiple instances of the same hash to be stored in
	//   the hash table, so keep checking for a matching hash until you
	//   chain to a 0 hash, indicating the chain ends
	// . check each matching hash to see if more than 5 words match
	// . get the max words that matched from all of the candidates
	// . demote the word and phrase weights based on the total/max
	//   number of words matching
	for ( int32_t i = 0 ; i < nw ; i++ ) {
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
		int32_t start = ringPos[ringi];
		// need at least 5 words in the ring buffer to do analysis
		if ( ++count < 5 ) continue;
		// sanity check
		//if ( start <= lastStart ) { char *xx = NULL; *xx = 0; }
		// look up in the hash table
		//int32_t n = h & mask;
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
			int32_t val = matchStart+1;
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
			for ( int32_t j = matchStart ; j < i ; j++ ) 
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
	for ( int32_t j = matchStart ; j < nw ; j++ ) 
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

// . aka NUGGABITS
// . now make the fast facts from the gigabits and the samples. 
// . these are sentences containing the query and a gigabit.
// . sets m_factBuf
bool Msg40::computeFastFacts ( ) {

	// skip for now
	//return true;

	bool debugGigabits = m_si->m_debugGigabits;

	//
	// hash gigabits by first wordid and # words, and phrase hash
	//
	HashTableX gbitTable;
	char gbuf[30000];
	if ( ! gbitTable.set(8,sizeof(Gigabit *),1024,gbuf,30000,
			     false,0,"gbtbl") )
		return false;
	int32_t numGigabits = m_gigabitBuf.length()/sizeof(Gigabit);
	Gigabit *gigabits = (Gigabit *)m_gigabitBuf.getBufStart();
	for ( int32_t i = 0 ; i < numGigabits ; i++ ) {
		// get the ith gigabit
		Gigabit *gi = &gigabits[i];
		// parse into words
		Words ww;
		ww.setx ( gi->m_term , gi->m_termLen , 0 );
		int64_t *wids = ww.getWordIds();
		// fix mere here
		//if ( ! wids[0] ) { char *xx=NULL;*xx=0; }
		if ( ! wids[0] )  {
			log("doc: wids[0] is null");
			return true;
		}
		// . hash first word
		// . so gigabit has # words in it so we can do a slower
		//   compare function to make sure entire gigabit is matched
		//   in the code below
		if ( ! gbitTable.addKey ( &wids[0] , &gi ) ) return false;
	}

	//
	// hash the query terms we need to match into table as well
	//
	Query *q = &m_si->m_q;
	HashTableX queryTable;
	char qbuf[10000];
	if ( ! queryTable.set(8,sizeof(QueryTerm *),512,qbuf,
			      10000,false,0,"qrttbl") )
		return false;
	for ( int32_t i = 0 ; i < q->m_numTerms ; i++ ) {
		// int16_tcut
		QueryTerm *qt = &q->m_qterms[i];
		// skip if no weight!
		if ( qt->m_popWeight <= 0.0 ) continue;
		// use RAW termid
		if ( ! queryTable.addKey ( &qt->m_rawTermId, &qt ) ) 
			return false;
	}


	//
	// store Facts (sentences) into this safebuf (nuggets)(nuggabits)
	//
	char ftmp[100000];
	SafeBuf factBuf(ftmp,100000);
	// scan docs in search results
	for ( int32_t i = 0 ; i < m_msg3a.m_numDocIds ; i++ ) {
		// skip if not visible
		if ( m_msg3a.m_clusterLevels[i] != CR_OK ) continue;
		// get it
		Msg20* thisMsg20 = m_msg20[i];
		// must be there! wtf?
		Msg20Reply *reply = thisMsg20->getReply();
		// if m_si->m_showErrors is true then reply can be NULL
		// if titleRec was not found
		if ( ! reply ) return true;
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
			//log("docId=%"INT64" EXCERPT=%s",docId,p);
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
	int32_t numFacts = factBuf.getLength() / sizeof(Fact);
	Fact *facts = (Fact *)factBuf.getBufStart();
	SafeBuf ptrBuf;
	if ( ! ptrBuf.reserve( numFacts * sizeof(Fact *) ) ) return false;
	for ( int32_t i = 0 ; i < numFacts ; i++ ) {
		Fact *fi = &facts[i];
		ptrBuf.pushPtr ( fi );
	}
	Fact **ptrs = (Fact **)ptrBuf.getBufStart();
	gbqsort ( ptrs , numFacts , sizeof(Fact *) , factCmp , 0 );



	//
	// now dedup and set m_gigabitModScore to 0 if a dup fact!
	//
	int32_t need = 0;
	for ( int32_t i = 0 ; i < numFacts ; i++ ) {
		// get it
		Fact *fi = &facts[i];
		char *v1 = fi->m_dedupVector;
		int32_t vsize = SAMPLE_VECTOR_SIZE;
		// compare its dedup vector to the facts before us
		int32_t j; for ( j = 0 ; j < i ; j++ ) {
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
	for ( int32_t i = 0 ; i < numFacts ; i++ ) {
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

	int32_t nw = ww.getNumWords();
	int64_t *wids = ww.getWordIds();

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
	for ( int32_t i = 0 ; i < nw ; i++ ) {
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
			int32_t x = i + 2;
			int32_t k;
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
	for ( int32_t j = 0 ; j < nw ; j++ ) {
		// make it this
		uint32_t widu;
		widu = (uint64_t)(wids[j]);
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
	vbuf.truncLen(((int32_t)SAMPLE_VECTOR_SIZE) - 4);
	// make last int32_t a 0 so Clusterdb::getSimilarity() likes it
	vbuf.pushLong(0);
	// now store it in the Fact struct
	gbmemcpy ( fact.m_dedupVector , vbuf.getBufStart(), vbuf.length() );


	// otherwise, add it
	if ( ! factBuf->safeMemcpy ( &fact , sizeof(Fact) ) ) return false;
	return true;
}


// . printSearchResult into "sb"
bool Msg40::printSearchResult9 ( int32_t ix , int32_t *numPrintedSoFar ,
				 Msg20Reply *mr ) {

	// . we stream results right onto the socket
	// . useful for thousands of results... and saving mem
	if ( ! m_si || ! m_si->m_streamResults ) { char *xx=NULL;*xx=0; }

	// get state0
	State0 *st = (State0 *)m_state;

	//SafeBuf *sb = &st->m_sb;
	// clear it since we are streaming
	//sb->reset();

	Msg40 *msg40 = &st->m_msg40;

	// then print each result
	// don't display more than docsWanted results
	if ( m_numPrinted >= msg40->getDocsWanted() ) {
		// i guess we can print "Next 10" link
		m_moreToCome = true;
		// hide if above limit
		if ( m_printCount == 0 )
			log(LOG_INFO,"msg40: hiding above docsWanted "
			    "#%"INT32" (%"UINT32")(d=%"INT64")",
			    m_printi,mr->m_contentHash32,mr->m_docId);
		m_printCount++;
		if ( m_printCount == 100 ) m_printCount = 0;
		// do not exceed what the user asked for
		return true;
	}

	// prints in xml or html
	if ( m_si->m_format == FORMAT_CSV ) {
		printJsonItemInCSV ( st , ix );
		//log("print: printing #%"INT32" csv",(int32_t)ix);
	}
	// print that out into st->m_sb safebuf
	else if ( ! printResult ( st , ix , numPrintedSoFar ) ) {
		// oom?
		if ( ! g_errno ) g_errno = EBADENGINEER;
		log("query: had error: %s",mstrerror(g_errno));
		m_hadPrintError = true;
	}

	
	// log(LOG_INFO,"msg40: printing #%"INT32" (%"UINT32")(d=%"INT64")",
	//     m_printi,mr->m_contentHash32,mr->m_docId);

	// count it
	m_numPrinted++;

	return true;
}
	

bool printHttpMime ( State0 *st ) {

	SearchInput *si = &st->m_si;

	// grab the query
	//Msg40 *msg40 = &(st->m_msg40);
	//char  *q    = msg40->getQuery();
	//int32_t   qlen = msg40->getQueryLen();

  	//char  local[ 128000 ];
	//SafeBuf sb(local, 128000);
	SafeBuf *sb = &st->m_sb;
	// reserve 1.5MB now!
	if ( ! sb->reserve(1500000 ,"pgresbuf" ) ) // 128000) )
		return true;
	// just in case it is empty, make it null terminated
	sb->nullTerm();

	char *ct = "text/csv";
	if ( si->m_format == FORMAT_JSON )
		ct = "application/json";
	if ( si->m_format == FORMAT_XML )
		ct = "text/xml";
	if ( si->m_format == FORMAT_HTML )
		ct = "text/html";
	//if ( si->m_format == FORMAT_TEXT )
	//	ct = "text/plain";
	if ( si->m_format == FORMAT_CSV )
		ct = "text/csv";

	// . if we haven't yet sent an http mime back to the user
	//   then do so here, the content-length will not be in there
	//   because we might have to call for more spiderdb data
	HttpMime mime;
	mime.makeMime ( -1, // totel content-lenght is unknown!
			0 , // do not cache (cacheTime)
			0 , // lastModified
			0 , // offset
			-1 , // bytesToSend
			NULL , // ext
			false, // POSTReply
			ct, // "text/csv", // contenttype
			"utf-8" , // charset
			-1 , // httpstatus
			NULL ); //cookie
	sb->safeMemcpy(mime.getMime(),mime.getMimeLen() );
	return true;
}

/////////////////
//
// CSV LOGIC from PageResults.cpp
//
/////////////////

/*
// return 1 if a should be before b
static int csvPtrCmp ( const void *a, const void *b ) {
	//JsonItem *ja = (JsonItem **)a;
	//JsonItem *jb = (JsonItem **)b;
	char *pa = *(char **)a;
	char *pb = *(char **)b;
	if ( strcmp(pa,"type") == 0 ) return -1;
	if ( strcmp(pb,"type") == 0 ) return  1;
	// force title on top
	if ( strcmp(pa,"product.title") == 0 ) return -1;
	if ( strcmp(pb,"product.title") == 0 ) return  1;
	if ( strcmp(pa,"title") == 0 ) return -1;
	if ( strcmp(pb,"title") == 0 ) return  1;
	// otherwise string compare
	int val = strcmp(pa,pb);
	return val;
}
*/
	
#include "Json.h"

// 
// print header row in csv
//
bool Msg40::printCSVHeaderRow ( SafeBuf *sb ) {

	//Msg40 *msg40 = &st->m_msg40;
	//int32_t numResults = msg40->getNumResults();

	/*
	char tmp1[1024];
	SafeBuf tmpBuf (tmp1 , 1024);

	char nbuf[27000];
	HashTableX nameTable;
	if ( ! nameTable.set ( 8,4,2048,nbuf,27000,false,0,"ntbuf") )
		return false;

	int32_t niceness = 0;

	// . scan every fucking json item in the search results.
	// . we still need to deal with the case when there are so many
	//   search results we have to dump each msg20 reply to disk in
	//   order. then we'll have to update this code to scan that file.

	for ( int32_t i = 0 ; i < m_needFirstReplies ; i++ ) {

		Msg20 *m20 = getCompletedSummary(i);
		if ( ! m20 ) break;

		// unless they specified &showerrors=1 do not show
		// doc not found errors from a bad title rec lookup
		if ( m20->m_errno && ! m_si->m_showErrors ) 
			continue;

		if ( ! m20->m_r ) { char *xx=NULL;*xx=0; }

		Msg20Reply *mr = m20->m_r;

		// get content
		char *json = mr->ptr_content;
		// how can it be empty?
		if ( ! json ) continue;

		// parse it up
		Json jp;
		jp.parseJsonStringIntoJsonItems ( json , niceness );

		// scan each json item
		for ( JsonItem *ji = jp.getFirstItem(); ji ; ji = ji->m_next ){

			// skip if not number or string
			if ( ji->m_type != JT_NUMBER && 
			     ji->m_type != JT_STRING )
				continue;

			// if in an array, do not print! csv is not
			// good for arrays... like "media":[....] . that
			// one might be ok, but if the elements in the
			// array are not simple types, like, if they are
			// unflat json objects then it is not well suited
			// for csv.
			if ( ji->isInArray() ) 
				continue;

			// reset length of buf to 0
			tmpBuf.reset();

			// . get the name of the item into "nameBuf"
			// . returns false with g_errno set on error
			if ( ! ji->getCompoundName ( tmpBuf ) )
				return false;

			// skip the "html" column, strip that out now
			if ( strcmp(tmpBuf.getBufStart(),"html") == 0 )
				continue;

			// is it new?
			int64_t h64 = hash64n ( tmpBuf.getBufStart() );
			if ( nameTable.isInTable ( &h64 ) ) continue;

			// record offset of the name for our hash table
			int32_t nameBufOffset = nameBuf.length();
			
			// store the name in our name buffer
			if ( ! nameBuf.safeStrcpy ( tmpBuf.getBufStart() ) )
				return false;
			if ( ! nameBuf.pushChar ( '\0' ) )
				return false;

			// it's new. add it
			if ( ! nameTable.addKey ( &h64 , &nameBufOffset ) )
				return false;

		}
	}

	// . make array of ptrs to the names so we can sort them
	// . try to always put title first regardless
	char *ptrs [ 1024 ];
	int32_t numPtrs = 0;
	for ( int32_t i = 0 ; i < nameTable.m_numSlots ; i++ ) {
		if ( ! nameTable.m_flags[i] ) continue;
		int32_t off = *(int32_t *)nameTable.getValueFromSlot(i);
		char *p = nameBuf.getBufStart() + off;
		ptrs[numPtrs++] = p;
		if ( numPtrs >= 1024 ) break;
	}

	// sort them
	qsort ( ptrs , numPtrs , sizeof(char *) , csvPtrCmp );

	HashTableX *columnTable = &m_columnTable;
	if ( ! columnTable->set ( 8,4, numPtrs * 4,NULL,0,false,0,"coltbl" ) )
		return false;

	// now print them out as the header row
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {
		if ( i > 0 && ! sb->pushChar(',') ) return false;
		if ( ! sb->safeStrcpy ( ptrs[i] ) ) return false;
		// record the hash of each one for printing out further json
		// objects in the same order so columns are aligned!
		int64_t h64 = hash64n ( ptrs[i] );
		if ( ! columnTable->addKey ( &h64 , &i ) ) 
			return false;
	}
	*/

	Msg20 *msg20s[100];
	int32_t i;
	for ( i = 0 ; i < m_needFirstReplies && i < 100 ; i++ ) {
		Msg20 *m20 = getCompletedSummary(i);
		if ( ! m20 ) break;
		msg20s[i] = m20;
	}

	int32_t numPtrs = 0;

	char tmp2[1024];
	SafeBuf nameBuf (tmp2, 1024);

	int32_t ct = 0;
	if ( msg20s[0] && msg20s[0]->m_r ) ct = msg20s[0]->m_r->m_contentType;

	CollectionRec *cr =g_collectiondb.getRec(m_firstCollnum);

	// . set up table to map field name to col for printing the json items
	// . call this from PageResults.cpp 
	printCSVHeaderRow2 ( sb , 
			     ct ,
			     cr ,
			     &nameBuf ,
			     &m_columnTable ,
			     msg20s ,
			     i , // numResults ,
			     &numPtrs 
			     );

	m_numCSVColumns = numPtrs;

	if ( ! sb->pushChar('\n') )
		return false;
	if ( ! sb->nullTerm() )
		return false;

	return true;
}

// returns false and sets g_errno on error
bool Msg40::printJsonItemInCSV ( State0 *st , int32_t ix ) {

	int32_t niceness = 0;

	//
	// get the json from the search result
	//
	Msg20 *m20 = getCompletedSummary(ix);
	if ( ! m20 ) return false;
	if ( m20->m_errno ) return false;
	if ( ! m20->m_r ) { char *xx=NULL;*xx=0; }
	Msg20Reply *mr = m20->m_r;
	// get content
	char *json = mr->ptr_content;
	// how can it be empty?
	if ( ! json ) { char *xx=NULL;*xx=0; }


	// parse the json
	Json jp;
	jp.parseJsonStringIntoJsonItems ( json , niceness );

	HashTableX *columnTable = &m_columnTable;
	int32_t numCSVColumns = m_numCSVColumns;

	//SearchInput *si = m_si;
	SafeBuf *sb = &st->m_sb;

	
	// make buffer space that we need
	char ttt[1024];
	SafeBuf ptrBuf(ttt,1024);
	int32_t maxCols = numCSVColumns;
	// allow for additionals colls
	maxCols += 100;
	int32_t need = maxCols * sizeof(JsonItem *);
	if ( ! ptrBuf.reserve ( need ) ) return false;
	JsonItem **ptrs = (JsonItem **)ptrBuf.getBufStart();

	// reset json item ptrs for csv columns. all to NULL
	memset ( ptrs , 0 , need );

	char tmp1[1024];
	SafeBuf tmpBuf (tmp1 , 1024);

	JsonItem *ji;

	///////
	//
	// print json item in csv
	//
	///////
	for ( ji = jp.getFirstItem(); ji ; ji = ji->m_next ) {

		// skip if not number or string
		if ( ji->m_type != JT_NUMBER && 
		     ji->m_type != JT_STRING )
			continue;

		// skip if not well suited for csv (see above comment)
		if ( ji->isInArray() ) continue;

		// . get the name of the item into "nameBuf"
		// . returns false with g_errno set on error
		if ( ! ji->getCompoundName ( tmpBuf ) )
			return false;

		// is it new?
		int64_t h64 = hash64n ( tmpBuf.getBufStart() );

		// ignore the "html" column
		if ( strcmp(tmpBuf.getBufStart(),"html") == 0 ) continue;

		int32_t slot = columnTable->getSlot ( &h64 ) ;
		// MUST be in there
		// get col #
		int32_t column = -1;
		if ( slot >= 0 )
			column =*(int32_t *)columnTable->getValueFromSlot ( slot);

		// sanity
		if ( column == -1 ) {//>= numCSVColumns ) { 
			// don't show it any more...
			continue;
			// add a new column...
			int32_t newColnum = numCSVColumns + 1;
			// silently drop it if we already have too many cols
			if ( newColnum >= maxCols ) continue;
			columnTable->addKey ( &h64 , &newColnum );
			column = newColnum;
			numCSVColumns++;
			//char *xx=NULL;*xx=0; }
		}

		// set ptr to it for printing when done parsing every field
		// for this json item
		ptrs[column] = ji;
	}

	// now print out what we got
	for ( int32_t i = 0 ; i < numCSVColumns ; i++ ) {
		// , delimeted
		if ( i > 0 ) sb->pushChar(',');
		// get it
		ji = ptrs[i];
		// skip if none
		if ( ! ji ) continue;

		// skip "html" field... too spammy for csv and > 32k causes
		// libreoffice calc to truncate it and break its parsing
		if ( ji->m_name && 
		     //! ji->m_parent &&
		     strcmp(ji->m_name,"html")==0)
			continue;

		//
		// get value and print otherwise
		//
		/*
		if ( ji->m_type == JT_NUMBER ) {
			// print numbers without double quotes
			if ( ji->m_valueDouble *10000000.0 == 
			     (double)ji->m_valueLong * 10000000.0 )
				sb->safePrintf("%"INT32"",ji->m_valueLong);
			else
				sb->safePrintf("%f",ji->m_valueDouble);
			continue;
		}
		*/

		int32_t vlen;
		char *str = ji->getValueAsString ( &vlen );

		// print the value
		sb->pushChar('\"');
		// get the json item to print out
		//int32_t  vlen = ji->getValueLen();
		// truncate
		char *truncStr = NULL;
		if ( vlen > 32000 ) {
			vlen = 32000;
			truncStr = " ... value truncated because "
				"Excel can not handle it. Download the "
				"JSON to get untruncated data.";
		}
		// print it out
		//sb->csvEncode ( ji->getValue() , vlen );
		sb->csvEncode ( str , vlen );
		// print truncate msg?
		if ( truncStr ) sb->safeStrcpy ( truncStr );
		// end the CSV
		sb->pushChar('\"');
	}

	sb->pushChar('\n');
	sb->nullTerm();

	return true;
}

// this is a safebuf of msg20s for doing facet string lookups
Msg20 *Msg40::getUnusedMsg20 ( ) {

	// make a safebuf of 50 of them if we haven't yet
	if ( m_unusedBuf.getCapacity() <= 0 ) {
		if ( ! m_unusedBuf.reserve ( (int32_t)MAX2 * sizeof(Msg20) ) ) {
			return NULL;
		}
		Msg20 *ma = (Msg20 *)m_unusedBuf.getBufStart();
		for ( int32_t i = 0 ; i < (int32_t)MAX2 ; i++ ) {
			ma[i].constructor();
			ma[i].m_owningParent = (void *)this;
			ma[i].m_constructedId = 3;
			// if we don't update length then Msg40::resetBuf2() 
			// will fail to call Msg20::destructor on them
			m_unusedBuf.m_length += sizeof(Msg20);
		}
	}
		

	Msg20 *ma = (Msg20 *)m_unusedBuf.getBufStart();

	for ( int32_t i = 0 ; i < (int32_t)MAX2 ; i++ ) {
		// m_inProgress is set to false right before it
		// calls Msg20::m_callback which is gotSummaryWrapper()
		// so we should be ok with this
		if ( ma[i].m_inProgress ) continue;
		return &ma[i];
	}

	// how can this happen???
	char *xx=NULL;*xx=0; 
	return NULL;
}

static bool gotFacetTextWrapper ( void *state ) {
	Msg20 *m20 = (Msg20 *)state;
	Msg40 *THIS = (Msg40 *)m20->m_hack;
	THIS->gotFacetText(m20);
	return true;
}

void Msg40::gotFacetText ( Msg20 *msg20 ) {

	m_numMsg20sIn++;
	//log("msg40: numin=%"INT32"",m_numMsg20sIn);

	if ( ! msg20->m_r ) {
		log("msg40: msg20 reply is NULL");
		return;
	}

	char *buf = msg20->m_r->ptr_facetBuf;

	// null as well?
	if ( ! buf ) {
		log("msg40: ptr_facetBuf is NULL");
		// try to launch more msg20s
		lookupFacets();
		return;
	}

	char *p = buf;
	// skip query term string
	p += gbstrlen(p) + 1;
	// then <val32>,<str32>
	FacetValHash_t fvh = atoll(p);
	char *text = strstr ( p , "," );
	// skip comma. text could be truncated/ellipsis-sized
	if ( text ) text++;

	int32_t offset = m_facetTextBuf.length();
	m_facetTextBuf.safeStrcpy ( text );
	m_facetTextBuf.pushChar('\0');

	// initialize this if it needs it
	if ( m_facetTextTable.m_ks == 0 )
		m_facetTextTable.set(sizeof(FacetValHash_t),4,
				     64,NULL,0,false,0,"fctxtbl");

	// store in buffer
	m_facetTextTable.addKey ( &fvh , &offset );

	// try to launch more msg20s
	if ( ! lookupFacets() ) return;
}

// return false if blocked, true otherwise
bool Msg40::lookupFacets ( ) {

	if ( m_doneWithLookup ) return true;

	if ( !m_calledFacets ) {
		m_calledFacets = true;
		m_numMsg20sOut = 0;
		m_numMsg20sIn  = 0;
		m_j = 0;
		m_i = 0;
	}

	lookupFacets2();

	// if not done return false
	if ( m_numMsg20sOut > m_numMsg20sIn ) return false;

	m_doneWithLookup = true;

	// did nothing? return true so control resumes from where
	// lookupFacets() was called
	if ( m_numMsg20sOut == 0 ) return true;

	// hack: dec since gotSummaryWrapper incs this
	m_numReplies--;
	// . ok, we blocked, so call callback, etc.
	// . pretend we just got another summary
	gotSummaryWrapper ( this );

	return true;
}

void Msg40::lookupFacets2 ( ) {

	// scan each query term
	for ( ; m_i < m_si->m_q.getNumTerms() ; m_i++ ) {

		QueryTerm *qt = &m_si->m_q.m_qterms[m_i];
		// skip if not STRING facet. we don't need to lookup
		// numeric facets because we already have the # for compiling
		// and presenting on the search results page.
		if ( qt->m_fieldCode != FIELD_GBFACETSTR ) //&&
		     //qt->m_fieldCode != FIELD_GBFACETINT &&
		     //qt->m_fieldCode != FIELD_GBFACETFLOAT )
			continue;

		HashTableX *fht = &qt->m_facetHashTable;

		// now they are sorted in Msg3a.cpp
		int32_t *ptr = (int32_t *)qt->m_facetIndexBuf.getBufStart();
		int numPtrs = qt->m_facetIndexBuf.length()/sizeof(int32_t);

		// scan every value this facet has
		//for (  ; m_j < fht->getNumSlots() ; m_j++ ) {
		for (  ; m_j < numPtrs ; m_j++ ) {
			// skip empty slots
			//if ( ! fht->m_flags[m_j] ) continue;
			int32_t slot = ptr[m_j];
			// get hash of the facet value
			FacetValHash_t fvh ;
			fvh = *(int32_t *)fht->getKeyFromSlot(slot);
			//int32_t count = *(int32_t *)fht->getValFromSlot(j);
			// get the docid as well
			FacetEntry*fe=(FacetEntry *)fht->getValFromSlot(slot);
			// how many docids in the results had this valud?
			//int32_t      count = fe->m_count;
			// one of the docids that had it
			int64_t docId = fe->m_docId;

			// more than 50 already outstanding?
			if ( m_numMsg20sOut - m_numMsg20sIn >= MAX2 )
				// wait for some to come back
				return;

			// lookup docid that has this to get text
			Msg20 *msg20 = getUnusedMsg20();
			// wait if none available
			if ( ! msg20 ) return;

			// make the request
			Msg20Request req;
			req.m_docId = docId;
			// supply the query term so we know what to return.
			// it's either an xpath facet, a json/xml field facet
			// or a meta tag facet.
			SafeBuf tmp;
			tmp.safeMemcpy ( qt->m_term , qt->m_termLen );
			tmp.nullTerm();
			req. ptr_qbuf = tmp.getBufStart();
			req.size_qbuf = tmp.length() + 1; // include \0

			req.m_justGetFacets = true;
			// need to supply the hash of the facet value otherwise
			// if a doc has multiple values for a facet it always
			// returns the first one. so tell it we want this one.
			req.m_facetValHash  = fvh;

			msg20->m_hack = this;//(int32_t)this;

			req.m_state     = msg20;
			req.m_callback  = gotFacetTextWrapper;

			// TODO: fix this
			req.m_collnum = m_si->m_firstCollnum;

			// get it
			if ( ! msg20->getSummary ( &req ) ) {
				m_numMsg20sOut++;
				//log("msg40: numout=%"INT32"",m_numMsg20sOut);
				continue;
			}

			// must have been error otherwise
			log("facet: error getting text: %s",
			    mstrerror(g_errno));
		}
		// done! reset scan of inner loop
		m_j = 0;
	}
}

// this is new PageResults.cpp
bool replaceParm ( char *cgi , SafeBuf *newUrl , HttpRequest *hr ) ;

bool Msg40::printFacetTables ( SafeBuf *sb ) {

	char format = m_si->m_format;

	int32_t saved = sb->length();

        // If json, print beginning of json array
        if ( format == FORMAT_JSON ) {
                if ( m_si->m_streamResults ) {
                        // if we are streaming results in json, we may have hacked off
                        // the last ,\n so we need a comma to put it back
                        bool needComma = true;

                        // check if the last non-whitespace char in the
                        // buffer is a comma
                        for (int32_t i= sb->m_length-1; i >= 0; i--) {
                                char c = sb->getBufStart()[i];
                                if (c == '\n' || c == ' ') {
                                        // ignore whitespace chars
                                        continue;
                                }

                                // If the loop reaches this point, we have a
                                // non-whitespace char, so we break the loop
                                // either way
                                if (c == ',') {
                                        // last non-whitespace char is a comma,
                                        // so we don't need to add an extra one
                                        needComma = false;
                                }
                                break;
                        }

                        if ( needComma ) {
                                sb->safeStrcpy(",\n\n");
                        }
                }
                sb->safePrintf("\"facets\":[");
	}

        int numTablesPrinted = 0;
	for ( int32_t i = 0 ; i < m_si->m_q.getNumTerms() ; i++ ) {
		// only for html for now i guess
		//if ( m_si->m_format != FORMAT_HTML ) break;
		QueryTerm *qt = &m_si->m_q.m_qterms[i];
		// skip if not facet
		if ( qt->m_fieldCode != FIELD_GBFACETSTR &&
		     qt->m_fieldCode != FIELD_GBFACETINT &&
		     qt->m_fieldCode != FIELD_GBFACETFLOAT )
			continue;

		// if had facet ranges, print them out
		if ( printFacetsForTable ( sb , qt ) > 0 )
			numTablesPrinted++;
	}

        // If josn, print end of json array
        if ( format == FORMAT_JSON ) {
                if ( numTablesPrinted > 0 ) {
                        sb->m_length -= 2; // hack off trailing comma
			sb->safePrintf("],\n"); // close off json array
	        }
		// if no facets then do not print "facets":[]\n,
		else {
			// revert string buf to original length
			sb->m_length = saved;
			// and cap the string buf just in case
			sb->nullTerm();
		}
        }

	// if json, remove ending ,\n and make it just \n
	if ( format == FORMAT_JSON && sb->length() != saved ) {
		// remove ,\n
		sb->m_length -= 2;
		// make just \n
		sb->pushChar('\n');
		//sb->safePrintf("],\n");

		// search results will follow so put a comma here if not
		// streaming result. if we are streaming results we print
		// the facets after the results so we can take advantage
		// of the msg20 summary lookups we already did to get the
		// facet text.
		if ( ! m_si->m_streamResults ) 
			sb->safePrintf(",\n");
	}

	return true;
}

int32_t Msg40::printFacetsForTable ( SafeBuf *sb , QueryTerm *qt ) {

	//QueryWord *qw = qt->m_qword;
	//if ( qw->m_numFacetRanges > 0 )

	HashTableX *fht = &qt->m_facetHashTable;
	
	int32_t *ptrs = (int32_t *)qt->m_facetIndexBuf.getBufStart();
	int32_t numPtrs = qt->m_facetIndexBuf.length() / sizeof(int32_t);

	if ( numPtrs == 0 )
		return 0;

	int32_t numPrinted = 0;

	// now scan the slots and print out
	HttpRequest *hr = &m_si->m_hr;

	bool isString = false;
	bool isFloat  = false;
	bool isInt = false;
	if ( qt->m_fieldCode == FIELD_GBFACETSTR ) isString = true;
	if ( qt->m_fieldCode == FIELD_GBFACETFLOAT ) isFloat = true;
	if ( qt->m_fieldCode == FIELD_GBFACETINT   ) isInt = true;
	char format = m_si->m_format;
	// a new table for each facet query term
	bool needTable = true;

	// print out the dumps
	for ( int32_t x= 0 ; x < numPtrs ; x++ ) {
		// skip empty slots
		//if ( ! fht->m_flags[j] ) continue;
		int32_t j = ptrs[x];
		// this was originally 32 bit hash of the facet val
		// but now it is 64 bit i guess
		FacetValHash_t *fvh ;
		fvh = (FacetValHash_t *)fht->getKeyFromSlot(j);
		// we store how many docids had this value
		//int32_t count = *(int32_t *)fht->getValFromSlot(j);
		FacetEntry *fe;
		fe = (FacetEntry *)fht->getValueFromSlot(j);
		int32_t count = 0;
		int64_t allCount = 0;
		// could be empty if range had no values in it
		if ( fe ) {
			count = fe->m_count;
			allCount = fe->m_outsideSearchResultsCount;
		}

		char *text = NULL;

		char *termPtr = qt->m_term;
		int32_t  termLen = qt->m_termLen;
		if ( termPtr[0] == ' ' ) { termPtr++; termLen--; }
		if ( strncasecmp(termPtr,"gbfacetstr:",11)== 0 ) {
			termPtr += 11; termLen -= 11; }
		if ( strncasecmp(termPtr,"gbfacetint:",11)== 0 ) {
			termPtr += 11; termLen -= 11; }
		if ( strncasecmp(termPtr,"gbfacetfloat:",13)== 0 ) {
			termPtr += 13; termLen -= 13; }
		char tmpBuf[64];
		SafeBuf termBuf(tmpBuf,64);
		termBuf.safeMemcpy(termPtr,termLen);
		termBuf.nullTerm();
		char *term = termBuf.getBufStart();

		char tmp9[128];
		SafeBuf sb9(tmp9,128);

		QueryWord *qw= qt->m_qword;

			
		if ( qt->m_fieldCode == FIELD_GBFACETINT && 
		     qw->m_numFacetRanges == 0 ) {
			sb9.safePrintf("%"INT32"",(int32_t)*fvh);
			text = sb9.getBufStart();
		}

		if ( qt->m_fieldCode == FIELD_GBFACETFLOAT 
		     && qw->m_numFacetRanges == 0 ) {
			sb9.printFloatPretty ( *(float *)fvh );
			text = sb9.getBufStart();
		}

		int32_t k2 = -1;

		// get the facet range that this FacetEntry represents (int)
		for ( int32_t k = 0 ; k < qw->m_numFacetRanges; k++ ) {
			if ( qt->m_fieldCode != FIELD_GBFACETINT )
				break;
			if ( *(int32_t *)fvh < qw->m_facetRangeIntA[k])
				continue;
			if ( *(int32_t *)fvh >= qw->m_facetRangeIntB[k])
				continue;
			sb9.safePrintf("[%"INT32"-%"INT32")"
				       ,qw->m_facetRangeIntA[k]
				       ,qw->m_facetRangeIntB[k]
				       );
			text = sb9.getBufStart();
			k2 = k;
		}

		// get the facet range that this FacetEntry represents (float)
		for ( int32_t k = 0 ; k < qw->m_numFacetRanges; k++ ) {
			if ( qt->m_fieldCode != FIELD_GBFACETFLOAT )
				break;
			if ( *(float *)fvh < qw->m_facetRangeFloatA[k])
				continue;
			if ( *(float *)fvh >= qw->m_facetRangeFloatB[k])
				continue;
			sb9.pushChar('[');
			sb9.printFloatPretty(qw->m_facetRangeFloatA[k]);
			sb9.pushChar('-');
			sb9.printFloatPretty(qw->m_facetRangeFloatB[k]);
			sb9.pushChar(')');
			sb9.nullTerm();
			text = sb9.getBufStart();
			k2 = k;
		}


		// lookup the text representation, whose hash is *fvh
		if ( qt->m_fieldCode == FIELD_GBFACETSTR ) {
			int32_t *offset;
			offset =(int32_t *)m_facetTextTable.getValue(fvh);
			// wtf?
			if ( ! offset ) {
				log("msg40: missing facet text for "
				    "val32=%"UINT32"",
				    (uint32_t)*fvh);
				continue;
			}
			text = m_facetTextBuf.getBufStart() + *offset;
		}


		if ( format == FORMAT_XML ) {
			numPrinted++;
			sb->safePrintf("\t<facet>\n"
				       "\t\t<field>%s</field>\n"
				       , term );
			sb->safePrintf("\t\t<totalDocsWithField>%"INT64""
				       "</totalDocsWithField>\n"
				       , qt->m_numDocsThatHaveFacet );
			sb->safePrintf("\t\t<totalDocsWithFieldAndValue>"
				       "%"INT64""
				       "</totalDocsWithFieldAndValue>\n"
				       , allCount );
			sb->safePrintf("\t\t<value>");

			if ( isString )
				sb->safePrintf("<![CDATA[%"UINT32",",
					       (uint32_t)*fvh);
			sb->cdataEncode ( text );
			if ( isString )
				sb->safePrintf("]]>");
			sb->safePrintf("</value>\n");
			sb->safePrintf("\t\t<docCount>%"INT32""
				       "</docCount>\n"
				       ,count);
			// some stats now for floats
			if ( isFloat && fe->m_count ) {
				sb->safePrintf("\t\t<average>");
				double sum = *(double *)&fe->m_sum;
				double avg = sum/(double)fe->m_count;
				sb->printFloatPretty ( (float)avg );
				sb->safePrintf("\t\t</average>\n");
				sb->safePrintf("\t\t<min>");
				float min = *(float *)&fe->m_min;
				sb->printFloatPretty ( min );
				sb->safePrintf("</min>\n");
				sb->safePrintf("\t\t<max>");
				float max = *(float *)&fe->m_max;
				sb->printFloatPretty ( max );
				sb->safePrintf("</max>\n");
			}
			// some stats now for ints
			if ( isInt && fe->m_count ) {
				sb->safePrintf("\t\t<average>");
				int64_t sum = fe->m_sum;
				double avg = (double)sum/(double)fe->m_count;
				sb->printFloatPretty ( (float)avg );
				sb->safePrintf("\t\t</average>\n");
				sb->safePrintf("\t\t<min>");
				int32_t min = fe->m_min;
				sb->safePrintf("%"INT32"</min>\n",min);
				sb->safePrintf("\t\t<max>");
				int32_t max = fe->m_max;
				sb->safePrintf("%"INT32"</max>\n",max);
			}
			sb->safePrintf("\t</facet>\n");
			continue;
		}

		// print that out
		if ( needTable && format == FORMAT_HTML ) {
			needTable = false;

			sb->safePrintf("<div id=facets "
				       "style="
				       "padding:5px;"
				       "position:relative;"
				       "border-width:3px;"
				       "border-right-width:0px;"
				       "border-style:solid;"
				       "margin-left:10px;"
				       "border-top-left-radius:10px;"
				       "border-bottom-left-radius:10px;"
				       "border-color:blue;"
				       "background-color:white;"
				       "border-right-color:white;"
				       "margin-right:-3px;"
				       ">"

				       "<table cellspacing=7>"
				       "<tr><td width=200px; "
				       "valign=top>"
				       "<center>"
				       "<img src=/facets40.jpg>"
				       "</center>"
				       "<br>"
				       );
			sb->safePrintf("<font color=gray>"
				       "values for</font> "
				       "<b>%s</b></td></tr>\n",
				       term);
		}


		if ( format == FORMAT_JSON ) {
			numPrinted++;
			sb->safePrintf("{\n"
				       "\t\"field\":\"%s\",\n"
				       , term 
				       );
			sb->safePrintf("\t\"totalDocsWithField\":%"INT64""
				       ",\n", qt->m_numDocsThatHaveFacet );
			sb->safePrintf("\t\"totalDocsWithFieldAndValue\":"
				       "%"INT64""
				       ",\n", 
				       allCount );
			sb->safePrintf("\t\"value\":\"");

			if (  isString )
				sb->safePrintf("%"UINT32","
					       , (uint32_t)*fvh);
			sb->jsonEncode ( text );
			//if ( isString )
			// just use quotes for ranges like "[1-3)" now
			sb->safePrintf("\"");
			sb->safePrintf(",\n");

			sb->safePrintf("\t\"docCount\":%"INT32""
				       , count );
			// if it's a # then we print stats after
			if ( isString || fe->m_count == 0 )
				sb->safePrintf("\n");
			else
				sb->safePrintf(",\n");
				

			// some stats now for floats
			if ( isFloat && fe->m_count ) {
				sb->safePrintf("\t\"average\":");
				double sum = *(double *)&fe->m_sum;
				double avg = sum/(double)fe->m_count;
				sb->printFloatPretty ( (float)avg );
				sb->safePrintf(",\n");
				sb->safePrintf("\t\"min\":");
				float min = *(float *)&fe->m_min;
				sb->printFloatPretty ( min );
				sb->safePrintf(",\n");
				sb->safePrintf("\t\"max\":");
				float max = *(float *)&fe->m_max;
				sb->printFloatPretty ( max );
				sb->safePrintf("\n");
			}
			// some stats now for ints
			if ( isInt && fe->m_count ) {
				sb->safePrintf("\t\"average\":");
				int64_t sum = fe->m_sum;
				double avg = (double)sum/(double)fe->m_count;
				sb->printFloatPretty ( (float)avg );
				sb->safePrintf(",\n");
				sb->safePrintf("\t\"min\":");
				int32_t min = fe->m_min;
				sb->safePrintf("%"INT32",\n",min);
				sb->safePrintf("\t\"max\":");
				int32_t max = fe->m_max;
				sb->safePrintf("%"INT32"\n",max);
			}

			sb->safePrintf("}\n,\n" );

			continue;
		}


		// make the cgi parm to add to the original url
		char nsbuf[128];
		SafeBuf newStuff(nsbuf,128);
		// they are all ints...
		//char *suffix = "int";
		//if ( qt->m_fieldCode == FIELD_GBFACETFLOAT )
		//	suffix = "float";
		//newStuff.safePrintf("prepend=gbequalint%%3A");
		if ( qt->m_fieldCode == FIELD_GBFACETINT &&
		     qw->m_numFacetRanges > 0 ) {
		     int32_t min = qw->m_facetRangeIntA[k2];
		     int32_t max = qw->m_facetRangeIntB[k2];
		     if ( min == max )
			     newStuff.safePrintf("prepend="
						 "gbequalint%%3A%s%%3A%"UINT32"+"
						 ,term
						 ,(int32_t)*fvh);
		     else
			     newStuff.safePrintf("prepend="
						 "gbminint%%3A%s%%3A%"UINT32"+"
						 "gbmaxint%%3A%s%%3A%"UINT32"+"
						 ,term
						 ,min
						 ,term
						 ,max-1
						 );
		}
		else if ( qt->m_fieldCode == FIELD_GBFACETFLOAT &&
			  qw->m_numFacetRanges > 0 ) {
			float min = qw->m_facetRangeFloatA[k2];
			float max = qw->m_facetRangeFloatB[k2];
			if ( min == max )
				newStuff.safePrintf("prepend="
						    "gbequalfloat%%3A%s%%3A%f+"
						    ,term
						    ,*(float *)fvh);
			else
			newStuff.safePrintf("prepend="
					    "gbminfloat%%3A%s%%3A%f+"
					    "gbmaxfloat%%3A%s%%3A%f+"
					    ,term
					    ,min
					    ,term
					    ,max
					    );
		}
		else if ( qt->m_fieldCode == FIELD_GBFACETFLOAT )
			newStuff.safePrintf("prepend="
					    "gbequalfloat%%3A%s%%3A%f",
					    term,
					    *(float *)fvh);
		else if ( qt->m_fieldCode == FIELD_GBFACETINT )
			newStuff.safePrintf("prepend="
					    "gbequalint%%3A%s%%3A%"UINT32"",
					    term,
					    (int32_t)*fvh);
		else if ( qt->m_fieldCode == FIELD_GBFACETSTR &&
			  // in XmlDoc.cpp the gbxpathsitehash123456: terms
			  // call hashFacets2() separately with val32 
			  // equal to the section inner hash which is not
			  // an exact hash of the string using hash32()
			  // unfortunately, so we can't use gbfieldmatch:
			  // which is case sensitive etc.
			  !strncmp(qt->m_term,
				   "gbfacetstr:gbxpathsitehash",26) )
			newStuff.safePrintf("prepend="
					    "gbequalint%%3Agbfacetstr%%3A"
					    "%s%%3A%"UINT32"",
					    term,
					    (int32_t)*fvh);
		else if ( qt->m_fieldCode == FIELD_GBFACETSTR ) {
			newStuff.safePrintf("prepend="
					    "gbfieldmatch%%3A%s%%3A%%22"
					    ,term
					    //"gbequalint%%3A%s%%3A%"UINT32""
					    //,(int32_t)*fvh
					    );
			newStuff.urlEncode(text);
			newStuff.safePrintf("%%22");
		}

		// get the original url and add 
		// &prepend=gbequalint:gbhopcount:1 type stuff to it
		SafeBuf newUrl;
		replaceParm ( newStuff.getBufStart(), &newUrl , hr );

		numPrinted++;

		// print the facet in its numeric form
		// we will have to lookup based on its docid
		// and get it from the cached page later
		sb->safePrintf("<tr><td width=200px; valign=top>"
			       //"<a href=?search="//gbfacet%3A"
			       //"%s:%"UINT32""
			       // make a search to just show those
			       // docs from this facet with that
			       // value. actually gbmin/max would work
			       "<a href=\"%s\">"
			       , newUrl.getBufStart()
			       );

		sb->safePrintf("%s (%"UINT32" documents)"
			       "</a>"
			       "</td></tr>\n"
			       ,text
			       ,count); // count for printing
	}

	if ( ! needTable && format == FORMAT_HTML ) 
		sb->safePrintf("</table></div><br>\n");

	return numPrinted;
}
