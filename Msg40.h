// Matt Wells, copyright Jul 2001

// . gets the title/summary/docLen/url results from a query

#ifndef _MSG40_H_
#define _MSG40_H_

#define SAMPLE_VECTOR_SIZE (32*4)

#include "SearchInput.h"
#include "UdpServer.h"  // UdpSlot type
#include "Multicast.h"  // multicast send
#include "Query.h"      // Query::set()
#include "Msg39.h"      // getTermFreqs()
#include "Msg20.h"      // for getting summary from docId
#include "Msg17.h"      // a distributed cache of serialized/compressed Msg40s
//#include "Msg2b.h"      // for generating directories
//#include "IndexReadInfo.h" // STAGE0,...
#include "Msg3a.h"
#include "PostQueryRerank.h"

// replace CollectionRec::m_maxDocIdsToCompute with this
//#define MAXDOCIDSTOCOMPUTE 500000
// make it 2B now. no reason not too limit it so low.
#define MAXDOCIDSTOCOMPUTE 2000000000

#define MAX_GIGABIT_WORDS 10

class Gigabit {
public:
	char *m_term;
	int32_t  m_termLen;
	int64_t m_termId64;
	float m_gbscore;
	int32_t m_minPop;
	int32_t m_numWords;
	int32_t  m_numPages;
	int64_t m_lastDocId;
	// the wordids of the words in the gigabit (m_numWords of them)
	int64_t m_wordIds[MAX_GIGABIT_WORDS];
};


//
// TODO: add Gigabit::m_firstFastFactOffset..
//


#define MAX_GIGABIT_PTRS 10

class Fact {
public:
	// offset of the gigabit in m_gigabitBuf we belong to
	int32_t  m_gigabitOffset;
	// . the sentence contaning the gigabit and a lot of the query terms
	// . ptr refrences into Msg20Reply::ptr_gigabitSample buffers
	char *m_fact;
	int32_t  m_factLen;
	float m_gigabitModScore;
	float m_queryScore;
	float m_maxGigabitModScore; // gigabitscore * #pagesItIsOn
	int32_t  m_numGigabits;
	char m_printed;
	class Gigabit *m_gigabitPtrs[MAX_GIGABIT_PTRS];
	int32_t  m_numQTerms;
	int64_t m_docId; // from where it came
	Msg20Reply *m_reply; // reply from where it came
	// for deduping sentences
	char  m_dedupVector[SAMPLE_VECTOR_SIZE]; // 128
};


class GigabitInfo {
 public:
        int32_t       m_pts;
        uint32_t   m_hash;
        int32_t       m_pop;
        int32_t       m_count;
        int32_t       m_numDocs;
        int64_t  m_lastDocId;
        int32_t       m_currentDocCount;
        char      *m_ptr;
        int32_t       m_len;
};


class Msg40 {

 public:

	Msg40();
	~Msg40();
	void resetBuf2 ( ) ;
	static bool registerHandler ();

        // . returns false if blocked, true otherwise
        // . sets errno on error
	// . uses Query class to parse query
	// . uses Msg37 to retrieve term frequencies for each termId in query
	// . uses Indexdb class to intersect the lists to get results
	// . fills local buffer, m_docIds, with resulting docIds
	// . set m_numDocIds to number of docIds in m_docIds
	// . a useCache of -1 means default, 1 means use the cache,0 means dont
	// . "displayMetas" is a space separated list of meta tag names
	//   that you want the content for along with the summary
        bool getResults ( class SearchInput *si ,
			  bool               forward ,
			  void              *state    ,
			  //void (* callback)(class Msg40 *THIS, void *state));
			  void             (* callback)(void *state));

	void makeCallback();
	bool gotCacheReply();
	// a continuation function of getResults() above
	bool prepareToGetDocIds ( );
	bool getDocIds ( bool recall );
	bool gotExternalReply ( ) ;
	bool postResultsProcessing();

	bool computeGigabits( class TopicGroup *tg );
	SafeBuf m_gigabitBuf;

	// nuggabits...
	bool computeFastFacts ( );
	bool addFacts ( HashTableX *queryTable,
			HashTableX *gbitTable ,
			char *pstart,
			char *pend,
			bool debugGigabits ,
			class Msg20Reply *reply,
			SafeBuf *factBuf ) ;

	SafeBuf m_factBuf;

