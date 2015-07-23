// Matt Wells, copyright Jul 2001

// . gets the resulting docIds from a query
// . TODO: use our own facility to replace Msg2? hash a list as it comes.

#ifndef _MSG39_H_
#define _MSG39_H_

#include "UdpServer.h"      // UdpSlot type
#include "Multicast.h"      // multicast send
#include "Query.h"          // Query::set()
#include "Msg37.h"          // getTermFreqs()
#include "Msg2.h"           // getLists()
#include "Posdb.h"
#include "IndexList.h"
#include "TopTree.h"
#include "Msg51.h"
#include "HashTableX.h"

#define MAX_MSG39_REQUEST_SIZE (500+MAX_QUERY_LEN)

void  handleRequest39 ( UdpSlot *slot , int32_t netnice ) ;

class Msg39Request {

 public:

	Msg39Request () { reset(); };

	void reset() {
		m_docsToGet               = 10;
		m_niceness                = MAX_NICENESS;
		m_maxAge                  = 0;
		m_maxQueryTerms           = 9999;
		//m_compoundListMaxSize     = 20000000;
		m_boolFlag                = 2;
		m_language                = 0;
		m_queryExpansion          = false;
		m_debug                   = 0;
		m_getDocIdScoringInfo     = true;
		m_doSiteClustering        = true;
		m_hideAllClustered        = false;
		//m_doIpClustering          = true;
		m_doDupContentRemoval     = true;
		m_restrictPosdbForQuery   = false;
		m_addToCache              = false;
		m_familyFilter            = false;
		m_timeout                 = -1; // -1 means auto-compute
		//m_useMinAlgo              = false;
		//m_fastIntersection        = -1;
		m_stripe                  = 0;
		m_collnum                 = -1;
		m_useQueryStopWords       = true;
		//m_useNewAlgo              = true;
		m_doMaxScoreAlgo          = true;
		m_seoDebug                = false;
		m_useSeoResultsCache      = false;
		
		ptr_readSizes             = NULL;
		ptr_query                 = NULL; // in utf8?
		ptr_whiteList             = NULL;
		//ptr_coll                  = NULL;
		m_forSectionStats         = false;
		size_readSizes            = 0;
		size_query                = 0;
		size_whiteList            = 0;
		m_sameLangWeight          = 20.0;
		m_maxFacets = -1;
		//size_coll                 = 0;

		m_getDocIdScoringInfo = 1;

		// -1 means to not to docid range restriction
		m_minDocId = -1LL;
		m_maxDocId = -1LL;

		m_numDocIdSplits = 1;

		// for widget, to only get results to append to last docid
		m_maxSerpScore = 0.0;
		m_minSerpDocId = 0LL;

		m_makeReply = true;

		// . search results knobs
		// . accumulate the top 10 term pairs from inlink text. lower
		//   it down from 10 here.
		m_realMaxTop = MAX_TOP;
	};

	// we are requesting that this many docids be returned. Msg40 requests
	// of Msg3a a little more docids than it needs because it assumes
	// some will be de-duped at summary gen time.
	int32_t    m_docsToGet;
	int32_t    m_maxFacets;
	int32_t    m_nqt; // # of query terms
	char    m_niceness;
	int32_t    m_maxAge;
	int32_t    m_maxQueryTerms;
	int32_t    m_numDocIdSplits;
	float   m_sameLangWeight;

	//int32_t    m_compoundListMaxSize;
	char    m_boolFlag;
	uint8_t m_language;

	// flags
	char    m_queryExpansion;
	char    m_debug;
	char    m_seoDebug;
	char    m_useSeoResultsCache;
	char    m_doSiteClustering;
	char    m_hideAllClustered;
	//char    m_doIpClustering;
	char    m_doDupContentRemoval;
	char    m_restrictPosdbForQuery;
	char    m_addToCache;
	char    m_familyFilter;
	char    m_getDocIdScoringInfo;
	char    m_realMaxTop;
	char    m_stripe;
	char    m_useQueryStopWords;
	//char    m_useNewAlgo;
	char    m_doMaxScoreAlgo;

	char    m_forSectionStats;

	// Msg3a still uses this
	//int32_t    m_myFacetVal32; // for gbfacet:xpathsite really sectionstats

	//char    m_useMinAlgo;
	//char    m_fastIntersection;

	collnum_t m_collnum;

	int64_t m_minDocId;
	int64_t m_maxDocId;
	bool      m_makeReply;

	// for widget, to only get results to append to last docid
	double    m_maxSerpScore;
	int64_t m_minSerpDocId;

	// msg3a stuff
	int32_t    m_timeout; // in seconds

	time_t  m_nowUTC;

	// do not add new string parms before ptr_readSizes or
	// after ptr_whiteList so serializeMsg() calls still work
	char   *ptr_readSizes;
	char   *ptr_termFreqWeights;
	char   *ptr_query; // in utf8?
	char   *ptr_whiteList;
	//char   *ptr_coll;
	
	// do not add new string parms before size_readSizes or
	// after size_whiteList so serializeMsg() calls still work
	int32_t    size_readSizes;
	int32_t    size_termFreqWeights;
	int32_t    size_query;
	int32_t    size_whiteList;
	//int32_t    size_coll;

