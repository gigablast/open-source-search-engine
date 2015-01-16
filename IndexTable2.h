// Matt Wells, copyright Aug 2001

// . calls for intersecting a bunch of IndexLists to generate docIds
// . IndexLists are data-less lists (keys only)
// . each key in an IndexList is a termId/score/adultBit/docId tuple
// . we try to use as small a sublist of each IndexList as possible to avoid
//   wasting network bandwidth

// TODO: implement site clustering??????? in getNumResults()

// TODO: if we have in cache we can hash right into the table, but
//       we must do that before blocking on something in case it disappears
//       from the cache

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

#ifndef _INDEXTABLE2_H_
#define _INDEXTABLE2_H_

#include "Query.h"         // MAX_QUERY_TERMS, qvec_t
#include "Indexdb.h"       // makeStartKey(), getTruncationLimit()
#include "IndexList.h"     // for m_lists[]
#include "HashTableT.h"

// the final score of a docid
typedef uint32_t score_t;
//typedef float score_t;

// . get the docid from the ptr
// . works for both docid ptrs from m_topTree and m_topDocIdPtrs[]
inline int64_t getDocIdFromPtr ( char *docIdPtr ) {
	int64_t d;
	gbmemcpy ( &d , docIdPtr , 6 );
	d >>= 2;
	d &= DOCID_MASK;
	return d;
};

// max # search results that can be viewed without using TopTree
#define MAX_RESULTS 1000

class IndexTable2 {

 public:

	// . returns false on error and sets errno
	// . we now support multiple plus signs before the query term
	// . start/endTermNums apply to phrase termIds only
	// . allows us to set multiple bits when a phrase termId is matched
	//   in case the singleton was truncated, but doc has the phrase
	// . if you want Default AND behaviour set requireAllTerms to true
	//   it is much faster, too
	// . "termFreqs" are 1-1 with q->m_qterms[]
	// . sets m_q to point to q
	void init (Query         *q               ,
		   bool           isDebug         ,
		   void          *logstate        ,
		   bool           requireAllTerms ,
		   class TopTree *topTree         ,
		   char          *coll            ,
		   IndexList     *lists           ,
		   int32_t           numLists        ,
		   HashTableX    *sortByDateTablePtr ,
		   int32_t           docsWanted      ,
		   int64_t       *termFreqs       ,
		   bool           useDateLists    ,
		   bool           sortByDate      ,
		   char           sortBy          , //1=time,2=dist,3=rel,4=pop
		   bool           showExpiredEvents ,
		   float          userLat         ,
		   float          userLon         ,
		   bool           doInnerLoopSiteClustering ,
		   bool           doSiteClustering          ,
		   bool           getWeights                ,
		   class          Msg39Request *r           );

	// pre-allocate memory since intersection runs in a thread
	bool alloc ( );

	bool allocTopTree ( );

	bool makeHashTables ( ) ;

	// . returns false on error and sets errno
	// . we assume there are "m_numTerms" lists passed in (see set() above)
	void addLists_r ( int32_t       *totalListSizes   ,
			  float       sortByDateWeight );

	// some generic stuff
	IndexTable2();
	~IndexTable2();
	void reset();

	// Msg39 needs to call these
	void freeMem ( ) ;

	// sets m_affWeights and m_freqWeights from the provided weights
	// in the Msg39Request.
	void setAffWeights ( Msg39Request *r ) ;

	bool recompute ( class Msg39Request *r ) ;

	bool cacheIntersectionForRecompute ( class Msg39Request *r ) ;

	void freeCacheRec ( int32_t i ) ;

	// sets m_freqWeights[] based on termFreqs (IDF) in IMAP space
	void   setFreqWeights ( Query *q , bool phrase );

	// computes a single final score from a score vector
	score_t getWeightedScore ( unsigned char  *scoresVec   ,
				   int32_t            nqt         ,
				   float          *freqWeights ,
				   float          *affWeights  ,
				   bool            requireAllTerms );

	void addLists2_r ( int32_t        numLists         ,
			   int32_t       *imap             ,
			   bool        lastRound        ,
			   int32_t        numBaseLists     ,
			   float       sortByDateWeight ,
			   int32_t       *minHardCountPtr  );

