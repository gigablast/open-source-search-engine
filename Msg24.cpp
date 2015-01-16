#include "gb-include.h"

#include "Msg51.h"
//#include "Msg24.h"
#include "Query.h"
#include "Msg20.h"
//#include "TermTable.h"
#include "Words.h"
#include "Speller.h"
#include <math.h>
#include "StopWords.h"
#include "HashTable.h"
#include "Clusterdb.h"
#include "Scores.h"
#include "Stats.h"
#include "Words.h"

// here's the knobs:

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
#define SPARSE_MARK          0.34
#define SPARSE_PENALTY       1000
#define FWC_PENALTY          500   // penalty for begining with common word
#define POP_ZONE_0           0.00001
#define POP_ZONE_1           0.0001
#define POP_ZONE_2           0.001
#define POP_ZONE_3           0.01
#define POP_BOOST_0          3.0
#define POP_BOOST_1          1.5
#define POP_BOOST_2          1.0
#define POP_BOOST_3          0.3
#define POP_BOOST_4          0.1


//static bool onSamePages(int32_t i,int32_t j,int32_t *slots,int32_t *heads,int32_t *pages);

static void handleRequest24 ( UdpSlot *slot , int32_t netnice ) ;

static void setRepeatScores ( char      *repeatScores        ,
			      int64_t *wids                ,
			      int32_t       nw                  ,
			      char      *repeatTable         ,
			      int32_t       repeatTableNumSlots ,
			      Words     *words               ) ;

Msg24::Msg24 ( ) {
	m_numTopics = 0;
	m_request   = NULL;
	m_reply     = NULL;

	m_topicPtrs   = NULL;
	m_topicLens   = NULL;
	m_topicScores = NULL;
	m_topicGids   = NULL;
	m_topicPops   = NULL;
	m_topicDocIds = NULL;
	m_topicNumDocIds = NULL;
	m_isUnicode = false;
}

Msg24::~Msg24 ( ) { reset(); }
	

void Msg24::reset ( ) {
	if ( m_request && m_request != m_requestBuf ) 
		mfree ( m_request , m_requestSize , "Msg24" );
	m_request = NULL;
	// free reply if we should
	if ( m_reply ) mfree ( m_reply , m_replySize , "Msg24" );
	m_reply = NULL;
	m_isUnicode = false;
}


bool Msg24::registerHandler ( ) {
        // . register ourselves with the udp server
        // . it calls our callback when it receives a msg of type 0x24
        if ( ! g_udpServer.registerHandler ( 0x24, handleRequest24 )) 
                return false;
        return true;
}

static void gotReplyWrapper24 ( void *state1 , void *state2 ) ;

bool Msg24::generateTopics ( char       *coll                ,
			     int32_t        collLen             ,
			     char       *query               ,
			     int32_t        queryLen            ,
			     //float     termFreqWeights     ,
			     //float     phraseAffWeights    ,
			     int64_t  *docIds              ,
			     char       *clusterLevels       ,
			     int32_t        numDocIds           ,
			     TopicGroup  *topicGroups        ,
			     int32_t         numTopicGroups     ,
			     //int32_t        docsToScanForTopics ,
			     //int32_t        minTopicScore       ,
			     //int32_t        maxTopics           ,
			     //int32_t        maxWordsPerPhrase   ,
			     int32_t        maxCacheAge         ,
			     bool        addToCache          ,
			     bool        returnDocIdCount    ,
			     bool        returnDocIds        ,
			     bool        returnPops          ,
			     void       *state               ,
			     void     (* callback) (void *state ),
			     int32_t        niceness) {
	// force it to be true, since hi bit is set in pops if topic is unicode
	returnPops       = true;
	// warning
	if ( ! coll ) log(LOG_LOGIC,"net: NULL collection. msg24.");
	// force it
	returnDocIdCount = true;
	// if we don't get docids, then deserialize doesn't work because it
	// expects the docids to be valid.
	returnDocIds     = true;
	// reset
	m_numTopics = 0;
	//m_docsToScanForTopics = docsToScanForTopics;
	//m_minTopicScore       = minTopicScore;
	//m_maxTopics           = maxTopics;
	m_numDocIds          = numDocIds;
	m_coll               = coll;
	m_collLen            = collLen;
	m_returnDocIdCount   = returnDocIdCount;
	m_returnDocIds       = returnDocIds;
	m_returnPops         = returnPops;
	// bail if no operations to do
	if ( numTopicGroups <= 0 ) return true;
	if ( numDocIds      <= 0 ) return true;

	int32_t numTopicsToGen = topicGroups->m_numTopics;
	// get the min we have to scan
	int32_t docsToScanForTopics = topicGroups[0].m_docsToScanForTopics;

	for ( int32_t i = 1 ; i < numTopicGroups ; i++ ) {
		int32_t x = topicGroups[i].m_docsToScanForTopics ;
		if ( x > docsToScanForTopics ) docsToScanForTopics = x;

		if ( topicGroups[i].m_numTopics > numTopicsToGen )
			numTopicsToGen = topicGroups[i].m_numTopics;
	}
	// bail if none
	if ( docsToScanForTopics <= 0 ) return true;
	if ( numTopicsToGen == 0      ) return true;

	m_state      = state;
	m_callback   = callback;

	m_startTime = gettimeofdayInMilliseconds();

	// save, caller should not delete this!
	m_topicGroups    = topicGroups;
	m_numTopicGroups = numTopicGroups;
	// truncate
	//if ( maxTopics > MAX_TOPICS ) maxTopics = MAX_TOPICS;
	// truncate
	//if ( numDocIds > MAX_DOCIDS_TO_SCAN )
	//	numDocIds = MAX_DOCIDS_TO_SCAN ;
	if ( numDocIds > docsToScanForTopics )
		numDocIds = docsToScanForTopics ;

	int32_t size = sizeof(TopicGroup) * numTopicGroups ;
	if ( queryLen > MAX_QUERY_LEN ) queryLen = MAX_QUERY_LEN;

	// how much space do we need?
	int32_t need = 4+4+4+size+
		queryLen+1+ 
		numDocIds*8 + 
		numDocIds +collLen+1 + sizeof(niceness);
	m_requestSize = need;

	// make enough room for the request
	if ( need < MSG24_REQUEST_SIZE ) m_request = m_requestBuf;
	else {
		m_request = (char *)mmalloc ( need , "Msg24a" );
		if ( ! m_request ) {
			log("topics: Failed to allocate %"INT32" bytes.",need);
			return true;
		}
	}

	char *p = m_request;
	// store the cache parms
	*(int32_t *)p = maxCacheAge        ; p += 4;
	*(char *)p = addToCache         ; p += 1;
	*(char *)p = returnDocIdCount   ; p += 1;
	*(char *)p = returnDocIds       ; p += 1;
	*(char *)p = returnPops         ; p += 1;
	*(int32_t *)p = niceness           ; p += sizeof(int32_t);
	// store minTopicScore
	//*(int32_t *)p = minTopicScore     ; p += 4;
	//*(int32_t *)p = maxTopics         ; p += 4;
	//*(int32_t *)p = maxWordsPerPhrase ; p += 4;
	// store topic group information
	*(int32_t *)p = numTopicGroups; p += 4;
	gbmemcpy ( p , topicGroups , size ); p += size;
	// then coll
	gbmemcpy ( p , coll , collLen ); p += collLen ;
	*p++ = '\0';
	// then query
	gbmemcpy ( p , query , queryLen ); p += queryLen;
	*p++ = '\0';
	// then docids
	gbmemcpy ( p , docIds , numDocIds * 8 ); p += numDocIds * 8;
	// then cluster levels
	gbmemcpy ( p , clusterLevels , numDocIds ); p += numDocIds ;
	// how big is it?
	//m_requestSize = p - m_request;
	// sanity check
	//if ( m_requestSize > 5+MAX_QUERY_LEN + 1 + MAX_DOCIDS_TO_SCAN * 9){
	//	char *xx = NULL ; *xx = 0; }
	if ( p - m_request != m_requestSize ) {
		log("Bad msg24 request size");
		char *xx = NULL ; *xx = 0; 
	}
	// . the groupId to handle... just pick randomly
	int32_t groupId = ((uint32_t)docIds[0]) & g_hostdb.m_groupMask;
	// . returns false and sets g_errno on error
	// . reply should be stored in UdpSlot::m_tmpBuf
        if ( ! m_mcast.send ( m_request       , 
			      m_requestSize   , 
			      0x24            , // msgType 0x24
			      false           , // m_mcast own m_request?
			      groupId         , // send to group (groupKey)
			      false           , // send to whole group?
			      (int32_t)docIds[0] , // key is lower bits of docId
			      this            , // state data
			      NULL            , // state data
			      gotReplyWrapper24 ,
			      30              , // 30 second time out
			      niceness        , // niceness
			      false           , // realtime?
			      -1              , // first hostid
			      NULL,//m_reply    , // store reply in here
			      0,//MAX_REPLY_LEN , // this is how big it can be
			      false           , // free reply buf?
			      false           , // do disk load balancing?
			      0               , // maxCacheAge
			      (key_t)0        , // cacheKey
			      RDB_NONE        , // TITLEDB // rdbId of titledb
			      0             ) ){// minRecSizes avg
		log("topics: Had error sending request for topics to host in "
		    "group #%"INT32": %s.",groupId,mstrerror(g_errno));
		return true;	
	}
	// otherwise, we blocked and gotReplyWrapper will be called
	return false;
}

void gotReplyWrapper24 ( void *state1 , void *state2 ) {
	Msg24 *THIS = (Msg24 *)state1;
	THIS->gotReply();
	THIS->m_callback ( THIS->m_state );
}

void Msg24::gotReply ( ) {
	// bail on error, multicast will free the reply buffer if it should
	if ( g_errno ) {
		log("topics: Had error getting topics: %s.",
		    mstrerror(g_errno));
		return;
	}
	// get the reply
	int32_t  maxSize   ;
	bool  freeIt    ;
	m_reply = m_mcast.getBestReply (&m_replySize, &maxSize, &freeIt);	
	relabel( m_reply, m_replySize, "Msg24-GBR" );
	// sanity check
	//if ( reply != m_reply ) { char *xx = NULL ; *xx = 0 ; }
	// . parse the reply, it should be our m_reply buffer
	// . topics are NULL terminated
	deserialize ( m_reply , m_replySize );

	int64_t now  = gettimeofdayInMilliseconds();
	g_stats.addStat_r ( 0           ,
			    m_startTime , 
			    now,
			    "get_gigabits",
			    0x00d1e1ff ,
			    STAT_QUERY );
	/*
	int32_t  i = 0;
	while ( p < pend && i < MAX_TOPICS ) {
		m_topicScores[i] = *(int32_t *)p ; p += 4;
		m_topicLens  [i] = *(int32_t *)p ; p += 4;
		m_topicGids  [i] = *(char *)p ; p += 1;
		m_topicPtrs  [i] = p          ; p += m_topicLens[i] + 1;
		i++;
	}
	m_numTopics = i;
	*/
}

// if this is too big we can run out of sockets to use to launch
#define MAX_OUTSTANDING 50

State24::State24 ( ) { 
	m_msg20 = NULL; 
	m_mem = NULL;
	m_memPtr = NULL;
	m_memEnd = NULL;

}
State24::~State24 ( ) {
	if ( m_msg20 == m_buf20 ) return;
	for ( int32_t i = 0 ; i < m_numDocIds ; i++ ) m_msg20[i].destructor();
	mfree ( m_msg20 , sizeof(Msg20) * m_numDocIds , "Msg24" );
	m_msg20 = NULL;
	if ( m_mem ) {
		mfree ( m_mem, m_memEnd - m_mem, "Msg24" );
		m_mem    = NULL;
		m_memEnd = NULL;
		m_memPtr = NULL;
	}
}


static void launchMsg20s     ( State24 *st, bool callsample, int32_t sampleSize );
static void gotSampleWrapper ( void *state ) ;

void handleRequest24 ( UdpSlot *slot , int32_t netnice ) {
	// if niceness is 0, use the higher priority udpServer
	UdpServer *us = &g_udpServer;
	//if ( niceness == 0 ) us = &g_udpServer2;
	// make the state
	State24 *st ;
	try { st = new (State24); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("topics: Could not allocate %i bytes for generating "
		    "topics. Replying with error.",sizeof(State24));
		us->sendErrorReply ( slot , EBADREQUESTSIZE );
		return;
	}
	mnew ( st , sizeof(State24) , "Msg24b" );
	// get the request
	char *request     = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;
	char *requestEnd  = request + requestSize;
	// parse the request
	char *p = request;
	// get cache parms
	//int32_t maxCacheAge = *(int32_t *)p ; p += 4;
	//char addToCache  = *(char *)p ; p += 1;
	st->m_maxCacheAge        = *(int32_t *)p ; p += 4;
	st->m_addToCache         = *(char *)p ; p += 1;
	st->m_returnDocIdCount   = *(char *)p ; p += 1;
	st->m_returnDocIds       = *(char *)p ; p += 1;
	st->m_returnPops         = *(char *)p ; p += 1;
	st->m_niceness           = *(int32_t *)p ; p += sizeof(int32_t);
	// first is minTopicScore
	//int32_t minTopicScore     = *(int32_t *)p ; p += 4;
	// until we roll to all hosts, lets keep the protocol standard
	//int32_t maxTopics         = *(int32_t *)p ; p += 4;
	//int32_t maxWordsPerPhrase = *(int32_t *)p ; p += 4;
	//int32_t maxTopics         = 100;
	//int32_t maxWordsPerPhrase = 6;
	//st->m_minTopicScore     = minTopicScore;
	//st->m_maxTopics         = maxTopics;
	//st->m_maxWordsPerPhrase = maxWordsPerPhrase;
	// get topic group information
	st->m_numTopicGroups = *(int32_t *)p ; p += 4;
	int32_t size = sizeof(TopicGroup) * st->m_numTopicGroups ;
	gbmemcpy ( st->m_topicGroups , p , size ); p += size;
	// then coll
	st->m_coll = p; p += strlen(p) + 1;
	// . then the query, a NULL terminated string
	// . store it in state
	int32_t qlen = strlen ( p );
	if ( qlen > MAX_QUERY_LEN ) qlen = MAX_QUERY_LEN;
	gbmemcpy ( st->m_query , p , qlen );
	st->m_query [ qlen ] = '\0';
	st->m_queryLen = qlen;
	p += qlen + 1;
	// then the docids
	//int64_t *docIds    = (int64_t *)p;
	//int32_t       numDocIds = (requestEnd - p) / 9;
	//p += numDocIds * 8;
	// cluster levels
	//char *clusterLevels = p;
	st->m_docIds    = (int64_t *)p;
	st->m_numDocIds = (requestEnd - p) / 9;
	p += st->m_numDocIds * 8;
	// cluster levels
	st->m_clusterLevels = p;

	// truncate
	//if ( numDocIds > MAX_DOCIDS_TO_SCAN ) 
	//	numDocIds = MAX_DOCIDS_TO_SCAN ;
	// see if anyone blocks at all
	//bool noBlock = true;
	// we haven't got any responses as of yet or sent any requests
	st->m_slot        = slot;
	//st->m_niceness    = 0; // niceness;
	st->m_numReplies  = 0;
	st->m_numRequests = 0;

	// allocate enough msg20s
	if ( st->m_numDocIds <= 50 ) 
		st->m_msg20 = st->m_buf20;
	else {
		st->m_msg20=(Msg20 *)mmalloc(sizeof(Msg20)*
					     st->m_numDocIds,"Msg24c");
		if ( ! st->m_msg20 ) {
			log("Msg24: alloc of msg20s for %"INT32" bytes failed",
			    sizeof(Msg20)*st->m_numDocIds);
			// prevent a core dump in Msg24::~Msg24
			st->m_numDocIds = 0;
			mdelete ( st , sizeof(State24) , "Msg24" );
			delete ( st );
			us->sendErrorReply ( slot , g_errno );
			return;
		}
		for ( int32_t i = 0 ; i < st->m_numDocIds ; i++ ) 
			st->m_msg20[i].constructor();
	}

	// set query if need be
	//Query qq;
	st->m_qq.set ( st->m_query , st->m_queryLen , NULL , 0, 2 , true );
	// make a display metas string to get content for out TopicGroups
	//char dbuf[1024];
	p    = st->m_dbuf;
	char *pend = st->m_dbuf + 1024;
	for ( int32_t i = 0 ; i < st->m_numTopicGroups ; i++ ) {
		TopicGroup *t = &st->m_topicGroups [ i ];
		int32_t tlen = strlen ( t->m_meta );
		if ( p + tlen + 1 >= pend ) break;
		if ( i > 0 ) *p++ = ' ';
		gbmemcpy ( p , t->m_meta , tlen );
		p += tlen;
	}
	//int32_t dbufLen = p - dbuf;
	st->m_dbufLen = p - st->m_dbuf;
	*p = '\0';
	st->m_n = 0;
	st->m_i = 0;
	launchMsg20s ( st , true ,st->m_topicGroups[0].m_topicSampleSize );
}

