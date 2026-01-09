// Matt Wells, copyright Aug 2001

// . calls for intersecting a bunch of IndexLists to generate docIds
// . IndexLists are data-less lists (keys only)
// . each key in an IndexList is a termId/score/adultBit/docId tuple
// . we try to use as small a sublist of each IndexList as possible to avoid
//   wasting network bandwidth
// . TODO: split into 2+ classes

// TODO: implement site clustering??????? in getNumResults()

// TODO: if we have in cache we can hash right into the table, but
//       we must do that before blocking on something in case it disappears
//       from the cache

// TODO: it is possible to get a better scoring result, even if we found
//       10 docIds in the heads of ALL the IndexLists. Because it may have
//       a really high score in 3 of the IndexLists, but a low score in the
//       fourth, but it's sum may be the highest of all docIds.

// TODO: the search "cell phone cable hp jornada 680", w/o quotes, should
//       be quote forced anyway. Some pages will match all but "cable hp"
//       so we should break that up into it's 2 terms, cable and hp

// 6912 (30%) of queries are 1 word queries  138
// 7825 (34%) of queries are 2 word queries  156
// 4512 (20%) of queries are 3 word queries  90
// 1771 ( 8%) of queries are 4 word queries  32
// 869  ( 4%) of queries are 5 word queries  17
// 391  ( 2%) of queries are 6 word queries  8
// 290  ( 1%) of queries are 7 word queries  6
// 183  ( 1%) of queries are 8 word queries (4 per second)

#ifndef _INDEXTABLE_H_
#define _INDEXTABLE_H_

#include "Query.h"         // MAX_QUERY_TERMS, qvec_t
#include "Indexdb.h"       // makeStartKey(), getTruncationLimit()
#include "IndexList.h"     // for m_lists[]
#include "Titledb.h"       // g_titledb.getTotalNumDocs()
#include "IndexReadInfo.h" // MAX_TIERS

// max # search results that can be viewed
#define MAX_RESULTS 1000

class IndexTable {

 public:

	// . returns false on error and sets errno
	// . we now support multiple plus signs before the query term
	// . start/endTermNums apply to phrase termIds only
	// . allows us to set multiple bits when a phrase termId is matched
	//   in case the singleton was truncated, but doc has the phrase
	// . if you want Default AND behaviour set requireAllTerms to true
	//   it is much faster, too
	void init (Query *q,bool isDebug,void *logstate,bool requireAllTerms,
		   class TopTree *topTree );

	// has init already been called?
	bool isInitialized ( ) { return m_initialized; };

	// sets m_positiveBits, etc.
	//void prepareToAddLists ( );

	// . returns false on error and sets errno
	// . we assume there are "m_numTerms" lists passed in (see set() above)
	void addLists_r ( IndexList   lists[MAX_TIERS][MAX_QUERY_TERMS] ,
			  int32_t        numTiers        ,
			  int32_t        numListsPerTier ,
			  Query      *q               ,
			  int32_t        docsWanted      ,
			  int32_t       *totalListSizes  ,
			  bool        useDateLists    ,
			  bool        sortByDate      ,
			  float       sortByDateWeight );

	// . these are set from calling addLists() above
	// . we log all matching topDocIds if isDebug is true
	int64_t *getTopDocIds ( int32_t tier ) { return m_topDocIds[tier]; };

	unsigned char *getTopBitScores ( int32_t tier ) 
		{ return m_topBitScores[tier]; };

	char *getTopExplicits ( int32_t tier ) { return m_topExplicits[tier]; };

	int32_t      *getTopScores ( int32_t tier ) { return m_topScores[tier]; };
	//uint32_t *getTopBitScores () { return m_finalTopBitScores; };

	// make sure to call getTopDocIds() before calling this
	int32_t   getNumTopDocIds ( int32_t tier ) { return m_numTopDocIds[tier]; };

	// . get how many results we have in the topDocIds list
	// . if "thatIncludeAllTerms" is true, results must have all terms
	//   from all indexLists that we haven't read ALL of yet
	int32_t getNumExactExplicitMatches ( int32_t tier ) { 
		return m_numExactExplicitMatches[tier];};