	bool eventHashLoop ( int32_t *listIndexes ,
				  char *listSigns   ,
				  qvec_t *listExplicitBits ,
				  char **listEnds ,
				  char *listHash ,
				  char *listHardCount ,
				  int32_t *listPoints ,
				  char **ptrs  ,
				  char **ptrEnds ,
				  char **oldPtrs ,
				  int32_t  numPtrs ,
				  int64_t maxDocId ,
				  int32_t      numListsToDo ,
				  int32_t      numSlots ,
				  char **docIdPtrs ,
				  qvec_t *explicitBits ,
				  int16_t *hardCounts ,
			     uint8_t  *eventIds ,
			     uint8_t *scoresVec ,
			     uint32_t *latVec,
			     uint32_t *lonVec,
			     //uint32_t *timeVec,
			     //uint32_t *endVec,
			     int32_t      nqt ) ;

	void hashTmpDocIds2 ( uint32_t  *maxDocId     ,
			      char          **docIdPtrs    ,
			      unsigned char  *scoresVec    ,
			      uint8_t        *eventIds     ,
			      uint32_t       *latVec       ,
			      uint32_t      *lonVec       ,
			      //uint32_t       *timeVec      ,
			      //uint32_t       *endVec       ,
			      qvec_t         *explicitBits ,
			      int16_t          *hardCounts   ,
			      uint32_t   mask         ,
			      int32_t            numSlots     ,
			      int32_t            nqt          ) ;


	// . used for getting which topDocId to kick out of the top list
	int32_t getWeakestTopDocId ( char          **topp         ,
				  score_t        *tops         ,
				  unsigned char  *topb         ,
				  int32_t            numTop       ,
				  unsigned char  *minBitScore2 ,
				  score_t        *score        ,
				  char          **docIdPtr     ) ;

	void zeroOutVectorComponents ( unsigned char *scoresVec  ,
				       qvec_t        *ebits      ,
				       int16_t         *hardCounts ,
				       int32_t           numDocIds  ,
				       char           rat        ) ;

	// . compute phrase and word weights based on phrase affinities
	//   and set final scores based on those weights
	void computeAffWeights    ( bool     rat          ,
				    int32_t     numDocIds    ,
				    char   **docIdPtrs    ,
				    uint8_t *scoresVec    ,
				    qvec_t  *explicitBits ,
				    int16_t   *hardCounts   ,
				    float   *affWeights   ,
				    float   *affinities   );

	//void computeWeightedScores ( int32_t           numDocIds    ,
	//			     int32_t          *finalScores  ,
	//			     unsigned char *scoresVec    ,
	//			     qvec_t        *explicitBits ,
	//			     float         *affWeights   ,
	//			     char         **docIdPtrs    );
	
	// fill top docids with the best from top docids 2
	int32_t fillTopDocIds ( //char         **topp    ,
			     //score_t       *tops    ,
			     //unsigned char *topb    ,
			     int32_t           numTop  ,
			     char         **tmpp2   ,
			     uint8_t       *tmpsv2  ,
			     uint32_t      *tmpdv2  ,
			     uint32_t      *tmplatv2  ,
			     uint32_t      *tmplonv2  ,
			     //uint32_t      *tmptimev2 ,
			     //uint32_t      *tmpendv2 ,
			     qvec_t        *tmpev2  ,
			     int16_t         *tmphc2  ,
			     uint8_t       *tmpeids2,
			     int32_t           numTmp2 );

	// comparison between a result and the min top result
	bool isBetterThanWeakest ( unsigned char bscore         ,
				   score_t       score          ,
				   char         *docIdPtr       ,
				   unsigned char minTopBitScore ,
				   score_t       minTopScore    ,
				   char         *minTopDocIdPtr );

	// has init already been called?
	bool isInitialized ( ) { return m_initialized; };

	// . these are set from calling addLists() above
	// . we log all matching topDocIds if isDebug is true
	char    **getTopDocIdPtrs ( ) { return m_topDocIdPtrs; };
	uint8_t  *getTopBitScores ( ) { return m_topBitScores; };
	score_t  *getTopScores    ( ) { return m_topScores;    };
	int32_t      getNumTopDocIds ( ) { return m_numTopDocIds; };

