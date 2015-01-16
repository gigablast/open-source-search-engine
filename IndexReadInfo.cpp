#include "gb-include.h"

#include "IndexReadInfo.h"
#include "Datedb.h"

IndexReadInfo::IndexReadInfo() {
	m_numLists    = 0;
	m_isDone      = false;
}

// . initialize initial read info
// . sets m_readSizes[i] for each list
// . sets startKey/endKey for each list, too
// . startKey set passed endKey to indicate no reading
void IndexReadInfo::init ( Query *q , 
			   int64_t *termFreqs ,
			   int32_t docsWanted , char callNum ,
			   int32_t stage0 ,
			   int32_t *tierStage,
			   bool useDateLists , bool sortByDate , 
			   uint32_t date1 , uint32_t date2 ,
			   bool isDebug ) {
	// save ptr but don't copy
	m_q            = q;
	m_useDateLists = useDateLists;
	m_sortByDate   = sortByDate;
	m_date1        = date1;
	m_date2        = date2;
	m_isDebug      = isDebug;
	if ( m_useDateLists ) m_ks = 16;
	else                  m_ks = 12;
	m_hks = m_ks - 6;
	// . now set m_includeList array
	// . set to false if we determine termId to be ousted due to dpf
	// . loop over each termId in the query
	for ( int32_t i = 0 ; i < m_q->getNumTerms() ; i++ ) {
		// ignore some
		//m_ignore   [i] = m_q->m_ignore[i];
		// no need to gen keys if ignored
		//if ( m_ignore[i] ) continue;
		// nothing ignored initially
		m_ignore[i] = false;
		// make our arrays 1-1 with those in Query class, q
		if ( m_useDateLists ) {
			// remember, date is complemented in the key, so use
			// the larger date for the startKey
			*(key128_t *)&m_startKeys [i*m_ks] = 
			      g_datedb.makeStartKey(m_q->getTermId(i),m_date2);
			*(key128_t *)&m_endKeys   [i*m_ks] = 
			      g_datedb.makeEndKey  (m_q->getTermId(i),m_date1);
			continue;
		}
		
		*(key_t *)&m_startKeys [i*m_ks] = 
			g_indexdb.makeStartKey ( m_q->getTermId(i) );
		*(key_t *)&m_endKeys   [i*m_ks] = 
			g_indexdb.makeEndKey   ( m_q->getTermId(i) );
	}
	// no negatives
	for ( int32_t i = 0; i < MAX_TIERS; i++ ){
		if ( tierStage[i] < 0 ) 
			tierStage[i] = 0;
	}

	// -1 means to use default
	if ( stage0 <= 0 ) {
		// adjust for dateLists, proportionally
		if ( m_useDateLists )
			m_stage[0] = (tierStage[0] * (16-6)) / (12-6);
		else
			m_stage[0] = tierStage[0]; // STAGE0;
	}
	else               
		m_stage[0] = stage0 * m_hks + 6;

	// for all the other stages just get the same tier size
	for ( int32_t i = 1; i < MAX_TIERS; i++ ){
		// adjust for dateLists, proportionally
		if ( m_useDateLists )
			m_stage[i] = (tierStage[i] * (16-6)) / (12-6);
		else
			m_stage[i] = tierStage[i];
	}


	// set # of lists
	m_numLists      = m_q->getNumTerms();
	// we're not done yet, we haven't even begun
	m_isDone        = false;

	// . how many docs do we need to read to get docsWanted hits?
	// . HITS = (X2 * ... * XN) / T^N   
	// . where Xi is docs read from each list
	// . T is the total # of docs in the index
	// . this assumes no dependence between the words
	// . So let's just start off reading 10,000, then 30k more then 60k
	// . So we break up our 100k truncation limit that way
	int32_t toRead = m_stage[(int)callNum]; 

	int64_t def = getStage0Default() ;
	int64_t *tf = termFreqs ;
	
	// . ...but if we're only reading 1 list...
	// . keys are 6 bytes each, first key is 12 bytes
	// . this made our result count inaccurate
	// . since we had to round up to half a PAGE_SIZE 
	//   (#defined to be 16k in RdbMap.h) we would never estimate at lower
	//   than about 4,000 docids for one-word queries
	// . so, since we're going to read at least a PAGE_SIZE anyway,
	//   removing this should not slow us down!!
	// . actually, should speed us up if all the guys site cluster which
	//   is especially probable for rare terms --- all from the same site
	// . SECONDLY, now i use Msg39::getStageNum() to do prettier clustering
	//   and that requires us to be consistent with our stages from Next
	//   10 to Next 10
	//if ( m_q->getNumTerms() <= 1 ) toRead = docsWanted * 6 + 6;
	// now loop through all non-ignored lists
	for ( int32_t i = 0 ; i < m_numLists ; i++ ) {
		// ignore lists that should be
		if ( m_ignore[i] ) { m_readSizes[i]=0; continue; }
		// don't include excluded lists in this calculation
		if ( m_q->m_qterms[i].m_termSign == '-' )
			m_readSizes[i] = m_stage[MAX_TIERS - 1] ; // STAGESUM;
		else if ( m_q->m_qterms[i].m_underNOT )
			m_readSizes[i] = m_stage[MAX_TIERS - 1] ; // STAGESUM;
		else if ( m_q->m_qterms[i].m_piped )
			m_readSizes[i] = m_stage[MAX_TIERS - 1] ; // STAGESUM;
			//m_readSizes[i] = g_indexdb.getTruncationLimit() *6+6;
		//	m_readSizes[i] = g_indexdb.getTruncationLimit()*6 ;
		// . this is set to max if we got more than 1 ignored list
		// . later we will use dynamic truncation
		/*else if (useNewTierSizing && m_q->m_termFreqs[i] > tierStage2)
			m_readSizes[i] = tierStage2;
		else if (useNewTierSizing && m_q->m_termFreqs[i] > tierStage1)
		m_readSizes[i] = tierStage1;*/
		else m_readSizes[i] = toRead;

		// . when user specifies the s0=X cgi parm and X is like 4M
		//   try to avoid allocating so much space when we do not need
		// . mark is using s0 to get exact hit counts
		int64_t max = tf[i] * m_hks+m_hks +GB_INDEXDB_PAGE_SIZE*10 ;
		if ( max < def ) max = def;
		if ( m_readSizes[i] > max ) m_readSizes[i] = max;
		// debug msg
		if ( m_isDebug || g_conf.m_logDebugQuery )
			logf ( LOG_DEBUG,"query: ReadInfo: "
			       "newreadSizes[%"INT32"]=%"INT32"",i,
			       m_readSizes[i] );

		// sanity check
		if ( m_readSizes[i] > ( 500 * 1024 * 1024 ) || 
		     m_readSizes[i] < 0 ){
			log( "minRecSize = %"INT32"", m_readSizes[i] );
			char *xx=NULL; *xx=0;
		}
	}
	// return for now
	return;
}

