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
#include "Msg2b.h"      // for generating directories
#include "IndexReadInfo.h" // STAGE0,...
#include "Msg3a.h"
#include "PostQueryRerank.h"

// replace CollectionRec::m_maxDocIdsToCompute with this
#define MAXDOCIDSTOCOMPUTE 500000

#define MAX_GIGABIT_WORDS 10

class Gigabit {
public:
	char *m_term;
	long  m_termLen;
	long long m_termId64;
	float m_gbscore;
	long m_minPop;
	long m_numWords;
	long  m_numPages;
	long long m_lastDocId;
	// the wordids of the words in the gigabit (m_numWords of them)
	long long m_wordIds[MAX_GIGABIT_WORDS];
};


//
// TODO: add Gigabit::m_firstFastFactOffset..
//


#define MAX_GIGABIT_PTRS 10

class Fact {
public:
	// offset of the gigabit in m_gigabitBuf we belong to
	long  m_gigabitOffset;
	// . the sentence contaning the gigabit and a lot of the query terms
	// . ptr refrences into Msg20Reply::ptr_gigabitSample buffers
	char *m_fact;
	long  m_factLen;
	float m_gigabitModScore;
	float m_queryScore;
	float m_maxGigabitModScore; // gigabitscore * #pagesItIsOn
	long  m_numGigabits;
	char m_printed;
	class Gigabit *m_gigabitPtrs[MAX_GIGABIT_PTRS];
	long  m_numQTerms;
	long long m_docId; // from where it came
	Msg20Reply *m_reply; // reply from where it came
	// for deduping sentences
	char  m_dedupVector[SAMPLE_VECTOR_SIZE]; // 128
};


class GigabitInfo {
 public:
        long       m_pts;
        uint32_t   m_hash;
        long       m_pop;
        long       m_count;
        long       m_numDocs;
        long long  m_lastDocId;
        long       m_currentDocCount;
        char      *m_ptr;
        long       m_len;
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

	bool gotCacheReply();
	// a continuation function of getResults() above
	bool prepareToGetDocIds ( );
	bool getDocIds ( bool recall );
	bool gotExternalReply ( ) ;
	bool postResultsProcessing();

	bool computeGigabits( class TopicGroup *tg );
	SafeBuf m_gigabitBuf;

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
	bool gotDocIds        ( ) ;
	bool launchMsg20s     ( bool recalled ) ;
	bool getSummaries     ( ) ;
	bool gotSummary       ( ) ;
	bool reallocMsg20Buf ( ) ;
	//bool printLocalTime ( class SafeBuf *sb );
	void uncluster ( long m ) ;
	// serialization routines used for caching Msg40s by Msg17
	long  getStoredSize ( ) ;
	long  serialize     ( char *buf , long bufLen ) ;
	long  deserialize   ( char *buf , long bufLen ) ;


	// see Msg51.h for CR_* values of crId
	long getFilterStats ( long crId ) { return m_filterStats[crId]; };
	long getNumCensored         ( ) { return m_filterStats[CR_DIRTY]; };

	long getNumTopicGroups      ( ) { return m_si->m_numTopicGroups; };

	// . estimated # of total hits
	// . this is now an EXACT count... since we read all posdb termlists
	long long getNumTotalHits (){return m_msg3a.m_numTotalEstimatedHits; }

	// . we copy query and coll to our own local buffer
	// . these routines give us back our inputted parameters we saved
	char *getQuery              ( ) { return m_si->m_q->getQuery(); };
	long  getQueryLen           ( ) { return m_si->m_q->getQueryLen(); };
	char *getColl               ( ) { return m_si->m_coll2; };
	long  getCollLen            ( ) { return m_si->m_collLen2; };
	long  getDocsWanted         ( ) { return m_si->m_docsWanted; };
	long  getFirstResultNum     ( ) { return m_si->m_firstResultNum; };

	long  getNumResults (        ){return m_msg3a.m_numDocIds; };
	long  getNumDocIds  (        ){return m_msg3a.m_numDocIds; };

	char   getClusterLevel(long i){return m_msg3a.m_clusterLevels[i];};

