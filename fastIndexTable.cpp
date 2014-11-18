#include "IndexTable.h"

IndexTable::IndexTable() { 
	// top docid info
	m_buf       = NULL; 
	m_topDocIds = NULL;
	m_topScores = NULL;
	m_numSlots = 0;
	reset();
}
IndexTable::~IndexTable() { reset(); }

void IndexTable::reset() {
	if ( m_buf ) mfree ( m_buf , m_bufSize ,"IndexTable"); 
	// free the table stuff
	if (m_docIdBits )mfree ( m_docIdBits  , m_numSlots * 8 ,"IndexTable");
	if (m_scores    )mfree ( m_scores     , m_numSlots * 4 ,"IndexTable");
	if ( m_termBits )mfree ( m_termBits   , m_numSlots * 2 ,"IndexTable");
        m_docIdBits       = NULL;
	m_scores          = NULL;
	m_termBits        = NULL;
	m_numTopDocIds    = 0;
	m_maxNumTopDocIds = 0;
	m_numSlots        = 0;
	m_mask            = 0;
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

// . returns false on error and sets errno
// . NOTE: termIds,termFreqs,termSigns are just referenced by us, not copied
// . sets m_startKeys, m_endKeys and m_minNumRecs for each termId
// . TODO: ensure that m_termFreqs[] are all UPPER BOUNDS on the actual #!!
//         we should be able to get an upper bound estimate from the b-tree
//         quickly!
// . we now support multiple plus signs before the query term
void IndexTable::set ( int32_t       docsWanted ,
		       int64_t *termIds    ,
		       int64_t *termFreqs  ,
		       char      *termSigns  ,
		       char      *numPlusses ,
		       char      *startTermNums , // phrase ranges
		       char      *endTermNums   , // phrase ranges
		       int32_t       numTerms      ) {
	// make sure our max score isn't too big
	//int32_t a     = MAX_QUERY_TERMS * MAX_QUERY_TERMS * 300   * 255   + 255;
	//int64_t aa=MAX_QUERY_TERMS * MAX_QUERY_TERMS * 300LL * 255LL + 255;
	//if ( a != aa ) { 
	//	log("IndexTable::set: MAX_QUERY_TERMS too big"); exit(-1); }
	// clear everything
	reset();
	// . HACK: we must always get the top 500 results for now
	// . so next results page doesn't have same results as a previous page
	// . TODO: refine this
	// . this is repeated in IndexReadInfo.cpp
	// . HACK: this prevents dups when clicking "Next X Results" link
	//if ( docsWanted < 500 ) docsWanted = 500;
	// remember these
	m_docsWanted    = docsWanted;
	m_termIds       = termIds;
	m_termSigns     = termSigns;
	m_startTermNums = startTermNums;
	m_endTermNums   = endTermNums;
	m_numTerms      = numTerms;
	// change termSign of '+' to '*' if it was truncated
	int32_t truncLimit = g_conf.m_indexdbTruncationLimit;
	// . termFreqs are already minimal, that is, approximate lower bound,
	//   since they subtract 2 PAGE_SIZEs worth of keys to make up for err
	// . see Rdb::getListSize() to see how we estimate termFreqs
	// . and then see Indexdb::getTermFreq() to see linear interpolation
	//   for truncated lists (should really be bell-curve interpolation)
	// . turn the '+' into a '*' to mark truncated, hard-required lists
	for ( int32_t i = 0 ; i < numTerms ; i++ ) {
		if ( termSigns[i] != '+'       ) continue;
		if ( termFreqs[i] < truncLimit ) continue;
		termSigns[i] = '*' ;
	}
	// . set score weight for each term based on termFreqs
	// . termFreqs is just the max size of an IndexList
	setScoreWeights ( termFreqs , numPlusses , numTerms );
	// . HACK: date hack
	// . set termSigns of the special date termIds to 'd' so addList()
	//   knows to throw these scores into the highest tier (just for them)
	// . also re-set the weights so the most significant 8-bits of our
	//   16-bit date is weighted by 256, and the low by 1
	for ( int32_t i = 0 ; i < numTerms ; i++ ) {
		if      ( termIds[i] == (0xdadadada & TERMID_MASK) ) {
			termSigns[i] = 'd';
			m_scoreWeights[i] = 256;
		}
		else if ( termIds[i] == (0xdadadad2 & TERMID_MASK) ) {
			termSigns[i] = 'd';
			m_scoreWeights[i] = 1;
		}
	}
}

// . the score weight is in [1,MAX_SCORE_WEIGHT=1000]
// . so max score would be MAX_QUERY_TERMS*1000*255 = 16*1000*255 = 4,080,000
// . now we use MAX_QUERY_TERMS * MAX_SCORE to be the actual max score so
//   that we can use 4,080,000 for truncated hard-required terms
// . TODO: make adjustable from an xml interpolated point map
void IndexTable::setScoreWeights ( int64_t *termFreqs  , 
				   char      *numPlusses ,
				   int32_t       numTerms   ) {
	// get an estimate on # of docs in the database
	//int64_t numDocs = g_titledb.getGlobalNumDocs();
	// this should only be zero if we have 0 docs, so make it 1 if so
	//if ( numDocs <= 0 ) numDocs = 1;
	// . compute the total termfreqs
	// . round small termFreqs up to half a PAGE_SIZE
	int64_t absMin  = PAGE_SIZE/(2*sizeof(key_t));
	int64_t minFreq = termFreqs[0];
	if ( minFreq < absMin ) minFreq = absMin;
	for ( int32_t i = 1 ; i < numTerms ; i++ ) {
		if ( termFreqs[i] > minFreq ) continue;
		if ( termFreqs[i] < absMin  ) continue;
		minFreq = termFreqs[i];
	}
	// loop through each term computing the score weight for it
	for ( int32_t i = 0 ; i < numTerms ; i++ ) {
		// reserve half the weight for up to 4 plus signs
		int64_t max = MAX_SCORE_WEIGHT / 3;
		// . 3 extra plusses can triple the score weight
		// . each extra plus adds "extra" to the score weight
		int32_t extra = (2 * MAX_SCORE_WEIGHT) / 9;
		// add 1/6 for each plus over 1
		if ( numPlusses[i] > 0 ) max += (numPlusses[i] - 1) * extra;
		// but don't exceed the absolute max
		if ( max > MAX_SCORE_WEIGHT ) max = MAX_SCORE_WEIGHT;
		// increase small term freqs to the minimum
		int64_t freq = termFreqs[i];
		if ( freq < absMin ) freq = absMin;
		// . now if one term is 100 times more popular than another
		//   it should count 1/100th as much
		// . since high scores are logs of the originals (to keep them
		//   in 8 bits) we should perhaps say:
		//   if one term is 100 times more popular than another
		//   it should count 1/log(100)th as much (TODO!)
		// . weight score by 300 
		// . the rarer the term, the lower the termFreq, the higher
		//   the score
		// . we're assured the end result must fit in a int32_t
		int32_t weight = ( max * minFreq ) / freq;
		// don't let this go to zero
		if ( weight <= 0 ) weight = 1; // if ( score < 0 ) score = 0
		m_scoreWeights[i] = weight; // (300 - score) + 100 ;
	}
}


// . this maps a term's bit mask to a score
// . this used to just count the # of bits on, but one singleton termId is more
//   important than one phrase termId
static uint16_t s_bitScores [ 65535 ] ;
static uint16_t s_maxBitMask;

//static int32_t s_collisions ;

// . returns false on error and sets errno
// . we hash the lists into the hash table
// . these "lists" MUST correspond 1-1 w/ m_termIds[],m_termFreqs[],m_termSigns
// . if "forcePhrases" is true we hard require all terms
// . lists are typically appendings of a bunch of lists so each list probably
//   does not have it's keys in order and may have unannihilated +/- recs
bool IndexTable::addLists (IndexList *lists, int32_t numLists, bool forcePhrases){
	// ensure they aren't messing with us after calling set() above
	if ( m_numTerms != numLists ) {
		errno = EBADENGINEER;
		return log("IndexTable::addLists: numLists=%"INT32", should be %"INT32"",
			   numLists, m_numTerms );
	}
	// which bits are for singletons?
	uint16_t singleMask = 0;
	uint16_t phraseMask = 0;
	int32_t           count      = 1;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		if ( m_startTermNums[i] == -1 ) singleMask |= (1 << i);
		else                            phraseMask |= (1 << i);
		count *= 2;
	}
	// count cannot exceed 65536
	if ( count > 65536 ) {
		log("IndexTable::addLists: bad engineer. grow the s_table.");
		count = 65536;
	}
	// remember for later
	s_maxBitMask = count - 1;
	// . init the table for converting a bit mask to a bit score
	// . we don't just rank by the # of bits on anymore since singleton
	//   bits are more important than phrase bits
	// . TODO: base s_bitScores[i] on termFreqs secondly!
	for ( int32_t i = 0 ; i < count ; i++ )
		s_bitScores [ i ] = 
			100 * getNumBitsOn ( i & singleMask ) +
			1   * getNumBitsOn ( i & phraseMask );
	// how many records/keys in all lists?
	int32_t numKeys = 0;
	for ( int32_t i = 0; i < numLists ; i++ ) 
		numKeys += lists[i].getNumKeys();
	// . grow hash table by that plus 20%
	// . grow table to powers of 2 only!
	// . increases m_numSlots by this much
	// . seems like we gotta lot of collisions so grow table by twice
	//   instead of 20%!
	if ( ! growTable ( (numKeys * 200) / 100 ) ) return false;
	// another timestamp
	log("IndexTable::addLists: adding %"INT32" lists now t=%"INT64", slots=%"INT32"",
	    numLists , gettimeofdayInMilliseconds() , m_numSlots );
	// keep track of collisions in hash table
	//s_collisions = 0;
	// now hash the docIds/score pairs in each key of each list
	for ( int32_t i = 0; i < numLists ; i++ ) {
		// . convert the list's termId to a termBitMask
		// . uses m_startTermNums and m_endTermNums for setting 
		//   multiple bits for phrases to dilute the effects of 
		//   truncation limit
		log("IndexTable::addLists: getting bit mask now t=%"INT64"",
		    gettimeofdayInMilliseconds() );
		uint16_t termBitMask = getTermBitMask ( i );
		log("IndexTable::addLists: got bit mask now t=%"INT64", sign=%c",
		    gettimeofdayInMilliseconds() , m_termSigns[i] );
		log("adding list #%"INT32", size=%"INT32"",i,lists[i].getListSize());
		addList ( &lists[i] , m_scoreWeights[i] , m_termSigns[i] ,
			  termBitMask ) ;
	}
	//log("IndexTable::addLists: collisions = %"INT32", #keys=%"INT32", #slots=%"INT32"",
	//    s_collisions, numKeys , m_numSlots );
	// another timestamp
	log("IndexTable::addLists: setting the top docIds now t=%"INT64"",
	    gettimeofdayInMilliseconds() );
	// now set the top docIds
	if ( ! setTopDocIds ( m_docsWanted , forcePhrases ) ) return false;
	// success
	return true;
}