	// . get how many results we have in the topDocIds list
	// . if "thatIncludeAllTerms" is true, results must have all terms
	//   from all indexLists that we haven't read ALL of yet
	// . these are just used for stats keeping purposes in Msg3a for now
	// . these are all 0 for top tree right now.. until i fix it
	int32_t getNumExactExplicitMatches (){return m_numExactExplicitMatches;};
	int32_t getNumExactImplicitMatches (){return m_numExactImplicitMatches;};

	float *getFreqWeights ( ) { return m_freqWeights; };
	// includes both synonym and phrase affinties combined
	float *getAffWeights  ( ) { return m_affWeights;  };
	float *getAffinities  ( ) { return m_affinities;  };

	float *getFreqWeightsQS ( ) { return m_freqWeightsQS; };
	float *getAffWeightsQS  ( ) { return m_affWeightsQS;  };

	void setStuffFromImap();

	// how long to add the last batch of lists
	int64_t       m_addListsTime;
	int64_t       m_t1 ;
	int64_t       m_t2 ;
	uint32_t   m_totalDocIds;
	int32_t            m_numPanics;
	int32_t            m_numCollisions;
	int32_t            m_numPtrs; // in the beginning at least
	int32_t            m_numLoops;

	int64_t       m_estimatedTotalHits;

	int32_t            m_errno;

	int32_t            m_numSlots;

	char           *m_coll;
	collnum_t       m_collnum;

	// this is true if no use to read more termlist data from disk, we
	// will not get any more docids...
	bool            m_isDiskExhausted;

	// point to array of term freqs, 1-1 with qterms
	int64_t *m_termFreqs;

	// how many docs in the collection?
	int64_t m_docsInColl;

	// . the imap stuff
	// . m_imap is set in Query.cpp, but we contain it here
	// . m_imap really only needed when doing rat=1 queries, but we use it 
	//   for rat=0 because we may filter out UOR'ed QueryTerms or 
	//   irrelevant synoynms
	// . allows us to speed up the intersection process by intersecting 
	//   smaller termlists first and thereby keeping the ongoing result 
	//   set as small as possible
	// . imap maps the query term's INTERSECTION ORDER to the query term 
	//   number in  Query::m_qterms[]. 
	// . so if it is the the ith termlist to intersect, then its 
	//   corresponding QueryTerm would be Query::m_qterms[m_imap[i]]
	// . sometimes query terms are ignored as far as the intersection goes
	//   and we also intersect the term's termlists in a different order
	//   than the term's number in the Query::m_qterms[] array has it
	// . m_blocksize[0] + m_blocksize[1] is how many QueryTerms termlists 
	//   to intersect for the first call to IndexTable2::addLists2_r() for 
	//   rat=1 operations. 
	// . we choose to intersect smaller termlists first to minimize the 
	//   result set and maximize the speed of future intersections, since 
	//   less results are involved
	// . m_blocksize[0]=X and m_blocksize[1]=Y means to intersect 
	//   m_lists[m_imap[0]], m_lists[m_imap[  1]],... m_lists[m_imap[X-1]] 
	//   (block #0)
	//   with
	//   m_lists[m_imap[X]], m_lists[m_imap[X+1]],... m_lists[m_imap[Y-1]] 
	//   (block #1)
	// . m_blocksize[i] can be > 1 because we need to intersect phrase 
	//   termlists when intersecting their constituent word termlists in 
	//   order to salvage docids that may not have the word terms 
	//   explicitly, but do have the phrase terms explicitly, and thereby 
	//   have the word terms implicitly.
	// . m_nb is how many total blocks we have
	// . m_sizes[i] is how many docids are in block #i total... *i think*..
	// . after we intersect block #0 with block #1, further intersections 
	//   are performed by calling addLists2_r() with one block at a time.
	// . addLists2_r() will preserve the ongoing intersection in 
	//   m_topDocIds2[], etc.
	// . when it is called with a single block it hashes the docids in 
	//   m_topDocIds2[] with the provided block of termlists to get the 
	//   new result set which is again stored in m_topDocIds2[], ...
	int32_t m_imap      [ MAX_QUERY_TERMS ];
	int32_t m_ni;
	int32_t m_sizes     [ MAX_QUERY_TERMS ];
	int32_t m_blocksize [ MAX_QUERY_TERMS ];
	int32_t m_nb;
	// maps from query term space to imap space
	int32_t m_revImap   [ MAX_QUERY_TERMS ];
	bool m_imapIsValid;

