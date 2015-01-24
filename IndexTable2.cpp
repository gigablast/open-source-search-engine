#include "gb-include.h"

#include "IndexTable2.h"
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

// when using Msg39.cpp to call addLists_r() on 2 5MB prefab lists:
// 1.  it takes 100ms just to load the 10MB from mainMem into L1cache.(133MB/s)
// 2.  Then we read AND write 10MB to the hash table in the L1 cache, 
//     which @ 400MB/s takes 25ms + 25ms = 50ms.
// 2b. Also, when hashing 2 millions keys, each key will read and write a 
//     full slot in hash table, so each slot is 10 bytes, that's 20 megs
//     @ 400MB/s which is 50ms + 50ms = 100ms.
// 2c. Then there's chaining, about 10% of docids will chain, 10ms
// 3.  Then we loop 2432 times over the 20k hash table in the L1 cache to
//     get the winners which is reading 49MB @ 400MB/s = 122ms.
// 4.  total mem access times is 382ms
// 5.  so out of the 620ms or so, for intersecting 'the .. sex' 382ms is 
//     just bare memory bottleneck!
// NOTE: i used membustest.cpp to measure L1 cache speed, main mem speed, etc.
//       on lenny

#define MAX_DOCS_PER_DOMAIN_HASH 100
#define MAX_DOCS_PER_SCORE_PER_DOMAIN_HASH 30
#define DEFAULT_AFFINITY    .10
//#define DEFAULT_CORRELATION .10

#define AFF_SAMPLE_SIZE  20.0
// this was 50.0, try 200.0 cuz 'new mexico tourism' had too high
// of a correlation weight because of all the implicit matches
#define CORR_SAMPLE_SIZE 200.0

// event ids range from 1 to 255, so that requires 256 bits which
// is 16 bytes!!
#define MAX_EVENT_ID_BYTES 16

IndexTable2::IndexTable2() { 
	// top docid info
	m_q             = NULL;
	m_buf           = NULL;
	//m_bigBuf        = NULL;
	m_localBuf      = NULL;
	//m_bitScoresBuf  = NULL;
	m_r             = NULL;
	m_useYesDocIdTable = false;
	m_useNoDocIdTable  = false;
	m_useBoolTable     = false;
	reset();
}

IndexTable2::~IndexTable2() { reset(); }

void IndexTable2::reset() {
	// has init() been called?
	m_initialized          = false;
	m_estimatedTotalHits   = -1;
	m_computedAffWeights   = false;
	// filterTopDocIds is now in msg3a, and so we need to init some stuff
	// or else it cores when addlists2_r is not executed.
	m_numTopDocIds            = 0;
	// Msg3a uses the exact explicit matches count to stats, that's it
	m_numExactExplicitMatches = 0;
	m_numExactImplicitMatches = 0;
	// if all of our termlists are BELOW the required read size, then
	// we call this an "undersized" query, and we tell that to Msg3a
	// so it will not re-call us or go to the next tier with us, because
	// we simply have no more termlist data on disk to work with.
	m_isDiskExhausted         = false;
	m_ni                      = -1;
	m_tmpDocIdPtrs2           = NULL;
	m_tmpScoresVec2           = NULL;
	m_tmpEbitVec2             = NULL;
	m_tmpHardCounts2          = NULL;
	m_useYesDocIdTable        = false;
	m_useNoDocIdTable         = false;
	m_errno                   = 0;
	m_imapIsValid             = false;
	m_dt.reset();
	m_et.reset();
	freeMem();
	// assume no-op
	m_t1 = 0LL;
}

// realloc to save mem if we're rat
void IndexTable2::freeMem ( ) {
	/*
	if ( m_bigBuf ) {
		mfree ( m_bigBuf , m_bigBufSize , "IndexTable22" );
		m_bigBuf = NULL;
	}
	*/
	if ( m_buf ) mfree ( m_buf , m_bufSize , "IndexTable2" );
	m_buf           = NULL;
	m_localBuf      = NULL;
	//m_bitScoresBuf  = NULL;
	m_tmpDocIdPtrs2 = NULL;
	m_tmpScoresVec2 = NULL;
	m_tmpDateVec2   = NULL;
	m_tmpEbitVec2   = NULL;
	m_tmpHardCounts2= NULL;
	m_maxTmpDocIds2 = 0;
	m_numTmpDocIds2 = 0;
	m_dt.reset();
	m_et.reset();
}

// . max score weight 
// . we base score weight on the termFreq(# of docs that have that term)
// . drop it down from 1000 to 100 because we logged "got score breach"
#define MAX_SCORE_WEIGHT 100

#define MIN_DOCIDS 100

// . returns false on error and sets g_errno
// . NOTE: termFreqs is just referenced by us, not copied
// . sets m_startKeys, m_endKeys and m_minNumRecs for each termId
// . TODO: ensure that m_termFreqs[] are all UPPER BOUNDS on the actual #!!
//         we should be able to get an upper bound estimate from the b-tree
//         quickly using Msg36!
// . we now support multiple plus signs before the query term
// . lists[] and termFreqs[] must be 1-1 with q->m_qterms[]
void IndexTable2::init ( Query     *q               , 
			 bool       isDebug         , 
			 void      *logstate        ,
			 bool       requireAllTerms , 
			 TopTree   *topTree         ,
			 char      *coll            , 
			 IndexList *lists           ,
			 int32_t       numLists        ,
			 HashTableX *sortByDateTablePtr ,
			 int32_t       docsWanted      ,
			 // termFreqs are in QUERY SPACE not IMAP SPACE
			 int64_t   *termFreqs       ,
			 bool       useDateLists    ,
			 bool       sortByDate      ,
			 char       sortBy          , // for events
			 bool       showExpiredEvents ,
			 float      userLat        ,
			 float      userLon        ,
			 bool       doInnerLoopSiteClustering ,
			 bool       doSiteClustering          ,
			 bool       getWeights                ,
			 Msg39Request *r                      ) {
	// sanity check -- watch out for double calls
	if ( m_initialized ) { char *xx= NULL; *xx =0; }
	// clear everything
	reset();
	// we are now
	m_initialized = true;
	// set debug flag
	m_isDebug = isDebug;
	// this mean to do it too!
	if ( g_conf.m_logDebugQuery ) m_isDebug = true;
	// point to them
	m_termFreqs    = termFreqs;
	// some other stuff
	m_useDateLists = useDateLists;
	m_sortByDate   = sortByDate;
	m_sortBy       = sortBy;
	m_showExpiredEvents  = showExpiredEvents;
	m_sortByDateTablePtr = sortByDateTablePtr;
	m_userLatIntComp = 0;
	m_userLonIntComp = 0;
	// sanity check - make sure it is NOT normalized (0 to 360)
	if ( userLat != 999.0 && userLat < -180.0 ) { char *xx=NULL;*xx=0; }
	if ( userLat != 999.0 && userLat >  180.0 ) { char *xx=NULL;*xx=0; }
	// now normalize here
	userLat += 180.0;
	userLon += 180.0;
	// . just like we normalized that lat/lon in Events::hash() we must
	//   do so with the user's location for sorting by distance
	// . also normalize the same way we do in Msg39::getList() now
	if ( userLat >= 0.0 && userLat <= 360.0 &&
	     userLon >= 0.0 && userLon <= 360.0 ) {
		m_userLatIntComp = (uint32_t) (userLat * 10000000.0);
		m_userLonIntComp = (uint32_t) (userLon * 10000000.0);
		// complement
		m_userLatIntComp = ~m_userLatIntComp;
		m_userLonIntComp = ~m_userLonIntComp;
	}
	// disable sort by distance if invalid m_userLat or m_userLon
	else if ( m_sortBy == SORTBY_DIST ) {
		m_sortBy = 0; // Dist = false;
	}
	// fix
	if ( m_sortByDate && ! m_useDateLists ) m_sortByDate= false;
	// we should save the lists!
	m_lists    = lists;
	m_numLists = q->m_numTerms;
	m_doSiteClustering          = doSiteClustering;
	m_getWeights                = getWeights;
	m_searchingEvents           = r->m_searchingEvents;
	m_showInProgress            = r->m_showInProgress;
	// save the request
	m_r = r;

	// save this
	m_coll = coll;
	// get the rec for it
        CollectionRec *cr = g_collectiondb.getRec ( m_coll );
        if ( ! cr ) { char *xx=NULL;*xx=0; }
	// set this now
	m_collnum = cr->m_collnum;


	// we always use it now
	if ( ! topTree ) {char *xx=NULL;*xx=0;}

	// how many docids to get, treat this as a minimum # of docids to get
	m_docsWanted = docsWanted;
	// . getting 100 takes same time as getting 1
	// . TODO: take docids form the same 8-bit sitehash into consideration
	//         and return a variety of sites even if it means exceeding
	//         m_docsWanted. it will save time!
	if ( m_docsWanted < MIN_DOCIDS ) m_docsWanted = MIN_DOCIDS;
	// you can only get up to the top MAX_RESULTS results unless you
	// use the RdbTree, m_topTree, to hold the top results, sorted by score
	//if ( ! topTree && m_docsWanted > MAX_RESULTS ) {char*xx=NULL; *xx=0;}

	// . get # of docs in collection
	// . we use this to help modify the affinities to compensate for very 
	//   popular words so 'new order' gets a decent affinity
	//int64_t cnt1 = 0;
	//int64_t cnt2 = 0;
	//RdbBase *base1 = getRdbBase ( RDB_CHECKSUMDB , coll );
	//RdbBase *base2 = getRdbBase ( RDB_CLUSTERDB  , coll );
	//if ( base1 ) cnt1 = base1->getNumGlobalRecs();
	//if ( base2 ) cnt2 = base2->getNumGlobalRecs();
	// get the max
	//if ( cnt1 > cnt2 ) m_numDocsInColl = cnt1;
	//else               m_numDocsInColl = cnt2;
	RdbBase *base = getRdbBase ( RDB_CLUSTERDB  , coll );	
	if ( base ) m_numDocsInColl = base->getNumGlobalRecs();
	// issue? set it to 1000 if so
	if ( m_numDocsInColl < 0 ) {
		log("query: Got num docs in coll of %"INT64" < 0",m_numDocsInColl);
		// avoid divide by zero below
		m_numDocsInColl = 1;
	}

	// . are we default AND? it is much faster.
	// . using ~/queries/queries.X.rex on rex:
	// . 81q/s raw=8&sc=0&dr=0&rat=1  sum = 288.0  avg = 0.3  sdev = 0.5
	// . 70q/s raw=8&sc=1&dr=1&rat=1  sum = 185.0  avg = 0.4  sdev = 0.5
	// . -O2                          sum = 204.00 avg = 0.15 sdev = 0.26
	// . 45q/s raw=8&sc=0&dr=0&rat=0  sum = 479.0  avg = 1.1  sdev = 1.1
	// . 38q/s raw=8&sc=1&dr=1&rat=0  sum = 429.0  avg = 1.2  sdev = 1.2
	// . -O2   raw=8&sc=0&dr=0&rat=0  sum = 351.00 avg = 0.60 sdev = 0.69
	// . speed up mostly from not having to look up as many title recs?
	// . the Msg39 reported time to intersect is 4 times higher for rat=0
	// . grep "intersected lists took" rat1 | awk '{print $8}' | add
	// . do this on host0c for speed testing: 
	//   ?q=windows+2000+server+product+key&usecache=0&debug=1&rat=1
	m_requireAllTerms = requireAllTerms;

	// force this off if boolean though!
	if ( q->m_isBoolean ) m_requireAllTerms = false;

	// and can not have more than 16 explicit bits!


	// save it
	m_topTree = topTree;
	// a ptr for debugging i guess
	g_topTree = topTree;
	// remember the query class, it has all the info about the termIds
	m_q = q;
	// just a int16_t cut
	m_componentCodes = m_q->m_componentCodes;
	// for debug msgs
	m_logstate = (int32_t)logstate;

	// calc the size of each termlist, 1-1 with m_q->m_qterms[]
	int64_t max = 0;
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		m_sizes[i] = 0;
		// . component lists are merged into one compound list
		// . they are basically ignored everywhere in IndexTable2.cpp
		if ( m_componentCodes[i] >= 0 ) continue;
		// PageParser.cpp gives us NULL lists, skip them
		if ( ! m_lists ) continue;
		m_sizes [i] += m_lists[i].getListSize();
		max += m_sizes[i];
	}

	// if setting up to compute final docids from cached winners,
	// bail now, we will get the imap from the cached rec
	if ( m_numLists <= 0 ) return;

	// . imap is int16_t for "intersection map"
	// . it tells us what termlists/lists to intersect FIRST
	// . it puts smallest lists first to optimize intersection times
	// . it leaves out component lists, and other "ignored" lists so
	//   m_ni may be less than m_q->m_numTerms
	// . that is why it needs m_sizes[], the sizes of the termlists
	// . see Query.cpp::getImap() for more info
	// . set imap here now so we can get the smallest block size to
	//   set m_maxNumTopDocIds2, the maximum number of docids we will
	//   have can not be larger than the smallest required termlist!
	// . we need this for calling setFreqWeights()
	m_nb = 0;
	m_ni = m_q->getImap ( m_sizes , m_imap , m_blocksize , &m_nb );
	// mark it as valid
	m_imapIsValid = true;

	// fill in m_revImap[], etc.
	setStuffFromImap();

	// . if we are computing the phr and aff weights, no need to
	//   deal with the tf weights?
	// . msg3a first calls each split with getWeights set to true so it
	//   can get everyone's sample points for the aff and tf weights. then
	//   it average those together and re-sends itself exactly the same
	//   request to each split, but with m_geWeights set to FALSE... that
	//   is the "second call"
	// . NO, msg3a also likes to send the same weights to everyone,
	//   and even now the tf weights here will all be computed exactly
	//   the same on each split, in the future we may do "relative tfs"
	//   and each split may calculate its own relative tfs and weights,
	//   to send back to msg3a so it can avg them and recompute the top
	//   docids on each split by doing a 2nd call
	//if ( m_getWeights ) return;

	// compute the min freq weight of them all, in IMAP space
	/*
	float min = -1.0;
	for ( int32_t i = 0 ; i < m_ni ; i++ )
		if ( m_freqWeights[i] < min || min < 0.0 ) 
			min = m_freqWeights[i];
	// . divide by that to normalize them all
	// . now the freq weights range from 1.0 to infinite
	for ( int32_t i = 0 ; i < m_ni ; i++ )
		m_freqWeights[i] /= min;
	*/
}

void IndexTable2::setStuffFromImap ( ) {
	// sanity check
	if ( ! m_imapIsValid ) { char *xx=NULL;*xx=0; }
	// . make the reverse imap
	// . for mapping from query term space into imap space
	// . zero out, i is in normal query term space
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) 
		m_revImap[i] = -1;
	// set it, i is now in crazy imap space
	for ( int32_t i = 0 ; i < m_ni ; i++ ) 
		m_revImap[m_imap[i]] = i;

	// now put m_q->m_qterms[i].m_leftPhraseTermNum into imap space
	// using revImap[]
	for ( int32_t i = 0 ; i < m_ni ; i++ ) {
		// map "m" into query term num space from imap space
		int32_t m = m_imap[i];
		// get query term #i
		QueryTerm *qt = &m_q->m_qterms[m];
		// assume not there
		m_imapLeftPhraseTermNum [i] = -1;
		m_imapRightPhraseTermNum[i] = -1;
		// . these are intially in query term space so we must convert
		//   into imap space, using revImap[]
		// . -1 implies no valid phrase term
		if ( qt->m_leftPhraseTermNum >= 0 ) {
			// . map into imap space
			// . will be -1 if not in imap space at all
			int32_t ileft = m_revImap[qt->m_leftPhraseTermNum];
			// store in array in imap space
			m_imapLeftPhraseTermNum[i] = ileft;
		}
		if ( qt->m_rightPhraseTermNum >= 0 ) {
			// . map into imap space
			// . will be -1 if not in imap space at all
			int32_t iright = m_revImap[qt->m_rightPhraseTermNum];
			// store in array in imap space
			m_imapRightPhraseTermNum[i] = iright;
		}
		// now m_qterms[m_imap[left]] is the query term you want
	}
	// . set score weight for each term based on termFreqs
	// . termFreqs is just the max size of an IndexList
	// . set weights for phrases and singletons independently
	// . m_termFreqs[] is in IMAP space
	if ( m_getWeights ) {
		setFreqWeights ( m_q , true  );
		setFreqWeights ( m_q , false );
	}

	// do not recompute the affinity and freq weights, use what we
	// are provided
	if ( ! m_getWeights ) setAffWeights ( m_r );
}

class CacheRec {
public:
	uint64_t   m_queryId;
	uint32_t   m_queryHash;
	char       m_tier;
	char      *m_buf;
	int32_t       m_bufSize;
	int32_t       m_maxTmpDocIds2;
	int32_t       m_numTmpDocIds2;
	char      *m_bufMiddle;
	int32_t       m_ni;
	int32_t       m_numExactExplicitMatches;
	int32_t       m_numExactImplicitMatches;
	int64_t  m_estimatedTotalHits;
	time_t     m_timestamp; // local clock time
	time_t     m_nowUTCMod;
	int32_t       m_localBufSize     ;
	//int32_t       m_bitScoresBufSize ;
	char      *m_listBufs    [MAX_QUERY_TERMS];
	int32_t       m_listBufSizes[MAX_QUERY_TERMS];
	int32_t       m_numLists;
	bool       m_isDiskExhausted;
	int32_t       m_imap        [MAX_QUERY_TERMS];
	int32_t       m_blocksize   [MAX_QUERY_TERMS];
	int32_t       m_nb;
};

#define ICACHESIZE 25

static CacheRec s_recs[ICACHESIZE];
static int32_t     s_maxi = 0;


void IndexTable2::setAffWeights ( Msg39Request *r ) {
	// m_imap must be valid
	if ( ! m_imapIsValid ) { char *xx=NULL;*xx=0; }
	// . use the weights passed into us when calling computeWeightedScore()
	//   from inside the fillTopDocids() function.
	// . copy this over into IMAP space so we can use them
	// . TODO: make sure m_imap[] is valid!
	float    *affWeightsQS = (float *)r->ptr_affWeights;
	float    *tfWeightsQS  = (float *)r->ptr_tfWeights;
	for ( int32_t i = 0 ; i < m_ni ; i++ ) {
		m_freqWeights[i] = tfWeightsQS [m_imap[i]];
		m_affWeights [i] = affWeightsQS[m_imap[i]];
	}
	//gbmemcpy ( m_freqWeights , tfWeights  , nqt * sizeof(float) );
	//gbmemcpy ( m_affWeights  , affWeights , nqt * sizeof(float) );

	// do not compute them ourselves again
	m_computedAffWeights = true;
}

// . recompute the top docids from a cached intersection using the supplied
//   weight vectors, affWeights[] and tfWeights[]
// . returns true if we could get intersection from cache
bool IndexTable2::recompute ( Msg39Request *r ) {

	// assume no-op
	m_t1 = 0LL;
	// reset this
	m_errno = 0;

	uint64_t  queryId    = r->m_queryId;

	// hash the query
	uint32_t queryHash = hash32 ( r->ptr_query , r->size_query );

	// . do not recompute the affinity and freq weights, use what we
	//   are provided
	// . this is now set in setStuffFromImap()
	//setAffWeights ( r );

	char *data = NULL;
	// get from cache
	int32_t i = 0;
	for ( i = 0 ; i < s_maxi ; i++ ) {
		// grab that thang
		data = s_recs[i].m_buf;
		// skip if empty
		if ( ! data )  continue;
		// is it us?
		if ( s_recs[i].m_queryId != queryId ) continue;
		// just a secondary precaution, if a host is quickly restarted it could 
		// send us another query with the same queryId
		if ( s_recs[i].m_queryHash != queryHash ) continue;
		// we need to match the tier too!
		if ( s_recs[i].m_tier != r->m_tier ) continue;//{ char*xx=NULL;*xx=0; }
		// how many docids we got in the intersection
		break;
	}
	// return false if could not get it!
	if ( i >= s_maxi ) {
		// note it
		log (LOG_DEBUG,"query: MISSED THE CACHE qid=0x%"XINT64" tier=%"INT32"",
		     m_r->m_queryId,(int32_t)m_r->m_tier);
		return false;
	}

	// log it
	if ( m_isDebug )
		logf(LOG_DEBUG,"query: found in cache qid=0x%"XINT64" tier=%"INT32"",
		     r->m_queryId,(int32_t)r->m_tier);

	// the number of valid quer terms we are dealing with
	int32_t  nqt = s_recs[i].m_ni;
	// sanity check
	if ( nqt == -1 ) { char *xx=NULL;*xx=0; }
	// set it
	m_ni = nqt;

	m_isDiskExhausted = s_recs[i].m_isDiskExhausted;

	// need this for sortByTime
	m_nowUTCMod = s_recs[i].m_nowUTCMod;

	// set our stuff from it
	int32_t nd  = s_recs[i].m_maxTmpDocIds2;
	char *p  = s_recs[i].m_bufMiddle;
	m_tmpDocIdPtrs2  = (char    **)p ; p += nd * 4;
	m_tmpScoresVec2  = (uint8_t  *)p ; p += nd * nqt;
	m_tmpEbitVec2    = (qvec_t   *)p ; p += nd * sizeof(qvec_t);
	m_tmpHardCounts2 = (int16_t    *)p ; p += nd * 2;
	if ( m_sortByDate ) {
		m_tmpDateVec2 = (uint32_t *)p; p += nd * 4; }
	if ( m_sortBy == SORTBY_DIST ) {
		m_tmpLatVec2 = (uint32_t *)p; p += nd * 4; 
		m_tmpLonVec2 = (uint32_t *)p; p += nd * 4; 
	}
	//if ( m_sortBy == SORTBY_TIME ) {
	//	m_tmpTimeVec2 = (uint32_t *)p; p += nd * 4; 
	//	m_tmpEndVec2  = (uint32_t *)p; p += nd * 4; 
	//}
	if ( m_searchingEvents ) {
		m_tmpEventIds2 = (uint8_t *)p; p += nd; }

	// for debug
	//for ( int32_t i = 0 ; i < 38 ; i++ )
	//	logf(LOG_DEBUG,"poo2: tt=%"UINT32"",m_tmpTimeVec2[i]);

	// sizes here
	m_localBufSize     = s_recs[i].m_localBufSize;
	//m_bitScoresBufSize = s_recs[i].m_bitScoresBufSize;
	// set the local hash table buf (unused i think for us)
	m_localBuf = p; p += m_localBufSize;
	// the boolean bit scores table
	//m_bitScoresBuf = p ; p += m_bitScoresBufSize;

	// set the # we got too
	m_numTmpDocIds2  = s_recs[i].m_numTmpDocIds2;

	// inherit these cuz msg3a re-gets it from the msg39 reply
	m_numExactExplicitMatches = s_recs[i].m_numExactExplicitMatches;
	m_numExactImplicitMatches = s_recs[i].m_numExactImplicitMatches;

	m_estimatedTotalHits      = s_recs[i].m_estimatedTotalHits;

	// retrieve the imap
	m_nb = s_recs[i].m_nb;
	gbmemcpy ( m_blocksize , s_recs[i].m_blocksize , m_nb * 4 );
	gbmemcpy ( m_imap      , s_recs[i].m_imap      , m_ni * 4 );

	// fill in the related stuff
	setStuffFromImap();

	// fake it
	//m_computedAffWeights = true;
	//m_topTree            = topTree;
	//m_q                  = q;

	// for allocTopTree, need these
	//m_docsWanted       = docsWanted;
	//m_doSiteClustering = doSiteClustering;

	// how many results did we get?
	int32_t numTopDocIds = 0;

	// make m_topTree alloc the mem it needs
	//if ( ! allocTopTree() ) goto skipFill;

	// we must also re-compute the table that maps the explicit
	// bits of a docid into a bit score so m_q->getBitScore(ebits) works!
	if ( ! m_q->m_bmapIsSet ) m_q->setBitMap();

	// . set boolean bit scores necessary for resolving boolean queries
	// . maps an implicit bit vector to a "true" if it is a valid search
	//   results or "false" if it is not
	//if ( m_q->m_isBoolean ) {
		// this returns false if no truths are possible, in which
		// case we have no search results
		//if ( ! m_q->setBitScoresBoolean ( m_bitScoresBuf     ,
		//				  m_bitScoresBufSize ) )
		//	return false;
	//}

	// set these to bogus values to prevent core
	m_timeTermOff = 999;
	m_endTermOff  = 999;
	m_latTermOff  = 999;
	m_lonTermOff  = 999;

	// . now fill the top docids with the top of the big list
	// . this will fill m_topTree if it is non-NULL
	numTopDocIds = fillTopDocIds ( //NULL             , // topp
				       //NULL             , // tops
				       //NULL             , // topb
				       m_docsWanted     ,
				       m_tmpDocIdPtrs2  ,
				       m_tmpScoresVec2  ,
				       m_tmpDateVec2    ,
				       m_tmpLatVec2     ,
				       m_tmpLonVec2     ,
				       //m_tmpTimeVec2    ,
				       //m_tmpEndVec2     ,
				       m_tmpEbitVec2    ,
				       m_tmpHardCounts2 ,
				       m_tmpEventIds2   ,
				       m_numTmpDocIds2  );
	// sanity check
	if ( numTopDocIds != m_topTree->m_numUsedNodes ) {
		char *xx=NULL;*xx=0; }
	// skipFill:

	freeCacheRec ( i );
	// we got the top docids, return true
	return true;
}

void IndexTable2::freeCacheRec ( int32_t i ) {
	// remove from cache
	if ( m_isDebug )
		logf(LOG_DEBUG,"query: freeing cache rec #%"INT32" qid=0x%"XINT64" "
		     "tier=%"INT32"",i,s_recs[i].m_queryId,(int32_t)s_recs[i].m_tier);
	// was it already freed? this happens sometimes, dunno why exactly...
	if ( ! s_recs[i].m_buf ) {
		logf(LOG_DEBUG,"query: Caught double free on itcache");
		return;
	}
	mfree ( s_recs[i].m_buf , s_recs[i].m_bufSize , "ITCache2");
	// free the lists
	for ( int32_t j = 0 ; j < s_recs[i].m_numLists ; j++ )
		mfree ( s_recs[i].m_listBufs    [j] ,
			s_recs[i].m_listBufSizes[j] , "ITCache3" );
	// free the cache slot
	s_recs[i].m_buf = NULL;
	// do not free again!
	s_recs[i].m_queryId = 0;
	s_recs[i].m_queryHash = 0;
}

