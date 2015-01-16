#include "gb-include.h"

#include "IndexTable.h"
#include "Stats.h"
#include <math.h>
#include "Conf.h"
#include "Mem.h"        // getHighestLitBitValue()
#include "TopTree.h"
#include "sort.h"

// global var
//TopTree *g_topTree;

// when using Msg39.cpp to call addLists_r() on 2 5MB prefab lists:
// 1.  it takes 100ms just to load the 10MB from mainMem into L1cache.(133MB/s)
// 2.  Then we read AND write 10MB to the hash table in the L1 cache, 
//     which @ 400MB/s takes 25ms + 25ms = 50ms.
// 2b. Also, when hashing 2 millions keys, each key will read and write a 
//     full slot in hash table, so each slot is 10 bytes, that's 20 megs
//     @ 400MB/s which is 50ms + 50ms = 100ms.
// 2c. Then there's chaining, about 10% of docids will chain, 10ms
// 3.  Then we loop 2432 times over the 20k hash table in the L1 cache to
//     get the winners which is reading 49MB @ 400MB/s = 122ms.
// 4.  total mem access times is 382ms
// 5.  so out of the 620ms or so, for intersecting 'the .. sex' 382ms is 
//     just bare memory bottleneck!
// NOTE: i used membustest.cpp to measure L1 cache speed, main mem speed, etc.
//       on lenny


IndexTable::IndexTable() { 
	// top docid info
	m_q             = NULL;
	m_topDocIdPtrs2 = NULL;
	m_topScores2    = NULL;
	m_topExplicits2 = NULL;
	m_topHardCounts2= NULL;
	m_buf           = NULL;
	m_bigBuf        = NULL;
	reset();
}

IndexTable::~IndexTable() { reset(); }

void IndexTable::reset() {
	m_initialized          = false;
	m_numTiers             = 0;
	//m_alreadySet           = false;
	m_estimatedTotalHits   = -1;
	m_doRecall             = true;
	// filterTopDocIds is now in msg3a, and so we need to init some stuff
	// or else it cores when addlists2_r is not executed.
	for ( int32_t i = 0; i < MAX_TIERS; i++ ){
		m_numTopDocIds[i] = 0;
		m_numExactExplicitMatches[i] = 0;
		m_numExactImplicitMatches[i] = 0;
	}
	freeMem();
}

// . max score weight 
// . we base score weight on the termFreq(# of docs that have that term)
// . it is now also based on the # of plus signs prepended before a term
#define MAX_SCORE_WEIGHT 1000

// . HACK: date hack
// . this is a special score weight i added for sorting by date
// . it scales the score of date indexlists
// . score of termIds  0xdadadada and 0xdadadad2 represent date of the document
// . 0xdadadada's score is the most signifincat 8-bits of the 16-bit date and
// . 0xdadadad2's score is the least signifincat
// . the date is stored in days since the epoch (16 bits needed = 2 scores)
// . unfortunately, it may not always work too well
// . the max score any doc can have (w/o date) is about 16,000
// . so when we weight the date by DATE_WEIGHT and add to the doc's score
//   just a 1 second difference will supercede any difference in term scores
#define DATE_WEIGHT  (2 * MAX_QUERY_TERMS * MAX_SCORE_WEIGHT)

// . returns false on error and sets g_errno
// . NOTE: termIds,termFreqs,termSigns are just referenced by us, not copied
// . sets m_startKeys, m_endKeys and m_minNumRecs for each termId
// . TODO: ensure that m_termFreqs[] are all UPPER BOUNDS on the actual #!!
//         we should be able to get an upper bound estimate from the b-tree
//         quickly!
// . we now support multiple plus signs before the query term
void IndexTable::init ( Query *q , bool isDebug , void *logstate ,
			bool requireAllTerms , TopTree *topTree ) {
	// bail if already initialized
	if ( m_initialized ) return;
	// clear everything
	reset();
	// we are now
	m_initialized = true;
	// set debug flag
	m_isDebug = isDebug;

	// list heads are not swapped yet
	for ( int32_t i = 0 ; i < q->getNumTerms() ; i++ ) 
		// loop over all lists in this term's tiers
		for ( int32_t j = 0 ; j < MAX_TIERS ; j++ ) 
			m_swapped[j][i] = false;

	// . are we default AND? it is much faster.
	// . using ~/queries/queries.X.rex on rex:
	// . 81q/s raw=8&sc=0&dr=0&rat=1  sum = 288.0  avg = 0.3  sdev = 0.5
	// . 70q/s raw=8&sc=1&dr=1&rat=1  sum = 185.0  avg = 0.4  sdev = 0.5
	// . -O2                          sum = 204.00 avg = 0.15 sdev = 0.26
	// . 45q/s raw=8&sc=0&dr=0&rat=0  sum = 479.0  avg = 1.1  sdev = 1.1
	// . 38q/s raw=8&sc=1&dr=1&rat=0  sum = 429.0  avg = 1.2  sdev = 1.2
	// . -O2   raw=8&sc=0&dr=0&rat=0  sum = 351.00 avg = 0.60 sdev = 0.69
	// . speed up mostly from not having to look up as many title recs?
	// . the Msg39 reported time to intersect is 4 times higher for rat=0
	// . grep "intersected lists took" rat1 | awk '{print $8}' | add
	// . do this on host0c for speed testing: 
	//   ?q=windows+2000+server+product+key&usecache=0&debug=1&rat=1
	m_requireAllTerms = requireAllTerms;
	// make sure our max score isn't too big
	//int32_t a     = MAX_QUERY_TERMS * MAX_QUERY_TERMS * 300   * 255   + 255;
	//int64_t aa=MAX_QUERY_TERMS * MAX_QUERY_TERMS * 300LL * 255LL + 255;
	//if ( a != aa ) { 
	//log("IndexTable::set: MAX_QUERY_TERMS too big"); exit(-1); }
	// save it
	m_topTree = topTree;
	//g_topTree = topTree;
	// . HACK: we must always get the top 500 results for now
	// . so next results page doesn't have same results as a previous page
	// . TODO: refine this
	// . this is repeated in IndexReadInfo.cpp
	// . HACK: this prevents dups when clicking "Next X Results" link
	//if ( docsWanted < 500 ) docsWanted = 500;
	// remember the query class, it has all the info about the termIds
	m_q          = q;
	// just a int16_t cut
	m_componentCodes = m_q->m_componentCodes;
	// for debug msgs
	m_logstate = (int32_t)logstate;
	// after this many docids for a term, we start dropping some
	//int32_t truncLimit = g_indexdb.getTruncationLimit();
	// . set score weight for each term based on termFreqs
	// . termFreqs is just the max size of an IndexList
	setScoreWeights ( m_q );
	// . HACK: date hack
	// . set termSigns of the special date termIds to 'd' so addList()
	//   knows to throw these scores into the highest tier (just for them)
	// . also re-set the weights so the most significant 8-bits of our
	//   16-bit date is weighted by 256, and the low by 1
	/*
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		if      ( m_q->m_termIds[i] == (0xdadadada & TERMID_MASK) ) {
			m_q->m_termSigns[i] = 'd';
			m_scoreWeights[i] = 256;
		}
		else if ( m_q->m_termIds[i] == (0xdadadad2 & TERMID_MASK) ) {
			m_q->m_termSigns[i] = 'd';
			m_scoreWeights[i] = 1;
		}
	}
	*/
}

// . the score weight is in [1,MAX_SCORE_WEIGHT=1000]
// . so max score would be MAX_QUERY_TERMS*1000*255 = 16*1000*255 = 4,080,000
// . now we use MAX_QUERY_TERMS * MAX_SCORE to be the actual max score so
//   that we can use 4,080,000 for truncated hard-required terms
// . TODO: make adjustable from an xml interpolated point map
void IndexTable::setScoreWeights ( Query *q ) {
	// set weights for phrases and singletons independently
	setScoreWeights ( q , true  );
	setScoreWeights ( q , false );
}

void IndexTable::setScoreWeights ( Query *q , bool phrases ) {
	// get an estimate on # of docs in the database
	//int64_t numDocs = g_titledb.getGlobalNumDocs();
	// this should only be zero if we have 0 docs, so make it 1 if so
	//if ( numDocs <= 0 ) numDocs = 1;
	// . compute the total termfreqs
	// . round small termFreqs up to half a GB_PAGE_SIZE
	double minFreq = 0x7fffffffffffffffLL;
	for ( int32_t i = 0 ; i < q->getNumTerms() ; i++ ) {
		// ignore termIds we should
		//if ( q->m_ignore[i] ) continue;
		// component lists are merged into one compound list
		if ( m_componentCodes[i] >= 0 ) continue;
		// . if we're setting phrases, it must be an UNQUOTED phrase
		// . quoted phrases have a term sign!
		if ( phrases ) {
			if (   q->isInQuotes (i)         ) continue;
			if ( ! q->isPhrase   (i)         ) continue;
			if (   q->getTermSign(i) != '\0' ) continue;
		}
		else if ( ! q->isInQuotes (i)         &&
			    q->isPhrase   (i)         &&
			    q->getTermSign(i) == '\0'  )
				continue;
		// is this the new min?
		if ( q->getTermFreq(i) < minFreq ) minFreq = q->getTermFreq(i);
	}
	// to balance things out don't allow minFreq below "absMin"
	double absMin  = GB_PAGE_SIZE/(2*sizeof(key_t));
	if ( minFreq < absMin ) minFreq = absMin;
	// loop through each term computing the score weight for it
	for ( int32_t i = 0 ; i < q->getNumTerms() ; i++ ) {
		// reserve half the weight for up to 4 plus signs
		//int64_t max = MAX_SCORE_WEIGHT / 3;
		// i eliminated the multi-plus thing
		//int64_t max = MAX_SCORE_WEIGHT ;
		// . 3 extra plusses can triple the score weight
		// . each extra plus adds "extra" to the score weight
		//int32_t extra = (2 * MAX_SCORE_WEIGHT) / 9;
		// add 1/6 for each plus over 1
		//if ( q->m_numPlusses[i] > 0 ) 
		//	max += (q->m_numPlusses[i] - 1) * extra;
		// but don't exceed the absolute max
		//if ( max > MAX_SCORE_WEIGHT ) max = MAX_SCORE_WEIGHT;
		// ignore termIds we should
		//if ( q->m_ignore[i] ) { m_scoreWeights[i] = 0; continue; }
		// component lists are merged into one compound list
		if ( m_componentCodes[i] >= 0 ) continue;
		// if we're setting phrases, it must be a phrase
		if ( phrases ) {
			if (   q->isInQuotes (i)         ) continue;
			if ( ! q->isPhrase   (i)         ) continue;
			if (   q->getTermSign(i) != '\0' ) continue;
		}
		else if ( ! q->isInQuotes (i)         &&
			    q->isPhrase   (i)         &&
			    q->getTermSign(i) == '\0'  )
				continue;
		// increase small term freqs to the minimum
		double freq = q->getTermFreq(i);
		if ( freq < absMin ) freq = absMin;
		// get ratio into [1,inf)
		double ratio1 = 2.71828 * freq / minFreq; // R1
		// . natural log it
		// . gives a x8 whereas log10 would give a x4 for a wide case
		double ratio2 = log ( ratio1 ); // R2
		// square
		// ratio = ratio * ratio;
		// make bigger now for '"warriors of freedom" game' query
		// so it weights 'game' less
		//double ratio3 = pow ( ratio2 , 2.6 ); // R3
		double ratio3 = pow ( ratio2, (double)g_conf.m_queryExp );// R3
		// now invert
		double ratio4 = 1.0 / ratio3; // R4
// Example for 'boots in the uk' query: (GB_PAGE_SIZE is 32k, absMin=1365)
// TERM                df        R1       R2      R3     R4      W1
// boots(184)      -->  7500000  7465     8.9179  295.58 .00338  1.1255
// uk   (207)      --> 78000000  77636    11.2597 541.97 .001845 .6143
// "boots.. uk"(25)-->     2731  2.71828  1.0     1.0    1.0     333
		// don't exceed multiplier
		//if ( ratio < 1.0 / g_conf.m_queryMaxMultiplier )
		//	ratio = 1.0 / g_conf.m_queryMaxMultiplier;
		// get the pure weight
		int32_t weight= (int32_t)(((double)MAX_SCORE_WEIGHT/3) * ratio4);//W1
		// ensure at least 1
		if ( weight < 1 ) weight = 1;

		// don't breech MAX_SCORE
		if ( weight > MAX_SCORE_WEIGHT ) weight = MAX_SCORE_WEIGHT;

		// store it for use by addLists_r
		m_scoreWeights[i] = weight; // (300 - score) + 100 ;
		// . if this is a phrase then give it a boost
		// . NOTE: this might exceed MAX_SCORE_WEIGHT then!!
		if ( q->isPhrase(i) ) 
			m_scoreWeights[i] = (int32_t)
				( ( (float)m_scoreWeights[i]   * 
				  g_conf.m_queryPhraseWeight )/
				100.0 ) ;
				
		// . apply user-defined weights
		// . we add this with completed disregard with date weighting
		QueryTerm *qt = &q->m_qterms[i];
		int64_t w ;
		if ( qt->m_userType == 'r' ) w = (int64_t)m_scoreWeights[i] ;
		else                         w = 1LL;
		w *= (int64_t)qt->m_userWeight;
		// it can be multiplied by up to 256 (the term count)
		int64_t max = 0x7fffffff / 256;
		if ( w > max ) {
		 log("query: Weight breech. Truncating to %"UINT64".",max);
			w = max;
		}
		m_scoreWeights[i] = w;


		// . stop words always get a weight of 1 regardless
		// . we don't usually look them up unless all words in the
		//   query are stop words
		//if ( q->m_isStopWords[i] ) weight = 1;
			
		// log it
		if ( m_isDebug || g_conf.m_logDebugQuery )
			logf(LOG_DEBUG,"query: [%"UINT32"] term #%"INT32" has freq=%"INT64" "
			    "r1=%.3f r2=%.3f r3=%.3f r4=%.3f score weight=%"INT32"",
			     m_logstate,i,q->m_termFreqs[i],
			     ratio1,ratio2,ratio3,ratio4,m_scoreWeights[i]);
	}

}

// doubling this will half the number of loops (m_numLoops) required, but if 
// we hit the L2 cache more then it might not be worth it. TODO: do more tests.
#define RAT_SLOTS (1024)

// . the 1.4Ghz Athlon has a 64k L1 data cache (another 64k for instructions)
// . TODO: put in conf file OR better yet... auto detect it!!!
// . we're like 3 times faster on host0 when L1 cache is 32k instead of 64k
//#define L1_DATA_CACHE_SIZE (64*1024)
//#define L1_DATA_CACHE_SIZE (32*1024)
// this is only 8k on my new pentium 4s!!!
#define L1_DATA_CACHE_SIZE (8*1024)