	// for speed we must map Query::m_qterms[].m_leftPhraseTermNum into 
	// "imap space"
	int32_t m_imapLeftPhraseTermNum  [ MAX_QUERY_TERMS ];
	int32_t m_imapRightPhraseTermNum [ MAX_QUERY_TERMS ];


	// . these describe the lists associated with each m_termId
	// . each query term has a corresponding 
	//   term frequency weight (m_freqWeights[])
	// . each query term has a corresponding 
	//   phrase affinity weight (m_affWeights[])
	// . each query term has a corresponding 
	//   phrase affinity (m_phraseAffinities[][])
	// . these are NOT exactly 1-1 with Query::m_qterms[]
	// . m_freqWeights[i] is the freq weight for m_qterms[m_imap[i]], 
	//   NOT m_qterms[i]
	// . m_imap essentially REORDERS the QueryTerms for better intersection
	//   and may also
	//   remove some QueryTerms (like UOR'ed query terms and synonyms)
	// . "nqt" is used throughout IndexTable2.cpp to indicate number of 
	//   QueryTerms in 
	//   "imap" which is always <= Query::m_numTerms = number of 
	//   Query::m_qterms[]
	float      m_freqWeights [ MAX_QUERY_TERMS ];
	float      m_affWeights  [ MAX_QUERY_TERMS ];
	float      m_affinities  [ MAX_QUERY_TERMS ];
	bool       m_computedAffWeights;
	// these two correlate with the Query::m_qterms[] instead of being 
	// in imap space. we pass these back in the Msg39Reply
	float      m_freqWeightsQS [ MAX_QUERY_TERMS ];
	float      m_affWeightsQS  [ MAX_QUERY_TERMS ];

	// we have a list of the top docids
	char           *m_topDocIdPtrs    [ MAX_RESULTS ];
	score_t         m_topScores       [ MAX_RESULTS ];
	//   0x80: we have all hard-required terms
	//   0x40: we have all other terms explicitly
	//   0x20: we have all other terms implicitly
	// & 0x1f: count of all terms we have implicitly (includes hard counts)
	unsigned char   m_topBitScores    [ MAX_RESULTS ];
	int32_t            m_numTopDocIds    ;

	// if getting more than MAX_RESULTS results, use this top tree to hold
	// them rather than the m_top*[] arrays above
	class TopTree *m_topTree;

	// NSD: Search for 'gigablast' on gk0 gave just 30 results. But when
	// a freshness of 30 days was given it gave over 200k results. That
	// is because we limit the maxDocIdsToCompute, and in the normal case
	// all the results were from gigablast.com and were being clustered out
	// So add a check to limit the number of results from the same
	// 8 bit dom hash having the same score to X (say 100)
	//bool            m_doInnerLoopSiteClustering;

	bool            m_doSiteClustering;

	// justed used by Msg3a for stats tracking
	int32_t            m_numExactExplicitMatches ;
	int32_t            m_numExactImplicitMatches ;

	// a reference to the query
	Query          *m_q;

	bool            m_useDateLists;
	bool            m_sortByDate;

	// for events
	char            m_sortBy;
	bool            m_showExpiredEvents;
	bool            m_showInProgress;
	// pointLat/pointLon multiplied by 10M into an int
	uint32_t        m_userLatIntComp;
	uint32_t        m_userLonIntComp;
	int32_t            m_latTermOff;
	int32_t            m_lonTermOff;
	int32_t            m_timeTermOff;
	int32_t            m_endTermOff;

	// these are NOT in imap space, but in query term space, 1-1 with 
	// Query::m_qterms[]
	IndexList      *m_lists;
	int32_t            m_numLists;

	int32_t            m_docsWanted;

	// has init() been called?
	bool            m_initialized;

	// are we in debug mode?
	bool            m_isDebug;

	// for debug msgs
	int32_t            m_logstate;