void launchMsg20s ( State24 *st , bool callsample , int32_t sampleSize ) {
	// launch all the msg20 to get big samples of each doc
	//int32_t n = 0;
	for ( ; st->m_i < st->m_numDocIds ; st->m_i++ ) {
		// skip if clustered out
		if ( st->m_clusterLevels[st->m_i] != CR_OK ) 
			continue;
		// wait for later if too many outstanding
		if ( st->m_numRequests - st->m_numReplies >= 
		     MAX_OUTSTANDING ) return;
		// use the jth slot if we should
		//if ( j >= 0 ) n = j;
		// save the msg index
		//st->m_msg20[n].m_n      =  n;
		//st->m_msg20[n].m_parent = st;
		// supply the display metas as the meta in our TopicGroups
		// . start up a Msg20 to get the relevant doc text
		// . this will return false if blocks
		// . a 32k sample takes 11ms to hash in hashSample() and
		//   most samples are below 5k anyway...
		Msg20 *mm = &st->m_msg20[st->m_n++];
		// set the summary request then get it!
		Msg20Request req;
		Query *q = &st->m_qq;
		//int32_t nt                = q->m_numTerms;
		req.ptr_qbuf             = q->getQuery();
		req.size_qbuf            = q->getQueryLen()+1;
		//req.ptr_termFreqs      = (char *)m_msg3a.m_termFreqs;
		//req.size_termFreqs     = 8 * nt;
		//req.ptr_affWeights     = (char *)m_msg3a.m_affWeights;
		//req.size_affWeights    = 4 * nt; // 4 = sizeof(float)
		req.ptr_coll             = st->m_coll;
		req.size_coll            = strlen(st->m_coll)+1;
		if ( st->m_dbufLen > 0 ) {
			req.ptr_displayMetas     = st->m_dbuf ;
			req.size_displayMetas    = st->m_dbufLen+1;
		}
		req.m_docId              = st->m_docIds[st->m_i];
		req.m_numSummaryLines    = 0;
		req.m_maxCacheAge        = st->m_maxCacheAge;
		req.m_wcache             = st->m_addToCache;
		req.m_state              = st;
		req.m_callback           = gotSampleWrapper;
		req.m_niceness           = st->m_niceness;
		//req.m_summaryMode      = m_si->m_summaryMode;
		req.m_boolFlag           = q->m_isBoolean; // 2 means auto?
		//req.m_allowPunctInPhrase = m_si->m_allowPunctInPhrase;
		//req.m_showBanned       = m_si->m_showBanned;
		//req.m_excludeLinkText  = m_si->m_excludeLinkText ;
		//req.m_hackFixWords     = m_si->m_hackFixWords    ;
		//req.m_hackFixPhrases   = m_si->m_hackFixPhrases  ;
		//req.m_includeCachedCopy= m_si->m_includeCachedCopy;//bigsm
		req.m_bigSampleRadius    = 100;
		req.m_bigSampleMaxLen    = sampleSize;
		if ( ! mm->getSummary ( &req )) {st->m_numRequests++;continue;}
#ifdef _OLDMSG20_
		if ( ! mm->getSummary ( &st->m_qq             ,
					NULL                  , // term freqs
					NULL                  , // aff weights
					st->m_docIds[st->m_i] ,
					1                     , // clusterLevel
					0                     , // # sum lines
					st->m_maxCacheAge     ,
					st->m_addToCache      ,
					st->m_coll            , // coll
					strlen(st->m_coll)    ,
					st                    , // state
					gotSampleWrapper      ,
					st->m_niceness        ,
					false                 , // root?
					st->m_dbuf            , // dt metas
					st->m_dbufLen         , // dtmetalen
					100                   , // smpl radius
					sampleSize           )){// smpl max
			st->m_numRequests++;
			// if just launching one, bail if this blocked
			//if ( j >= 0 ) return;
			continue;
		}
#endif
		// deal with an error
		if ( g_errno ) {
			// log it
			log("topics: Received error when getting "
			    "document with docId %"INT64": %s. Document will not "
			    "contribute to the topics generation.",
			    st->m_docIds[st->m_i],mstrerror(g_errno));
			// reset g_errno
			g_errno   = 0;
		}
		// . otherwise we got summary without blocking
		// . increment # of replies (instant reply) and results
		st->m_numReplies++; 
		st->m_numRequests++; 
		// if we were just launching one and it did not block, return
		//if ( j >= 0 ) return;
	}
	// did anyone block? if so, return false for now
	if ( st->m_numReplies < st->m_numRequests ) return ;
	// . otherwise, we got everyone, so go right to the merge routine
	// . returns false if not all replies have been received 
	// . returns true if done
	// . sets g_errno on error
	if ( callsample ) gotSampleWrapper ( st );
}

static bool hashSample ( Query *q, char *sample , int32_t sampleLen , 
			 TermTable *master, int32_t *nqiPtr , 
			 TopicGroup *t , 
			 State24* st,
			 int64_t docId ,
			 char *vecs , int32_t *numVecs ,
			 class Words *wordsPtr , class Scores *scoresPtr ,
			 bool isUnicode ,
			 char *repeatTable , int32_t repeatTableNumSlots ,
			 char language );

void gotSampleWrapper ( void *state ) {
	// get ptr to our state 24 class
	State24 *st = (State24 *)state;
	// if niceness is 0, use the higher priority udpServer
	UdpServer *us = &g_udpServer;
	//if ( st->m_niceness == 0 ) us = &g_udpServer2;
	//else                       us = &g_udpServer ;
	UdpSlot *slot = st->m_slot;
	// just bitch if there was an error, then ignore it
	if ( g_errno ) {
		log("topics: Had error getting document: %s. Document will "
		    "not contribute to the topics generation.",
		    mstrerror(g_errno));
		g_errno = 0;
	}
	// we got one
	st->m_numReplies++;
	// launch another request if we can
	// return if all done
	launchMsg20s ( st , false , st->m_topicGroups[0].m_topicSampleSize ) ;
	// wait for all replies to get here
	if ( st->m_numReplies < st->m_numRequests ) return;
	// get time now
	//int64_t now = gettimeofdayInMilliseconds();
	// . add the stat
	// . use purple for tie to get all summaries
	//g_stats.addStat_r ( 0           , 
	//		    m_startTime , 
	//		    now         ,
	//		    0x008220ff  );
	// timestamp log
	//int64_t startTime = gettimeofdayInMilliseconds();
	log(LOG_DEBUG,"topics: msg24: Got %"INT32" titleRecs.",// in %"INT64" ms",
	    st->m_numReplies );//, now - m_startTime );

	// set query
	//Query q;
	//q.set ( st->m_query , st->m_queryLen , NULL , 0 , 2/*auto*/, true);

	// . init table for up to about 5k total distinct pronouns & phrases
	// . it automatically grows by like 20% if it runs out of space
	// . only alloc space for linked lists if docid info is wanted
	TermTable master;
	if ( ! master.set ( 20000 , true , true , 
			    st->m_returnDocIdCount | st->m_returnDocIds ,
			    st->m_returnPops , true, false, NULL ) ) {
		mdelete ( st , sizeof(State24) , "Msg24" );
		delete ( st );
		log("topics: Could not allocate memory for topic generation.");
		us->sendErrorReply ( slot , ENOMEM );
		return ;
	}

	// timestamp log
	int64_t startTime = gettimeofdayInMilliseconds();

	// debug
	//char *pp = (char *)mmalloc ( 4 , "foo");
	//*(int32_t *)pp = 0;
	//us->sendReply_ass ( pp , 4 , pp , 4 , slot );
	//delete(st);
	//return;

	// store all topics (scores/gids) in this buffer
	//char buf [ 128*1024 ];
	//char *p    = buf;
	//char *pend = buf + 128*1024;
	char *buf     = NULL;
	int32_t  bufSize = 0;
	//for ( int32_t yyy = 0 ; yyy < 100 ; yyy++ ) {	master.clear();//mdw
	// loop over all topic groups
	for ( int32_t i = 0 ; i < st->m_numTopicGroups ; i++ ) {
		// get ith topic group descriptor
		TopicGroup *t = &st->m_topicGroups[i];
		// . generate topics for this topic group
		// . serialize them into "p"
		// . getTopics will realloc() this "buf" to exactly the size
		//   it needs
		getTopics ( st , t , &master , &st->m_qq , i , 
			    // getTopics will realloc this buffer
			    &buf , &bufSize , NULL , NULL , NULL );
		// clear master table each time
		if ( i + 1 < st->m_numTopicGroups ) master.clear();
	}
	//}

	// free mem now to avoid fragmentation
	master.reset();

	// if small enough, copy into slot's tmp buffer
	char *reply     = buf;
	int32_t  replySize = bufSize;
	// launch it
	us->sendReply_ass ( reply , replySize , reply , replySize , slot );
	mdelete ( st , sizeof(State24) , "Msg24" );
	delete ( st );

	// . on host0, this is 21.3 ms with a std.dev. of 17.5 using dsrt=30
	//   measured on log[b-d] with the limit of 4 words per "giga bit".
	// . now time with our new 6 word phrase maximum:
	//   sum = 1294.0  avg = 16.0 sdev = 10.8 ... our rewrite was faster!!
	//if ( g_conf.m_timingDebugEnabled )
	int64_t took = gettimeofdayInMilliseconds() - startTime ;
	if ( took > 1 ) 
		log(LOG_TIMING,"topics: Took %"INT64" ms to parse out topics.", 
		     took );
	// timing debug
	else log(LOG_TIMING,"topics: Took %"INT64" ms to parse out topics.", took);
}

class DocIdLink {
public:
	int64_t  m_docId;
	int32_t       m_next; // offset into st->m_mem to DocIdLink
};