// . this is in bytes. the minimal size of a cached chunk of mem.
// . error on the large side if you don't know
// . this is 64 bytes for Athlon's from what I've heard
// . this is 64bytes for my new pentium 4s, too
#define L1_CACHE_LINE_SIZE  64

void IndexTable::hashTopDocIds2 ( uint32_t  *maxDocId     ,
				  char          **docIdPtrs    ,
				  int32_t           *scores       ,
				  qvec_t         *explicitBits ,
				  int16_t          *hardCounts   ,
				  uint32_t   mask         ,
				  int32_t            numSlots     ) {
	// if none left to hash, we're done
	if ( m_nexti >= m_numTopDocIds2 ) {
		*maxDocId = (uint32_t)0xffffffff;
		return;
	}
	*maxDocId = 0;
	int32_t maxi = m_nexti + (numSlots >> 1);
	if ( maxi > m_numTopDocIds2 ) maxi = m_numTopDocIds2;
	int32_t oldmaxi = maxi;
	// move maxi down to a maxDocId slot
	while ( maxi > 0 && m_topScores2[maxi-1] != 0 ) maxi--;
	// sometimes, the block can be bigger than numSlots when we 
	// overestimate maxDocIds and the second list hits the first like 80%
	// or more of the time or so
	if ( maxi == 0 || maxi <= m_nexti ) {
		maxi = oldmaxi;
		int32_t bigmax = m_nexti + numSlots;
		if ( bigmax > m_numTopDocIds2 ) bigmax = m_numTopDocIds2;
		// we can equal bigmax if we have exactly m_numSlots(1024)
		// winners!! VERY RARE!! but it happened for me on the query
		// 'https://www.highschoolalumni.com/'. we filled up our hash
		// table exactly so we got m_numSlots winners, and before this
		// was "maxi < bigmax" which stopped int16_t of what we needed.
		// maxi should techincally allowed to equal m_numTopDocIds2
		while ( maxi <= bigmax && m_topScores2[maxi-1] != 0 ) maxi++;
		if ( m_topScores2[maxi-1] != 0 ) { 
			log(LOG_LOGIC,"query: bad top scores2.");
			char *xx = NULL; *xx = 0; }
	}
	// set maxDocId
	*maxDocId = (int32_t)m_topDocIdPtrs2[maxi-1];
	// sanity check
	if ( *maxDocId == 0 ) { 
		log(LOG_LOGIC,"query: bad maxDocId."); 
		char *xx = NULL; *xx = 0; }
	// debug msg
	if ( m_isDebug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: Hashing %"INT32" top docids2, [%"INT32", %"INT32")",
		     maxi-m_nexti,m_nexti,maxi);
	int32_t nn;
	// we use a score of 0 to denote docid blocks
	for ( int32_t i = m_nexti ; i < maxi ; i++ ) {
		// . if score is zero thats a tag block
		// . all the docids before this should be < this max
		if ( m_topScores2[i] == 0 ) continue;
		// hash the top 32 bits of this docid
		nn = (*(uint32_t *)(m_topDocIdPtrs2[i]+1) ) & mask ;
	chain:
		// . if empty, take right away
		// . this is the most common case so we put it first
		if ( docIdPtrs[nn] == NULL ) {
			// hold ptr to our stuff
			docIdPtrs    [ nn ] = m_topDocIdPtrs2[i];
			// store score
			scores       [ nn ] = m_topScores2   [i];
			// now we use the explicitBits!
			explicitBits [ nn ] = m_topExplicits2[i];
			// and hard counts
			hardCounts   [ nn ] = m_topHardCounts2[i];
			// add the next one
			continue;
		}
		// if docIds bits don't match, chain to next bucket
		// since we don't have dups and this is the first list we do
		// not need to check for matching docids
		if ( ++nn >= numSlots ) nn = 0;
		goto chain;
	}
	// save the old one in case of a panic and rollback
	m_oldnexti = m_nexti;
	// advance next i
	m_nexti = maxi;
}

// alloc m_topDocIdPtrs2/m_topScores2
bool IndexTable::alloc (IndexList lists[MAX_TIERS][MAX_QUERY_TERMS],
			int32_t      numTiers                  ,
			int32_t      numListsPerTier           ,
			int32_t      docsWanted                ,
			bool      sortByDate                ) {

	// pre-allocate all the space we need for intersecting the lists
	int64_t need = 0;
	int32_t nqt = numListsPerTier; // number of query terms
	// component lists are merged into compound lists
	nqt -= m_q->getNumComponentTerms();
	int32_t ntt = nqt * MAX_TIERS;
	need += ntt * 256 * sizeof(char *) ; // ptrs
	need += ntt * 256 * sizeof(char *) ; // pstarts
	need += ntt * 256 * sizeof(char *) ; // oldptrs
	need += nqt * 256 * sizeof(int32_t  ) + nqt * sizeof(int32_t *);// scoretbls
	need += ntt       * sizeof(char  ) ; // listSigns
	need += ntt       * sizeof(char  ) ; // listHardCount
	need += ntt       * sizeof(int32_t *) ; // listScoreTablePtrs
	need += ntt       * sizeof(qvec_t) ; // listExplicitBits
	need += ntt       * sizeof(char *) ; // listEnds
	need += ntt       * sizeof(char  ) ; // listHash
	need += ntt       * sizeof(qvec_t) ; // listPoints

	// mark spot for m_topDocIdPtrs2 arrays
	int32_t off = need;

	// if sorting by date we need a much larger hash table than normal
	if ( sortByDate ) {
		// we need a "ptr end" (ptrEnds[i]) for each ptr since the 
		// buckets are not delineated by a date or score
		need += ntt * 256 * sizeof(char *) ;
		// get number of docids in largest list
		int32_t max = 0;
		for ( int32_t i = 0 ; i < numTiers ; i++ ) {
			for ( int32_t j = 0 ; j < numListsPerTier ; j++ ) {
				// datedb lists are 10 bytes per half key
				int32_t nd = lists[i][j].getListSize() / 10;
				if ( nd > max ) max = nd;
			}
		}
		// how big to make hash table?
		int32_t slotSize  = 4+4+2+sizeof(qvec_t);
		int64_t need = slotSize * max;
	        // have some extra slots in between for speed
		need = (need * 5 ) / 4;
		// . do not go overboard
		// . let's try to keep it in the L2 cache
		// . we can only do like 1 million random access to main
		//   memory per second. L2 mem should be much higher.
		//if ( need > 30*1024*1024 ) need = 30*1024*1024;
		//if ( need > 512*1024 ) need = 512*1024;
		//if ( need > 1024*1024 ) need = 1024*1024;
		// . this is balance between two latency functions. with more
		//   "need" we can have a bigger hash table which decrease
		//   m_numLoops, but increases our memory access latency since
		//   we'll be hitting more of main memory. use hashtest.cpp
		//   with a high # of slots to see the affects of increasing
		//   the size of the hash table. every loop in m_numLoops
		//   means we read the entire datedb lists and we can only do
		//   that at most at 2.5GB/sec or so on the current hardware.
		// . i don't expect memory access times to improve as fast as
		//   memory throughput, so in the future to maintain optimal
		//   behaviour, we should decrease the max for "need"???
		//if ( need > 4*1024*1024 ) need = 4*1024*1024;
		if ( need > 2*1024*1024   ) need = 2*1024*1024;
		//if ( need > 512*1024   ) need = 512*1024;
		// we need to have AT LEAST 1024 slots in our hash table
		if ( need < slotSize*1024 ) need = slotSize*1024;
		// clear this out in case it was set
		if ( m_bigBuf ) {
			mfree ( m_bigBuf , m_bigBufSize , "IndexTable2" );
			m_bigBuf = NULL;
		}
	tryagain:
		// alloc again
		m_bigBuf = (char *)mmalloc ( need , "IndexTable2" );
		if ( ! m_bigBuf && need > 1024*1024 ) {
			need >>= 1; goto tryagain; }
		// set it
		m_bigBufSize = need;
		if ( ! m_bigBuf ) {
			log("query: Could not allocate %"INT64" for query "
			    "resolution.",need);
			return false;
		}
	}

	// if not default AND no need for the list of docids from intersecting
	// the last two termlists (and their associated phrase termlists)
	if ( ! m_requireAllTerms ) {
		// do it
		m_bufSize = need;
		m_buf = (char *) mmalloc ( m_bufSize , "IndexTable" );
		if ( ! m_buf ) return log("query: Table alloc(%"INT64")"
					  ": %s",need,mstrerror(g_errno));
		// save it for error checking
		m_bufMiddle = m_buf + off;
		m_topDocIdPtrs2 = NULL;
		m_topScores2    = NULL;
		m_topExplicits2 = NULL;
		m_topHardCounts2= NULL;
		return true;
	}

	// sanity check
	if ( m_topDocIdPtrs2 ) { 
		log(LOG_LOGIC,"query: bad top docid ptrs2."); 
		char *xx = NULL; *xx = 0; }

	// calc list sizes
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		m_sizes[i] = 0;
		// component lists are merged into one compound list
		if ( m_componentCodes[i] >= 0 ) continue;
		for ( int32_t j = 0 ; j < numTiers ; j++ )
			m_sizes [i] += lists[j][i].getListSize();
	}

	// . get imap here now so we can get the smallest block size to
	//   set m_maxNumTopDocIds2
	// . get the smallest list (add all its tiers) to find least number
	//   of docids we'll have in m_topDocIdPtrs2
	m_nb = m_q->getImap ( m_sizes , m_imap , m_blocksize );

	// bail now if we have none!
	if ( m_nb <= 0 ) return true;

	int64_t min = 0;
	for ( int32_t i = 0 ; i < m_blocksize[0]; i++ )
		for ( int32_t j = 0 ; j < numTiers ; j++ )
			min += lists[j][m_imap[i]].getListSize() / 6 ;

	// debug msg
	//log("minSize = %"INT32" docids q=%s",min,m_q->m_orig);

	// now add in space for m_topDocIdPtrs2 (holds winners of a
	// 2 list intersection (more than 2 lists if we have phrases) [imap]
	int64_t nd = (105 * min) / 100 + 10 ;
	need += (4+               // m_topDocIdPtrs2
		 4+               // m_topScores2
		 sizeof(qvec_t)+  // m_topExplicits2
		 2                // m_topHardCounts2
		 ) * nd;

	// do it
	m_bufSize = need;
	m_buf = (char *) mmalloc ( m_bufSize , "IndexTable" );
	if ( ! m_buf ) return log("query: table alloc(%"INT64"): %s",
				  need,mstrerror(g_errno));

	// save it for error checking
	m_bufMiddle = m_buf + off;

	// . allow for 5% more for inserting maxDocIds every block
	//   so hashTopDocIds2() can use that
	// . actually, double in case we get all the termIds, but only one
	//   per slot scan/hash, so every other guy is a maxdocid
	// . no, now we use lastGuy/lastGuy2 to check if our new maxDocId
	//   is within 50 or less of the second last maxDocId added, and
	//   if it is, we bury the lastGuy and replace him.
	char *p = m_buf + off;
	m_topDocIdPtrs2 = (char           **)p ;  p += 4 * nd;
	m_topScores2    = (int32_t            *)p ;  p += 4 * nd;
	m_topExplicits2 = (qvec_t          *)p ;  p += sizeof(qvec_t) * nd;
	m_topHardCounts2= (int16_t           *)p ;  p += 2 * nd;
	m_maxTopDocIds2 = nd;
	m_numTopDocIds2 = 0;

	return true;
}

// realloc to save mem if we're rat
void IndexTable::freeMem ( ) {
	if ( m_bigBuf ) {
		mfree ( m_bigBuf , m_bigBufSize , "IndexTable2" );
		m_bigBuf = NULL;
	}
	if ( ! m_buf ) return;
	//mfree ( (char *)m_topDocIdPtrs2 , m_maxTopDocIds2 * 10,"IndexTable");
	mfree ( m_buf , m_bufSize , "IndexTable" );
	m_buf           = NULL;
	m_topDocIdPtrs2 = NULL;
	m_topScores2    = NULL;
	m_topExplicits2 = NULL;
	m_topHardCounts2= NULL;
	m_maxTopDocIds2 = 0;
	m_numTopDocIds2 = 0;
}

