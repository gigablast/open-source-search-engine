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
			long long *docIds               ,
			long       numRecs              ,
			long       maxDocIdsPerHostname ,
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
	// . we just store the "long" part of the cluster rec
	bool getClusterRecs ( long long     *docIds                   ,
			      char          *clusterLevels            ,
			      key_t         *clusterRecs              ,
			      long           numDocIds                ,
			      char          *coll                     ,
			      long           maxCacheAge              ,
			      bool           addToCache               ,
			      void          *state                    ,
			      void        (* callback)( void *state ) ,
			      long           niceness                 ,
			      // output to clusterRecs[]
			      bool           isDebug                  ) ;

	// see Clusterdb.h for this bitmap. we store the lower 64 bits of
	// the clusterdb key into the "clusterRecs" array
	bool isFamilyBitOn ( uint64_t clusterRec ) {
		return g_clusterdb.hasAdultContent((char *)&clusterRec); };
	char     getLangId     ( uint64_t clusterRec ) {
		return g_clusterdb.getLanguage((char *)&clusterRec); };
	unsigned long getSiteHash26   ( uint64_t clusterRec ) {
		return g_clusterdb.getSiteHash26((char *)&clusterRec); };


        key_t getClusterRec ( int32_t i ) { return m_clusterRecs[i]; };

	/*
	bool isDocIdVisible ( long i ) {
		if ( m_clusterRecs[i] == 0     ) return false;
		if ( m_clusterRecs[i] <= CR_OK ) return true;
		return false;
	};
	*/

	bool sendRequests   ( long k );
	bool sendRequest    ( long i );

	void gotClusterRec  ( class Msg0 *msg0 );

	// docIds we're getting clusterRecs for
	long long   *m_docIds;
	long         m_numDocIds;

	// the lower 64 bits of each cluster rec
	key_t      *m_clusterRecs;
	char       *m_clusterLevels;
	long        m_clusterRecsSize;

	void     (*m_callback ) ( void *state );
	void      *m_state;

	// next cluster rec # to get (for m_docIds[m_nexti])
	long      m_nexti;
	// so we don't re-get cluster recs we got last call
	long      m_firsti;

	// use to get the cluster recs
	long       m_numRequests;
	long       m_numReplies;
	long       m_errno;

	long       m_niceness;

	long       m_firstNode;
	long       m_nextNode;

	char      *m_coll;
	long       m_collLen;
	
	// cache info
	long       m_maxCacheAge;
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