// . store the allocated mem in a little cache thingy
// . returns false on error and sets g_errno, true on success
bool IndexTable2::cacheIntersectionForRecompute ( Msg39Request *r ) {

	if ( ! m_topTree ) { char*xx=NULL;*xx=0;}

	// clear last one so we have on to store this in
	if ( s_maxi < ICACHESIZE ) s_recs[s_maxi].m_buf = NULL;
	// the max for i
	int32_t maxi = s_maxi + 1;
	// limit to 50
	if ( maxi > ICACHESIZE ) maxi = ICACHESIZE;

	uint32_t queryHash = hash32 ( r->ptr_query , r->size_query );

	int32_t nowLocal = getTimeLocal();

	// free our old intersection
	int32_t i;
	for ( i = 0 ; i < maxi ; i++ ) {
		// skip if can't free
		if ( ! s_recs[i].m_buf ) continue;
		// if we advance to the next tier, we will find the results
		// of the first tier in here!
		if ( s_recs[i].m_queryId == r->m_queryId &&
		     s_recs[i].m_queryHash == queryHash ) 
			freeCacheRec ( i );
		else if ( nowLocal - s_recs[i].m_timestamp > 120 ) {
			if ( m_isDebug )
				log("query: freeing aged CacheRec");
			freeCacheRec ( i );
		}
	}

	// find and empty slot to store it in
	for ( i = 0 ; i < maxi ; i++ )
		// stop if empty
		if ( ! s_recs[i].m_buf ) break;

	// return if no room in the cache
	if ( i >= maxi ) {
		logf(LOG_DEBUG,"query: could not add cache rec for "
		     "qid=0x%"XINT64" tier=%"INT32"",
		     m_r->m_queryId,(int32_t)m_r->m_tier);
		return false;
	}

	if ( m_isDebug )
		logf(LOG_DEBUG,"query: adding cache rec #%"INT32". "
		     "numTmpDocIds2=%"INT32" qid=0x%"XINT64" tier=%"INT32"",i,  
		     m_numTmpDocIds2,m_r->m_queryId,(int32_t)m_r->m_tier);

	// save m_buf/m_bufSize
	s_recs[i].m_buf           = m_buf;
	s_recs[i].m_bufSize       = m_bufSize;
	s_recs[i].m_bufMiddle     = m_bufMiddle;
	s_recs[i].m_maxTmpDocIds2 = m_maxTmpDocIds2;
	s_recs[i].m_numTmpDocIds2 = m_numTmpDocIds2;
	s_recs[i].m_timestamp     = nowLocal;
	s_recs[i].m_nowUTCMod     = m_nowUTCMod;
	// do not allow it to be freed now
	m_buf = NULL;

	// for debug
	//for ( int32_t i = 0 ; i < 38 ; i++ )
	//	logf(LOG_DEBUG,"poo: tt=%"UINT32"",m_tmpTimeVec2[i]);

	// # docids we got buf for
	//int32_t nd  = m_maxTmpDocIds2;

	// the last two bufs
	s_recs[i].m_localBufSize     = m_localBufSize;
	//s_recs[i].m_bitScoresBufSize = m_bitScoresBufSize;

	// alloc for it
	//char *data = (char *)mmalloc ( keepSize , "IT2Cache" );
	//if ( ! data ) return false;

	// hopefully this is super fast
	//gbmemcpy ( data , keepStart , keepSize );

	// save the termlists since m_tmpDocIdPtrs2[] references into them
	for ( int32_t j = 0 ; j < m_numLists ; j++ ) {
		s_recs[i].m_listBufs    [j] = m_lists[j].m_alloc;
		s_recs[i].m_listBufSizes[j] = m_lists[j].m_allocSize;
		// tell list not to free its stuff
		m_lists[j].m_ownData = false;
	}
	s_recs[i].m_numLists = m_numLists;

	// miscellaneous
	s_recs[i].m_queryId   = r->m_queryId;
	s_recs[i].m_queryHash = hash32 ( r->ptr_query , r->size_query );
	s_recs[i].m_tier      = r->m_tier;
	s_recs[i].m_ni        = m_ni;

	s_recs[i].m_isDiskExhausted = m_isDiskExhausted;

	// save these since Msg3a needs it for determining if it needs to
	// go to the next tier
	s_recs[i].m_numExactExplicitMatches = m_numExactExplicitMatches;
	s_recs[i].m_numExactImplicitMatches = m_numExactImplicitMatches;

	// and save the estimate
	s_recs[i].m_estimatedTotalHits = m_estimatedTotalHits;

	// sanity check
	if ( ! m_imapIsValid ) { char *xx=NULL;*xx=0;}
	// store the imap in case it changes
	s_recs[i].m_nb = m_nb;
	gbmemcpy ( s_recs[i].m_blocksize , m_blocksize , m_nb * 4 );
	gbmemcpy ( s_recs[i].m_imap      , m_imap      , m_ni * 4 );
	//m_ni = m_q->getImap ( m_sizes , m_imap , m_blocksize , &m_nb );


	if ( s_maxi < i+1 ) s_maxi = i + 1;

	return true;
}


// these should be in IMAP space!
void IndexTable2::setFreqWeights ( Query *q , bool phrases ) {
	if ( ! m_imapIsValid ) { char *xx=NULL;*xx=0; }
	// get the minimum TF, "minFreq"
	double minFreq = 0x7fffffffffffffffLL;
	for ( int32_t i = 0 ; i < q->getNumTerms() ; i++ ) {
		// component lists are merged into one compound list
		if ( m_componentCodes[i] >= 0 ) continue;
		// . if we're setting phrases, it must be an UNQUOTED phrase
		// . quoted phrases have a term sign!
		if ( phrases ) {
			if (   q->isInQuotes (i)         ) continue;
			if ( ! q->isPhrase   (i)         ) continue;
			if (   q->getTermSign(i) != '\0' ) continue;
		}
		else if ( ! q->isInQuotes (i)         &&
			    q->isPhrase   (i)         &&
			    q->getTermSign(i) == '\0'  )
				continue;
		// is this the new min?
		if ( m_termFreqs[i] < minFreq ) minFreq = m_termFreqs[i];
	}
	// to balance things out don't allow minFreq below "absMin"
	double absMin  = GB_INDEXDB_PAGE_SIZE/(2*sizeof(key_t));
	if ( minFreq < absMin ) minFreq = absMin;

	float wmax = 0.0;
	// . loop through each term computing the score weight for it
	// . j is in imap space
	for ( int32_t j = 0 ; j < m_ni ; j++ ) {
		// reset frequency weight to default
		float fw = 1.0;
		// . get query term num, q->m_qterms[i]
		// . i is in query term space
		int32_t i = m_imap[j];
		// sanity checks
		if ( i < 0              ) { char *xx = NULL; *xx = 0; }
		if ( i >= q->m_numTerms ) { char *xx = NULL; *xx = 0; }
		// component lists are merged into one compound list
		if ( m_componentCodes[i] >= 0 ) continue;
		// is it a "weak" phrase term?
		bool isWeakPhrase = true;
		if ( ! q->isPhrase   (i)         ) isWeakPhrase = false;
		if (   q->isInQuotes (i)         ) isWeakPhrase = false;
		if (   q->getTermSign(i) != '\0' ) isWeakPhrase = false;
		// if we're setting phrases, it must be a weak phrase
		if (   phrases && ! isWeakPhrase ) continue;
		if ( ! phrases &&   isWeakPhrase ) continue;
		// increase small term freqs to the minimum
		double freq = m_termFreqs[i];
		if ( freq < absMin ) freq = absMin;
		// get ratio into [1,inf)
		double ratio1 = 2.71828 * freq / minFreq; // R1
		// . natural log it
		// . gives a x8 whereas log10 would give a x4 for a wide case
		double ratio2 = log ( ratio1 ); // R2
		// square
		// ratio = ratio * ratio;
		// make bigger now for '"warriors of freedom" game' query
		// so it weights 'game' less
		//double ratio3 = pow ( ratio2 , 2.6 ); // R3
		// raise to the m_queryExp power
		double ratio3 = pow ( ratio2, (double)g_conf.m_queryExp );// R3
		// now invert
		double ratio4 = 1.0 / ratio3; // R4
// Example for 'boots in the uk' query: (GB_PAGE_SIZE is 32k, absMin=1365)
// TERM                df        R1       R2      R3     R4      W1
// boots(184)      -->  7500000  7465     8.9179  295.58 .00338  1.1255
// uk   (207)      --> 78000000  77636    11.2597 541.97 .001845 .6143
// "boots.. uk"(25)-->     2731  2.71828  1.0     1.0    1.0     333
		// don't exceed multiplier
		//if ( ratio < 1.0 / g_conf.m_queryMaxMultiplier )
		//	ratio = 1.0 / g_conf.m_queryMaxMultiplier;
		// get the pure weight
		int32_t weight= (int32_t)(((double)MAX_SCORE_WEIGHT) * ratio4);

		// HACK: just make all phrases have a 1.0 weight here, 
		// because "mexico tourism" in "new mexico tourism" is a
		// somewhat rare thing compare to "new mexico" BUT it should
		// not be boosted because of that!!
		if ( phrases ) weight = MAX_SCORE_WEIGHT;

		// ensure at least 1
		if ( weight < 1 ) weight = 1;
		// don't breech MAX_SCORE
		if ( weight > MAX_SCORE_WEIGHT ) weight = MAX_SCORE_WEIGHT;
		// store it for use by addLists_r
		fw = (float)weight;
		// . apply user-defined weights
		// . we add this with completed disregard with date weighting
		QueryTerm *qt = &q->m_qterms[i];
		// . if this is a phrase then give it a boost
		// . NOTE: this might exceed MAX_SCORE_WEIGHT then!!
		if ( qt->m_isPhrase )
			fw = ( fw * g_conf.m_queryPhraseWeight ) / 100.0 ;
		//the user weight, "w"
		float w;
		if ( qt->m_userType == 'r' ) w = fw;
		else                         w = 1LL;
		// 'r' means relative, so multiply this by "fw", otherwise
		// the provided weight is absolute...
		w *= (float)qt->m_userWeight;
		// . it can be multiplied by up to 256 (the term count)
		// . then it can be multiplied by an affinity weight, but
		//   that ranges from 0.0 to 1.0.
		int64_t max = 0x7fffffff / 256;
		if ( w > max ) {
			log("query: Weight breech. Truncating to %"UINT64".",max);
			w = (float)max;
		}

		// if this is a special term like lat/lon/time/end then we
		// do not want it to contribute to the score of the result
		// so set this to zero. thus getWeightedScore() should ignore
		// it completely.
		if ( qt->m_fieldCode == FIELD_GBLATRANGE ||
		     qt->m_fieldCode == FIELD_GBLONRANGE ||
		     qt->m_fieldCode == FIELD_GBLATRANGECITY ||
		     qt->m_fieldCode == FIELD_GBLONRANGECITY )
		     //qt->m_fieldCode == FIELD_GBSTARTRANGE ||
		     //qt->m_fieldCode == FIELD_GBENDRANGE )
			w = 0.0;

		// . let's put this into IMAP space
		// . so it will be 1-1 with m_affWeights!
		m_freqWeights[j] = w;
		// get max
		if ( w > wmax ) wmax = w;

		// . stop words always get a weight of 1 regardless
		// . we don't usually look them up unless all words in the
		//   query are stop words
		//if ( q->m_isStopWords[i] ) weight = 1;

		// log it
		if ( m_isDebug || g_conf.m_logDebugQuery ) {
			// get the term in utf8
			//char bb[256];
			//utf16ToUtf8(bb, 256, qt->m_term, qt->m_termLen);
			char *tpc = qt->m_term + qt->m_termLen;
			char c = *tpc;
			*tpc = '\0';
			char sign = qt->m_termSign;
			if ( sign == 0 ) sign = '0';
			logf(LOG_DEBUG,"query: [%"UINT32"] term=\"%s\" "
			     "qnum=%"INT32" has freq=%"INT64" "
			     "r1=%.3f r2=%.3f r3=%.3f r4=%.3f score "
			     "freqWeight=%.3f termId=%"INT64" "
			     "sign=%c field=%"INT32"", 
			     m_logstate,qt->m_term,i,m_termFreqs[i],
			     ratio1,ratio2,ratio3,ratio4,w,
			     qt->m_termId,sign,(int32_t)qt->m_fieldCode);
			// put it back
			*tpc = c;
		}
	}

	// normalize so max is 1.0
	for ( int32_t j = 0 ; j < m_ni ; j++ ) {
		// skip if zero because we should ignore it
		if ( m_freqWeights[j] == 0.0 ) continue;
		// normalize it
		m_freqWeights[j] /= wmax;
		// cap it to floor
		if ( m_freqWeights[j] < 0.0001 )
			m_freqWeights[j] = 0.0001;
	}

	// set weights in query space too!
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ )
		m_freqWeightsQS [i] = -1.0;
	// store the weights in query space format for passing back in the
	// Msg39Reply for processing
	for ( int32_t i = 0 ; i < m_ni ; i++ )
		m_freqWeightsQS [m_imap[i]] = m_freqWeights [i];


	/*
	// we compute word scores after phrases, wait for that
	if ( phrases ) return;

	if ( ! g_conf.m_useDynamicPhraseWeighting ) return;

	// . reduce weights of single word terms if their term freq is close
	//   to that of one of the phrase terms they are in
	// . this is really only necessary because we weight words low if they
	//   are in a repeated phrase on a document. century21.com has really
	//   low scores for real and estate, but not for 'real estate' and
	//   is not coming up in the top 50!
	// . coldwellbanker has a high score for 'real' because it contains
	//   it in its title, so it gets into the concept table as a single
	//   word, then a neighborhood pwid contains it NOT in the phrase 
	//   "real estate"...
	for ( int32_t i = 0 ; i < q->m_numWords ; i++ ) {

		// get the query word
		QueryWord *qw = &q->m_qwords[i];
		// how can this be null?
		if ( ! qw ) continue;

		// skip if no associated QueryTerm class. punct word?
		if ( ! qw->m_queryWordTerm ) continue;
		// get the term # for word #i (query word term num)
		int32_t qwtn = qw->m_queryWordTerm - &q->m_qterms[0];
		// get the freq of that
		int64_t freq = q->m_termFreqs[qwtn];

		// component lists are merged into one compound list
		if ( m_componentCodes[qwtn] >= 0 ) continue;

		// get the query word # that starts the phrase on our left
		int32_t wordnum1 = qw->m_leftPhraseStart;
		// get that word into "qw1"
		QueryWord *qw1 = NULL;
		if ( wordnum1 >= 0 ) qw1 = &q->m_qwords[wordnum1];
		// get the QueryTerm of the PHRASE that word starts
		QueryTerm *qt1 = NULL;
		if ( qw1 ) qt1 = qw1->m_queryPhraseTerm;
		// get term # of that phrase
		int32_t term1 = -1;
		if ( qt1 ) term1 = qt1 - &q->m_qterms[0];
		// sanity check
		if ( term1 >= q->m_numTerms ) { char *xx = NULL; *xx = 0; };
		// get the term freq of the left phrase
		int64_t leftFreq  = -1LL;
		if ( term1 >= 0 ) leftFreq = q->m_termFreqs [ term1 ];

		// get the phrase term this query word starts
		QueryTerm *qt2 = qw->m_queryPhraseTerm;
		// get term # of that phrase
		int32_t term2 = -1;
		if ( qt2 ) term2 = qt2 - &q->m_qterms[0];
		// sanity check
		if ( term2 >= q->m_numTerms ) { char *xx = NULL; *xx = 0; };
		// get the term freq of the left phrase
		int64_t rightFreq  = -1LL;
		if ( term2 >= 0 ) rightFreq = q->m_termFreqs [ term2 ];

		// . take the min phrase freq
		// . these are -1 if not set
		int64_t min = leftFreq;
		if ( min < 0 ) min = rightFreq;
		if ( rightFreq < min && rightFreq >= 0 ) min = rightFreq;

		// skip if no phrases at all for query term #i
		if ( min == -1LL ) continue;

		// . what percentage is the phrase freq of the word?
		// . if the phrase's term freq equals that of the word's then
		//   it is safe to demote the word weight to very little,
		//   just above 0...
		double scalar = 1.0 - ( (double)min / (double)freq );

		// sanity check really
		if ( scalar < 0.0 ) scalar = 0.0;

		float newWeight = m_freqWeights[qwtn] * scalar;

		// debug msg
		logf(LOG_DEBUG,"query: reduce query word score #%"INT32" from %f "
		     "to %f",  i,m_freqWeights[qwtn],newWeight);

		// reduce the word's term score
		m_freqWeights[qwtn] = newWeight;
	}
	*/
}

// max temp buffer slots for rat=0 (NORAT_
//#define NORAT_TMP_SLOTS 655350

// . alloc all the mem we need to do the intersection
// . do allocs here since intersection is usually in a thread and we can't
//   call malloc from the thread (not thread safe)
// . the "need" buffer holds space that is basically pointers to lists
//   for all the termlists involved in the intersection. 
// . each list has an array of ptrs into its termlist. each ptr points to a 
//   sublist which is sorted by docid and that makes it convenient to
//   intersect one range of docids at a time, since the hash table is small
// . the "need2" buffer is pointed to by m_tmpDocIdPtrs2[] etc. that buffer
//   is used to hold the docids that are in the "active intersection", which
//   is carried over from one intersection operation to the next with every
//   call the addList2_r(). It shrinks with each interscetino operation.
// . the "need2" buffer is also used to hold the top NUMRAT_SLOTS docids
//   in the case of a rat=0 intersection which intersects all termlists in
//   a single call the addLists2_r() until enough docids are obtained to
//   calculcate the affinities and affinity weights so that we can calcualte
//   the proper scores of each docid and therefore only keep the top X scoring
//   docids.
// . the "need3" buffer is m_localBuf
bool IndexTable2::alloc ( ) {
	// no need to allocate if we have nothing to intersect!
	if ( m_nb <= 0 ) return true;

	// sanity check -- must have called m_ni = getImap() in init() above
	if ( m_ni <  0 ) { char *xx = NULL; *xx = 0; }

	// . alloc space for top tree
	// . returns false and sets g_errno on error
	// . increase docs to get if doing site clustering
	// . this is no longer done in Msg40.cpp for FULL SPLITS, we do it here
	// . the top tree is only needed if m_recopute is true now!!
	//if ( ! allocTopTree() ) return false;

	// pre-allocate all the space we need for intersecting the lists
	int64_t need = 0;

	// we only look at the lists in imap space, there are m_ni of them
	int32_t nqt = m_ni;
	need += nqt * 256 * sizeof(char *) ; // ptrs
	need += nqt * 256 * sizeof(char *) ; // pstarts
	if ( m_useDateLists ) 
	need += nqt * 256 * sizeof(char *) ; // ptrEnds
	need += nqt * 256 * sizeof(char *) ; // oldptrs
	// one table for each list to map 1-byte score to tf*affWeight score
	need += nqt * 256 * sizeof(int32_t  ) ;
	// and one table for just mapping to affWeighted score
	need += nqt * 256 * sizeof(int32_t  ) ;
	need += nqt       * sizeof(char  ) ; // listSigns
	need += nqt       * sizeof(char  ) ; // listHardCount
	need += nqt       * sizeof(int32_t *) ; // listScoreTablePtrs1
	need += nqt       * sizeof(int32_t *) ; // listScoreTablePtrs2
	need += nqt       * sizeof(int32_t  ) ; // listIndexes
	need += nqt       * sizeof(char *) ; // listEnds
	need += nqt       * sizeof(char  ) ; // listHash
	need += nqt       * sizeof(qvec_t) ; // listExplicitBits
	need += nqt       * sizeof(int32_t  ) ; // listPoints

	// mark spot for m_tmpDocIdPtrs2 arrays
	int32_t middle = need;

	/*
	// if sorting by date we need a much larger hash table than normal
	if ( sortByDate ) {
		// we need a "ptr end" (ptrEnds[i]) for each ptr since the 
		// buckets are not delineated by a date or score
		need += nqt * 256 * sizeof(char *) ;
		// get number of docids in largest list
		int32_t max = 0;
		for ( int32_t j = 0 ; j < numLists ; j++ ) {
			// datedb lists are 10 bytes per half key
			int32_t nd = lists[j].getListSize() / 10;
			if ( nd > max ) max = nd;
		}
		// how big to make hash table?
		int32_t slotSize  = 4+4+2+sizeof(qvec_t);
		int64_t need = slotSize * max;
	        // have some extra slots in between for speed
		need = (need * 5 ) / 4;
		// . do not go overboard
		// . let's try to keep it in the L2 cache
		// . we can only do like 1 million random access to main
		//   memory per second. L2 mem should be much higher.
		//if ( need > 30*1024*1024 ) need = 30*1024*1024;
		//if ( need > 512*1024 ) need = 512*1024;
		//if ( need > 1024*1024 ) need = 1024*1024;
		// . this is balance between two latency functions. with more
		//   "need" we can have a bigger hash table which decrease
		//   m_numLoops, but increases our memory access latency since
		//   we'll be hitting more of main memory. use hashtest.cpp
		//   with a high # of slots to see the affects of increasing
		//   the size of the hash table. every loop in m_numLoops
		//   means we read the entire datedb lists and we can only do
		//   that at most at 2.5GB/sec or so on the current hardware.
		// . i don't expect memory access times to improve as fast as
		//   memory throughput, so in the future to maintain optimal
		//   behaviour, we should decrease the max for "need"???
		//if ( need > 4*1024*1024 ) need = 4*1024*1024;
		if ( need > 2*1024*1024   ) need = 2*1024*1024;
		//if ( need > 512*1024   ) need = 512*1024;
		// we need to have AT LEAST 1024 slots in our hash table
		if ( need < slotSize*1024 ) need = slotSize*1024;
		// clear this out in case it was set
		if ( m_bigBuf ) {
			mfree ( m_bigBuf , m_bigBufSize , "IndexTable22" );
			m_bigBuf = NULL;
		}
	tryagain:
		// alloc again
		m_bigBuf = (char *)mmalloc ( need , "IndexTable22" );
		if ( ! m_bigBuf && need > 1024*1024 ) {
			need >>= 1; goto tryagain; }
		// set it
		m_bigBufSize = need;
		if ( ! m_bigBuf ) {
			log("query: Could not allocate %"INT64" for query "
			    "resolution.",need);
			return false;
		}
	}
	*/

	// sanity check
	if ( m_tmpDocIdPtrs2 ) { 
		log(LOG_LOGIC,"query: bad top docid ptrs2."); 
		char *xx = NULL; *xx = 0; }

	// hks = half key size (size of everything except the termid)
	char hks  = 6; 
	// fks = full key size
	char fks  = 12;
	// dateLists are 4 more bytes per key than standard 12-byte key lists
	// because they have a 4-byte date/time stamp in addition to 
	// everything else
	if ( m_useDateLists ) { hks += 4; fks += 4; }

	// . we expect up to "min" docids to be stored in memory at a time
	// . for rat=1 these docids are the docids (and related info) 
	//   contained in the "active intersection" (m_tmpDocIdsPtrs2[],etc.)
	// . the "active intersection" is the result of the last intersection
	//   we performed. 
	// . we intersect each termlist with the "active intersection" by
	//   calling addLists2_r()
	int64_t min = 0;
	if ( m_requireAllTerms && m_nb > 0 ) {
		for ( int32_t i = 0 ; i < m_blocksize[0]; i++ )
			min += m_lists[m_imap[i]].getListSize() / hks ;
	}
	// . for rat=0, we now use these buffers to hold the last 
	//   NORAT_TMP_SLOTS docids.
	// . for rat=0, we intersect all termlists in one call to addLists2_r()
	//   so there is no "active intersection" that needs to be preserved
	//   between multiple calls to addLists2_r(), however, it is not
	//   as fast.
	// . but now we do need to store a lot of docids (NORAT_TMP_SLOTS)
	//   in order to do our phrase and synonym affinity calculations
	// . one we determine those affinities we can then determine scores
	//   for docids as we get them, and we no longer need to store all
	//   of the result docids into m_tmpDocIdPtrs2[], HOWEVER, we do need
	//   to store the top-scoring docids (usually 100 or so) in the
	//   m_topDocIds[] array, which is easy and efficient.
	else {
		// for rat = 0 we'll have a limited sized buffer
		//min = NORAT_TMP_SLOTS;
		// MDW: no, lets cache the whole lot of 'em now since we
		// broke up the msg3a thing into two phases, weight generation
		// and converting the score vectors into the top X docids
		for ( int32_t i = 0 ; i < m_numLists; i++ )
			// TODO: ignore the ignored lists!!
			min += m_lists[i].getListSize() / hks ;
		
	}

	// crap when searching events we have multiple event ids that share
	// one datedb key, so we have to count each on separately! that is
	// we have a range of one byte eventids, a to b in the datedb key
	// as a form of compression, since frequently such events share
	// many terms in common. we might have a boolean query like
	// 'health OR +sports' in which case we should probably add up
	// every event ids in every key of every list anyhow...
	//if ( m_requireAllTerms && m_nb > 0 ) {
	if ( m_nb > 0 && m_searchingEvents ) {
		// . reset this since we are counting eventids now
		// . no we need this in case fieldCode == FIELD_GBLATRANGE etc
		//   and num == 1, so do not nuke "min"!!
		//min = 0;
		// how many lists should we scan for counting eventids
		int32_t num ;
		// just count event ids in the first termlist, the int16_test
		// termlist (and its associated phrase terms), if 
		// requireAllTerms is true.
		if ( m_requireAllTerms ) num = m_blocksize[0];
		// otherwise, we are doing a boolean OR or rat=0 and
		// we need to count all eventids in all lists in case they
		// all make it into the final search results.
		// don't use numLists because it is not deduped for same
		// query terms like m_ni???
		else num = m_ni;//numLists;
		// then scan just those termlists
		for ( int32_t i = 0 ; i < num ; i++ ) {
			// point to that list
			RdbList *clist = &m_lists[m_imap[i]];
			// get query term
			QueryTerm *qt = &m_q->m_qterms[m_imap[i]];
			// skip if a lat/lon/time key list, we only
			// have one of these keys per event id. we already
			// added one for each docid in the loops above
			// that were for non-event searching, so if we only
			// have on termlist and its a lat/lon we should be
			// covered from those loops, so skip it here!!
			if ( qt->m_fieldCode == FIELD_GBLATRANGE ||
			     qt->m_fieldCode == FIELD_GBLONRANGE ||
			     qt->m_fieldCode == FIELD_GBLATRANGECITY ||
			     qt->m_fieldCode == FIELD_GBLONRANGECITY )
			     //qt->m_fieldCode == FIELD_GBSTARTRANGE ||
			     //qt->m_fieldCode == FIELD_GBENDRANGE )
				continue;
			// start at first key
			clist->resetListPtr();
			// scan that list
			for(;!clist->isExhausted();clist->skipCurrentRecord()){
				int32_t a = ((uint8_t *)clist->m_listPtr)[7];
				int32_t b = ((uint8_t *)clist->m_listPtr)[6];
				if ( a - b < 0 ) { 
					// corruption?
					min += b-a;
					log("index: strangr corruption");
					continue;
					//char *xx=NULL;*xx=0;
				}
				// . count eventid range 8-8 as one...
				// . we already added 1 for each key in the
				//   section above... so no need to add one
				//   here again!
				min += a-b;//+1;
			}
		}
	}
	/*
	  MDW: this was coring on gk144 for 'health OR +sports' query.
	       and m_requireAllTerms was false...
	else {
		for ( int32_t i = 0 ; i < m_numLists ; i++ ) {
			// point to that list
			RdbList *clist = &m_lists[i];
			// start at first key
			clist->resetListPtr();
			// scan that list
			for(;!clist->isExhausted();clist->skipCurrentRecord()){
				int32_t a = ((uint8_t *)clist->m_listPtr)[7];
				int32_t b = ((uint8_t *)clist->m_listPtr)[6];
				if ( a - b < 0 ) { char *xx=NULL;*xx=0; }
				min += a-b;
			}
		}
	}
	*/

	// debug msg
	//log("minSize = %"INT32" docids q=%s",min,m_q->m_orig);

	// . now add in space for m_tmpDocIdPtrs2 (holds winners of a
	//   2 list intersection (more than 2 lists if we have phrases) [imap]
	// . allow for 5% more for inserting maxDocIds every block
	//   so hashTmpDocIds2() can use that
	// . actually, double in case we get all the termIds, but only one
	//   per slot scan/hash, so every other guy is a maxdocid
	// . no, now we use lastGuy/lastGuy2 to check if our new maxDocId
	//   is within 50 or less of the second last maxDocId added, and
	//   if it is, we bury the lastGuy and replace him.
	// . TODO: can we end up with a huge list and only one last guy?
	//         won't that cause performance issues? panics?
	int64_t nd = (105 * min) / 100 + 10 ;
	if ( min < 0 ) { char *xx=NULL;*xx=0;}
	if ( nd < 0  ) { char *xx=NULL;*xx=0;}
	int32_t need2 =
		( 4             +  // m_tmpDocIdPtrs2
		  nqt           +  // m_tmpScoresVec2
		  sizeof(qvec_t)+  // m_tmpEbitVec2
		  2                // m_tmpHardCounts2
		  ) * nd;
	// m_tmpDateVec2
	if ( m_sortByDate ) need2 += 4 * nd;
	// sorting by dist, we have a lat and lon vec per resut
	if ( m_sortBy == SORTBY_DIST ) need2 += 8 * nd;
	//if ( m_sortBy == SORTBY_TIME ) need2 += 8 * nd;

	if ( m_searchingEvents )
		// like we have a score for each query term, we now have
		// a single event id, which ranges from 1 to 255 (one byte)
		need2 += nd;
	
	// for both rat=0 and rat=1 we add docids to m_tmpDocIdPtrs2[] to
	// hold the docids until we can calculate affinities later and store
	// them finall into m_topDocIds[] after we score them.

	// . add space for our hash table for doing the intersections
	// . up to 10000 slots in the hash table
	// . docidptr     = 4
	//   scoreVec     = 1*nqt         // score vec, 1 byte per query term
	//   explicitBits = 1*sizeof(qvec_t)
	//   hardcount    = 2 // each list only needs 1 byte, but we use 2 here
	//   dates        = 4             // iff sortByDate
	int32_t need3 = 10000 * (4 + nqt + sizeof(qvec_t) + 2);
	// add in space for dates
	if ( m_sortByDate ) need3 += 10000 * 4;
	// and lat/lon of each
	if ( m_sortBy == SORTBY_DIST ) need3 += 10000 * 8;
	// two endpoints of time for each event
	//if ( m_sortBy == SORTBY_TIME ) need3 += 10000 * 8;
	// and event ids, one per entry
	if ( m_searchingEvents ) need3 += 10000 ;

	// . and finally, space for the boolean bit scores table!
	// . we now call Query::setBitScoresBoolean() from this thread so
	//   it does not block things up!
	// . how many bit combinations do we have for these terms?
	// . the bit #i represents term #i

	// No more truth table!

	//uint64_t numCombos = 0;
	// only for boolean queries
	//if ( m_q->m_isBoolean ) numCombos = 1LL << m_q->m_numExplicitBits;
	// do not use any more than 10MB for a boolean table right now
	//if ( numCombos > 10000000 ) { g_errno = ENOMEM; return false; }
	// 1 byte per combo
	//int32_t need4 = numCombos;

	// do it
	m_bufSize = need + need2 + need3;

	// . sanity! prevent a core from calling mmalloc with negative size
	// . if the query has too many terms that are not UOR'ed this will
	//   happen! especially since MAX_TERMS is now 1500...
	if ( m_bufSize < 0 ) {
		g_errno = ENOMEM;
		return log("query: table alloc overflow");
	}

	m_buf = (char *) mmalloc ( m_bufSize , "IndexTable2" );
	if ( ! m_buf ) return log("query: table alloc(%"INT64"): %s",
				  need,mstrerror(g_errno));

	// save it for breach checking below
	m_bufMiddle = m_buf + middle;

	m_tmpLatVec2  = NULL;
	m_tmpLonVec2  = NULL;
	//m_tmpTimeVec2 = NULL;

	char *p = m_buf + middle;
	m_tmpDocIdPtrs2  = (char    **)p ; p += nd * 4;
	m_tmpScoresVec2  = (uint8_t  *)p ; p += nd * nqt;
	m_tmpEbitVec2    = (qvec_t   *)p ; p += nd * sizeof(qvec_t);
	m_tmpHardCounts2 = (int16_t    *)p ; p += nd * 2;
	if ( m_sortByDate ) {
		m_tmpDateVec2 = (uint32_t *)p; p += nd * 4; }
	if ( m_sortBy == SORTBY_DIST ) {
		m_tmpLatVec2 = (uint32_t *)p; p += nd * 4; 
		m_tmpLonVec2 = (uint32_t *)p; p += nd * 4; 
	}
	//if ( m_sortBy == SORTBY_TIME ) {
	//	m_tmpTimeVec2 = (uint32_t *)p; p += nd * 4; 
	//	m_tmpEndVec2  = (uint32_t *)p; p += nd * 4;
	//}
	// like we have a score vector, we have an event id bit vector
	if ( m_searchingEvents ) {
		// each ptr is 4 bytes, unlike scores which are 1 byte each
		m_tmpEventIds2 = (uint8_t *)p; 
		p += nd ;
	}
	// we don't have any stored in there yet
	m_numTmpDocIds2 = 0;
	// we can't exceed this max
	m_maxTmpDocIds2 = nd;

	// set the hash table buf to the end
	m_localBuf = p;
	m_localBufSize = need3;
	p += need3;

	// the boolean bit scores tbale
	//m_bitScoresBuf     = p;
	//m_bitScoresBufSize = need4;
	//p += need4;

	// sanity -- must match up exactly
	if ( p - m_buf != m_bufSize ) { char *xx = NULL; *xx = 0; }

	if ( ! makeHashTables() ) return false;

	// sort by distance needs this too to see if events are expired!
	//if ( m_sortBy == SORTBY_TIME ) {
	if ( m_r->m_clockSet ) m_nowUTCMod = m_r->m_clockSet;
	else                   m_nowUTCMod = m_r->m_nowUTC;
	// add to it
	m_nowUTCMod += m_r->m_clockOff;
	//}

	// success
	return true;
}