void IndexTable::addLists_r (IndexList  lists[MAX_TIERS][MAX_QUERY_TERMS],
			     int32_t       numTiers                  ,
			     int32_t       numListsPerTier           ,
			     Query     *q                         ,
			     int32_t       docsWanted                ,
			     int32_t      *totalListSizes            ,
			     bool       useDateLists              ,
			     bool       sortByDate                ,
			     float      sortByDateWeight          ) {

	// sanity check
	if ( ! useDateLists && sortByDate ) { 
		log(LOG_LOGIC,"query: bad useDateLists/sortByDate.");
		char *xx = NULL; *xx = 0; }

	// if we don't get enough results should we grow the lists and recall?
	m_doRecall = true;

	// bail if nothing to intersect... if all lists are empty
	if ( ! m_buf /*m_nb <= 0*/ ) return;

	// set start time
	int64_t t1 = gettimeofdayInMilliseconds();

	char hks  = 6; // half key size (size of everything except the termid)
	char fks  = 12;
	// dateLists are 4 more bytes per key than standard 12-byte key lists
	if ( useDateLists ) { hks += 4; fks += 4; }

	// set up for a pointer (array index actually) sort of the lists
	//int32_t imap [ MAX_QUERY_TERMS ];

	// now swap the top 12 bytes of each list back into original order
	for ( int32_t i = 0 ; i < numListsPerTier ; i++ ) {
	// loop over all lists in this term's tiers
	for ( int32_t j = 0 ; j < numTiers ; j++ ) {
		// skip if list is empty, too
		if ( lists[j][i].isEmpty() ) continue;
		// skip if already swapped
		if ( m_swapped[j][i] ) continue;
		// flag it
		m_swapped[j][i] = true;
		// point to start
		char *p = lists[j][i].getList();
		// remember to swap back when done!!
		//char ttt[6];
		//gbmemcpy ( ttt   , p     , 6 );
		//gbmemcpy ( p     , p + 6 , 6 );
		//gbmemcpy ( p + 6 , ttt   , 6 );
		char ttt[10];
		gbmemcpy ( ttt   , p       , hks );
		gbmemcpy ( p     , p + hks , 6   );
		gbmemcpy ( p + 6 , ttt     , hks );
		// point to the low "hks" bytes now
		p += 6;
		// turn half bit on
		*p |= 0x02;
	}
	}

	// if query is boolean, turn off rat
	if ( m_q->m_isBooleanQuery ) m_requireAllTerms = false;

	// count # of panics
	m_numPanics = 0;
	// and # of collisions
	m_numCollisions = 0;
	// count # loops we do
	m_numLoops = 0;

	// . set m_q->m_bitScores[]
	// . see Query.h for description of m_bitScores[]
	// . this is now preset in Msg39.cpp since it may now require an 
	//   alloc since we allow more than 16 query terms for metalincs
	if ( ! m_q->m_bmap || ! m_q->m_bitScores ) { //alreadySet ) {
		//m_q->setBitScores();
		//m_alreadySet = true;
		log (LOG_LOGIC,"query: bit map or scores is NULL. Fix this.");
		return;
	}

	int32_t minHardCount = 0;
	
	// if not rat, do it now
	if ( ! m_requireAllTerms ) { 
		// no re-arranging the query terms for default OR searches
		// because it is only beneficial when doing sequential
		// intersectinos to minimize the intersection and speed up
		int32_t count = 0;
		for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
			// component lists are merged into one compound list
			if ( m_componentCodes[i] >= 0 ) continue;
			m_imap[count++] = i;
		}
		addLists2_r ( lists           ,
			      numTiers        ,
			      count           , // numListsPerTier
			      q               ,
			      docsWanted      ,
			      m_imap          ,
			      true            ,
			      0               ,
			      useDateLists    ,
			      sortByDate      ,
			      sortByDateWeight,
			      &minHardCount   );
		goto swapBack;
	}


	{ // i added this superfluous '{' so we can call "goto swapBack" above.

	// TODO: use ptrs to score so when hashing m_topDocIdPtrs2 first and 
	// another non-required list second we can just add to the scores 
	// directly? naahhh. too complicated.

	// get a map that sorts the query terms for optimal intersectioning
	//int32_t blocksize [ MAX_QUERY_TERMS ];
	//int32_t nb = m_q->getImap ( sizes , imap , blocksize );

	// . if first list is required and has size 0, no results
	// . 'how do they do that' is reduced to a signless phrase
	// . careful, i still saw some chinese crap whose two terms were
	//   both signless phrases somehow... hmmm, m_nb was 0 and m_imap
	//   was not set
	//if (m_nb <= 0||/*m_q->isRequired(imap[0])&&*/ m_sizes[m_imap[0]]<12){
	if (m_nb <=0 || /*m_q->isRequired(imap[0])&&*/ m_sizes[m_imap[0]]<fks){
		m_doRecall = false;
		goto swapBack;
	}

	// count number of base lists
	int32_t numBaseLists = m_blocksize[0];

	// how many lists to intersect initially? that is numLists.
	int32_t numLists = m_blocksize[0];
	// if we got a second block, add his lists (single term plus phrases)
	if ( m_nb > 1 ) numLists += m_blocksize[1];

	// component lists are merged into one compound list
	int32_t total = 0;
	for ( int32_t i = 0 ; i < m_nb ; i++ ) // q->m_numTerms ; i++ ) 
		//if ( m_componentCodes[i] < 0 ) total++;
		total += m_blocksize[i];

	// if this is true we set m_topDocIds/m_topScores/etc. in addLists2_r()
	bool lastRound = (numLists == total); // numListsPerTier);

	addLists2_r ( lists , numTiers , numLists , q , docsWanted , 
		      m_imap , lastRound , numBaseLists , useDateLists ,
		      sortByDate , sortByDateWeight , &minHardCount );
	// . were both lists under the sum of tiers size?
	// . if they were and we get no results, no use to read more, so we
	//   set m_doRecall to false to save time
	bool underSized = true;
	int32_t m0 = m_imap[0];
	int32_t m1 = 0 ;
	if ( m_nb > 1 ) m1 = m_imap[m_blocksize[0]];
	if (     m_sizes[m0] >= totalListSizes[m0]&&m_q->getTermSign(m0)!='-')
		underSized = false;
	if(m_nb>1&& m_sizes[m1]>=totalListSizes[m1]&&m_q->getTermSign(m1)!='-')
		underSized = false;

	// . offset into imap
	// . imap[off] must NOT be a signless phrase term
	int32_t off = numLists;

	// follow up calls
	for ( int32_t i = 2 ; i < m_nb ; i++ ) {
		// if it is the lastRound then addLists2_r() will compute
		// m_topDocIds/m_topScores arrays
		lastRound = (i == m_nb - 1);
		// . if we have no more, might as well stop!
		// . remember, maxDocIds is stored in here, so it will be at
		//   least 1
		if ( m_numTopDocIds2 <= 1 ) break;
		// is this list undersized?
		int32_t mx = m_imap[off];
		if (m_sizes[mx]>=totalListSizes[mx]&&m_q->getTermSign(mx)!='-')
			underSized = false;
		// set number of lists
		numLists = m_blocksize[i];
		// add it to the intersection
		addLists2_r ( lists , numTiers , numLists , q , docsWanted , 
			      m_imap + off , lastRound, 0 , useDateLists ,
			      sortByDate , sortByDateWeight , &minHardCount );
		// skip to next block of lists
		off += m_blocksize[i];
	}
	// . now if we have no results and underSize is true, there is no
	//   use to read more for each list... pointless
	// . remember, maxDocIds is stored in here, so it will be at least 1
	if ( m_numTopDocIds2 <= 1 && underSized ) {
		// debug
		//fprintf(stderr,"UNDERSIZED quitting\n");
		m_doRecall = false;
	}
	}

swapBack:
	// compute total number of docids we dealt with
	m_totalDocIds = 0;
	// now swap the top 12 bytes of each list back into original order
	for ( int32_t i = 0 ; i < numListsPerTier ; i++ ) {
	// loop over all lists in this term's tiers
	for ( int32_t j = 0 ; j < numTiers ; j++ ) {
		// skip if list is empty, too
		if ( lists[j][i].isEmpty() ) continue;
		// compute total number of docids we dealt with
		//m_totalDocIds += (lists[j][i].getListSize()-6)/6;
		// date lists have 5 bytes scores, not 1 byte scores
		m_totalDocIds += (lists[j][i].getListSize()-6)/hks;
		// point to start
		//char *p = lists[j][i].getList();
		// remember to swap back when done!!
		//char ttt[6];
		//gbmemcpy ( ttt   , p     , 6 );
		//gbmemcpy ( p     , p + 6 , 6 );
		//gbmemcpy ( p + 6 , ttt   , 6 );
		// turn half bit off again 
		//*p &= 0xfd;
	}
	}
	// get time now
	int64_t now = gettimeofdayInMilliseconds();
	// store the addLists time
	m_addListsTime = now - t1;
	// . measure time to add the lists in bright green
	// . use darker green if rat is false (default OR)
	int32_t color;
	if ( ! m_requireAllTerms ) color = 0x00008000 ;
	else                       color = 0x0000ff00 ;
	g_stats.addStat_r ( 0 , t1 , now , color );
}