	// keep these public since called by wrapper functions
	bool federatedLoop ( ) ;
	bool gotDocIds        ( ) ;
	bool launchMsg20s     ( bool recalled ) ;
	class Msg20 *getAvailMsg20();
	class Msg20 *getCompletedSummary ( int32_t ix );
	bool getSummaries     ( ) ;
	bool gotSummary       ( ) ;
	bool reallocMsg20Buf ( ) ;
	//bool printLocalTime ( class SafeBuf *sb );
	void uncluster ( int32_t m ) ;
	// serialization routines used for caching Msg40s by Msg17
	int32_t  getStoredSize ( ) ;
	int32_t  serialize     ( char *buf , int32_t bufLen ) ;
	int32_t  deserialize   ( char *buf , int32_t bufLen ) ;


	// see Msg51.h for CR_* values of crId
	int32_t getFilterStats ( int32_t crId ) { return m_filterStats[crId]; };
	int32_t getNumCensored         ( ) { return m_filterStats[CR_DIRTY]; };

	int32_t getNumTopicGroups      ( ) { return m_si->m_numTopicGroups; };

	// . estimated # of total hits
	// . this is now an EXACT count... since we read all posdb termlists
	int64_t getNumTotalHits (){return m_msg3a.m_numTotalEstimatedHits; }

	// . we copy query and coll to our own local buffer
	// . these routines give us back our inputted parameters we saved
	char *getQuery              ( ) { return m_si->m_q.getQuery(); };
	int32_t  getQueryLen           ( ) { return m_si->m_q.getQueryLen(); };
	//char *getColl               ( ) { return m_si->m_coll2; };
	//int32_t  getCollLen            ( ) { return m_si->m_collLen2; };
	int32_t  getDocsWanted         ( ) { return m_si->m_docsWanted; };
	int32_t  getFirstResultNum     ( ) { return m_si->m_firstResultNum; };

	int32_t  getNumResults (        ){return m_msg3a.m_numDocIds; };
	int32_t  getNumDocIds  (        ){return m_msg3a.m_numDocIds; };

	char   getClusterLevel(int32_t i){return m_msg3a.m_clusterLevels[i];};

	int64_t getDocId  ( int32_t i ){return m_msg3a.m_docIds[i]; };
	int64_t *getDocIds(        ){return m_msg3a.m_docIds; };
	double  getScore  ( int32_t i ){return m_msg3a.m_scores[i]; };
	class DocIdScore *getScoreInfo(int32_t i){
		if ( ! m_msg3a.m_scoreInfos ) return NULL;
		return m_msg3a.m_scoreInfos[i];
	}
	//LinkInfo *getLinkInfo( int32_t i){return m_msg20[i]->m_linkInfo; }
	bool  moreResultsFollow ( )   {return m_moreToCome; };
	time_t getCachedTime ( )      {return m_cachedTime; };

	/*
	char *getTopicPtr   ( int32_t i ){return m_gigabitInfos[i].m_ptr; };
	int32_t  getTopicLen   ( int32_t i ){return m_gigabitInfos[i].m_len; };
	int32_t  getTopicScore ( int32_t i ){return m_gigabitInfos[i].m_pts; };
	char  getTopicGid   ( int32_t i ){return 0; }; // temporarily
	int32_t  getNumTopics  (        ){return m_numGigabitInfos; };
	// advanced gigabit/topic attributes
	int32_t getTopicDocIdCount(int32_t i){return m_gigabitInfos[i].m_numDocs; };
	int32_t       getTopicPop(int32_t  i){return m_gigabitInfos[i].m_pop; };
	// intersectGigabits() in Msg40.cpp fills these in when we call it
	// from Msg40.cpp
	GigabitInfo m_gigabitInfos[50];
	int32_t m_numGigabitInfos;
	*/

	int32_t getNumGigabits (){return m_gigabitBuf.length()/sizeof(Gigabit);};
	Gigabit *getGigabit ( int32_t i ) {
		Gigabit *gbs = (Gigabit *)m_gigabitBuf.getBufStart();
		return &gbs[i];
	};

        int64_t *getDocIdPtr() { return m_msg3a.m_docIds; }

	// Msg39 and all Msg20s must use the same clock timestamp
	time_t m_nowUTC;

	int32_t m_lastHeartbeat;

	bool printSearchResult9 ( int32_t ix , int32_t *numPrintedSoFar ,
				  class Msg20Reply *mr ) ;

	SafeBuf m_unusedBuf;
	int32_t m_numMsg20sOut ;
	int32_t m_numMsg20sIn  ;
	int32_t m_j ;
	int32_t m_i ;
	bool m_doneWithLookup;
	HashTableX m_facetTextTable;
	SafeBuf m_facetTextBuf;
	bool m_calledFacets;
	int32_t m_omitCount;