	int32_t getNumExactImplicitMatches ( int32_t tier ) { 
		return m_numExactImplicitMatches[tier];};

	// some generic stuff
	 IndexTable();
	~IndexTable();
	void reset();

	// . call to set the m_final* member vars from the m_top* member vars
	// . ALWAYS call this BEFORE calling 
	//   getTopDocIds(), getNumTopDocIds() or getNumExactMatches()
	void filterTopDocIds ( ) ;

	// how long to add the last batch of lists
	int64_t       m_addListsTime;
	uint32_t   m_totalDocIds;
	int32_t            m_numPanics;
	int32_t            m_numCollisions;
	int32_t            m_numPtrs; // in the beginning at least
	int32_t            m_numLoops;

	// how long to get top docIds
	int64_t       m_setTopDocIdsTime;

	int64_t       m_estimatedTotalHits;

	int32_t            m_numSlots;

	// Msg39 needs to call these
	void freeMem ( ) ;
	bool alloc (IndexList lists[MAX_TIERS][MAX_QUERY_TERMS],
		    int32_t      numTiers                  ,
		    int32_t      numListsPerTier           ,
		    int32_t      docsWanted                ,
		    bool      sortByDate                );

	bool        doRecall() { return m_doRecall; };

	int32_t getNumDocsInTier ( int32_t i ) { return m_numDocsInTier[i]; };

	// . sets m_scoreWeights[] based on termFreqs (IDF)
	void setScoreWeights ( Query *q );
	void setScoreWeights ( Query *q , bool phrase );
	int32_t *getScoreWeights ( ) { return m_scoreWeights; };

 private:

	void addLists2_r ( IndexList   lists[MAX_TIERS][MAX_QUERY_TERMS] ,
			   int32_t        numTiers        ,
			   int32_t        numListsPerTier ,
			   Query      *q               ,
			   int32_t        docsWanted      ,
			   int32_t       *imap            ,
			   bool        lastRound       ,
			   int32_t        numBaseLists    ,
			   bool        useDateLists    ,
			   bool        sortByDate      ,
			   float       sortByDateWeight,
			   int32_t       *minHardCountPtr );

	void hashTopDocIds2 ( uint32_t  *maxDocId     ,
			      char          **docIdPtrs    ,
			      int32_t           *scores       ,
			      qvec_t         *explicitBits ,
			      int16_t          *hardCounts   ,
			      uint32_t   mask         ,
			      int32_t            numSlots     ) ;


	// . used for getting which topDocId to kick out of the top list
	int32_t getWeakestTopDocId ( char          **topp         ,
				  int32_t           *tops         ,
				  unsigned char  *topb         ,
				  int32_t            numTop       ,
				  unsigned char  *minBitScore2 ,
				  int32_t           *score        ,
				  char          **docIdPtr     ) ;

	// . get the termBits for the termId represented by this list
	// . only phrases may set multiple bits
	qvec_t getTermImplicitBitMask_r ( int32_t i );

	// . set the m_bitScores[] array
	// . "count" is the # of query term (single or phrase) bit combinations
	void setBitScores ( int32_t count );

	// are lists swapped?
	bool m_swapped [ MAX_TIERS ] [ MAX_QUERY_TERMS ] ;

	// these describe the lists associated with each m_termId
	int32_t       m_scoreWeights  [ MAX_QUERY_TERMS ];

	// for each tier we have a list of the top docids
	int64_t       m_topDocIds       [ MAX_TIERS ] [ MAX_RESULTS ];
	char           *m_topDocIdPtrs    [ MAX_TIERS ] [ MAX_RESULTS ];
	int32_t            m_topScores       [ MAX_TIERS ] [ MAX_RESULTS ];
	//int16_t           m_topHardCounts   [ MAX_TIERS ] [ MAX_RESULTS ];
	unsigned char   m_topBitScores    [ MAX_TIERS ] [ MAX_RESULTS ];
	char            m_topExplicits    [ MAX_TIERS ] [ MAX_RESULTS ];
	int32_t            m_numTopDocIds    [ MAX_TIERS ] ;
	int32_t            m_numExactExplicitMatches [ MAX_TIERS ];
	int32_t            m_numExactImplicitMatches [ MAX_TIERS ];
	int32_t            m_numTiers;