	bool            m_doRecalc;

	bool            m_requireAllTerms;

	int64_t       m_numDocsInColl;

	// the current "intersection" is stored in this table
	char          **m_tmpDocIdPtrs2;
	uint32_t       *m_tmpDateVec2;
	uint32_t       *m_tmpLatVec2;
	uint32_t       *m_tmpLonVec2;
	//uint32_t       *m_tmpTimeVec2;
	//uint32_t       *m_tmpEndVec2;
	uint8_t        *m_tmpScoresVec2;
	uint8_t        *m_tmpEventIds2;
	qvec_t         *m_tmpEbitVec2;
	int16_t          *m_tmpHardCounts2;
	int32_t            m_maxTmpDocIds2;
	int32_t            m_numTmpDocIds2;
	int32_t            m_nexti;
	int32_t            m_oldnexti;

	time_t m_nowUTCMod;

	// allocated memory
	char *m_buf;
	int32_t  m_bufSize;
	char *m_bufMiddle;

	// for large hashtable for sortByDate
	//char *m_bigBuf;
	//int32_t  m_bigBufSize;

	// little buffer for the intersection
	char *m_localBuf;
	int32_t  m_localBufSize;

	// used by our call to Query::setBitScoresBoolean()
	//char *m_bitScoresBuf;
	//int32_t  m_bitScoresBufSize;

	// these are for removing component lists replaced by their compounds
	int32_t *m_componentCodes;

	bool m_getWeights;

	bool m_searchingEvents;

	HashTableX *m_sortByDateTablePtr;

	class Msg39Request *m_r;

	// . a hash table of docids
	// . the &sq=docid1+docid2+...+docidN cgi parm can restrict the
	//   search results to this list of docids
	bool m_useYesDocIdTable;
	HashTableT<int64_t,char> m_dt;
	// likewise, exclude any docid in this table
	bool m_useNoDocIdTable;
	HashTableT<int64_t,char> m_et;
	// cache boolean results
	bool m_useBoolTable;
	HashTableT<qvec_t,char> m_bt;

	// . encapsulate bool cache lookup for bitscores
	// . FUCK! addLists2_r() calls this and tries to grow the table
	//   while in a thread!
	inline uint8_t getBitScore(qvec_t ebits){
		if (!m_useBoolTable)
			return m_q->getBitScore(ebits);
		// if table is mostly full do not add anything to it! we are
		// like in a thread
		if ( 100 * (m_bt.m_numSlotsUsed+1) >= m_bt.m_numSlots * 75 )
			return m_q->getBitScore(ebits);
		int32_t slot = -1;
		uint8_t bscore;
		if ((slot = m_bt.getSlot(ebits)) >=0){
			//if (m_isDebug)
			//	logf(LOG_DEBUG, 
			//	     "query: getBitScoreCacheHit "
			//	     "bits=0x%016"XINT64"",
			//	     (int64_t) ebits);
			bscore = m_bt.getValueFromSlot(slot);
		}
		else {
			//if (m_isDebug)
			//	logf(LOG_DEBUG, 
			//	     "query: getBitScoreCacheMiss "
			//	     "bits=0x%016"XINT64"",
			//	     (int64_t) ebits);
			bscore = m_q->getBitScore(ebits);
		}
		// store new bool value
		if (slot < 0){
			//if (m_isDebug)
			//	logf(LOG_DEBUG, 
			//	     "query: getBitScoreCacheAdd "
			//	     "bits=0x%016"XINT64"",
			//	     (int64_t) ebits);
			m_bt.addKey(ebits,bscore,NULL);
		}
		return bscore;
	};

};