bool IndexTable2::allocTopTree ( ) {
	int32_t nn = m_docsWanted;
	if ( m_doSiteClustering ) nn *= 2;
        // limit to this regardless!
        CollectionRec *cr = g_collectiondb.getRec ( m_coll );
        if ( ! cr ) return false;
        //if ( nn > cr->m_maxDocIdsToCompute ) nn = cr->m_maxDocIdsToCompute;
	if ( ! m_topTree->setNumNodes(nn,m_doSiteClustering,m_searchingEvents))
		return false;
	return true;
}

bool IndexTable2::makeHashTables ( ) {
	// reset
	m_useYesDocIdTable = false;
	m_useNoDocIdTable  = false;
	m_useBoolTable     = false;
	// clear this
	m_dt.reset();
	m_et.reset();
	m_bt.reset();

	// populate our hash table of docids
	if ( ! m_r ) return true;
	// count how many docids we got, they are a cgi value, so represented
	// in ascii separated by +'s. i.e. "12345+435322+3439333333"
	if ( ! hashFromString ( &m_dt , m_r->ptr_sqDocIds ) ) return false;
	// set a flag
	if ( m_dt.m_numSlotsUsed > 0 && m_r->size_sqDocIds > 1 )
		m_useYesDocIdTable = true;

	// likewise, exclude thse docids
	if ( ! hashFromString ( &m_et , m_r->ptr_noDocIds ) ) return false;
	// set a flag
	if ( m_et.m_numSlotsUsed > 0 && m_r->size_noDocIds > 1 )
		m_useNoDocIdTable = true;
	
	if ( m_q->m_isBoolean ) {
		//if (m_isDebug)
		//	logf(LOG_DEBUG, 
		//	     "query: getBitScoreCacheInit "
		//	     "queryId=%lld",
		//	     m_r->m_queryId);
		
		m_useBoolTable = true;
		// reasonable initial number of slots?
		// should be more than enough for most queries
		if (!m_bt.set(1024)) return false;
	}
	return true;
}


IndexTable2 *g_this = NULL;

// . these lists[] are 1-1 with q->m_qterms
void IndexTable2::addLists_r ( int32_t *totalListSizes , float sortByDateWeight ){

	// . assume no use to read more from disk
	// . so Msg39 knows if we can go to the next tier to get more 
	//   docs. this is the "isDiskExhausted" parm in Msg3a.cpp
	m_isDiskExhausted= true;

	g_this = this;

	// clear, set to ECORRUPTDATA below
	m_errno = 0;

	// assume no-op
	m_t1 = 0LL;

	// sanity check
	if ( ! m_useDateLists && m_sortByDate ) { 
		log(LOG_LOGIC,"query: bad useDateLists/sortByDate.");
		//char *xx = NULL; *xx = 0; }
		m_sortByDate = false;
	}

	// bail if nothing to intersect... if all lists are empty
	if ( m_ni == 0 ) return;

	// it is -1 if init() never called
	if ( m_ni == -1 ) return;

	// sanity check - we always need at least one block to intersect
	if ( m_nb <= 0 ) { char *xx = NULL; *xx = 0; }

	// if query is boolean, turn off rat
	if ( m_q->m_isBoolean ) m_requireAllTerms = false;

	// hks = half key size (size of everything except the termid)
	char hks  = 6; 
	// fks = full key size
	char fks  = 12;
	// dateLists are 4 more bytes per key than standard 12-byte key lists
	// because they have a 4-byte date/time stamp in addition to 
	// everything else
	if ( m_useDateLists ) { hks += 4; fks += 4; }

	// is the intersection empty?
	bool empty = false;
	// it is if first termlist is empty, if so, intersection may be empty
	if ( m_sizes[m_imap[0]] < fks ) empty = true;
	// not for rat = 0 though
	if ( ! m_requireAllTerms ) empty = false;
	// git its ebit
	qvec_t ebit = m_q->m_qterms [ m_imap[0] ].m_explicitBit;
	// and not empty if a synonym termlist implies this and is non-empty
	for ( int32_t i = 0 ; empty && i < m_q->m_numTerms ; i++ ) {
		// skip ourselves
		if ( i == m_imap[0] ) continue;
		// does this guy imply us? if not skip him too
		if ( ! (m_q->m_qterms[i].m_implicitBits & ebit) ) continue;
		// is he empty? skip if so
		if ( m_sizes[i] < fks ) continue;
		// otherwise, we have a fighting chance!
		empty = false;
	}

	// if intersection is definitely empty, just return now,
	// m_isDiskExhausted should already be set to true
	if ( empty ) return;

	// set start time
	int64_t t1 = gettimeofdayInMilliseconds();

	// . set the bit map so m_tmpq.m_bmap[][] gets set and we can convert
	//   an explicit vector to an implicit bit vector by 
	//   Query::getImplicits()
	// . this might take some time so do it in a thread
	// . this may be already set if we are being re-called from Msg39's
	//   re-call to gotLists() because of excessive site clustering
	if ( ! m_q->m_bmapIsSet ) m_q->setBitMap();

	// No more truth table!
	// . set boolean bit scores necessary for resolving boolean queries
	// . maps an implicit bit vector to a "true" if it is a valid search
	//   results or "false" if it is not
	//if ( m_q->m_isBoolean ) {
		// this returns false if no truths are possible, in which
		// case we have no search results
	//	if ( ! m_q->setBitScoresBoolean ( m_bitScoresBuf     ,
	//					  m_bitScoresBufSize ) )
	//		return;
	//}

	// . now swap the top 12 bytes of each list
	// . this gives a contiguous list of 6-byte score/docid entries
	//   because the first record is always 12 bytes and the rest are
	//   6 bytes (with the half bit on) due to our termid compression
	// . this makes the lists much much easier to work with, but we have
	//   to remember to swap back when done!
	for ( int32_t i = 0 ; i < m_numLists ; i++ ) {
		// skip if list is empty, too
		if ( m_lists[i].isEmpty() ) continue;
		// point to start
		char *p = m_lists[i].getList();
		// remember to swap back when done!!
		char ttt[10];
		gbmemcpy ( ttt   , p       , hks );
		gbmemcpy ( p     , p + hks , 6   );
		gbmemcpy ( p + 6 , ttt     , hks );
		// point to the low "hks" bytes now
		p += 6;
		// turn half bit on
		*p |= 0x02;
	}

	// . we use a hash table to intersect the termlists
	// . count # of panics (panics when hash table gets full)
	m_numPanics = 0;
	// and # of collisions in the hash table
	m_numCollisions = 0;
	// count # of hash loops we do
	m_numLoops = 0;

	// . we can ditch the bit vector and use a "hard count" for each docid
	// . this is how many REQUIRED query terms a doc has
	// . some list have a "hard count" INSTEAD of an explicit bit, for them
	//   we just add up our hard count and make sure we have at least
	//   minHardCount for the docid to be a match.
	int32_t minHardCount = 0;
	
	// if not rat, do it now
	if ( ! m_requireAllTerms ) { 
		// . no re-arranging the query terms for default OR searches
		//   because it is only beneficial when doing sequential
		//   intersectinos to minimize the intersection and speed up
		// . but we did get Query::getImap() above, however, that is
		//   only good for rat=1 queries
		/*
		int32_t count = 0;
		for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
			// component lists are merged into one compound list
			if ( m_componentCodes[i] >= 0 ) continue;
			// skip repeated terms too!
			if ( m_q->m_qterms[i].m_repeat ) continue;
			m_imap[count] = i;
			// don't forget to update revImap
			m_revImap[i] = count;
			// inc count now
			count++;
		}
		// set the new imap size here too
		m_ni = count;
		*/
		// . intersect all termlists in a single call to addLists2_r
		// . not nearly as fast as doing a single intersection with
		//   each call to addLists2_r() as rat=1 does, because that
		//   shrinks the active intersection with each call and
		//   eliminates a lot of computations.
		// . "minHardCount" will be set to the number of query terms
		//   required to be a perfect result.
		// . even though m_numLists may be greater than m_ni, 
		//   we select the lists from imap space in addLists2_r()
		addLists2_r ( m_ni             , // numLists
			      m_imap           ,
			      true             ,
			      0                ,
			      sortByDateWeight ,
			      &minHardCount    );
		goto swapBack;
	}

	// i added this superfluous '{' so we can call "goto swapBack" above.
	{ 

	// TODO: use ptrs to score so when hashing m_tmpDocIdPtrs2 first and 
	// another non-required list second we can just add to the scores 
	// directly? naahhh. too complicated.

	// count number of base lists
	int32_t numBaseLists = m_blocksize[0];

	// how many lists to intersect initially? that is numLists.
	int32_t numListsToDo = m_blocksize[0];
	// if we got a second block, add his lists (single term plus phrases)
	if ( m_nb > 1 ) numListsToDo += m_blocksize[1];

	// how many "total" termlists are we intersecting?
	int32_t total = 0;
	for ( int32_t i = 0 ; i < m_nb ; i++ ) 
		total += m_blocksize[i];

	// . if this is true we set m_topDocIds/m_topScores/etc. in 
	//   addLists2_r() to the FINAL list of winning docids/scores/etc.
	// . so if this is a two word query it will be the last round and
	//   we end up only calling addLists2_r() once
	bool lastRound = (numListsToDo == total); 

	// this will intersect the numListsToDo lists and if this is the 
	// "lastRound" then the top docids, m_topDocIds[], etc., will be set, 
	// otherwise, the current intersection will be stored into 
	// m_tmpDocIdPtrs2[], etc., and we have to call addLists2_r() again 
	// with the next block below.
	addLists2_r ( numListsToDo , 
		      m_imap , lastRound , numBaseLists , 
		      sortByDateWeight , &minHardCount );

	// the first list in each block is the explicit list i guess, and
	// the other lists in the same block imply the first list. so
	// "cat" would be the first list and "cat dog" would be the 2nd list
	// in the same block as "cat".
	int32_t m0 = m_imap[0];
	int32_t m1 = 0 ;
	if ( m_nb > 1 ) m1 = m_imap[m_blocksize[0]];
	// totalListSizes[m0] is the size (number of docids) we were trying 
	// to get for this list. m_sizes[m0] is the actual size. was it int16_t?
	// if so, that means there are no more docids onDisk/available.
	if (     m_sizes[m0] >= totalListSizes[m0]&&m_q->getTermSign(m0)!='-')
		m_isDiskExhausted = false;
	if(m_nb>1&& m_sizes[m1]>=totalListSizes[m1]&&m_q->getTermSign(m1)!='-')
		m_isDiskExhausted = false;

	// . offset into imap
	// . imap[off] must NOT be a signless phrase term
	// . we already intersected the first 2 blocks of lists at this point
	//   so make "off" point to the next block to throw into the 
	//   intersection
	int32_t off = numListsToDo;

	// . follow up calls
	// . intersect one block at a time into the "active intersection"
	// . the "active intersection" is stored in the m_tmpDocIdPtrs2[]
	//   array and is hashed into the hash table along with the lists
	//   in block #i.
	// . this is the rat=1 algo that is the reason why it is much faster
	//   than rat=0, because the intersection is ever-shrinking requiring
	//   less and less CPU cycles to intersect.
	for ( int32_t i = 2 ; i < m_nb ; i++ ) {
		// if it is the lastRound then addLists2_r() will compute
		// the final m_topDocIds/m_topScores arrays which contains
		// the highest-scoring docids
		lastRound = (i == m_nb - 1);
		// . if we have no more, might as well stop!
		// . remember, maxDocIds is stored in here, so it will be at
		//   least 1
		// . "maxDocIds" is a fake docid that tells us how many docids
		//   are stored in m_tmpDocIdPtrs2[] so we know how many
		//   docids are in the "active intersection"
		// . so if this count is 1 we are still empty!
		if ( m_numTmpDocIds2 <= 1 ) break;
		// is this list undersized? i.e. more left on disk
		int32_t mx = m_imap[off];
		if (m_sizes[mx]>=totalListSizes[mx]&&m_q->getTermSign(mx)!='-')
			m_isDiskExhausted = false;
		// . set number of lists in block #i
		// . this is how many lists we are intersecting with the
		//   "active intersection"
		numListsToDo = m_blocksize[i];
		// . add it to the intersection
		// . this will intersect the docids in m_tmpDocIds2[] with
		//   this block of lists
		addLists2_r ( numListsToDo , 
			      m_imap + off , lastRound, 0 , 
			      sortByDateWeight , &minHardCount );
		// skip to next block of lists
		off += m_blocksize[i];
	}

	// end superfluous {
	}

swapBack:
	// compute total number of docids we dealt with
	m_totalDocIds = 0;
	// now swap the top 12 bytes of each list back into original order
	for ( int32_t i = 0 ; i < m_numLists ; i++ ) {
		// skip if list is empty, too
		if ( m_lists[i].isEmpty() ) continue;
		// . compute total number of docids we dealt with
		// . date lists have 5 bytes scores, not 1 byte scores
		m_totalDocIds += (m_lists[i].getListSize()-6)/hks;
		// point to start
		//char *p = m_lists[i].getList();
		// swap back
		//char ttt[10];
		//gbmemcpy ( ttt   , p       , hks );
		//gbmemcpy ( p     , p + hks , 6   );
		//gbmemcpy ( p + 6 , ttt     , hks );
		// turn half bit off
		//*p &= ~0x02;
	}
	// get time now
	int64_t now = gettimeofdayInMilliseconds();
	// store the addLists time
	m_addListsTime = now - t1;
	m_t1 = t1;
	m_t2 = now;
	/*
	// . measure time to add the lists in bright green
	// . use darker green if rat is false (default OR)
	int32_t color;
	char *label;
	if ( ! m_requireAllTerms ) {
		color = 0x00008000 ;
		label = "termlist_intersect_soft";
	}
	else {
		color = 0x0000ff00 ;
		label = "termlist_intersect";
	}
	g_stats.addStat_r ( 0 , t1 , now , label, color );
	*/
}


// . this makes 2 tables
// . the weightTable[] maps the single byte score from a particular query term
//   to the 4 byte score, and it includes the affinity (phrase AND synonym)
//   and term frequence weights
// . the weightTable[] also includes the effects of user defined weights
// . the affWeightTable[] does the same, but only includes the affinity Weights
/*
void makeScoreTable ( ) {
	// . make score table to avoid IMUL instruction (16-36 cycles)
	// . 1k per table! will that fit in L1/L2 cache, too?
	// . if too big, limit j to ~32 to cover most common scores
	// . remember that scores in the indexdb keys are complemented
	//   so higher guys appear on top
	// . if score is weighted with a relative 0 weight, that means
	//   that the term is required, but should not contribute to
	//   the final ranking in any way, so use a score of 1 despite
	//   the term count of this term in the page
	QueryTerm *qt = &m_q->m_qterms[m];
	// one score table per query term
	for ( int32_t i = 0 ; i < numListsPerTier; i++ ) { 
		scoreTable [ i ] = (int32_t *)p; p += 256 * sizeof(int32_t); }
	if ( qt->m_userWeight == 0 && qt->m_userType == 'r' )
		for ( int32_t k = 0 ; k < 256 ; k++ )
			scoreTable[i][k] = 1;
	else
		for ( int32_t k = 0 ; k < 256 ; k++ )
			scoreTable[i][k] = m_freqWeights[m] * (255-k);
	// sorty by date uses the dates as scores, but will add the
	// normal score to the date, after weighting it with this
	if ( m_sortByDate ) // && sortByDateWeight > 0.0 )
		for ( int32_t k = 0 ; k < 256 ; k++ )
			scoreTable[i][k] = (int32_t)((float)
						  scoreTable[i][k] * 
						  sortByDateWeight);
}
*/

// doubling this will half the number of loops (m_numLoops) required, but if 
// we hit the L2 cache more then it might not be worth it. TODO: do more tests.
#define RAT_SLOTS (1024)

// . the 1.4Ghz Athlon has a 64k L1 data cache (another 64k for instructions)
// . TODO: put in conf file OR better yet... auto detect it!!!
// . we're like 3 times faster on host0 when L1 cache is 32k instead of 64k
//#define L1_DATA_CACHE_SIZE (64*1024)
//#define L1_DATA_CACHE_SIZE (32*1024)
// this is only 8k on my new pentium 4s!!!
#define L1_DATA_CACHE_SIZE (8*1024)

// . this is in bytes. the minimal size of a cached chunk of mem.
// . error on the large side if you don't know
// . this is 64 bytes for Athlon's from what I've heard
// . this is 64bytes for my new pentium 4s, too
#define L1_CACHE_LINE_SIZE  64