// returns false and set g_errno on error, true otherwise
bool getTopics ( State24       *st        , 
		 TopicGroup    *t         , 
		 TermTable     *master    , 
		 Query         *q         ,
		 char           gid       , 
		 char         **buf       , 
		 int32_t          *bufSize   ,
		 // these ptrs are supplied by the spider when trying to 
		 // generate the gigabit vector for a document it is indexing
		 class Words   *wordsPtr  , 
		 class Scores  *scoresPtr ,
		 int32_t          *hashes    ,
		 unsigned char  language  ,
		 int32_t           niceness  ,
		 LinkInfo*      linkInfo,
		 LinkInfo*      linkInfo2) {
	
	////////////////////////////////////////////
	//
	// GENERATE THE TOPICS
	//
	////////////////////////////////////////////
	
	
	//int64_t start = gettimeofdayInMilliseconds();
	
	// only allow one vote per ip
	HashTable iptable;
	// return fales and set g_errno if this alloc fails
	if ( t->m_ipRestrict && ! iptable.set ( st->m_numRequests * 4 ) ) 
		return false;
	
	// space for all vectors for deduping samples that are 80% similar
	char  vbuf [ 64*1024 ];
	char *vecs    = vbuf;
	int32_t  numVecs = 0;
	int32_t  vneed   = st->m_numRequests * SAMPLE_VECTOR_SIZE;
	if ( t->m_dedupSamplePercent >= 0 && vneed > 64*1024 ) 
		vecs = (char *)mmalloc ( vneed , "Msg24d" );
	if ( ! vecs ) return false;

	// hack, if words supplied, treat as one request
	if ( wordsPtr ) st->m_numRequests = 1;

	//
	//
	// . make the hash table used for repeated fragment detection
	// . one slot per word, over all samples
	//
	//

	// for every sample estimate the number of words so we know how big
	// to make our repeat hash table
	int32_t maxWords = 0;
	Words tmpw;
	// if getting a gigabit vector for a single doc, we know the # of words
	if ( wordsPtr ) maxWords += wordsPtr->getNumWords();
	// otherwise, get max # of words for each big sample via Msg20
	int32_t numMsg20Used = 0;
	for ( int32_t i = 0 ; ! wordsPtr && i < st->m_numRequests ; i++ ) {
		Msg20* thisMsg20 = NULL;
		if(wordsPtr) {}
		else if(st->m_msg20) thisMsg20 = &st->m_msg20[i];
		else {
			thisMsg20 = st->m_msg20Ptrs[i];
			if ( st->m_clusterLevels[i] != CR_OK ) continue;
		}
		//continue if we've gotten no content
		if(!wordsPtr && 
		   (!thisMsg20 || (thisMsg20 && thisMsg20->m_errno)))
			continue;
		// make sure the summary is not in a foreign language (aac)
		if (thisMsg20) {
		    unsigned char sLang;
		    sLang = thisMsg20->m_r->m_summaryLanguage;
		    if (language != langUnknown && sLang != language) continue;   
		};
		// get the ith big sample
		char *sample = NULL;
		int32_t  slen   = 0;
		// but if doing metas, get the display content
		char  *next = NULL;
		if(thisMsg20) next = thisMsg20->getDisplayBuf();
		if ( t->m_meta[0] && next)
			sample = thisMsg20->getNextDisplayBuf(&slen,&next);
		// XmlDoc::getGigabitVector() provides us with the Words/Scores
		// classes for the whole document. that is the "sample"
		else {
			sample = thisMsg20->getBigSampleBuf();
			slen   = thisMsg20->getBigSampleLen();
		}
		// are we unicode?
		bool isUnicode = thisMsg20->isUnicode();
		// set parser vars
		char *p    = sample;
		char *pend = sample + slen;
		// each sample consists of multiple \0 terminated excerpts
		int32_t sampleWords = 0;
#ifdef DEBUG_MSG24
		int32_t numExcerpts = 0;
#endif
		while ( p < pend ) {
			int32_t plen ;
			if ( isUnicode ) plen = ucStrNLen    (p,pend-p);
			else             plen = strlen       (p);
			if ( isUnicode ) sampleWords += countWords((UChar *)p,plen);
			else             sampleWords += countWords( p,plen);
			// advance to next exerpt
			p += plen + 1;
#ifdef DEBUG_MSG24
			numExcerpts++;
#endif
		};
#ifdef DEBUG_MSG24
		if ( sampleWords > 2048 ) {
		    char *dbgBuf = NULL;
		    log("topics: Unusually int32_t sample in Msg24: " 
			"sampleWords=%"INT32" numExcerpts=%"INT32"", 
			sampleWords, numExcerpts);
		    if ( (dbgBuf = (char *)mmalloc(slen+1, "DEBUG_MSG24")) ) {
			int jjStep = 1;
			if (isUnicode) jjStep = 2;
			int kk = 0;
			for (int jj = 0; jj< slen; jj += jjStep) {
			    if (sample[jj]) {
				dbgBuf[kk++] = sample[jj];
			    }	    
			    else {
				dbgBuf[kk++] = '#';
			    };
			};
			dbgBuf[kk++] = '\0';
			log("topics: \tsample was: %s", dbgBuf);
		    };
		}
		else {
		    log("topics: Reasonable sample in Msg24: "
			"sampleWords=%"INT32" numExcerpts=%"INT32"", 
			sampleWords, numExcerpts);
		};
#endif
		if (maxWords + sampleWords > 0x08000000) {
		    log("topics: too many words in samples. "
			"Discarding the remaining samples "
			"(maxWords=%"INT32")", maxWords);
		    break;
		}
		else {
		    maxWords += sampleWords;
		    numMsg20Used++;
		};
	}
	// make it big enough so there are gaps, so chains are not too long
	int32_t  minBuckets = (int32_t)(maxWords * 1.5);
	if(minBuckets < 512) minBuckets = 512;
	int32_t  numSlots   = 2 * getHighestLitBitValue ( minBuckets ) ;
	int32_t  need2      = numSlots * (8+4);
	char *rbuf       = NULL;
	char  tmpBuf2[13000];
	// sanity check 
	if ( need2 < 0 ) {
		g_errno = EBADENGINEER;
		return log("query: bad engineer in Msg24.cpp. need2=%"INT32" "
			   "numSlots=%"INT32" maxWords=%"INT32" q=%s", need2,numSlots,maxWords,q->m_orig);
	}
	if ( need2 < 13000 ) rbuf = tmpBuf2;
	else                  rbuf = (char *)mmalloc ( need2 , "WeightsSet3");
	if ( ! rbuf ) return false;
	// sanity check 
	if ( numSlots * 8 > need2 || numSlots * 8 < 0 ) {
		g_errno = EBADENGINEER;
		return log("query: bad engineer in Msg24.cpp. need2=%"INT32" "
			   "numSlots=%"INT32" q=%s", need2,numSlots,q->m_orig);
	}
	// clear the keys in the hash table (empty it out)
	memset ( rbuf , 0 , numSlots * 8 );
	// set the member var to this
	char *repeatTable         = rbuf;
	int32_t  repeatTableNumSlots = numSlots;

	//
	//
	// end making the hash table for repeated fragment detection
	//
	//

	
	// now combine all the pronouns and pronoun phrases into one big hash 
	// table and collect the top 10 topics
	int32_t nqi = 0;   // how many query terms actually used? for normalizing.
	int32_t tcount = 0; // how many title recs did we process?
	QUICKPOLL(niceness);

	for ( int32_t i = 0 ; i < numMsg20Used ; i++ ) {
		Msg20* thisMsg20 = NULL;
		if(wordsPtr) {}
		else if(st->m_msg20) thisMsg20 = &st->m_msg20[i];
		else {
			thisMsg20 = st->m_msg20Ptrs[i];
			if ( st->m_clusterLevels[i] != CR_OK ) continue;
		}
		// make sure the summary is not in a foreign language (aac)
		if (thisMsg20) {
		    unsigned char sLang;
		    sLang = thisMsg20->m_r->m_summaryLanguage;
		    if (language != langUnknown && sLang != language) continue;   
		};
		//continue if we've gotten no content
		if(!wordsPtr && 
		   (!thisMsg20 || (thisMsg20 && thisMsg20->m_errno)))
			continue;
		// skip if from an ip we already did
		if ( t->m_ipRestrict ) {
			int32_t ipd = ipdom (thisMsg20->getIp() );
			// zero is invalid!
			if ( ! ipd ) continue;
			//log("url=%s",thisMsg20->getUrl()); 
			if ( iptable.getValue(ipd) ) {
				//log("dup=%s",thisMsg20->getUrl()); 
				continue; 
			}
			// now we also check domain
			Url uu;
			uu.set ( thisMsg20->getUrl()    ,
				 thisMsg20->getUrlLen() );
			// "mid dom" is the "ibm" part of ibm.com or ibm.de
			char *dom  = uu.getMidDomain();
			int32_t  dlen = uu.getMidDomainLen();
			if ( dom && dlen > 0 ) {
				int32_t  h = hash32 ( dom , dlen );
				if ( iptable.getValue(h) ) continue; 
				iptable.addKey (h,1);
			}
			// add ip
			iptable.addKey (ipd,1);
		}
		// get the ith big sample
		char *bigSampleBuf = NULL;
		int32_t  bigSampleLen = 0;
		// but if doing metas, get the display content
		char  *next = NULL;
		if(thisMsg20) next = thisMsg20->getDisplayBuf();
		// but if doing metas, get the display content
		if ( t->m_meta[0] && next) {
			bigSampleBuf = 
				thisMsg20->getNextDisplayBuf(&bigSampleLen,&next);
		}
		// XmlDoc::getGigabitVector() provides us with the Words/Scores
		// classes for the whole document. that is the "sample"
		else if ( ! wordsPtr ) {
			bigSampleBuf = thisMsg20->getBigSampleBuf();
			bigSampleLen = thisMsg20->getBigSampleLen();
		}
		// skip if empty
		if ( !wordsPtr && (bigSampleLen<=0 ||!bigSampleBuf)) continue;
		// otherwise count it
		tcount++;
		// the docid
		int64_t docId = 0;
		if ( ! wordsPtr ) docId = thisMsg20->getDocId();
		// are we unicode?
		bool isUnicode;
		if ( ! wordsPtr ) isUnicode = thisMsg20->isUnicode();
		else              isUnicode = wordsPtr->isUnicode();
		unsigned char lang = language;
		if ( ! wordsPtr ) lang = thisMsg20->getLanguage();
		// continue; // mdw
		QUICKPOLL(niceness);
		// . hash it into the master table
		// . this may alloc st->m_mem, so be sure to free below
		hashSample ( q, bigSampleBuf, bigSampleLen, master, &nqi , t ,
			     st, docId ,
			     vecs , &numVecs , 
			     wordsPtr , scoresPtr , isUnicode ,
			     repeatTable , repeatTableNumSlots , lang );
		// ignore errors
		g_errno = 0;

		// hash the inlink texts and neighborhoods
		for(Inlink *k=NULL;linkInfo&&(k=linkInfo->getNextInlink(k));){
			char *s = k->ptr_linkText;
			int32_t len = k->size_linkText - 1;
			hashSample ( q, s, len, master, &nqi , t ,
				     st,     docId , // 0
				     vecs , &numVecs , 
				     NULL , NULL , k->m_isUnicode,
				     repeatTable , repeatTableNumSlots , 
				     lang );
			// and surrounding
			s   = k->ptr_surroundingText;
			len = k->size_surroundingText - 1;
			hashSample ( q, s, len, master, &nqi , t ,
				     st,     docId , // 0
				     vecs , &numVecs , 
				     NULL , NULL , k->m_isUnicode,
				     repeatTable , repeatTableNumSlots , 
				     lang );
		}
		for(Inlink*k=NULL;linkInfo2&&(k=linkInfo2->getNextInlink(k));){
			char *s = k->ptr_linkText;
			int32_t len = k->size_linkText - 1;
			hashSample ( q, s, len, master, &nqi, t ,
				     st,   docId , // docId
				     vecs , &numVecs , 
				     NULL , NULL, isUnicode,
				     repeatTable, repeatTableNumSlots,
				     lang );
		}
		// ignore errors
		g_errno = 0;
	}

	//hash meta keywords and meta description when generating the gigabit
	//vector, mainly useful for docs which have all of their content in frames
	if(st->m_dbufLen > 0 && wordsPtr) {
		hashSample ( q, st->m_dbuf, st->m_dbufLen, master, &nqi , t ,
			     st,  0/*docId*/ ,
			     vecs , &numVecs , 
			     NULL , NULL , wordsPtr->isUnicode() ,
			     repeatTable , repeatTableNumSlots , language );
	}

	//log("did samples in %"INT64" ",gettimeofdayInMilliseconds()-start);

	int32_t  nt = master->getNumTerms();

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
	int32_t need = t->m_maxTopics ;
	// get this many winners
	int32_t maxWinners = need;
	// double it in case some get deduped
	if ( t->m_dedup ) maxWinners *= 2; // mdw
	// count how many get removed, might have to recompute
	int32_t removed ;
	int32_t got = 0;

	// now get the top MAX_TOPICS or maxWinners pronouns or pronoun phrases
	//int32_t           scores [ MAX_TOPICS ];
	//char          *ptrs   [ MAX_TOPICS ];
	//unsigned char  lens   [ MAX_TOPICS ];
	int32_t  *scores  = NULL;
	char **ptrs    = NULL;
	int32_t  *lens    = NULL;
	char  *isunis  = NULL;
	int32_t  *slots   = NULL;
	int32_t  *pages   = NULL;
	// these vars are used below
	//char *ptrs2 [ MAX_TOPICS ];
	//int32_t  lens2 [ MAX_TOPICS ];
	char **ptrs2 = NULL;
	int32_t  *lens2 = NULL;

	char  *tmpBuf  = NULL;
	int32_t   tmpSize = 0;
	//bool   triedLinkInfo = false;
 redo:
	// ensure maxWinners not too big
	//if ( maxWinners > MAX_TOPICS ) maxWinners = MAX_TOPICS;

	// allocate enough space
	int32_t  newSize = maxWinners*(sizeof(char *)+4+4+4+4+sizeof(char *)+4+1);
	char *newBuf  = (char *)mrealloc(tmpBuf,tmpSize , newSize , "Msg24e" );
	if ( ! newBuf ) {
		if ( tmpBuf ) mfree ( tmpBuf , tmpSize , "Msg24" );
		// free the links in the linked list, if any
		if ( st->m_mem ) {
			mfree ( st->m_mem, st->m_memEnd - st->m_mem, "Msg24" );
			st->m_mem    = NULL;
			st->m_memEnd = NULL;
			st->m_memPtr = NULL;
		}
		if ( vecs != vbuf ) mfree ( vecs , vneed , "Msg24" );
		return log("topics: realloc to %"INT32" failed.",newSize);
	}
	tmpBuf   = newBuf;
	tmpSize  = newSize;
	char *pp = tmpBuf;
	ptrs     = (char **)pp ; pp += sizeof(char *) * maxWinners;
	scores   = (int32_t  *)pp ; pp += 4 * maxWinners;
	lens     = (int32_t  *)pp ; pp += 4 * maxWinners;
	isunis   =          pp ; pp += maxWinners;
	slots    = (int32_t  *)pp ; pp += 4 * maxWinners;
	pages    = (int32_t  *)pp ; pp += 4 * maxWinners;
	ptrs2    = (char **)pp ; pp += sizeof(char *) * maxWinners;
	lens2    = (int32_t  *)pp ; pp += 4 * maxWinners;

	int32_t *pops = master->m_pops;

	QUICKPOLL(niceness);

	int32_t  np = 0;
	int32_t  minScore = 0x7fffffff;
	int32_t  minj = -1;
	int32_t  i ;
 	int32_t *heads = master->getHeads();
	bool  callRedo = true;
	// total # of pages sampled
	int32_t  sampled = numMsg20Used;
	for ( i = 0 ; i < nt && np < maxWinners ; i++ ) {
		// skip term #i from "table" if it has 0 score
		int32_t score = master->m_scores[i]; // getScoreFromTermNum(i) ;
		if ( ! score ) continue;

		// . make it higher the more popular a term is
		// . these are based on a MAXPOP of 10000
		int32_t mdc = (int32_t)((((double)sampled * 3.0 * 
				    (double)(pops[i]&0x7fffffff))+0.5)/MAXPOP);
		if ( mdc < t->m_minDocCount ) mdc = t->m_minDocCount;

		// skip if does not meet the min doc count
		int32_t count = 0;
		//if ( mdc > 1 || st->m_returnDocIds ) {
		if ( t->m_minDocCount > 1 || st->m_returnDocIds ) {
			DocIdLink *link = (DocIdLink *)(st->m_mem+heads[i]);
			while ( (char *)link >= st->m_mem ) { 
				count++; 
				link = (DocIdLink*)(st->m_mem + link->m_next); 
			}
			if ( count < mdc ) continue;
		}

		// set the min of all in our list
		if ( score < minScore ) { minScore = score; minj = np; }
		// i've seen this become NULL at line 753 on gb1 below for
		// /search?code=mammaXbG&uip=12.41.126.39&n=15&raw=8&q=
		//  manhattan,+ny 
		// so let's try it again and try to find out why maybe
		if ( master->m_termLens[i] <= 0 ) {
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
		scores [ np ] = score;
		ptrs   [ np ] = master->m_termPtrs[i]; // getTermPtr(i) ;
		lens   [ np ] = master->m_termLens[i]; // getTermLen(i);
		isunis [ np ] = master->m_isunis[i];
		slots  [ np ] = i;
		pages  [ np ] = count;
		np++;
	}

	QUICKPOLL(niceness);
	// if not enough no matter what, do not redo
	if ( np < maxWinners ) callRedo = false;
	// now do the rest
	for ( ; i < nt ; i++ ) {
		// skip term #i from "table" if it has 0 score
		int32_t score = master->m_scores[i]; // getScoreFromTermNum(i) ;
		// bail if empty
		if ( score <= 0 ) continue;
		// ignore if not a winner
		if ( score <= minScore ) continue;
		// . make it higher the more popular a term is
		// . these are based on a MAXPOP of 10000
		int32_t mdc = (int32_t)((((double)sampled * 3.0 * 
				    (double)(pops[i]&0x7fffffff))+0.5)/MAXPOP);
		if ( mdc < t->m_minDocCount ) mdc = t->m_minDocCount;

		// skip if does not meet the min doc count
		int32_t count = 0;
		if ( t->m_minDocCount > 1 || st->m_returnDocIds ) {
			DocIdLink *link = (DocIdLink *)(st->m_mem+heads[i]);
			// m_next is -1 to indicate end
			while ( (char *)link >= st->m_mem ) { 
				count++; 
				link = (DocIdLink *)(st->m_mem + link->m_next);
			}
			if ( count < mdc ) continue;
		}
		// find the score it will replace, the min one
		//int32_t j ;
		//for ( j = 0 ; j < np ; j++ )
		//	if ( scores [ j ] == minScore ) break;
		// bad engineer?
		//if ( j == np ) { char *xx = NULL; *xx = 0; }
		// recalc the score
		//double frac1 = ((MAXPOP-(pops[i]&0x7fffffff))*100.0)/MAXPOP;
		//double frac2 = ((double)count * 100.0) / (double)sampled;
		//int32_t   newScore = (int32_t)((frac1 * frac2) / 100.0);
		//int32_t   oldminj  = minj;
		// replace jth guy
		scores [ minj ] = score;
		ptrs   [ minj ] = master->m_termPtrs[i]; // getTermPtr(i) ;
		lens   [ minj ] = master->m_termLens[i]; // getTermLen(i);
		isunis [ minj ] = master->m_isunis[i];
		pages  [ minj ] = count;
		slots  [ minj ] = i;
		//log("ptrs[%"INT32"]=%"XINT32"",j,ptrs[j]);
		// hopefully we increased the min score in our top set now
		minScore = 0x7fffffff;
		for ( int32_t j = 0 ; j < np ; j++ ) {
			if ( scores[j] < minScore ) {
				minScore = scores[j];
				minj     = j;
			}
		}
		//scores [oldminj] = newScore;
	}

	// bubble sort the top winners
 again:
	bool flag = 0;
	for ( int32_t i = 1 ; i < np ; i++ ) {
		if ( scores[i-1] >= scores[i] ) continue;
		int32_t   ts = scores[i];
		char  *tp = ptrs  [i];
		int32_t   tl = lens  [i];
		char   tu = isunis[i];
		int32_t   tc = pages [i];
		int32_t   tt = slots [i];
		scores [i  ] = scores[i-1];
		ptrs   [i  ] = ptrs  [i-1];
		lens   [i  ] = lens  [i-1];
		isunis [i  ] = isunis[i-1];
		pages  [i  ] = pages [i-1];
		slots  [i  ] = slots [i-1];
		scores [i-1] = ts;
		ptrs   [i-1] = tp;
		lens   [i-1] = tl;
		isunis [i-1] = tu;
		pages  [i-1] = tc;
		slots  [i-1] = tt;
		flag = 1;
	}
	if ( flag == 1 ) goto again;

	QUICKPOLL(niceness);

	// . normalize all scores
	// . assume 20000 pointer per query term per page
	// . an topic term will get 20000 points for each query term it is
	//   close to
	int32_t max = nqi * tcount * MAX_SCORE_MULTIPLIER ; //10000;
        if ( nqi == 0 ) max = tcount * ALT_MAX_SCORE;
	if ( max == 0 ) max = 1;
	for ( i = 0 ; i < np ; i++ ) {
		// skip if length is 0, it was a dup from above
		//if ( lens[i] <= 0 ) continue;
		scores[i] = (scores[i] * 100) / max;
		if ( scores[i] <= 0   ) scores[i] = 1;
		if ( scores[i] >= 100 ) scores[i] = 100; // add a log statement here? (aac)
	}

	// . now set ptrs2/lens2 to point to comparison string in each topic
	// . skip it over stop words, don't compare those
	// . this way we can do a more flexible strcasestr and ignore common
	//   words when comparing, they don't add much beyond repetition
	// . "super bowl" + "the super bowl" --> "super bowl"
	//char *ptrs2 [ MAX_TOPICS ];
	//int32_t  lens2 [ MAX_TOPICS ];
	for ( i = 0 ; i < np ; i++ ) {
		/*
		Words w;
		w.set ( false , ptrs[i] , lens[i] , false );
		int32_t nw = w.getNumWords();
		// skip if none
		if ( nw <= 0 ) continue;
		*/
		// establish our new ptrs
		ptrs2 [ i ] = ptrs[i];
		lens2 [ i ] = lens[i];
		// skip initial common words
		//----> not if capitalized!! leave those in tact. like 
		//      Michael Jackson's "Beat It"
		/*
		int32_t h;
		int32_t j = 0;
		if ( w.isPunct(j) ) j++;
		for (  ; j < nw ; j += 2 ) {
			char *ww    = w.getWord   (j);
			int32_t  wwlen = w.getWordLen(j);
			// if capitlized, leave it
			if ( is_upper(ww[0]) ) break;
			// single letter lower case is common word
			if ( wwlen <= 1 && is_alpha(ww[0]) ) goto gotone;
			// leave it if not common
			h= hash64d(w.getWord(j),w.getWordLen(j));
			if ( ! isCommonWord ( h ) ) break;
			// otherwise, scrub it off
		gotone:
			ptrs2 [i] = w.getWord(j+2);
		}
		// skip trailing common words
		int32_t k = nw - 1 ;
		if ( w.isPunct(k) ) k--;
		for (  ; k >= j ; k -= 2 ) {
			char *ww    = w.getWord   (k);
			int32_t  wwlen = w.getWordLen(k);
			// if capitlized, leave it
			if ( is_upper(ww[k]) ) break;
			// single letter lower case is common word
			if ( wwlen <= 1 && is_alpha(ww[0]) ) goto gotone;
			// left off here!!
			if ( w.getWordLen(j) <= 1&&is_alpha(w.getWord(j)[0]) )
				continue;
			h=hash64d(w.getWord(j),w.getWordLen(j));
			if ( ! isCommonWord ( h ) ) break;
		}
		// set new length
		char *end2 = w.getWord(k) + w.getWordLen(k);
		lens2[i] = end2 - ptrs2[i];
		*/
	}

	if ( ! t->m_dedup ) goto skipdedup;
	//goto skipdedup; // mdw

	removed = 0;
	// now remove similar terms from the top topics
	for ( int32_t i = 0 ; i < np - 1 ; i++ ) {
		// skip if nuked already
		if ( lens[i] == 0 ) continue;
		// scan down to this score, but not below
		//int32_t minScore = (scores[i] * 75) / 100 ;
		int32_t minScore = scores[i] - 25;
		// if we get replaced by a longer guy, remember him
		int32_t replacerj = -1;
		// . a longer term than encapsulates us can eliminate us
		// . or, if we're the longer, we eliminate the int16_ter
		for ( int32_t j = i + 1 ; j < np ; j++ ) {
			// skip if nuked already
			if ( lens[j] == 0 ) continue;
			// null term both
			char c1 = ptrs2[i][lens2[i]];
			char c2 = ptrs2[j][lens2[j]];
			ptrs2[i][lens2[i]] = '\0';
			ptrs2[j][lens2[j]] = '\0';
			// if we are the int16_ter, and longer contains us
			// then it nukes us... unless his score is too low
			if ( lens2[i] < lens2[j] ) {
				// if int16_ter is contained
				char *s;
				if (isunis[j] == 0 && isunis[i] == 0)
					s = gb_strcasestr (ptrs2[j],ptrs2[i]) ;
				else if (isunis[j] == 0 && isunis[i] == 1)
					s = ucStrNCaseStr(
						ptrs2[j],
						(UChar*)ptrs2[i], lens2[i]>>1);
				else if (isunis[j] == 1 && isunis[i] == 0)
					s = (char*)ucStrNCaseStr(
						(UChar*)ptrs2[j], lens2[j]>>1,
						ptrs2[i]);
				else
					s = (char*)ucStrNCaseStr(
						(UChar*)ptrs2[j], lens2[j]>>1,
						(UChar*)ptrs2[i], lens2[i]>>1);
				// un-null term both
				ptrs2[i][lens2[i]] = c1;
				ptrs2[j][lens2[j]] = c2;
				// even if he's longer, if his score is too
				// low then he cannot nuke us
				if ( scores[j] < minScore ) continue;
				// if we were NOT contained by someone below...
				if ( ! s ) continue;
				// he's gotta be on all of our pages, too
				//if ( ! onSamePages(i,j,slots,heads,pages) )
				//	continue;
				// int16_ter gets our score (we need to sort)
				// not yet! let him finish, then replace him!!
				replacerj = j;
				// see if we can nuke other guys at least
				continue;
			}
			// . otherwise, we are the longer
			// . we can nuke any int16_ter below us, all scores
			char *s;
			if (isunis[i] == 0 && isunis[j] == 0)
				s = gb_strcasestr (ptrs2[i],ptrs2[j]) ;
			else if (isunis[i] == 0 && isunis[j] == 1)
				s = ucStrNCaseStr(
					ptrs2[i],
					(UChar*)ptrs2[j], lens2[j]>>1);
			else if (isunis[i] == 1 && isunis[j] == 0)
				s = (char*)ucStrNCaseStr(
					(UChar*)ptrs2[i], lens2[i]>>1,
					ptrs2[j]);
			else
				s = (char*)ucStrNCaseStr(
					(UChar*)ptrs2[i], lens2[i]>>1,
					(UChar*)ptrs2[j], lens2[j]>>1);
			// un-null term both
			ptrs2[i][lens2[i]] = c1;
			ptrs2[j][lens2[j]] = c2;

			QUICKPOLL(niceness);


			// keep going if no match
			if ( ! s ) continue;
			// remove him if we contain him
			lens[j] = 0;
			// count him
			removed++;
			// the redo flag
			//rflag = 1;

		}
		// if we got replaced by a longer guy, he replaces us
		// and takes our score
		if ( replacerj >= 0 ) {
			ptrs  [i] = ptrs  [replacerj];
			lens  [i] = lens  [replacerj];
			pages [i] = pages [replacerj];
			slots [i] = slots [replacerj];
			ptrs2 [i] = ptrs2 [replacerj];
			lens2 [i] = lens2 [replacerj];
			//scores[i] = scores[replacerj];
			lens  [replacerj] = 0;
			i--;
			// count him
			removed++;
			// the redo flag
			//rflag = 1;
		}
	}

	// . PROBLEM #2: often a phrase and the next phrase, +1, are in
	//   there... how to fix? the higher scoring one should swallow
	//   up the lower scoring one, even if only 3 of the 4 words match
	//   (do not count common words)

	// . #3 or when all non-query, non-common terms match... pick the
	//   longer and remove the common words, but keep query words.

	// again2:
	//char rflag = 0;
	// if two terms are close in score, and one is a longer version
	// of the other, choose it and remove the int16_ter
	for ( int32_t i = 0 ; i < np - 1 ; i++ ) {
		// skip if nuked already
		if ( lens[i] == 0 ) continue;
		// scan down to this score, but not below
		//int32_t minScore = (scores[i] * 75) / 100 ;
		int32_t minScore = scores[i] - 15;
		// if we get replaced by a longer guy, remember him
		int32_t replacerj = -1;
		// . a longer term than encapsulates us can eliminate us
		// . or, if we're the longer, we eliminate the int16_ter
		for ( int32_t j = i + 1 ; j < np ; j++ ) {
			// skip if nuked already
			if ( lens[j] == 0 ) continue;
			// null term both
			char c1 = ptrs[i][lens[i]];
			char c2 = ptrs[j][lens[j]];
			ptrs[i][lens[i]] = '\0';
			ptrs[j][lens[j]] = '\0';
			// if we are the int16_ter, and longer contains us
			// then it nukes us... unless his score is too low
			if ( lens[i] < lens[j] ) {
				// if int16_ter is contained
				char *s;
				if (isunis[j] == 0 && isunis[i] == 0)
					s = gb_strcasestr (ptrs2[j],ptrs2[i]) ;
				else if (isunis[j] == 0 && isunis[i] == 1)
					s = ucStrNCaseStr(
						ptrs2[j],
						(UChar*)ptrs2[i], lens2[i]>>1);
				else if (isunis[j] == 1 && isunis[i] == 0)
					s = (char*)ucStrNCaseStr(
						(UChar*)ptrs2[j], lens2[j]>>1,
						ptrs2[i]);
				else
					s = (char*)ucStrNCaseStr(
						(UChar*)ptrs2[j], lens2[j]>>1,
						(UChar*)ptrs2[i], lens2[i]>>1);
				// un-null term both
				ptrs[i][lens[i]] = c1;
				ptrs[j][lens[j]] = c2;
				// even if he's longer, if his score is too
				// low then he cannot nuke us
				if ( scores[j] < minScore ) continue;
				// if we were NOT contained by someone below...
				if ( ! s ) continue;
				// if we are not on the same pages as the
				// int16_ter one, then we cannot absorb him
				//if ( ! onSamePages(i,j,slots,heads,pages)) 
				//	continue;
				// int16_ter gets our score (we need to sort)
				// not yet! let him finish, then replace him!!
				replacerj = j;
				// see if we can nuke other guys at least
				continue;
			}
			// . otherwise, we are the longer
			// . we can nuke any int16_ter below us, all scores
			char *s;
			if (isunis[i] == 0 && isunis[j] == 0)
				s = gb_strcasestr (ptrs2[i],ptrs2[j]) ;
			else if (isunis[i] == 0 && isunis[j] == 1)
				s = ucStrNCaseStr(
					ptrs2[i],
					(UChar*)ptrs2[j], lens2[j]>>1);
			else if (isunis[i] == 1 && isunis[j] == 0)
				s = (char*)ucStrNCaseStr(
					(UChar*)ptrs2[i], lens2[i]>>1,
					ptrs2[j]);
			else
				s = (char*)ucStrNCaseStr(
					(UChar*)ptrs2[i], lens2[i]>>1,
					(UChar*)ptrs2[j], lens2[j]>>1);
			// un-null term both
			ptrs[i][lens[i]] = c1;
			ptrs[j][lens[j]] = c2;

			QUICKPOLL(niceness);

			// keep going if no match
			if ( ! s ) continue;
			// if we are not on the same pages as the
			// int16_ter one, then we cannot absorb him
			//if ( ! onSamePages(i,j,slots,heads,pages))
			//	continue;
			// remove him if we contain him
			lens[j] = 0;
			// count him
			removed++;
			// the redo flag
			//rflag = 1;

		}
		// if we got replaced by a longer guy, he replaces us
		// and takes our score
		if ( replacerj >= 0 ) {
			ptrs  [i] = ptrs  [replacerj];
			lens  [i] = lens  [replacerj];
			pages [i] = pages [replacerj];
			slots [i] = slots [replacerj];
			//scores[i] = scores[replacerj];
			lens  [replacerj] = 0;
			i--;
			// count him
			removed++;
			// the redo flag
			//rflag = 1;
		}
	}
	// if someone got replaced, loop more
	//if ( rflag ) goto again2;

	// remove common phrases
	for ( int32_t i = 0 ; i < np ; i++ ) {
		// skip if nuked already
		if ( lens[i] == 0 ) continue;
		// compare
		bool remove = false;
		if ( isunis[i] == 0 ) { //com org dom xhtml html dtd
		  if (!strncasecmp(ptrs[i], "all rights reserved",lens[i]) ||
		      !strncasecmp(ptrs[i], "rights reserved"    ,lens[i]) ||
		      !strncasecmp(ptrs[i], "in addition"        ,lens[i]) ||
		      !strncasecmp(ptrs[i], "for example"        ,lens[i]) ||
		      !strncasecmp(ptrs[i], "in order"           ,lens[i]) ||
		      !strncasecmp(ptrs[i], "in fact"            ,lens[i]) ||
		      !strncasecmp(ptrs[i], "in general"         ,lens[i]) ||
		      !strncasecmp(ptrs[i], "contact us"         ,lens[i]) ||
		      !strncasecmp(ptrs[i], "at the same time"   ,lens[i]) ||
		      !strncasecmp(ptrs[i], "http"               ,lens[i]) ||
		      !strncasecmp(ptrs[i], "html"               ,lens[i]) ||
		      !strncasecmp(ptrs[i], "s "                 ,lens[i]) ||
		      !strncasecmp(ptrs[i], "for more information",lens[i]))
			  remove = true;
		}
		else {
		  if ( !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
				    "all rights reserved", 19) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		     		    "rights reserved", 15) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "in addition", 11) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "for example", 11) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "in order", 8) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "in fact", 7) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "in general", 10) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "contact us", 10) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "at the same time", 16) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "http", 4) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "s ", 2) ||
		       !ucStrCaseCmp( (UChar*)ptrs[i], lens[i] >> 1,
		                    "for more information", 20) )
			  remove = true;
		}
		if ( remove ) {
			lens[i] = 0;
			// count him
			removed++;
		}
	}
	QUICKPOLL(niceness);
	// now after longer topics replaced the int16_ter topics which they
	// contained, remove the longer topics if they have too many words
	// remove common phrases
	for ( int32_t i = 0 ; i < np ; i++ ) {
		// skip if nuked already
		if ( lens[i] == 0 ) continue;
		if ( ! ptrs[i]    ) continue;

		Words w;
		w.set ( false , false, ptrs[i] , lens[i] , TITLEREC_CURRENT_VERSION,
			false, false, niceness );
		int32_t nw = w.getNumWords();
		// . does it have comma? or other punct besides an apostrophe?
		// . we allow gigabit phrases to incorporate a int32_t stretch
		//   of punct... only before the LAST word in the phrase,
		//   that way our overlap removal still works well.
		bool hasPunct = false;
		for ( int32_t k = 0 ; k < lens[i] ; k++ ) {
			if ( ! is_punct(ptrs[i][k]) ) continue;
			// apostrophe is ok as int32_t as alnum follows
			if ( ptrs[i][k] == '\'' &&
			     is_alnum(ptrs[i][k+1]) ) continue;
			// . period ok, as int32_t as space or alnum follows
			// . if space follows, then an alnum must follow that
			// . same goes for colon
			QUICKPOLL(niceness);

			// . for now, until we get abbreviations working,
			//   alnum must follow period
			if ( (ptrs[i][k] == '.' || ptrs[i][k] == ':' ) &&
			     ( is_alnum(ptrs[i][k+1])  ||
			       // accept single intial before the period, too
			       (ptrs[i][k+1] ==' ' && is_alnum(ptrs[i][k+2]) 
				&& k>=2 && ptrs[i][k-2]==' ')))
				continue;
			// comma is ok if surrounded by digits
			if ( (ptrs[i][k] == ',' &&
			      is_digit(ptrs[i][k-1]) &&
			      is_digit(ptrs[i][k+1])   )) continue;
			// percent is ok
			if ( ptrs[i][k] == '%' ) continue;
			if ( ptrs[i][k] == '&' ) continue;
			if ( ptrs[i][k] == '@' ) continue;
			if ( ptrs[i][k] == '-' ) continue;
			//if ( ptrs[i][k] == '(' ) continue;
			//if ( ptrs[i][k] == ')' ) continue;
			hasPunct = true;
			break;
		}
		// keep it if words are under limit
		// and has no commas
		if ( nw <= 2*t->m_maxWordsPerTopic -1 && ! hasPunct ) 
			continue;
		lens[i] = 0;
		removed++;
	}

	QUICKPOLL(niceness);
	// if we removed enough to fall below maxWinners, redo
	got = np - removed;
	if ( got >= need ) goto skipdedup;
	// if we already did all from "master", no more left!
	if ( np  >= master->getNumTermsUsed() ) goto skipdedup;
	// if we didn't have enough raw results, do not redo it
	if ( ! callRedo ) goto skipdedup;
	// or if already hit MAX_TOPICS
	//if ( maxWinners >= MAX_TOPICS ) goto skipdedup; mdw
	if ( got == 0 ) maxWinners = maxWinners*2;
	else            maxWinners = ((int64_t)maxWinners * 
				      (int64_t)need * 110LL) / 
				((int64_t)got * 100LL) + 10;
	goto redo; // mdw

 skipdedup:

	// free the repeat table if it allocated mem
	if ( repeatTable != tmpBuf2 ) {
		mfree ( repeatTable , need2 , "Msg24" );
		repeatTable = NULL;
	}


	// how much space do we need for reply?
	int32_t size = 0;
	// 4 bytes for number of topics
	size += 4;
	// then how much for each topic?
	int32_t ntp  = 0;
	for ( i = 0 ; i < np ; i++ ) {
		// cutoff at min score
		if ( scores[i] < t->m_minTopicScore ) continue;
		// skip if length is 0, it was a dup from above
		if ( lens[i] <= 0 ) continue;
		// we always get the count now
		if ( st->m_returnDocIds ) {
			int32_t count = 0;
			DocIdLink *link = (DocIdLink *)(st->m_mem+heads[slots[i]]);
			while ( (char *)link >= st->m_mem ) { 
				count++; 
				link = (DocIdLink *)(st->m_mem + link->m_next); 
			}
			// space for the docids if they want them
			size += 8 * count;
			// sanity check
			if ( count != pages[i] ) { char *xx = NULL; *xx = 0; }
		}
		// length (include \0 for null termination)
		size += 4 + 4 + 4 + 1 + lens[i] + 1;
		// . do we send back docid info?
		// . each termId can have a linked list of docids
		// . how many are in that list? (0 if none)
		size += 4;
		// 4 bytes for the dummy place holder. each one of these
		// can be a ptr to the list of docids, but it will be NULL
		// if we do not have a list of docids for this gigabit.
		size += 4;
		// the popularity... topic pop
		size += 4;
		// count numbre of topics we'll store
		ntp++;
	}
	// realloc reply
	newSize = *bufSize + size;
	char *s = (char *) mrealloc ( *buf , *bufSize , newSize , "Msg24f" );
	if ( ! s ) {
		if ( tmpBuf ) mfree ( tmpBuf , tmpSize , "Msg24" );
		if ( *buf   ) mfree ( *buf , *bufSize , "Msg24" );
		*buf     = NULL;
		*bufSize = 0;
		// free the links in the linked list, if any
		if ( st->m_mem ) {
			mfree ( st->m_mem, st->m_memEnd - st->m_mem, "Msg24" );
			st->m_mem    = NULL;
			st->m_memEnd = NULL;
			st->m_memPtr = NULL;
		}
		if ( vecs != vbuf ) mfree ( vecs , vneed , "Msg24" );
		return log("topics: Realloc reply buf to %"INT32" failed.",newSize);
	}
	// we realloc'd successfully, use it
	*buf = s;
	// copy into reply after previous topic groups
	char *p = *buf + *bufSize;
	// serialize ourselves into the buffer
	//serialize2 ( p , ptrs , scores , lens , gids );
	// store number of topics first
	*(int32_t *)p = ntp; p += 4;
	// arrays first
	char      **pptrs   = (char      **)p; p += ntp * 4;
	int32_t       *pscores = (int32_t       *)p; p += ntp * 4;
	int32_t       *plens   = (int32_t       *)p; p += ntp * 4;
	int32_t       *ndocids = (int32_t       *)p; p += ntp * 4;
	int64_t **dptrs   = (int64_t **)p; p += ntp * 4; // place holder
	int32_t       *ppops   = (int32_t       *)p; p += ntp * 4;
	char       *pgids   = (char       *)p; p += ntp ;
	char       *ptext   = p;
	int32_t        j       = 0;
	for ( i = 0 ; i < np ; i++ ) {
		// cutoff at min score
		if ( scores[i] < t->m_minTopicScore ) continue;
		// skip if length is 0, it was a dup from above
		if ( lens[i] <= 0 ) continue;
		// store it
		pptrs   [j] = (char *)(ptext - p);
		pscores [j] = scores [i];
		plens   [j] = lens   [i];
		pgids   [j] = gid;
		if ( pops ) ppops [j] = pops[slots[i]];
		else        ppops [j] = 0;
		ndocids [j] = 0;
		dptrs   [j] = NULL; // dummy placeholder
		gbmemcpy ( ptext , ptrs[i] , lens[i] ); ptext += lens[i];
		//if ( hashes && j < GIGABITS_IN_VECTOR )
		//	hashes[j] = hash32Lower (ptrs[i],lens[i]);
		*ptext++ = '\0';
		j++;
	}
	QUICKPOLL(niceness);

	
	// fill in docid info
	if ( st->m_returnDocIdCount || st->m_returnDocIds ) {
		// reset j for this repeat loop
		j = 0;
		// this loop header is the same as above
		for ( i = 0 ; i < np ; i++ ) {
			// cutoff at min score
			if ( scores[i] < t->m_minTopicScore ) continue;
			// skip if length is 0, it was a dup from above
			if ( lens[i] <= 0 ) continue;
			// count em
			int32_t count = 0;
			DocIdLink *link = (DocIdLink *)(st->m_mem+heads[slots[i]]);
			while ( (char *)link >= st->m_mem ) { 
				count++; 
				if ( st->m_returnDocIds ) {
					*(int64_t *)ptext = link->m_docId;
					ptext += 8;
				}
				link = (DocIdLink *)(st->m_mem + link->m_next);
			}
			ndocids[j] = count;
			j++;
		}
	}
	//skipd:
	// update buf parms for re-calls
	*bufSize = newSize;
	
	// free tmp buf
	mfree ( tmpBuf , tmpSize , "Msg24" );
	// free the links in the linked list, if any
	if ( st->m_mem ) {
		mfree ( st->m_mem , st->m_memEnd - st->m_mem , "Msg24" );
		st->m_mem    = NULL;
		st->m_memEnd = NULL;
		st->m_memPtr = NULL;
	}
	if ( vecs != vbuf ) mfree ( vecs , vneed , "Msg24" );
	// copy into reply topic buf
	//char *start = slot->m_tmpBuf;
	//char *p     = slot->m_tmpBuf;
	//char *pend  = p + TMPBUFSIZE;
	/*
	for ( i = 0 ; i < np ; i++ ) {
		// cutoff at min score
		if ( scores[i] < t->m_minTopicScore ) continue;
		// skip if length is 0, it was a dup from above
		if ( lens[i] <= 0 ) continue;
		if ( p + lens[i] + 9 >= pend ) break;
		*(int32_t *)p = scores[i]; p += 4;
		*(int32_t *)p = lens  [i]; p += 4;
		*(char *)p = gid      ; p += 1;
		gbmemcpy ( p , ptrs[i] , lens[i] ); p += lens[i];
		*p++ = '\0';
	}
	*/
	return true;
}