// . get the LOWEST scoring docId from our list of top docIds
// . set "minBitScore22" and "score" for that lowest docId
// . inline this for speed
// . BUT lower docIds are considered higher scoring than higher docIds
inline int32_t IndexTable2::getWeakestTopDocId ( char          **topp         ,
					      score_t        *tops         ,
					      unsigned char  *topb         ,
					      int32_t            numTop       ,
					      unsigned char  *minBitScore2 ,
					      score_t        *score        ,
					      char          **docIdPtr     ) {
	int64_t      tmp         = 0LL;
	score_t        minScore    = 0x7fffffff;
	unsigned char  minBitScore = 0xff;
	char          *minDocIdPtr = (char *)&tmp; 
	int32_t           mini        = -1;
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

// . checks a result against the weakest to see which is better
inline bool IndexTable2::isBetterThanWeakest ( unsigned char bscore         ,
					       score_t       score          ,
					       char         *docIdPtr       ,
					       unsigned char minTopBitScore ,
					       score_t       minTopScore    ,
					       char         *minTopDocIdPtr ) {
	// . branch on the bit score
	// . ignore 0x80 | 0x40 bits
	if ( (bscore&~0xc0) < (minTopBitScore&~0xc0) ) return false;
	// automatically add to top if our bscore is the highest so far
	if ( (bscore&~0xc0) > (minTopBitScore&~0xc0) ) return true;
	// docId must have a better score if it tied matching terms
	if ( score < minTopScore ) {
		// clear the slot
		//explicitBits[i] = 0;
		//docIdPtrs[i] = NULL;
		return false;
	}
	if ( score > minTopScore ) return true;
	// continue if docId is too big
	if ( *(uint32_t *)(docIdPtr+1) >
	     *(uint32_t *)(minTopDocIdPtr+1) ) {
		// clear the slot
		//explicitBits[i] = 0;
		//docIdPtrs[i] = NULL;
		return false;
	}
	// if top is equal, compare lower 6 bits
	if ( (*(uint32_t *)(docIdPtr      +1)       ==
	      *(uint32_t *)(minTopDocIdPtr+1))      &&
	     (*(unsigned char *)(docIdPtr      ) & 0xfc) >=
	     (*(unsigned char *)(minTopDocIdPtr) & 0xfc)    ) {
		// clear the slot
		//explicitBits[i] = 0;
		//docIdPtrs[i] = NULL;
		return false;
	}
	return true;
}

inline score_t IndexTable2::getWeightedScore ( unsigned char  *scoresVec   ,
					       int32_t            nqt         ,
					       float          *freqWeights ,
					       float          *affWeights  ,
					       bool           requireAllTerms){
	// get the min of the scores
	float min       = 9999999.0;
	float phraseMin = 9999999.0;
	float pscore;
	float pre;
	int32_t  j;
	// reset score
	float score = 0.0;
	// loop vars
	//unsigned char iscore;
	float         s;
	qvec_t        ebit;
	// tends to neutralize the affects of outliers
	for ( int32_t t = 0; t < nqt; t++ ) {

		// . if this is zero, ignore it completely
		// . we do this for gbxlatitude2: etc.
		//   query terms whose scores are really
		//   eventids
		if ( freqWeights[t] == 0.0 ) continue;

		// invert score
		pre = 255 - scoresVec[t];

		// apply affinity weights
		if ( affWeights[t] > 0.0 ) 
			pre = pre * affWeights[t];

		// get score
		s = freqWeights[t] * 100.0 * pre;

		// add it up
		score += s;

		// look at this term and its corresponding word or phrse 
		// terms and get the max of all. must apply the affWeights!

		int32_t it = m_imap[t];

		// which is the explicit bit for term #t?
		ebit = m_q->m_qterms[it].m_explicitBit;

		// . skip if term #t is not "hard" required
		// . it could be a phrase term or a synonym term and not
		//   necessary
		if ( (ebit & (m_q->m_requiredBits)) == 0 ) {
			// demote if phrase
			//pre /= 30.0;
			// if we are a phrase term, keep the phrase min
			if ( pre < phraseMin ) phraseMin = pre;
			// get next term
			continue;
		}

		// . get the max between us and our phrase terms
		// . do not consider ourselves if phrase term has
		//   higher affinity than us!!
		if ( (j = m_imapLeftPhraseTermNum[t]) >= 0 ) {
			pscore = 255-scoresVec[j] ;
			// if phrase is not really strong, demote it
			//if ( affWeights[j] < affWeights[t] ) pscore /= 30.0;
			if ( affWeights[j] > 0 ) pscore *= affWeights[j];
			if ( pscore > pre ) pre = pscore;
			// we should also weight it by our affinity weight, but
			// punish by .5 since it is a phrase
			pscore = 255-scoresVec[j] ;
			if ( affWeights[t] > 0 ) pscore *= affWeights[t];
			if ( pscore > pre ) pre = pscore;		
		}
		if ( (j = m_imapRightPhraseTermNum[t]) >= 0 ) {
			pscore = 255-scoresVec[j] ;
			// if phrase is not really strong, demote it
			//if ( affWeights[j] < affWeights[t] ) pscore /= 30.0;
			if ( affWeights[j] > 0 ) pscore *= affWeights[j];
			if ( pscore > pre ) pre = pscore;
			// we should also weight it by our affinity weight, but
			// punish by .5 since it is a phrase
			pscore = 255-scoresVec[j] ;
			if ( affWeights[t] > 0 ) pscore *= affWeights[t];
			if ( pscore > pre ) pre = pscore;		
		}

		// we must also consider all of our synonyms!!
		if ( m_q->m_hasSynonyms ) {
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				// skip if term #k is not our synonym
				//if (   m_q->m_qterms[k].m_synonymOf != 
				//       & m_q->m_qterms[it] ) continue;
				// if term #k IMPLIES us, use his score if bigr
				if ( (m_q->m_qterms[k].m_implicitBits &
				      m_q->m_qterms[it].m_explicitBit ) == 0 )
					continue;
				// convert him to imap space
				j = m_imap[k];
				// get his score
				pscore = 255-scoresVec[j] ;
				// . get the scores
				// . 'bib' for the query 'michael bibby fact'
				//   has a syn weight of 0... so allow 0, it
				//   should punish us!
				//if ( affWeights[j] > 0)pscore*=affWeights[j];
				if ( affWeights[j] >= 0) pscore*=affWeights[j];
				//if ( pscore > pre ) pre = pscore;
				// we should also weight it by our affinity 
				// weight,but punish by .5 since it is a phrase
				//pscore = 255-scoresVec[j] ;
				if ( affWeights[t] >= 0) pscore*=affWeights[t];
				if ( pscore > pre ) pre = pscore;	       
			}	
		}

		// do we have a new min score?
		if ( pre >= min ) continue;
		// rat=0 and boolean queries might not have this term!
		// do not allow min to be 0!
		if ( pre <= 0.0 && ! requireAllTerms ) continue;
		// ok, we got a winner
		if ( pre < min ) min = pre;
	}

	// use phrase min if we had no terms explicitly
	if ( min == 9999999.0 ) min = phraseMin;
	// multiply by the min, this rewards non-outliers
	//if ( min > 1 && min != 9999999.0 ) score *= min * min;
	if ( min != 9999999.0 ) score *= min * min;
	//if ( min > 1 && min != 9999999.0 ) score *= min ;
	//if ( min > 1 ) score *= min;
	// scale down
	score /= 100;
	// sanity check
	if ( ((float)((int32_t)(score+1.0))) < score ) { 
		logf(LOG_DEBUG,"query: got score breach, score=%f",score);
		score = (float)0x7ffffff0;
		//char *xx=NULL; *xx = 0;
	}
	// make sure never 0
	return (score_t)(score + 1.0);
}

/*
// . computes a final score from a score vector and term weights
inline int32_t IndexTable2::getWeightedScore ( unsigned char  *scoresVec ,
					    int32_t            nqt ,
					    float          *affWeights ) {
	// tends to neutralize the affects of outliers
	float score = 0.0;
	for ( int32_t t = 0; t < nqt; t++ ) {
		float s = m_freqWeights[t] * 100.0 *(255-scoresVec[t]);
		// let's take this out until we figure out a way
		// to deal with small samples without having
		// to make Msg3a go to the next tier, when the
		// combined samples from each split would be
		// enough to compute an affinity. also, we do not
		// want splits using different affinities!
		if ( affWeights[t] > 0.0 ) 
			s = s * affWeights[t];
		// square root it
		s = sqrt((double)s);
		// do it again
		s = sqrt((double)s);
		// add up
		score += s;
	}
	// average
	float avg = score / nqt;
	// square
	//avg *= avg;
	// scale up for round off
	avg *= 100;
	// never 0
	avg += 1;
	// square
	return (int32_t)avg;
}
*/

#endif