// . DO NOT set g_errno cuz this is probably in a thread
// . these lists should be 1-1 with the query terms
// . there are multiple tiers of lists, each tier is an array of lists
//   one for each query term
// . we try to restrict the hash table to the L1 cache to avoid slow mem
// . should result in a 10x speed up on the athlon, 50x on newer pentiums,
//   even more in the future as the gap between L1 cache mem speed and
//   main memory widens
void IndexTable::addLists2_r ( IndexList  lists[MAX_TIERS][MAX_QUERY_TERMS] ,
			       int32_t       numTiers                  ,
			       int32_t       numListsPerTier           ,
			       Query     *q                         ,
			       int32_t       docsWanted                ,
			       int32_t      *imap                      ,
			       bool       lastRound                 ,
			       int32_t       numBaseLists              ,
			       bool       useDateLists              ,
			       bool       sortByDate                ,
			       float      sortByDateWeight          ,
			       int32_t      *minHardCountPtr           ) {
	// set up for rat
	int32_t rat   = m_requireAllTerms;
	m_nexti = 0;
	// sanity test -- fails if 2nd guy is int16_t negative
	/*
	int32_t size0 = 0;
	int32_t size1 = 0;
	if ( rat && numListsPerTier == 2 ) {
		for ( int32_t i = 0 ; i < numTiers ; i++ ) {
			size0 += lists[i][imap[0]].getListSize();
			size1 += lists[i][imap[1]].getListSize();
		}
		if ( size0 > size1 ) { char *xx = NULL; *xx = 0; }
	}
	*/

	// getting 100 takes same time as getting 1
	if ( docsWanted < 100 ) docsWanted = 100;

	qvec_t          requiredBits = m_q->m_requiredBits ;
	unsigned char  *bitScores    = m_q->m_bitScores;
	qvec_t         *bmap         = m_q->m_bmap;

	// . keep track of max tier we've processed
	// . if Msg39 finds that too many docids are clustered away it
	//   will recall us with docsWanted increased and the numTiers will
	//   be the same as the previous call
	if ( numTiers < m_numTiers  ||  numTiers > m_numTiers + 1 )
		log(LOG_LOGIC,"query: indextable: Bad number of tiers."
			      " %"INT32" vs. %"INT32"", numTiers, m_numTiers );
	else if ( numTiers == m_numTiers + 1 )
		m_numTiers++;

	// current tier #
	int32_t tier = numTiers - 1;

	// sanity check
	if ( ! rat && 
	     numListsPerTier != m_q->getNumTerms()-m_q->getNumComponentTerms())
		log(LOG_LOGIC,"query: indextable: List count mismatch.");

	// truncate this
	if (!m_topTree) if (docsWanted > MAX_RESULTS) docsWanted = MAX_RESULTS;

	// convenience ptrs
	char          **topp  = m_topDocIdPtrs [ tier ];
	int32_t           *tops  = m_topScores    [ tier ];
	unsigned char  *topb  = m_topBitScores [ tier ];
	char           *tope  = m_topExplicits [ tier ];

	// assume no top docIds now
	int32_t numTopDocIds    = 0;

	////////////////////////////////////
	// begin hashing setup
	////////////////////////////////////

	// count # of docs that EXPLICITLY have all query singleton terms
	int32_t explicitCount = 0;

	// count # of docs that IMPLICITY have all query singleton terms
	int32_t implicitCount = 0;

	// highest bscore we can have
	//unsigned char maxbscore = bitScores [ requiredBits ];

	// . count all mem that should be in the L1 cache
	// . the less mem we use the more will be available for the hash table
	int32_t  totalMem = 0;

	// . we mix up the docIdBits a bit before hashing using this table
	// . TODO: what was the reason for this? particular type of query
	//   was too slow w/o it?
	// . a suburl:com collision problem, where all docids have the same
	//   score and are spaced equally apart somehow made us have too
	//   many collisions before, probably because we included the score
	//   in the hash...? use bk revtool IndexTable.cpp to see the old file.
	/*
	static uint32_t s_mixtab [ 256 ] ;
	// is the table initialized?
	static bool s_mixInit = false;
	if ( ! s_mixInit ) {
		srand ( 1945687 );
		for ( int32_t i = 0 ; i < 256 ; i++ ) 
			s_mixtab [i]= ((uint32_t)rand());
		s_mixInit = true;
		// randomize again
		srand ( time(NULL) );
	}
	*/

	// s_mixtab should be in the L1 cache cuz we use it to hash
	//totalMem += 256 * sizeof(int32_t);

	// . now form a set of ptrs for each list
	// . each ptr points to the first 6-byte key for a particular score
	char *p   = m_buf;
	int32_t  nqt = numListsPerTier; // q->getNumQueryTerms();
	int32_t  ntt = numListsPerTier * numTiers;
	// we now point into m_buf to save stack since we're in a thread
	//char           *ptrs          [ MAX_QUERY_TERMS * MAX_TIERS * 256 ];
	//char           *pstarts       [ MAX_QUERY_TERMS * MAX_TIERS * 256 ];
	//int32_t            scoreTable    [ MAX_QUERY_TERMS ] [ 256 ];
	//char           *oldptrs       [ MAX_QUERY_TERMS * MAX_TIERS * 256 ];
	//char             listSigns          [ MAX_QUERY_TERMS * MAX_TIERS ];
	//int32_t            *listScoreTablePtrs [ MAX_QUERY_TERMS * MAX_TIERS ];
	//qvec_t           listExplicitBits   [ MAX_QUERY_TERMS * MAX_TIERS ];
	//char            *listEnds           [ MAX_QUERY_TERMS * MAX_TIERS ];
	//char             listHash           [ MAX_QUERY_TERMS * MAX_TIERS ];
	//qvec_t           listPoints         [ MAX_QUERY_TERMS * MAX_TIERS ];
	char **ptrs       = (char **)p; p += ntt * 256 * sizeof(char *);
	char **pstarts    = (char **)p;	p += ntt * 256 * sizeof(char *);
	char **ptrEnds    = NULL;
	if ( useDateLists ) {
		ptrEnds = (char **)p;	p += ntt * 256 * sizeof(char *);
	}
	char **oldptrs    = (char **)p; p += ntt * 256 * sizeof(char *);
	int32_t **scoreTable = (int32_t **)p; p += nqt *       sizeof(int32_t *);
	// one score table per query term
	for ( int32_t i = 0 ; i < nqt ; i++ ) { 
		scoreTable [ i ] = (int32_t *)p; p += 256 * sizeof(int32_t); }
	// we have to keep this info handy for each list
	char   *listSigns          = (char  *)p; p += ntt * sizeof(char  );
	char   *listHardCount      = (char  *)p; p += ntt * sizeof(char  );
	int32_t  **listScoreTablePtrs = (int32_t **)p; p += ntt * sizeof(int32_t *);
	char  **listEnds           = (char **)p; p += ntt * sizeof(char *);
	char   *listHash           = (char  *)p; p += ntt * sizeof(char  );
	qvec_t *listExplicitBits   = (qvec_t *)p; p += ntt * sizeof(qvec_t);
	qvec_t *listPoints         = (qvec_t *)p; p += ntt * sizeof(qvec_t);

	// do not breech
	if ( p > m_bufMiddle ) {
		log(LOG_LOGIC,"query: indextable: Got table "
		    "mem breech.");
		char *xx = NULL; *xx = 0;
	}

	char hks  = 6; // half key size (size of everything except the termid)
	// dateLists are 4 more bytes per key than standard 12-byte key lists
	if ( useDateLists ) hks += 4;

	int32_t             numPtrs   = 0;
	int32_t             numLists  = 0;
	uint32_t    numDocIds = 0;
	int32_t             numSorts  = 0;
	// . make the ebitMask 
	// . used when rat=1 to determine the hits from both (or the) list
	qvec_t ebitMask = 0;
	// how many of the terms we are intersecting are "required" and 
	// therefore do not have an associated explicit bit. this allows us
	// to support queries of many query terms.
	int32_t minHardCount = *minHardCountPtr;
	// each list can have up to 256 ptrs, corrupt data may mess this up
	int32_t maxNumPtrs = ntt * 256;
	// only log this error message once per call to this routine
	char pflag = 0;
	// time the gbsorting for the datelists
	int64_t t1 = gettimeofdayInMilliseconds();

	for ( int32_t i = 0 ; i < numListsPerTier ; i++ ) {
		// map i to a list number
		int32_t m = imap[i];
		// skip if ignored
		//if ( m_q->m_ignore[i] ) continue;
		// skip if list in first tier is empty
		if ( lists[0][m].isEmpty() ) continue;
		// . make score table to avoid IMUL instruction (16-36 cycles)
		// . 1k per table! will that fit in L1/L2 cache, too?
		// . if too big, limit j to ~32 to cover most common scores
		// . remember that scores in the indexdb keys are complemented
		//   so higher guys appear on top
		// . if score is weighted with a relative 0 weight, that means
		//   that the term is required, but should not contribute to
		//   the final ranking in any way, so use a score of 1 despite
		//   the term count of this term in the page
		QueryTerm *qt = &q->m_qterms[m];
		if ( qt->m_userWeight == 0 && qt->m_userType == 'r' )
			for ( int32_t k = 0 ; k < 256 ; k++ )
				scoreTable[i][k] = 1;
		else
			for ( int32_t k = 0 ; k < 256 ; k++ )
				scoreTable[i][k] = m_scoreWeights[m] * (255-k);
		// sorty by date uses the dates as scores, but will add the
		// normal score to the date, after weighting it with this
		if ( sortByDate ) // && sortByDateWeight > 0.0 )
			for ( int32_t k = 0 ; k < 256 ; k++ )
				scoreTable[i][k] = (int32_t)((float)
							  scoreTable[i][k] * 
							  sortByDateWeight);
		// are we using the hard count instead of an explicit bit?
		char hc = 0;
		if ( qt->m_explicitBit == 0 ) {
			// . it is either a phrase in quotes or a single word
			// . or something like cd-rom or www.abc.com
			if ( qt->m_isPhrase ) {
				if ( qt->m_inQuotes        ) minHardCount++;
				if ( qt->m_termSign == '+' ) minHardCount++;
				if ( qt->m_termSign == '*' ) minHardCount++;
				hc = 1;
			}
			else {
				minHardCount++;
				hc = 1;
			}
		}
		// count mem from score tables
		totalMem += 256 * sizeof(int32_t);
		// loop over all lists in this term's tiers
		for ( int32_t j = 0 ; j < numTiers ; j++ ) {
			// skip if first tier list is empty, too
			if ( lists[j][m].isEmpty() ) continue;
			// corrupt data can make numPtrs too big
			if ( numPtrs >= maxNumPtrs ) continue;
			//if ( numPtrs >= MAX_QUERY_TERMS*MAX_TIERS*256 ) 
			//	continue;
			// get the sign
			char sign = m_q->getTermSign(m);
			// are we date?
			if ( useDateLists && sortByDate ) sign = 'd';
			else if (useDateLists) sign='e';
			// set sign of list (\0,- or d)
			listSigns[numLists] = sign;
			// point to its table
			listScoreTablePtrs[numLists] = scoreTable[i];
			// and its explicit bit
			listExplicitBits[numLists] = qt->m_explicitBit ;//1<<m;
			// some have a hard count instead of an explicit bit
			listHardCount[numLists] = hc;
			// rat is special
			if ( rat ) {
				//if ( i < numBaseLists )
				//	listExplicitBits[numLists] = 0x00 ;
				if      ( sign == '-' )
					ebitMask |= 0x00; // dummy
				else if ( m_q->isPhrase(m) && sign&& sign!='d')
					ebitMask |= listExplicitBits[numLists];
				else if ( ! m_q->isPhrase(m) )
					ebitMask |= listExplicitBits[numLists];
			}
			// the end of this list
			listEnds[numLists] = lists[j][m].getListEnd();
			// . should list be hashed first?
			// . only applies to rat=1
			if   ( i < numBaseLists ) listHash[numLists] = true;
			else                      listHash[numLists] = false;
			if   ( ! rat            ) listHash[numLists] = true;
			// and the # of it's first ptr in the ptrs[] array
			listPoints[numLists] = numPtrs;
			// it should be in L1 cache, too
			totalMem += 1 + 4 + 2 + 4 + 2;
			// inc list count
			numLists++;
			// . now fill the ptr[] array
			// . reset ptrs to start of list
			lists[j][m].resetListPtr();
			// point to start
			char *p = lists[j][m].getList();
			// and end
			char *pend = lists[j][m].getListEnd();
			// add to total docids
			//numDocIds += (lists[j][m].getListSize() - 6) / 6;
			numDocIds += (lists[j][m].getListSize() - 6) / hks;
			// this is now done in addLists_r()
			// remember to swap back when done!!
			//char ttt[6];
			//gbmemcpy ( ttt   , p     , 6 );
			//gbmemcpy ( p     , p + 6 , 6 );
			//gbmemcpy ( p + 6 , ttt   , 6 );
			// point to the low 6 bytes now
			p += 6;
			// turn half bit on
			*p |= 0x02;
			// find the tiers
			ptrs [ numPtrs ] = p;

			// . ok, no need to set pstarts[] and ptrs[] any more
			//   since we do not have score tiers for date sort
			// . we do not do score tiering for date lists, since 
			//   we use 5 bytes for the score, not one, so we must 
			//   scan the whole list for every maxDocId used. This 
			//   may be a few times slower. If we had a large L1 
			//   cache this would hurt us less. but L2 caches are 
			//   like 1MB  nowadays and having the hash table 
			//   contained in the L2 cache should only be like 3 
			//   times as slow.
			//if ( useDateLists ) continue;
			// to make sort by date faster, we now artificially
			// create 256 buckets on each date list and then
			// sort each bucket by docid
			if ( useDateLists ) {
				int32_t listSize = pend - p;
				int32_t pstep    = listSize / 250;
				// . do not go too low
				// . TODO: try lowering this to see if it
				//         gets faster. i could not test lower
				//         values because buzz starting 
				//         querying again.
				if ( pstep < 10000 ) pstep = 10000;
				// make sure it is divisible by 10
				while ( (pstep % hks) ) pstep--;
				// do not go crazy
				if ( pstep <= 0 ) pstep = listSize;
				// now make 256 approximately equally sized
				// buckets of datedb records . a lot of times
				// the dates are the same and they are already
				// sorted by docid, so we might want to check
				// for that to save time. (TODO)
				for ( ; p < pend ; p += pstep ) {
					// get the end
					char *end  = p + pstep;
					if ( end > pend ) end = pend;
					// store the current ptr info
					pstarts [ numPtrs   ] = p;
					ptrEnds [ numPtrs   ] = end;
					ptrs    [ numPtrs++ ] = p;
					int32_t  size = end - p;
					// count it
					numSorts++;
					// now sort each p
					hsort_10_1_5((uint8_t *)p, size / 10 );
					//gbsort ( p, size, hks, dateDocIdCmp );
				}
				// skip the rest
				continue;
			}

			// set this new var so we don't rollback too much
			// when re-hashing because of a panic
			pstarts [ numPtrs++ ] = p;
			// advance
			p += 6;
			// fill up the ptrs array
			//for ( ; p < pend ; p += 6 ) {
			for ( ; p < pend ; p += hks ) {
				// if we got a new score, add a new ptr
				if ( p[5] != p[-1] ) {
					// if data is corrupt, we have
					// to watch so we don't breech!!
					if ( numPtrs >= maxNumPtrs ) {
						if ( ! pflag )
							log(LOG_LOGIC,
							    "query: Got "
							    "indextable "
							    "breech. np=%"INT32"",
							    numPtrs);
						pflag = 1;
						break;
					}
					//if ( numPtrs >=
					//     MAX_QUERY_TERMS*MAX_TIERS*256 )
					//	break;
					// speed test -- remove the small head
					// score is COMPLEMENTED!!!
					//unsigned char c =(unsigned char)p[5];
					//if ( 255-c <= (unsigned char)25 ) {
					pstarts [ numPtrs   ] = p;
					ptrs    [ numPtrs++ ] = p;
					//}
				}
				// . jump by 30 if he's got same score as us
				// . this saves a good amount of time!
				while (p+300*6 < pend && p[300*6+5] == p[5] )
					p += 300*6;
			}
		}
	}

	// time the gbsorting for the datelists
	if ( m_isDebug || g_conf.m_logDebugQuery ) {
		int64_t t2 = gettimeofdayInMilliseconds();
		logf(LOG_DEBUG,"query: Took %"INT64" ms to prepare list ptrs. "
		     "numDocIds=%"UINT32" numSorts=%"INT32"",
		     t2 - t1 , numDocIds , numSorts );
	}

	// save it
	*minHardCountPtr = minHardCount;
	// count mem from ptrs
	totalMem += numPtrs * sizeof(char *);
	// . and what they point to too! first line should/will be in cache.
	// . assume a cache line size of 64 bytes
	totalMem += numPtrs * L1_CACHE_LINE_SIZE;
	// if we got no non-empty lists, we're done
	//if ( n == 0 ) goto done;
	// count miscellaneous mem access (like "point" and "next")
	totalMem += 256;
	// convenience vars
	register int32_t i = 0 ;
	int32_t j;

	// a dummy var
	int64_t tmpHi = 0x7fffffffffffffffLL;

	// . the info of the weakest entry in the top winners
	// . if its is full and we get another winner, the weakest will be
	//   replaced by the new winner
	unsigned char  minTopBitScore  = 0 ;
	int32_t           minTopScore     = 0 ;
	char          *minTopDocIdPtr  = (char *)&tmpHi;

	// . this is the hash table
	// . it is small cuz we are trying to stuff it all into the L1 cache
	// . it could be as big as 128k, so reserve enough to take advantage
	// . some of the cache will be used for scoreTables[], ptrs[] etc.
	// . i tried using a struct that holds these 3 but it made everything
	//   about 10% slower
	char  localBuf[10000*(4+4+2+sizeof(qvec_t))];
	char *pp = localBuf;
	//char           *docIdPtrs    [ 10000 ];
	//int32_t            scores       [ 10000 ];
	//qvec_t          explicitBits [ 10000 ];
	//int16_t           hardCounts   [ 10000 ];
	// . how many slots in the in-mem-cache hash table... up to 10000
	// . use the remainder of the L1 cache to hold this hash table
	int32_t availSlots = (L1_DATA_CACHE_SIZE - totalMem) / 10;
	// make a power of 2 for easy hashing (avoids % operator)
	int32_t numSlots = getHighestLitBitValue ( availSlots );
	// don't go below this min even if we must leave the cache
	if ( numSlots < 1024 ) numSlots = 1024;
	// damn, now we have to keep this fixed for rat because hashTopDocIds2
	// needs to have numSlots consistent for all calls to addLists2_r()
	// because m_topDocIdPtrs2 needs to hash full blocks. blocks are
	// separated by maxDocId values, which are stored as docId ptrs in
	// m_topDocIdPtrs2, and indicated by having a score of 0. so if we
	// stored one block into m_topDocIdPtrs2 when numSlots was 2048 that
	// block might not hash into a 1024 slot table...
	if ( rat ) numSlots = RAT_SLOTS;

	// sanity check
	if ( numSlots > 10000 ) { 
		log(LOG_LOGIC,"query: bad numSlots.");
		char *xx = NULL; *xx = 0; }

	// sort by date should try to maximize maxDocId and the size of the
	// hash table in order to prevent rescanning the termlists
	if ( sortByDate ) {
		numSlots = m_bigBufSize / (4+4+2+sizeof(qvec_t));
		// make a power of 2 for easy hashing (avoids % operator)
		numSlots = getHighestLitBitValue ( numSlots );
		// don't go below this min even if we must leave the cache
		if ( numSlots < 1024 ) { 
			log(LOG_LOGIC,"query: bad numSlots2.");
			char *xx = NULL; *xx = 0; }
		// point to the buffer space
		pp = m_bigBuf;
	}
		
	char   **docIdPtrs    = (char  **)pp; pp += numSlots*4;
	int32_t    *scores       = (int32_t   *)pp; pp += numSlots*4;
	int16_t   *hardCounts   = (int16_t  *)pp; pp += numSlots*2;
	qvec_t  *explicitBits = (qvec_t *)pp; pp += numSlots*sizeof(qvec_t);

	// for hashing we need a mask to use instead of the % operator
	uint32_t mask = (uint32_t)numSlots - 1;
	// empty all buckets
	for ( int32_t i = 0 ; i < numSlots ; i++ ) 
		docIdPtrs[i] = NULL; // explicitBits[i] = 0;

	// . use numSlots to get first docid upper bound
	// . this is just the top 4 bytes of docids allowed to hash...
	// . since hash table is so small we can only hash docids below
	//   "maxDocId"
	// . therefore, we have to do serveral iterations
	// . i found a slight increase in speed chaning to 8/20 instead of 
	//   1/2 on the 'the .. sex' test now commented out in Msg39.cpp
	// . if rat (require all terms) is true we only have two lists and
	//   only the smallest one (first one) gets hashed..
	uint32_t step = numSlots * 8 / 20 ; // >> 1 ;
	uint32_t dd   = numDocIds ;
	if ( dd <= 0 ) dd = 1;
	// max it out if numDocIds is smaller than numSlots/2
	if ( dd <= step ) step = 0xffffffff;
	else step *= ((uint32_t)0xffffffff / dd) ;
	uint32_t maxDocId = step;
	// we overwrite m_topDocIdPtrs2/m_topScores2
	int32_t newTopDocIds2 = 0;
	// these two guys save us on memory
	int32_t lastGuy2 = -10000;
	int32_t lastGuy  = -10000;
	// save the last maxDocId in case we have to rollback for a panic
	uint32_t lastMaxDocId = 0;
	// used for hashing
	int32_t nn = 0;
	int32_t nnstart;
	// these vars are specific to each list
	char             sign          = 0;
	int32_t            *scoreTablePtr = NULL;
	qvec_t           ebits         = 0;
	char            *listEnd       = NULL;
	bool             hashIt        = true;
	char             hc            = 0;
	// if maxDocId is too big step down by this much / 2
	uint32_t downStep = step;
	uint32_t oldDocId ;
	int32_t          printed = -1;

	int32_t weakest = -1;

	// reset these counts in addLists_r() now so rat=1 can count right
	// count # of panics
	//m_numPanics = 0;
	// and # of collisions
	//m_numCollisions = 0;
	// save # of ptrs for stats
	m_numPtrs = numPtrs;
	// count # loops we do
	//m_numLoops = 0;
	// save for debug
	m_numSlots = numSlots;

	//t1 = gettimeofdayInMilliseconds();

	/////////////////////////////////////////////////
	// begin the hashing loop
	// this is where we need SUPER SPEED!!!!
	/////////////////////////////////////////////////

	// should we hash topDocIds2? only if we got some
	bool hashThem = ( rat && m_topDocIdPtrs2 && numBaseLists == 0 );

	// these vars help us change the list-specific vars
	int16_t point ;
	int16_t next  ;

	// . don't do anything if we're rat and list is negative and empty
	// . positive lists are always before negative and they are sorted
	//   by list size, so if a positive is ever empty, we add it first
	if ( rat && numLists == 0 ) {
		// if this is the last round we must set m_topDocIds et al
		if ( ! lastRound ) return;
		newTopDocIds2=m_numTopDocIds2; 
		goto done;
	}

	// . if we have none, intersection is empty, no results found
	// . this is now done in addLists_r()
	//if ( ! firstRound && hashThem && m_numTopDocIds2 <= 0 ) return ;

 top:
	m_numLoops++;
	// these vars help us change the list-specific vars
	point = 0;

	next  = 0;

	// reset ptrs for datedb since we scan the whole thing each loop
	//if (useDateLists) for ( i = 0; i < numPtrs; i++ ) ptrs[i]=pstarts[i];

	// . if this is a successive rat call, m_topDocIdPtrs2 should be hashed
	//   and then we hash the one list we should have
	// . this is good when rat is 0 now because we're adding the phrases
	//   so it's like partial rat going on
	// . TODO: if m_numTopDocIds2 is 1, that is just the stored maxDocId!!!
	if ( hashThem && m_numTopDocIds2 > 0 )
		hashTopDocIds2 ( &maxDocId    ,
				 docIdPtrs    ,
				 scores       ,
				 explicitBits ,
				 hardCounts   ,
				 mask         ,
				 numSlots     );

	// loop over the ptrs
	for ( i = 0 ; i < numPtrs ; i++ ) { 
		// when i reaches this break point we've switched lists
		if ( i == point ) {
			sign          = listSigns          [ next ];
			scoreTablePtr = listScoreTablePtrs [ next ];
			ebits         = listExplicitBits   [ next ];
			listEnd       = listEnds           [ next ];
			hashIt        = listHash           [ next ];
			hc            = listHardCount      [ next ];
			// if that was the last list, then we'll exit the
			// loop by i hitting numPtrs, so don't worry
			next++;
			if ( next < numLists ) point = listPoints [ next ];
			else                   point = -1;
		}
		// skip if emptied (end of that list fragment)
		if ( ! ptrs[i] ) continue;
	addMore:
		// . date lists are not divided into sublists based on
		//   the one byte score, they are just one big list...
		// . this at "top:" we must reset each whole termlist to
		//   point to the start, thus, do not re-hash docids from 
		//   the last time (lastMaxDocId)
		/*
		if ( useDateLists ) {
			// we expect to skip over a lot of docids in a row
			// because we usually only have 32k slots and millions
			// of docids, so we're talking doing hundreds of loops
			// (calls to top:) which means there's like >99% chance
			// we end up skipping this docid here...
			// TODO: make a linked list for each docid "section"..
			//       call this a jump table... to avoid having to
			//       scan over the whole list every loop. this
			//       might make us 2x faster, and sort by date is
			//       currently ~10x slower than regular search for
			//       the same number of docids.
			pp = ptrs[i]+1;
		tightloop:
			if ( *(uint32_t *)pp > maxDocId      ||
			     *(uint32_t *)pp <= lastMaxDocId  ) {
				pp += 10;
				if ( pp < listEnd ) goto tightloop;
				oldptrs[i] = pp-1;
				ptrs[i] = NULL;
				continue;
			}
			// set it back
			ptrs[i] = pp-1;
		}
		*/
		// if the top 4 bytes of his docid is > maxDocId, 
		// then skip to next ptr
		//else if( *(uint32_t *)(ptrs[i]+1) > maxDocId ) continue;
		if ( *(uint32_t *)(ptrs[i]+1) > maxDocId ) continue;
		// otherwise, hash him, use the top 32 bits of his docid
		nn = (*(uint32_t *)(ptrs[i]+1) )& mask ;
		// removing the mix table reduces time by about 10%
		//^ (s_mixtab[ptrs[i][2]])) & mask;
		// save start position so we can see if we chain too much
		nnstart = nn;
		// debug point
		//int64_t ddd ;
		//gbmemcpy ( &ddd , ptrs[i] , 6 );
		//ddd >>= 2;
		//ddd &= DOCID_MASK;
		//if ( ddd == 261380478983LL )
		//	log("got it");
		/*
		if ( ddd == 
			unsigned char ss = (unsigned char)ptrs[i][5];
			int32_t sss = scoreTablePtr[ss];
			logf(LOG_DEBUG,
			     "nt=%"INT32" i=%"INT32" max=%"UINT64" sc=%hhu sc2=%"UINT32" d=%"UINT64"",
			    (int32_t)numTiers,
			    (int32_t)i,
			    (int64_t)(((int64_t)maxDocId)<<6) | 0x3fLL, 
			     255-ss, 
			     sss,
			    (int64_t)ddd );
		}
		*/

	chain:
		// . if empty, take right away
		// . this is the most common case so we put it first
		if ( docIdPtrs[nn] == NULL ) { // explicitBits[nn] == 0 ) {
			// . did we miss it?
			// . if rat is true, then advance to next right now
			if ( ! hashIt ) {
				if ( ! useDateLists ) goto advance;
				else if(sortByDate)   goto dateAdvance;
				else                  goto dateAdvance1;
			}
			// hold ptr to our stuff
			docIdPtrs    [ nn ] = ptrs[i];
			// set pure bits
			explicitBits [ nn ] = ebits;
			hardCounts   [ nn ] = hc;
			// if we're not exclusive or date search, do it quick
			if ( sign == '\0' || sign == '+' || sign == '*' )
			 scores[nn]=scoreTablePtr[((unsigned char)ptrs[i][5])];
			// deal with excluded terms
			else if ( sign == '-' ) scores[nn] = -500000000;
			else if (sign == 'e') {
					if ( scores[nn]<0 ) {
					scores[nn] = 0x7fffffff;
				}
			
				// replicate the code below for speed
			dateAdvance1:
				ptrs[i] += 10;
				if (ptrs[i] < ptrEnds[i] ) {
					goto addMore;
				}
				oldptrs[i] = ptrs[i];
				ptrs[i] = NULL;
				continue;	
			}
			// deal with date search, sign is 'd'
			//else scores[nn] = 
			//	    ((unsigned char)(~ptrs[i][5]))*DATE_WEIGHT;
			// date sort
			else {
				scores[nn]=~(*((uint32_t *)&ptrs[i][6]));
				scores[nn]+=
				    scoreTablePtr[((unsigned char)ptrs[i][5])];
				if ( scores[nn]<0 ) scores[nn] = 0x7fffffff;
				// replicate the code below for speed
			dateAdvance:
				ptrs[i] += 10;
				if (ptrs[i] < ptrEnds[i] ) goto addMore;
				oldptrs[i] = ptrs[i];
				ptrs[i] = NULL;
				continue;
			}
		advance:
			// debug msg
			//log("added score=%05"INT32" totalscore=%05"INT32" to "
			//    "slotnum=%04"INT32" ptrList=%"INT32"",
			//    scoreTablePtr[((unsigned char)ptrs[i][5])],
			//    scores[nn],nn,i);
			// advance ptr to point to next score/docid 6 bytes
			//ptrs[i] += 6;
			//ptrs[i] += hks; // half key size
			ptrs[i] += 6;
			// if he points to his lists end or a different score
			// then remove him from the list of ptrs
			//if (ptrs[i] >= listEnd || ptrs[i][5] != ptrs[i][-1]){
			//if (ptrs[i] >= listEnd || (ptrs[i][5]!=ptrs[i][-1]&&
			//                           !useDateLists)) { 
			if (ptrs[i] >= listEnd || ptrs[i][5] != ptrs[i][-1] ){
				// if no more, we're done
				//if ( numPtrs == 1 ) goto done;
				// replace him with last ptr
				//ptrs[i] = ptrs[--numPtrs];
				// save him in case we have to roll back
				oldptrs[i] = ptrs[i];
				// signify his demis
				ptrs[i] = NULL;
				continue;
			}
			// otherwise, try to add more from this ptr list
			goto addMore;
		}
		// if docIds bits don't match, chain to next bucket
		if ( *(int32_t *)(ptrs[i]+1) != *(int32_t *)(docIdPtrs[nn]+1) ||
		     (*ptrs[i] & 0xfd) != (*docIdPtrs[nn] & 0xfd) ) {
			if ( ++nn >= numSlots ) nn = 0;
			// if we wrapped back, table is FULL!!
			if ( nn == nnstart ) goto panic;
			// count the number of times we miss
			m_numCollisions++;
			goto chain;
		}
		// got dup docid for the same termid due to index corruption?
		if ( /*! rat &&*/ explicitBits[nn] & ebits ) {
			// no point in logging since in thread!
			//int64_t dd ;
			//gbmemcpy ( &dd , ptrs[i] , 6 );
			//dd >>= 2;
			//dd &= DOCID_MASK;
			//fprintf(stderr,"got dup score for docid=%"INT64"\n",dd);
			if ( ! useDateLists ) goto advance;
			else if(sortByDate)   goto dateAdvance;
			else                  goto dateAdvance1;			
		}
		// sometimes the first list has dups!
		//if ( rat && hashIt ) goto advance;
		// if docIdBits match OR in the pure bits
		explicitBits[nn]  |= ebits;
		// . else if we are using hardCounts for *many* query terms...
		// . terms not explicitly required will still have ebits > 0
		//   in order to support boolean expressions along side of
		//   hard required terms
		// . non-required phrases can all share the same ebit when we
		//   have a lot of query terms, so they will not be
		//   able to imply hard-required, single-word terms but at 
		//   least they can add to scores[]. they have ebits of 0.
		// . an ebits of 0x80000000 means it's a hard required term
		if ( hc ) hardCounts[nn]++;
		// and add the score 
		if ( sign == '\0' || sign == '+' || sign == '*' )
			scores[nn] += scoreTablePtr[(unsigned char)ptrs[i][5]];
		// excluded this term
		else if (sign == '-') scores[nn] -= 500000000;
		// apply a date search weight if sign is 'd'
		//else  scores[nn] += (unsigned char)ptrs[i][5] * DATE_WEIGHT;
		// date sort
		else {
			//scores[nn] =~(*((uint32_t *)&ptrs[i][6]));
			scores[nn]+=scoreTablePtr[((unsigned char)ptrs[i][5])];
			if ( scores[nn]<0 ) scores[nn] = 0x7fffffff;
		}
		// advance our ptr
		if ( ! useDateLists ) goto advance;
		else if(sortByDate)   goto dateAdvance;
		else                  goto dateAdvance1;		
	}

	// get the winners
	goto getWinners;

 panic:
	// . keep cutting the maxDocId in half until all docids below
	//   it fit into our little hash table
	// . "downStep" will be reset to "step" on our next round of hash
	//downStep >>= 1;
	// . scan hash table to find a good maxDocId/downstep
	// . compute the average of all the docids
	downStep = maxDocId - lastMaxDocId;
	// and then hack it for good measure
	downStep >>= 1;
	// sanity test, log if not sane
	//log(LOG_LOGIC,"query: last=%"UINT32" downstep=%"UINT32" max=%"UINT32"",
	//    lastMaxDocId,downStep,maxDocId);
	// debug msg
	//log("panicing");
	// count
	m_numPanics++;
	// . if it is zero we're fucked! how can this happen really?
	// . very very rare
	// . TODO: look into this more
	if ( downStep == 0 || downStep >= maxDocId ) {
		log(LOG_LOGIC,"query: indextable: Major panic. "
		    "downstep=%"UINT32" maxDocId=%"UINT32" numPanics=%"INT32"",
		    downStep,maxDocId,m_numPanics);
		goto done;
	}
	// why is this still maxed out after a panic?
	if ( maxDocId == 0xffffffff && m_numPanics > 1 ) {
		log(LOG_LOGIC,"query: indextable: Logic panic. "
		    "downstep=%"UINT32" maxDocId=%"UINT32" numPanics=%"INT32"",
		    downStep,maxDocId,m_numPanics);
		goto done;
	}
	// decrease docid ceiling by half a step each time this is called
	maxDocId -= downStep ;
	// clear the hash table
	//for ( int32_t i = 0 ; i < numSlots ; i++ ) 
	//	docIdPtrs[i] = NULL ; // explicitBits[i] = 0;
	memset ( docIdPtrs , 0 , numSlots * 4 );
	// roll back m_nexti so hashTopDocIds2() works again
	m_nexti = m_oldnexti;
	// . now move each ptrs[i] backwards if we need to
	// . if the docid is in this hash table that panicked,move it backwards
	j = 0;
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {
		char *p = ptrs[i];
		// rollback special for date lists
		if ( useDateLists ) {
			// since we only have one sublist, as opposed to many
			// sublists defined by a one byte score, just roll
			// it all the way back
			//while ( p - hks >= pstarts[i] &&
			//	*(uint32_t *)((p-hks)+1) > lastMaxDocId){
			//	p -= hks;
			//	j++;
			//}
			// re-assign
			//ptrs[i] = p;
			if ( p ) j = (ptrs[i] - pstarts[i])/hks;
			ptrs[i] = pstarts[i];
			continue;
		}
		// was it emptied? if so we just negate the ptr and go back 6
		if ( ! p ) {
			// get last value
			p = oldptrs[i];
			// back him up to the last docid for this score
			//p -= 6;
			p -= hks;
			// if hashed int32_t ago, continue
			if ( *(uint32_t *)(p+1) <= lastMaxDocId )
				continue;
			j++;
		}
		// get his score
	        unsigned char score = (unsigned char)(p[5]);
		// was previous guy in this hash table? if so, rollback
		while ( p - 6 >= pstarts[i] &&
			*(uint32_t *)((p-6)+1) > lastMaxDocId && 
			(unsigned char)(p-6)[5] == score ) {
			p -= 6;
			j++;
		}
		// re-assign
		ptrs[i] = p;
	}
	if ( m_isDebug || g_conf.m_logDebugQuery )
		logf(LOG_DEBUG,"query: indextable: Rolled back over "
		     "%"INT32" docids.",j);
	// try to fit all docids into it from the beginning
	goto top;


	/////////////////////////////////////
	// scrape off any winners
	/////////////////////////////////////
 getWinners:
	// . rat needs to know what the primary ebit mask we need is
	// . this should be the ebits of the non-base lists ORed together
	// . call it ebitMask
	int32_t     tn ;
	TopNode *t  ;
	for ( int32_t i = 0 ; i < numSlots ; i++ ) {
		// skip empty slots
		//if ( explicitBits[i] == 0 ) continue;
		if ( docIdPtrs[i] == NULL ) continue;
		// . to be a winner it must pass the min mask test
		// . that is, any terms with '+' signs must be accounted for
		// . THIS has been replaced by bit #7, the hard-required bit
		//if ((explicitBits[i] & minMask) != minMask) {
		//	// clear the slot
		//	explicitBits[i] = 0;
		//	continue;
		//}
		// add to tree if we're returning a *lot* of docids
		if ( rat && lastRound && m_topTree ) {
			// skip if zero or negative (negative query term)
			if ( scores[i] <= 0 ) { docIdPtrs[i] = NULL; continue;}
			// get implied bits from explicit bits
			qvec_t ibits = bmap [ explicitBits[i] ];
			// . we need to have the required term count to make it
			// . this is increased by one with each call to
			//   addLists_r() and start with a value of 1 or 2
			if ( (ibits & ebitMask) != ebitMask ) {
				docIdPtrs[i] = NULL; continue; }
			// we now also check the hard count
			if ( hardCounts[i] < minHardCount ) {
				docIdPtrs[i] = NULL; continue; }
			// . WE GOT A WINNER, ADD IT TO THE TREE
			// . bscore's bit #6 (0x40) is on if he has all terms
			//   explicitly, use this to disallow a high-tiered
			//   node from topping a node that has bit #6 on
			tn =  m_topTree->getEmptyNode();
			t  = &m_topTree->m_nodes[tn];
			//t->m_bscore   = bscore;
			t->m_tier     = tier;
			t->m_score    = scores[i];
			t->m_docIdPtr = docIdPtrs[i];
			// we don't have a cluster rec for this guy yet, -3
			t->m_clusterLevel = -3;
			// it only matter if we have all explicitly or 
			// implicitly when doing rat
			if ( (explicitBits[i] & ebitMask) == ebitMask) {
				explicitCount++;
				t->m_bscore = 0x60; // 0x40 + 0x20
			}
			else {
				implicitCount++;
				t->m_bscore = 0x20;
			}
			// debug
			//if ( t->getDocId() == 071325612500LL )
			//	log("hey");
			// . nodes that have that all the terms explicitly
			//   cannot be passed by higher-tiered nodes, 
			//   otherwise they are game
			// . we can have dup docids in here from multiple tiers
			// . de-duping is done at display time
			// . this will not add if tree is full and it is
			//   less than the m_lowNode in score
			// . if it does get added to a full tree, lowNode will
			//   be removed
			//if ( ! m_topTree->checkTree ( 1 ) || tn == 4  ) 
			//	log("hey");
			m_topTree->addNode ( t , tn );
			//if ( ! m_topTree->checkTree ( 1 ) ) {
			//	char *xx = NULL; *xx = 0; }
			docIdPtrs[i] = NULL;
			continue;
		}
		// add it right away if we're rat, it did not get nuked
		// by a negative
		if ( rat ) {
			// skip if zero or negative (negative query term)
			if ( scores[i] <= 0 ) {	
				docIdPtrs[i] = NULL; continue; }
			// get implied bits from explicit bits
			qvec_t ibits = bmap [ explicitBits[i] ];
			// . we need to have the required term count to make it
			// . this is increased by one with each call to
			//   addLists_r() and start with a value of 1 or 2
			if ( (ibits & ebitMask) != ebitMask ) {
				docIdPtrs[i] = NULL; continue; }
			// we now also check the hard count
			if ( hardCounts[i] < minHardCount ) {
				docIdPtrs[i] = NULL; continue; }
			// mark it
			//log("adding %"INT32"",newTopDocIds2);
			// sanity check
			if ( newTopDocIds2 >= m_maxTopDocIds2 ) {
				log(LOG_LOGIC,"query: bad newTopDocIds2.");
				char *xx = NULL; *xx = 0; }
			// . store in the buffer
			// . we now save ebits because a phrase in the base
			//   block may imply a term we add later on, but do
			//   not have explicitly. this way we still get it.
			m_topDocIdPtrs2 [ newTopDocIds2 ] = docIdPtrs   [i];
			m_topScores2    [ newTopDocIds2 ] = scores      [i];
			m_topExplicits2 [ newTopDocIds2 ] = explicitBits[i];
			m_topHardCounts2[ newTopDocIds2 ] = hardCounts  [i];
			newTopDocIds2++;
			docIdPtrs[i] = NULL;
			continue;
		}
		// . get his bit score
		// . pass in the EXplicit bits... will map to # of implicit 
		//   single, unignored word bits on. that is bscore.
		// . so a phrase will turn on bits for its single word 
		//   constituents
		// . TODO: do not use a map for this, instead OR in the
		//         implicit bits from each QueryTerm. that way we
		//         are not limited to 16 query terms for the map's sake

		// MDW: how can we tell if we have all terms implicitly
		//      without the bitScores[] array? getting an explicit
		//      count is easy, just add one for each term you have
		//      to the hash bucket.
		// MDW: we still need to keep the bitScores[] array for
		//      boolean queries, however...
		// MDW: we could support boolean queries with int32_t 
		//      sequences of ORs by making all the ORs to one bit!
		// MDW: we could use the int64_t bit vector, one per bucket
		//      but we should probably OR in the implicits at 
		//      intersection time, AND we should keep a separate count
		//      for the number of explicits. but all of this would
		//      be in addition to the LORs (Long ORs) or UORs
		//      (Unified ORs)

		unsigned char bscore = bitScores [ explicitBits[i] ] ;
		// if requireAllTerms is true, only accept docs that have ALL
		// terms either explicitly or implicitly
		//if ( requireAllTerms2 && ! (bscore & 0x60) ) continue;
		// . if bit #7 is SET iff he has ALL HARD-required terms,if any
		// . hard-required terms typically have + signs
		// . boolean logic also works with hard required terms
		// . if we don't meet that standard, skip this guy
		// . TODO: make bit vector a int64_t so we can have 64
		//         query terms. then AND this with the hardRequired
		//         bit vector to make sure we got it all. Or better
		//         yet, have a hardCount that gets inc'd everytime
		//         a docid matches a hard required term.
		if ( ! (bscore & 0x80) ) {
			// clear the slot
			//explicitBits[i] = 0;
			docIdPtrs[i] = NULL;
			continue;
		}
		// must have all hard counts, too (for boolean queries)
		if ( hardCounts[i] < minHardCount ) {
			docIdPtrs[i] = NULL;
			continue;
		}
		// . count EXplicit matches
		// . bit #6 is on iff he has all "required terms" EXplicitly
		// . "required terms" just means all the terms that we would
		//   need to have if we were a default AND engine
		// . now he must have all phrase bits, too, where
		//   phrases start and end with non-ignored single words
		// . if ((explicitBits[i] & requiredBits) == requiredBits ) 
		//	  explicitCount++;
		if ( (bscore & 0x40) ) 
			explicitCount++;
		// . count IMplicit matches
		// . bit #5 is on if he has all requird terms IMplicitly
		// . if the doc has the phrase "A B" then he has term A and 
		//   term B IMplicitly
		// . if ( bscore >= maxbscore ) implicitCount++;
		if ( bscore & 0x20 ) 
			implicitCount++;
		// . don't let explicits always sort above the implicits
		// . but guys with all required terms should always sort
		//   above guys without, so we DO NOT clear 0x20 (bit #5)
		// . might as well clear bit #7, too, it should always be set
		// . REMOVE THIS line if you want any doc that has
		//   all the terms EXplicitly to always outrank any 
		//   doc that only has them IMplicitly
		bscore &= ~0xc0;
		// add in our hard count (for boolean queries)
		bscore += hardCounts[i];
		// add to tree if we're returning a *lot* of docids
		if ( m_topTree ) {
			// . WE GOT A WINNER, ADD IT TO THE TREE
			// . bscore's bit #6 (0x40) is on if he has all terms
			//   explicitly, use this to disallow a high-tiered
			//   node from topping a node that has bit #6 on
			tn =  m_topTree->getEmptyNode();
			t  = &m_topTree->m_nodes[tn];
			t->m_bscore   = bscore;
			t->m_tier     = tier;
			t->m_score    = scores[i];
			t->m_docIdPtr = docIdPtrs[i];
			// we don't have a cluster rec for this guy yet, -3
			t->m_clusterLevel = -3;
			// . nodes that have that all the terms explicitly
			//   cannot be passed by higher-tiered nodes, 
			//   otherwise they are game
			// . we can have dup docids in here from multiple tiers
			// . de-duping is done at display time
			// . this will not add if tree is full and it is
			//   less than the m_lowNode in score
			// . if it does get added to a full tree, lowNode will
			//   be removed
			m_topTree->addNode ( t , tn );
			docIdPtrs[i] = NULL;
			continue;
		}

		// empty the slot for next round of hashing
		//explicitBits[i] = 0;
		// branch on the bit score
		if ( bscore < minTopBitScore ) {
			// clear the slot
			//explicitBits[i] = 0;
			docIdPtrs[i] = NULL;
			continue;
		}
		// automatically add to top if our bscore is the highest so far
		if ( bscore > minTopBitScore ) goto addIt;
		// docId must have a better score if it tied matching terms
		if ( scores[i] < minTopScore ) {
			// clear the slot
			//explicitBits[i] = 0;
			docIdPtrs[i] = NULL;
			continue;
		}
		if ( scores[i] > minTopScore ) goto addIt;
		// continue if docId is too big
		if ( *(uint32_t *)(docIdPtrs[i]+1) >
		     *(uint32_t *)(minTopDocIdPtr+1) ) {
			// clear the slot
			//explicitBits[i] = 0;
			docIdPtrs[i] = NULL;
			continue;
		}
		// if top is equal, compare lower 6 bits
		if ( (*(uint32_t *)(docIdPtrs[i]  +1)       ==
		      *(uint32_t *)(minTopDocIdPtr+1))          &&
		     (*(unsigned char *)(docIdPtrs[i]  ) & 0xfc) >=
		     (*(unsigned char *)(minTopDocIdPtr) & 0xfc)          ) {
			// clear the slot
			//explicitBits[i] = 0;
			docIdPtrs[i] = NULL;
			continue;
		}
	addIt:
		// if we have less than the max, add right away
		if ( weakest == -1 ) j = numTopDocIds++ ;
		else                 j = weakest;
		// debug msg
		/*
		if ( weakest >= 0 ) 
			log("bscore=%04"UINT32" score=%04"INT32" topp+1=%08"XINT32" "
			"replacing "
			     "#%02"INT32" bscore=%04"UINT32" score=%04"INT32" topp+1=%08"XINT32"",
			     bscore,scores[i],*(int32_t *)(docIdPtrs[i]+1),
			     j,topb[j],tops[j],*(int32_t *)(topp[j]+1));
		else
		       log("bscore=%04"UINT32" score=%04"INT32" topp+1=%08"XINT32" adding #%"INT32"",
			   bscore,scores[i],*(int32_t *)(docIdPtrs[i]+1),j);
		*/
		//  now we got in the top
		tops  [j] = scores[i] ;
		// just use the pure bits to set this bit score
		topb  [j] = bscore ; 
		// do not store actual docid, just a pointer to the low 6 bytes
		topp  [j] = docIdPtrs[i];
		// . save number of terms we have EXplicitly
		// . only used for tier integration code in filterTopDocIds()
		tope  [j] = getNumBitsOn ( (qvec_t)
					   (explicitBits[i] & requiredBits));
		// clear the slot
		//explicitBits[i] = 0;
		docIdPtrs[i] = NULL;

		// debug msg
		//log("added #%"INT32" docid = %"UINT64"  minPtr=%"UINT32"",
		//j,topd[j],(int32_t)minPtr);
		// don't get new weakest parms if we're still under limit
		if ( numTopDocIds >= docsWanted ) 
			weakest = getWeakestTopDocId ( topp            ,
						       tops            ,
						       topb            ,
						       numTopDocIds    ,
						       &minTopBitScore ,
						       &minTopScore    ,
						       &minTopDocIdPtr );
	}

	// adjustStep:
	// if rat then store the maxDocId for re-hashing m_topDocIdPtrs2
	if ( rat ) {
		// sanity check
		if ( newTopDocIds2 >= m_maxTopDocIds2 ) {
			log(LOG_LOGIC,"query: bad newTopDocIds2b.");
			char *xx = NULL; *xx = 0; }
		// . if 2nd last guy is within 50 of us then bury the last guy
		// . this guarantees all maxDocIds stored in m_topDocIdPtrs2
		//   are at least 50 units away from each other, with the
		//   exception of the last one added, of course, and this
		//   allows us to save memory (see IndexTable::alloc() above)
		if ( lastGuy2 >= 0 && newTopDocIds2 - lastGuy2 < 50 ) {
			newTopDocIds2--;
			m_topDocIdPtrs2 [ lastGuy ] = 
				m_topDocIdPtrs2 [ newTopDocIds2 ];
			m_topScores2    [ lastGuy ] = 
				m_topScores2    [ newTopDocIds2 ];
			m_topExplicits2 [ lastGuy ] =
				m_topExplicits2 [ newTopDocIds2 ];
			m_topHardCounts2[ lastGuy ] =
				m_topHardCounts2[ newTopDocIds2 ];
			// and replace him...
			lastGuy = lastGuy2;
		}
		//log("adding marker %"INT32"",newTopDocIds2);
		// overwrite last entry if no actual docids were added since
		// then, this allows us to save memory and fixes the seg fault
		//if ( newTopDocIds2 > 0 && 
		//     m_topScores2 [ newTopDocIds2 - 1 ] == 0 )
		//	newTopDocIds2--;
		m_topDocIdPtrs2 [ newTopDocIds2 ] = (char *)maxDocId;
		m_topScores2    [ newTopDocIds2 ] = 0;
		newTopDocIds2++;
		// update second last guy
		lastGuy2 = lastGuy ;
		lastGuy  = newTopDocIds2 - 1 ;
	}		
	// . reset our step down function
	// . do not reset this if we've panicked more than 20 times
	// . globalspec's single term query termlist's docids were all
	//   essentially sequential thus requiring VERY small steps of like
	//   100 or something... it seemed to block forever.
        if ( m_numPanics < 5 ) downStep = step ;
        // log it the first time it happens
        else if ( m_numPanics >= 5 && printed != m_numPanics ) {
                printed = m_numPanics;
                // it should be impossible that downStep be 0 or small like
                // that because we have a lot of slots in the hash table
                //char *qq = "";
                //if ( m_q && m_q->m_orig && m_q->m_orig[0] ) qq = m_q->m_orig;
                log(LOG_INFO,"query: Got %"INT32" panics. Using small steps of "
                    "%"INT32"",m_numPanics,downStep);
                // set step to downStep, otherwise, maxDocId will not be
                // able to be decreased close to within lastMaxDocId because
                // it is only decremented by downStep above, but
                // incremented below by step below. i think this matters
                // more for date termlists cuz those use lastMaxDocId
                step = downStep;
        }
	//#else
	//if ( m_numPanics > 0 && printed != m_numPanics ) {
	//	printed = m_numPanics;
	//	// only print every 20 panics
	//	if ( (printed % 20) == 0 )
	//		log(LOG_INFO,"query: Got %"INT32" panics.",m_numPanics);
	//}
        //downStep = step;
	//#endif
	// save it to check for wraps
	oldDocId = maxDocId;
	// if we were maxxed, we're done
	if ( maxDocId == (uint32_t)0xffffffff ) goto done;
	// save the last maxDocId in case we have to rollback for a panic
	lastMaxDocId = maxDocId;
	// now advance the ceiling
	maxDocId += step;
        // if wrapped, set to max
        if ( maxDocId <= oldDocId ) {
                maxDocId = (uint32_t)0xffffffff;
                // . if we panic after this, come down "half the distance to
                //   the goal line" and try to hash all the docIds in the
                //   range: (lastMaxDocId,maxDocId-downStep]
                // . downStep is divided by 2 (right shifted) in panic: above
                // . otherwise, downStep can be too big and mess us up!
                downStep = (maxDocId - lastMaxDocId);
        }
        // sanity check
        if ( maxDocId - (downStep / 2) <= lastMaxDocId+1 )
                log("query: Got infinite loop criteria.");
	
	// . do another round
	// . TODO: start with bottom of ptrs to take advantage of cache more!
	goto top;

 done:

	int64_t *topd;
	bool       didSwap ;

	// . if we're rat and we've hashed all the termlists for this tier
	//   then let's compute ex/implicitCount
	// . we need to store the explicit bits to compute the bscore!!
	// . if we're rat then we MUST have all terms in every doc...
	// . if we're using TopTree and done for this tier, set the counts
	if ( m_topTree ) { // && ((rat && lastRound) || ! rat) ) {
		// set the number of exact matches
		m_numExactExplicitMatches [ tier ]=explicitCount;
		// implicit matches
		m_numExactImplicitMatches [ tier ]=implicitCount+explicitCount;
		// . set our # of top docids member var
		// . CAUTION: THIS COUNT INCLUDES DUPS FROM OTHER TIERS
		m_numTopDocIds            [ tier ] = m_topTree->m_numUsedNodes;
	}

	// update the new count now
	m_numTopDocIds2 = newTopDocIds2;

	// . if we use top tree, TRY to estimate the hits now
	// . will only estimate if this is the first round
	if ( m_topTree ) goto estimateHits;


	// point to straight docids
	topd = m_topDocIds [ tier ];

	// . if we're rat, then store the m_topDocIdPtrs2/m_topScores2 info
	//   into topd/tops... but only if this is the last list...
	// . non-rat method just keeps the list of winners as it goes, but
	//   we only compute them at the very end.
	if ( rat && lastRound ) {
		// now get the top docsWanted docids from 
		// m_topDocIdPtrs2/Scores2 and store in m_topDocIds/m_topScores
		//unsigned char  minb = 0 ;
		int16_t          minb = 0 ;
		int32_t           mins = 0 ;
		int64_t      mind = 0 ;
		char          *minp = NULL ;
		int32_t           mini = -1 ;
		int32_t i = 0 ;
		// point to final docids and scores
		int64_t  *topd = m_topDocIds [ tier ];
		int32_t       *tops = m_topScores [ tier ];
		char      **tdp2 = m_topDocIdPtrs2 ;
		int32_t count = 0;
		//unsigned char bscore;
		int16_t       bscore;
		// count our explicits in this loop
		explicitCount = 0;
		for ( ; i < m_numTopDocIds2 ; i++ ) {
			// skip if it is a stored maxDocId
			if ( m_topScores2[i] <= 0  ) continue;
			// count hits with all terms implied
			count++;
			// get the bit score, # terms implied
			bscore = bitScores [ m_topExplicits2[i] ] ;
			// count it if it has all terms EXplicitly
			if ( (bscore & 0x40) && 
			     m_topHardCounts2[i] >= minHardCount ) 
				explicitCount++;
			// get the number of terms we got either EX or IMplicit
			//bscore &= ~0xc0 ;
			// REMOVE THIS line if you want any doc that has
			// all the terms EXplicitly to always outrank any 
			// doc that only has them IMplicitly
			bscore &= ~0xffc0 ; // turn off 0x20? (0xa0)
			// add in the hard count, will be 0 most of the time
			bscore += m_topHardCounts2[i];
			// if we're one of the first "docsWanted" add right now
			if ( count <= docsWanted ) {
				mini = count - 1;
				goto gotIt;
			}
			// this was taken from IndexTable.h::getWeakest...()
			if ( bscore < minb ) continue;
			if ( bscore > minb ) goto gotIt;
			if ( m_topScores2[i] < mins ) continue;
			if ( m_topScores2[i] > mins ) goto gotIt;
			if ( *(uint32_t *)(tdp2[i]+1  )  > 
			     *(uint32_t *)(minp+1)    ) continue;
			if ( *(uint32_t *)(tdp2[i]+1  )  < 
			     *(uint32_t *)(minp+1)    ) goto gotIt;
			if ( (*(unsigned char *)(tdp2[i]    ) & 0xfc) >
			     (*(unsigned char *)(minp) & 0xfc) ) continue;
		gotIt:
			gbmemcpy ( &topd[mini] , tdp2[i] , 6 );
			topd[mini] >>= 2;
			topd[mini] &= DOCID_MASK;
			topp[mini] = tdp2        [i];
			tops[mini] = m_topScores2[i];
			// bit score
			topb[mini] = bscore ;
			// debug msg
			//log("d=%"INT64" takes slot #%"INT32" (score=%"INT32" bscore=%"UINT32")",
			//    topd[mini],mini,tops[mini],(int32_t)topb[mini]);
			// . save number of terms we have EXplicitly
			// . used 4 tier integration code in filterTopDocIds()
			tope[mini] = getNumBitsOn ( (qvec_t)
					  (m_topExplicits2[i] & requiredBits));
			// add in the hard count, usually this is 0
			tope[mini] = tope[mini] + m_topHardCounts2[i];
			// are we still filling up the list? continue, if so.
			if ( count < docsWanted ) continue;
			// find new min in the list
			//minb = 0xff;
			minb = 0x7fff;
			mins = 0x7fffffff;
			mind = 0LL;
			mini = -1000000; // sanity check
			for ( int32_t j = 0 ; j < docsWanted ; j++ ) {
				if ( topb[j] > minb ) continue;
				if ( topb[j] < minb ) goto gotMin;
				if ( tops[j] > mins ) continue;
				if ( tops[j] < mins ) goto gotMin;
				if ( topd[j] < mind ) continue;
			gotMin:
				minb = topb[j];
				mins = tops[j]; 
				mind = topd[j];
				minp = topp[j];
				mini = j; 
			}
			//log("mini=%"INT32" minb=%"INT32" mins=%"INT32" mind=%"INT64"",
			//    mini,(int32_t)minb,(int32_t)mins,(int64_t )mind);
		}
		// how many top docids do we have? don't exceed "docsWanted"
		numTopDocIds  = count; 
		if ( numTopDocIds > docsWanted ) numTopDocIds = docsWanted;
		implicitCount = count;
	}

	// . fill in the m_topDocIds array
	// . the m_topDocIdPtrs array is score/docid 6 byte combos
	else if ( ! rat ) {
		for ( int32_t i = 0 ; i < numTopDocIds ; i++ ) {
			gbmemcpy ( &topd[i] , topp[i] , 6 );
			topd[i] >>= 2;
			topd[i] &= DOCID_MASK;
		}
	}

	// set the number of exact matches
	m_numExactExplicitMatches [ tier ] = explicitCount;

	// implicit matches
	m_numExactImplicitMatches [ tier ] = implicitCount;

	// set our # of top docids member var
	m_numTopDocIds [ tier ] = numTopDocIds;

	// . now sort by m_topBitScores/m_topScores/m_topDocIds
	// . use a local static ptr since gbsort doesn't have THIS ptr
	// . do a quick bubble sort
 keepSorting:
	// assume no swap will happen
	didSwap = false;
	for ( int32_t i = 1 ; i < numTopDocIds ; i++ ) {
		// continue if no switch needed
		if ( topb [i-1] >  topb [i] ) continue;
		if ( topb [i-1] <  topb [i] ) goto doSwap;
		if ( tops [i-1] >  tops [i] ) continue;
		if ( tops [i-1] <  tops [i] ) goto doSwap;
		if ( topd [i-1] <= topd [i] ) continue;
	doSwap:
		int32_t           tmpScore     = tops [i-1];
		unsigned char  tmpBitScore  = topb [i-1];
		int64_t      tmpDocId     = topd [i-1];
		char           tmpe         = tope [i-1];
		tops [i-1]  = tops [i  ];
		topb [i-1]  = topb [i  ];
		topd [i-1]  = topd [i  ];
		tope [i-1]  = tope [i  ];
		tops [i  ]  = tmpScore;
		topb [i  ]  = tmpBitScore;
		topd [i  ]  = tmpDocId;
		tope [i  ]  = tmpe;
		didSwap = true;
	}
	// if it's sorted then return
	if ( didSwap ) goto keepSorting;

 estimateHits:
	// . now extrapolate the total # of hits from what we got
	// . use default AND for this
	// . use this for boolean queries, too
	// . only set for the first call, so this number is consistent!!
	// . little graph: X's are matching terms, O's are unmatched
	//   and the hyphens separate stages. see the combinations.
	// . list #1      list #2
	// . X  ------->   X
	// . X  ------->   X
	// . X  ------->   X
	// . O             O
	// . O             O
	// . O             O
	// . ----------------
	// . O             O
	// . O             O
	// . O             O
	// . O
	// . O
	// . O
	// . we can re-compute if we didn't get many hits the first time!
	if ( m_estimatedTotalHits == -1 || m_doRecalc ) {
		// # of tried and untried combinations respectfully
		double   tried = 1.0;
		double untried = 1.0;
		double noretry = 1.0;
		// minimum term frequency of the eligible query terms
		int64_t mintf = 0x7fffffffffffffffLL;
		// . total hits we got now
		// . we use explicit, because we're only taking combinations
		//   of non-negative terms and positive phrase terms, using
		//   implicit matches would mess our count up
		// . furthermore, re-arranging query words would change the
		//   hit count because it would change the implicit count
		int32_t totalHits = explicitCount;
		// . use combinatorics, NOT probability theory for this cuz
		//   we're quite discrete
		// . MOST of the error in this is from inaccurate term freqs
		//   because i think this logic is PERFECT!!!
		// . how many tuple combinations did we have?
		// . do not use imap here
		for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
			// component lists are merged into one compound list
			if ( m_componentCodes[i] >= 0 ) continue;
			// skip if negative or unsigned phrase
			QueryTerm *qt = &m_q->m_qterms[i];
			if ( qt->m_termSign == '-' ) continue;
			if ( qt->m_termSign =='\0' && qt->m_isPhrase) continue;
			// if its a boolean query under a NOT sign
			if ( qt->m_underNOT ) continue;
			// get current total size of list #i(combine all tiers)
			int32_t total = 0;
			for ( int32_t j = 0 ; j < m_numTiers ; j++ ) {
				int32_t size = lists[j][i].getListSize();
				if ( size >= 12 ) size -= 6;
				size /= 6;
				total += size;
			}
			// how many docs have this term?
			int64_t tf = m_q->m_termFreqs[i];
			// . multiply to get initial # of combinations of terms
			// . "tried" means we tried these combinations to 
			//   produce the "totalHits" search results
			tried *= (double)total;
			// get # of untried combinations
			untried *= (double) ( tf - totalHits );
			// get # of combinations we tried amongst STAGE0 guys
			// so we don't retry in untried
			noretry *= (double) ( total - totalHits );
			// count required terms
			//nn++;
			// if we only have one required term, nothing isuntried
			// record the min tf as a safety catch
			if ( tf < mintf ) mintf = tf;
		}
		// don't retry combos tried in "tried" count (intersection)
		untried -= noretry;
		// don't go negative... if tf == total
		if ( untried < 0.0 ) untried = 0.0;
		// dont divide by 0
		if ( tried < 1.0 ) tried = 1.0;
		// . out of "tried" combinations we got "totalHits" hits!
		// . what's the prob. a particular combination is a hit?
		double percent = (double)totalHits / (double)tried;
		// out of the untried combinations,how many hits can we expect?
		m_estimatedTotalHits = totalHits + 
			(int64_t) (untried * percent);
		// don't exceed the max tf of any one list (safety catch)
		if ( m_estimatedTotalHits > mintf ) 
			m_estimatedTotalHits = mintf;
		// make it at least what we got for sure
		if ( m_estimatedTotalHits < totalHits )
			m_estimatedTotalHits = totalHits;
		// if we get a re-call, replace the total hits calc
		if ( explicitCount <= 0 ) m_doRecalc = true;
		else                      m_doRecalc = false;
	}
}