/*
bool onSamePages ( int32_t i, int32_t j, int32_t *slots, int32_t *heads, int32_t *pages ) {
	if ( pages[i] != pages[j] ) return false;
	DocIdLink *link1 = (DocIdLink *)(st->m_mem+heads[slots[i]]);
	DocIdLink *link2 = (DocIdLink *)(st->m_mem+heads[slots[j]]);
	while ( (char *)link1 >= st->m_mem ) { 
		if ( link1->m_docId != link2->m_docId ) return false;
		link1 = (DocIdLink *)(st->m_mem + link1->m_next);
		link2 = (DocIdLink *)(st->m_mem + link2->m_next);
	}
	return true;
}
*/

void hashExcerpt ( Query *q , uint64_t *qids , int32_t *qpops , 
		   int32_t nqi , TermTable *tt , char *buf , int32_t bufLen , 
		   Words *w , TopicGroup *t , Scores *scoresPtr , 
		   bool isUnicode , char *repeatTable , 
		   int32_t repeatTableNumSlots , char language );

// . returns false and sets g_errno on error
// . here's the tricky part
// . *nqiPtr is how many query terms we used - so caller can normalize scores
bool hashSample ( Query *q, char *bigSampleBuf , int32_t bigSampleLen ,
		  TermTable *master, int32_t *nqiPtr , TopicGroup *t ,
		  State24 *st, int64_t docId ,
		  char *vecs , int32_t *numVecs ,
		  Words *wordsPtr , Scores *scoresPtr , bool isUnicode ,
		  char *repeatTable , int32_t repeatTableNumSlots ,
		  char language ) {
	// numTerms must be less than this
	//if ( q && q->getNumTerms() > MAX_QUERY_TERMS ) (aac)
	if ( q && q->m_numWords > MAX_QUERY_TERMS ) 
		return log("topics: Too many query terms for "
			   "topic generation.");

	//bool returnDocIdCount = st->m_returnDocIdCount;
	//bool returnDocIds = st->m_returnDocIds; 
	bool returnPops = st->m_returnPops;
	

	// this is the pure content now
	char *content     = bigSampleBuf;
	int32_t  contentLen  = bigSampleLen;
	// truncate it to 40k, that's enough
	//if ( contentLen > 50*1024 ) contentLen = 50*1024;
	// bail if empty!
	if ( ! wordsPtr && (! content || contentLen <= 0) ) { 
		log("topics: Got empty document for topic generation."); 
		return true; 
	}
	// make buf point to the available space
	char *buf = content;
	// get length of the buffer
	int32_t bufLen = contentLen;

#ifdef DEBUG_MSG24
	if (q) {
	    log("topics: Query stats in hashSample");
	    int32_t numQT = q->getNumTerms();
	    int32_t numQW = q->m_numWords;
	    log("topics: \tnumQueryTerms = %"INT32"", numQT);
	    log("topics: \tnumQueryWords = %"INT32"", numQW);
	    char *thisQT, *thisQW, iCode, tmpBuf[1024];
	    int32_t qtLen, qwLen, i, j, k;
	    for (i = 0; i < numQT; i++) {
		thisQT = q->getTerm(i);
		qtLen  = q->getTermLen(i);
		k = 0;
		for (j = 0; j < qtLen && k < 1023; j++) {
		    if (thisQT[j]) tmpBuf[k++] = thisQT[j];
		};
		tmpBuf[k] = '\0';
		log ("topics: \tQT[%"INT32"] = %s", i, &tmpBuf[0]); 
	    };	
	    for (i = 0; i < numQW; i++) {
		thisQW = q->m_qwords[i].m_word;
		qwLen  = q->m_qwords[i].m_wordLen;
		iCode  = q->m_qwords[i].m_ignoreWord;
		k = 0;
		for (j = 0; j < qwLen && k < 1023; j++) {
		    if (thisQW[j]) tmpBuf[k++] = thisQW[j];
		};
		tmpBuf[k] = '\0';
		log ("topics: \tQW[%"INT32"] = %s,\tignore = %i", i, &tmpBuf[0], iCode); 
	    };	
	};
#endif

	// get query hashes/ids, 32 bit, skip phrases
	uint64_t qids [MAX_QUERY_TERMS];
	int32_t qpops[MAX_QUERY_TERMS];
	int32_t nqi = 0;
	//for ( int32_t i=0 ; q && i<q->getNumTerms() && nqi<MAX_QUERY_TERMS; i++){ (aac)
	for ( int32_t i=0 ; q && i < q->m_numWords && nqi<MAX_QUERY_TERMS; i++){
		//if ( q->isPhrase       (i) ) continue; (aac)
		//if ( q->isQueryStopWord(i) ) continue; (aac)
		char ignCode = q->m_qwords[i].m_ignoreWord;
		if ( ignCode && ignCode != 8 ) continue;
		char *s    = q->m_qwords[i].m_word;    // q->getTerm(i);    (aac)
		int32_t  slen = q->m_qwords[i].m_wordLen; // q->getTermLen(i); (aac)
		int32_t qpop;
		int32_t encodeType = csISOLatin1;
		if ( q->isUnicode() ) encodeType = csUTF16;
		qids[nqi] = hash64d(s, slen, encodeType);
		qpop = g_speller.getPhrasePopularity(s, qids[nqi], true,
						     language);
		if       ( qpop < QPOP_ZONE_0 ) qpop = QPOP_MULT_0;
		else if  ( qpop < QPOP_ZONE_1 ) qpop = QPOP_MULT_1;
		else if  ( qpop < QPOP_ZONE_2 ) qpop = QPOP_MULT_2;
		else if  ( qpop < QPOP_ZONE_3 ) qpop = QPOP_MULT_3;
		else if  ( qpop < QPOP_ZONE_4 ) qpop = QPOP_MULT_4;
		else                            qpop = 1;
		// qpop = 1; // this makes no sense here (aac)
		qpops[nqi] = qpop;
		nqi++;
	}
	// tell caller how many query terms we used so he can normalize scores
	*nqiPtr = nqi;

	//int64_t start = gettimeofdayInMilliseconds();

	TermTable tt;
	if ( ! tt.set(20000,true,true, false , returnPops, false, false,NULL)){
		log("topics: Had error allocating a table for topic "
		    "generation: %s.",mstrerror(g_errno));
		//mfree ( buf , bufMaxLen , "Msg24" );
		return true;
	}

	Words w;

	//---> word next to both query terms should not be between by word just
	//next to one....
	//---> weight by query popularity too!

	//log("******** hashing doc *********");

	// hash each excerpt
	char *p    = buf;
	// most samples are under 5k, i've seend a 32k sample take 11ms!
	char *pend = buf + bufLen;
	while ( p < pend ) {
		// debug
		//log("docId=%"INT64" EXCERPT=%s",docId,p);
		int32_t plen ;
		if ( isUnicode ) plen = ucStrNLen(p,pend-p);
		else             plen = strlen(p);
		// p is only non-NULL if we are doing it the old way
		hashExcerpt ( q, qids, qpops, nqi, &tt, p, plen, &w, t , NULL,
			      isUnicode , repeatTable , repeatTableNumSlots ,
			      language );
		// advance to next excerpt
		if ( isUnicode ) p += plen + 2;
		else             p += plen + 1;
	}

	// hash the provided wordsPtr as one excerpt if there
	if ( wordsPtr ) 
		hashExcerpt ( q, qids, qpops, nqi, &tt, NULL,0, wordsPtr, t ,
			      scoresPtr , isUnicode , 
			      repeatTable , repeatTableNumSlots ,
			      language );

	// . compute the fingerprint/similarirtyVector from this table
	//   the same way we do for documents for deduping them at query time
	// . or we could just wait for our dedup algo to kick in... (mdw)
	//   then comment this stuff out ...
	if ( t->m_dedupSamplePercent >= 0 ) {
		char *v1 = vecs + (*numVecs * SAMPLE_VECTOR_SIZE);
		g_clusterdb.getSampleVector ( v1 , &tt );
		// compare to others done so far
		char *v2 = vecs ;
		for ( int32_t i = 0 ; i < *numVecs ; i++,v2+=SAMPLE_VECTOR_SIZE){
			char ss = g_clusterdb.getSampleSimilarity(v1,v2, 
							   SAMPLE_VECTOR_SIZE);
			// return true if too similar to another sample we did
			if ( ss >= t->m_dedupSamplePercent ) { // 80 ) {
				log(LOG_DEBUG,"topics: removed dup sample.");
				return true;
			}
		}
		// we have another vector to contend with for next time
		*numVecs = *numVecs + 1;
	}

	//log("TOOK %"INT64" ms plen=%"INT32"",gettimeofdayInMilliseconds()-start,
	//    bufLen);

	// . this termtable carries two special buckets per slot in order
	//   to hold a linked list of docids with each termid in the hash table
	// . heads is NULL if returnDocIdCount and returnDocIds are false
	int32_t *heads = master->getHeads();
	// . now hash the entries of this table, tt, into the master
	// . the master contains entries from all the other tables
	//log("have %"INT32" terms in termtable. adding to master.",
	//     tt.getNumTermsUsed());
	int32_t nt = tt.getNumTerms();
	int32_t pop = 0 ;
	for ( int32_t i = 0 ; i < nt ; i++ ) {
		// this should be indented
		//if ( ! tt.getScoreFromTermNum(i) ) continue;
		if ( ! tt.m_scores[i] ) continue;
		//int32_t ii = (int32_t)tt.getTermPtr(i);
		// then divide by that
		int32_t score = tt.getScoreFromTermNum(i) ;
		// watch out for 0
		if ( score <= 0 ) continue;
		// . get the bucket
		// . may be or may not be full (score is 0 if empty)
		int32_t n = master->getTermNum ( tt.getTermId(i) );
		// skip if 0, i've seen this happen before
		if ( tt.getTermId(i) == 0 ) continue;
		// . but now we add one more things to the termtable,
		//   a linked list field for keeping track of the docids
		//   of the documents that contain each termid
		// . grab some mem for the link
		// . "heads" is NULL if we should not do this...
		if ( heads ) {
			if ( st->m_memPtr + sizeof(DocIdLink) > st->m_memEnd ) {
				int32_t oldSize = st->m_memEnd - st->m_mem;
				int32_t newSize = oldSize + 256*1024;
				char *s = (char *)mrealloc(st->m_mem,oldSize,
							   newSize,"Msg24g");
				if ( !s )
					return log("Msg24: realloc failed.");
				int32_t off = st->m_memPtr - st->m_mem;
				st->m_mem    = s;
				st->m_memEnd = s + newSize;
				st->m_memPtr = s + off;
			}
			DocIdLink *link = (DocIdLink *)st->m_memPtr;
			st->m_memPtr += sizeof(DocIdLink);
			link->m_docId = docId;
			// if empty... make new head
			if ( master->m_scores[n] == 0 ) {
				link->m_next  = -1;
				master->m_heads[n] = (char *)link - st->m_mem;
			}
			// otherwise, add link to tail of this bucket
			else  {
				link->m_next = master->m_heads[n];
				master->m_heads[n] = (char *)link - st->m_mem;
			}
		}
		if ( returnPops ) pop = tt.m_pops[i];
		// set hi bit of "pop" if in unicode
		if ( isUnicode ) pop |= 0x80000000;
		else             pop &= 0x7fffffff;
		// . add term to master table
		// . don't keep filling it up if we failed to alloc more space
		//   because that causes getTermNum() above to crash if the
		//   table is 100% full.
		if ( ! master->addTerm ( tt.getTermId(i)             ,
					 // divide by the AVG score used
					 //tt.getScoreFromTermNum(i)+30000/pop,
					 score ,
					 //tt.getScoreFromTermNum(i)+30000,
					 0x7fffffff                  ,
					 false                       ,
					 TITLEREC_CURRENT_VERSION    ,
					 tt.getTermPtr(i) ,
					 tt.getTermLen(i) ,
					 n                ,// termNum 
					 NULL             ,// dummy(char *)link
					 pop,
					 isUnicode ) )
			break;
		// debug msg
		if ( g_conf.m_logDebugQuery ) {
		        char *ww = tt.getTermPtr(i);
			int32_t  wwlen = tt.getTermLen(i);
			char c     = ww[wwlen];
			ww[wwlen]='\0';
			log(LOG_DEBUG,"topics: master termId=%"UINT32" "
			    "score=%"INT32" cumscore=%"INT32" len=%"INT32" term=%s\n",
			    (int32_t)tt.getTermId(i),
			    score,master->getScoreFromTermId(tt.getTermId(i)),
			    wwlen,ww);
			ww[wwlen]=c;
		}
	}

	//log("master has %"INT32" terms",master->getNumTermsUsed());
	// clear any error
	if ( g_errno ) {
		log("topics: Had error getting topic candidates from document: "
		    "%s.",mstrerror(g_errno));
		g_errno = 0;
	}
	//mfree ( buf , bufMaxLen , "Msg24" );
	return true;
}