// . DO NOT set g_errno cuz this is probably in a thread
// . these lists should be 1-1 with the query terms
// . we try to restrict the hash table to the L1 cache to avoid slow mem
// . should result in a 10x speed up on the athlon, 50x on newer pentiums,
//   even more in the future as the gap between L1 cache mem speed and
//   main memory widens
void IndexTable2::addLists2_r ( int32_t       numListsToDo     ,
				int32_t      *imap             ,
				bool       lastRound        ,
				int32_t       numBaseLists     ,
				float      sortByDateWeight ,
				int32_t      *minHardCountPtr  ) {
	// int16_tcut
	int32_t rat = m_requireAllTerms;
	// reset for hashTmpDocIds2()
	m_nexti = 0;

	// sanity check
	if ( numListsToDo <= 0 ) { char *xx = NULL; *xx = 0; }

	// . get the "required" bits bit mask
	// . every docid in the results has a bit vector
	// . if that bit vector matches "requiredBits" vector then the docid
	//   has all the required query terms to be a match
	qvec_t requiredBits = 0;
	// any term that has a '+'" in front of it is a forced term
	qvec_t forcedBits  = 0;
	// what explicit bits (query terms) are preceeded by a minus sign?
	qvec_t negativeBits = m_q->m_negativeBits;

	// the number of query terms in imap space, excludes query terms
	// that do not directly have a termlist at this point
	int32_t nqt = m_ni;

	// . convenience ptrs
	// . the highest-scoring docids go into these array which contain
	//   the final results, sorted by score, when we are done
	//char          **topp  = m_topDocIdPtrs ;
	//score_t        *tops  = m_topScores    ;
	//unsigned char  *topb  = m_topBitScores ;

	// assume no top docIds now
	int32_t numTopDocIds    = 0;

	////////////////////////////////////
	// begin hashing setup
	////////////////////////////////////

	// count # of docs that EXPLICITLY have all query singleton terms
	//int32_t explicitCount = 0;
	// count # of docs that IMPLICITLY have all query singleton terms
	//int32_t implicitCount = 0;
	// . count all mem that should be in the L1 cache
	// . the less mem we use the more will be available for the hash table
	int32_t  totalMem = 0;

	// . we mix up the docIdBits a bit before hashing using this table
	// . TODO: what was the reason for this? particular type of query
	//   was too slow w/o it?
	// . a suburl:com collision problem, where all docids have the same
	//   score and are spaced equally apart somehow made us have too
	//   many collisions before, probably because we included the score
	//   in the hash...? use bk revtool IndexTable2.cpp to see the old file
	/*
	static uint32_t s_mixtab [ 256 ] ;
	// is the table initialized?
	static bool s_mixInit = false;
	if ( ! s_mixInit ) {
		srand ( 1945687 );
		for ( int32_t i = 0 ; i < 256 ; i++ ) 
			s_mixtab [i]= ((uint32_t)rand());
		s_mixInit = true;
		// randomize again
		srand ( time(NULL) );
	}
	// s_mixtab should be in the L1 cache cuz we use it to hash
	totalMem += 256 * sizeof(int32_t);
	*/

	// . now form a set of ptrs for each list
	// . each "ptrs[i]" pts to a sublist of a list in lists[]
	// . each "ptrs[i]" pts to the first 6-byte key for a particular score
	// . each sublist contains all the docids that have the same termid
	//   and same score
	// . each sublist is sorted by docid
	// . this makes it easy to intersect a small RANGE of docids at a time
	//   into our small hash table, contained in the L1 cache for speed
	char  *p   = m_buf;
	char **ptrs                = (char  **)p; p += nqt *sizeof(char *)*256;
	char **pstarts             = (char  **)p; p += nqt *sizeof(char *)*256;
	char **ptrEnds             = NULL;
	if ( m_useDateLists ) {
		ptrEnds            = (char  **)p; p += nqt *sizeof(char *)*256;
	}
	// these are for rolling back ptrs[] in the even of a panic
	char **oldptrs             = (char  **)p; p += nqt *sizeof(char *)*256;
	// . this mem is used for two tables now. saves us MUL instructions.
	// . see makeScoreTables() 
	int32_t **scoreTable1         = (int32_t  **)p; p += nqt *sizeof(int32_t );
	int32_t **scoreTable2         = (int32_t  **)p; p += nqt *sizeof(int32_t );
	// we have to keep this info handy for each list
	char   *listSigns          = (char   *)p; p += nqt *sizeof(char  );
	char   *listHardCount      = (char   *)p; p += nqt *sizeof(char  );
	int32_t  **listScoreTablePtrs1= (int32_t  **)p; p += nqt *sizeof(int32_t **);
	int32_t  **listScoreTablePtrs2= (int32_t  **)p; p += nqt *sizeof(int32_t **);
	int32_t   *listIndexes        = (int32_t   *)p; p += nqt *sizeof(int32_t);
	char  **listEnds           = (char  **)p; p += nqt *sizeof(char *);
	char   *listHash           = (char   *)p; p += nqt *sizeof(char  );
	qvec_t *listExplicitBits   = (qvec_t *)p; p += nqt *sizeof(qvec_t);
	int32_t   *listPoints         = (int32_t   *)p; p += nqt *sizeof(int32_t  );

	// do not breech
	if ( p > m_bufMiddle ) {
		log(LOG_LOGIC,"query: indextable: Got table "
		    "mem breech.");
		char *xx = NULL; *xx = 0;
	}

	// half key size (size of everything except the termid)
	char hks  = 6; 
	// dateLists are 4 more bytes per key than standard 12-byte key lists
	if ( m_useDateLists ) hks += 4;

	// . "numPtrs" is how many sublists we have from all termslists
	// . all the docids in each sublist have the same termid/score
	int32_t             numPtrs   = 0;
	// how many docids are in all lists we are intersecting here
	uint32_t    numDocIds = 0;
	// just used for stats
	int32_t             numSorts  = 0;
	// . how many of the terms we are intersecting are "required" and 
	//   therefore do not have an associated explicit bit. this allows us
	//   to support queries of many query terms, when the query terms 
	//   exceeds the bits in "qvec_t".
	// . resume where we left off, this build on itself since we keep a 
	//   total ongoing hard count in m_tmpHardCounts2[]
	int32_t minHardCount = *minHardCountPtr;
	// . each list can have up to 256 ptrs, corrupt data may mess this up
	// . because there are 256 total possible single byte scores
	int32_t maxNumPtrs = nqt * 256;
	// only log this error message once per call to this routine
	char pflag = 0;
	// time the gbsorting for the datelists
	int64_t t1 = gettimeofdayInMilliseconds();

	// set latTermOff/lonTermOff
	m_latTermOff  = -1;
	m_lonTermOff  = -1;
	m_timeTermOff = -1;
	m_endTermOff  = -1;

	// . add a bunch of lists info for every termlist we have
	// . list ptrs and info are stored in the list*[] arrays
	// . loop over every imapped term, there are m_ni of them
	// . terms which are ignored, etc. are not imapped
	for ( int32_t i = 0 ; i < numListsToDo ; i++ ) {
		// . map i to a list number in query term space
		// . this "imap" may be shifted from the normal m_imap
		int32_t m = imap[i];
		// skip if list is empty. NO! that will screw up the 1-1
		// correlation of the "numLists" with imap space
		//if ( lists[m].isEmpty() ) continue;
		// are we using the hard count instead of an explicit bit?
		char hc = 0;
		// get the query term
		QueryTerm *qt = &m_q->m_qterms[m];
		// if we use hard counts we can require a lot more terms
		// because we are not limited by the # of bits in qvec_t,
		// however, a phrase term cannot IMPLY us because we have
		// no explicit bit! so this is not a good thing in general
		// unless you do not care about small tier sizes truncating
		// your result set. BUT, some lists can have the explicit bit
		// and some may just use a hard count, so you can have a 
		// hybrid, and the ones that have a hard count, perhaps do not
		// need to be IMPLIED by a phrase term...
		if ( qt->m_explicitBit == 0 ) {
			// . it is either a phrase in quotes or a single word
			// . or something like cd-rom or www.abc.com
			if ( qt->m_isPhrase ) {
				if ( qt->m_inQuotes        ) minHardCount++;
				if ( qt->m_termSign == '+' ) minHardCount++;
				if ( qt->m_termSign == '*' ) minHardCount++;
				hc = 1;
			}
			else {
				minHardCount++;
				hc = 1;
			}
		}
		// build a local require bit vector, since we may not be
		// intersecting all termlists in this one call
		if ( qt->m_explicitBit & m_q->m_requiredBits ) 
			requiredBits |= qt->m_explicitBit;
		if ( qt->m_explicitBit & m_q->m_forcedBits )
			forcedBits   |= qt->m_explicitBit;
		// count mem from score table
		totalMem += 256 * sizeof(int32_t);
		// there is also a affWeight only score table now too
		totalMem += 256 * sizeof(int32_t);
		// corrupt data can make numPtrs too big
		if ( numPtrs >= maxNumPtrs ) {m_errno = ECORRUPTDATA; return;}
		// every query term has a unique identifier bit
		// unless it uses the hard count (hc) method
		qvec_t ebit = qt->m_explicitBit;
		// get the sign
		char sign = qt->m_termSign;
		// . rat is special
		// . do not change the sign to 'd' before checking if its '-'
		// . we use the definition from Query.h::m_requiredBits
		/*
		if ( rat ) {
			// negative terms are NEVER required
			if ( sign == '-' )
				required |= 0x00;
			// . certain signed phrases are always required
			// . '*' means cd-rom (connected) or in quotes
			// . '+' means +"my phrase" (force required)
			else if ( qt->m_isPhrase && (sign=='+' || sign=='*'))
				required |= ebit;
			// single word terms ALWAYS required for rat=1
			else if ( ! qt->m_isPhrase )
				required |= ebit;
		}
		*/
		// are we date?
		if ( m_useDateLists && m_sortByDate ) sign = 'd';
		// this is not sort by date...
		else if ( m_useDateLists ) sign= 'e';
		// a speciali list?
		if ( qt->m_fieldCode == FIELD_GBLATRANGE ||
		     qt->m_fieldCode == FIELD_GBLONRANGE ||
		     qt->m_fieldCode == FIELD_GBLATRANGECITY ||
		     qt->m_fieldCode == FIELD_GBLONRANGECITY )
		     //qt->m_fieldCode == FIELD_GBSTARTRANGE ||
		     //qt->m_fieldCode == FIELD_GBENDRANGE )
			sign = 'x';
		// set sign of list
		listSigns[i] = sign;
		// . point to its table
		// . these tables will be initialized before
		//   fillTopDocIds() is called!
		// . we need to call makeScoreTables() to do that
		listScoreTablePtrs1[i] = scoreTable1[i];
		listScoreTablePtrs2[i] = scoreTable2[i];
		//listIndexes      [i] = i;
		// int16_tcut
		int32_t rm = m_revImap[m];
		listIndexes        [i] = rm;

		// set this
		if ( qt->m_fieldCode == FIELD_GBLATRANGE  )  m_latTermOff = rm;
		if ( qt->m_fieldCode == FIELD_GBLATRANGECITY)m_latTermOff = rm;
		if ( qt->m_fieldCode == FIELD_GBLONRANGE  )  m_lonTermOff = rm;
		if ( qt->m_fieldCode == FIELD_GBLONRANGECITY)m_lonTermOff = rm;
		//if ( qt->m_fieldCode == FIELD_GBSTARTRANGE ) 
		//	m_timeTermOff =rm;
		//if ( qt->m_fieldCode == FIELD_GBENDRANGE   ) m_endTermOff=rm;

		// sanity check
		if ( listIndexes[i] < 0 || listIndexes[i] >=m_q->m_numTerms ){
			char *xx = NULL; *xx = 0; }
		// and its explicit bit
		listExplicitBits[i] = ebit;
		// synonyms are special, if we contain one, assume we 
		// explicitly have what they are a synonym of.
		// NO! because it messes up the computeAffinities(), that looks
		// for docs that contain all explicit bits to gen synAff. also
		// it messes up zeroOutComponentVectors()
		//if ( qt->m_synonymOf )
		//	listExplicitBits[i] = m_q->getImplicits ( ebit );
		// some have a hard count instead of an explicit bit
		listHardCount[i] = hc;
		// . the end of this list
		// . the hash loop removes it from the array when it
		//   has been exhausted (i.e. all of its docids have
		//   been hashed/intersected)
		listEnds[i] = m_lists[m].getListEnd();
		// . should list be hashed first?
		// . only applies to rat=1
		// . when rat=1 and we are called for the first time
		//   two *blocks* of lists are passed in. each block
		//   may have multiple lists in it. numBaseLists is
		//   how many lists are in the first block, block[0].
		if   ( i < numBaseLists ) listHash[i] = true;
		else                      listHash[i] = false;
		if   ( ! rat            ) listHash[i] = true;
		// what is the first sublist # this list has?
		listPoints[i] = numPtrs;
		// . it should be in L1 cache, too
		// . 1      = listSign
		//   4      = listScoreTablePtrs1
		//   4      = listScoreTablePtrs2
		//   4      = listIndex
		//   qvec_t = listExplicitBits
		//   1      = listHardCount
		//   4      = listEnds
		//   1      = listHash
		//   4      = listPoints
		totalMem += 1 + 4 + 4 + 4 + sizeof(qvec_t) + 1 + 4 + 1 + 4;
		// . now fill the ptr[] array
		// . reset ptrs to start of list
		m_lists[m].resetListPtr();
		// point to start
		char *p = m_lists[m].getList();
		// and end
		char *pend = m_lists[m].getListEnd();
		// . if empty watch out!
		// . we still need to inc numLists and have this list here
		//   as a place holder because we listIndexes[i] needs to
		//   be 1-1 with the imap for setting the scoresVec[]
		if ( m_lists[m].getListSize() == 0 ) {
			// must let inner intersectino loop know we are empty
			ptrs    [ numPtrs ] = NULL;
			pstarts [ numPtrs ] = NULL;
			if ( m_useDateLists ) ptrEnds [ numPtrs ] = NULL;
			// inc this
			numPtrs++;
			// try next termlist
			continue;
		}
		// add to total docids
		numDocIds += (m_lists[m].getListSize() - 6) / hks;
		// point to the low 6 bytes now
		p += 6;
		// turn half bit on
		*p |= 0x02;
		// set ptr to sublist #numPtrs
		ptrs [ numPtrs ] = p;
		
		// to make sort by date faster, we now artificially
		// create 256 buckets on each date list and then
		// sort each bucket by docid
		if ( m_useDateLists ) {
			int32_t listSize = pend - p;
			int32_t pstep    = listSize / 250;
			// . do not go too low
			// . TODO: try lowering this to see if it
			//         gets faster. i could not test lower
			//         values because buzz starting 
			//         querying again.
			if ( pstep < 10000 ) pstep = 10000;
			// make sure it is divisible by 10
			while ( (pstep % hks) ) pstep--;
			// do not go crazy
			if ( pstep <= 0 ) pstep = listSize;
			// now make 256 approximately equally sized
			// buckets of datedb records . a lot of times
			// the dates are the same and they are already
			// sorted by docid, so we might want to check
			// for that to save time. (TODO)
			for ( ; p < pend ; p += pstep ) {
				// get the end
				char *end  = p + pstep;
				if ( end > pend ) end = pend;
				// store the current ptr info
				pstarts [ numPtrs   ] = p;
				ptrEnds [ numPtrs   ] = end;
				ptrs    [ numPtrs++ ] = p;
				int32_t  size = end - p;
				// count it
				numSorts++;
				// the biggest event datedb termlists are
				// probable gbresultset:1 and maybe the
				// lat lon lists, and other flag-ish lists
				// so i'd say they all are like
				// evidrange 1-1 or n-n, a score of 1 and
				// then docid... so quickly check to see if
				// the docids are already in order. and even
				// better might be to partition the buckets
				// on n-n boundaries if possible.
				// also, don't include gbdaily:1 UOR ...
				// if they are all checked!!
				/*
				  CRAP! this was dropping results!!
				if ( *(int64_t *)(end-10+5) ==
				     *(int64_t *)(p+5) &&
				     *(int16_t *)(end-10+8) ==
				     *(int16_t *)(p+8)
				     )
					continue;
				*/
				// now sort each p
				// NO NO this is super slow for us!!
				hsort_10_1_5((uint8_t *)p, size / 10 );
				//gbsort( p, size, hks, dateDocIdCmp );
			}
			// skip the rest
			continue;
		}
		
		// . set this new var so we don't rollback too much
		//   when re-hashing because of a panic
		// . a panic occurs when the hash table fills up
		//   because we did not select a small enough docid
		//   range.
		// . when we panic we decrease ptrs[i] for every i
		//   until pointing to the beginning of the docid range
		//   so we need to make sure we do not drift into
		//   the previous sublist.
		// . we can't just use ptrs[i-1] cuz it might be in a 
		//   different termlist!
		pstarts [ numPtrs++ ] = p;
		// advance
		p += 6;
		// . fill up the ptrs array
		// . find all the sublists
		// . each sublist is responsible for a single score
		// . there are up to 256 different scores
		for ( ; p < pend ; p += hks ) {
			// if we got a new score, add a new ptr
			if ( p[5] != p[-1] ) {
				// if data is corrupt, we have
				// to watch so we don't breech!!
				if ( numPtrs >= maxNumPtrs ) {
					if ( ! pflag )
						log(LOG_LOGIC,
						    "query: Got indextable "
						    "breech. np=%"INT32"",numPtrs);
					pflag = 1;
					break;
				}
				// speed test -- remove the small head
				// score is COMPLEMENTED!!!
				//unsigned char c =(unsigned char)p[5];
				//if ( 255-c <= (unsigned char)25 ) {
				pstarts [ numPtrs   ] = p;
				ptrs    [ numPtrs++ ] = p;
				//}
			}
			// . jump by 30 if he's got same score as us
			// . this saves a good amount of time!
			while (p+300*6 < pend && p[300*6+5] == p[5] )
				p += 300*6;
		}
	}

	// . init these to NULL! in case we panic and a list is empty, its
	//   oldptrs[i] should be NULL. otherwise, we rollback into garbage.
	// . oldptrs[] are 1-1 with ptrs[]/numPtrs
	memset ( oldptrs , 0 , numPtrs * 4 );

	// time the gbsorting for the datelists
	if ( m_isDebug || g_conf.m_logDebugQuery ) {
		int64_t t2 = gettimeofdayInMilliseconds();
		logf(LOG_DEBUG,"query: Took %"INT64" ms to prepare list ptrs. "
		     "numDocIds=%"UINT32" numSorts=%"INT32"",
		     t2 - t1 , numDocIds , numSorts );
		logf(LOG_DEBUG,"query: numListsToDo=%"INT32" "
		     "lastRound=%"INT32" numBaseLists=%"INT32" "
		     "sortByDateWeight=%f negbits=0x%"XINT32"",
		     numListsToDo,(int32_t)lastRound,numBaseLists,
		     sortByDateWeight,(int32_t)negativeBits);
		for ( int32_t i = 0 ; i < numListsToDo ; i++ ) {
			int32_t m = imap[i];
			// add in bonus since "imap" points within m_imap
			int32_t off = imap - m_imap;
			logf(LOG_DEBUG,"query: imap[%"INT32"]=%"INT32"",i+off,m);
		}
	}

	// . save it
	// . how many query terms are required but had no explicit bit set?
	// . this is an ongoing count and increases with each call to 
	//   addLists2_r()
	*minHardCountPtr = minHardCount;
	// count mem from ptrs
	totalMem += numPtrs * sizeof(char *);
	// . and what they point to too! first line should/will be in cache.
	// . assume a cache line size of 64 bytes
	totalMem += numPtrs * L1_CACHE_LINE_SIZE;
	// count miscellaneous mem access (like "point" and "next")
	totalMem += 256;
	// convenience vars
	//register int32_t i = 0 ;
	int32_t j;

	// a dummy var
	//int64_t tmpHi = 0x7fffffffffffffffLL;
	// . the info of the weakest entry in the top winners
	// . if its is full and we get another winner, the weakest will be
	//   replaced by the new winner
	//unsigned char  minTopBitScore  = 0 ;
	//score_t        minTopScore     = 0 ;
	//char          *minTopDocIdPtr  = (char *)&tmpHi;

	// . how many slots in the in-mem-cache hash table... up to 10000
	// . use the remainder of the L1 cache to hold this hash table
	int32_t availSlots = (L1_DATA_CACHE_SIZE - totalMem) / 10;
	// make a power of 2 for easy hashing (avoids % operator)
	int32_t numSlots = getHighestLitBitValue ( availSlots );
	// don't go below this min even if we must leave the cache
	if ( numSlots < 1024 ) numSlots = 1024;
	// damn,now we have to keep this fixed for rat because hashTmpDocIds2()
	// needs to have numSlots consistent for all calls to addLists2_r()
	// because m_tmpDocIdPtrs2 needs to hash full blocks. blocks are
	// separated by maxDocId values, which are stored as docId ptrs in
	// m_tmpDocIdPtrs2[], and indicated by having a score of 0. so if we
	// stored one block into m_tmpDocIdPtrs2[] when numSlots was 2048 that
	// block might not hash into a 1024 slot table...
	if ( rat ) numSlots = RAT_SLOTS;
	// sanity check
	if ( numSlots > 10000 ) { 
		log(LOG_LOGIC,"query: bad numSlots.");
		char *xx = NULL; *xx = 0; }
	// . sort by date should try to maximize maxDocId and the size of the
	//   hash table in order to prevent rescanning the termlists
	// . no, not now, we split the lists to prevent rescanning...
	/*
	if ( sortByDate ) {
		numSlots = m_bigBufSize / (4+4+2+sizeof(qvec_t));
		// make a power of 2 for easy hashing (avoids % operator)
		numSlots = getHighestLitBitValue ( numSlots );
		// don't go below this min even if we must leave the cache
		if ( numSlots < 1024 ) { 
			log(LOG_LOGIC,"query: bad numSlots2.");
			char *xx = NULL; *xx = 0; }
		// point to the buffer space
		pp = m_bigBuf;
	}
	*/

	// . this is the hash table
	// . it is small cuz we are trying to stuff it all into the L1 cache
	// . it could be as big as 128k, so reserve enough to take advantage
	// . some of the cache will be used for scoreTables[], ptrs[] etc.
	// . i tried using a struct that holds these 3 but it made everything
	//   about 10% slower
	// . now this buffer is more dynamic, instead of a int32_t per score
	//   we will store a byte per query term to store the original score
	//   and compute the final score later
	char     *pp           = m_localBuf;
	char    **docIdPtrs    = (char   **)pp; pp += numSlots*4;
	uint8_t  *scoresVec    = (uint8_t *)pp; pp += numSlots*nqt;
	int16_t    *hardCounts   = (int16_t   *)pp; pp += numSlots*2;
	qvec_t   *explicitBits = (qvec_t  *)pp; pp += numSlots*sizeof(qvec_t);
	uint32_t *dateVec      = NULL;
	uint32_t *latVec       = NULL;
	uint32_t *lonVec       = NULL;
	//uint32_t *timeVec      = NULL; // time startpoints termlist
	//uint32_t *endVec       = NULL; // time endpoints   termlist
	uint8_t  *eventIds     = NULL;

	// each docid has a "date" now
	if ( m_sortByDate ) {
		dateVec = (uint32_t *)pp; pp += numSlots*4; }

	if ( m_sortBy == SORTBY_DIST ) {
		latVec = (uint32_t *)pp; pp += 4*numSlots; 
		lonVec = (uint32_t *)pp; pp += 4*numSlots; 
	}
	//if ( m_sortBy == SORTBY_TIME ) {
	//	timeVec = (uint32_t *)pp; pp += numSlots*4; 
	//	endVec  = (uint32_t *)pp; pp += numSlots*4; 
	//}

	// each event id is one byte
	if ( m_searchingEvents ) {eventIds = (uint8_t *)pp; pp += numSlots; }

	// sanity check
	if ( pp > m_buf + m_bufSize ) { char *xx = NULL; *xx = 0; }
	//if ( pp > m_bitScoresBuf    ) { char *xx = NULL; *xx = 0; }
	
	// for hashing we need a mask to use instead of the % operator
	uint32_t mask = (uint32_t)numSlots - 1;

	// empty all buckets in the hash table
	for ( int32_t i = 0 ; i < numSlots ; i++ ) docIdPtrs[i] = NULL;

	// . use numSlots to get first docid upper bound
	// . this is just the top 4 bytes of docids allowed to hash...
	// . since hash table is so small we can only hash docids below
	//   "maxDocId"
	// . therefore, we have to do several iterations
	// . i found a slight increase in speed chaning to 8/20 instead of 
	//   1/2 on the 'the .. sex' test now commented out in Msg39.cpp
	// . if rat (require all terms) is true we only have two lists and
	//   only the smallest one (first one) gets hashed..
	uint32_t step = numSlots * 8 / 20 ; // >> 1 ;
	uint32_t dd   = numDocIds ;
	if ( dd <= 0 ) dd = 1;
	// max it out if numDocIds is smaller than numSlots/2
	if ( dd <= step ) step = 0xffffffff;
	else step *= ((uint32_t)0xffffffff / dd) ;
	// hash all docids in (lastMaxDocId,maxDocId] into hash table
	uint32_t maxDocId = step;
	// we overwrite m_tmpDocIdPtrs2/m_tmpScoresVec2
	int32_t newTmpDocIds2 = 0;
	// these two guys save us on memory
	int32_t lastGuy2 = -10000;
	int32_t lastGuy  = -10000;
	// save the last maxDocId in case we have to rollback for a panic
	uint32_t lastMaxDocId = 0;
	// used for hashing
	int32_t    nn             = 0;
	int32_t    nnstart;
	// these vars are specific to each list
	char    sign           = 0;
	//int32_t   *scoreTablePtr1 = NULL;
	//int32_t   *scoreTablePtr2 = NULL;
	qvec_t  ebits          = 0;
	char   *listEnd        = NULL;
	bool    hashIt         = true;
	char    hc             = 0;
	// if maxDocId is too we panic (hash table gets full) and we big
	// step down by this much / 2
	uint32_t downStep = step;
	// used to check for overflow when incrementing maxDocId
	uint32_t oldDocId ;
	// a flag used for logging the number of panics
	int32_t          printed = -1;

	int32_t weakest = -1;
	//int32_t numIlscRedos = 0;

	static char s_flag = 0;

	//int32_t     tn ;
	//TopNode *t  ;

	// save # of ptrs for stats
	m_numPtrs = numPtrs;
	// count # loops we do, for stats purposes
	m_numLoops = 0;
	// save for debug
	m_numSlots = numSlots;

	/////////////////////////////////////////////////
	// begin the hashing loop
	// this is where we need SUPER SPEED!!!!
	/////////////////////////////////////////////////

	// . should we hash topDocIds2? only if we got some
	// . numBaseLists is how many lists are in block #0, but this is 0
	//   if we are NOT hashing block #0 at this time.
	// . does not apply to rat=0 since we hash all termlists at once
	bool hashThem = ( rat && m_tmpDocIdPtrs2 && numBaseLists == 0 );

	// these vars help us change the list-specific vars
	int16_t point ;
	int16_t next  ;
	int16_t listi ;

 top:

	// this is just for stats reporting
	m_numLoops++;

	// these vars help us change the list-specific vars
	point = 0;

	next  = 0;
	listi = 0;

	// this kinda replaces zeroOutVectorComponents()
	//memset(scoresVec,255,numSlots*nqt);

	// . if this is a successive rat call, m_tmpDocIdPtrs2[] points to the
	//   "active intersection" and should be hashed
	// . and we also hash it with the one list/blockOfLists supplied to us
	// . this is good when rat is 0 now because we're adding the phrases
	//   so it's like partial rat going on
	// . if m_numTmpDocIds2 is 1, that is just the stored maxDocId, it
	//   is a "fake" docid and means we are really empty..
	// . this will dictate "maxDocId" for the hashing the provided block
	//   of lists.
	if ( hashThem && m_numTmpDocIds2 > 0 )
		hashTmpDocIds2 ( &maxDocId    ,
				 docIdPtrs    ,
				 scoresVec    ,
				 eventIds     ,
				 latVec       ,
				 lonVec       ,
				 //timeVec      ,
				 //endVec       ,
				 explicitBits ,
				 hardCounts   ,
				 mask         ,
				 numSlots     ,
				 nqt          );


	if ( m_searchingEvents ) 
		// returns false if it paniced
		if ( ! eventHashLoop ( listIndexes ,
				       listSigns   ,
				       listExplicitBits ,
				       listEnds ,
				       listHash ,
				       listHardCount ,
				       
				       listPoints,
				       ptrs,
				       ptrEnds,
				       oldptrs,
				       numPtrs ,
				       
				       maxDocId ,
				       numListsToDo,
				       numSlots ,
				       
				       docIdPtrs ,
				       explicitBits,
				       hardCounts  ,
				       eventIds    ,
				       scoresVec   ,
				       latVec,
				       lonVec,
				       //timeVec,
				       //endVec,
				       nqt ) )
			goto panic;
			

	// loop over the ptrs to the SUBlists
	for ( register int32_t i = 0 ; i < numPtrs ; i++ ) { 
		// when i reaches this break point we've switched lists
		if ( i == point ) {

			// do not do this for loop since we called
			// eventHashLoop() above
			if ( m_searchingEvents ) break;

			listi         = listIndexes        [ next ];
			//logf(LOG_DEBUG,"query: imap[%"INT32"]=%"INT32"",
			//	       listi, imap[listi]);
			sign          = listSigns          [ next ];
			//scoreTablePtr = listScoreTablePtrs [ next ];
			ebits         = listExplicitBits   [ next ];
			listEnd       = listEnds           [ next ];
			hashIt        = listHash           [ next ];
			hc            = listHardCount      [ next ];
			//scoreTablePtr = scoreTable         [ listi ];
			// if that was the last list, then we'll exit the
			// loop by i hitting numPtrs, so don't worry
			next++;
			if ( next < numListsToDo ) point = listPoints [ next ];
			else                       point = -1;
		}
		// skip if emptied (end of that list fragment)
		if ( ! ptrs[i] ) continue;
	addMore:
		// if the top 4 bytes of his docid is > maxDocId, 
		// then skip to next ptr/sublist
		if ( *(uint32_t *)(ptrs[i]+1) > maxDocId ) continue;
		// otherwise, hash him, use the top 32 bits of his docid
		nn = (*(uint32_t *)(ptrs[i]+1) )& mask ;
		// removing the mix table reduces time by about 10%
		//^ (s_mixtab[ptrs[i][2]])) & mask;
		// save start position so we can see if we chain too much
		nnstart = nn;
		// the score position is nn * numTermsPerList + [list index]
		// debug point
		/*
		int64_t ddd ;
		gbmemcpy ( &ddd , ptrs[i] , 6 );
		ddd >>= 2;
		ddd &= DOCID_MASK;
		if ( ddd == 7590103015LL )
			log("got it");
		*/
		/*
		if ( ddd == 
			unsigned char ss = (unsigned char)ptrs[i][5];
			int32_t sss = scoreTablePtr[ss];
			logf(LOG_DEBUG,"i=%"INT32" max=%"UINT64" sc=%hhu sc2=%"UINT32" d=%"UINT64"",
			    (int32_t)i,
			    (int64_t)(((int64_t)maxDocId)<<6) | 0x3fLL, 
			     255-ss, 
			     sss,
			    (int64_t)ddd );
		}
		*/

	chain:
		// . if the hashtable slot is empty (docIdPtrs[nn] is NULL),
		//   then take right away
		// . this is the most common case so we put it first
		if ( docIdPtrs[nn] == NULL ) {
			// . did we miss it?
			// . if rat is true, then advance to next right now
			if ( ! hashIt ) {
				if ( ! m_useDateLists)goto advance;
				else if(m_sortByDate) goto dateAdvance1;
				else                  goto dateAdvance2;
			}
			// hold ptr to our stuff
			docIdPtrs    [ nn ] = ptrs[i];
			// set pure bits
			explicitBits [ nn ] = ebits;
			hardCounts   [ nn ] = hc;
			// if we're not exclusive or date search, do it quick
			if ( sign=='\0' || sign=='*' || sign=='+' || sign=='-')
				scoresVec[nn*nqt+listi] = (uint8_t)ptrs[i][5];
			// date contstrain (still read from datedb)
			else if (sign == 'e') {
				// there is no score based on date in this case
				// but we are still getting the termlists from
				// datedb, we just use the regular score that
				// is contained in the datedb termlist, after
				// the date itself.
				scoresVec[nn*nqt+listi] = (uint8_t)ptrs[i][5];
				// sanity check (MDW)
				//if ( scoresVec[nn*nqt+listi] == 0 ) {
				//     char *xx = NULL; *xx = 0; }
				// replicate the code below for speed
			dateAdvance2:
				ptrs[i] += 10;
				if (ptrs[i] < ptrEnds[i] ) goto addMore;
				oldptrs[i] = ptrs[i];
				ptrs   [i] = NULL;
				continue;	
			}
			// . sort by date
			// . actually a hybrid sort between date and score
			else {
				// sanity check
				if ( sign != 'd' ) { char *xx=NULL; *xx=0; }
				// store the regular score too
				scoresVec[nn*nqt+listi] = (uint8_t)ptrs[i][5];
				// sanity check (MDW)
				//if ( scoresVec[nn*nqt+listi] == 0 ) {
				//	char *xx = NULL; *xx = 0; }
				// we must store the date since it is also 
				// used to determine the final score
				dateVec  [nn] = ~*(uint32_t *)(&ptrs[i][6]);
				// replicate the code below for speed
			dateAdvance1:
				ptrs[i] += 10;
				if (ptrs[i] < ptrEnds[i] ) goto addMore;
				oldptrs[i] = ptrs[i];
				ptrs   [i] = NULL;
				continue;
			}
		advance:
			// debug msg
			//log("added score=%05"INT32" totalscore=%05"INT32" to "
			//    "slotnum=%04"INT32" ptrList=%"INT32"",
			//    scoreTablePtr[((unsigned char)ptrs[i][5])],
			//    scoresVec[nn],nn,i);
			// advance ptr to point to next score/docid 6 bytes
			ptrs[i] += 6;
			// if he points to his lists end or a different score
			// then remove him from the list of ptrs
			if ( ptrs[i] >= listEnd || ptrs[i][5] != ptrs[i][-1] ){
				// save him in case we have to roll back
				oldptrs[i] = ptrs[i];
				// signify this sublist's demise
				ptrs[i] = NULL;
				continue;
			}
			// otherwise, try to add more from this ptr list
			goto addMore;
		}
		// if docIds bits don't match, chain to next bucket
		if ( *(int32_t *)(ptrs[i]+1) != *(int32_t *)(docIdPtrs[nn]+1) ||
		     (*ptrs[i] & 0xfd) != (*docIdPtrs[nn] & 0xfd) ) {
			if ( ++nn >= numSlots ) nn = 0;
			// if we wrapped back, table is FULL!! PANIC!
			if ( nn == nnstart ) goto panic;
			// count the number of times we miss
			m_numCollisions++;
			goto chain;
		}
		// got dup docid for the same termid due to index corruption?
		if ( explicitBits[nn] & ebits ) {
			// no point in logging since in thread!
			//int64_t dd ;
			//gbmemcpy ( &dd , ptrs[i] , 6 );
			//dd >>= 2;
			//dd &= DOCID_MASK;
			//fprintf(stderr,"got dup score for docid=%"INT64"\n",dd);
			if      ( ! m_useDateLists ) goto advance;
			else if (   m_sortByDate   ) goto dateAdvance1;
			else                         goto dateAdvance2;
		}
		// if docIdBits match OR in the pure bits
		explicitBits[nn]  |= ebits;
		// . else if we are using hardCounts for *many* query terms...
		// . terms not explicitly required will still have ebits > 0
		//   in order to support boolean expressions along side of
		//   hard required terms
		// . non-required phrases can all share the same ebit when we
		//   have a lot of query terms, so they will not be
		//   able to imply hard-required, single-word terms but at 
		//   least they can add to scores[]. they have ebits of 0.
		// . an ebits of 0x80000000 means it's a hard required term
		if ( hc ) hardCounts[nn]++;
		// always store in scores vec
		scoresVec[nn*nqt+listi] =(uint8_t)ptrs[i][5];
		// sanity check (MDW)
		//if ( scoresVec[nn*nqt+listi] == 0 ) {
		//	char *xx = NULL; *xx = 0; }
		// get next docid from sublist and hash that, too!
		if ( sign == '\0' || sign == '+' || sign == '*' ||sign == '-')
			goto advance;
		// 'e' means not sorting by date, just using datedb lists
		if ( sign == 'e' ) 
			goto dateAdvance2;
		// we are sorting by date, store this
		dateVec  [nn] = ~*(uint32_t *)(&ptrs[i][6]);
		// we got a 'd'
		goto dateAdvance1;
	}

	// . ok, we come here when we have hashed all the docids from the
	//   range given by the old maxDocId up to the current maxDocId
	// . now get the winners
	// . for rat=1 the winners must have the "required" bits
	// . the "required" bits match the query terms that are required
	// . for rat=0 we throw everyone into m_tmpDocIdPtrs2[] until we have
	//   NORAT_TMP_SLOTS of them, at which point we set our affinityWeights
	//   so we can begin formulating a score from every docid going forward
	//   and therefore, can just store the top-scoring "docWanted" docids
	//   from that point forward.
	goto getWinners;

	// we "panic" if the hash table got filled up and we have to cut
	// the docid range in half and re-try
 panic:
	// . keep cutting the maxDocId in half until all docids below
	//   it fit into our little hash table
	// . TODO: scan hash table to find a good maxDocId/downstep?
	downStep = maxDocId - lastMaxDocId;
	// and then half it for good measure
	downStep >>= 1;
	// sanity check
	if ( lastMaxDocId >= maxDocId ) { char *xx = NULL; *xx = 0; }
	// sanity test, log if not sane
	//log(LOG_LOGIC,"query: last=%"UINT32" downstep=%"UINT32" max=%"UINT32"",
	//    lastMaxDocId,downStep,maxDocId);
	// count
	m_numPanics++;
	// . if it is zero we're fucked! how can this happen really?
	// . very very rare
	// . TODO: look into this more
	if ( downStep == 0 || downStep >= maxDocId || m_numPanics > 100) {
		log(LOG_LOGIC,"query: indextable: Major panic. "
		    "downstep=%"UINT32" maxDocId=%"UINT32" numPanics=%"INT32"",
		    downStep,maxDocId,m_numPanics);
		goto done;
	}
	// why is this still maxed out after a panic?
	if ( maxDocId == 0xffffffff && m_numPanics > 1 ) {
		log(LOG_LOGIC,"query: indextable: Logic panic. "
		    "downstep=%"UINT32" maxDocId=%"UINT32" numPanics=%"INT32"",
		    downStep,maxDocId,m_numPanics);
		goto done;
	}
	// decrease docid ceiling by half a step each time this is called
	maxDocId -= downStep ;
	// clear the hash table
	memset ( docIdPtrs , 0 , numSlots * 4 );
	// roll back m_nexti so hashTmpDocIds2() works again
	m_nexti = m_oldnexti;
	// . now move each ptrs[i] backwards if we need to
	// . if the docid is in this hash table that panicked,move it backwards
	j = 0;
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {
		char *p = ptrs[i];
		// was it emptied? if so we just negate the ptr and go back 6
		if ( ! p ) {
			// get last value
			p = oldptrs[i];
			// lists that were empty to begin with have NULL
			// oldptrs[i]
			if ( ! p ) continue;
			// back him up to the last docid for this score
			p -= hks;
			// if hashed int32_t ago, continue
			if ( *(uint32_t *)(p+1) <= lastMaxDocId )
				continue;
			// for logging stats, inc j
			j++;
		}
		// get his score
	        //unsigned char score = (unsigned char)(p[5]);
		// was previous guy in this hash table? if so, rollback
		while ( p - hks >= pstarts[i] &&
			*(uint32_t *)((p-hks)+1) > lastMaxDocId ) {
			// backup a key
			p -= hks;
			// for logging stats, inc j
			j++;
		}
		// sanity check, does not apply to date lsits
		//if(! m_useDateLists && (unsigned char)(p-hks)[5] != score ) {
		//	char *xx = NULL ; *xx = 0; }
		// re-assign the rolled back value so we can try to rehash
		// all docds in the new range: (lastMaxDocId,newMaxDocId]
		ptrs[i] = p;
	}
	if ( m_isDebug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: indextable: Rolled back over "
		     "%"INT32" docids.",j);
	// sanity check, no, you can hash more if docids intersect with
	// existing docids in hash table. then one slot gives multiple
	// ptr rollbacks.
	//if ( j != numSlots ) { char *xx = NULL; *xx = 0; }
	// try to fit all docids into it from the beginning
	goto top;


 getWinners:

	uint8_t bscore;

	/////////////////////////////////////
	// scrape off any winners
	/////////////////////////////////////

	if ( m_isDebug && s_flag == 0 ) {
		s_flag = 1;
		logf(LOG_DEBUG,"query: "
		     "requiredBits=0x%08"XINT32" "
		     "negativeBits=0x%08"XINT32" "
		     "forcedBits=0x%08"XINT32" "
		     "minHardCount=0%"INT32" "
		     "useYes=%"INT32" "
		     "useNo=%"INT32"",
		     (int32_t)requiredBits,
		     (int32_t)negativeBits,
		     (int32_t)forcedBits,
		     (int32_t)minHardCount,
		     (int32_t)m_useYesDocIdTable,
		     (int32_t)m_useNoDocIdTable );
	}

	// sanity checks
	if ( m_useYesDocIdTable && m_r && m_r->size_sqDocIds <= 1 ) {
		char *xx=NULL;*xx=0; }
	if ( m_useNoDocIdTable  && m_r && m_r->size_noDocIds <= 1 ) {
		char *xx=NULL;*xx=0; }

	for ( int32_t i = 0 ; i < numSlots ; i++ ) {
		// skip empty slots
		if ( docIdPtrs[i] == NULL ) continue;
		// get implied bits from explicit bits
		qvec_t ibits = m_q->getImplicits ( explicitBits[i] );
		// log each one!
		if ( m_isDebug ) {
			int64_t d = getDocIdFromPtr(docIdPtrs[i]);
			if ( d == 17601280831LL ) 
				log("you");
			log("query: checking docid %"INT64" "
			    "ibits=0x%08"XINT32"",d,(int32_t)ibits);
		}
		// add it right away to m_tmpDocIdPtrs2[] if we're rat
		if ( rat ) {
			// . we need to have the required term count to make it
			// . this is increased by one with each call to
			//   addLists_r() and start with a value of 1 or 2
			if ( (ibits & requiredBits) != requiredBits ) {
				docIdPtrs[i] = NULL;
				continue;
			}
			// . skip if it contains a negative term
			// . like dog in 'cat -dog'
			if ( negativeBits && (ibits & negativeBits) ) {
				docIdPtrs[i] = NULL;
				continue;
			}
			// must have all the terms preceeded by a '+' sign
			if ( forcedBits && (ibits & forcedBits) != forcedBits){
				docIdPtrs[i] = NULL;
				continue;
			}
			// we now also check the hard count
			if ( hardCounts[i] < minHardCount ) {
				docIdPtrs[i] = NULL;
				continue;
			}
			// skip if if not in the sqDocIds
			if ( m_useYesDocIdTable ) {
				// make a docid out of it
				int64_t d = getDocIdFromPtr(docIdPtrs[i]);
				// if not in table, skip it
				if ( m_dt.getSlot(d) < 0 ) {
					docIdPtrs[i] = NULL; continue; }
			}
			if ( m_useNoDocIdTable ) {
				// make a docid out of it
				int64_t d = getDocIdFromPtr(docIdPtrs[i]);
				// if in this table skip it
				if ( m_et.getSlot(d) >= 0 ) {
					docIdPtrs[i] = NULL; continue; }
			}
			// sanity check
			if ( newTmpDocIds2 >= m_maxTmpDocIds2 ) {
				log(LOG_LOGIC,"query: bad newTmpDocIds2.");
				char *xx = NULL; *xx = 0;
			}
			// . store in the buffer
			// . we now save ebits because a phrase in the base
			//   block may imply a term we add later on, but do
			//   not have explicitly. this way we still get it.
			m_tmpDocIdPtrs2 [ newTmpDocIds2 ] = docIdPtrs [i];
			// some of the phrase term vector components may
			// be non-zero when they should be zero! fix this
			// below when computing the final winners in done:.
			gbmemcpy ( &m_tmpScoresVec2[nqt*newTmpDocIds2] ,
				 &scoresVec      [nqt*i            ] , 
				 nqt                                 );
			// like we have a score vector, one score per query 
			// term, we have an event id bit vector, where a bit
			// is on if the term corresponds to that event id
			if ( m_searchingEvents ) {
				// always have event ids
				m_tmpEventIds2[newTmpDocIds2] = eventIds[i];
				// lat/lon?
				if ( latVec ) {
					m_tmpLatVec2[newTmpDocIds2]=latVec[i];
					m_tmpLonVec2[newTmpDocIds2]=lonVec[i];
				}
				// since we may not be intersecting all the
				// termlists this round, only store these if
				// we had one
				//if ( timeVec ) //&& timeVec[i] )
				//     m_tmpTimeVec2[newTmpDocIds2]=timeVec[i];
				//if ( endVec ) // && endVec[i] )
				//	m_tmpEndVec2[newTmpDocIds2]=endVec[i];
			}

			// store the dates!
			if ( dateVec )
				m_tmpDateVec2[newTmpDocIds2] = dateVec[i];
			// each query term has a different "explicit bit" which
			// is set in this vector. save the vector.
			m_tmpEbitVec2    [ newTmpDocIds2 ] = explicitBits[i];
			m_tmpHardCounts2 [ newTmpDocIds2 ] = hardCounts  [i];
			newTmpDocIds2++;
			// clear this slot in the hash tbale
			docIdPtrs[i] = NULL;
			continue;
		}

		// TODO: we could support boolean queries with int32_t 
		//       sequences of ORs by making all the ORs to one bit!


		// 
		//
		// AT THIS POINT WE ARE NOT RAT
		//
		//

		// . skip if it contains a negative term
		// . like dog in 'cat -dog'
		if ( negativeBits && (ibits & negativeBits) ) {
			docIdPtrs[i] = NULL;
			continue;
		}
		// must have all hard counts, too (works for boolean queries?)
		if ( hardCounts[i] < minHardCount ) {
			docIdPtrs[i] = NULL;
			continue;
		}
		// must have all the terms preceeded by a '+' sign
		if ( forcedBits && (ibits & forcedBits) != forcedBits ) {
			docIdPtrs[i] = NULL;
			continue;
		}
		// . count EXplicit matches
		// . bit #6 is on iff he has all "required terms" EXplicitly
		// . "required terms" just means all the terms that we would
		//   need to have if we were a default AND engine
		// . now he must have all phrase bits, too, where
		//   phrases start and end with non-ignored single words
		//if ( bscore & 0x40 ) 
		//	explicitCount++;
		// . count IMplicit matches
		// . bit #5 is on if he has all requird terms IMplicitly
		// . if the doc has the phrase "A B" then he has term A and 
		//   term B IMplicitly
		//if ( bscore & 0x20 ) 
		//	implicitCount++;
		// . don't let explicits always sort above the implicits
		// . but guys with all required terms should always sort
		//   above guys without, so we DO NOT clear 0x20 (bit #5)
		// . might as well clear bit #7, too, it should always be set
		// . REMOVE THIS line if you want any doc that has
		//   all the terms EXplicitly to always outrank any 
		//   doc that only has them IMplicitly
		// . 0xc0 = 0x80 + 0x40  and the ~0xc0 = 0x3f
		// . no, we have to preserve this!
		//bscore &= ~0xc0;
		// add in our hard count (for boolean queries)
		//bscore += hardCounts[i];

		/*
		if ( weakest >= 0 ) 
			log("bscore=%04"UINT32" score=%04"INT32" topp+1=%08"XINT32" "
			"replacing "
			     "#%02"INT32" bscore=%04"UINT32" score=%04"INT32" topp+1=%08"XINT32"",
			     bscore,scores[i],*(int32_t *)(docIdPtrs[i]+1),
			     j,topb[j],tops[j],*(int32_t *)(topp[j]+1));
		else
		       log("bscore=%04"UINT32" score=%04"INT32" topp+1=%08"XINT32" adding #%"INT32"",
			   bscore,scores[i],*(int32_t *)(docIdPtrs[i]+1),j);
		*/

		//uint8_t bscore;

		// . if we are boolean, and bscore is 0, filter it out, that
		//   means it does not satisfy the truth table
		// . this was causing a ton of panics!
		if ( m_q->m_isBoolean ) {
			//if (m_isDebug)
			//	logf(LOG_DEBUG, 
			//	     "query: getBitScore1 "
			//	     "queryId=%lld bits=0x%016"XINT64"",
			//	     m_r->m_queryId,
			//	     (int64_t) explicitBits[i]);
			uint8_t bscore = getBitScore(explicitBits[i]);
				
			// if we are boolean we must have the right bscore
			if ( bscore == 0 ) {
				docIdPtrs[i] = NULL;
				continue;
			}
		}
		/*
		else if ( weakest != -1 ) {
		*/

		/*
		if ( weakest != -1 ) {
			bscore = m_q->getBitScore(explicitBits[i]);
			// add in hard counts
			bscore += hardCounts[i];
			// remove 0x80 | 0x40 so we do not sort explicit
			// exact matches above implicit exact matches
			//bscore &= ~0xc0;
		}

		// . add top tree if it is NON-NULL
		// . only do this after we've computed the affinities because
		//   i do not have score vectors in TopNode, just m_score.
		// . this is rat=0, so at some point we compute the affinities
		//   and we keep doing more intersections... after we compute
		//   the affinities weakest is NOT -1.
		// . we can only call getWeightedScore() after weakest!=-1
		//   because we do not have the affinities necessary to compute
		//   the affinity weights required for computing the score.
		if ( weakest != -1 ) { // && m_topTree ) {
			// . WE GOT A WINNER, ADD IT TO THE TREE
			// . bscore's bit #6 (0x40) is on if he has all terms
			//   explicitly, use this to disallow a high-tiered
			//   node from topping a node that has bit #6 on
			tn = m_topTree->getEmptyNode();
			// get the empty TopNode ptr
			t  = &m_topTree->m_nodes[tn];
			// fill it up
			t->m_bscore = bscore;
			// if sorting by date, score is just the date
			if ( m_sortByDate )
				t->m_score= dateVec[i];
			else 
				t->m_score=getWeightedScore(&scoresVec[nqt*i],
							    nqt              ,
							    m_freqWeights    ,
							    m_affWeights     ,
							    m_requireAllTerms);
			// store ptr to docid in TopNode
			//t->m_docIdPtr = docIdPtrs[i];
			t->m_docId = getDocIdFromPtr(docIdPtrs[i]);
			// . nodes that have that all the terms explicitly
			//   cannot be passed by higher-tiered nodes, 
			//   otherwise they are game
			// . de-duping is done at display time
			// . this will not add if tree is full and it is
			//   less than the m_lowNode in score
			// . if it does get added to a full tree, lowNode will
			//   be removed
			// dummy variables
			//bool removedNode = false;
			//char *removedDocIdPtr = NULL;
			m_topTree->addNode(t,tn);//,&removedNode,&removedDocId
			// REMEMBER TO clear the slot
			docIdPtrs[i] = NULL;
			// get the next docid from the hash table
			continue;
		}
		*/

		// . store it in m_tmpDocIdPtrs2[], etc.
		// . m_topDocIds2[] is now used to hold the first 
		//   "NORAT_TMP_SLOTS" rat=0 results that have any of the 
		//   query terms...
		// . the sample, NORAT_TMP_SLOTS should be pretty big since we
		//   are not guaranteed that all results have all required 
		//   query terms  since it is rat=0
		// . now get the next empty bucket and advance ptr
		j = newTmpDocIds2++;
		// store the ptr to the docid
		m_tmpDocIdPtrs2[j] = docIdPtrs[i];
		// store the score vector
		gbmemcpy ( &m_tmpScoresVec2[j * nqt], &scoresVec[nqt*i], nqt);
		// store this too
		if ( m_searchingEvents ) {
			m_tmpEventIds2[j] = eventIds[i];
			if ( latVec  ) m_tmpLatVec2 [j] = latVec[i];
			if ( lonVec  ) m_tmpLonVec2 [j] = lonVec[i];
			//if ( timeVec ) m_tmpTimeVec2[j] = timeVec[i];
			//if ( endVec  ) m_tmpEndVec2 [j] = endVec[i];
		}
		// store date if sorting by date
		if ( m_sortByDate )
			m_tmpDateVec2[j] = dateVec[i];
		// save the explicit bits vector
		m_tmpEbitVec2    [j] = explicitBits[i];
		m_tmpHardCounts2 [j] = hardCounts  [i];
		// clear the slot
		docIdPtrs[i] = NULL;

		// fill it up!!!
		continue;

		/*
		// . keep adding to m_tmpDocIdPtrs2[] until filled up!
		// . only then will we try to compute the affinity weights
		if ( newTmpDocIds2 < NORAT_TMP_SLOTS ) continue;
		// we have to make sure to fill up m_topDocIds[], etc., so
		// that getWeakest() works right
		if ( newTmpDocIds2 < m_docsWanted    ) continue;


		//
		// . NOW WE HAVE ENOUGH DOCIDS FOR A GOOD SAMPLE
		// . COMPUTE THE AFFINITY WEIGHTS FOR RAT=0
		//

		// zero out the vector components that do not 
		// exist according to the explicitBits[] array.
		zeroOutVectorComponents ( m_tmpScoresVec2  , 
					  m_tmpEbitVec2    ,
					  m_tmpHardCounts2 ,
					  newTmpDocIds2    ,
					  rat              );
		// if we haven't computed our term weights yet, do it with the
		// existing sample
		computeAffWeights  ( rat              ,
				     newTmpDocIds2    ,
				     m_tmpDocIdPtrs2  ,
				     m_tmpScoresVec2  ,
				     m_tmpEbitVec2    ,
				     m_tmpHardCounts2 ,
				     m_affWeights     ,
				     m_affinities     );
		// . we need to compute the scores of the current winners
		// . sets m_tmpScores2[] array
		//computeWeightedScores ( newTmpDocIds2   ,
		//			m_tmpScoresVec2 ,
		//			m_tmpEbitVec2   ,
		//			m_affWeights    ,
		//			m_tmpDocIdPtrs2 );
		// . now fill the top docids with the top of the big list
		// . put them into "topp,tops,topb,tope" which are the 
		//   m_topDocIds[] arrays
		// . only put at most "docsWanted" results into
		//   "topp,tops,topb,tope" arrays
		// . fill top tree if m_topTree (MDW)
		numTopDocIds = fillTopDocIds ( topp             ,
					       tops             ,
					       topb             ,
					       m_docsWanted     ,
					       m_tmpDocIdPtrs2  ,
					       m_tmpScoresVec2  ,
					       m_tmpDateVec2    ,
					       m_tmpEbitVec2    ,
					       m_tmpHardCounts2 ,
					       newTmpDocIds2    );
		// fake it out if using top tree
		if ( m_topTree ) {
			weakest = 999999;
			continue;
		}
		// get the next weakest so we discard any future results below 
		// minTopBitScore/minTopScore/...
		weakest = getWeakestTopDocId ( topp            ,
					       tops            ,
					       topb            ,
					       numTopDocIds    ,
					       &minTopBitScore ,
					       &minTopScore    ,
					       &minTopDocIdPtr );
		// sanity check
		if ( weakest == -1 ) { char *xx=NULL; *xx=0; }
		*/
	}


	//
	//
	// NOW PREPARE TO HASH THE NEXT RANGE OF DOCIDS
	//
	//

	// sanity check (MDW)
	//zeroOutVectorComponents ( m_tmpScoresVec2  , 
	//			  m_tmpEbitVec2    ,
	//			  m_tmpHardCounts2 ,
	//			  newTmpDocIds2    ,
	//			  rat              );


	// . sanity check
	// . "newTmpDocIds2" is the "active intersection" for rat=1
	// . "newTmpDocIds2" is the NORAT_TMP_SLOTS winner buffer for rat=0
	// . make sure we do not breach it
	if ( newTmpDocIds2 >= m_maxTmpDocIds2 ) {
		log(LOG_LOGIC,"query: bad newTmpDocIds2b.");
		char *xx = NULL; *xx = 0; 
	}

	// . if rat then store the maxDocId for re-hashing m_tmpDocIdPtrs2,
	//   the "active intersection", the next time addLists2_r() is called
	// . store the docids that were in the intersection above into
	//   m_tmpDocIdPtrs2, etc. so that when addLists2_r() is called again
	//   with another termlist(s) then we can hash this intersection
	//   into the hash table with the termlist(s) to get the next
	//   intersection. we try to hash the smallest termlists first in order
	//   to reduce the intersection as fast as possible, thereby improving
	//   performance. (m_imap[] is responsible for re-ordering the 
	//   termlists to improve intersection speed)
	// . this only applies to rat=1, because rat=0 intersects ALL termlists
	//   at the same time with a single call to addLists2_r()
	if ( rat ) {
		// . if 2nd last guy is within 50 of us then bury the last guy
		// . this guarantees all maxDocIds stored in m_tmpDocIdPtrs2
		//   are at least 50 units away from each other, with the
		//   exception of the last one added, of course, and this
		//   allows us to save memory (see IndexTable2::alloc() above)
		// . we are basically collapsing hash loops into a single
		//   "hash loop"
		// . these "maxDocIds" stored in m_tmpDocIdPtrs2[] are fake
		//   docids that dictate the docid ranges we will use the
		//   next time addLists2_r() is called with another termlist
		if ( lastGuy2 >= 0 && newTmpDocIds2 - lastGuy2 < 50 ) {
			// peel the guy off the end of the list
			newTmpDocIds2--;
			// . use him to bury "lastGuy"
			// . we should only be overwriting the "maxDocId" ptr
			//   that is stored in m_tmpDocIdPtrs2[lastGuy]
			//   (see below)
			// . we are essentially combining two separate lists
			//   into one, thereby, only requiring one hashing
			//   loop above instead of two
			m_tmpDocIdPtrs2 [ lastGuy ] = 
				m_tmpDocIdPtrs2 [ newTmpDocIds2 ];
			gbmemcpy(&m_tmpScoresVec2[lastGuy*nqt],
			       &m_tmpScoresVec2[newTmpDocIds2*nqt],
			       nqt);
			if ( m_searchingEvents ) {
				m_tmpEventIds2[lastGuy]=
					m_tmpEventIds2[newTmpDocIds2];
				if ( m_tmpLatVec2 ) 
					m_tmpLatVec2[lastGuy] =
						m_tmpLatVec2[newTmpDocIds2];
				if ( m_tmpLonVec2 ) 
					m_tmpLonVec2[lastGuy] =
						m_tmpLonVec2[newTmpDocIds2];
				//if ( m_tmpTimeVec2 ) 
				//	m_tmpTimeVec2[lastGuy] =
				//		m_tmpTimeVec2[newTmpDocIds2];
				//if ( m_tmpEndVec2 ) 
				//	m_tmpEndVec2[lastGuy] =
				//		m_tmpEndVec2[newTmpDocIds2];
			}
			m_tmpEbitVec2 [lastGuy]=m_tmpEbitVec2[newTmpDocIds2];
			m_tmpHardCounts2[ lastGuy ] =
				m_tmpHardCounts2[ newTmpDocIds2 ];
			// and replace him...
			lastGuy = lastGuy2;
		}
		//log("adding marker %"INT32"",newTmpDocIds2);
		// overwrite last entry if no actual docids were added since
		// then, this allows us to save memory and fixes the seg fault
		//if ( newTmpDocIds2 > 0 && 
		//     m_tmpScores2 [ newTmpDocIds2 - 1 ] == 0 )
		//	newTmpDocIds2--;
		// add the new maxDocId "marker" (fake docid)
		m_tmpDocIdPtrs2  [ newTmpDocIds2 ] = (char *)maxDocId;
		// indicate that he is a special (fake) docid
		m_tmpHardCounts2 [ newTmpDocIds2 ] = -1;
		//memset(&m_tmpScoresVec2[newTmpDocIds2*nqt], 0, nqt);
		newTmpDocIds2++;
		// update second last guy
		lastGuy2 = lastGuy ;
		lastGuy  = newTmpDocIds2 - 1 ;
	}		

	// sanity check (MDW)
	//zeroOutVectorComponents ( m_tmpScoresVec2  ,
	//			  m_tmpEbitVec2    ,
	//			  m_tmpHardCounts2 ,
	//			  newTmpDocIds2    ,
	//			  rat              );

	// . reset our step down function
	// . do not reset this if we've panicked more than 20 times
	// . globalspec's single term query termlist's docids were all
	//   essentially sequential thus requiring VERY small steps of like
	//   100 or something... it seemed to block forever.
        if ( m_numPanics < 5 ) downStep = step ;
        // log it the first time it happens
        else if ( m_numPanics >= 5 && printed != m_numPanics ) {
                printed = m_numPanics;
                // it should be impossible that downStep be 0 or small like
                // that because we have a lot of slots in the hash table
                //char *qq = "";
                //if ( m_q && m_q->m_orig && m_q->m_orig[0] ) qq = m_q->m_orig;
		// i am seeing a lot of 5 and 6 panics msgs, stop that...
		if ( m_numPanics >= 7 )
			log(LOG_INFO,"query: Got %"INT32" panics. Using small steps of "
			    "%"INT32"",m_numPanics,downStep);
                // set step to downStep, otherwise, maxDocId will not be
                // able to be decreased close to within lastMaxDocId because
                // it is only decremented by downStep above, but
                // incremented below by step below. i think this matters
                // more for date termlists cuz those use lastMaxDocId
                step = downStep;
        }
	// save it to check for wraps
	oldDocId = maxDocId;
	// if we were maxxed, we're done
	if ( maxDocId == (uint32_t)0xffffffff ) goto done;
	// save the last maxDocId in case we have to rollback for a panic
	lastMaxDocId = maxDocId;
	// now advance the ceiling
	maxDocId += step;
        // if wrapped, set to max
        if ( maxDocId <= oldDocId ) {
                maxDocId = (uint32_t)0xffffffff;
                // . if we panic after this, come down "half the distance to
                //   the goal line" and try to hash all the docIds in the
                //   range: (lastMaxDocId,maxDocId-downStep]
                // . downStep is divided by 2 (right shifted) in panic: above
                // . otherwise, downStep can be too big and mess us up!
                downStep = (maxDocId - lastMaxDocId);
        }
        // sanity check
        if ( maxDocId - (downStep / 2) <= lastMaxDocId+1 ) {
                log("query: Got infinite loop criteria.");
		maxDocId = 0xffffffff;
		goto done;
	}
	
	// . do another round
	// . TODO: start with bottom of ptrs to take advantage of cache more!
	goto top;


	//
	//
	// . COME HERE WHEN ALL DONE
	// . rat=1 will have m_tmpDocIdPtrs2[] all filled up and needing to
	//   be scored and ranked.
	// . rat=0 may or may not have m_tmpDocIdPtrs2[] filled up. if it
	//   does then it needs to be scored and ranked too! if it does not
	//   then its m_topDocIds[] are the valid winners!
	//
 done:

	// update the new count now
	m_numTmpDocIds2 = newTmpDocIds2;

	
	qvec_t timap[MAX_QUERY_TERMS];
	// map each query term to a qvec_t implied
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		// get query term explicit
		qvec_t ebit = m_q->m_qterms[i].m_explicitBit;
		// map it
		timap[i] = m_q->getImplicits ( ebit );
	}

	// . sanity check for our hacked parsing
	// . key bitmap from datedb.h:
	// . tttttttt tttttttt tttttttt tttttttt  t = termId (48bits)
	// . tttttttt tttttttt 00000000 00000000  
	//   iiiiiiii IIIIIIII ssssssss dddddddd  s = ~score, [i-I] = evid rang
	// . dddddddd dddddddd dddddddd dddddd0Z  d = docId (38 bits)
	int32_t date = (13<<8) | (66);
	date = ~date;
	key128_t dk = g_datedb.makeKey ( 0,date,33,9999,false);
	char *dkp = (char *)&dk;
	if ( dkp[7] != 13 || dkp[6] != 66 ) { char *xx=NULL;*xx=0; }

	//
	// removed event logic from here
	// 


	// compute the scores here if we need to
	if ( (  rat && lastRound ) ||
	     ( !rat && weakest == -1 ) ) {
		// . zero out the vector components that do not exist
		//   according to the explicitBits[] array. 
		// . we do not bother keeping components zeroed out in
		//   the hash table for performance reasons, so just do
		//   it here for the "winning" docids
		zeroOutVectorComponents ( m_tmpScoresVec2  , 
					  m_tmpEbitVec2    ,
					  m_tmpHardCounts2 ,
					  m_numTmpDocIds2  ,
					  rat              );
		// compute the affinities and weights
		computeAffWeights    ( rat              ,
				       m_numTmpDocIds2  ,
				       m_tmpDocIdPtrs2  ,
				       m_tmpScoresVec2  ,
				       m_tmpEbitVec2    ,
				       m_tmpHardCounts2 ,
				       m_affWeights     ,
				       m_affinities     );
		// . compute the score for each docid now
		// . sets m_tmpScores2[] array
		//computeWeightedScores ( m_numTmpDocIds2 ,
		//			m_tmpScoresVec2 ,
		//			m_tmpEbitVec2   ,
		//			m_affWeights    ,
		//			m_tmpDocIdPtrs2 );
		// . now fill the top docids with the top of the big list
		// . this will fill m_topTree if it is non-NULL
		// . now it always just fills top tree...
		numTopDocIds = fillTopDocIds ( //topp             ,
					       //tops             ,
					       //topb             ,
					       //tope             ,
					       m_docsWanted     ,
					       m_tmpDocIdPtrs2  ,
					       m_tmpScoresVec2  ,
					       m_tmpDateVec2    ,
					       m_tmpLatVec2     ,
					       m_tmpLonVec2     ,
					       //m_tmpTimeVec2    ,
					       //m_tmpEndVec2    ,
					       m_tmpEbitVec2    ,
					       m_tmpHardCounts2 ,
					       m_tmpEventIds2   ,
					       m_numTmpDocIds2  );
	}


	/////////////////////////////////////
	// sort the winners
	/////////////////////////////////////

	// get the counts of the winners
	//int32_t explicitCount = 0;
	//int32_t implicitCount = 0;
	//bool didSwap;

	// explicit matches
	m_numExactExplicitMatches = 0;
	// implicit matches
	m_numExactImplicitMatches = 0;

	// . look in the intersection for these i guess
	// . we save these values in the cache so ::recompute() has them
	//   set properly.
	for ( int32_t i = 0 ; i < m_numTmpDocIds2 ; i++ ) {
		//if (m_isDebug)
		//	logf(LOG_DEBUG, 
		//	     "query: getBitScore2 "
		//	     "queryId=%lld bits=0x%016"XINT64"",
		//	     m_r->m_queryId,
		//	     (int64_t) m_tmpEbitVec2[i]);
		// get the bit score, # terms implied
		bscore = getBitScore ( m_tmpEbitVec2[i] ) ;
		// count it if it has all terms EXplicitly
		if ( (bscore & 0x40) &&  m_tmpHardCounts2[i] >= minHardCount ) 
			m_numExactExplicitMatches++;
		// count hits with all terms implied
		if ( bscore &0x20 ) 
			m_numExactImplicitMatches++;
	}

	/*
	// . if we're rat and we've hashed all the termlists then let's 
	//   compute ex/implicitCount
	// . we need to store the explicit bits to compute the bscore!!
	// . if we're rat then we MUST have all terms in every doc...
	if ( m_topTree ) {
		// . set the number of exact matches
		// . TODO: loop through tree to fix this later!
		m_numExactExplicitMatches = 0; //m_topTree->m_explicits;
		// implicit matches
		m_numExactImplicitMatches = 0; //m_topTree->m_implicits;
		// . set our # of top docids member var
		m_numTopDocIds            = m_topTree->m_numUsedNodes;
		// . if we use top tree, TRY to estimate the hits now
		// . will only estimate if this is the first round
		goto doEstimation;
	}

	// get the counts
	for ( int32_t i = 0 ; i < numTopDocIds ; i++ ) {
		// get the bit score, # terms implied
		bscore = m_q->getBitScore ( m_tmpEbitVec2[i] ) ;
		// count it if it has all terms EXplicitly
		if ( (bscore & 0x40) &&  m_tmpHardCounts2[i] >= minHardCount ) 
			explicitCount++;
		// count hits with all terms implied
		if ( bscore &0x20 ) 
			implicitCount++;
	}
	
	// set the number of exact matches
	m_numExactExplicitMatches = explicitCount;
	// implicit matches
	m_numExactImplicitMatches = implicitCount;
	// set our # of top docids member var
	m_numTopDocIds            = numTopDocIds;
	*/

	/*
	if ( m_topTree ) goto doEstimation;

        // . now sort by m_topBitScores/m_topScores/m_topDocIdPtrs
        // . do a quick bubble sort
 keepSorting:
        // assume no swap will happen
	didSwap = false;
        for ( int32_t i = 1 ; i < numTopDocIds ; i++ ) {
                // continue if no switch needed
		if ( isBetterThanWeakest ( topb[i-1] ,
					   tops[i-1] ,
					   topp[i-1] ,
					   topb[i  ] ,
					   tops[i  ] ,
					   topp[i  ] ) )
			continue;
		// otherwise swap
                score_t        tmpScore    = tops [i-1];
                unsigned char  tmpBitScore = topb [i-1];
                char          *tmpDocIdPtr = topp [i-1];
                tops [i-1] = tops [i  ];
                topb [i-1] = topb [i  ];
                topp [i-1] = topp [i  ];
                tops [i  ] = tmpScore;
                topb [i  ] = tmpBitScore;
                topp [i  ] = tmpDocIdPtr;
                didSwap = true;
        }
        // if it's sorted then return
        if ( didSwap ) goto keepSorting;

 doEstimation:
	*/

	// . now extrapolate the total # of hits from what we got
	// . use default AND for this
	// . use this for boolean queries, too
	// . only set for the first call, so this number is consistent!!
	// . little graph: X's are matching terms, O's are unmatched
	//   and the hyphens separate stages. see the combinations.
	// . list #1      list #2
	// . X  ------->   X
	// . X  ------->   X
	// . X  ------->   X
	// . O             O
	// . O             O
	// . O             O
	// . ----------------
	// . O             O
	// . O             O
	// . O             O
	// . O
	// . O
	// . O
	// . we can re-compute if we didn't get many hits the first time!
	if ( m_estimatedTotalHits == -1 || m_doRecalc ) {
		// # of tried and untried combinations respectfully
		double   tried = 1.0;
		double untried = 1.0;
		double noretry = 1.0;
		// minimum term frequency of the eligible query terms
		int64_t mintf = 0x7fffffffffffffffLL;
		// . total hits we got now
		// . we use explicit, because we're only taking combinations
		//   of non-negative terms and positive phrase terms, using
		//   implicit matches would mess our count up
		// . furthermore, re-arranging query words would change the
		//   hit count because it would change the implicit count
		int32_t totalHits = m_numExactExplicitMatches;
		// . use combinatorics, NOT probability theory for this cuz
		//   we're quite discrete
		// . MOST of the error in this is from inaccurate term freqs
		//   because i think this logic is PERFECT!!!
		// . how many tuple combinations did we have?
		// . do not use imap here
		for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
			// component lists are merged into one compound list
			if ( m_componentCodes[i] >= 0 ) continue;
			// skip if negative or unsigned phrase
			QueryTerm *qt = &m_q->m_qterms[i];
			if ( qt->m_termSign == '-' ) continue;
			if ( qt->m_termSign =='\0' && qt->m_isPhrase) continue;
			// if its a boolean query under a NOT sign
			if ( qt->m_underNOT ) continue;
			// get current total size of list #i
			int32_t total = 0;
			//int32_t size = m_lists[i].getListSize();
			int32_t size = m_sizes[i];
			if ( size >= 12 ) size -= 6;
			size /= 6;
			total += size;

			// NSD: but for a one word query, totalHits should be
			// equal to the term Freq!
			if ( m_q->m_numTerms == 1 ) totalHits = m_termFreqs[i];

			// how many docs have this term?
			int64_t tf = m_termFreqs[i];
			// . multiply to get initial # of combinations of terms
			// . "tried" means we tried these combinations to 
			//   produce the "totalHits" search results
			tried *= (double)total;
			// get # of untried combinations
			untried *= (double) ( tf - totalHits );
			// get # of combinations we tried amongst STAGE0 guys
			// so we don't retry in untried
			noretry *= (double) ( total - totalHits );
			// count required terms
			//nn++;
			// if we only have one required term, nothing isuntried
			// record the min tf as a safety catch
			if ( tf < mintf ) mintf = tf;
		}
		// don't retry combos tried in "tried" count (intersection)
		untried -= noretry;
		// don't go negative... if tf == total
		if ( untried < 0.0 ) untried = 0.0;
		// dont divide by 0
		if ( tried < 1.0 ) tried = 1.0;
		// . out of "tried" combinations we got "totalHits" hits!
		// . what's the prob. a particular combination is a hit?
		double percent = (double)totalHits / (double)tried;
		// out of the untried combinations,how many hits can we expect?
		m_estimatedTotalHits = totalHits + 
			(int64_t) (untried * percent);
		// don't exceed the max tf of any one list (safety catch)
		if ( m_estimatedTotalHits > mintf ) 
			m_estimatedTotalHits = mintf;
		// make it at least what we got for sure
		if ( m_estimatedTotalHits < totalHits )
			m_estimatedTotalHits = totalHits;
		// if we get a re-call, replace the total hits calc
		if ( m_numExactExplicitMatches <= 0 ) m_doRecalc = true;
		else                                  m_doRecalc = false;
	}
	if (m_useBoolTable)
		log(LOG_DEBUG,"query: boolTable: queryId=%lld numSlotsUsed=%"INT32"",
		    m_r->m_queryId,m_bt.m_numSlotsUsed);
}

