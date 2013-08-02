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
			  long        numTiers        ,
			  long        numListsPerTier ,
			  Query      *q               ,
			  long        docsWanted      ,
			  long       *totalListSizes  ,
			  bool        useDateLists    ,
			  bool        sortByDate      ,
			  float       sortByDateWeight );

	// . these are set from calling addLists() above
	// . we log all matching topDocIds if isDebug is true
	long long *getTopDocIds ( long tier ) { return m_topDocIds[tier]; };

	unsigned char *getTopBitScores ( long tier ) 
		{ return m_topBitScores[tier]; };

	char *getTopExplicits ( long tier ) { return m_topExplicits[tier]; };

	long      *getTopScores ( long tier ) { return m_topScores[tier]; };
	//unsigned long *getTopBitScores () { return m_finalTopBitScores; };

	// make sure to call getTopDocIds() before calling this
	long   getNumTopDocIds ( long tier ) { return m_numTopDocIds[tier]; };

	// . get how many results we have in the topDocIds list
	// . if "thatIncludeAllTerms" is true, results must have all terms
	//   from all indexLists that we haven't read ALL of yet
	long getNumExactExplicitMatches ( long tier ) { 
		return m_numExactExplicitMatches[tier];};

	long getNumExactImplicitMatches ( long tier ) { 
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
	long long       m_addListsTime;
	unsigned long   m_totalDocIds;
	long            m_numPanics;
	long            m_numCollisions;
	long            m_numPtrs; // in the beginning at least
	long            m_numLoops;

	// how long to get top docIds
	long long       m_setTopDocIdsTime;

	long long       m_estimatedTotalHits;

	long            m_numSlots;

	// Msg39 needs to call these
	void freeMem ( ) ;
	bool alloc (IndexList lists[MAX_TIERS][MAX_QUERY_TERMS],
		    long      numTiers                  ,
		    long      numListsPerTier           ,
		    long      docsWanted                ,
		    bool      sortByDate                );

	bool        doRecall() { return m_doRecall; };

	long getNumDocsInTier ( long i ) { return m_numDocsInTier[i]; };

	// . sets m_scoreWeights[] based on termFreqs (IDF)
	void setScoreWeights ( Query *q );
	void setScoreWeights ( Query *q , bool phrase );
	long *getScoreWeights ( ) { return m_scoreWeights; };

 private:

	void addLists2_r ( IndexList   lists[MAX_TIERS][MAX_QUERY_TERMS] ,
			   long        numTiers        ,
			   long        numListsPerTier ,
			   Query      *q               ,
			   long        docsWanted      ,
			   long       *imap            ,
			   bool        lastRound       ,
			   long        numBaseLists    ,
			   bool        useDateLists    ,
			   bool        sortByDate      ,
			   float       sortByDateWeight,
			   long       *minHardCountPtr );

	void hashTopDocIds2 ( unsigned long  *maxDocId     ,
			      char          **docIdPtrs    ,
			      long           *scores       ,
			      qvec_t         *explicitBits ,
			      short          *hardCounts   ,
			      unsigned long   mask         ,
			      long            numSlots     ) ;


	// . used for getting which topDocId to kick out of the top list
	long getWeakestTopDocId ( char          **topp         ,
				  long           *tops         ,
				  unsigned char  *topb         ,
				  long            numTop       ,
				  unsigned char  *minBitScore2 ,
				  long           *score        ,
				  char          **docIdPtr     ) ;

	// . get the termBits for the termId represented by this list
	// . only phrases may set multiple bits
	qvec_t getTermImplicitBitMask_r ( long i );

	// . set the m_bitScores[] array
	// . "count" is the # of query term (single or phrase) bit combinations
	void setBitScores ( long count );

	// are lists swapped?
	bool m_swapped [ MAX_TIERS ] [ MAX_QUERY_TERMS ] ;

	// these describe the lists associated with each m_termId
	long       m_scoreWeights  [ MAX_QUERY_TERMS ];

	// for each tier we have a list of the top docids
	long long       m_topDocIds       [ MAX_TIERS ] [ MAX_RESULTS ];
	char           *m_topDocIdPtrs    [ MAX_TIERS ] [ MAX_RESULTS ];
	long            m_topScores       [ MAX_TIERS ] [ MAX_RESULTS ];
	//short           m_topHardCounts   [ MAX_TIERS ] [ MAX_RESULTS ];
	unsigned char   m_topBitScores    [ MAX_TIERS ] [ MAX_RESULTS ];
	char            m_topExplicits    [ MAX_TIERS ] [ MAX_RESULTS ];
	long            m_numTopDocIds    [ MAX_TIERS ] ;
	long            m_numExactExplicitMatches [ MAX_TIERS ];
	long            m_numExactImplicitMatches [ MAX_TIERS ];
	long            m_numTiers;

	// when filterTopDocIds() is called it uniquifies and combines
	// m_topDocIds[*][] into m_finalDocIds
	/*	long long       m_finalTopDocIds    [ MAX_RESULTS ];
	long            m_finalTopScores    [ MAX_RESULTS ];
	//unsigned long   m_finalTopBitScores [ MAX_RESULTS ];
	long            m_finalNumExactExplicitMatches ;
	long            m_finalNumExactImplicitMatches ;
	long            m_finalNumTopDocIds ;*/

	// a reference to the query
	Query          *m_q;

	// has init() been called?
	bool            m_initialized;

	// are we in debug mode?
	bool            m_isDebug;

	// for debug msgs
	long            m_logstate;

	// . did we already call m_q->setBitScores() for this query?
	// . don't call it more than once
	bool            m_alreadySet;

	bool            m_doRecalc;

	bool             m_requireAllTerms;
	char           **m_topDocIdPtrs2;
	long            *m_topScores2;
	qvec_t          *m_topExplicits2;
	short           *m_topHardCounts2;
	long             m_maxTopDocIds2;
	long             m_numTopDocIds2;
	long             m_nexti;
	long             m_oldnexti;
	bool             m_doRecall;

	// allocated memory
	char *m_buf;
	long  m_bufSize;
	char *m_bufMiddle;

	// for large hashtable for sortByDate
	char *m_bigBuf;
	long  m_bigBufSize;

	// the imap stuff
	long m_imap      [ MAX_QUERY_TERMS ];
	long m_sizes     [ MAX_QUERY_TERMS ];
	long m_blocksize [ MAX_QUERY_TERMS ];
	long m_nb;

	class TopTree *m_topTree;

	// these are for removing component lists replaced by their compounds
	long *m_componentCodes;
	//char       *m_ignore;
	//bool        m_scoresSet;
	long  m_numDocsInTier [ MAX_TIERS ] ;
};