// . set all the m_final* member vars from the m_top* member vars
// . this essentially merges the results we got from each tier
/*void IndexTable::filterTopDocIds ( ) { 
	// return now if no tiers
	if ( m_numTiers <= 0 ) return;
	// what tier are we?
	char tier = m_numTiers - 1;
	int32_t count = 0;
	uint64_t   d;
	uint32_t        h;
	uint32_t        numBuckets = 0 ;
	int32_t                 bufSize    = 0 ;
	char                *buf = NULL ;
	uint64_t  *htable = NULL;
	TopNode            **vtable = NULL;
	int32_t                 explicitCount;
	int32_t                 implicitCount;
	int32_t                 docCount = 0;
	bool                 dedup;
	int32_t                 currTier = 0;

	// initialize this cuz we now count final results per tier
	for ( int32_t i = 0; i < MAX_TIERS; i++ )
		m_numDocsInTier[i] = 0;

	// . the TopTree is already sorted correctly so does not need filtering
	// . it is also immune to adding dups
	if ( ! m_topTree ) goto skip;

	numBuckets = m_topTree->m_numUsedNodes * 2;
	bufSize    = 12 * numBuckets;
	explicitCount = 0; // m_numExactExplicitMatches [ tier ];
	implicitCount = 0; // m_numExactImplicitMatches [ tier ] ;
	// set up the hash table for deduping the docids
	// since we may have multiple tiers in here
	dedup = (m_numTiers > 1);
	if ( dedup ) {
		// TODO: move this alloc into the TopTree allocation?
		buf = (char *)mmalloc ( bufSize , "Msg39" );
		if ( ! buf ) {
			log("query: Hash alloc of %"INT32" failed.",
			    bufSize);
			return;
		}
		htable  = (uint64_t *)buf;
		vtable  = (TopNode  **)(buf + numBuckets * 8) ;
		memset ( htable , 0 , numBuckets * 8 );
	}
	// loop over all results
	for ( int32_t ti = m_topTree->getHighNode() ; ti >= 0 ; 
	      ti = m_topTree->getPrev(ti) ) {
		// get the guy
		TopNode *t = &m_topTree->m_nodes[ti];
		// skip if no dedup needed
		if ( ! dedup ) goto skip2;
		// get his docid
		d = t->getDocId();
		//log("deduping d=%"UINT64"",d);
		// dedup him in the hash table
		h = (uint32_t)d % numBuckets;
		while ( htable[h] && htable[h] != d )
			if ( ++h >= numBuckets ) h = 0;
		// if got one, its a dup
		if ( htable[h] ) {
			// debug
			//log("%"UINT64" is a dup",d);
			// give site/content hash info to it
			// to save Msg38 a look up
			if ( t->m_siteHash ) {
				TopNode *f = (TopNode *)vtable;
				if ( f->m_siteHash ) continue;
				f->m_siteHash = t->m_siteHash;
				f->m_contentHash=t->m_contentHash;
			}
			// . mark him as a dup
			// . we cannot delete because TopTree will not update
			//   emptyNode correctly
			t->m_clusterLevel = 5;
			continue;
		}
		// otherwise, if not a dup, add to hash table
		htable[h] = d;
		vtable[h] = t;
	skip2:
		docCount++;
		if ( t->m_bscore & 0x40 ) {
			explicitCount++;
			implicitCount++;
		}
		else if ( t->m_bscore & 0x020 ) {
			implicitCount++;
		}
		// increment tier count, we can actually just do this in Msg39
		// since toptree stores the tiers
		//if ( t->m_tier > currTier )
		//	currTier = t->m_tier;
		//m_numDocsInTier[currTier]++;
	}
	// free hash table mem
	if ( buf ) { mfree ( buf , bufSize , "Msg39" ); buf = NULL; }
	// nodedup:
	m_finalNumTopDocIds = docCount;
	// use the last tier for this info, it should be right
	m_finalNumExactExplicitMatches = explicitCount;
	m_finalNumExactImplicitMatches = implicitCount;
	// debug print
	//if ( ! m_isDebug && ! g_conf.m_logDebugQuery ) return;
	if ( ! m_isDebug && ! g_conf.m_logDebugQuery ) return;
	// force log it cuz m_isDebug might be true
	logf(LOG_DEBUG,"query: indextable: Tier %"INT32" has %"INT32" deduped explicit "
	     "matches. dedup=%"INT32"",  (int32_t)tier,  explicitCount, dedup);
	logf(LOG_DEBUG,"query: indextable: Tier %"INT32" has %"INT32" deduped implicit "
	     "matches.",(int32_t)tier,implicitCount);
	for ( int32_t ti = m_topTree->getHighNode() ; ti >= 0 ; 
	      ti = m_topTree->getPrev(ti) ) {
		TopNode *t = &m_topTree->m_nodes[ti];
		logf(LOG_DEBUG,"query: indextable: [%"UINT32"] %03"INT32") docId=%012"UINT64" "
		    "sum=%"INT32" tier=%"UINT32" bs=0x%hhx cl=%"INT32"",
		    m_logstate,
		    count++ ,
		    t->getDocId() ,
		    t->m_score ,
		    t->m_tier ,
		    t->m_bscore,
		    (int32_t)t->m_clusterLevel);
	}
	return;

skip:	
	// debug msg
	//if ( m_isDebug || g_conf.m_logDebugQuery ) {
	if ( m_isDebug || g_conf.m_logDebugQuery ) {
		for ( int32_t j = m_numTiers-1 ; j < m_numTiers ; j++ ) {
			// force log it even if debug turned off
		       logf(LOG_DEBUG,"query: indextable: [%"UINT32"] tier #%"INT32" has "
			    "%"INT32" top docIds and %"INT32" exact explicit matches. "
			    "Note: not all explicit matches may have scores "
			    "high enough to be in this list of winners:",
			    m_logstate,
			    j , 
			    m_numTopDocIds[j], 
			    m_numExactExplicitMatches[j]) ;
			for ( int32_t i = 0 ; i < m_numTopDocIds[j]; i++) 
				logf(LOG_DEBUG,"query: indextable: [%"UINT32"] "
				     "%03"INT32") docId=%012"UINT64" "
				    "sum=%"INT32" imb=%"UINT32" [%"UINT32"] exb=%"INT32"",
				    m_logstate,
				    i,
				    m_topDocIds    [j][i] ,
				    m_topScores    [j][i] ,
				    // bit #7,#6 & #5 are special
				    (int32_t)(m_topBitScores [j][i] & 0x1f),
				    (int32_t)(m_topBitScores [j][i] ),
				    (int32_t)m_topExplicits [j][i] );
		}
	}

	// . if we were only called once we don't need to filter
	// . no, we might have docid dups due to errors
	// . we need to set m_numFiltered, too now
	//if ( m_numTiers <= 1 ) return m_topDocIds[0];

	// reset this
	int32_t nn = 0;

	// combine all docIds and bit scores from m_topDocIds[X] into 
	// these 2 arrays:
	int64_t      docIds    [ MAX_RESULTS * MAX_TIERS ];
	unsigned char  bitScores [ MAX_RESULTS * MAX_TIERS ];
	char           explicits [ MAX_RESULTS * MAX_TIERS ];
	int32_t           scores    [ MAX_RESULTS * MAX_TIERS ];
	char           tiers     [ MAX_RESULTS * MAX_TIERS ];
	// use gbmemcpy for speed reasons
	for ( int32_t i = 0 ; i < m_numTiers ; i++ ) {
		// how many top docIds in this one?
		int32_t nt = m_numTopDocIds[i];
		// bitch and skip if we don't have enough room to store
		if ( nn + nt > MAX_RESULTS * MAX_TIERS ) {
			log(LOG_LOGIC,"query: indextable: "
			    "filterTopDocIds had issue.");
			continue;
		}
		// . if he's got less than 10 exact matches, ignore him,
		//   unless he's the last tier
		// . no! now we don't allow tier jumping whatsoever
		//if ( m_numExactExplicitMatches[i] < 10 && i != m_numTiers-1)
		//	continue;
		// store all top docIds from all tiers into "docIds"
		gbmemcpy ( & docIds    [ nn ] , &m_topDocIds   [i] , nt * 8 );
		// also save the bit scores, for sorting
		gbmemcpy ( & scores    [ nn ] , &m_topScores   [i] , nt * 4 );
		gbmemcpy ( & bitScores [ nn ] , &m_topBitScores[i] , nt );
		gbmemcpy ( & explicits [ nn ] , &m_topExplicits[i] , nt );
		memset ( & tiers     [ nn ] , i                  , nt );
		// inc the count
		nn += nt;
	}

	// . now only bubble up a docId if it has a higher termBitScore!!!!
	// . a call stage auto advances to the next if ANY one of it's docIds
	//   does not have ALL terms in it! (including phrase termIds)
	// . convenience ptrs
	int64_t      *topd = docIds;
	int32_t           *tops = scores;
	unsigned char  *topb = bitScores;
	char           *tope = explicits;
	char           *topt = tiers;
	int64_t       tmpd;
	int32_t            tmps;
	unsigned char   tmpb;
	char            tmpe;
	char            tmpt;
	// max # of required terms we can have
	qvec_t  requiredBits = m_q->m_requiredBits ;
	int32_t            maxe = getNumBitsOn ( (qvec_t) requiredBits );
	// . now sort by m_topBitScores only
	// . use a local static ptr since gbsort doesn't have THIS ptr
	// . do a quick bubble sort
	bool didSwap = false;
 keepSorting:
	// reset flag
	didSwap = false;
	// do a bubble pass
	for ( int32_t i = 1 ; i < nn ; i++ ) {
		// . only allow guys to propagate upwards if they have
		//   more explicit bits
		// . this is only to ensure that the "Next 10" line up with
		//   the first 10
		// . if defender has more explicit bits he always wins
		//if ( tope [i-1] >  tope [i] ) continue;
		// . if defender has all explicit bits, he cannot be moved 
		//   aside because he may have been used for a previous page of
		//   results
		// . for instance, tier #0 had 550 explicit hits, those docids
		//   are sorted so that the top 550 (at least) will have
		//   all terms IMplicitly or EXplicitly. so tier #1's winners
		//   cannot bubble past a full EXplicit hit in tier #0, but
		//   they may bubble passed a full IMplicit hit in tier #0,
		//   which is OK, as int32_t as we don't much with tier #0's
		//   top 550 hits, otherwise we'd mess of the Next/Prev
		//   serps flow. this next line guarantees we won't muck it up.
		if ( tope [i-1] == maxe ) continue;
		// . if defender has more bits IMplicitly, he wins
		// . bit #5 is set if he has all terms implicitly
		if ( topb [i-1] > topb[i] ) continue;
		// . before switching, though, you must have higher scores,etc.
		// . otherwise, what's the point? and you'll mess up the
		//   search results ordering from page to page, which is the
		//   whole point of tracking the explicit bits... so we know
		//   if the previous tier fulfilled the request completely
		if ( topb [i-1] ==  topb [i] ) {
			if ( tops [i-1] >  tops [i] ) continue;
			if ( tops [i-1] == tops [i] &&
			     topd [i-1] <=  topd [i] ) continue;
		}
		tmpd        = topd [i-1];
		tmps        = tops [i-1];
		tmpb        = topb [i-1];
		tmpe        = tope [i-1];
		tmpt        = topt [i-1];
		topd [i-1]  = topd [i  ];
		tops [i-1]  = tops [i  ];
		topb [i-1]  = topb [i  ];
		tope [i-1]  = tope [i  ];
		topt [i-1]  = topt [i  ];
		topd [i  ]  = tmpd;
		tops [i  ]  = tmps;
		topb [i  ]  = tmpb;
		tope [i  ]  = tmpe;
		topt [i  ]  = tmpt;
		didSwap = true;
	}
	// if we did a swap then do another bubble pass until it's sorted
	if ( didSwap ) goto keepSorting;

	// debug point
	if ( m_isDebug || g_conf.m_logDebugQuery ) {
		logf(LOG_DEBUG,"query: indextable: imb is how many query "
		     "terms the result has IMPLICITLY. "
		     "(phrase terms imply single word terms)");
		logf(LOG_DEBUG,"query: indextable: exb is how many query "
		     "terms the result has EXPLICITLY.");
		logf(LOG_DEBUG,"query: indextable: the last number in []'s "
		     "is a bitmap. See Query.cpp::setBitScore to see where "
		     "those bits are defined.");
		for ( int32_t i = 0 ; i < nn ; i++) 
			logf(LOG_DEBUG,"query: indextable: [%"UINT32"] %03"INT32") final "
			     "docId=%012"UINT64" "
			    "sum=%"INT32" imb=%"UINT32" [0x%"XINT32"] exb=%"INT32" tier=%"INT32"",
			    m_logstate,
			    i,
			    topd [i] ,
			    tops [i] ,
			    // bit #7,#6 & #5 are special
			    (int32_t)(topb [i] & 0x1f),
			    (int32_t)(topb [i] & 0xe0),
			    (int32_t)tope[i] ,
			    (int32_t)topt[i] );
	}

	// we have nothing in m_finalTop* right now
	int32_t nf = 0;

	// tmp vars
	int64_t docId;
	int32_t j;

	// uniquify docIds from docIds[] into m_filtered
	for ( int32_t i = 0 ; i < nn ; i++ ) {
		// get the docId
		docId = docIds [ i ];
		// is this docId already in m_filtered?
		for ( j = 0 ; j < nf ; j++)
			if ( docId == m_finalTopDocIds [ j ] ) break;
		// skip if docId already in m_finalTopDocIds[]
		if ( j < nf ) continue;
		// otherwise, store it
		m_finalTopDocIds [ nf ] = docIds[i];
		m_finalTopScores [ nf ] = scores[i];
		nf++;
		// increment the tier count
		if ( tiers[i] > currTier )
			currTier = tiers[i];
		m_numDocsInTier[currTier]++;
		// break if we have enough
		if ( nf >= MAX_RESULTS ) break;
	}

	// . sanity check
	// . IndexTable::getTopDocIds: bad engineer. nf=17 ntd[1]=19
	// . this happens when we got lots of docid dups from errors
	// . the last time was same docid score but different punish bits
	int32_t min = m_numTopDocIds [ m_numTiers - 1 ];
	if ( nf < min ) {
		//errno = EBADENGINEER;
		//log("IndexTable::getTopDocIds: bad engineer. "
		//     "nf=%"INT32" ntd[%"INT32"]=%"INT32"",
		//     nf,m_numTiers-1,m_numTopDocIds[m_numTiers-1]);
		log("query: Got %"INT32" duplicate docids.",min - nf );
		//sleep(50000);
		// just count as an error for now
		//return NULL;
		// pad it with doc of the last one, maybe nobody will notice
		int64_t lastDocId = 0;
		if ( nf > 0 ) lastDocId = m_finalTopDocIds [ nf - 1 ];
		while ( nf < min && nf < MAX_RESULTS )
			m_finalTopDocIds [ nf++ ] = lastDocId;
	}

	m_finalNumTopDocIds = nf;
	
	// . often the exact count is bigger than the docids we got
	// . adjust it down so it's no longer bigger in case we had dup docids
	m_finalNumExactExplicitMatches=m_numExactExplicitMatches[m_numTiers-1];

	// store implict matches too so we can put the gray bar separating
	// results that have all terms from those that don't
	m_finalNumExactImplicitMatches=m_numExactImplicitMatches[m_numTiers-1];
}
*/