// hasheventloop eventloophash
bool IndexTable2::eventHashLoop ( int32_t *listIndexes ,
				  char *listSigns   ,
				  qvec_t *listExplicitBits ,
				  char **listEnds ,
				  char *listHash ,
				  char *listHardCount ,
				  int32_t *listPoints ,
				  char **ptrs  ,
				  char **ptrEnds ,
				  char **oldptrs ,
				  int32_t  numPtrs ,
				  int64_t maxDocId ,
				  int32_t      numListsToDo ,
				  int32_t      numSlots ,
				  char **docIdPtrs ,
				  qvec_t *explicitBits ,
				  int16_t *hardCounts ,
				  uint8_t *eventIds ,
				  uint8_t *scoresVec ,
				  uint32_t *latVec,
				  uint32_t *lonVec,
				  //uint32_t *timeVec,
				  //uint32_t *endVec,
				  int32_t     nqt ) {

	// these vars help us change the list-specific vars
	int16_t point = 0;
	int16_t next  = 0;
	int16_t listi = 0;

	uint32_t mask = (uint32_t)numSlots - 1;

	char    sign;
	qvec_t  ebits;
	char   *listEnd;
	char    hc;
	bool    hashIt;
	// before the for loop was wrapping "a" when "b" was 255 so make this
	// a int16_t
	uint16_t a,b;
	int32_t    nn;
	int32_t    nnstart;

	// loop over the ptrs to the SUBlists
	for ( register int32_t i = 0 ; i < numPtrs ; i++ ) { 
		// when i reaches this break point we've switched lists
		if ( i == point ) {
			listi         = listIndexes        [ next ];
			sign          = listSigns          [ next ];
			ebits         = listExplicitBits   [ next ];
			listEnd       = listEnds           [ next ];
			hashIt        = listHash           [ next ];
			hc            = listHardCount      [ next ];
			// if that was the last list, then we'll exit the
			// loop by i hitting numPtrs, so don't worry
			next++;
			if ( next < numListsToDo ) point = listPoints [ next ];
			else                       point = -1;
		}
		// skip if emptied (end of that list fragment)
		if ( ! ptrs[i] ) continue;

	addMore:
		// if the top 4 bytes of his docid is > maxDocId, 
		// then skip to next ptr/sublist
		if ( *(uint32_t *)(ptrs[i]+1) > maxDocId ) continue;

		// is this a gbxlatitude/gbxlongitude/gbxstarttime
		// field key? then event id is in score field.
		if ( sign == 'x' ) {
			a = b = 255 - *(uint8_t *)(ptrs[i]+5);
		}
		// is is a regular word in an event description?
		// then event id is a range in the date field.
		else {
			// get event id range
			a = 255 - *(uint8_t *)(ptrs[i]+7);
			b = 255 - *(uint8_t *)(ptrs[i]+6);
			// sanity
			if ( a > b ) { char *xx=NULL;*xx=0; }
		}

		// for debuging lat/lon/times for an event
		//int64_t d = getDocIdFromPtr(ptrs[i]);
		//if ( d == 17601280831LL && a==1 )
		//	log("hey listi=%"INT32"",listi);

		//
		// loop over event ids for this key
		//
		for ( ; a <= b ; a++ ) {

		// . otherwise, hash him, use the top 32 bits of his docid
		// . added in event id here
		nn = (*(uint32_t *)(ptrs[i]+1) + a*33 )& mask ;
		// save start position so we can see if we chain too much
		nnstart = nn;

	chain:
		// . if the hashtable slot is empty (docIdPtrs[nn] is NULL),
		//   then take right away
		// . this is the most common case so we put it first
		if ( docIdPtrs[nn] == NULL ) {

			// if we are sorting by distance we need to
			// add the "date" which is the lat or the lon
			if ( latVec ) {
				if ( listi == m_latTermOff )
					latVec[nn]=*(uint32_t*)(&ptrs[i][6]);
				if ( listi == m_lonTermOff )
					lonVec[nn]=*(uint32_t*)(&ptrs[i][6]);
			}
			/*
			// same for sorting by "time"
			if ( timeVec ) {
				if ( listi == m_timeTermOff )
					timeVec[nn]=*(uint32_t*)(&ptrs[i][6]);
				else
					timeVec[nn]=0;
			}
			
			if ( endVec ) {
				if ( listi == m_endTermOff )
					endVec[nn]=*(uint32_t*)(&ptrs[i][6]);
				else
					endVec[nn]=0;
			}
			*/
			// . did we miss it?
			// . if rat is true, then advance to next right now
			if ( ! hashIt ) continue;
			// hold ptr to our stuff
			docIdPtrs    [ nn ] = ptrs[i];
			// set pure bits
			explicitBits [ nn ] = ebits;
			hardCounts   [ nn ] = hc;
			eventIds     [ nn ] = a;
			// date contstrain (still read from datedb)
			// there is no score based on date in this case
			// but we are still getting the termlists from
			// datedb, we just use the regular score that
			// is contained in the datedb termlist, after
			// the date itself.
			scoresVec[nn*nqt+listi] = (uint8_t)ptrs[i][5];
			// replicate the code below for speed
			continue;	
		}

		// if docIds bits don't match, chain to next bucket
		if ( *(int32_t *)(ptrs[i]+1) != *(int32_t *)(docIdPtrs[nn]+1) ||
		     // event ids must match too now
		     a != eventIds[nn] ||
		     (*ptrs[i] & 0xfd) != (*docIdPtrs[nn] & 0xfd) ) {
			if ( ++nn >= numSlots ) nn = 0;
			// if we wrapped back, table is FULL!! PANIC!
			if ( nn == nnstart ) return false; // goto panic;
			// count the number of times we miss
			m_numCollisions++;
			goto chain;
		}

		// add in start and end times, but only if not set from above
		// where docIptr == NULL, we do not want to overwrite the first
		// one we encounter.
		/*
		if ( timeVec ) {
			// if the timeVec[nn] slot is unset, or if we are 
			// less than what's in it, then put us in there
			if ( listi == m_timeTermOff ) {
				uint32_t tt = *(uint32_t*)(&ptrs[i][6]);
				if      ( timeVec[nn] == 0 ) timeVec[nn] = tt;
				// these times are COMPLEMENTED!
				else if ( tt > timeVec[nn] ) timeVec[nn] = tt;
			}
			// same for the time endpoint
			if ( listi == m_endTermOff ) {
				uint32_t tt = *(uint32_t*)(&ptrs[i][6]);
				if      ( endVec[nn] == 0 ) endVec[nn] = tt;
				// these times are COMPLEMENTED!
				else if ( tt > endVec[nn] ) endVec[nn] = tt;
			}
		}
		*/

		// . got dup docid for the same termid due to index corruption?
		// . this happens a lot for m_searchingEvents because it adds
		//   multiple keys for the same termid but with different
		//   "dates", which are really hidden eventId ranges in 
		//   disguise
		if ( explicitBits[nn] & ebits ) {
			// . add in the score
			// . TODO: not correct because we are adding 8 bit
			//   scores that were mapped from 32-bit scores...
			//   but for now let's see what happens
			scoresVec[nn*nqt+listi] += (uint8_t)ptrs[i][5];
			continue;
		}

		// if docIdBits match OR in the pure bits
		explicitBits[nn]  |= ebits;
		// . else if we are using hardCounts for *many* query terms...
		// . terms not explicitly required will still have ebits > 0
		//   in order to support boolean expressions along side of
		//   hard required terms
		// . non-required phrases can all share the same ebit when we
		//   have a lot of query terms, so they will not be
		//   able to imply hard-required, single-word terms but at 
		//   least they can add to scores[]. they have ebits of 0.
		// . an ebits of 0x80000000 means it's a hard required term
		if ( hc ) hardCounts[nn]++;
		// always store in scores vec
		scoresVec[nn*nqt+listi] =(uint8_t)ptrs[i][5];

		// if we are sorting by distance we need to
		// add the "date" which is the lat or the lon
		if ( latVec ) {
			if ( listi == m_latTermOff )
				latVec[nn]=*(uint32_t*)(&ptrs[i][6]);
			if ( listi == m_lonTermOff )
				lonVec[nn]=*(uint32_t*)(&ptrs[i][6]);
		}

		//
		// end the for loop over the key
		//
		}

		// advance to next key now that we've exhausted the event id
		// range, [a,b]
		ptrs[i] += 10;
		if (ptrs[i] < ptrEnds[i] ) goto addMore;
		oldptrs[i] = ptrs[i];
		ptrs   [i] = NULL;

	//
	// end the big for loop here
	//
	}
	// no panic? then return true
	return true;
}