// . TODO: this must be very fast!!!!
// . this is still taking 378ms to add about 500,000 keys!!! (from 7 lists)
// . we should be able to hand code assembly to do about 30 cycler per key
//   which would mean 500,000 keys would take 15 million cycles = 
//   which is 15/1400 of a second = ~10 ms. that's 30 times faster!!
void IndexTable::addList ( IndexList *list, int32_t scoreWeight, char termSign ,
			   uint16_t termBitMask ) {
	// . route if necessary
	// . these are specialized for speed
	// . exlcuded query term
	if ( termSign == '-' ) {addList2(list,termBitMask);return;}
	// . date search term
	if ( termSign == 'd' ) {addList3(list,scoreWeight,termBitMask);return;}
	// records are data-less, just keys
	int64_t      docIdBits;
	int16_t          score;
	int32_t           n;
	register unsigned char *k   = (unsigned char *) list->getList();
	unsigned char *end = (unsigned char *) list->getListEnd();
	// compute huge score
	for ( ; k < end ; k += sizeof(key_t) ) {
		// . get the score
		// . NOTE: scores are complemented for ordering purposes in db
		// . so the min score is actually the max
		// . so we uncomplement it here
		// . this is 6 cycles of assembly
		// . all of this is about 100 lines of assembly
		score = 255 - *(k+5);
		// . if delbit is set make the score negative
		// . i.e. make negative if this key is a delete
		// . this is dependent on endianess of machine
		// . we're small endian, and the lower int64_t comes first
		//   and IT stores the least significant int32_t first
		if ( *k & (unsigned char)0x01 == 0 ) score = -score;
		// just mask out everything else, but the docId from ith key
		docIdBits =(*((uint64_t *)k)) & 0x0000007ffffffffeLL;
		// . hash docId
		// . it's faster to use a power-of-2 table size and a mask
		n = ( (*(uint32_t *)k) >>1 ) & m_mask;
		// debug
		//log("docIdBits=%"UINT64" slot=%"INT32"",docIdBits,n);
		// . chain until we find empty bucket
		// . empty bucket has a score of 0
		while ( m_scores[n] != 0 && m_docIdBits[n] != docIdBits ) {
			//s_collisions++;
			//log("COLLISION with docIdBits=%"UINT64"",m_docIdBits[n]);
			if ( ++n >= m_numSlots ) n = 0;
		}
		// if it's new come here since m_termBits was not calloc'd
		// it was malloc'd so it may be garbage
		if ( m_scores[n] == 0 ) {
			m_docIdBits  [ n ] = docIdBits;
			m_termBits   [ n ] = termBitMask;
		}
		else
			m_termBits   [ n ] |= termBitMask;
		// . add the score
		// . weight the score by the idf (inverse termFreq)
		m_scores [ n ] +=  (int32_t)score * scoreWeight;
	}
}