void hashExcerpt ( Query *q , uint64_t *qids , int32_t *qpops, int32_t nqi,
		   TermTable *tt , char *buf , int32_t bufLen , 
		   Words *w , TopicGroup *t , Scores *scoresPtr ,
		   bool isUnicode , char *repeatTable , 
		   int32_t repeatTableNumSlots , char language ) {
	// . bring it out
	// . allow one more word per gigabit, then remove gigabits that
	//   are that length. this fixes the problem of having the same
	//   sentence repeated in different documents, which are fairly 
	//   different as a whole, but have the same repeated sentence or
	//   paragraph.
	// . by only adding one, if the next word is a common word then
	//   we would fail to make a larger gigabit, that's why i added
	//   the maxjend code below this.
	int32_t maxWordsPerPhrase  = t->m_maxWordsPerTopic ;
	if ( t->m_topicRemoveOverlaps ) maxWordsPerPhrase += 2;
	char enforceQueryRadius = ! t->m_meta[0];
	char delimeter          = t->m_delimeter; // 0 means none (default)
	char idf                = t->m_useIdfForTopics;
	// or if no query, no query radius
	if ( ! q || q->getNumNonFieldedSingletonTerms() == 0 )
		enforceQueryRadius = false;
	// . now all the data is in buf/bufLen
	// . parse it up into Words
	// . now XmlDoc::getGigabitVector() calls us and it already has the
	//   Words pased up, so it will use a NULL buf
	if ( buf ) w->set ( isUnicode ,  // isUnicode?
			    false     ,  // isNormalized?
			    buf       ,
			    bufLen    ,
			    TITLEREC_CURRENT_VERSION,
			    true      ,  // compute word ids?
			    true      ); // has html entities?
	int32_t nw = w->getNumWords();
	// don't breech our arrays man
	if ( nw > 10000 ) nw = 10000;
	void *lrgBuf;
	int32_t lrgBufSize = 0;
       	lrgBufSize += 1002 * MAX_QUERY_TERMS * sizeof(int32_t);
	lrgBufSize += 2 * nw * sizeof(int32_t);
	lrgBufSize += 3 * nw * sizeof(char);
	lrgBufSize += nw * sizeof(uint64_t);
	lrgBuf = (char *)mmalloc(lrgBufSize, "hashExcerpt (Msg24)");
	if (! lrgBuf) {
	    nw >>= 2;
	    lrgBufSize = 0;
	    lrgBufSize += 1002 * MAX_QUERY_TERMS * sizeof(int32_t);
	    lrgBufSize += 2 * nw * sizeof(int32_t);
	    lrgBufSize += 3 * nw * sizeof(char);
	    lrgBufSize += nw * sizeof(uint64_t);
	    lrgBuf = (char *)mmalloc(lrgBufSize, "hashExcerpt (Msg24)");
	};
	if (! lrgBuf) {
	    log("topics: could not allocate local buffer "
		"(%"INT32" bytes required)", lrgBufSize);
	    return;
	};
	char *lrgBufPtr = (char *)lrgBuf;

	// . the popularity of word #i is pops[i]
	// . but we only set below if we need to
	int32_t *pops = (int32_t *) lrgBufPtr; // popularity 1-1 with first 10000 words
	lrgBufPtr += nw * sizeof(int32_t);
	char *iqt = lrgBufPtr; // is query term? 1-1 with words
	lrgBufPtr += nw * sizeof(char);
	char *icw = lrgBufPtr; // do not let frags end in these words
	lrgBufPtr += nw * sizeof(char);
	int32_t *qtrs = (int32_t *)lrgBufPtr; // the raw QTR scores (aac)
	lrgBufPtr += nw * sizeof(int32_t);

	// record list of word positions for each query term
	int32_t *pos = (int32_t *)lrgBufPtr;
	lrgBufPtr += MAX_QUERY_TERMS * 1000 * sizeof(int32_t);
	int32_t *posLen = (int32_t *)lrgBufPtr;
	lrgBufPtr += MAX_QUERY_TERMS * sizeof(int32_t);
	int32_t *posPtr = (int32_t *)lrgBufPtr;
        lrgBufPtr += MAX_QUERY_TERMS * sizeof(int32_t);
	//for ( int32_t i = 0 ; q && i < q->getNumTerms() ; i++ ) { (aac)
	for (int32_t i = 0; q && i < q->m_numWords && i < MAX_QUERY_TERMS; i++) {
		posLen[i] = 0; posPtr[i] = 0; }

	// skip punct
	int32_t i  = 0;
	if ( i < nw && w->isPunct(i) ) i++;
	qtrs[i] = 0;
	uint64_t *wids = (uint64_t *)lrgBufPtr;
	lrgBufPtr += nw * sizeof(uint64_t);
	// record the positions of all query words
	char **wp   = w->m_words;
	int32_t  *wlen = w->m_wordLens;
	int32_t   step = 2;
	int64_t *rwids  = w->getWordIds();
	int32_t      *scores = NULL;

	// . now we keep a hash table to zero out repeated fragments
	// . it uses a sliding window of 5 words
	// . it stores the hash of those 5 words in the hash table
	// . if sees how many 5-word matches it gets in a row
	// . the more matches it gets, the more it demotes the word scores
	// . these are stored in the weights class
	// . a repeatScore of 0 means to demote it out completely, 100 means
	//   it is not repeated at all
	// . multiply the final gigabit score by the repeatScore/100.
	char *repeatScores = lrgBufPtr;
	lrgBufPtr += nw * sizeof(char);
	setRepeatScores ( repeatScores , rwids , nw , repeatTable , 
 			  repeatTableNumSlots , w );

	QUICKPOLL(0);
	// single char length in bytes, etc.
	char oneChar    = 1;
	char twoChars   = 2;
	char threeChars = 3;
	if ( isUnicode ) {
		oneChar    = 2;
		twoChars   = 4;
		threeChars = 6;
	}
	// . advance one word at a time if doing it the new way
	// . also, the word ids will already be set, so use those to see what
	//   is indexable and what isn't
	if ( ! buf ) { 
		step   = 1; 
		scores = scoresPtr->m_scores;
	}
	// loop over the words in our sample
	//for ( ; i < nw ; i += 2 ) {
	for ( ; i < nw ; i += step ) {
	        qtrs[i] = 0;
		// do we have pre-supplied words and scores from XmlDoc.cpp?
		//if ( rwids ) {
		// skip if not indexable
 		if ( ! rwids[i] ) continue;
		// or if score is <= 0
		if ( scores && scores[i] <= 0 ) continue;
		// or repeated too much
		if ( repeatScores[i] <= 20 ) continue;
		//}
		// reset popularity
		if   ( idf ) pops[i] = -1;
		else         pops[i] =  1; // assume all same if not using idf
		// reset "is query term" array
		iqt[i] = 0;
		// store the id
		int32_t encodeType = csISOLatin1;
		if ( isUnicode ) encodeType = csUTF16;
		wids[i] = hash64d(wp[i], wlen[i], encodeType);
		// . is it a common word?
		// . it is if it is just one letter
		// . what about X-windows coming up for a 'windows' query?
		//   or e-mail coming up for a query?
		// . METALINCS likes to have 1 digit topics
		if ( wlen[i] <= oneChar && is_lower(wp[i][0]) ) icw[i] = 1;
		// unicode ~equivalent
		//if ( isUnicode && wlen[i] == 2 ) icw[i] = 1;
		// 2004 is common here but if it makes it in, don't remove it
		// in the top topics list... no. loses 'atari 2600' then!
		//else if ( is_digit(w->getWord(i)[0]) ) 
		//	icw[i] = 1;
#ifndef _METALINCS_
		else icw[i] = isCommonWord ( (int32_t)rwids[i] );
#else
		// always allow gigabits that start with numbers for metalincs
		else if ( ! is_digit(wp[i][0])) 
			icw[i] = isCommonWord ( (int32_t)rwids[i] );
		else                            
			icw[i] = 0;
#endif
		// debug msg
		/*
		char *s    = w->getWord(i);
		int32_t  slen = w->getWordLen(i);
		char  c    = s[slen];
		s[slen]='\0';
		log("icw=%"INT32" %s",icw[i],s);
		s[slen]=c;
		*/
		// is it a query term? if so, record its word # in "pos" arry
		for ( int32_t j = 0 ; j < nqi ; j++ ) {
			if ( wids[i] != qids[j] ) continue;
			if ( posLen[j] >= 1000  ) continue;
			pos    [ 1000 * j + posLen[j] ] = i;
			posLen [ j ]++;
			// mark this word so if a phrase only has
			// all query terms we do not hash it
			iqt[i] = 1;
			break;
		}
	}

	QUICKPOLL(0);
	// max score -- ONE max scoring hits per doc
	int32_t maxScore = nqi * MAX_SCORE_MULTIPLIER;
	// this happens when generating the gigabit vector for a single doc
	// so don't hamper it to such a small ceiling
	if ( nqi == 0 ) maxScore = ALT_MAX_SCORE;

	// skip punct
	i = 0;
	if ( i < nw && w->isPunct(i) ) i++;
	// score each word based on distance to query terms
	int32_t score;
	// loop through all the words
	//for ( ; i < nw ; i += 2 ) {x
	for ( ; i < nw ; i += step ) {
		// debug point
		//if ( strncasecmp( wp[i],"Microsoft",9) == 0 )
		//	log("hey");
		// do we have pre-supplied words and scores from XmlDoc.cpp?
		//if ( rwids ) {
		// skip if not indexable
		if ( ! rwids[i] ) continue;
		// or if score is <= 0
		if ( scores && scores[i] <= 0 ) continue;
		//}
		// skip if in a repeat chunk of doc
		if ( repeatScores[i] <= 20 ) continue;
		// protect against misspelled html entities (aac) 
		if ( (wp[i][-oneChar] == '&' && is_alnum(wp[i][0])) ||
		     (wp[i][0] == '&' && is_alnum(wp[i][oneChar]))   )	continue;
		// no more one or two letter gigabits (aac)
		if ( wlen[i] < threeChars && (! is_digit(wp[i][0])) ) continue;
		//continue; //mdw
		// if we had a delimeter, previous word must have it
		// or be the first punct word
		if ( delimeter && i >= 2 && ! w->hasChar(i-1,delimeter) ) 
			continue;
		// skip if a query term, it's ineligible
		//if ( w->getWordLen(i) == 0 ) continue;
		// if query is NULL, assume we are restricting to meta tags
		// and query is not necessary
		if   ( enforceQueryRadius ) score = 0;
		else                        score = ALT_START_SCORE;
		int32_t j ;
		int32_t nm = 0; // number of matches
		for ( j = 0 ; j < nqi ; j++ ) {
			// skip if no query terms in doc for query term #j
			if ( posLen[j] <= 0 ) continue;
			// get distance in words
			int32_t d1 = i - pos[ 1000 * j + posPtr[j] ] ;
			if ( d1 < 0   ) d1 = d1 * -1;
			if ( posPtr[j] + 1 >= posLen[j] ) {
				if (d1 >= QTR_ZONE_3) continue;
				if (iqt[i] || icw[i] || 
				    wlen[i] <= threeChars) {
				    // common word, query terms, int16_t words
				    // are all second class citizens when it
				    // comes to scoring: they get a small
				    // bonus, to ensure that they are
				    // considered in the next stage, but do not
				    // benefit from QPOP and multiple hit
				    // bonuses (aac)
				    score += QTR_BONUS_CW;
				    continue; 
				};
				if (d1 < QTR_ZONE_0) 
				    score += QTR_BONUS_0;
				else if (d1 < QTR_ZONE_1) 
				    score += QTR_BONUS_1;
				else if (d1 < QTR_ZONE_2) 
				    score += QTR_BONUS_2;
				else                         
				    score += QTR_BONUS_3;
				nm++;
				score *= qpops[j];
				continue;
			}
			int32_t d2 = pos[ 1000 * j + posPtr[j] + 1 ] - i ;
			if ( d2 < 0  ) d2 = d2 * -1;
			if ( d2 > d1 ) {
				// if      ( d1 >=20 ) continue;
				// if      ( d1 <  4 ) score += 1000;
				// else if ( d1 <  8 ) score += 800;
				// else if ( d1 < 12 ) score += 500;
				// else                score += 200;
				// nm++;
				// score *= qpops[j];
				// continue;
				if (d1 >= QTR_ZONE_3) continue;
				if (iqt[i] || icw[i] || 
				    wlen[i] <= threeChars) {
				    // common word, query terms, int16_t words
				    // are all second class citizens when it
				    // comes to scoring: they get a small
				    // bonus, to ensure that they are
				    // considered in the next stage, but do not
				    // benefit from QPOP and multiple hit
				    // bonuses (aac)
				    score += QTR_BONUS_CW;
				    continue; 
				};
				if (d1 < QTR_ZONE_0) 
				    score += QTR_BONUS_0;
				else if (d1 < QTR_ZONE_1) 
				    score += QTR_BONUS_1;
				else if (d1 < QTR_ZONE_2) 
				    score += QTR_BONUS_2;
				else                         
				    score += QTR_BONUS_3;
				nm++;
				score *= qpops[j];
				continue;
			}
			// if      ( d2 >=20 ) { posPtr[j]++; continue; }
			// if      ( d2 <  4 ) score += 1000;
			// else if ( d2 <  8 ) score += 800;
			// else if ( d2 < 12 ) score += 500;
			// else                score += 200;
			// nm++;
			// score  *= qpops[j];
			if (d2 >= QTR_ZONE_3) { posPtr[j]++; continue; };
			if (iqt[i] || icw[i] || wlen[i] <= threeChars) {
			    // common word, query terms, int16_t words
			    // are all second class citizens when it
			    // comes to scoring: they get a small
			    // bonus, to ensure that they are
			    // considered in the next stage, but do not
			    // benefit from QPOP and multiple hit
			    // bonuses (aac)
			    score += QTR_BONUS_CW;
			    continue; 
			};
			if (d2 < QTR_ZONE_0) score += QTR_BONUS_0;
			else if (d2 < QTR_ZONE_1) score += QTR_BONUS_1;
			else if (d2 < QTR_ZONE_2) score += QTR_BONUS_2;
			else                      score += QTR_BONUS_3;
			nm++;
			score *= qpops[j];
			continue;
			posPtr[j]++;
		}

		// skip if too far away from all query terms
		if ( score <= 0 ) continue;

		// no longer count closeness to query terms for score,
		// just use # times topic is in doc(s) and popularity
		//score = 1000;

		// set pop if it is -1
		if ( pops[i] == -1 ) {
			pops[i] = g_speller.
				getPhrasePopularity( wp[i],wids[i], true,
						     language );
		       // decrease popularity by half if 
		       // capitalized so Jack does not have 
		       // same pop as "jack"
		       if ( is_upper (wp[i][0]) ) pops[i] >>= 1;
		       if ( pops[i] == 0 ) pops[i] = 1;
		       QUICKPOLL(0);
		}

		// give a boost for multiple hits 
		// the more terms in range, the bigger the boost
		if ( nm > 1 ) {
			//log("nm=%"INT32"",nm);
			score += MULTIPLE_HIT_BOOST * nm;
		};

		// save the raw QTR score
		qtrs[i] = score;
	};

	QUICKPOLL(0);
	int32_t mm = 0;
	// skip punct
	i = 0;
	if ( i < nw && w->isPunct(i) ) i++;
	for ( ; i < nw ; i += step ) {
	        float pop;
		int32_t score;
		int32_t bonus;
		// must start with a QTR-scoring word
	        if (qtrs[i] <= 0) continue;
		// add it to table
		// init for debug here
		char *ww;
		int32_t  wwlen;
		//char  c;
		int32_t  ss;
		ww    = wp  [i]; // w->getWord(i);
		wwlen = wlen[i]; // w->getWordLen(i);
		if ( icw[i] ) {
		    // . skip this and all phrases if we're "to"
		    // . avoid "to use..." "to do..." "to make..." annoying
		    // . "to" has score 1, "and" has score 2, "of" is 3,
		    // . "the" is 4, "this" is 5
		    if ( icw[i] <= 5 ) continue;
		    // cannot start with any common word, unless capitalized
		    if ( is_lower(wp[i][0]) ) continue;
		}
		// if a hyphen is immediately before us, we cannot start
		// a phrase... fu-ture, preven-tion
		if ( i > 0 && wp[i][-oneChar]=='-' ) continue;
		// same for colon
		if ( i > 0 && wp[i][-oneChar]==':' ) continue;
		// . if a "'s " is before us, we cannot start either
		// . "valentine's day cards"
		if ( i >= 3 && 
		     wp[i][-threeChars]=='\'' && 
		     wp[i][-twoChars  ]=='s' &&
		     is_space(wp[i][-oneChar]) ) continue;
		// or if our first char is a digit and a "digit," is before us
		// because we don't want to break numbers with commas in them
		if ( is_digit(wp[i][0]) && i >= 2 && wp[i][-oneChar]==',' && 
		     is_digit(wp[i][-twoChars]) ) continue;
		// set initial popularity
		if (pops[i] > 0) {
		    pop = ((float) pops[i]) / MAXPOP;
		}
		else {
		    pop = 1.0 / MAXPOP;
		};
		// set initial score and bonus
		score = qtrs[i];
		bonus = 0;
		uint64_t  h = wids[i]; // hash value
		// if first letter is upper case, double the score
		//if ( is_upper (w->getWord(i)[0]) ) score <<= 1;

		// . loop through all phrases that start with this word
		// . up to 6 real words per phrase
		// . 'j' counts our 'words' which counts a $ of puncts as word
		int32_t jend    = i + maxWordsPerPhrase * 2; // 12;
		int32_t maxjend = jend ;
		if ( t->m_topicRemoveOverlaps ) maxjend += 8;
		if ( jend    > nw ) jend    = nw;
		if ( maxjend > nw ) maxjend = nw;

		QUICKPOLL(0);

		int32_t count = 0;
		int32_t nqc   = 0; // # common/query words in our phrase
		int32_t nhw   = 0; // # of "hot words" (contribute to score)
		if ( scores ) mm = scores[i];
		//for ( int32_t j = i ; j < jend ; j += 2 ) {
		for ( int32_t j = i ; j < jend ; j += step ) {
			// skip if not indexable
			if ( ! rwids[j] ) continue;
			// or if score is <= 0
			if ( scores && scores[j] <= 0 ) continue;
			if ( repeatScores[j] <= 20 ) continue;
			// no ending in ing on capitalized
			if ( wlen[j] > threeChars &&
			     wp[j][wlen[j]-oneChar   ]=='g' &&
			     wp[j][wlen[j]-twoChars  ]=='n' &&
			     wp[j][wlen[j]-threeChars]=='i' &&
			     is_lower(wp[j][0]) )
				continue;
			if (j == i) {
			    if (icw[j] || wlen[j] < threeChars) bonus -= FWC_PENALTY;  
			    // if word is 4 letters or more and ends in ed, do not
			    // allow to be its own gigabit
			    if ( wlen[j] > threeChars &&
				 wp[j][wlen[j]-oneChar ]=='d' &&
				 wp[j][wlen[j]-twoChars]=='e' )
				    continue;
			    // no more "com" gigabits, please! (aac)
			    if ( wlen[j] == threeChars &&
				 wp[j][0       ]=='c' &&
				 wp[j][oneChar ]=='o' &&
				 wp[j][twoChars]=='m') continue;
			};
			// let's generalize even more! do not allow common
			// single words as gigabits, with 250+ pop
			//if ( pop > 100 && j == i && is_lower(wp[j][0]) ) continue;
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
				if (wlen[j-1]>t->m_topicMaxPunctLen) break;
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
				if ( delimeter && w->hasChar(j-1,delimeter)) 
					break;
				// make sure we could phrase across last word
				//if ( wlen[j-1] > 1 && 
				//   bits.getPunctuationBits(wp[j-1],wlen[j-1])
				//   == 0 ) break;
				// accumulate the phrase's hash
				h = hash64 ( h , wids[j] );
				// set pop if it is -1
				if ( pops[j] == -1 ) {
					pops[j]= g_speller.
						getPhrasePopularity( wp[j],
						wids[j], true, language );
					// decrease popularity by half if 
					// capitalized so Jack does not have 
					// same pop as "jack"
					if ( is_upper (wp[j][0]) ) 
						pops[j] >>= 1;
					// why was this in there?
					if ( pops[j] <= 0 ) pops[j] = 1;
					QUICKPOLL(0);
				}
				// adjust popularity
				pop = (pop * pops[j])/MAXPOP;
				// watch our for overflow
				if ( pop <= 0.0 ) pop = 1.0/MAXPOP;
				// get lowest of scores
				if ( scores && scores[j] > mm )	mm = scores[j];
			}

			// keep track of words
			count++;
			if ( iqt[j] || icw[j] ) {
			    nqc++; // increment number of query/commoners
			}
			else if (qtrs[j] > 0) {
			    score += qtrs[j];
			    nhw++; // increment "hot word" counter
			};
			// keep phrasing until next punct word is delimeter
			// or the end
			if ( delimeter ) {
				// if we end on a punct word, then hash
				// our phrase, otherwise, only hash it if
				// the next word has the delimeter
				if ( j+2 < jend && ! w->hasChar(j+1,delimeter))
					continue;
			}
			// otherwise, ensure phrase is not ALL query terms
			else {
				// if phrase is all commoners  & query skip it
				if ( nqc == count ) {
#ifdef DEBUG_MSG24
				    char saveChar = ww[wwlen];
				    ww[wwlen] = '\0';
				    log("topics: phrase is all QT or CW; skipping" 
					    " phrase %s", ww);
				    ww[wwlen] = saveChar;
#endif
				    continue;
				};
			}
			// . skip if we're common, pair across common words
			// . BUT it is common to end a meta from tag in ".com"
			//   so we should not count that one as common
			if ( icw[j] ) { 
				// allow for more words only for purposes
				// of fixing the ABCD and BCDE overlap bug 
				// without having to raise jend for all cases
				if ( jend < maxjend ) jend++; 
				continue;
			}
			// do not stop if - . or @ follows us right b4 alnum
			if ( j+1 < nw && is_alnum(wp[j+1][oneChar]) ) {
			if ( wp[j+1][0]=='-' ) continue;
			if ( wp[j+1][0]=='.' ) continue;
			if ( wp[j+1][0]=='\'') continue;
			if ( wp[j+1][0]=='@' ) continue;
			// . do not split phrases between capitalized words
			// . this should fix the Costa Rica, Costa Blah bug
			// . it may decrease score of Belkin for query 
			//   'Belkin Omni Cube' but that's ok because if 
			//   Belkin is important it will be used independently.
			if ( is_upper(wp[j][0]) &&
			     j + 2 < nw &&
			     wp[j+1][0]==' ' &&
			     is_upper(wp[j+2][0]) &&
			     wlen[j+1] == oneChar &&
			     t->m_maxWordsPerTopic > 1 ) 
				continue;
			}
			// do not mix caps
			if ( is_upper(wp[i][0]) != is_upper(wp[j][0]) )
			     continue;
			// . do not stop on a single capital letter
			// . so we don't stop on "George W->" (george w. bush)
			// . i added the " && j > i" so METALINCS can have
			//   single digit gigabits
			if ( wlen[j] == oneChar && j > i ) continue;
			// . do not split after Mr. or St. or Ms. or Mt. ...
			// . fixes 'st. valentines day'
			if ( wlen[j] == twoChars && is_upper(wp[j][0]) &&
			     wp[j][twoChars]=='.' ) continue;
			// sgt. or col.
			if ( wlen[j] == threeChars && wp[j][threeChars]=='.' ){
				if ( to_lower(wp[j][0       ])=='s' &&
				     to_lower(wp[j][oneChar ])=='g' &&
				     to_lower(wp[j][twoChars])=='t' ) continue;
				if ( to_lower(wp[j][0       ])=='c' &&
				     to_lower(wp[j][oneChar ])=='o' &&
				     to_lower(wp[j][twoChars])=='l' ) continue;
				if ( to_lower(wp[j][0       ])=='m' &&
				     to_lower(wp[j][oneChar ])=='r' &&
				     to_lower(wp[j][twoChars])=='s' ) continue;
			}
			// . do not split commas in numbers
			// . like 1,000,000,000
			if ( j >= 2 && 
			              wp[j][-oneChar ]==',' && 
			     is_digit(wp[j][-twoChars])     &&
			     wp[j][wlen[j]]==',' && 
			     is_digit(wp[j][wlen[j]+oneChar]))
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
			if ( i > 0 && ww[-oneChar] == '(' ) { 
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
			if ( ww[0] == '(' && ww[wwlen-oneChar] == ')' ) {
				ww++; wwlen -= twoChars; }
			// now double score if capitalized, we need more
			// proper nouns for topic clustering to work better,
			// but it doesn't count if start of a sentence, so 
			// there must be some alnum word right before it.
			//if (is_upper(ww[0]) && !isUnicode && wwlen>=2 && 
			if ( is_upper(ww[0]) && wwlen>=twoChars && 
			     is_alnum(ww[-twoChars])) 
				ss <<= 1; // 1;
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
			ss = score;
			if (nhw > 0) ss /= nhw;
			ss += bonus;
			float boost;
			if ( ((float)nhw) / count < SPARSE_MARK) 
			    ss -= SPARSE_PENALTY;	
			if (pop < POP_ZONE_0)      boost = POP_BOOST_0;
			else if (pop < POP_ZONE_1) boost = POP_BOOST_1;
			else if (pop < POP_ZONE_2) boost = POP_BOOST_2;
			else if (pop < POP_ZONE_3) boost = POP_BOOST_3;
			else                       boost = POP_BOOST_4;	
			ss = (int32_t)(boost *ss);
			if ( ss <= 0 ) ss = 1;
			// store it
			int32_t ipop = (int32_t)(pop * MAXPOP);
			if ( ! tt->addTerm ((int64_t)h,ss,maxScore,false,
					    TITLEREC_CURRENT_VERSION    ,
					    ww,wwlen,-1,NULL,ipop) ) {
				log("topics: No memory to grow table.");
				return;
			}

			// stop after indexing a word after a int32_t string of
			// punct, this is the overlap bug fix without taking
			// a performance hit. hasPunct above will remove it.
			if ( j > i && wlen[j-1] > twoChars ) break;
		}
	}
	// clear any error
	if ( g_errno ) {
		log("topics: Had error getting topic candidates from "
		    "document: %s.",mstrerror(g_errno));
		g_errno = 0;
	}
	mfree(lrgBuf, lrgBufSize, "hashExcerpt (Msg24)");
}