// . set *maxDocId for hashing the other termlists in addLists2_r()
// . store our "current/ongoing intersection" into the hash table
void IndexTable2::hashTmpDocIds2 ( uint32_t  *maxDocId     ,
				   char          **docIdPtrs    ,
				   unsigned char  *scoresVec    ,
				   uint8_t        *eventIds     ,
				   uint32_t       *latVec       ,
				   uint32_t       *lonVec       ,
				   //uint32_t       *timeVec      ,
				   //uint32_t       *endVec       ,
				   qvec_t         *explicitBits ,
				   int16_t          *hardCounts   ,
				   uint32_t   mask         ,
				   int32_t            numSlots     ,
				   int32_t            nqt          ) {
	// if none left to hash, we're done
	if ( m_nexti >= m_numTmpDocIds2 ) {
		*maxDocId = (uint32_t)0xffffffff;
		return;
	}
	*maxDocId = 0;
	int32_t maxi = m_nexti + (numSlots >> 1);
	if ( maxi > m_numTmpDocIds2 ) maxi = m_numTmpDocIds2;
	int32_t oldmaxi = maxi;
	// . move maxi down to a maxDocId slot
	// . we now use a hard count of -1 to indicate this special slot
	while ( maxi > 0 && m_tmpHardCounts2[maxi-1] != -1 ) maxi--;
	// sometimes, the block can be bigger than numSlots when we 
	// overestimate maxDocIds and the second list hits the first like 80%
	// or more of the time or so
	if ( maxi == 0 || maxi <= m_nexti ) {
		maxi = oldmaxi;
		int32_t bigmax = m_nexti + numSlots;
		if ( bigmax > m_numTmpDocIds2 ) bigmax = m_numTmpDocIds2;
		// we can equal bigmax if we have exactly m_numSlots(1024)
		// winners!! VERY RARE!! but it happened for me on the query
		// 'https://www.highschoolalumni.com/'. we filled up our hash
		// table exactly so we got m_numSlots winners, and before this
		// was "maxi < bigmax" which stopped int16_t of what we needed.
		// maxi should technically be allowed to equal m_numTmpDocIds2
		while ( maxi <= bigmax && m_tmpHardCounts2[maxi-1]!=-1) maxi++;
		// sanity check
		if ( m_tmpHardCounts2[maxi-1] != -1 ) {
			log(LOG_LOGIC,"query: bad tmpHardCounts.");
			char *xx = NULL; *xx = 0; 
		}
	}
	// set maxDocId
	*maxDocId = (int32_t)m_tmpDocIdPtrs2[maxi-1];
	// sanity check
	if ( *maxDocId == 0 ) { 
		log(LOG_LOGIC,"query: bad maxDocId."); 
		char *xx = NULL; *xx = 0; }
	// debug msg
	if ( m_isDebug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: Hashing %"INT32" top docids2, [%"INT32", %"INT32")",
		     maxi-m_nexti,m_nexti,maxi);
	int32_t nn;
	// we use a hard count of -1 to denote docid blocks
	for ( int32_t i = m_nexti ; i < maxi ; i++ ) {
		// . if hard count is -1 then that's a tag block
		// . all the docids before this should be < the docid in here
		if ( m_tmpHardCounts2[i] == -1 ) continue;
		// hash the top 32 bits of this docid
		// get event id if we should
		if ( m_searchingEvents ) 
			nn = (*(uint32_t *)(m_tmpDocIdPtrs2[i]+1) + 
			      m_tmpEventIds2[i]*33) & mask ;
		else
			nn = (*(uint32_t *)(m_tmpDocIdPtrs2[i]+1)) & mask ;
	chain:
		// . if empty, take right away
		// . this is the most common case so we put it first
		if ( docIdPtrs[nn] == NULL ) {
			// hold ptr to our stuff
			docIdPtrs    [ nn ] = m_tmpDocIdPtrs2[i];
			// store score
			gbmemcpy ( &scoresVec [nn * nqt] ,
				 &m_tmpScoresVec2[i * nqt],
				 nqt );
			// and this vector
			if ( m_searchingEvents ) {
				eventIds[nn] = m_tmpEventIds2[i];
				if ( latVec  ) latVec  [nn] = m_tmpLatVec2[i];
				if ( lonVec  ) lonVec  [nn] = m_tmpLonVec2[i];
				//if(timeVec ) timeVec [nn] = m_tmpTimeVec2[i];
				//if(endVec  ) endVec  [nn] = m_tmpEndVec2[i];
			}
			// insane sanity check (MDW)
			/*
			for ( int32_t x = 0 ; x < nqt ; x++ ) {
				int32_t m = m_imap[x];
				QueryTerm *qt = &m_q->m_qterms[m];
				qvec_t eb = qt->m_explicitBit;
				if ( !(eb & m_tmpEbitVec2[i]) ) continue;
				if ( scoresVec[nn*nqt+x] == 0 ) {
					char *xx = NULL; *xx = 0; }
			}
			*/
			// now we use the explicitBits!
			explicitBits [ nn ] = m_tmpEbitVec2[i];
			// and hard counts
			hardCounts   [ nn ] = m_tmpHardCounts2[i];
			// add the next one
			continue;
		}
		// if docIds bits don't match, chain to next bucket
		// since we don't have dups and this is the first list we do
		// not need to check for matching docids
		if ( ++nn >= numSlots ) nn = 0;
		goto chain;
	}
	// save the old one in case of a panic and rollback
	m_oldnexti = m_nexti;
	// advance next i
	m_nexti = maxi;
}