// this is almost an exact dup of addList() above, but it's for terms with
// a minus term in front of them (exlcude from search results)
void IndexTable::addList2 ( IndexList *list, uint16_t termBitMask ) {
	// records are data-less, just keys
	int64_t     docIdBits;
	int16_t         score;
	int32_t          n;
	key_t        *k   = (key_t *) list->getList();
	key_t        *end = (key_t *) list->getListEnd();
	// compute huge score
	for ( ; k < end ; k++ ) {
		// . get the score
		// . NOTE: scores are complemented for ordering purposes in db
		// . so the min score is actually the max
		// . so we uncomplement it here
		score = 255 - *(((unsigned char *)k) + 5);
		// . if delbit is set make the score negative
		// . i.e. make negative if this key is a delete
		if ( *((uint32_t *)k) & 0x01 == 0 ) score = -score;
		// just mask out everything else, but the docId from ith key
		docIdBits = k->n0 &  0x0000007ffffffffeLL;
		// . hash docId
		// . it's faster to use a power-of-2 table size and a mask
		n = ((uint32_t)docIdBits) & m_mask;
		// . chain until we find empty bucket
		// . empty bucket has a score of 0
		while ( m_scores[n] != 0 && m_docIdBits[n] != docIdBits ) 
			if ( ++n >= m_numSlots ) n = 0;
		// adjust score based on if delBit is set or not
		if ( score < 0 ) m_scores   [ n ] += 500000000;
		else             m_scores   [ n ] -= 5000000000;
		m_docIdBits [ n ] = docIdBits;
		m_termBits  [ n ] = 0;
	}
}


