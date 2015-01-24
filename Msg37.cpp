#include "gb-include.h"

#include "Msg37.h"

static void gotTermFreqWrapper ( void *state ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "termIds/termFreqs" should NOT be on the stack in case we block
// . i based this on ../titled/Msg25.cpp since it sends out multiple msgs at 
//   the same time, too
bool Msg37::getTermFreqs ( collnum_t collnum,//char       *coll       ,
			   int32_t        maxAge     ,
			   int64_t  *termIds    ,
			   int32_t        numTerms   ,
			   int64_t  *termFreqs  ,
			   void       *state      ,
			   void (* callback)(void *state ) ,
			   int32_t        niceness   ,
			   bool        exactCount ) {

	// warning
	if ( collnum < 0 ) log(LOG_LOGIC,"net: bad collection. msg37.");
	// we haven't got any responses as of yet or sent any requests
	m_callback    = callback;
	m_state       = state;
	m_exactCount  = exactCount;
	m_niceness    = niceness;
	m_numReplies  = 0;
	m_numRequests = 0;
	m_errno       = 0;
	m_numTerms    = numTerms;
	m_termFreqs   = termFreqs;
	m_collnum     = collnum;
	//m_coll        = coll;
	m_maxAge      = maxAge;
	m_termIds     = termIds;
	// set all to 1 in case there's an error
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		//if ( ignore[i] ) m_termFreqs[i] = 0LL;
		//else             m_termFreqs[i] = 1LL;
		m_termFreqs[i] = 1LL;
	}
	// reset
	m_i = 0;
	memset ( m_inUse , 0 , MAX_MSG36_OUT );
	// launch the requests
	if ( ! launchRequests() ) return false;
	// set our array
	gotTermFreq ( NULL );
	// we did not block, return true
	return true;
}

bool Msg37::launchRequests ( ) {
	// see if anyone blocks at all
	//bool noBlock = true;
	// . send out all msg23 requests at once!
	// . make slots for all
	for (  ; m_i < m_numTerms ; m_i++ ) {
		// break if too many are out
		if ( m_numRequests - m_numReplies >= MAX_MSG36_OUT ) 
			return false;
		// get query term
		//QueryTerm *qt = m_q->m_qterms[m_i];
		// skip if query term is the sum of UOR'ed query terms
		//if ( qt->m_componentCode == -1 ) {
		//	// reset to 0
		//	m_termFreqs[m_i] = 0LL;
		//	continue;
		//}
		// get available slot
		int32_t j ;
		for ( j = 0 ; j < MAX_MSG36_OUT ; j++ ) 
			if ( ! m_inUse[j] ) break;
		if ( j >= MAX_MSG36_OUT ) {
			log("query: msg37 failed sanity check 1.");
			char *xx = NULL; *xx = 0;
		}
		// skip if ignored
		//if ( ignore[i] ) continue;
		// store some info there
		m_msg36[j].m_this = this;
		m_msg36[j].m_j    = j;
		m_msg36[j].m_i    = m_i;
		// . start up a Msg36 to get it
		// . this will return false if blocks
		if ( ! m_msg36[j].getTermFreq ( m_collnum ,
						m_maxAge ,
						m_termIds[m_i] ,
						&m_msg36[j],
						gotTermFreqWrapper ,
						m_niceness ,
						m_exactCount ) ) {
			//noBlock = false;
			m_numRequests++;
			m_inUse[j] = 1;
			continue;
		}
		// . we didn't block
		// . count these even if g_errno was set
		m_numReplies++; 
		m_numRequests++;
		// if no error set out term freq
		if ( ! g_errno ) m_termFreqs[m_i] = m_msg36[j].m_termFreq;
		// continue if no error
		if ( ! g_errno ) continue;
		// remember the error
		m_errno = g_errno;
		// clear it
		g_errno = 0;
		// hardcore bitch
		//log("Msg37::getTermFreqs: msg36: %s", mstrerror(g_errno));
	}
	// did anyone block? if so, return false for now
	//if ( ! noBlock ) return false;
	// . otherwise, we got everyone, so go right to the merge routine
	// . returns false if not all replies have been received 
	// . returns true if done
	// . sets g_errno on error
	//return gotTermFreq ( NULL );
	//return noBlock;
	if ( m_numReplies < m_numRequests ) return false;
	// we got all the replies
	return true;
}

void gotTermFreqWrapper ( void *state ) {
	Msg36 *msg36 = (Msg36 *) state;
	Msg37 *THIS  = (Msg37 *) msg36->m_this;
	// it returns false if we're still awaiting replies
	if ( ! THIS->gotTermFreq ( msg36 ) ) return;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
}

// . returns false if not all replies have been received (or timed/erroredout)
// . returns true if done (or an error finished us)
// . sets g_errno on error
bool Msg37::gotTermFreq ( Msg36 *msg36 ) {
	int32_t i ;
	int32_t j;
	// if called from above skip down to bottom
	if ( ! msg36 ) goto skip;
	// . set our m_errno if there was an error so everyone else knows
	// . don't overwrite it if it's already set
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	// . now m_linkInfo[i] (for some i, i dunno which) is filled
	m_numReplies++;
	// extract info we stored in there
	i = msg36->m_i ;
	j = msg36->m_j ;
	// sanity check
	if ( &m_msg36[j] != msg36 ) {
		log("query: msg37 failed sanity check 3.");
		char *xx = NULL; *xx = 0;
	}
	// if no error set out term freq
	if ( ! g_errno ) m_termFreqs[i] = msg36->m_termFreq;
	// sanity check
	if ( ! m_inUse[j] ) {
		log("query: msg37 failed sanity check 2.");
		char *xx = NULL; *xx = 0;
	}
	// mark as available
	m_inUse[j] = 0;
	// try to launch more, returns true if all done though
	if ( ! launchRequests() ) return false;
	// wait until we got all the replies before we attempt to merge
	//if ( m_numReplies < m_numRequests ) return false;
 skip:
	// . did we have an error from any reply?
	// . return true if we got all replies
	// . do not merge since someone had an error
	if ( m_errno ) { g_errno = m_errno ; return true; }
	// set all to 1 in case there's an error
	//for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
	//	// skip if ignored
	//	//if ( m_termFreqs[i] == 0LL ) continue;
	//	m_termFreqs[i] = m_msg36[i].getTermFreq();
	//}
	// . return true cuz we're done
	// . g_errno may be set though
	return true;
}