int32_t IndexReadInfo::getStage0Default ( ) { return STAGE0; }

// . updates m_readSizes
// . sets m_isDone to true if all lists are exhausted 
void IndexReadInfo::update ( IndexList *lists, int32_t numLists,
			     char callNum ) {
	// loop over all lists and update m_startKeys[i]
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// ignore lists that should be
		if ( m_ignore[i] ) continue;
		// . how many docIds did we read into this list?
		// . double the size since the lists are compress to half now
		//int32_t docsRead = lists[i].getListSize() / 6 ;
		// . remove the endKey put at the end by RdbList::appendLists()
		// . iff we did NOT do a merge
		//if ( ! didMerge && docsRead > 0 ) docsRead--;
		// debug
		//log("startKey for list #%"INT32" is n1=%"XINT32",n0=%"XINT64" "
		//     "(docsRead=%"INT32")",
		//     i,m_startKeys[i].n1,m_startKeys[i].n0,docsRead);
		// . if we read less than supposed to, this list is exhausted 
		//   so we set m_ignore[i] to true so we don't read again
		// . we also now update termFreq to it's exact value
		// . ok this condition doesn't apply now because when we
		//   append lists so that they are all less than a common
		//   endKey some lose some keys so the minRecSizes goes down
		// . we should just see that if the # read is 0!
		//if ( docsRead < m_docsToRead[i] ) {
		if ( lists[i].getListSize() < m_readSizes[i] ) {
			m_ignore [i] = true;
			//m_readSizes[i] = 0;
			continue;
		}
		// if we didn't meet our quota...
		//else if ( docsRead < m_docsToRead[i] ) 
		//	m_startKeys [i] = m_endKeys [i] ;
		// point to last compressed 6 byte key in list
		char *list     = (char *)lists[i].getList();
		int32_t  listSize =         lists[i].getListSize();
		// don't seg fault
		if ( listSize < m_hks ) {
			m_ignore [i] = true;
			// keep the old readsize
			//	m_readSizes[i] = 0;
			continue;
		}
		// we now do NOT call appendLists() again since
		// we're using fast superMerges
		//char *lastPart = list + listSize - 6;
		char *lastPart = list + listSize - m_hks;
		// . we update m_startKey to the endKey of each list
		// . get the startKey now
		//key_t startKey = m_startKeys[i];
		char *startKey = &m_startKeys[i*m_ks];
		// . load lastPart into lower 6 bytes of "startKey"
		// . little endian
		//gbmemcpy ( &startKey , lastPart , 6 );
		gbmemcpy ( startKey , lastPart , m_hks );
		// debug msg
		//log("pre-startKey for list #%"INT32" is n1=%"XINT32",n0=%"XINT64"",
		//     i,startKey.n1,startKey.n0);
		// sanity checks
		//if ( startKey < m_startKeys[i] ) {
		if ( KEYCMP(startKey,&m_startKeys[i*m_ks],m_ks)<0 ) {
			log("query: bad startKey. "
			    "a.n1=%016"XINT64" a.n0=%016"XINT64" < "
			    "b.n1=%016"XINT64" b.n0=%016"XINT64"" ,
			    KEY1(startKey,m_ks),
			    KEY0(startKey     ),
			    KEY1(&m_startKeys[i*m_ks],m_ks),
			    KEY0(&m_startKeys[i*m_ks]     ));
			//startKey.n1 = 0xffffffff;
			//startKey.n0 = 0xffffffffffffffffLL;
		}
		// update startKey to read the next piece now
		//m_startKeys[i] = startKey;
		KEYSET(&m_startKeys[i*m_ks],startKey,m_ks);
		// add 1 to startKey
		//m_startKeys[i] += (uint32_t) 1;
		KEYADD(&m_startKeys[i*m_ks],1,m_ks);
		// debug msg
		//log("NOW startKey for list #%"INT32" is n1=%"XINT32",n0=%"XINT64"",
		//     i,m_startKeys[i].n1,m_startKeys[i].n0);
		// . increase termFreqs if we read more than was estimated
		// . no! just changes # of total results when clicking Next 10
		//if ( docsRead > m_q->m_termFreqs[i] ) 
		//	m_q->m_termFreqs[i] = docsRead;
	}
	// break if a list can read more, if it can read more, that is
	int32_t i;
	for ( i = 0 ; i < numLists ; i++ ) if ( ! m_ignore[i] ) break;
	// if all lists are exhausted, set m_isDone
	if ( i >= numLists ) { m_isDone = true; return; }
	// . based on # of results we got how much more should we have to read
	//   to get what we want, "docsWanted"
	// . just base it on linear proportion
	// . keep in mind, if we double the amount to read we will quadruple
	//   the results if reading 2 indexLists, x8 if reading from 3.
	// . that doesn't take into account phrases though...
	// . let's just do it this way
	// loop over all lists and update m_startKeys[i] and m_totalDocsRead
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// ignore lists that should be
		if ( m_ignore[i] ) continue;
		// update each list's docs to read
		m_readSizes[i] = m_stage[(int)callNum];
		/*		if      ( m_readSizes[i] < m_stage[0]) 
			m_readSizes[i] = m_stage0;
		else if ( m_readSizes[i] < m_stage[1]) 
			m_readSizes[i] = m_stage1;
		else                                 
		m_readSizes[i] = m_stage2;*/
		// debug msg
		log("newreadSizes[%"INT32"]=%"INT32"",i,m_readSizes[i]);
	}
}