// . just like addList()s above but for date search terms
void IndexTable::addList3 ( IndexList *list, int32_t scoreWeight, 
			   uint16_t termBitMask ) {
	// records are data-less, just keys
	int64_t     docIdBits;
	int16_t         score;
	int32_t          n;
	key_t        *k   = (key_t *) list->getList();
	key_t        *end = (key_t *) list->getListEnd();
	// compute huge score
	for ( ; k < end ; k++ ) {
		// . get the score
		// . NOTE: scores are complemented for ordering purposes in db
		// . so the min score is actually the max
		// . so we uncomplement it here
		score = 255 - *(((unsigned char *)k) + 5);
		// . if delbit is set make the score negative
		// . i.e. make negative if this key is a delete
		if ( *((uint32_t *)k) & 0x01 == 0 ) score = -score;
		// just mask out everything else, but the docId from ith key
		docIdBits = k->n0 &  0x0000007ffffffffeLL;
		// . hash docId
		// . it's faster to use a power-of-2 table size and a mask
		n = ((uint32_t)docIdBits) & m_mask;
		// . chain until we find empty bucket
		// . empty bucket has a score of 0
		while ( m_scores[n] != 0 && m_docIdBits[n] != docIdBits ) 
			if ( ++n >= m_numSlots ) n = 0;
		// if it's new come here since m_termBits was not calloc'd
		// it was malloc'd so it may be garbage
		if ( m_scores[n] == 0 ) {
			m_docIdBits  [ n ] = docIdBits;
			m_termBits   [ n ] = termBitMask;
		}
		else
			m_termBits   [ n ] |= termBitMask;
		// . HACK: date hack
		// . now this isn't perfect, but score is usually around 45
		//   when scoreWeight is 256
		// . it should be good enough most of the time
		m_scores [ n ] += score * scoreWeight * DATE_WEIGHT;
	}
}