	// when filterTopDocIds() is called it uniquifies and combines
	// m_topDocIds[*][] into m_finalDocIds
	/*	int64_t       m_finalTopDocIds    [ MAX_RESULTS ];
	int32_t            m_finalTopScores    [ MAX_RESULTS ];
	//uint32_t   m_finalTopBitScores [ MAX_RESULTS ];
	int32_t            m_finalNumExactExplicitMatches ;
	int32_t            m_finalNumExactImplicitMatches ;
	int32_t            m_finalNumTopDocIds ;*/

	// a reference to the query
	Query          *m_q;

	// has init() been called?
	bool            m_initialized;

	// are we in debug mode?
	bool            m_isDebug;

	// for debug msgs
	int32_t            m_logstate;

	// . did we already call m_q->setBitScores() for this query?
	// . don't call it more than once
	bool            m_alreadySet;

	bool            m_doRecalc;

	bool             m_requireAllTerms;
	char           **m_topDocIdPtrs2;
	int32_t            *m_topScores2;
	qvec_t          *m_topExplicits2;
	int16_t           *m_topHardCounts2;
	int32_t             m_maxTopDocIds2;
	int32_t             m_numTopDocIds2;
	int32_t             m_nexti;
	int32_t             m_oldnexti;
	bool             m_doRecall;

	// allocated memory
	char *m_buf;
	int32_t  m_bufSize;
	char *m_bufMiddle;

	// for large hashtable for sortByDate
	char *m_bigBuf;
	int32_t  m_bigBufSize;

	// the imap stuff
	int32_t m_imap      [ MAX_QUERY_TERMS ];
	int32_t m_sizes     [ MAX_QUERY_TERMS ];
	int32_t m_blocksize [ MAX_QUERY_TERMS ];
	int32_t m_nb;

	class TopTree *m_topTree;

	// these are for removing component lists replaced by their compounds
	int32_t *m_componentCodes;
	//char       *m_ignore;
	//bool        m_scoresSet;
	int32_t  m_numDocsInTier [ MAX_TIERS ] ;
};

// . get the LOWEST scoring docId from our list of top docIds
// . set "minBitScore22" and "score" for that lowest docId
// . inline this for speed
// . BUT lower docIds are considered higher scoring than higher docIds
inline int32_t IndexTable::getWeakestTopDocId ( char          **topp         ,
					     int32_t           *tops         ,
					     unsigned char  *topb         ,
					     int32_t            numTop       ,
					     unsigned char  *minBitScore2 ,
					     int32_t           *score        ,
					     char          **docIdPtr     ) {
	int64_t      tmp         = 0LL;
	int32_t           minScore    = 0x7fffffff;
	unsigned char  minBitScore = 0xff;
	char          *minDocIdPtr = (char *)&tmp; 
	int32_t           mini        = 0;
	for ( int32_t i = 0 ; i < numTop ; i++ ) {
		if ( topb [i] > minBitScore ) continue;
		if ( topb [i] < minBitScore ) goto gotIt;
		if ( tops [i] > minScore    ) continue;
		if ( tops [i] < minScore    ) goto gotIt;
		if ( *(uint32_t *)(topp[i]+1  )  < 
		     *(uint32_t *)(minDocIdPtr+1)    ) continue;
		if ( *(uint32_t *)(topp[i]+1  )  > 
		     *(uint32_t *)(minDocIdPtr+1)    ) goto gotIt;
		if ( (*(unsigned char *)(topp[i]    ) & 0xfc) <
		     (*(unsigned char *)(minDocIdPtr) & 0xfc) ) continue;
		// ties should not be happening for docid, unless
		// it tied with initial setting of minDocIdPtr, in that
		// case we should add it!
	gotIt:
		minScore     = tops [i];
		minBitScore  = topb [i];
		minDocIdPtr  = topp [i];
		mini         = i;
	}
	// set the callers ptrs
	*minBitScore2 = minBitScore;
	*score        = minScore;
	*docIdPtr     = minDocIdPtr;
	// return the lowest scoring docId's position
	return mini;
}

#endif