// taken from Weights.cpp's set3() function
void setRepeatScores ( char      *repeatScores        ,
		       int64_t *wids                ,
		       int32_t       nw                  ,
		       char      *repeatTable         ,
		       int32_t       repeatTableNumSlots ,
		       Words     *words               ) {
	// if no words, nothing to do
	if ( nw == 0 ) return;

	char      *ptr      = repeatTable;
	int32_t       numSlots = repeatTableNumSlots;
	int64_t *hashes   = (int64_t *)ptr; ptr += numSlots * 8;
	int32_t      *vals     = (int32_t      *)ptr; ptr += numSlots * 4;

	int64_t   ringWids [ 5 ];
	int32_t        ringPos  [ 5 ];
	int32_t        ringi = 0;
	int32_t        count = 0;
	int64_t   h     = 0;

	// make the mask
	uint32_t mask = numSlots - 1;

	// clear ring of hashes
	memset ( ringWids , 0 , 5 * sizeof(int64_t) );

	// for sanity check
	//int32_t lastStart = -1;

	// count how many 5-word sequences we match in a row
	int32_t matched    = 0;
	int32_t matchStart = -1;

	// reset
	memset ( repeatScores , 100 , nw );

	// return until we fix the infinite loop bug
	//return;

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
		int32_t n = h & mask;
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
	loop:
		// all done if empty
		if ( ! hashes[n] ) {
			// add ourselves to the hash table now
			hashes[n] = h;
			// this is where the 5-word sequence starts
			vals  [n] = matchStart+1;
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
				repeatScores[j] = 0;
			// get next word
			continue;
		}
		// get next in chain if hash does not match
		if ( hashes[n] != h ) { 
			// wrap around the hash table if we hit the end
			if ( ++n >= numSlots ) n = 0; 
			// check out bucket #n now
			goto loop; 
		}
		// save start of matching sequence for demote loop
		if ( matched == 0 ) matchStart = start;
		// inc the match count
		matched++;
	}
	// if we ended without nulling out some matches
	if ( matched < 3 ) return;
	for ( int32_t j = matchStart ; j < nw ; j++ ) repeatScores[j] = 0;

}