// . returns false and sets errno on error
// . get the top X scoring docIds (docIdBits actually)
// . order by m_numTerms first then by score (complemented)
// . sets "numTopDocIds" to actual number we got
bool IndexTable::setTopDocIds ( int32_t topn , bool forcePhrases ) {
	// reset any existing buffer
	if ( m_buf ) {	mfree ( m_buf , m_bufSize,"IndexTable"); m_buf = NULL;}
	// timing debug
	int64_t startTime = gettimeofdayInMilliseconds();
	// make new buffer to hold top docids/scores/termBits
	m_bufSize = (8 + 4 + 2) * topn;
	m_buf = (char *) mmalloc ( m_bufSize ,"IndexTable" );
	if ( ! m_buf ) return log("IndexTable::setTopDocIds: mmalloc failed");
	// set our arrays as offsets into the allocated buffer
	m_topDocIds       = (int64_t      *) m_buf;
	m_topScores       = (int32_t           *) (m_buf + 8     * topn);
	m_topBitScores    = (uint16_t *) (m_buf + (8+4) * topn);
	m_maxNumTopDocIds = topn ;
	m_numTopDocIds    = 0 ;
	// . set a minimal mask from hard required ('+') terms
	// . NOTE: truncated termlists had their sign changed to a '*'
	uint16_t minMask = 0;
	// don't let i be >= 32 cuz << shift won't work right
	for ( int32_t i = 0 ; i < m_numTerms && i < 32 ; i++ ) 
		if ( m_termSigns[i]=='+' || forcePhrases ) minMask |= (1 << i);
	// add the first m_maxNumTopDocIds terms to list, no questions asked
	int32_t nt = 0;
	int32_t i;
	for ( i = 0 ; nt < m_maxNumTopDocIds && i < m_numSlots ; i++ ) {
		// skip empty buckets and negative scores
		if ( m_scores[i] <= 0 ) continue;
		// don't allow docId into initial top list if minMask not met
		if ( (m_termBits[i] & minMask) != minMask ) continue;
		m_topScores    [nt] = m_scores    [i];
		m_topBitScores [nt] = s_bitScores [ m_termBits[i] ];
		m_topDocIds    [nt] =(m_docIdBits[i]>>1) &0x0000003fffffffffLL;
		nt++;
	}
	m_numTopDocIds = nt;
	// these are the min info from the topDocIds
	char      minBitScore  ;
	int32_t      minScore     ;
	int64_t minDocId     ;
	// get new lowest parms
	getLowestTopDocId ( &minBitScore , &minScore , &minDocId );
	// timing debug
	// this chunk of code takes the longest (1-4 seconds), the bubble sort
	// is only 15ms
	log("setTopDocIds: phase 1 took %"INT64" ms", 
	    gettimeofdayInMilliseconds() - startTime );
	startTime = gettimeofdayInMilliseconds();	
	// your bitsOn must be >= minBitsOn and score > minScore to get into
	// our list of the top docIds
	for ( ; i < m_numSlots ; i++ ) {
		// skip empty buckets (or negative scores from '-' operator)
		if ( m_scores[i] <= 0 ) continue;
		// these must also meet the minMask
		if ( (m_termBits[i] & minMask) != minMask ) continue;
		// docId must have at least this many matching terms
		if ( s_bitScores [ m_termBits[i] ] < minBitScore ) continue;
		if ( s_bitScores [ m_termBits[i] ] > minBitScore ) goto addIt;
		// docId must have a better score if it tied matching terms
		if ( m_scores[i] < minScore ) continue;
		if ( m_scores[i] > minScore ) goto addIt;
		// continue if docId is too small
		if ( ((m_docIdBits[i]>>1) &0x0000003fffffffffLL) > minDocId ) 
			continue;
	addIt:
		// get the docId
		int64_t docId = (m_docIdBits[i]>>1) &0x0000003fffffffffLL;
		// kick out the lowest
		int32_t j = getLowestTopDocId ( );
		// debug
		//log("replacing #%"INT32" docId=%"INT64", bitsOn=%"INT32", score=%"INT32" "
		//    "with #%"INT32" docId=%"INT64", bitsOn=%"INT32", score=%"INT32"",
		//    j,m_topDocIds[j],m_topBitScores[j],m_topScores[j],
		//    i,docId,s_bitScores [ m_termBits[i] ],m_scores[i]);
		//  now we got in the top
		m_topScores    [j] = m_scores    [i];
		m_topBitScores [j] = s_bitScores [ m_termBits[i] ];
		m_topDocIds    [j] = docId;
		// get new lowest parms
		getLowestTopDocId ( &minBitScore , &minScore, &minDocId );
	}
	// timing debug
	log("setTopDocIds: phase 2 took %"INT64" ms", 
	    gettimeofdayInMilliseconds() - startTime );
	// sort the m_topDocIds/m_topScores/m_topBitScores arrays now
	sortTopDocIds();
	// timing debug
	log("setTopDocIds: @ phase 3 time is %"INT64" ms", 
	    gettimeofdayInMilliseconds() - startTime );
	return true;
}