// . output stored in affWeights[] and affinities[]
// . affinities[i] ranges from 0.0 to 1.0
// . affWeights[i] ranges from 0.0 to 1.0
void IndexTable2::computeAffWeights    ( bool           rat          ,
					 int32_t           numDocIds    ,
					 char         **docIdPtrs    ,
					 unsigned char *scoresVec    ,
					 qvec_t        *explicitBits ,
					 int16_t         *hardCounts   ,
					 float         *affWeights   ,
					 float         *affinities   ) {
	// did we already do it?
	if ( m_computedAffWeights ) return;
	// sanity check.
	if ( ! m_getWeights ) { char *xx=NULL;*xx=0; }
	// do not re-do
	m_computedAffWeights = true;
	// in imap space
	int32_t nqt = m_ni;

	//int64_t startTime = gettimeofdayInMilliseconds();
	//logf ( LOG_DEBUG, "query: ComputeAffWeights nd=%"INT32" nqt=%"INT32"",
	//		  numDocIds, nqt );

 	/////////////////////////////////////////
	//
	// compute phrase affinities and weights
	//
	/////////////////////////////////////////

	// counts for domain voting restriction
	uint8_t domCount[256];
	int32_t    maxCount;

	float phrAffinities [ MAX_QUERY_TERMS ];
	float phrWeights    [ MAX_QUERY_TERMS ];

	// initialize all the weights and affinities to 1.0
	for ( int32_t i = 0; i < nqt; i++ ) {
		phrWeights    [i] =  1.0f;
		// -1.0f means the affinity is unknown
		phrAffinities [i] = -1.0f;
	}
	// . initialize the total to 0
	// . these are in imapped space
	int32_t termTotals   [ MAX_QUERY_TERMS ];
	int32_t phraseTotals [ MAX_QUERY_TERMS ];

	// loop through this many docids
	int32_t nd = numDocIds;
	// . if doing rat, do not oversample
	// . limit to sample size to keep things consistent
	// . if we look at too many then affinity can drop for rarer 
	//   phrases that are actually very relevent for the query.
	// . if we stop above "MIN_DOCIDS" we risk being inconsistent 
	//   on same-tier re-calls due to clustered/filtered results.
	//if ( rat && nd > MIN_DOCIDS ) nd = MIN_DOCIDS;
	// for non-rat, we limit sample to NORAT_TMP_SLOTS docids...
	//if ( ! rat && nd > NORAT_TMP_SLOTS ) nd = NORAT_TMP_SLOTS;

	// DEBUG: if not rat leave weights at 1
	//if ( !rat ) goto skipWeights;

	// first go through and add up all the totals for each phrase
	for ( int32_t t = 0; t < nqt; t++ ) {
		// reset counts
		termTotals[t] = 0;
		phraseTotals[t] = 0;
		// get the correct term num
		int32_t it = m_imap[t];
		// is this a phrase?
		if ( !m_q->isPhrase(it) || m_q->isInQuotes(it) ) continue;
		// get the query term
		QueryTerm *qt = &m_q->m_qterms[it];

		// . only allow a dom to vote at most 3 times
		// . prevents 'new mexico tourism' affinity from being controlled by
		//   newmexico.org cuz it has like the top 100 positions, and they
		//   all have the phrase "new mexico tourism" so "mexico tourism"
		//   ends up getting a high phrase affinity
		memset ( domCount , 0 , 256 );
		// max votes per domHash starts at 1, if we have 20+ explicit
		// matches, it will move to 2, 50+ then to 3.
		maxCount = 1;

		// loop through the results and count for this phrase
		for ( int32_t i = 0; i < nd ; i++ ) {
			// skip if a fake docid (used as token in m_tmp*2[]
			if ( hardCounts[i] == -1 ) continue;
			// get the matching bits
			qvec_t ebits = explicitBits[i];
			// skip if docid has minus query terms (not a match)
			if ( ebits & m_q->m_negativeBits ) continue;
			// does this docid explicitly have all the terms 
			// implied by phrase term #t? if not, continue.
			if ((ebits & qt->m_implicitBits)!= qt->m_implicitBits)
				continue;

			// . only allow up to 5 docids from any one domain
			// . the 8 bits of the docid is now the hash of the
			//   domain, primarily, for this purpose!
			uint8_t dh=g_titledb.getDomHash8((uint8_t *)docIdPtrs[i]);
			// only allow 3 votes per domain
			if ( domCount[dh] >= maxCount ) continue;
			// inc it
			domCount[dh]++;
			// 20+/50+ explicit matches, set maxCount to 2/3
			if ( termTotals[t] == 20 ) maxCount = 2;
			if ( termTotals[t] == 50 ) maxCount = 3;

			// debug
			if ( m_isDebug ) {
				int64_t d = getDocIdFromPtr(docIdPtrs[i]);
				int32_t cb = 0;
				if ( ebits & qt->m_explicitBit ) cb = 1;
				logf(LOG_DEBUG,"query: affweights d=%"INT64" "
				     "qtn=%"INT32""
				     " phrase=%"INT32" dh=0x%"XINT32"",d,it,cb,(int32_t)dh);
			}
			// count this docid. it has aANDb... (MDW)
			termTotals[t]++;
			// . does the result have this phrase explicitly?
			// . count for aADJb
			if ( ebits & qt->m_explicitBit )
				phraseTotals[t]++;
			// limit to sample size to keep things consistent
			if ( termTotals[t] >= MIN_DOCIDS ) break;
			//logf ( LOG_DEBUG, "query: Doc[%"INT32"] ibits=%"XINT32"  "
			//		  "Term[%"INT32"] ebit=%"XINT32" ibits=%"XINT32"",
			//		  i, (int32_t)ibits,
			//		  t, (int32_t)term->m_explicitBit,
			//		  (int32_t)term->m_implicitBits );
		}
	}

	// now compute the actual affinites from the counts
	for ( int32_t i = 0; i < nqt; i++ ) {
		// get the correct term num
		int32_t it = m_imap[i];
		// . words or quoted phrase terms do not have phrase affinities
		// . use -1.0 to signify this
		if ( ! m_q->isPhrase(it) || m_q->isInQuotes(it) ) {
			phrAffinities[i] = -1.0f;
			continue;
		}
		// how many docs had all required query terms explicitly?
		float total = (float)termTotals[i];
		// . use -1 to indicate affinity unknown
		// . do not divide by 0
		//if ( total <= 0.0 ) {
		//	phrAffinities[i] = -1.0f;
		//	continue;
		//}
		// divide docs that have the ith phrase term by "total" to get
		// the base affinity
		float ratio ;
		if ( total > 0.0 ) ratio = (float)phraseTotals[i] / total;
		// i think the default is .1
		else               ratio = (float)DEFAULT_AFFINITY;
		// . move it closer to default the fewer sample points we have
		// . take a weighted average
		// . perfect average if we have 20 docs that have all required
		//   query terms explicitly
		// . the default phrase affinity is about .10 i reckon'
		ratio = (total * ratio + AFF_SAMPLE_SIZE * DEFAULT_AFFINITY) / 
			(total + AFF_SAMPLE_SIZE );
		// store it
		phrAffinities[i] = ratio;
	}

	// . save "phraseAffinity" for logging before we mod it below
	// . this is in imap space
	float baseAff[MAX_QUERY_TERMS];
	for ( int32_t i = 0; i < nqt; i++ )
		baseAff[i] = phrAffinities[i];

	// now if two adjacent phrase terms correlate, i.e. if every
	// doc that has phrase A also has phrase B, then increase
	// the affinity of both A and B. BUT do not do this if
	// the current affinities of A and B are not close in order to
	// avoid increasing the affinity for "crazy real" in the phrase
	// "crazy real estate".

	// zero out first in a separate loop since a phrase term has a shot
	// at two different correlations for the phrase term to its left and
	// the phrase term to its right. we choose the maximum in the loop
	// below.
	float correlations     [ MAX_QUERY_TERMS ];
	int32_t  correlationTotal [ MAX_QUERY_TERMS ];
	for ( int32_t t = 0; t < nqt; t++ ) {
		// zero out
		correlations     [t] = -1.00;
		correlationTotal [t] = 0;
	}

	for ( int32_t t = 0; t < nqt; t++ ) {
		// skip if not a phrase term or invalid
		if ( phrAffinities[t] < 0.0 ) continue;
		// get imap space query term num
		int32_t it1 = m_imap[t];
		// get query term
		QueryTerm *qt = &m_q->m_qterms[it1];
		// get our implicit bits
		qvec_t ibits1 = qt->m_implicitBits;
		// . now find the next phrase term that shares an implicit bit
		// . start looking at the term after us
		int32_t j = t + 1;
		for ( ; j < nqt ; j++ ) {
			// skip if not a phrase term or invalid
			if ( phrAffinities[j] < 0.0 ) continue;
			// get imap space query term num
			int32_t it2 = m_imap[j];
			// get query term
			QueryTerm *t2 = &m_q->m_qterms[it2];
			// get our implicit bits
			qvec_t ibits2 = t2->m_implicitBits;
			// skip if no overlap
			if ( ! ( ibits1 & ibits2 ) ) continue;
			// get got the match
			break;
		}
		// skip if no correlating term
		if ( j >= nqt ) continue;
		// get imap space query term num
		int32_t it2 = m_imap[j];
		// . make the mask, one bit per word term in this phrase
		// . this can be empty for phrases like "to be or not to be"
		//uint32_t mask = m_q->m_qterms[t-1].m_implicitBits;
		qvec_t mask1    = m_q->m_qterms[it1].m_explicitBit;
		qvec_t mask2    = m_q->m_qterms[it2].m_explicitBit;
		qvec_t maskBoth = mask1 | mask2;
		// now we require all terms explicitly if not last tier
		qvec_t allSingles = 0;
		allSingles |= m_q->m_qterms[it1].m_implicitBits;
		allSingles |= m_q->m_qterms[it2].m_implicitBits;
		//if ( tier == MAX_TIERS - 1 ) allSingles = 0;
		// init counts
		int32_t total     = 0;
		int32_t totalBoth = 0;

		// do not allow one domain to dominate here either!
		memset ( domCount , 0 , 256 );
		// max votes per domHash starts at 1, if we have 20+ explicit
		// matches, it will move to 2, 50+ then to 3.
		maxCount = 1;

		// compute a correlation score
		for ( int32_t i = 0 ; i < nd ; i++ ) {
			// skip if a fake docid (used as token in m_tmp*2[]
			if ( hardCounts[i] == -1 ) continue;
			// skip if docid has NEITHER singleton term
			// in the phrase term (term #(t-1))
			//if ( ! (explicitBits[i] & mask) ) continue;
			if ( !(explicitBits[i] & maskBoth) ) continue;

			// . only allow up to 5 docids from any one domain
			// . the 8 bits of the docid is now the hash of the
			//   domain, primarily, for this purpose!
			uint8_t dh;
			dh=g_titledb.getDomHash8((uint8_t *)docIdPtrs[i]);
			// only allow 3 votes per domain
			if ( domCount[dh] >= maxCount ) continue;
			// inc it
			domCount[dh]++;
			// up the max as we go...
			if ( total == 128 ) maxCount = 2;
			if ( total == 256 ) maxCount = 3;

			// total count
			total++;
			// . does it have both?
			// . otherwise it only has one!!!
			if ( (explicitBits[i] & maskBoth) == maskBoth ) 
				totalBoth++;
		}
		// get correlation of the two phrase terms
		float corr = (float)totalBoth / (float)total;
		// count for logging, too
		correlationTotal[t] = total;

		// . the correlation depends on the size of the sample as well
		// . use the weighted average approach
		// . the default correlation is about .10 i reckon'?????
		//corr =  (total * corr + 10.0 * DEFAULT_CORRELATION) / 
		//	(total + 10.0 );
		// . assume 0 is the default correlation (no correlation), so
		//   to outperform that you need well over 10 sample points
		// . if we only have a few docids in the results in tier 0
		//   there's a good chance some of them have the terms 
		//   implicitly because they have the phrase terms... and that
		//   will bias the correlation score, so do a weighted of
		//   of it with a sample size of 50...
		corr =  (total * corr + CORR_SAMPLE_SIZE * 0.0 ) /
			(total + CORR_SAMPLE_SIZE );
		// assign
		correlations[j] = corr;
		// this may already have a correlation, if so, then
		// pick the highest one! give it the benefit of the doubt.
		if ( corr > correlations[t] )
			correlations[t] = corr;
		//if ( corr > correlations[j] )
		//	correlations[j] = corr;
	}

	// initialize
	float popAffMods   [MAX_QUERY_TERMS];
	int32_t  savedScore   [MAX_QUERY_TERMS];
	int32_t  scorePos     [MAX_QUERY_TERMS];
	float tfAffinities [MAX_QUERY_TERMS];
 	for ( int32_t i = 0; i < nqt; i++ ) {
		popAffMods   [i] = 1.0;
		savedScore   [i] = 0;
		scorePos     [i] = 0;
		tfAffinities [i] = DEFAULT_AFFINITY;
	}

	/*
	  problem is that "matchmaking india" actually gets a high phrase
	  affinity from this logic. bad! and MANY other phrase terms as well!
	  "mexico tourism" in 'new mexico tourism', etc.
	*/

	// . add up the top 100 scores from each phrase termlist.
	// . get the 100th score of the phrase termlist
	// . 100th*x, actually, where x is # docs / 1B
	// . if that score is < 20 then, decrease affinity, > 20 increase
 	for ( int32_t i = 0; i < nqt; i++ ) {
		// map to query term space
		int32_t it = m_imap[i];
		// skip if not an unquoted phrase
		if ( ! m_q->isPhrase(it) || m_q->isInQuotes(it) ) continue;
		// scale based on 1B pages
		float scalar = (float)m_numDocsInColl / (float)1000000000LL;
		// the magic number changes
		int32_t nn = 500;
		// . changes based on splits
		// . we may have legacy split!! like on gb1
		nn /= g_hostdb.m_indexSplits;
		// changes based on size of index
		nn = (int32_t)((float)nn * scalar);
		// min of 10
		if ( nn < 10 ) nn = 10;
		// get it
		RdbList *list = &m_lists[it];
		// use raw data
		char *data = list->getList();

		// get 2nd key! (first key is 12 bytes)
		char *rec = data + 12;
		// get the end of the termlist
		char *listEnd = list->getListEnd();
		// set this score
		unsigned char score = 0;
		// count total valid voters
		int32_t totalVoters = 0;

		// do not allow one domain to dominate here either!
		memset ( domCount , 0 , 256 );
		// max votes per domHash starts at 1, if we have 20+ explicit
		// matches, it will move to 2, 50+ then to 3.
		maxCount = 1;

		// loop over all recs
		for ( ; rec < listEnd ; rec += 6 ) {
			// . only allow up to 5 docids from any one domain
			// . the 8 bits of the docid is now the hash of the
			//   domain, primarily, for this purpose!
			uint8_t dh=g_titledb.getDomHash8((uint8_t *)rec);
			// only allow 3 votes per domain
			if ( domCount[dh] >= maxCount ) continue;
			// inc it
			domCount[dh]++;
			// every 128 gives an increase to "maxCount"
			if ( ((++totalVoters)%128) == 0 ) maxCount++;
			// watch out! how can this happen?
			if ( maxCount >= 254 ) break;
			// bust out?
			if ( totalVoters < nn ) continue;
			// . snag that score, we are done
			// . treat as score of 1 if breached
			if ( rec + 5 >= listEnd ) score = 1;
			// otherwise get score from termlist
			else score = 255 - (unsigned char)rec[5];
			// all done!
			break;
		}
			
		// preserve the score for logging
		savedScore[i] = (int32_t)score;
		scorePos  [i] = nn;
		// get 8 bit dom hash

		// . for every .01 over .10 weight affinity by 1.05
		// . "water fight"'s 100th score is 64 on a 1B pg index
		//   and i consider that to be a borderline phrase
		// . i changed the 64 to a 32: MDW
		// . 'new mexico tourism' had a score of 6
		//   and 'office max denver' like 62
		bool boosted = false;
		while ( score > 32 ) {
			score--;
			boosted = true;
			// increase by 5% for every 1 score pt above 20
			popAffMods    [i] *= 1.04;
			tfAffinities  [i] *= 1.04;
			//phrAffinities[i]*= 1.04;
		}
		// . lowest demotion is about .30 if we use .98
		// . do not punish weights for now, the problem is mostly
		//   that "new order" was not getting the proper high affweight
		//   because 'new' and 'order' are so generic and popular
		// . let's do it in case we have a small inaccurate sample
		//   for the aADJb calc above, however, punish very slightly!
		// . we have to punish now since we get the max score*affWeight
		//   from a word and its phrases it is in now, without dividing
		//   the phrase score by 30...
		// . NO! do not punish, if we query 'zak betz' it has very low
		//   term frequency!!
		//while ( score < 64 ) {
		//	score++;
		//	// decrease by 5% for every 1 score pt above 20
		//	popAffMods    [i] *= 0.98; // was 0.96
		//	phrAffinities [i] *= 0.98;
		//}

		// set phrase affinity to the max of the countAffinity or the tfAffinity
		// MDW: disable for now. 'emmi medical billing' query was not getting
		// the www.emmicorp.com/... url because "medical billing"'s phraff was
		// too high because of this. and a query for 'new .. order' seems to
		// work just fine now!
		//if ( (tfAffinities[i] > phrAffinities[i]) && boosted )
		//	phrAffinities[i] = tfAffinities[i];
	}


	// average correlation with phrase affinity, provided we have a valid
	// correlation, which means another phrase term must follow us
	for ( int32_t t = 0; t < nqt ; t++ ) {
		// must be a valid phrase term
		if ( phrAffinities[t] < 0.0 ) continue;
		if ( correlations [t] < 0.0 ) continue;
		// . only increase phrase affinity from this!
		// . we do not want to punish "running shoes" in the query
		//   'running shoes in the uk' just because 'shoes in the uk'
		//   does not correlate with 'running shoes'
		if ( correlations[t] < phrAffinities[t] ) continue;
		// average it and store
		float avg;
		avg = (phrAffinities[t+0] + correlations[t]) / 2.0;
		// only do it if it is higher
		if ( avg > phrAffinities[t+0] ) phrAffinities[t] = avg;
		// do not apply to the next guy if he is not a phrase term
		if ( phrAffinities[t+1] < 0.0 ) continue;
		// . he uses our correlation, we correlated with him
		// . he might correlate with the phrase term following him too!
		// . only do it if it is higher
		avg = (phrAffinities[t+1] + correlations[t]) / 2.0;
		if ( avg > phrAffinities[t+1] ) phrAffinities[t+1] = avg;
	}


	// sanity cutoff
	for ( int32_t t = 0; t < nqt ; t++ ) 
		if ( phrAffinities[t] > 1.0 ) phrAffinities[t] = 1.0;

	// now compute the actual affinity weights
	for ( int32_t i = 0; i < nqt; i++ ) {
		// ez var
		float p = phrAffinities[i];
		// is this a phrase?
		if ( p < 0.0 ) continue;
		// go to query term space
		int32_t it = m_imap[i];
		// compute the weight
		QueryTerm *qt = &m_q->m_qterms[it];

		// . if "qt" is a synonym, then skip this loop
		// . otherwise, synonyms "imply" what they are a synonym of
		//   so the synonym phrase term, "auto insurance" of the
		//   phrase term "car insurance" would imply that, and have
		//   its explicit bit in its qt->m_implicitBits and it would
		//   mess up its phrase weight here. synonyms should inherit
		//   their phrase and syn weights and affinities in the loop
		//   right below here.
		if ( qt->m_synonymOf ) continue;

		// demote phrase weight, if affinity is below default
		if ( p < DEFAULT_AFFINITY ) {
			// . demote phrase #i if affinity <= 0.30
			// . fixes 'books retail' query
			// . TODO: make more continuous
			while ( p < DEFAULT_AFFINITY ) {
				// newmexico.org dominates results for 
				// 'new mexico tourism' thereby making 
				// 'mexico tourism' have a higher affinity 
				// than it should, so do not demote too much! 
				// BUT if we do it too little then we get 
				// planeta.com coming up for mexico!
				phrWeights[i] *= 0.50;
				//phrWeights[i] *= 0.80;
				p             *= 2.00;
			}
			continue;
		}
		// demote the words in phrase term #i if affinity >= .40
		for ( int32_t j = 0; j < nqt; j++ ) {
			// query term space (i-transformed=it)
			int32_t it = m_imap[j];
			// get query term #j
			QueryTerm *checkTerm = &m_q->m_qterms[it];
			// skip if word not part of phrase term #i
			if (!(qt->m_implicitBits & checkTerm->m_explicitBit))
				continue;
			// re-assign for each word
			p = phrAffinities[i];
			// . demote word #j
			// . cut aff weight in half for every 10% pts above 
			//   the default affinity, (currently 10%)
			// . so if you have a 100% affinity (1.0) then the
			//   words in the phrase get their affinity weight
			//   cut in half 10 times!!
			// . TODO: make more continuous
			while ( p > DEFAULT_AFFINITY ) {
				// newmexico.org dominates results for 
				// 'new mexico tourism' thereby making 
				// 'mexico tourism' have a higher affinity 
				// than it should, so do not demote too much! 
				// BUT if we do it too little then we get 
				// planeta.com coming up for mexico!
				phrWeights[j] *= 0.50;
				//phrWeights[j] *= 0.80;
				p             -= 0.10;
			}
			continue;
		}
	}

	// now loop through all non-phrase synonyms and apply the
	// phrWeight from the term they are a synonym of to their phrWeight,
	// otherwise, for the query 'car insurance', where 'car' has a very
	// low phrWeight, the synonym, 'auto' will not! actually, in that
	// case we should not consider synonyms that are synonyms of a single
	// word in such a tight phrase!
	for ( int32_t i = 0; i < nqt; i++ ) {
		// ez var
		float p = phrAffinities[i];
		// skip if a phrase, should be -1 if not a phrase
		if ( p >= 0.0 ) continue;
		// go to query term space
		int32_t it = m_imap[i];
		// compute the weight
		QueryTerm *qt = &m_q->m_qterms[it];
		// get who it is a syn of
		QueryTerm *parent = qt->m_synonymOf;
		// skip if we are not a synonym
		if ( ! parent ) continue;
		// get parent term's index #
		int32_t pn = parent - m_q->m_qterms ;
		// skip if < 0! how can this happen?
		if ( pn < 0 ) continue;
		// phrWeights is in imap space, so go back
		int32_t ipn = m_revImap[pn];
		// get parent's phrWeight
		float pw = phrWeights[ipn];
		// multiply it to ours
		phrWeights[i] *= pw;
	}
		
	

 	//////////////////////////////////////////
	//
	// compute synonym affinities and weights
	//
	//////////////////////////////////////////

	float synAffinities      [ MAX_QUERY_TERMS ];
	float synWeights         [ MAX_QUERY_TERMS ];
	int32_t  docsWithAll        [ MAX_QUERY_TERMS ];
	int32_t  docsWithAllAndSyn  [ MAX_QUERY_TERMS ];

	// reset to defaults
	for ( int32_t i = 0; i < nqt; i++ ) {
		// default weight is 1.0
		synWeights    [i] =  1.0f;
		// -1.0f means the affinity is unknown
		synAffinities [i] = -1.0f;
		// clear the stats
		docsWithAll       [i] = 0;
		docsWithAllAndSyn [i] = 0;
	}

	qvec_t requiredBits = m_q->m_requiredBits;

	int32_t count1 = 0;
	int32_t count2 = 0;
	// . see how well synonyms correlate to what they are a synonym of
	// . only look at results that match the query
	for ( int32_t t = 0; t < nqt; t++ ) {
		// get the correct term num
		int32_t it = m_imap[t];
		// get the query term
		QueryTerm *qt = &m_q->m_qterms[it];
		// only deal with synonyms here
		if ( ! qt->m_synonymOf ) continue;
		// what are we a synonym of
		//qvec_t ebits1 = qt->m_implicitBits;
		// our ebit
		qvec_t ebits2 = qt->m_explicitBit;

		// reset counts
		count1 = 0;
		count2 = 0;

		// . only allow a dom to vote at most 3 times
		// . prevents a dominating domain from controlling a 
		//   synonym's affinity
		memset ( domCount , 0 , 256 );
		// max votes per domHash starts at 1, if we have 20+ explicit
		// matches, it will move to 2, 50+ then to 3.
		maxCount = 1;

		// loop through the results
		for ( int32_t i = 0 ; i < nd ; i++ ) {
			// skip if a fake docid (used as token in m_tmp*2[]
			if ( hardCounts[i] == -1 ) continue;
			// get the matching bits
			//qvec_t ibits = m_q->getImplicits(explicitBits[i]);
			//if ( ! (ibits & ebits1) ) continue;
			// must EXPLICITLY contain what we are a synonym of, 
			// skip if not
			//if ( ! (explicitBits[i] & ebits1) ) continue;
			// actually, let's require we got them all! 
			if ( (explicitBits[i] & requiredBits) != requiredBits)
				continue;
			// . only allow up to 5 docids from any one domain
			// . the 8 bits of the docid is now the hash of the dom
			uint8_t dh;
			dh=g_titledb.getDomHash8((uint8_t *)docIdPtrs[i]);
			// only allow 3 votes per domain
			if ( domCount[dh] >= maxCount ) continue;
			// inc it
			domCount[dh]++;
			// 20+/50+ explicit matches, set maxCount to 2/3
			if ( count1 == 20 ) maxCount = 2;
			if ( count1 == 50 ) maxCount = 3;

			// count it MDW
			count1++;

			// debug
			if ( m_isDebug ) {
				int64_t d = getDocIdFromPtr(docIdPtrs[i]);
				int32_t cb = 0;
				if ( explicitBits[i] & ebits2 ) cb=1;
				logf(LOG_DEBUG,"query: syn term #%"INT32" "
				     "docid=%"INT64" "
				     "hasSynTerm=%"INT32" "
				     "dh=0x%"XINT32"",t,d,cb,(int32_t)dh);
			}

			// and contain us?
			//if ( ! (ibits & ebits2) ) continue;
			if ( ! (explicitBits[i] & ebits2) ) continue;
			// count it
			count2++;
		}
		// set synonym affinity in imap space, not "it" query term spc
		//float aff = (float)count2/(float)count1;
		// get the weighted synonym affinity, 0.0 is the default!
		float waff = ((float)count2 + 20.0 * 0.0) / 
			((float)count1 + 20.0);
		// sanity cut off
		if ( waff > 1.0 ) waff = 1.0;
		// store it
		synAffinities[t] = waff;
		// now compute the synonym affinity weights, synWeights[], for
		// these guys it is the same for now...
		synWeights   [t] = waff * 2.0;
		// to not go overboard! it is just a synonym after all
		if ( synWeights[t] > .80 ) 
			synWeights[t] = .80;

		// count it for stats debug
		docsWithAll       [t] = count1;
		docsWithAllAndSyn [t] = count2;

		// if we are a phrase synonym compare our tf to the
		// thing we are a syn of, be it a phrase or word. so
		// if we do the query 'automobile insurance' then
		// "car insurance" receives a big boost...
		if ( ! qt->m_isPhrase ) continue;
		// . the tf of the syn
		// . m_termFreqs is in QUERY SPACE not IMAP SPACE
		float synTf = m_termFreqs[it];
		// and the thing it is a syn of
		int32_t pn = qt->m_synonymOf - m_q->m_qterms;
		float origTf = m_termFreqs[pn];
		// give a bonus if we're more popular
		if ( synTf < origTf ) continue;
		// how much to boost the syn weight?
		float boost = 1.0;
		// watch out for divide by 0
		if ( origTf >= 1.0 ) boost = synTf / origTf;
		// no more than 10x
		if ( boost > 10.0 ) boost = 10.0;
		// apply it
		synWeights[t] *= boost;

		/*
		// if the term is a high syn affinity, it will get a high 
		// syn weight. 1.0 is the highest syn weight possible.
		synWeights[t] = 1.0;
		float p = synAffinities[t];
		// if you have a syn affinity of .75, meaning you are
		// in half the docs that the word/phrase you are a syn of
		// is in, that is pretty good! in that case leave the syn
		// weight as 1.0, meaning you got a good chance of getting
		// the same final weight as the term you a are a syn of gets!
		while ( p < .70 ) {
			synWeights[t] *= 0.80;
			p             += 0.10;
		}
		*/

	}

	///////////////////////////////////////////
	//
	// compute the final affinity-based weights
	//
	///////////////////////////////////////////

	// incorporate both the phrase and synonym affinity
	// into affWeights[]
	for ( int32_t t = 0; t < nqt; t++ ) {
		// default
		affWeights[t] =  1.0f;
		// if phrase weight invalid, just use synWeight
		float pw = phrWeights[t];
		if  ( pw == -1.00 ) pw = 1.00;
		// store the final weight
		affWeights[t] = pw * synWeights[t] ;
		// pure affinities
		affinities[t] = 1.0;
		if (phrAffinities[t] >= 0.0) affinities[t] *= phrAffinities[t];
		if (synAffinities[t] >= 0.0) affinities[t] *= synAffinities[t];
	}

	// . reset the weights in query space because some query terms are 
	//   ignored and not even mentioned in imap space
	// . do not use "nqt" cuz that is really m_ni! not same as 
	//   m_q->m_numTerms if some query terms are ignored
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		m_affWeightsQS  [i] = -1.0;
		// this done in setFreqWeights() now
		//m_freqWeightsQS [i] = -1.0;
	}
	// store the weights in query space format for passing back in the
	// Msg39Reply for processing
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		m_affWeightsQS  [m_imap[i]] = affWeights  [i];
		// this done in setFreqWeights() now
		//m_freqWeightsQS [m_imap[i]] = freqWeights [i];
	}

	//////////////////////////////////////////
	//
	// debug info
	//
	//////////////////////////////////////////

	// log the final weights
	if ( ! m_isDebug ) return;

	for ( int32_t i = 0; i < nqt; i++ ) {
		// query term space
		int32_t it = m_imap[i];
		// get the term in utf8
		QueryTerm *qt = &m_q->m_qterms[it];
		//char bb[256];
		//utf16ToUtf8(bb, 256, qt->m_term, qt->m_termLen);
		char *tpc = qt->m_term + qt->m_termLen;
		char c = *tpc;
		*tpc = '\0';
		char sign = qt->m_termSign;
		if ( sign == 0 ) sign = '0';
		int32_t pn = -1;
		QueryTerm *parent = qt->m_synonymOf;
		if ( parent ) pn = parent - m_q->m_qterms;
		// print query term
		logf ( LOG_DEBUG, "query: Term "
		       "term=\"%s\" "
		       "termId=%"INT64" "
		       "synOfTermQnum#=%"INT32" "
		       "sign=%c "
		       "qnum=%"INT32" inum=%"INT32" "
		       "docsWithSingleTermsExplicitly(aANDb)=%"INT32" "
		       "suchDocsAlsoWithPhrase(aADJb)=%"INT32" "
		       //"freqWeight=%"INT32" "
		       "termFreq=%f "
		       "freqWeight=%.04f "

		       "phrAff=max{"
		       "basePhrAff,"
		       "affModTF,"
		       "(corrAff+basePhrAff)/2,"
		       "(corrAff+affModTF)/2"
		       "}... "

		       "basePhrAff=%f " // preCorPhrAff
		       //"affModTF=%f"
		       "tfPhrAff=%f"
		       "(scalar,midscore=%"INT32" nn=%"INT32") "
		       "corrAff=%f"
		       "(corrtotal=%"UINT32") "
		       "phrAffinity=%f "
		       "phrWeight=%f "
		       "synAffinity=%f "
		       "synWeight=%f "
		       "docsWithAll=%"INT32" "
		       "docsWithAllAndSyn=%"INT32" "
		       "finalAffinity=%f "
		       "finalAffWeight=%f "
		       "FINALWEIGHT=%f"
		       ,
		       qt->m_term,//bb ,
		       (int64_t)qt->m_termId ,
		       pn ,
		       sign,
		       it                  ,
		       i                   , 
		       (int32_t)termTotals    [i] , 
		       (int32_t)phraseTotals  [i] ,
		       (float)m_termFreqs[it]/(float)m_numDocsInColl,
		       m_freqWeights   [i] ,
		       baseAff         [i] ,
		       //popAffMods      [i] ,
		       tfAffinities    [i] , // based on midscore and tf
		       savedScore      [i] ,
		       scorePos        [i] ,
		       correlations    [i] ,
		       correlationTotal[i] ,
		       phrAffinities   [i] ,
		       phrWeights      [i] ,
		       synAffinities   [i] ,
		       synWeights      [i] ,
		       docsWithAll      [i] ,
		       docsWithAllAndSyn[i],
		       affinities      [i] ,
		       affWeights      [i] ,
		       affWeights      [i] * m_freqWeights[i] );
		// put it back
		*tpc = c;
	}
	/*
	int64_t endTime = gettimeofdayInMilliseconds();
	logf ( LOG_DEBUG, "query: ComputeAffWeights took %"INT64" ms",
			  endTime - startTime );
	*/
}

