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

void  handleRequest39 ( UdpSlot *slot , long netnice ) ;

class Msg39Request {

 public:

	Msg39Request () { reset(); };

	void reset() {
		m_docsToGet               = 10;
		m_niceness                = MAX_NICENESS;
		m_maxAge                  = 0;
		m_maxQueryTerms           = 9999;
		m_compoundListMaxSize     = 20000000;
		m_boolFlag                = 2;
		m_language                = 0;
		m_queryExpansion          = false;
		m_debug                   = 0;
		m_getDocIdScoringInfo     = true;
		m_doSiteClustering        = true;
		m_doIpClustering          = true;
		m_doDupContentRemoval     = true;
		m_restrictPosdbForQuery   = false;
		m_addToCache              = false;
		m_familyFilter            = false;
		m_timeout                 = -1; // -1 means auto-compute
		m_getSectionStats         = false;
		m_useMinAlgo              = false;
		m_fastIntersection        = -1;
		m_stripe                  = 0;
		m_useQueryStopWords       = true;
		m_useNewAlgo              = true;
		m_doMaxScoreAlgo          = true;
		m_seoDebug                = false;
		m_useSeoResultsCache      = false;

		ptr_readSizes             = NULL;
		ptr_query                 = NULL; // in utf8?
		ptr_coll                  = NULL;

		size_readSizes            = 0;
		size_query                = 0;
		size_coll                 = 0;

		m_getDocIdScoringInfo = 1;

		// -1 means to not to docid range restriction
		m_minDocId = -1;
		m_maxDocId = -1;

		m_makeReply = true;

		// . search results knobs
		// . accumulate the top 10 term pairs from inlink text. lower
		//   it down from 10 here.
		m_realMaxTop = MAX_TOP;
	};

	// we are requesting that this many docids be returned. Msg40 requests
	// of Msg3a a little more docids than it needs because it assumes
	// some will be de-duped at summary gen time.
	long    m_docsToGet;
	long    m_nqt; // # of query terms
	char    m_niceness;
	long    m_maxAge;
	long    m_maxQueryTerms;
	long    m_compoundListMaxSize;
	char    m_boolFlag;
	uint8_t m_language;

	// flags
	char    m_queryExpansion;
	char    m_debug;
	char    m_seoDebug;
	char    m_useSeoResultsCache;
	char    m_doSiteClustering;
	char    m_doIpClustering;
	char    m_doDupContentRemoval;
	char    m_restrictPosdbForQuery;
	char    m_addToCache;
	char    m_familyFilter;
	char    m_getDocIdScoringInfo;
	char    m_realMaxTop;
	char    m_stripe;
	char    m_useQueryStopWords;
	char    m_useNewAlgo;
	char    m_doMaxScoreAlgo;

	char    m_getSectionStats;
	long    m_siteHash32;// for m_getSectionStats

	char    m_useMinAlgo;
	char    m_fastIntersection;

	long long m_minDocId;
	long long m_maxDocId;
	bool      m_makeReply;

	// msg3a stuff
	long    m_timeout; // in seconds

	time_t  m_nowUTC;

	char   *ptr_readSizes;
	char   *ptr_termFreqWeights;
	char   *ptr_query; // in utf8?
	char   *ptr_coll;
	
	long    size_readSizes;
	long    size_termFreqWeights;
	long    size_query;
	long    size_coll;

	char    m_buf[0];
};


class Msg39Reply {

public:

	// zero ourselves out
	void reset() { memset ( (char *)this,0,sizeof(Msg39Reply) ); };

	long   m_numDocIds;
	// # of "unignored" query terms
	long   m_nqt;
	// # of estimated hits we had
	long   m_estimatedHits;
	// for when m_getSectionStats is true
	SectionStats m_sectionStats;
	// error code
	long   m_errno;

	char  *ptr_docIds         ; // the results, long long
	char  *ptr_scores;        ; // floats
	char  *ptr_scoreInfo      ; // transparency info
	char  *ptr_pairScoreBuf   ; // transparency info
	char  *ptr_singleScoreBuf ; // transparency info
	char  *ptr_siteHashList   ; // for m_getSectionStats
	char  *ptr_clusterRecs    ; // key_t (might be empty)
	
	long   size_docIds;
	long   size_scores;
	long   size_scoreInfo;
	long   size_pairScoreBuf  ;
	long   size_singleScoreBuf;
	long   size_siteHashList;
	long   size_clusterRecs;

	// . this is the "string buffer" and it is a variable size
	// . this whole class is cast to a udp reply, so the size of "buf"
	//   depends on the size of that udp reply
	char       m_buf[0];
};



class Msg39 {

 public:

	Msg39();
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
	bool gotLists ( bool updateReadInfo ) ;
	// this is called after thread exits, or if thread creation failed
	bool addedLists();

	// incoming parameters passed to Msg39::getDocIds() function
	//void       *m_state;
	//void      (* m_callback ) ( void *state );

	// . this is used by handler to reconstruct the incoming Query class
	// . TODO: have a serialize/deserialize for Query class
	Query       m_tmpq;

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

	long m_numDocIdSplits;
	bool m_allocedTree;
	long long m_ddd;
	long long m_dddEnd;
	bool doDocIdSplitLoop();

	// . we hold our IndexLists here for passing to PosdbTable
	// . one array for each of the tiers
	IndexList  m_lists [ MAX_QUERY_TERMS ];
	
	// used for timing
	long long  m_startTime;

	// this is set if PosdbTable::addLists() had an error
	long       m_errno;

	// always use top tree now
	TopTree    m_tt;

	char       m_boolFlag;

	long       m_firstResultNum;

	long long  m_numTotalHits;

	long       m_numCensored;

	// for indexdb splitting
	char      m_paritySplit;

	long        m_bufSize;
	char       *m_buf;
	long long  *m_clusterDocIds;
	char       *m_clusterLevels;
	key_t      *m_clusterRecs;
	long        m_numClusterDocIds;
	long        m_numVisible;
	long        m_numDocIds;
	Msg51       m_msg51;
	bool        m_gotClusterRecs;
	void        estimateHits   ();
	bool        setClusterRecs ();
	void        gotClusterRecs ();

	// hack stuff
	void *m_tmp;
	long  m_tmp2;
	bool  m_blocked;
	void (*m_callback)( void *state );
	void  *m_state;
	long long m_topDocId;
	float     m_topScore;
	long long m_topDocId2;
	float     m_topScore2;

	// . for the top 50 algo in seo.cpp
	// . will be the score of the last result if < 50 results
	float     m_topScore50;
	long long m_topDocId50;

	bool  m_inUse;
};		

#endif