void IndexTable::sortTopDocIds ( ) {
	// . now sort by m_topBitScores/m_topScores/m_topDocIds
	// . use a local static ptr since gbsort doesn't have THIS ptr
	// . do a quick bubble sort
	bool didSwap = false;
 keepSorting:
	for ( int32_t i = 1 ; i < m_numTopDocIds ; i++ ) {
		// continue if no switch needed
		if ( m_topBitScores [i-1] >  m_topBitScores [i] ) continue;
		if ( m_topBitScores [i-1] <  m_topBitScores [i] ) goto doSwap;
		if ( m_topScores    [i-1] >  m_topScores    [i] ) continue;
		if ( m_topScores    [i-1] <  m_topScores    [i] ) goto doSwap;
		if ( m_topDocIds    [i-1] <= m_topDocIds    [i] ) continue;
	doSwap:
		int32_t           tmpScore     = m_topScores     [i-1];
		uint16_t tmpBitScore  = m_topBitScores  [i-1];
		int64_t      tmpDocId     = m_topDocIds     [i-1];
		m_topScores     [i-1]  = m_topScores     [i  ];
		m_topBitScores  [i-1]  = m_topBitScores  [i  ];
		m_topDocIds     [i-1]  = m_topDocIds     [i  ];
		m_topScores     [i  ]  = tmpScore;
		m_topBitScores  [i  ]  = tmpBitScore;
		m_topDocIds     [i  ]  = tmpDocId;
		didSwap = true;
	}
	// if it's sorted then return
	if ( ! didSwap ) return;
	// otherwise keep sorting
	didSwap = false; 
	goto keepSorting; 
}