	long long getDocId  ( long i ){return m_msg3a.m_docIds[i]; };
	long long *getDocIds(        ){return m_msg3a.m_docIds; };
	float  getScore  ( long i ){return m_msg3a.m_scores[i]; };
	class DocIdScore *getScoreInfo(long i){return m_msg3a.m_scoreInfos[i];}
	//LinkInfo *getLinkInfo( long i){return m_msg20[i]->m_linkInfo; }
	bool  moreResultsFollow ( )   {return m_moreToCome; };
	time_t getCachedTime ( )      {return m_cachedTime; };

	/*
	char *getTopicPtr   ( long i ){return m_gigabitInfos[i].m_ptr; };
	long  getTopicLen   ( long i ){return m_gigabitInfos[i].m_len; };
	long  getTopicScore ( long i ){return m_gigabitInfos[i].m_pts; };
	char  getTopicGid   ( long i ){return 0; }; // temporarily
	long  getNumTopics  (        ){return m_numGigabitInfos; };
	// advanced gigabit/topic attributes
	long getTopicDocIdCount(long i){return m_gigabitInfos[i].m_numDocs; };
	long       getTopicPop(long  i){return m_gigabitInfos[i].m_pop; };
	// intersectGigabits() in Msg40.cpp fills these in when we call it
	// from Msg40.cpp
	GigabitInfo m_gigabitInfos[50];
	long m_numGigabitInfos;
	*/

	long getNumGigabits (){return m_gigabitBuf.length()/sizeof(Gigabit);};
	Gigabit *getGigabit ( long i ) {
		Gigabit *gbs = (Gigabit *)m_gigabitBuf.getBufStart();
		return &gbs[i];
	};

        long long *getDocIdPtr() { return m_msg3a.m_docIds; }

	// Msg39 and all Msg20s must use the same clock timestamp
	time_t m_nowUTC;

	long m_msg3aRecallCnt;
	Msg39Request m_r;

	long       m_docsToGet;
	long       m_docsToGetVisible;

	// incoming parameters 
	void       *m_state;
	void      (* m_callback ) ( void *state );
	
	// max outstanding msg20s
	//long       m_maxOutstanding;

	// # of contiguous msg20 replies we have received (no gaps)
	//long       m_numContiguous;
	// of thos contiguous results, how many are visible? (unfiltered,.etc)
	//long       m_visibleContiguous;

	// . do not uncluster more than this many docids! it slows things down.
	// . kind of a HACK until we do it right
	long       m_unclusterCount;

	// how many of the m_numContiguous have been checked for dups?
	//long       m_numChecked;

	// do we have enough visible docids? stop launch msg20s when we do
	//bool       m_gotEnough;

	// a bunch of msg20's for getting summaries/titles/...
	Msg20    **m_msg20; 
	long       m_numMsg20s;

	char      *m_msg20StartBuf;
	long       m_numToFree;

	// use msg3a to get docIds
	Msg3a      m_msg3a;

	// use this for getting compressed, cached images of ourselves
	Msg17      m_msg17;
	char      *m_cachePtr;
	long       m_cacheSize;

	//long m_maxDocIdsToCompute;

	// count summary replies (msg20 replies) we get
	long       m_numRequests;
	long       m_numReplies;

	// we launched all docids from 0 to m_maxiLaunched
	//long       m_maxiLaunched;

	// true if more results follow these
	bool       m_moreToCome;

	long m_lastProcessedi;

	// a multicast class to send the request
	Multicast  m_mcast;

	// for timing how long to get all summaries
	long long  m_startTime;

	// was Msg40 cached? if so, at what time?
	bool       m_cachedResults;
	time_t     m_cachedTime;

	// gigabits
	//Msg24 m_msg24;

	// references
	//Msg1a m_msg1a;
	
	long m_tasksRemaining;


	// buffer we deserialize from, allocated by Msg17, but we free it
	char *m_buf;
	long  m_bufMaxSize;

	// for holding the msg20s
	char *m_buf2;
	long  m_bufMaxSize2;

	long  m_errno;

	// was family filter on and query had dirty words?
	bool m_queryCensored;

	// did we have dups in the list of docids that we had to remove?
	bool m_removedDupContent;

	// up to 30 different CR_ values in Msg51.h
	long       m_filterStats[30];

	SearchInput   *m_si;


	// for topic clustering, saved from CollectionRec
	long       m_topicSimilarCutoff;
	long       m_docsToScanForTopics;

	// Msg2b for generating a directory
	Msg2b  m_msg2b;

	PostQueryRerank m_postQueryRerank;

        HashTableT<uint64_t, uint64_t> m_urlTable;
};		

#endif
