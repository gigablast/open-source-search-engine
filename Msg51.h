// Matt Wells, copyright Jun 2007

// . gets the clusterRecs for a list of docIds
// . list of docids can be from an IndexList if provided, or a straightup array
// . meant as a replacement for some of Msg38
// . see Clusterdb.h for fomat of clusterRec
// . actually only stores the lower 64 bits of each cluster rec, that is all
//   that is interesting

#ifndef _MSG51_H_
#define _MSG51_H_

#include "Msg0.h"
#include "Clusterdb.h"
#include "IndexList.h"
#include "Msg5.h"

// how many Msg0 requests can we launch at the same time?
#define MSG51_MAX_REQUESTS 60

// . m_clusterLevels[i] takes on one of these values
// . these describe a docid
// . they tell us why the docid is not ok to be displayed in the search results
// . this is used as part of the post query filtering step, after we get the
//   resulting docids from Msg3a.
// . these are set some in Msg51.cpp but mostly in Msg40.cpp
enum {
	// if clusterdb rec was not found...
	CR_NOTFOUND        = 0 ,
	// clusterdb rec never set... how did this happen?
	CR_UNINIT          ,
	// we got the clusterdb rec, this is a transistional value.
	CR_GOT_REC         ,
	// had adult content
	CR_DIRTY           ,
	// language did not match the language filter (iff langFilter>0)
	CR_BAD_LANG        ,
	// a 3rd+ result from the same hostname
	CR_CLUSTERED       ,
	// has xml tag syntax in the url (was 12)
	CR_BAD_URL         ,
	// the url is banned in tagdb or url filters table
	CR_BANNED_URL      ,
	// the url is missing query terms (BIG HACK)
	CR_MISSING_TERMS   ,
	// error getting summary (Msg20::m_errno is set)
	CR_ERROR_SUMMARY   ,
	// a summary dup of a higher-scoring result
	CR_DUP_SUMMARY     ,
	// for events...
	CR_MERGED_SUMMARY     ,
	// a gigabit vector dup
	CR_DUP_TOPIC       ,
	// another error getting it... could be one of many
        CR_ERROR_CLUSTERDB , 
        // the url is a dup of a previous url (wiki pages capitalization)
	CR_DUP_URL         ,  // 14

	// these are for buzzlogic (buzz)
	//CR_BELOWMINDATE    ,
	//CR_ABOVEMAXDATE    ,
	//CR_BELOWMININLINKS ,
	//CR_ABOVEMAXINLINKS ,

	// . subset of the CR_OK (visible) results are "wasted" titlerec lookup
	// . only used for stats by Msg40.cpp/Stats.cpp
	CR_WASTED          ,
	// the docid is ok to display!
	CR_OK              , // 16

	// from a blacklisted site hash
	CR_BLACKLISTED_SITE  ,
	// was filtered because of ruleset
	CR_RULESET_FILTERED ,

	// verify this is LAST entry cuz we use i<CR_END for ending for-loops
	CR_END
};

// define in Msg51.cpp
extern char *g_crStrings[];

bool setClusterLevels ( key_t     *clusterRecs          ,
			int64_t *docIds               ,
			int32_t       numRecs              ,
			int32_t       maxDocIdsPerHostname ,
			bool       doHostnameClustering ,
			bool       familyFilter         ,
			char       langFilter           ,
			// blacklisted sites
			//char      *negativeSiteHashes   ,
			bool       isDebug              ,
			// output to clusterLevels[]
			char      *clusterLevels        );

class Msg51 {

 public:

	Msg51();
	~Msg51();
	void reset();

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . we just store the "int32_t" part of the cluster rec
	bool getClusterRecs ( int64_t     *docIds                   ,
			      char          *clusterLevels            ,
			      key_t         *clusterRecs              ,
			      int32_t           numDocIds                ,
			      //char          *coll                     ,
			      collnum_t collnum ,
			      int32_t           maxCacheAge              ,
			      bool           addToCache               ,
			      void          *state                    ,
			      void        (* callback)( void *state ) ,
			      int32_t           niceness                 ,
			      // output to clusterRecs[]
			      bool           isDebug                  ) ;

	// see Clusterdb.h for this bitmap. we store the lower 64 bits of
	// the clusterdb key into the "clusterRecs" array
	bool isFamilyBitOn ( uint64_t clusterRec ) {
		return g_clusterdb.hasAdultContent((char *)&clusterRec); };
	char     getLangId     ( uint64_t clusterRec ) {
		return g_clusterdb.getLanguage((char *)&clusterRec); };
	uint32_t getSiteHash26   ( uint64_t clusterRec ) {
		return g_clusterdb.getSiteHash26((char *)&clusterRec); };


        key_t getClusterRec ( int32_t i ) { return m_clusterRecs[i]; };

	/*
	bool isDocIdVisible ( int32_t i ) {
		if ( m_clusterRecs[i] == 0     ) return false;
		if ( m_clusterRecs[i] <= CR_OK ) return true;
		return false;
	};
	*/

	bool sendRequests   ( int32_t k );
	bool sendRequest    ( int32_t i );

	void gotClusterRec  ( class Msg0 *msg0 );

	// docIds we're getting clusterRecs for
	int64_t   *m_docIds;
	int32_t         m_numDocIds;

	// the lower 64 bits of each cluster rec
	key_t      *m_clusterRecs;
	char       *m_clusterLevels;
	int32_t        m_clusterRecsSize;

	void     (*m_callback ) ( void *state );
	void      *m_state;

	// next cluster rec # to get (for m_docIds[m_nexti])
	int32_t      m_nexti;
	// so we don't re-get cluster recs we got last call
	int32_t      m_firsti;

	// use to get the cluster recs
	int32_t       m_numRequests;
	int32_t       m_numReplies;
	int32_t       m_errno;

	int32_t       m_niceness;

	int32_t       m_firstNode;
	int32_t       m_nextNode;

	//char      *m_coll;
	//int32_t       m_collLen;
	collnum_t m_collnum;
	
	// cache info
	int32_t       m_maxCacheAge;
	bool       m_addToCache;

	bool       m_isDebug;

	// for super quick disk page cache lookups
	//Msg5       m_msg5;

	Msg0       m_msg0  [ MSG51_MAX_REQUESTS ];
	RdbList    m_lists [ MSG51_MAX_REQUESTS ];
	Msg5       m_msg5  [ MSG51_MAX_REQUESTS ];
};

extern RdbCache s_clusterdbQuickCache;

#endif