// . grow the hash table by "n" slots
// . returns false on alloc error, true on success
bool IndexTable::growTable ( int32_t growBy ) {
	// how big should the new arrays be?
	int32_t n = m_numSlots + growBy ;
	// round up to nearest power of 2 if we need to
	uint32_t highest;
	if ( getNumBitsOn ( n ) == 1 ) highest = n;
	// get the value of the highest lit bit (0,1,2,4,8, ...)
	else highest = getHighestLitBitValue ( n ) << 1 ;
	// bail if none list
	//if ( highest == 0 ) {
	//	errno = EBADENGINEER;
	//	return log("IndexTable::growTable: bad engineer");
	//}
	// overgrow in order for size to be a power of 2
	n = highest;
	// . alloc new arrays
	// . make sure to set all scores to 0
	int64_t *newDocIdBits  = (int64_t *) mmalloc ( n * 8,"IndexTable");
	int32_t      *newScores     = (int32_t      *) mcalloc ( n * 4,"IndexTable");
	uint16_t *newBits  = (uint16_t *)mmalloc(n*2,"IndexTable");
	// return false on alloc error
	if ( ! newDocIdBits  ||  ! newScores  ||  ! newBits ) {
		if ( newDocIdBits  ) mfree ( newDocIdBits ,n * 8,"IndexTable");
		if ( newScores     ) mfree ( newScores    ,n * 4,"IndexTable");
		if ( newBits       ) mfree ( newBits      ,n * 2,"IndexTable");
		return log("IndexTable::growTable: mmalloc failed");
	}
	// we use m_mask instead of the % operator for hashing
	m_mask = highest - 1;
	// rehash into the new arrays
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
                // skip the empty buckets 
		if ( ! m_scores[i] ) continue;
                // get the new slot number for this slot (might be the same!)
                //int32_t newn = (uint64_t)m_docIdBits[i] % n;
		int32_t newn = (uint64_t)m_docIdBits[i] & m_mask;
                while ( newScores [ newn ] != 0 ) if ( ++newn >= n ) newn = 0;
                // move the docIdBits/score/bitCount to this new slot
		newDocIdBits [newn] = m_docIdBits [i];
		newScores    [newn] = m_scores    [i];
		newBits      [newn] = m_termBits  [i];
        }
        // free the old guys
        mfree ( m_docIdBits , m_numSlots * 8, "IndexTable");
        mfree ( m_scores    , m_numSlots * 4, "IndexTable");
        mfree ( m_termBits  , m_numSlots * 2, "IndexTable");
        // assign the new guys
        m_docIdBits  = newDocIdBits;
	m_scores     = newScores;
	m_termBits   = newBits;
	// update m_numSlots to n
	m_numSlots = n;
        return true;
}

// . get number of docids in m_topDocIds
// . only return # of docIds that have all the termIds if "thatIncludeAllTerms"
int32_t IndexTable::getNumResults ( bool thatIncludeAllTerms ) {
	// return 0 if we have no topDocIds
	if ( ! m_topDocIds || m_numTopDocIds == 0 ) return 0;
	// if they don't need to include all terms just return this
	if ( ! thatIncludeAllTerms ) return m_numTopDocIds;
	// . if they want all terms, make a min bit mask
	// . turn all bits on and see what the bit score is
	uint16_t minBitScore = s_bitScores [ s_maxBitMask ];
	// otherwise, see if the top docIds have ALL the terms
	int32_t count = 0;
	for ( int32_t i = 0 ; i < m_numTopDocIds ; i++ ) {
		// skip empty buckets (or negative scores)
		if ( m_topScores   [i] <= 0 ) continue;
		if ( m_topBitScores[i] >= minBitScore ) count++;
	}
	// return that
	return count;
}

int64_t *IndexTable::getTopDocIds ( bool isAdmin ) { 
	if ( isAdmin ) {
		log("----- # top docIds = %"INT32"",m_numTopDocIds);
		for ( int32_t i = 0 ; i < m_numTopDocIds; i++ ) 
			//log("%"INT32") docId=%"INT64", # terms Matched=%"INT32", sum=%"INT32"",
			log("%"INT32") docId=%"INT64", bit score=%hu, sum=%"INT32"",
			    i,m_topDocIds[i],m_topBitScores[i],
			    m_topScores[i]);
	}
	return m_topDocIds; 
}

// . get the termBits for the termId represented by this list
// . only phrases may set multiple bits
uint16_t IndexTable::getTermBitMask ( int32_t i ) {
	// set the ith bit
	uint16_t mask = (1 << i);
	// get range of termIds this termId encloses, iff it's a phrase termId
	char a = m_startTermNums[i] ;
	char b = m_endTermNums  [i] ;
	// if not a phrase return now
	if ( a == -1 ) return mask;
	// otherwise set one bit for each query termId used by phrase
	for ( char j = a ; j <= b ; j++ ) mask |= (1 << j);
	// return that
	return mask;
}