/*
// is it a stop word?
char isCommonPhrase ( int32_t h ) {
	static TermTable  s_table;
	static bool       s_isInitialized = false;
	// . these have the stop words above plus some foreign stop words
	// . these aren't
	// . i shrunk this list a lot
	// . see backups for the hold list
	// . i shrunk this list a lot
	// . see backups for the hold list
	static char      *s_stopPhrases[] = {
		"all rights reserved" ,
		"in addition" ,
		"for example" ,
		"for more information" 
	};
	// include a bunch of foreign prepositions so they don't get required
	// by the bitScores in IndexTable.cpp
	if ( ! s_isInitialized ) {
		// set up the hash table
		if ( ! s_table.set ( sizeof(s_stopPhrases) * 2 ) ) 
			return log("Msg24::isCommonPhrase: error set table");
		// now add in all the stop words
		int32_t n = (int32_t)sizeof(s_stopPhrases)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set the phrases
			char *sw    = s_stopPhrases[i];
			int32_t  swlen = strlen ( sw );
			Words w;
			w->set ( false , sw , swlen );
			int32_t h = hash64d ( w->getWord   (0),
						     w->getWordLen(0));
			for ( int32_t j = 1 ; j < w->getNumWords() ; j++ ) 
				int32_t h2 = 

			int32_t  swh   = hash64d ( sw , swlen );
			s_table.addTerm ((int32_t)swh,i+1,0x7fffffff,true);
		}
		s_isInitialized = true;
	} 

	// . all 1 char letter words are stop words
	// . good for initials and some contractions
	//if ( len == 1 && is_alpha(*s) ) return true;

	// get from table
	return (char)s_table.getScoreFromTermId ( h );
}
*/