// . get the LOWEST scoring docId from our list of top docIds
// . set "minBitScore22" and "score" for that lowest docId
// . inline this for speed
// . BUT lower docIds are considered higher scoring than higher docIds
inline long IndexTable::getWeakestTopDocId ( char          **topp         ,
					     long           *tops         ,
					     unsigned char  *topb         ,
					     long            numTop       ,
					     unsigned char  *minBitScore2 ,
					     long           *score        ,
					     char          **docIdPtr     ) {
	long long      tmp         = 0LL;
	long           minScore    = 0x7fffffff;
	unsigned char  minBitScore = 0xff;
	char          *minDocIdPtr = (char *)&tmp; 
	long           mini        = 0;
	for ( long i = 0 ; i < numTop ; i++ ) {
		if ( topb [i] > minBitScore ) continue;
		if ( topb [i] < minBitScore ) goto gotIt;
		if ( tops [i] > minScore    ) continue;
		if ( tops [i] < minScore    ) goto gotIt;
		if ( *(unsigned long *)(topp[i]+1  )  < 
		     *(unsigned long *)(minDocIdPtr+1)    ) continue;
		if ( *(unsigned long *)(topp[i]+1  )  > 
		     *(unsigned long *)(minDocIdPtr+1)    ) goto gotIt;
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
