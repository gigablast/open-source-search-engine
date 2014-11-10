// Matt Wells, copyright Jul 2001

// . get many RdbLists at once
// . used by ../indexdb/Msg39.cpp to get many IndexLists (RdbLists)

#ifndef _MSG2_H_
#define _MSG2_H_

#include "Query.h"  // MAX_QUERY_TERMS
#include "Msg0.h"

// define the max # of lists you can get as the max # of query terms for now
#define MAX_NUM_LISTS MAX_QUERY_TERMS

// launch up to 25 msg0 requests at a time
//#define MSG2_MAX_REQUESTS 25

// how many outstanding msg5 requests at one time?
#define MSG2_MAX_REQUESTS MAX_QUERY_TERMS

// support the &sites=xyz.com+abc.com+... to restrict search results to
// provided sites.
#define MAX_WHITELISTS 500

class Msg2 {

 public:

	Msg2();
	void reset();

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "termIds/termFreqs" should NOT be on the stack in case we block
	bool getLists ( int32_t     rdbId       ,
			collnum_t collnum ,//char    *coll        ,
			int32_t     maxAge      ,
			bool     addToCache  ,
			//key_t   *startKeys   ,
			//key_t   *endKeys     ,
			//char    *startKeys   ,
			//char    *endKeys     ,
			//class QueryTerm *qterms ,
			class Query *query ,
			// restrict search results to this list of sites,
			// i.e. "abc.com+xyz.com+..." (Custom Search)
			char *whiteList,
			// for intersecting ranges of docids separately
			// to prevent OOM errors
			int64_t docIdStart,
			int64_t docIdEnd,
			// isSplit[i] is true if list #i is split.
			// i.e. gbdom:xyz.com, etc.
			//char    *isSplit , 
			int32_t    *minRecSizes ,
			//int32_t     numLists    ,
			RdbList *lists       ,
			void    *state       ,
			void (* callback)(void *state )  ,
			class Msg39Request *request ,
			int32_t     niceness = MAX_NICENESS ,
			bool     doMerge  = true         ,
			bool     isDebug  = false        ,
			int32_t    *bestSenderHostIds = NULL    ,
			bool     restrictPosdb   = false   ,
			char     forceParitySplit     = -1   ,
			bool     checkCache           = false);
	bool getLists();

	int32_t  m_i;

	// list of sites to restrict search results to. space separated
	char *m_whiteList;
	int64_t m_docIdStart;
	int64_t m_docIdEnd;
	char *m_p;
	int32_t  m_w;
	RdbList m_whiteLists [ MAX_WHITELISTS ];

	// for posdbtable to get lists
	//int32_t getNumListGroups ( ) { return m_query->m_numTerms; }

	// . get each list group
	// . lists are from oldest to newest
	//RdbList **getListGroup       ( int32_t i ){return m_msg5[i].m_listPtrs;}
	//int32_t   getNumListsInGroup ( int32_t i ){return m_msg5[i].m_numListPtrs;}


	RdbList *getList ( int32_t i ) { return &m_lists[i]; }
	int32_t getNumLists ( ) { return m_query->m_numTerms; }

	// get how many bytes we read
	//int32_t getTotalRead() { return m_totalRead; };

	class Msg5 *getAvailMsg5();
	void returnMsg5 ( class Msg5 *msg5 ) ;

	// leave public so C wrapper can call
	bool gotList ( RdbList *list );

	// we can get up to MAX_QUERY_TERMS term frequencies at the same time
	Msg5 m_msg5  [ MSG2_MAX_REQUESTS ];
	//Msg0 m_msg0  [ MSG2_MAX_REQUESTS ];
	bool m_avail [ MSG2_MAX_REQUESTS ]; // which msg0s are available?

	int32_t m_errno;

	RdbList *m_lists;

	//char      m_inProgress [ MAX_NUM_LISTS ];
	//char      m_slotNum    [ MAX_NUM_LISTS ];

	// used for getting component lists if compound list is empty
	void     mergeLists_r       ( ) ;
	int32_t    *m_componentCodes;
	char    *m_ignore;
	class Query *m_query;
	class QueryTerm *m_qterms;
	//char     m_cacheKeys[MAX_NUM_LISTS * MAX_KEY_BYTES];
	int32_t    *m_minRecSizes;
	int32_t     m_maxAge;
	int32_t     m_numLists;
	bool     m_getComponents;
	char     m_rdbId;
	bool     m_addToCache;
	//char    *m_coll;
	collnum_t m_collnum;
	bool     m_restrictPosdb;
	int32_t     m_compoundListMaxSize;
	char     m_forceParitySplit;
	bool     m_checkCache;
	int32_t     m_k;
	int32_t     m_n;
	//RdbList *m_listPtrs [ MAX_NUM_LISTS ];

	class Msg39Request *m_req;
	
	// true if its a compound list that needs to be inserted into the cache
	//char m_needsCaching [ MAX_NUM_LISTS ];

	int32_t m_numReplies  ;
	int32_t m_numRequests ;

	void *m_state ;
	void ( * m_callback ) ( void *state );
	int32_t  m_niceness;

	// . should lists from cache, tree and disk files be merged into one?
	// . or appended out of order? 
	// . avoiding merge saves query engine valuable time
	bool m_doMerge;

	// if this is true we log more output
	bool m_isDebug;

	// keep a count of bytes read from all lists (local or remote)
	//int32_t m_totalRead;

	// start time
	int64_t m_startTime;
};

#endif