int32_t Msg24::getStoredSize ( ) {
	// store number of topics into 4 bytes
	int32_t size = 4;
	// store number of topics we have
	// all related topics that have scores >= m_minTopicScore
	for ( int32_t i = 0 ; i < m_numTopics ; i++ ) {
		// get group info
		//TopicGroup *t = &m_topicGroups[m_topicGids[i]];
		// break if buf is too small
		//if ( size + m_topicLens[i] + 2 + 8 > MAX_REPLY_LEN ) break;
		// include \0 and 4 byte score and 4 byte topic length
		size += 
			4 + // topic ptr
			4 + // topicScore
			4 + // topicLen
			4 + // numDocIds
			4 + // ptr to docids
			4 + // topic pop
			1 + // topic gid
			m_topicLens[i] + 1 +   // topic string with \0
			m_topicNumDocIds[i]*8; // actual docids
	}
	return size;
}

// . serialize ourselves for the cache
// . returns bytes written
// . returns -1 and sets g_errno on error
// . just like serializing the reply
int32_t Msg24::serialize ( char *buf , int32_t bufLen ) { 
	char *p = buf;
	// store number of topics
	*(int32_t *)p = m_numTopics; p += 4;
	// if no topics, bail
	if ( m_numTopics <= 0 ) return 4;
	// then the ptrs, with offset relative to m_topicPtrs[0] so
	// deserialize works
	char *base = m_topicPtrs[0];
	for ( int32_t i = 0 ; i < m_numTopics ; i++ ) {
		*(int32_t *)p = m_topicPtrs[i] - base; p += 4; }
	// then the scores
	gbmemcpy ( p , m_topicScores   , m_numTopics * 4 ); p += m_numTopics * 4;
	gbmemcpy ( p , m_topicLens     , m_numTopics * 4 ); p += m_numTopics * 4;
	gbmemcpy ( p , m_topicNumDocIds, m_numTopics * 4 ); p += m_numTopics * 4;
	// these m_topicDocIds, are just essentially placeholders for ptrs
	// to the docids, just like the topic ptrs above, but these call all
	// be NULL if we didn't get back the list of docids for each gigabit
	p += m_numTopics * 4;
	// then the popularity rating of each topic
	gbmemcpy ( p , m_topicPops     , m_numTopics * 4 ); p += m_numTopics * 4;
	gbmemcpy ( p , m_topicGids     , m_numTopics     ); p += m_numTopics;
	// then the text
	for ( int32_t i = 0 ; i < m_numTopics ; i++ ) {
		gbmemcpy ( p , m_topicPtrs[i] , m_topicLens[i] ) ;
		p += m_topicLens[i];
		*p++ = '\0';
	}
	// and one array of docids per topic
	for ( int32_t i = 0 ; i < m_numTopics ; i++ ) {
		gbmemcpy ( p , m_topicDocIds[i] , m_topicNumDocIds[i] * 8 );
		p += m_topicNumDocIds[i] * 8;
		// sanity check
		//for ( int32_t k = 0 ; k < m_topicNumDocIds[i] ; k++ )
		//	if ( m_topicDocIds[i][k] & ~((int64_t)DOCID_MASK) ) {
		//		log("query: Msg24 bad docid in serialize.");
		//		char *xx = NULL; *xx = 0; 
		//	}
	}
	// debug msg
	//log("in nt=%"INT32"",*nt);
	if ( p - buf > bufLen ) {
		log("query: Msg24 serialize overflow.");
		char *xx = NULL; *xx = 0;
	}
	return p - buf;
}

// . deserialize ourselves for the cache
// . returns bytes written
// . returns -1 and sets g_errno on error
// . Msg40 owns the buffer, so we can reference it without having to copy
int32_t Msg24::deserialize ( char *buf , int32_t bufLen ) {
	// sanity check, i've seen this happen before when the handle of
	// the Msg24 runs out of memory at a certain plance and ends up 
	// sending back a 0 length reply
	if ( bufLen < 4 ) {
		g_errno = EBADREPLY;
		log("query: Msg24::deserialize: bad reply.");
		return -1;
	}
	char *p = buf;
	m_numTopics   = *(int32_t *)p; p += 4;
	// another sanity check, just in case
	if ( bufLen < m_numTopics * (6*4+1) ) {
		g_errno = EBADREPLY;
		log("query: Msg24::deserialize: bad reply 2.");
		return -1;
	}
	m_topicPtrs      =  (char      **)p; p += m_numTopics * 4;
	m_topicScores    =  (int32_t       *)p; p += m_numTopics * 4;
	m_topicLens      =  (int32_t       *)p; p += m_numTopics * 4;
	m_topicNumDocIds =  (int32_t       *)p; p += m_numTopics * 4; //voters
	m_topicDocIds    =  (int64_t **)p; p += m_numTopics * 4; //placehldrs
	m_topicPops      =  (int32_t       *)p; p += m_numTopics * 4;
	m_topicGids      =                p; p += m_numTopics;
	// . make ptrs to topic text
	// . we were just provided with offsets to make it portable
	char *off = p;
	for ( int32_t i = 0 ; i < m_numTopics ; i++ ) {
		m_topicPtrs[i] = (int32_t)m_topicPtrs[i] + off;
		p += m_topicLens[i] + 1;
	}
	// now for the array of docids per topic
	for ( int32_t i = 0 ; i < m_numTopics ; i++ ) {
		m_topicDocIds[i] = (int64_t *)p;
		p += m_topicNumDocIds[i] * 8;
		// sanity check
		//for ( int32_t k = 0 ; k < m_topicNumDocIds[i] ; k++ )
		//	if ( m_topicDocIds[i][k] & ~((int64_t)DOCID_MASK) ) {
		//		log("query: Msg24 bad docid in deserialize.");
		//		char *xx = NULL; *xx = 0; 
		//	}
	}
	if ( p - buf > bufLen ) {
		log("query: Msg24 deserialize overflow.");
		char *xx = NULL; *xx = 0;
	}
	return p - buf;
}


//if we already have the msg20s, just generate the gigabits from those.
bool Msg24::generateTopicsLocal ( char       *coll                ,
				  int32_t        collLen             ,
				  char       *query               ,
				  int32_t        queryLen            ,
				  Msg20**     msg20Ptrs           ,
				  int32_t        numMsg20s           ,
				  char       *clusterLevels       ,
				  TopicGroup  *topicGroups        ,
				  int32_t         numTopicGroups     ,
				  unsigned char lang              ) { // (aac)
	// force it to be true, since hi bit is set in pops if topic is unicode
	m_returnPops       = true;
	// warning
	if ( ! coll ) log(LOG_LOGIC,"net: NULL collection. msg24.");
	// force it
	m_returnDocIdCount = true;
	// if we don't get docids, then deserialize doesn't work because it
	// expects the docids to be valid.
	m_returnDocIds     = true;
	// reset
	m_numTopics = 0;
	//m_docsToScanForTopics = docsToScanForTopics;
	//m_minTopicScore       = minTopicScore;
	//m_maxTopics           = maxTopics;
	m_numDocIds          = 0;
	m_coll               = coll;
	m_collLen            = collLen;
	// bail if no operations to do

	int32_t numTopicsToGen = topicGroups->m_numTopics;
	// get the min we have to scan
	int32_t docsToScanForTopics = topicGroups[0].m_docsToScanForTopics;

	for ( int32_t i = 1 ; i < numTopicGroups ; i++ ) {
		int32_t x = topicGroups[i].m_docsToScanForTopics ;
		if ( x > docsToScanForTopics ) docsToScanForTopics = x;

		if ( topicGroups[i].m_numTopics > numTopicsToGen )
			numTopicsToGen = topicGroups[i].m_numTopics;
	}
	// bail if none
	if ( docsToScanForTopics <= 0 ) return true;
	if ( numTopicsToGen == 0      ) return true;

	m_startTime = gettimeofdayInMilliseconds();

	// save, caller should not delete this!
	m_topicGroups    = topicGroups;
	m_numTopicGroups = numTopicGroups;
	// truncate
	//if ( maxTopics > MAX_TOPICS ) maxTopics = MAX_TOPICS;
	// truncate
	//if ( numDocIds > MAX_DOCIDS_TO_SCAN )
	//	numDocIds = MAX_DOCIDS_TO_SCAN ;
	// 	if ( numDocIds > docsToScanForTopics )
	// 		numDocIds = docsToScanForTopics ;


	State24 st;
	st.m_slot             = NULL;
	st.m_niceness         = 0;
	st.m_numRequests      = numMsg20s;
	st.m_numReplies       = numMsg20s;

	gbmemcpy ( st.m_query , query , queryLen );
	st.m_query [ queryLen ] = '\0';
	st.m_queryLen = queryLen;
	st.m_qq.set ( st.m_query , st.m_queryLen , NULL , 0, 2 , true );

	st.m_numTopicGroups   = m_numTopicGroups;
	gbmemcpy(st.m_topicGroups, m_topicGroups, 
	       sizeof(TopicGroup) * m_numTopicGroups);
	st.m_maxCacheAge      = 0;
	st.m_addToCache       = false;
	st.m_returnDocIdCount = m_returnDocIdCount;
	st.m_returnDocIds     = m_returnDocIds;
	st.m_returnPops       = true; // ??? use this in dedup vector?
	st.m_docIds           = NULL;
	st.m_numDocIds        = 0;
	st.m_clusterLevels    = clusterLevels;
	st.m_n                = 0;
	st.m_i                = 0;
	st.m_coll             = coll;
	st.m_msg20Ptrs        = msg20Ptrs;
	st.m_msg20            = NULL;


	TermTable master;
	if ( ! master.set ( 20000 , true , true , 
			    st.m_returnDocIdCount | st.m_returnDocIds ,
			    st.m_returnPops , true, false, NULL ) ) {
		log("topics: Could not allocate memory for topic generation.");
		return true;
	}


	char *buf     = NULL;
	int32_t  bufSize = 0;
	for ( int32_t i = 0 ; i < st.m_numTopicGroups ; i++ ) {
		// get ith topic group descriptor
		TopicGroup *t = &st.m_topicGroups[i];
		// . generate topics for this topic group
		// . serialize them into "p"
		// . getTopics will realloc() this "buf" to exactly the size
		//   it needs
		getTopics ( &st , t , &master , &st.m_qq , i , 
			    // getTopics will realloc this buffer
			    &buf , &bufSize , NULL , NULL , NULL, lang ); // (aac)
		// clear master table each time
		if ( i + 1 < st.m_numTopicGroups ) master.clear();
	}
	//}

	// free mem now to avoid fragmentation
	master.reset();
	deserialize ( buf , bufSize );

	//we are pointing into buf, but we want to make sure it gets freed when we
	//are done with it, so we make it our m_reply
	m_reply = buf;
	m_replySize = bufSize;
	g_stats.addStat_r ( 0           ,
			    m_startTime , 
			    gettimeofdayInMilliseconds(),
			    "get_gigabits",
			    0x00d1e1ff ,
			    STAT_QUERY );
	return true;
}