// when hashing docids into the small hash table for doing the intersection,
// it may not have a phrase term, and therefore, never store a value in that
// part of the scores vector, therefore, it is uninitialized. this routine
// resets it to 0. it is fast because we only consider the lastRound winners.
void IndexTable2::zeroOutVectorComponents ( unsigned char *scoresVec  ,
					    qvec_t        *ebits      ,
					    int16_t         *hardCounts ,
					    int32_t           numDocIds  ,
					    char           rat        ) {
	// we are in imap space
	int32_t nqt = m_ni;
	// loop over the query terms in imap space
	for ( int32_t t = 0 ; t < nqt ; t++ ) {
		// map from imap space to query term numspace
		int32_t it = m_imap[t];
		// get query term's ebit
		qvec_t ebit = m_q->m_qterms[it].m_explicitBit;
		// loop over all docids
		for ( int32_t i = 0 ; i < numDocIds ; i++ ) {
			// skip if marker
			if ( hardCounts[i] == -1 ) continue;
			// does it have it? if so, skip it
			if ( ebits[i] & ebit ) {
				// sanity check (MDW)
				//if ( rat && scoresVec[i*nqt+t] == 0 ){
				//	char*xx=NULL;*xx=0;}
				continue;
			}
			// otherwise, zero out vector component (complement it)
			scoresVec[i*nqt+t] = 255;
		}
	}
}
/*
void IndexTable2::computeWeightedScores ( int32_t            numDocIds    ,
					  int32_t           *finalScores  ,
					  unsigned char  *scoresVec    ,
					  qvec_t         *explicitBits ,
					  float          *affWeights   ,
					  char          **docIdPtrs    ) {

	// in imap space
	int32_t nqt = m_ni;
*/
	// find the average score for each query term
	//int32_t avgs[MAX_QUERY_TERMS];
	/*
	for ( int32_t t = 0 ; numDocIds > 0 && t < nqt ; t++ ) {
		// reset
		float sum = 0;
		// loop over all docids
		for ( int32_t j = 0 ; j < numDocIds ; j++ ) {
			// get score
			float s = m_freqWeights[t] * 100.0 *(255-scoresVec[t]);
			// let's take this out until we figure out a way
			// to deal with small samples without having
			// to make Msg3a go to the next tier, when the
			// combined samples from each split would be
			// enough to compute an affinity. also, we do not
			// want splits using different affinities!
			if ( affWeights[t] > 0.0 ) 
				s = s * affWeights[t];
			// add em up
			sum += s;
		}
		// make avg
		m_avgs[t] = (float)sum / (float)numDocIds ;
	}

	if ( m_isDebug || 1 == 1 ) {
		// show the avg score for each term
		for ( int32_t t = 0 ; numDocIds > 0 && t < nqt ; t++ ) 
			logf(LOG_DEBUG,"query: term #%"INT32" avg score=%f",
			     t,m_avgs[t]);
	}
	*/
/*
	//int64_t startTime = gettimeofdayInMilliseconds();
	//logf ( LOG_DEBUG, "query: ComputeWeightedScores nd=%"INT32" nqt=%"INT32"",
	//		  numDocIds, nqt );
	/////////////////////////////////////
	// compute final scores
	/////////////////////////////////////
	for ( int32_t i = 0; i < numDocIds; i++ ) {
		// skip negatives and empty slots
		//if ( flags[i] == 0 ) {
		//	finalScores[i] = 0;
		//	continue;
		//}
		// get the final score
		finalScores[i] = getWeightedScore ( &scoresVec[i * nqt],
						    nqt                ,
						    m_freqWeights      ,
						    affWeights         );
	}

	if ( ! m_isDebug ) return;

	char tmp[4096];
	char *pend = tmp + 4095;
	for ( int32_t i = 0; i < numDocIds; i++ ) {
		//if ( flags[i] == 0 ) continue;
		int64_t d = 0;
		gbmemcpy(&d, docIdPtrs[i], 6);
		d >>= 2;
		d &= DOCID_MASK;
		// log the score vec and the final score
		char *p = tmp;
		p += sprintf(p,"query: IndexTable2 - [%012"UINT64"] "
			     "ebits=%"UINT32" vec=[ ",
			     d,(int32_t)explicitBits[i]);
		float min = 9999999.0;
		float phraseMin = 9999999.0;
		for ( int32_t t = 0; t < nqt; t++ ) {
			if ( pend - p < 32 ) break;
			p += sprintf(p, "%03"INT32" ",
				     (int32_t)(255-scoresVec[i*nqt+t]));
			float pre = 255 - scoresVec[i*nqt+t];
			if ( affWeights[t] > 0.0 )
				pre = pre * affWeights[t];
			//float s = m_freqWeights[t]*100.0*pre;
			// skip if term is not hard required
			qvec_t ebit = m_q->m_qterms[t].m_explicitBit;
			if ( (ebit & (m_q->m_requiredBits)) == 0 ) {
				if ( pre < phraseMin ) phraseMin = pre;
				continue;
			}
			// get the max between us and our phrase terms
			int32_t j;
			float pscore;
			//int32_t it = m_imap[t];
			//if((j=m_q->m_qterms[it].m_leftPhraseTermNum)>=0){
			if((j=m_imapLeftPhraseTermNum[t])>=0){
				pscore= 255-scoresVec[i*nqt+j];
				if ( affWeights[j]>0)
					pscore *= affWeights[j];
				if ( pscore > pre ) pre = pscore;
			}
			//if((j=m_q->m_qterms[it].m_rightPhraseTermNum)>=0){
			if((j=m_imapRightPhraseTermNum[t])>=0){
				pscore = 255-scoresVec[i*nqt+j];
				if ( affWeights[j]>0)
					pscore *= affWeights[j];
				if ( pscore > pre ) pre = pscore;
			}
			if ( pre < min ) min = pre;
		}
		// use phrase min if we had no terms explicitly
		if ( min == 9999999.0 ) min = phraseMin;
		p += sprintf(p, "] min=%f [",min);
		float sum = 0.0;
		for ( int32_t t = 0; t < nqt; t++ ) {
			if ( pend - p < 32 ) break;
			float s = m_freqWeights[t] * 100.0 *
				(255-scoresVec[i*nqt+t]);
			if ( affWeights[t] > 0.0 ) 
				s = s * affWeights[t];
			p += sprintf(p, "%f ", s );
			sum += s;
			// skip if not required
			//qvec_t requiredBits = m_q->m_requiredBits ;
			//  qvec_t ebit = m_q->m_qterms[t].m_explicitBit;
			//  if ( (ebit & requiredBits) == 0 ) continue;
			//  //if ( min >= 0.0 && s > min ) continue;
			//  //min = s;
			//  if ( min >= 0.0 && 255-scoresVec[i*nqt+t]>min) 
			//  continue;
			//  min = 255-scoresVec[i*nqt+t];
			//
		}
		p += sprintf(p, "] "
			     //"min=%f "
			     "score=%"INT32"", 
			     //min,
			     finalScores[i]);
		logf ( LOG_DEBUG, "%s", tmp );
	}
	//int64_t endTime = gettimeofdayInMilliseconds();
	//logf ( LOG_DEBUG, "query: ComputeWeightedScores took %"INT64" ms",
	//		  endTime - startTime );
}
*/

// fill topp[],tops[] and topb[] with up to "numTop" of the highest-scoring 
// docids from topp2[],tops2[],topdv2[],topev2[],tophc2[].
int32_t IndexTable2::fillTopDocIds ( //char         **topp      ,
				  //score_t       *tops      ,
				  //unsigned char *topb      ,
				  int32_t           numTop    ,
				  char         **tmpp2     ,
				  uint8_t       *tmpsv2    ,
				  uint32_t      *tmpdv2    ,

				  uint32_t      *tmplatv2  ,
				  uint32_t      *tmplonv2  ,
				  //uint32_t      *tmptimev2 ,
				  //uint32_t      *tmpendv2 ,
				  
				  qvec_t        *tmpev2    ,
				  int16_t         *tmphc2    ,
				  uint8_t       *tmpeid2   , // event ids
				  int32_t           numTmp2   ) {
	// affinities must be computed
	if ( ! m_computedAffWeights ) { char *xx = NULL; *xx = 0; }

	// sanity check (do not check last guy)
	//for ( int32_t i = 0 ; i < numTmp2 - 1 ; i++ ) {
	//	if ( tmptimev2 && tmptimev2[i] == 0 ) { char *xx=NULL;*xx=0; }
	//}

	// only compute these on the SECOND CALL
	if ( m_getWeights ) return 0;

	// how many docids are we putting into "topd", etc.?
	int32_t max = numTop;
	if ( max > numTmp2 ) max = numTmp2;

	int32_t           i            = 0;
	//unsigned char  minBitScore  = 0;
	//score_t        minScore     = 0;
	//char          *minDocIdPtr  = NULL;
	//int32_t           weakest      = -1;
	//int32_t           count        = 0;

	// top tree stuff
	int32_t           nqt          = m_ni;
	int32_t           tn;
	TopNode       *t;
	qvec_t         ebits ;
	score_t        score;
	// this is used by TopTree only if TopTree::m_searchingEvents is true
	score_t        score2 = 0;
	int32_t notFoundCount = 0;

	//uint8_t domHash = 0;
	int32_t    numDomHashDocIdsTotal[256];
	memset ( numDomHashDocIdsTotal, 0, 256 * sizeof(int32_t) );
	//bool    removedNode = false;
	//char *removedDocIdPtr = NULL;

	// . pick the best table
	// . if searcher provided a "setclock=" or "clockset="
	//   cgi parm then they want to see what the results would
	//   have looked like at the provided time_t time. so in that
	//   case we should have generated a custom hashtable by
	//   scanning all of timedb for the next occuring time of
	//   each event from that time.
	HashTableX *ht = m_sortByDateTablePtr;
	// otherwise, if no "setclock=" was provided we use the
	// current time and therefore use this current hash table
	// that is being continuously updated so that given an eventid
	// it provides the next occuring time for that event.
	if ( ! ht ) {
		// get it for that coll
		CollectionRec *cr = g_collectiondb.getRec(m_collnum);
		//if ( ! cr ) return true;
		ht = &cr->m_sortByDateTable;
	}

	//
	//
	// loop to fill top tree with the highest-scoring docids
	//
	//
 loop1:
	// all done? if so, return how many we got in TopTree!
	if ( i >= numTmp2 ) {
		// this is because of doing a clockset? see comment in Timedb.h
		if ( notFoundCount ) 
			log("timedb: %"INT32" events not found in timedb",
			    notFoundCount);
		// all done
		return m_topTree->m_numUsedNodes;
	}
	// skip if it is a bogus flag marker docid
	if ( tmphc2[i] == -1 ) { i++; goto loop1; }
	// also skip if it is a bogus docId, for innerLoopClustering
	//if ( getDocIdFromPtr(tmpp2[i]) == 0 )  { i++; goto loop1; }
	//if ( getDocIdFromPtr(tmpp2[i]) == 216132219700LL )
	//	log("hey");

	score = getWeightedScore ( &tmpsv2[i * nqt] ,
				   nqt              ,
				   m_freqWeights    ,
				   m_affWeights     ,
				   m_requireAllTerms);
	
	// . the m_affWeights[] are computed at this point
	// . score is just the date if sorting by date
	if ( m_sortByDate ) {
		// use regular score as secondary score
		score2 = score;
		// assume none
		//score_t score2 = 0;
		// set it differently depedning...
		//if ( ! m_searchingEvents )
		//	score2 = (score_t)tmpdv2[i];
		// . of one the datedb termlists has a termid that is a hash
		//   of the event id for that docid, and whose date is the
		//   start of the event, so use that as the score. by default
		//   assume it is the last term. 
		// . BUT to be safe, just use m_timeTermOff like how we
		//   use m_latTermOff below
		// . also, we now store the date of EACH termlist since the
		//   date fields will be different when searching events since
		//   we use the date field to hold the range of event ids that
		//   the term's section spans.
		//else 
		score = (score_t)tmpdv2[i*nqt];//+m_timeTermOff];
		// scale relevancy score
		//if ( score > 256 ) { char *xx=NULL;*xx=0; }
		// just add it in... 256 seconds shouldn't be a big deal
		//score += score2;
		//score = score2;
	}
	else if ( m_sortBy == SORTBY_DIST ) {
		// check to see if expired!!!
		int32_t tt = getTimeScore (getDocIdFromPtr(tmpp2[i]), // docid
					tmpeid2[i] , // eventId
					m_nowUTCMod,
					ht ,
					m_showInProgress );
		// skip if event is over and not displaying expired events
		if ( tt == 1 && ! m_showExpiredEvents ) {
			i++;
			goto loop1;
		}
		// not found?
		if ( tt == 0 ) notFoundCount++;
		// use regular score as secondary score
		score2 = score;
		// . sanity checks
		// . no, these could be -1 if we added the lat/lon 
		//   termlists first and other termlists after with a 
		//   2nd call to addLists_r() to refine the intersection!!
		//if ( m_latTermOff == -1 ) { char *xx=NULL;*xx=0; }
		//if ( m_lonTermOff == -1 ) { char *xx=NULL;*xx=0; }
		// . the date value is the lat or the lon depending on if this
		//   is termlist gbxlatitude or gbxlongitude
		// . make sure to have multiplied m_userLat by 10M
		// . remember the bits in the date are complemented!
		int32_t latDiff = tmplatv2[i] - m_userLatIntComp;
		if ( latDiff < 0 ) latDiff *= -1;
		int32_t lonDiff = tmplonv2[i] - m_userLonIntComp;
		if ( lonDiff < 0 ) lonDiff *= -1;
		// shrink a little to prevent overflow
		//latDiff /= 2;
		//lonDiff /= 2;
		// make the final score
		score = latDiff + lonDiff;
		// complement that since closer is better (smaller is better)
		score = ~score;
		// boost by 100 -- nah, already could be 360*10M*2....
		//score2 *= 100;
		// scale relevancy score?
		//if ( score > 256 ) { char *xx=NULL;*xx=0; }
		// just add it in... 256 points shouldn't be a big deal
		//score += score2;
		//score = score2;
	}
	else if ( m_sortBy == SORTBY_TIME ) {
		// use regular score as secondary score
		score2 = score;
		// consult the timedb table (getTimeScore() is in Timedb.h)
		//score = g_sortByDateTable.getScore ( docIdPtr,eventId );
		score = getTimeScore (//m_collnum,
				      getDocIdFromPtr(tmpp2[i]), // docid
				      tmpeid2[i] , // eventId
				      m_nowUTCMod,
				      ht ,
				      m_showInProgress );
		// skip if event is over and not displaying expired events
		if ( score == 1 && ! m_showExpiredEvents ) {
			i++;
			goto loop1;
		}
		// is it in progress (and showing in progress is disabled), 
		// over or no longer in timedb?
		if ( score == 0 ) {
			i++;
			goto loop1;
		}
		// debug
		//int64_t d = getDocIdFromPtr(tmpp2[i]);
		//logf(LOG_DEBUG,"gb: docid=%012"UINT64" eventid=%03"INT32" score=%"UINT32"",  
		//     d, (int32_t)tmpeid2[i],score);
		/*
		// sanity checks
		//if ( m_timeTermOff == -1 ) { char *xx=NULL;*xx=0; }
		// on stack now
		//score_t score2;
		// get start time
		time_t start = ~tmptimev2[i];
		// get endpoint
		time_t end = ~tmpendv2[i];
		// how is this?
		if ( start == -1 ) { char *xx=NULL;*xx=0; }
		// end point is now -1 to indicate no tod range, just a start
		// time. before we were setting this to midnight which is not
		// a good assumption.
		if ( end   == -1 ) { char *xx=NULL;*xx=0; }
		// debug
		int64_t d = getDocIdFromPtr(tmpp2[i]);
		logf(LOG_DEBUG,"gb: start=%"UINT32" end=%"UINT32" storehrs=%"INT32" "
		     "end-start=%"INT32" docid=%"UINT64" eventid=%"INT32"",  
		     start,end,(end&0x01),end-start,
		     d,
		     (int32_t)tmpeid2[i]);
		// . if this is a "store hours" "event" AND the "store" is 
		//   currently open, then we want to score it by how much time
		//   is left before it closes.
		// . last bit in end time is 0 to indicate NOT store hours,
		//    and 1 to indicate store hours
		// . and endvec2[i] is the ending time, complemented
		// . store hours are now indicated by having a tod range,
		//   and if no tod range is given, then the Interval::m_b is
		//   set to m_a in Dates.cpp.
		if ( end != start && // (end & 0x01 ) == 1 && 
		     // and it is not yet closed
		     end > m_nowUTCMod &&
		     // next start time is after that...
		     start > end ) {
			// then the score is the complement of how many secs
			// are left before it closes
			score = ~(end - m_nowUTCMod);
			// note score
			//log(LOG_DEBUG,"gb: storescore=%"UINT32"",(end-m_nowUTC));
		}
		// set score otherwise
		else {
			score = ~(start - m_nowUTCMod);
			//log(LOG_DEBUG,"gb: eventscore=%"UINT32"",start-m_nowUTC);
		}
		*/

		// mdw mdw mdw
		// #8  s=4294957824.000 eventid=9
		// #10 s=4294957824.000 eventid=8 *wrong score*
		// #15 s=4294950656.000 eventid=10

		// #8 has the right score, but not #10

		// complement, no, it is already complemented so leave it
		// that way so events that have lower start times have a 
		// higher score
		//score2 = ~score2;
		// scale relevancy score
		//if ( score > 256 ) { char *xx=NULL;*xx=0; }
		// just add it in... 256 seconds shouldn't be a big deal
		//score += score2;
		//score = score2;
	}

	// add it to node #tn
	tn = m_topTree->getEmptyNode();
	t  = &m_topTree->m_nodes[tn];
	// set the score and docid ptr
	t->m_score = score;
	t->m_score2 = score2;
	t->m_docId = getDocIdFromPtr(tmpp2[i]); //tmpp2[i];
	// get explicit bits and use to set t->m_bscore
	ebits = tmpev2[i];
	//if (m_isDebug)
	//	logf(LOG_DEBUG, 
	//	     "query: getBitScore3 "
	//	     "queryId=%lld bits=0x%016"XINT64"",
	//	     m_r->m_queryId,
	//	     (int64_t) ebits);
	// set the bit score
	t->m_bscore = getBitScore ( ebits );
	// add in hard count (why didn't tree have this before?)
	t->m_bscore += tmphc2[i];
	// . ignore the top bits
	// . no! msg3a looks at them to see how many explicit hits it got
	//   in this tier...
	//t->m_bscore &= ~0xc0;

	// the actual docids has the event id multiplied by 33 and added to
	// it in order to keep the event ids separated. so we have to
	// remember to subtract that when done...
	if ( m_searchingEvents ) t->m_eventId = tmpeid2[i];

	// DEBUG - show the winner
	if ( m_isDebug ) {
		// TMP debug vars
		char tt[10024];
		char *pp ;
		int64_t d ;
		pp = tt;
		pp += sprintf(pp,"[");
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			// map into imap space
			int32_t qi = m_revImap[k];
			if ( qi == -1 )
				pp += sprintf(pp,"-- ");
			else
				pp += sprintf(pp,"%"INT32" ",
					      (int32_t)255-tmpsv2[i*nqt+qi]);
		}
		pp += sprintf(pp,"] affw=[");
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			// map into imap space
			int32_t qi = m_revImap[k];
			if ( qi == -1 )
				pp += sprintf(pp,"-- ");
			else
				pp += sprintf(pp,"%f ",m_affWeights[qi]);
		}
		pp += sprintf(pp,"] freqw=[");
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			// map into imap space
			int32_t qi = m_revImap[k];
			if ( qi == -1 )
				pp += sprintf(pp,"-- ");
			else
				pp += sprintf(pp,"%.04f ",m_freqWeights[qi]);
		}
		pp += sprintf(pp,"] revImap=[");
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			// map into imap space
			int32_t qi = m_revImap[k];
			if ( qi == -1 )
				pp += sprintf(pp,"-- ");
			else
				pp += sprintf(pp,"%"INT32" ",qi);
		}
		pp += sprintf(pp,"]");
		d = getDocIdFromPtr(tmpp2[i]);
		uint8_t dh=g_titledb.getDomHash8((uint8_t *)tmpp2[i]);
		// show event id?
		if ( m_searchingEvents ) {
			// get event id
			uint8_t eid = tmpeid2[i];
			pp += sprintf(pp," eventid=%"UINT32"" ,(int32_t)eid );
			// fix docid 
			//d -= 33 * eid;
		}
		// print out score_t
		logf(LOG_DEBUG,"query: T %"INT32") d=%"UINT64" %s s=%"UINT32" dh=0x%hhx "
		     "bs=0x%02"XINT32" ebits=0x%"XINT32" required=0x%"XINT64"", i,d,tt,score,dh,
		     (int32_t)t->m_bscore,(int32_t)ebits,
		     (int64_t)m_q->m_requiredBits);
	}

	// . this will not add if tree is full and it is less than the 
	//   m_lowNode in score
	// . if it does get added to a full tree, lowNode will be removed
	m_topTree->addNode ( t, tn);//, &removedNode, &removedDocIdPtr );

	// advance i
	i++;
	// repeat with next docids
	goto loop1;
}