// . updates m_readSizes
// . sets m_isDone to true if all lists are exhausted 
// . used by virtual split in msg3b to check if we're done or not.
void IndexReadInfo::update ( int64_t *termFreqs,
			     int32_t numLists,
			     char callNum ) {
	// loop over all lists and update m_startKeys[i]
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// ignore lists that should be
		if ( m_ignore[i] ) continue;
		// . how many bytes did we read ? Since these are
		// . half keys, multiply termFreqs by 6 and add 6 for the
		// . first key which is full 12 bytes
		int64_t listSize = termFreqs[i] * 6 + 6;

		if ( listSize < m_readSizes[i] ) {
			m_ignore [i] = true;
			//m_readSizes[i] = 0;
			continue;
		}
		// if we didn't meet our quota...
		//else if ( docsRead < m_docsToRead[i] ) 
		//	m_startKeys [i] = m_endKeys [i] ;
		// point to last compressed 6 byte key in list
		//char *list     = (char *)lists[i].getList();

		// don't seg fault
		if ( listSize < m_hks ) {
			m_ignore [i] = true;
			//m_readSizes[i] = 0;
			continue;
		}
	}
	// break if a list can read more, if it can read more, that is
	int32_t i;
	for ( i = 0 ; i < numLists ; i++ ) if ( ! m_ignore[i] ) break;
	// if all lists are exhausted, set m_isDone
	if ( i >= numLists ) { m_isDone = true; return; }
	// . based on # of results we got how much more should we have to read
	//   to get what we want, "docsWanted"
	// . just base it on linear proportion
	// . keep in mind, if we double the amount to read we will quadruple
	//   the results if reading 2 indexLists, x8 if reading from 3.
	// . that doesn't take into account phrases though...
	// . let's just do it this way
	// loop over all lists and update m_startKeys[i] and m_totalDocsRead
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// debug msg
		//log("oldreadSizes[%"INT32"]=%"INT32"",i,m_readSizes[i]);
		// update each list's docs to read if we're not on the last 
		// tier
		if ( !m_ignore[i] && callNum < MAX_TIERS && 
		     m_readSizes[i] < m_stage[(int)callNum] )
			m_readSizes[i] = m_stage[(int)callNum];
		/*if      ( m_readSizes[i] < m_stage0) 
			m_readSizes[i] = m_stage0;
		else if ( m_readSizes[i] < m_stage1) 
			m_readSizes[i] = m_stage1;
		else
		m_readSizes[i] = m_stage2;*/
		// debug msg
		if ( m_isDebug || g_conf.m_logDebugQuery )
			logf ( LOG_DEBUG,"query: ReadInfo: "
			       "newreadSizes[%"INT32"]=%"INT32"",i,m_readSizes[i] );
	}
}