	bool printFacetTables ( class SafeBuf *sb ) ;
	int32_t printFacetsForTable ( SafeBuf *sb , QueryTerm *qt );
	bool lookupFacets ( ) ;
	void lookupFacets2 ( ) ;
	void gotFacetText ( class Msg20 *msg20 ) ;
	class Msg20 *getUnusedMsg20 ( ) ;


	HashTableX m_columnTable;
	bool printCSVHeaderRow ( class SafeBuf *sb );
	bool printJsonItemInCSV ( class State0 *st , int32_t ix );
	int32_t m_numCSVColumns;


	HashTableX m_dedupTable;

	int32_t m_msg3aRecallCnt;
	// this goes into msg3a now so we can send multiple msg3as out,
	// 1 per collection
	//Msg39Request m_r;

	int32_t       m_docsToGet;
	int32_t       m_docsToGetVisible;

	// incoming parameters 
	void       *m_state;
	void      (* m_callback ) ( void *state );

	int32_t m_needFirstReplies;

	// max outstanding msg20s
	//int32_t       m_maxOutstanding;

	// # of contiguous msg20 replies we have received (no gaps)
	//int32_t       m_numContiguous;
	// of thos contiguous results, how many are visible? (unfiltered,.etc)
	//int32_t       m_visibleContiguous;

	// . do not uncluster more than this many docids! it slows things down.
	// . kind of a HACK until we do it right
	int32_t       m_unclusterCount;

	// how many of the m_numContiguous have been checked for dups?
	//int32_t       m_numChecked;

	// do we have enough visible docids? stop launch msg20s when we do
	//bool       m_gotEnough;

	// a bunch of msg20's for getting summaries/titles/...
	Msg20    **m_msg20; 
	int32_t       m_numMsg20s;

	char      *m_msg20StartBuf;
	int32_t       m_numToFree;

	bool m_hadPrintError ;
	int32_t m_numPrinted    ;
	bool m_printedHeader ;
	bool m_printedTail   ;
	bool m_lastChunk     ;
	int32_t m_sendsOut      ;
	int32_t m_sendsIn       ;
	int32_t m_printi        ;
	int32_t m_numDisplayed  ;
	int32_t m_numPrintedSoFar;
	int32_t m_socketHadError;


	// use msg3a to get docIds
	Msg3a      m_msg3a;

	// use this for getting compressed, cached images of ourselves
	Msg17      m_msg17;
	char      *m_cachePtr;
	int32_t       m_cacheSize;

	//int32_t m_maxDocIdsToCompute;

	// count summary replies (msg20 replies) we get
	int32_t       m_numRequests;
	int32_t       m_numReplies;

	// we launched all docids from 0 to m_maxiLaunched
	//int32_t       m_maxiLaunched;

	// true if more results follow these
	bool       m_moreToCome;

	int32_t m_lastProcessedi;

	bool m_didSummarySkip;

	// a multicast class to send the request
	Multicast  m_mcast;

	// for timing how long to get all summaries
	int64_t  m_startTime;

	// was Msg40 cached? if so, at what time?
	bool       m_cachedResults;
	time_t     m_cachedTime;

	// gigabits
	//Msg24 m_msg24;

	// references
	//Msg1a m_msg1a;
	
	int32_t m_tasksRemaining;

	int32_t m_printCount;

	// buffer we deserialize from, allocated by Msg17, but we free it
	char *m_buf;
	int32_t  m_bufMaxSize;

	// for holding the msg20s
	char *m_buf2;
	int32_t  m_bufMaxSize2;

	int32_t  m_errno;

	// was family filter on and query had dirty words?
	bool m_queryCensored;

	// did we have dups in the list of docids that we had to remove?
	bool m_removedDupContent;

	// up to 30 different CR_ values in Msg51.h
	int32_t       m_filterStats[30];

	SearchInput   *m_si;


	// for topic clustering, saved from CollectionRec
	int32_t       m_topicSimilarCutoff;
	int32_t       m_docsToScanForTopics;

	// Msg2b for generating a directory
	//Msg2b  m_msg2b;

	bool mergeDocIdsIntoBaseMsg3a();
	int32_t m_numCollsToSearch;
	class Msg3a **m_msg3aPtrs;
	SafeBuf m_msg3aPtrBuf;
	int32_t m_num3aRequests;
	int32_t m_num3aReplies;
	collnum_t m_firstCollnum;

	PostQueryRerank m_postQueryRerank;

        HashTableT<uint64_t, uint64_t> m_urlTable;
};		

#endif