	char    m_buf[0];
};


class Msg39Reply {

public:

	// zero ourselves out
	void reset() { memset ( (char *)this,0,sizeof(Msg39Reply) ); };

	int32_t   m_numDocIds;
	// # of "unignored" query terms
	int32_t   m_nqt;
	// # of estimated hits we had
	int32_t   m_estimatedHits;
	// error code
	int32_t   m_errno;

	// do not add new string parms before ptr_docIds or
	// after ptr_clusterRecs so serializeMsg() calls still work
	char  *ptr_docIds         ; // the results, int64_t
	char  *ptr_scores;        ; // now doubles! so we can have intScores
	char  *ptr_scoreInfo      ; // transparency info
	char  *ptr_pairScoreBuf   ; // transparency info
	char  *ptr_singleScoreBuf ; // transparency info
	char  *ptr_numDocsThatHaveFacetList ;
	// this is now 1-1 with # of query terms!
	char  *ptr_facetHashList   ; // list of all the facet values in serps
	char  *ptr_clusterRecs    ; // key_t (might be empty)
	
	// do not add new string parms before size_docIds or
	// after size_clusterRecs so serializeMsg() calls still work
	int32_t   size_docIds;
	int32_t   size_scores;
	int32_t   size_scoreInfo;
	int32_t   size_pairScoreBuf  ;
	int32_t   size_singleScoreBuf;
	int32_t   size_numDocsThatHaveFacetList ;
	int32_t   size_facetHashList;
	int32_t   size_clusterRecs;

	// . this is the "string buffer" and it is a variable size
	// . this whole class is cast to a udp reply, so the size of "buf"
	//   depends on the size of that udp reply
	char       m_buf[0];
};


class Msg39 {

 public:

	Msg39();
	~Msg39();
	void reset();
	void reset2();
	// register our request handler for Msg39's
	bool registerHandler ( );
	// called by handler when a request for docids arrives
	void getDocIds ( UdpSlot *slot ) ;
	// XmlDoc.cpp seo pipeline uses this call
	void getDocIds2 ( class Msg39Request *req ) ;
	// retrieves the lists needed as specified by termIds and PosdbTable
	bool getLists () ;
	// called when lists have been retrieved, uses PosdbTable to hash lists
	bool intersectLists ( );//bool updateReadInfo ) ;
	// this is called after thread exits, or if thread creation failed
	bool addedLists();

	// incoming parameters passed to Msg39::getDocIds() function
	//void       *m_state;
	//void      (* m_callback ) ( void *state );

	// . this is used by handler to reconstruct the incoming Query class
	// . TODO: have a serialize/deserialize for Query class
	Query       m_tmpq;

	int64_t m_docIdStart ;
	int64_t m_docIdEnd   ;

	// used to get IndexLists all at once
	Msg2        m_msg2;

	// holds slot after we create this Msg39 to handle a request for docIds
	UdpSlot    *m_slot;

	// . used for getting IndexList startKey/endKey/minNumRecs for each 
	//   termId we got from the query
	// . used for hashing our retrieved IndexLists
	PosdbTable m_posdbTable;

	// keep a ptr to the request
	Msg39Request *m_r;

	char       m_debug;

	//int32_t m_numDocIdSplits;
	bool m_allocedTree;
	int64_t m_ddd;
	int64_t m_dddEnd;
	bool doDocIdSplitLoop();

	// . we hold our IndexLists here for passing to PosdbTable
	// . one array for each of the tiers
	//IndexList  m_lists [ MAX_QUERY_TERMS ];
	IndexList *m_lists;
	SafeBuf m_stackBuf;
	
	// used for timing
	int64_t  m_startTime;

	// this is set if PosdbTable::addLists() had an error
	int32_t       m_errno;

	// always use top tree now
	TopTree    m_tt;

	char       m_boolFlag;

	int32_t       m_firstResultNum;

	int64_t  m_numTotalHits;

	int32_t       m_numCensored;

	// for indexdb splitting
	char      m_paritySplit;

	int32_t        m_bufSize;
	char       *m_buf;
	int64_t  *m_clusterDocIds;
	char       *m_clusterLevels;
	key_t      *m_clusterRecs;
	int32_t        m_numClusterDocIds;
	int32_t        m_numVisible;
	int32_t        m_numDocIds;
	Msg51       m_msg51;
	bool        m_gotClusterRecs;
	bool        controlLoop();
	int32_t m_phase;
	void        estimateHitsAndSendReply   ();
	bool        setClusterRecs ();
	bool        gotClusterRecs ();

	// hack stuff
	void *m_tmp;
	int32_t  m_tmp2;
	bool  m_blocked;
	void (*m_callback)( void *state );
	void  *m_state;
	int64_t m_topDocId;
	float     m_topScore;
	int64_t m_topDocId2;
	float     m_topScore2;

	// . for the top 50 algo in seo.cpp
	// . will be the score of the last result if < 50 results
	float     m_topScore50;
	int64_t m_topDocId50;

	bool  m_inUse;
};		

#endif
