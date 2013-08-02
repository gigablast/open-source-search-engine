#include "gb-include.h"

#include "Query.h"
//#include "Indexdb.h" // g_indexdb.getTruncationLimit() g_indexdb.getTermId()
#include "Words.h"
#include "Bits.h"
#include "Phrases.h"
#include "Url.h"
#include "Clusterdb.h" // g_clusterdb.getNumGlobalRecs()
#include "StopWords.h" // isQueryStopWord()
#include "Speller.h"
//#include "Thesaurus.h"
#include "Mem.h"
#include "Msg3a.h"
#include "HashTableX.h"
#include "Synonyms.h"
#include "Wiki.h"

Query::Query ( ) {
	constructor();
}

void Query::constructor ( ) {
	//m_bmap      = NULL;
	m_bitScores = NULL;
	m_qwords      = NULL;
	m_expressions = NULL;
	m_qwordsAllocSize      = 0;
	m_expressionsAllocSize = 0;
	m_qwords               = NULL;
	m_expressions          = NULL;
	reset ( );
}

void Query::destructor ( ) {
	reset();
}

Query::~Query ( ) {
	reset ( );
}

void Query::reset ( ) {
	m_docIdRestriction = 0LL;
	m_groupThatHasDocId = NULL;
	m_bufLen      = 0;
	m_origLen     = 0;
	m_numWords    = 0;
	m_numOperands = 0;
	m_numTerms    = 0;
	m_synTerm     = 0;
	//m_numIgnored  = 0;
	//m_numRequired = -1;
	m_numComponents = 0;
	//if ( m_bmap && m_bmapSize ) // != m_bmbuf )
	//	mfree ( m_bmap , m_bmapSize , "Query1" );
	//if ( m_bitScores && m_bitScoresSize ) //  != m_bsbuf )
	//	mfree ( m_bitScores , m_bitScoresSize , "Query2" );
	//m_bmap = NULL;
	m_bitScores = NULL;
	//m_bmapSize      = 0;
	m_bitScoresSize = 0;
	if ( m_expressionsAllocSize )
		mfree ( m_expressions , m_expressionsAllocSize , "Query3" );
	if ( m_qwordsAllocSize )
		mfree ( m_qwords      , m_qwordsAllocSize      , "Query4" );
	m_expressionsAllocSize = 0;
	m_qwordsAllocSize      = 0;
	m_qwords               = NULL;
	m_expressions          = NULL;
	m_numExpressions       = 0;
	m_gnext                = m_gbuf;
	m_hasUOR               = false;
	m_bmapIsSet            = false;
	// the site: and ip: query terms will disable site clustering & caching
	m_hasPositiveSiteField         = false;
	m_hasIpField           = false;
	m_hasUrlField          = false;
	m_hasSubUrlField       = false;
	m_hasIlinkField        = false;
	m_hasGBLangField       = false;
	m_hasGBCountryField    = false;
	m_hasQuotaField        = false;
	m_hasLinksOperator     = false;
	m_truncated            = false;
	m_hasSynonyms          = false;
}

// . returns false and sets g_errno on error
// . "query" must be NULL terminated
// . if boolFlag is 0 we ignore all boolean operators
// . if boolFlag is 1  we assume query is boolen
// . if boolFlag is 2  we attempt to detect if query is boolean or not
// . if "keepAllSingles" is true we do not ignore any single word UNLESS
//   it is a boolean operator (IGNORE_BOOLOP), fieldname (IGNORE_FIELDNAME)
//   a punct word (IGNORE_DEFAULT) or part of one field value (IGNORE_DEFAULT)
//   This is used for term highlighting (Highlight.cpp and Summary.cpp)
bool Query::set2 ( char *query        , 
		   //long  queryLen     ,
		   //char *coll         , 
		   //long  collLen      ,
		   //char  boolFlag     ,
		   //bool  keepAllSingles ,
		   // need language for doing synonyms
		   uint8_t  langId ,
		   char     queryExpansion ,
		   bool     useQueryStopWords ) {
		  //long  maxQueryTerms  ) {

	m_langId = langId;
	m_useQueryStopWords = useQueryStopWords;
	// fix summary rerank and highlighting.
	bool keepAllSingles = true;

	// assume not boolean
	char boolFlag = 0;

	// come back up here if we changed our boolean minds
 top:

	reset();

	if ( ! query ) return true;

	// set to 256 for synonyms?
	m_maxQueryTerms = 256;
	m_queryExpansion = queryExpansion;

	long queryLen = gbstrlen(query);
	// override this to 32 at least for now
	//if ( m_maxQueryTerms < 32 ) m_maxQueryTerms = 32;
	// save collection info
	//m_coll    = coll;
	//m_collLen = collLen;
	// truncate query if too big
	if ( queryLen >= MAX_QUERY_LEN ) {
		log("query: Query length of %li must be less than %li. "
		    "Truncating.",queryLen,(long)MAX_QUERY_LEN);
		queryLen = MAX_QUERY_LEN - 1;
		m_truncated = true;
	}
	// save original query
	
	m_origLen = queryLen;
	memcpy ( m_orig , query , queryLen );
	m_orig [ m_origLen ] = '\0';

	log(LOG_DEBUG, "query: set called = %s", m_orig);

	char *q = query;
	// see if it should be boolean...
	for ( long i = 0 ; boolFlag && i < queryLen ; i++ ) {
		if ( q[i]=='A' && q[i+1]=='N' && q[i+2]=='D' &&
		     (q[i+3]==' ' || q[i+3]=='(') )
			boolFlag = 1;
		if ( q[i]=='O' && q[i+1]=='R' && 
		     (q[i+2]==' ' || q[i+2]=='(') )
			boolFlag = 1;
		if ( q[i]=='N' && q[i+1]=='O' && q[i+2]=='T' &&
		     (q[i+3]==' ' || q[i+3]=='(') )
			boolFlag = 1;		
	}
	
	// come back up here if we find no bool operators but had ()'s
	// top:
	// reset anything that was allocated... in case we're being 
	// called from below... m_qwords may have been allocated in call
	// to setQWords() below
	// NO! this resets m_origLen to 0!!! not to mention other member vars
	// that were set somewhere above!!! i moved top: label above!
	//reset();

	// convenience ptr
	char *p    = m_buf;
	char *pend = m_buf + MAX_QUERY_LEN;
	// . copy query into m_buf
	// . translate ( and ) to special query operators so Words class
	//   can parse them as their own word to make parsing bool queries ez
	//   for parsing out the boolean operators in setBitScoresBoolean()
	for ( long i = 0 ; i < queryLen ; i++ ) {
		// dst buf must be big enough
		if ( p + 8 >= pend ) {
			g_errno = EBUFTOOSMALL;
			return log(LOG_LOGIC,"query: query: query too big.");
		}
		// translate ( and )
		if ( boolFlag != 0 && query[i] == '(' ) {
			memcpy ( p , " LeFtP " , 7 ); p += 7;
			continue;
		}
		if ( boolFlag != 0 && query[i] == ')' ) {
			memcpy ( p , " RiGhP " , 7 ); p += 7;
			continue;
		}
		if ( query[i] == '|' ) {
			memcpy ( p , " PiiPE " , 7 ); p += 7;
			continue;
		}
		// translate [#a] [#r] [#ap] [#rp] [] [p] to operators
		if ( query[i] == '[' && is_digit(query[i+1])) {
			long j = i+2;
			long val = atol ( &query[i+1] );
			while ( is_digit(query[j]) ) j++;
			char c = query[j];
			if ( (c == 'a' || c == 'r') && query[j+1]==']' ) {
				sprintf ( p , " LeFtB %li %c RiGhB ",val,c);
				p += gbstrlen(p);
				i = j + 1;
				continue;
			}
			else if ( (c == 'a' || c == 'r') && 
				  query[j+1]=='p' && query[j+2]==']') {
				sprintf ( p , " LeFtB %li %cp RiGhB ",val,c);
				p += gbstrlen(p);
				i = j + 2;
				continue;
			}
		}
		if ( query[i] == '[' && query[i+1] == ']' ) {
			sprintf ( p , " LeFtB RiGhB ");
			p += gbstrlen(p);
			i = i + 1;
			continue;
		}
		if ( query[i] == '[' && query[i+1] == 'p' && query[i+2]==']') {
			sprintf ( p , " LeFtB RiGhB ");
			p += gbstrlen(p);
			i = i + 2;
			continue;
		}
		char *q = &(query[i]);
		// Skip old buzz permalink keywords
		if (*q == 'g' && *(q+1) == 'b'){
			// do not skip anymore, Msg5e.cpp needs this
			/*
			if (*(q+2) == 'p' && *(q+3) == 'e' && *(q+4) == 'r' 
			    && *(q+5) == 'm' && *(q+6) == 'a' && *(q+7) == 'l' 
			    && *(q+8) == 'i' && *(q+9) == 'n' && *(q+10) == 'k' 
			    && *(q+11) == ':' && *(q+12) =='1'){
				//i += 12;
				static bool s_printed = false;
				if ( ! s_printed )
					logf(LOG_DEBUG,"query: skipping "
					     "gbpermalink term for buzz.");
				if ( ! s_printed ) s_printed = true;
				continue;
			}
			*/
			if (*(q+2)=='k' && *(q+3)=='e' && *(q+4) == 'y'
				 && *(q+5)=='w' && *(q+6)=='o' && *(q+7) == 'r'
				 && *(q+8) == 'd' && *(q+9) == ':' 
				 && *(q+10)=='r' && *(q+11)=='3' && *(q+12)=='6' 
				 && *(q+13) == 'p' && *(q+14) == '1'){
				//logf(LOG_DEBUG,"query: skipping funky "
				//     "keyword term for buzz.");
				i += 14;
				continue;
			}
		}
 
		// TODO: copy altavista's operators here? & | !
		// otherwise, just a plain copy
		*p = query [i];
		p++;
	}
	// NULL terminate
	*p = '\0';
	// debug statement
	//log(LOG_DEBUG,"Query: Got new query=%s",tempBuf);
	//printf("query: query: Got new query=%s\n",tempBuf);

	// set length
	m_bufLen = p - m_buf;

	Words words;
	Phrases phrases;

	// set m_qwords[] array from m_buf
	if ( ! setQWords ( boolFlag , keepAllSingles , words , phrases ) ) 
		return false;
	//log(LOG_DEBUG, "Query: QWords set");
	// did we have any boolean operators
	char found  = 0;
	char parens = 0;
	if ( boolFlag >= 1 ) {
		for ( long i = 0 ; i < m_numWords ; i++ ) {
			char *w    = m_qwords[i].m_word;
			long  wlen = m_qwords[i].m_wordLen;
			if      (wlen==2 &&w[0]=='O'&&w[1]=='R'           )
				found=1;
			else if (wlen==3 &&w[0]=='A'&&w[1]=='N'&&w[2]=='D')
				found=1;
			else if (wlen==3 &&w[0]=='N'&&w[1]=='O'&&w[2]=='T')
				found=1;
			if      (wlen==5 &&w[0]=='L' && w[1]=='e' && 
				 w[2]=='F' && w[3]=='t' && w[4]=='P' ) 
				parens=1;
			else if (wlen==5 &&w[0]=='R' && w[1]=='i' && 
				 w[2]=='G' && w[3]=='h' && w[4]=='P' ) 
				parens=1;
		}
		// if we were told it was a bool query or to auto-detect
		// and it has no operators, but had parens, re-do so parens
		// do not get translated to LeFtP or RiGhP
		if ( boolFlag >= 1 && found == 0 && parens == 1 ) {
			boolFlag = 0; goto top; }
		// if no bool operators, it's definitely not a boolean query
		if ( found == 0 ) boolFlag = 0;
	}

	// set m_qterms from m_qwords, always succeeds
	setQTerms ( words , phrases );

	// . now add in compound termlists
	// . compound query terms replace lists of UOR'd query terms that
	//   share the same QueryTerm::m_exclusiveBit (ebit)
	// . if it cannot get the compound termlist from a remote cache, then
	//   Msg2 should get its components
	// . component termlists have their compound termlist number
	//   as their m_componentCode, compound termlists have a componentCode 
	//   of -1, other termlists have a componentCode of -2.
	// . Query::addCompoundTerms() will add one extra query term for every 
	//   sequence of UOR'd query terms that share the same ebit. 
	//   Furthermore, it sets the m_componentCodes[] array.
	// . The compound term must have the same ebit as its component terms.
	// . we use the termid of compound termlists (and NOT their components)
	//   when routing this query to the host that can use the least
	//   amount of bandwidth to download/get the termlists. if the compound
	//   termlist is not in the cache then it will not be on disk or
	//   in the tree since it is a vitual termlist, BUT we will still
	//   create it and store it in the cache, so assume it is in a cache,
	//   because the act of storing it in the cache may require sending
	//   it to another machine.
	// . if m_compoundListMaxSize is 0, do not do compound lists
	// . Query::addCompoundTerms() will set the termfreq of compound terms 
	//   to the sum of the termfreqs of its component termlists
	//if ( m_compoundListMaxSize > 0 ) addCompoundTerms( );
	// . always add them for now
	//addCompoundTerms( );

	// if m_isBoolean was set and we only have OP_UOR then
	// we should probably unset it here (mdw)

	// set m_expressions[] and m_operands[] arrays and m_numOperands 
	// for boolean queries
	if ( m_isBoolean )
		if ( ! setBooleanOperands() ) return false;

	// disable stuff for site:, ip: and url: queries
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord  ) continue;
		if      ( qw->m_fieldCode == FIELD_SITE &&
			  qw->m_wordSign != '-' ) 
			m_hasPositiveSiteField = true;
		else if ( qw->m_fieldCode == FIELD_IP ) 
			m_hasIpField   = true;
		else if ( qw->m_fieldCode == FIELD_URL )
			m_hasUrlField  = true;
		else if ( qw->m_fieldCode == FIELD_ILINK )
			m_hasIlinkField  = true;
		else if ( qw->m_fieldCode == FIELD_GBLANG )
			m_hasGBLangField = true;
		else if ( qw->m_fieldCode == FIELD_GBCOUNTRY )
			m_hasGBCountryField = true;
		else if ( qw->m_fieldCode == FIELD_QUOTA )
			m_hasQuotaField = true;
		else if ( qw->m_fieldCode == FIELD_SUBURL )
			m_hasSubUrlField = true;
	}

	// set m_docIdRestriction if a term is gbdocid:
	for ( long i = 0 ; i < m_numTerms && ! m_isBoolean ; i++ ) {
		// get it
		QueryTerm *qt = &m_qterms[i];
		// gbdocid:?
		if ( qt->m_fieldCode != FIELD_GBDOCID ) continue;
		// get docid
		char *ds = m_qterms[i].m_term + 8;
		m_docIdRestriction = atoll(ds);
		unsigned long gid;
		gid = g_hostdb.getGroupIdFromDocId(m_docIdRestriction);
		m_groupThatHasDocId = g_hostdb.getGroup(gid);
		break;
	}

	// . if it is not truncated, no need to use hard counts
	// . comment this line and the next one out for testing hard counts
	if ( ! m_truncated ) return true;
	// if got truncated AND under the HARD max, nothing we can do, it
	// got cut off due to m_maxQueryTerms limit in Parms.cpp
	if ( m_numTerms < (long)MAX_EXPLICIT_BITS ) return true;
	// if they just hit the admin's ceiling, there's nothing we can do
	if ( m_numTerms >= m_maxQueryTerms ) return true;
	// a temp log message
	log(LOG_DEBUG,"query: Encountered %li query terms.",m_numTerms);

	// otherwise, we're below m_maxQueryTerms BUT above MAX_QUERY_TERMS
	// so we can use hard counts to get more power...

	// . use the hard count for excessive query terms to save explicit bits
	// . just look for operands on the first level that are not OR'ed
	char redo = 0;
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		// get the ith word
		QueryWord *qw = &m_qwords[i];
		// mark him as NOT hard required
		qw->m_hardCount = 0;
		// skip if not on first level
		if ( qw->m_level != 0 ) continue;
		// stop at first OR on this level
		if ( qw->m_opcode == OP_OR ) break;
		// skip all punct
		if (  qw->m_isPunct ) continue;
		// if we are a boolean query,the next operator can NOT be OP_OR
		// because we can not used terms that are involved in an OR
		// as a hard count term, because they are not required terms
		for ( long j=i+1 ; m_isBoolean && j<m_numWords; j++ ) {
			// stop at previous operator
			char opcode = m_qwords[j].m_opcode;
			if ( ! opcode          ) continue;
			if (   opcode != OP_OR ) break;
			// otherwise, the next operator is an OR, so do not
			// use a hard count for this term
			goto stop;
		}
		// mark him as required, so he won't use an explicit bit now
		qw->m_hardCount = 1;
		// mark it so we can reduce our number of explicit bits used
		redo = 1;
	}

 stop:
	// if nothing changed, return now
	if ( ! redo ) return true;

	// . set the query terms again if we have a long query
	// . if QueryWords has m_hardCount set, ensure the explicit bit is 0
	// . non-quoted phrases that contain a "required" single word should
	//   themselves have 0 for their implicit bits, BUT 0x8000 for their
	//   explicit bit
	if ( ! setQTerms ( words , phrases ) )
		return false;


	// a temp log message
	//log(LOG_DEBUG,"query: Compressed to %li query terms, %li hard. "
	//    "(nt=%li)",
	//     m_numExplicitBits,m_numTerms-m_numExplicitBits,m_numTerms);

	if ( ! m_isBoolean ) return true;

	// free cuz it was already set
	if ( m_expressionsAllocSize ) 
		mfree(m_expressions,m_expressionsAllocSize , "Query" );
	m_expressionsAllocSize = 0;
	m_expressions = NULL;

	// also set the boolean stuff again too!
	if ( ! setBooleanOperands() ) return false;

	return true;
}

/*
// count how many so PageResults will know if he should offer
// a default OR alternative search if no more results for
// the default AND (rat=1)
long Query::getNumRequired ( ) {
	if ( m_numRequired >= 0 ) return m_numRequired;
	m_numRequired = 0;
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// don't require if negative
		if ( qt->m_termSign == '-' ) continue;
		// skip signless phrases
		if ( qt->m_isPhrase && qt->m_termSign == '\0' ) continue;
		if ( qt->m_synonymOf ) continue;
		// count it up
		m_numRequired++;
	}
	return m_numRequired;
}
*/

// returns false and sets g_errno on error
bool Query::setQTerms ( Words &words , Phrases &phrases ) {

	//long shift = 0;
	// . set m_qptrs/m_qtermIds/m_qbits 
	// . use one bit position for each phraseId and wordId
	// . first set phrases
	long n = 0;
	// what is the max value for "shift"?
	long max = (long)MAX_EXPLICIT_BITS;
	if ( max > m_maxQueryTerms ) max = m_maxQueryTerms;
	//char u8Buf[256]; 

	for ( long i = 0 ; i < m_numWords && n < MAX_QUERY_TERMS ; i++ ) {
		// break out if no more explicit bits!
		/*
		if ( shift >= max ) {
			log("query: Query1 has more than %li unique terms. "
			    "Truncating.",max);
			m_truncated = true;
			break; 
		}
		*/
		QueryWord *qw  = &m_qwords[i];
		// skip if ignored... mdw...
		if ( ! qw->m_phraseId ) continue;
		if (   qw->m_ignorePhrase ) continue; // could be a repeat
		// none if weight is absolute zero
		if ( qw->m_userWeightPhrase == 0   && 
		     qw->m_userTypePhrase   == 'a'  ) continue;

		// stop breach
		if ( n >= MAX_QUERY_TERMS ) {
			log("query: lost query phrase terms to max term "
			    "limit of %li",(long)MAX_QUERY_TERMS );
			break;
		}

		QueryTerm *qt = &m_qterms[n];
		qt->m_qword     = qw ;
		qt->m_piped     = qw->m_piped;
		qt->m_isPhrase  = true ;
		qt->m_isUORed   = false;
		qt->m_UORedTerm   = NULL;
		qt->m_synonymOf = NULL;
		qt->m_ignored   = 0;
		// assume not a repeat of another query term (set below)
		qt->m_repeat    = false;
		// stop word? no, we're a phrase term
		qt->m_isQueryStopWord = false;
		// change in both places
		qt->m_termId    = qw->m_phraseId & TERMID_MASK;
		m_termIds[n]    = qw->m_phraseId & TERMID_MASK;
		//log(LOG_DEBUG, "Setting query phrase term id %d: %lld", n, m_termIds[n]);
		qt->m_rawTermId = qw->m_rawPhraseId;
		// assume explicit bit is 0
		qt->m_explicitBit = 0;
		qt->m_matchesExplicitBits = 0;
		// boolean queries are not allowed term signs for phrases
		// UNLESS it is a '*' soft require sign which we need for
		// phrases like: "cat dog" AND pig
		if ( m_isBoolean && qw->m_phraseSign != '*' ) {
			qt->m_termSign = '\0';
			m_termSigns[n] = '\0';
		}
		// if not boolean, ensure to change signs in both places
		else {
			qt->m_termSign  = qw->m_phraseSign;
			m_termSigns[n]  = qw->m_phraseSign;
		}
		//
		// INSERT UOR LOGIC HERE
		//
// 		long pw = i-1;
// 		// . back up until word that contains quote if in a quoted 
// 		//   phrase
// 		// . UOR can only support two word phrases really...
// 		if (m_qwords[i].m_quoteStart >= 0)
// 			pw = m_qwords[i].m_quoteStart - 1;
// 		if ( pw >= 0 && m_qwords[pw].m_quoteStart >= 0 ) 
// 			pw = m_qwords[pw].m_quoteStart - 1;

// 		// back two more if field
// 		//if ( pw >= 0 && m_qwords[pw].m_ignoreWord==IGNORE_FIELDNAME )
// 		//	pw -= 2;
// 		while (pw>0 && 
// 		       ((m_qwords[pw].m_ignoreWord == IGNORE_DEFAULT) ||
// 			(m_qwords[pw].m_ignoreWord == IGNORE_FIELDNAME))) pw--;

// 		// is UOR operator? if so, backup over it
// 		if ( pw >= 0 && m_qwords[pw].m_opcode == OP_UOR ) pw -= 2;
// 		else          goto notUORPhrase;
// 		if ( pw < 0 ) goto notUORPhrase;
// 		// . if previous term is UOR'd with us then share the same ebit
// 		// . this allows us to use lots of UOR'd query terms
// 		// . the UOR'd lists may also be merged together into a single
// 		//   list if "mergeListMaxSize" is positive
// // 		if ( n >= 1 && 
// // 		     i >= 4 &&
// // 		     //m_qterms[n-1].m_qword  == &m_qwords[pw] &&
// // 		     shift > 0 &&
// // 		     qw->m_hardCount == 0 ) 
// // 			shift--;
// 		// set the UOR term sign
// 		qt->m_isUORed = true;
// 	notUORPhrase:



		// do not use an explicit bit up if we have a hard count
		qt->m_hardCount = qw->m_hardCount;
// 		if ( qw->m_hardCount == 0 ) {
// 			qt->m_explicitBit = 1 << shift ;
// 			shift++;
// 		}
		qw->m_queryWordTerm = NULL;
		// IndexTable.cpp uses this one
		qt->m_inQuotes  = qw->m_inQuotes;
		// point to the string itself that is the phrase
		qt->m_term      = qw->m_word;
		qt->m_termLen   = qw->m_phraseLen;

		// the QueryWord should have a direct link to the QueryTerm,
		// at least for phrase, so we can OR in the bits of its
		// constituents in the for loop below
		qw->m_queryPhraseTerm = qt ;
		// include ourselves in the implicit bits
// 		qt->m_implicitBits = qt->m_explicitBit;
		// doh! gotta reset to 0
		qt->m_implicitBits = 0;
		// assume not under a NOT bool op
		qt->m_underNOT = false;
		// assign score weight, we're a phrase here
		qt->m_userWeight = qw->m_userWeightPhrase ;
		qt->m_userType   = qw->m_userTypePhrase   ;
		qt->m_fieldCode  = qw->m_fieldCode  ;
		// stuff before a pipe always has a weight of 1
		if ( qt->m_piped ) {
			qt->m_userWeight = 1;
			qt->m_userType   = 'a';
		}
		// debug
		//char tmp[1024];
		//memcpy ( tmp , qt->m_term , qt->m_termLen );
		//tmp [ qt->m_termLen ] = 0;
		//logf(LOG_DEBUG,"got term %s (%li)",tmp,qt->m_termLen);
		// otherwise, add it
		n++;
	}

	// now if we have enough room, do the singles
	for ( long i = 0 ; i < m_numWords && n < MAX_QUERY_TERMS ; i++ ) {
		// break out if no more explicit bits!
		/*
		if ( shift >= max ) {
			logf(LOG_DEBUG,
			     "query: Query2 has more than %li unique terms. "
			    "Truncating.",max);
			m_truncated = true;
			break; 
		}
		*/
		QueryWord *qw  = &m_qwords[i];

 		if ( qw->m_ignoreWord && 
 		     qw->m_ignoreWord != IGNORE_QSTOP) continue;
// 		if ( qw->m_ignoreWord ) continue;

		// ignore if in quotes
		if ( qw->m_quoteStart >= 0 ) continue;

		// if nore if weight is absolute zero
		if ( qw->m_userWeight == 0   && 
		     qw->m_userType   == 'a'  ) continue;

		// stop breach
		if ( n >= MAX_QUERY_TERMS ) {
			log("query: lost query terms to max term "
			    "limit of %li",(long)MAX_QUERY_TERMS );
			break;
		}

		QueryTerm *qt = &m_qterms[n];
		qt->m_qword     = qw ;
		qt->m_piped     = qw->m_piped;
		qt->m_isPhrase  = false ;
		qt->m_isUORed   = false;
		qt->m_UORedTerm   = NULL;
		qt->m_synonymOf = NULL;
		// ignore some synonym terms if tf is too low
		qt->m_ignored = qw->m_ignoreWord;
		//		qt->m_ignored = 0;
		// assume not a repeat of another query term (set below)
		qt->m_repeat    = false;
		// stop word? no, we're a phrase term
		qt->m_isQueryStopWord = qw->m_isQueryStopWord;
		// change in both places
		qt->m_termId    = qw->m_wordId & TERMID_MASK;
		m_termIds[n]    = qw->m_wordId & TERMID_MASK;
		qt->m_rawTermId = qw->m_rawWordId;
		// assume explicit bit is 0
		qt->m_explicitBit = 0;
		qt->m_matchesExplicitBits = 0;
		//log(LOG_DEBUG, "Setting query phrase term id %d: %lld raw: %lld", n, m_termIds[n], qt->m_rawTermId);
		// boolean queries are not allowed term signs
		if ( m_isBoolean ) {
			qt->m_termSign = '\0';
			m_termSigns[n] = '\0';
			// boolean fix for "health OR +sports" because
			// the + there means exact word match, no synonyms.
			if ( qw->m_wordSign == '+' ) {
				qt->m_termSign  = qw->m_wordSign;
				m_termSigns[n]  = qw->m_wordSign;
			}
		}
		// if not boolean, ensure to change signs in both places
		else {
			qt->m_termSign  = qw->m_wordSign;
			m_termSigns[n]  = qw->m_wordSign;
		}
		// get previous text word
		//long pw = i - 2;
 		long pw = i-1;
// 		// . back up until word that contains quote if in a quoted 
// 		//   phrase
// 		// . UOR can only support two word phrases really...
 		if (m_qwords[i].m_quoteStart >= 0)
			pw = m_qwords[i].m_quoteStart ;
		if ( pw > 0 ) pw--;

 		// back two more if field
		long fieldStart=-1;
		long fieldLen=0;

		if ( pw == 0 && m_qwords[pw].m_ignoreWord==IGNORE_FIELDNAME)
			fieldStart = pw;

  		if ( pw > 0&& m_qwords[pw-1].m_ignoreWord==IGNORE_FIELDNAME ){
  			pw -= 1;
 			fieldStart = pw;
 		}
 		while (pw>0 && 
 		       ((m_qwords[pw].m_ignoreWord == IGNORE_FIELDNAME))) {
			pw--;
			fieldStart = pw;
		}

		if (fieldStart > -1) {
			pw = i;
			while (pw < m_numWords && m_qwords[pw].m_fieldCode)
				pw++;

			fieldLen = m_qwords[pw-1].m_word + 
				m_qwords[pw-1].m_wordLen -
				m_qwords[fieldStart].m_word;
		}
// 		// is UOR operator? if so, backup over it
// 		if ( pw >= 0 && m_qwords[pw].m_opcode == OP_UOR ){
// 			pw -= 2;
// 		}
// 		else          goto notUOR;
// 		if ( pw < 0 ) goto notUOR;
// 		// . if previous term is UOR'd with us then share the same ebit
// 		// . this allows us to use lots of UOR'd query terms
// 		// . the UOR'd lists may also be merged together into a single
// 		//   list if "mergeListMaxSize" is positive
// // 		if ( n >= 1 && 
// // 		     i >= 4 &&
// // 		     //m_qterms[n-1].m_qword  == &m_qwords[pw] &&
// // 		     shift > 0 &&
// // 		     qw->m_hardCount == 0 ) 
// // 			shift--;
// 		// set the UOR term sign
// 		qt->m_isUORed = true;
// 		if (m_qwords[pw].m_queryWordTerm) 
// 			m_qwords[pw].m_queryWordTerm->m_isUORed = true;
// 		if (m_qwords[pw].m_queryPhraseTerm) 
// 			m_qwords[pw].m_queryPhraseTerm->m_isUORed = true;
// 	notUOR:
		// do not use an explicit bit up if we have a hard count
		qt->m_hardCount = qw->m_hardCount;
// 		if ( qw->m_hardCount == 0 ) {
// 			qt->m_explicitBit = 1 << shift ;
// 			shift++;
// 		}
		qw->m_queryWordTerm   = qt;
		// IndexTable.cpp uses this one
		qt->m_inQuotes  = qw->m_inQuotes;
		// point to the string itself that is the word

		if (fieldLen > 0) {
			qt->m_term    = m_qwords[fieldStart].m_word;
			qt->m_termLen = fieldLen;
			// skip past the end of the field value
			i = pw-1;
		}
		else {
			qt->m_termLen   = qw->m_wordLen;
			qt->m_term      = qw->m_word;
			//log(LOG_DEBUG, "query: *** term \"%s\"", u8Buf);
		}
					  
		// reset our implicit bits to 0
		qt->m_implicitBits = 0;
// 		// . OR ourselves into our parent phrase's m_implicitBits
// 		// . this makes setting m_bitScores[] easy because if a
// 		//   doc contains this prhase then it IMPLICITLY contains us
// 		//   which will make it easier to satisfy requiredBits
//  		if ( qw->m_queryPhraseTerm ) 
//  			qw->m_queryPhraseTerm->m_implicitBits |= 
//  				qt->m_explicitBit;
// 		// if we're in the middle of the phrase
// 		long pn = qw->m_leftPhraseStart;
// 		// convert word to its phrase QueryTerm ptr, if any
// 		QueryTerm *tt = NULL;
// 		if ( pn >= 0 ) tt = m_qwords[pn].m_queryPhraseTerm;
// 		if ( tt      ) tt->m_implicitBits |= qt->m_explicitBit;
// 		// . there might be some phrase term that actually contains
// 		//   the same word as we are, but a different occurence
// 		// . like '"knowledge management" AND NOT management' query
// 		for ( long j = 0 ; j < i ; j++ ) {
// 			// must be our same wordId (same word, different occ.)
// 			QueryWord *qw2 = &m_qwords[j];
// 			if ( qw2->m_wordId != qw->m_wordId ) continue;
// 			// get first word in the phrase that jth word is in
// 			long pn2 = qw2->m_leftPhraseStart;
// 			if ( pn2 < 0 ) continue;
// 			// he implies us!
// 			QueryTerm *tt2 = m_qwords[pn2].m_queryPhraseTerm;
// 			if ( tt2 ) tt2->m_implicitBits |= qt->m_explicitBit;
// 			break;
// 		}
		// assume not under a NOT bool op
		qt->m_underNOT = false;
		// assign score weight, we're a phrase here
		qt->m_userWeight = qw->m_userWeight ;
		qt->m_userType   = qw->m_userType   ;
		qt->m_fieldCode  = qw->m_fieldCode  ;
		// stuff before a pipe always has a weight of 1
		if ( qt->m_piped ) {
			qt->m_userWeight = 1;
			qt->m_userType   = 'a';
		}
		// debug
		//char tmp[1024];
		//memcpy ( tmp , qt->m_term , qt->m_termLen );
		//tmp [ qt->m_termLen ] = 0;
		//logf(LOG_DEBUG,"got term %s (%li)",tmp,qt->m_termLen);
		n++;
	}
	



	// now handle the explicit bits
	// moved out of separate phrase and singleton loops
	// for phrase UOR support

	/*
	for ( long i = 0 ; i < m_numWords ; i++ ) {

		// break out if no more explicit bits!
// 		if ( shift >= max ) {
// 			log("query: Query has more than %li unique terms. "
// 			    "Truncating.",max);
// 			m_truncated = true;
// 			break; 
// 		}

		long pw;
		QueryWord *qw = &m_qwords[i];
		if (!qw->m_queryWordTerm && !qw->m_queryPhraseTerm) 
			continue;
		QueryTerm *qt = qw->m_queryPhraseTerm?
			qw->m_queryPhraseTerm :
			qw->m_queryWordTerm;
		if (!qt) continue;
	doAgain:
		pw = i-1;
		// . back up until word that contains quote if in a quoted 
		//   phrase
		// . UOR can only support two word phrases really...
		//if (m_qwords[i].m_quoteStart >= 0)
		//	pw = m_qwords[i].m_quoteStart - 1;
		while (pw>0 && 
		       ((m_qwords[pw].m_ignoreWord == IGNORE_DEFAULT) ||
			(m_qwords[pw].m_ignoreWord == IGNORE_FIELDNAME))) pw--;

		// is UOR operator? if so, backup over it
		if ( pw < 0 || m_qwords[pw].m_opcode != OP_UOR )
			goto notUOR;

		pw--;
		while (pw>0 && 
		       ((m_qwords[pw].m_ignoreWord == IGNORE_DEFAULT) ||
			(m_qwords[pw].m_ignoreWord == IGNORE_FIELDNAME))) pw--;

		if ( pw >= 0 && m_qwords[pw].m_quoteStart >= 0 ) 
			//pw = m_qwords[pw].m_quoteStart + 1;
			pw = m_qwords[pw].m_quoteStart;
		if (pw < 0) goto notUOR;
		// . if previous term is UOR'd with us then share the same ebit
		// . this allows us to use lots of UOR'd query terms
		// . the UOR'd lists may also be merged together into a single
		//   list if "mergeListMaxSize" is positive

		qt->m_isUORed = true;

		// set uor flag on all words in phrase
		if (qw->m_queryPhraseTerm && m_qwords[i].m_quoteStart >= 0){
			long quoteStart = m_qwords[i].m_quoteStart;
			for (long j=quoteStart;j<m_numWords;j++){
				if (m_qwords[j].m_ignoreWord) continue;
				if (m_qwords[j].m_quoteStart != quoteStart)
					break;
				QueryTerm *qtp = m_qwords[j].m_queryWordTerm;
				if (qtp) {
					qtp->m_isUORed = true;
					qtp->m_UORedTerm = 
						m_qwords[pw].m_queryPhraseTerm;
				}
			}
		}
		
		//QueryTerm *pqt = NULL;
 		if (m_qwords[pw].m_queryWordTerm){
 			m_qwords[pw].m_queryWordTerm->m_isUORed = true;
			qt->m_UORedTerm = m_qwords[pw].m_queryWordTerm;
		}
 		//pqt = m_qwords[pw].m_queryWordTerm;
		// set uor flag on all words in previous phrase
 		if (m_qwords[pw].m_queryPhraseTerm &&
		    m_qwords[pw].m_quoteStart >= 0) {
 			m_qwords[pw].m_queryPhraseTerm->m_isUORed = true;
			qt->m_UORedTerm = m_qwords[pw].m_queryPhraseTerm;
			long quoteStart = m_qwords[pw].m_quoteStart;
			for (long j=quoteStart;j<m_numWords;j++){
				if (m_qwords[j].m_ignoreWord) continue;
				if (m_qwords[j].m_quoteStart != quoteStart)
					break;
				QueryTerm *qtp = m_qwords[j].m_queryWordTerm;
				if (qtp) {
					qtp->m_isUORed = true;
					qtp->m_UORedTerm = 
						m_qwords[pw].m_queryPhraseTerm;
				}
			}
		}

		
// 		if ( n >= 1 && 
// 		     i >= 4 &&
// 		     //m_qterms[n-1].m_qword  == &m_qwords[pw] &&
// 		     shift > 0 &&
// 		     qw->m_hardCount == 0 ) {
// 			shift--;
// 		}
	notUOR:		
//  		if ( qt->m_hardCount == 0 ) {
// // 			qt->m_explicitBit = 1 << shift ;
//  			qt->m_explicitBit = shift ;
//  			shift++;

//  		}

// 		// . OR ourselves into our parent phrase's m_implicitBits
// 		// . this makes setting m_bitScores[] easy because if a
// 		//   doc contains this prhase then it IMPLICITLY contains us
// 		//   which will make it easier to satisfy requiredBits
//  		if ( qw->m_queryPhraseTerm ) 
//  			qw->m_queryPhraseTerm->m_implicitBits |= 
//  				qt->m_explicitBit;
// 		// if we're in the middle of the phrase
// 		long pn = qw->m_leftPhraseStart;
// 		// convert word to its phrase QueryTerm ptr, if any
// 		QueryTerm *tt = NULL;
// 		if ( pn >= 0 ) tt = m_qwords[pn].m_queryPhraseTerm;
// 		if ( tt      ) tt->m_implicitBits |= qt->m_explicitBit;
// 		// . there might be some phrase term that actually contains
// 		//   the same word as we are, but a different occurence
// 		// . like '"knowledge management" AND NOT management' query
// 		for ( long j = 0 ; j < i ; j++ ) {
// 			// must be our same wordId (same word, different occ.)
// 			//QueryWord *qw2 = m_qterms[j].m_qword;
// 			QueryWord *qw2 = &m_qwords[j];
// 			if ( qw2->m_wordId != qw->m_wordId ) continue;
// 			// get first word in the phrase that jth word is in
// 			long pn2 = qw2->m_leftPhraseStart;
// 			if ( pn2 < 0 ) continue;
// 			// he implies us!
// 			QueryTerm *tt2 = m_qwords[pn2].m_queryPhraseTerm;
// 			if ( tt2 ) tt2->m_implicitBits |= qt->m_explicitBit;
// 			break;
// 		}

		if (qt == qw->m_queryPhraseTerm){
			if ( qw->m_queryWordTerm){
				qt = qw->m_queryWordTerm;
				goto doAgain;
			}
		}
	}
	*/

	/*
	// Handle exclusive explicit bits only
	shift = 0;
	int n2 = 0;
	for ( long i = 0; i < n ; i++ ){
 		// break out if no more explicit bits!
 		if ( shift >= max ) {
 			logf(LOG_DEBUG,
			    "query: Query4 has more than %li unique terms. "
 			    "Truncating.",max);
 			m_truncated = true;
 			break; 
 		}
		QueryTerm *qt = &m_qterms[i];
		if (qt->m_UORedTerm) continue;
		// sometims UORedTerm is NULL i guess because of IGNORE_BREECH
		if ( qt->m_isUORed && qt->m_qword && qt->m_qword->m_ignoreWord ) 
			continue;
		// Skip duplicate terms before we waste an explicit bit
		bool skip=false;
		for (long j=0;j<i;j++){
			if ( qt->m_termId != m_qterms[j].m_termId   ||
			     qt->m_termSign != m_qterms[j].m_termSign){
				continue;
			}
			skip = true;
			qt->m_explicitBit  = m_qterms[j].m_explicitBit;
			break;
		}
		n2++;
		if (skip) continue;

 		if ( qt->m_hardCount == 0 ) {
			qt->m_explicitBit = 1 << shift++;
		}
	}
	// count them for doing number of combos
	m_numExplicitBits = shift;
	*/

	// Handle shared explicit bits
	for ( long i = 0; i < n ; i++ ){
		QueryTerm *qt = &m_qterms[i];
		// assume not in a phrase
		qt->m_inPhrase           = 0;
		qt->m_rightPhraseTermNum = -1;
		qt->m_leftPhraseTermNum  = -1;
		qt->m_rightPhraseTerm    = NULL;
		qt->m_leftPhraseTerm     = NULL;
		QueryTerm *qt2 = qt->m_UORedTerm;
		if (!qt2) continue;
		// chase down first term in UOR chain
		while (qt2->m_UORedTerm) qt2 = qt2->m_UORedTerm;
		//if (!qt2->m_explicitBit) continue;
		//qt->m_explicitBit = qt2->m_explicitBit;
		//n2++;
	}
	//m_numTerms = n2;

	// . set implicit bits, m_implicitBits
	// . set m_inPhrase
	for (long i = 0; i < m_numWords ; i++ ){
		QueryWord *qw = &m_qwords[i];
		QueryTerm *qt = qw->m_queryWordTerm;
		if (!qt) continue;
 		if ( qw->m_queryPhraseTerm )
 			qw->m_queryPhraseTerm->m_implicitBits |=
				qt->m_explicitBit;
		// set flag if in a a phrase, and set phrase term num
		if ( qw->m_queryPhraseTerm  ) {
			qt->m_inPhrase           = 1;
			QueryTerm *pt = qw->m_queryPhraseTerm;
			qt->m_rightPhraseTermNum = pt - m_qterms;
			qt->m_rightPhraseTerm    = pt;
		}
		// if we're in the middle of the phrase
		long pn = qw->m_leftPhraseStart;
		// convert word to its phrase QueryTerm ptr, if any
		QueryTerm *tt = NULL;
		if ( pn >= 0 ) tt = m_qwords[pn].m_queryPhraseTerm;
		if ( tt      ) tt->m_implicitBits |= qt->m_explicitBit;
		if ( tt      ) {
			qt->m_inPhrase          = 1;
			qt->m_leftPhraseTermNum = tt - m_qterms;
			qt->m_leftPhraseTerm    = tt;
		}
		// . there might be some phrase term that actually contains
		//   the same word as we are, but a different occurence
		// . like '"knowledge management" AND NOT management' query
		// . made it from "j < i" into "j < m_numWords" because
		//   'test "test bed"' was not working but '"test bed" test'
		//   was working.
		for ( long j = 0 ; j < m_numWords ; j++ ) {
			// must be our same wordId (same word, different occ.)
			QueryWord *qw2 = &m_qwords[j];
			if ( qw2->m_wordId != qw->m_wordId ) continue;
			// get first word in the phrase that jth word is in
			long pn2 = qw2->m_leftPhraseStart;
			// we might be the guy that starts it!
			if ( pn2 < 0 && qw2->m_quoteStart != -1 ) pn2 = j;
			// if neither is the case, skip this query word
			if ( pn2 < 0 ) continue;
			// he implies us!
			QueryTerm *tt2 = m_qwords[pn2].m_queryPhraseTerm;
			if ( tt2 ) tt2->m_implicitBits |= qt->m_explicitBit;
			if ( tt2 ) {
				qt->m_inPhrase          = 1;
				qt->m_leftPhraseTermNum = tt2 - m_qterms;
				qt->m_leftPhraseTerm    = tt2;
			}
			break;
		}
	}

	/*
	// synonym terms should have copy all the implicit/explicit bits
	// into their implicit bits field
	for (long i = 0; i < m_numTerms; i++) {
		QueryTerm *qt = &m_qterms[i];
		QueryTerm *st = qt->m_synonymOf;
		if (!st) continue;
		// also, if we are "auto insurance", a synonymOf 
		// "car insurance", we should also imply "car insurance"'s
		// terms, 'car' and 'insurance' for purposes of 
		// IndexTable2.cpp::getWeightScore()'s calculation of "min".
		// Because when finding the "max" score of a word, we also
		// allow its phrase and synonyms' scores to compete.
		qt->m_implicitBits = st->m_implicitBits | st->m_explicitBit;
		// now skip if not a phrase synonym
		if ( ! qt->m_isPhrase ) continue;
		// . we also imply the two words bookending this phrase, if any
		// . so see if the leftSynHash is in the syn list
		for ( long k = m_synTerm ; k < m_numTerms ; k++ ) {
			// get term
			QueryTerm *tt = &m_qterms[k];
			// skip if phrase
			if ( tt->m_isPhrase ) continue;
			// must be synonym
			if ( ! tt->m_synonymOf ) continue;
			// must match one of our ids
			if ( tt->m_qword->m_rawWordId != qt->m_leftRawWordId &&
			     tt->m_qword->m_rawWordId != qt->m_rightRawWordId )
				continue;
			// we imply it now!
			qt->m_implicitBits |= tt->m_explicitBit;
		}
	}
	*/

	////////////
	//
	// . add synonym query terms now
	// . skip this part if language is unknown i guess
	//
	////////////
	long sn = 0;
	Synonyms syn;
	// loop over all words in query and process its synonyms list
	if ( m_langId != langUnknown && m_queryExpansion ) 
		sn = m_numWords;

	long long to = hash64n("to",0LL);

	for ( long i = 0 ; i < sn ; i++ ) {
		// get query word
		QueryWord *qw  = &m_qwords[i];
		// skip if in quotes, we will not get synonyms for it
		if ( qw->m_inQuotes ) continue;
		// skip if has plus sign in front
		if ( qw->m_wordSign == '+' ) continue;
		// no url: stuff, maybe only title
		if ( qw->m_fieldCode &&
		     qw->m_fieldCode != FIELD_TITLE )
			continue;
		// skip if ignored like a stopword (stop to->too)
		//if ( qw->m_ignoreWord ) continue;
		// no, hurts 'Greencastle IN economic development'
		if ( qw->m_wordId == to ) continue;
		// single letters...
		if ( qw->m_wordLen == 1 ) continue;
		// set the synonyms for this word
		char tmpBuf [ TMPSYNBUFSIZE ];
		long naids = syn.getSynonyms ( &words ,
					       i ,
					       m_langId ,
					       tmpBuf ,
					       0 ); // m_niceness );
		// if no synonyms, all done
		if ( naids <= 0 ) continue;
		// get the term for this word
		QueryTerm *origTerm = qw->m_queryWordTerm;
		// loop over synonyms for word #i now
		for ( long j = 0 ; j < naids ; j++ ) {
			// stop breach
			if ( n >= MAX_QUERY_TERMS ) {
				log("query: lost synonyms due to max term "
				    "limit of %li",(long)MAX_QUERY_TERMS );
				break;
			}
			// this happens for 'da da da'
			if ( ! origTerm ) continue;
			// add that query term
			QueryTerm *qt   = &m_qterms[n];
			qt->m_qword     = qw; // NULL;
			qt->m_piped     = qw->m_piped;
			qt->m_isPhrase  = false ;
			qt->m_isUORed   = false;
			qt->m_UORedTerm = NULL;
			// synonym of this term...
			qt->m_synonymOf = origTerm;
			// nuke this crap since it was done above and we
			// missed out!
			qt->m_inPhrase           = 0;
			qt->m_rightPhraseTermNum = -1;
			qt->m_leftPhraseTermNum  = -1;
			qt->m_rightPhraseTerm    = NULL;
			qt->m_leftPhraseTerm     = NULL;
			// need this for Matches.cpp
			qt->m_synWids0 = syn.m_wids0[j];
			qt->m_synWids1 = syn.m_wids1[j];
			long na        = syn.m_numAlnumWords[j];
			// how many words were in the base we used to
			// get the synonym. i.e. if the base is "new jersey"
			// then it's 2! and the synonym "nj" has one alnum
			// word.
			long ba        = syn.m_numAlnumWordsInBase[j];
			qt->m_numAlnumWordsInSynonym = na;
			qt->m_numAlnumWordsInBase    = ba;

			// crap, "nj" is a synonym of the PHRASE TERM
			// bigram "new jersey" not of the single word term
			// "new" so fix that.
			if ( ba == 2 && origTerm->m_rightPhraseTerm )
				qt->m_synonymOf = origTerm->m_rightPhraseTerm;

			// ignore some synonym terms if tf is too low
			qt->m_ignored = qw->m_ignoreWord;
			// assume not a repeat of another query term(set below)
			qt->m_repeat    = false;
			// stop word? no, we're a phrase term
			qt->m_isQueryStopWord = qw->m_isQueryStopWord;
			// change in both places
			qt->m_termId    = syn.m_aids[j] & TERMID_MASK;
			m_termIds[n]    = syn.m_aids[j] & TERMID_MASK;
			qt->m_rawTermId = syn.m_aids[j];
			// assume explicit bit is 0
			qt->m_explicitBit = 0;
			qt->m_matchesExplicitBits = 0;
			// boolean queries are not allowed term signs
			if ( m_isBoolean ) {
				qt->m_termSign = '\0';
				m_termSigns[n] = '\0';
				// boolean fix for "health OR +sports" because
				// the + there means exact word match, no syns
				if ( qw->m_wordSign == '+' ) {
					qt->m_termSign  = qw->m_wordSign;
					m_termSigns[n]  = qw->m_wordSign;
				}
			}
			// if not bool, ensure to change signs in both places
			else {
				qt->m_termSign  = qw->m_wordSign;
				m_termSigns[n]  = qw->m_wordSign;
			}
			// do not use an explicit bit up if we got a hard count
			qt->m_hardCount = qw->m_hardCount;
			//qw->m_queryWordTerm   = qt;
			// IndexTable.cpp uses this one
			qt->m_inQuotes  = qw->m_inQuotes;
			// point to the string itself that is the word
			qt->m_term     = syn.m_termPtrs[j];
			qt->m_termLen  = syn.m_termLens[j];
			// reset our implicit bits to 0
			qt->m_implicitBits = 0;
			// assume not under a NOT bool op
			qt->m_underNOT = false;
			// assign score weight, we're a phrase here
			qt->m_userWeight = qw->m_userWeight ;
			qt->m_userType   = qw->m_userType   ;
			qt->m_fieldCode  = qw->m_fieldCode  ;
			// stuff before a pipe always has a weight of 1
			if ( qt->m_piped ) {
				qt->m_userWeight = 1;
				qt->m_userType   = 'a';
			}
			// otherwise, add it
			n++;
		}
	}

	m_numTerms = n;
	
	if ( n > MAX_QUERY_TERMS ) { char *xx=NULL;*xx=0; }


	// count them for doing number of combos
	//m_numExplicitBits = shift;

	// . repeated terms have the same termbits!!
	// . this is only for bool queries since regular queries ignore
	//   repeated terms in setWords()
	// . we need to support: "trains AND (perl OR python) NOT python"
	for ( long i = 0 ; i < n ; i++ ) {
		// BUT NOT IF in a UOR'd list!!! Metalincs bug...
		if ( m_qterms[i].m_isUORed ) continue;
		// that didn't seem to fix it right, for dup terms that
		// are the FIRST term in a UOR sequence... they don't seem
		// to have m_isUORed set
		if ( m_hasUOR ) continue;
		for ( long j = 0 ; j < i ; j++ ) {
			// skip if not a termid match
			if(m_qterms[i].m_termId!=m_qterms[j].m_termId)continue;
			m_qterms[i].m_explicitBit = m_qterms[j].m_explicitBit;
			// if doing phrases, ignore the unrequired phrase
			if ( m_qterms[i].m_isPhrase ) {
				if ( m_qterms[j].m_implicitBits )
					m_qterms[j].m_repeat = true;
				else 
					m_qterms[i].m_repeat = true;
				continue;
			}
			// if not doing phrases, just ignore term #i
			m_qterms[i].m_repeat      = true;
		}
	}


	// if we're a special range: term and a doc has us, then
	// assume it has our associates too because we are all
	// essentially the same term. we don't want this to be a
	// factor in the ranking. since gigablast usually puts docs
	// with all the terms (between OR operators) above terms that do
	// not have all ther terms. that is not a good thing for these terms.
	/*
	long nw = m_numWords;
	for ( long i = 0 ; i < nw ; i++ ) {
		// skip if not a range: query term
		if ( m_qwords[i].m_fieldCode != FIELD_RANGE ) continue;
		// loop over all our associates (in same parens level) to
		// get the OR of all the explicit bits
		qvec_t allBits = 0;
		for ( long j=i;j<nw &&m_qwords[j].m_opcode!=OP_RIGHTPAREN;j++){
			if ( m_qwords[j].m_ignoreWord ) continue;
			// get the jth word's term
			QueryTerm *qt = m_qwords[j].m_queryWordTerm;
			// this can be NULL if we already got 16 query terms!
			if ( ! qt ) continue;
			// skip if no value
			if ( ! qt->m_explicitBit ) continue;
			// grab it
			allBits |= qt->m_explicitBit ;
		}
		// now make everyone use just one of those bits
		for ( long j=i;j<nw &&m_qwords[j].m_opcode!=OP_RIGHTPAREN;j++){
			if ( m_qwords[j].m_ignoreWord ) continue;
			// get the jth word's term
			QueryTerm *qt = m_qwords[j].m_queryWordTerm;
			// this can be NULL if we already got 16 query terms!
			if ( ! qt ) continue;
			// skip if no value
			if ( ! qt->m_explicitBit ) continue;
			// force it to use the common bit
			qt->m_explicitBit  = allBits;
			qt->m_implicitBits = allBits;
		}
	}
	*/

	// . if only have one term and it is a signless phrase, make it signed
	// . don't forget to set m_termSigns too!
	if ( n == 1 && m_qterms[0].m_isPhrase && ! m_qterms[0].m_termSign ) {
		m_qterms[0].m_termSign = '*';
		m_termSigns[0]         = '*';
	}

	// . or bits into the m_implicitBits member of phrase QueryTerms that
	//   represent the consitutent words
	// . loop over each 
	//m_numTerms = n2;

	// . how many of the terms are non fielded singletons?
	// . this is just for support of the BIG HACK in Summary.cpp
	/*
	m_numTermsSpecial = 0;
	for ( long i = 0 ; i < n ; i++ ) {
		if ( m_qterms[i].m_isPhrase        ) continue;
		if ( m_qterms[i].m_fieldCode       ) continue;
		if ( m_qterms[i].m_isUORed         ) continue;
		// only skip query stop words if in quotes, if it is in 
		// quotes then we gotta have it...
		if ( m_qterms[i].m_isQueryStopWord && !
		     m_qterms[i].m_inQuotes        ) continue;
		if ( m_qterms[i].m_underNOT        ) continue;
		if ( m_qterms[i].m_termSign == '-' ) continue;
		m_numTermsSpecial++;
	}
	*/

	// . set m_componentCodes all to -2
	// . addCompoundTerms() will set these appropriately
	// . see Msg2.cpp for more info on componentCodes
	// . -2 means unset, neither a compound term nor a component term at
	//   this time
	for ( long i = 0 ; i < m_numTerms ; i++ ) m_componentCodes[i] = -2;
	m_numComponents = 0;

	// . now set m_phrasePart for Summary.cpp's hackfix filter
	// . only set this for the non-phrase terms, since keepAllSingles is
	//   set to true when setting the Query for Summary.cpp::set in order
	//   to match the singles
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// assume not in a phrase
		m_qterms[i].m_phrasePart = -1; 
		//if ( ! m_qterms[i].m_isPhrase ) continue;
		// skip cd-rom too, if not in quotes
		if ( ! m_qterms[i].m_inQuotes ) continue;
		// is next term also in a quoted phrase?
		if ( i - 1 < 0 ) continue;
		//if ( ! m_qterms[i+1].m_isPhrase ) continue;
		if ( ! m_qterms[i-1].m_inQuotes ) continue;
		// are we in the same quoted phrase?
		if ( m_qterms[i+0].m_qword->m_quoteStart !=
		     m_qterms[i-1].m_qword->m_quoteStart  ) continue;
		// ok, we're in the same quoted phrase
		m_qterms[i+0].m_phrasePart=m_qterms[i+0].m_qword->m_quoteStart;
		m_qterms[i-1].m_phrasePart=m_qterms[i+0].m_qword->m_quoteStart;
	}

	// . set m_requiredBits
	// . these are 1-1 with m_qterms (QueryTerms)
	// . required terms have no - sign and have no signless phrases
	// . these are what terms doc would NEED to have if we were default AND
	//   BUT for boolean queries that doesn't apply
	m_requiredBits = 0; // no - signs, no signless phrases
	m_negativeBits = 0; // terms with - signs
	m_forcedBits   = 0; // terms with + signs
	m_synonymBits  = 0;
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// don't require if negative
		if ( qt->m_termSign == '-' ) {
			m_negativeBits |= qt->m_explicitBit; // (1 << i );
			continue;
		}
		// forced bits
		if ( qt->m_termSign == '+' && ! m_isBoolean ) 
			m_forcedBits |= qt->m_explicitBit; //(1 << i);
		// skip signless phrases
		if ( qt->m_isPhrase && qt->m_termSign == '\0' ) continue;
		if ( qt->m_synonymOf ) {
			m_synonymBits |= qt->m_explicitBit; 
			continue;
		}
		// fix gbhastitleindicator:1 where "1" is a stop word
		if ( qt->m_isQueryStopWord && ! m_qterms[i].m_fieldCode ) 
			continue;
		// OR it all up
		m_requiredBits |= qt->m_explicitBit; // (1 << i);
	}

	// set m_matchRequiredBits which we use for Matches.cpp
	m_matchRequiredBits = 0;
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// don't require if negative
		if ( qt->m_termSign == '-' ) continue;
		// skip all phrase terms
		if ( qt->m_isPhrase ) continue;
		// OR it all up
		m_matchRequiredBits |= qt->m_explicitBit;
	}

	// if we have '+test -test':
	if ( m_negativeBits & m_requiredBits ) 
		m_numTerms = 0;

	// we need to remember this now for tier integration in IndexTable.cpp
	//m_requiredBits = requiredBits;

        // now set m_matches,ExplicitBits, used only by Matches.cpp so far
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// set it up
		m_qterms[i].m_matchesExplicitBits = m_qterms[i].m_explicitBit;
		// or in the repeats
		for ( long j = 0 ; j < m_numTerms ; j++ ) {
			// skip if termid mismatch
			if ( m_qterms[i].m_termId != m_qterms[j].m_termId ) 
				continue;
			// i guess signs do not have to match
			//m_qterms[i].m_termSign == m_qterms[j].m_termSign){
			m_qterms[i].m_matchesExplicitBits |= 
				m_qterms[j].m_explicitBit;
		}
	}

	m_numRequired = 0;
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// assume not required
		qt->m_isRequired = false;
		// don't require if negative
		// no, consider required, but NEGATIVE required...
		//if ( qt->m_termSign == '-' ) continue;
		// skip signless phrases
		if ( qt->m_isPhrase && qt->m_termSign == '\0' ) continue;
		if ( qt->m_isPhrase && qt->m_termSign == '*'  ) continue;
		if ( qt->m_synonymOf ) continue;
		// IGNORE_QSTOP?
		if ( qt->m_ignored ) continue;
		// mark it
		qt->m_isRequired = true;
		// count them
		m_numRequired++;
	}


	// required quoted phrase terms
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// quoted phrase?
		if ( ! qt->m_isPhrase ) continue;
		if ( ! qt->m_inQuotes ) continue;
		// mark it
		qt->m_isRequired = true;
		// count them
		m_numRequired++;
	}


	// . for query 'to be or not to be shakespeare'
	//   require 'tobe' 'beor' 'tobe' because
	//   they are bigrams in the wikipedia phrase 'to be or not to be'
	//   and they all consist solely of query stop words. as of
	//   8/20/2012 i took 'not' off the query stop word list.
	// . require bigrams that consist of 2 query stop words and
	//   are in a wikipedia phrase. set termSign to '+' i guess?
	// . for 'in the nick' , a wiki phrase, make "in the" required
	//   and give a big bonus for "the nick" below.
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// don't require if negative
		if ( qt->m_termSign == '-' ) continue;
		// only check bigrams here
		if ( ! qt->m_isPhrase ) continue;
		// get the query word that starts this phrase
		QueryWord *qw1 = qt->m_qword;
		// must be in a wikiphrase
		if ( qw1->m_wikiPhraseId <= 0 ) continue;
		// what query word # is that?
		long qwn = qw1 - m_qwords;
		// get the next alnum word after that
		// assume its the last word in our bigram phrase
		QueryWord *qw2 = &m_qwords[qwn+2];
		// must be in same wikiphrase
		if ( qw2->m_wikiPhraseId != qw1->m_wikiPhraseId ) continue;
		// must be two stop words
		if ( ! qw1->m_isQueryStopWord ) continue;
		if ( ! qw2->m_isQueryStopWord ) continue;
		// mark it
		qt->m_isRequired = true;
		// count them
		m_numRequired++;
	}

	//
	// new logic for XmlDoc::setRelatedDocIdWeight() to use
	//
	long shift = 0;
	m_requiredBits = 0;
	for ( long i = 0; i < n ; i++ ){
		QueryTerm *qt = &m_qterms[i];
		qt->m_explicitBit = 0;
		if ( ! qt->m_isRequired ) continue;
		// negative terms are "negative required", but we ignore here
		if ( qt->m_termSign == '-' ) continue;
		qt->m_explicitBit = 1<<shift;
		m_requiredBits |= qt->m_explicitBit;
		shift++;
		if ( shift >= (long)(sizeof(qvec_t)*8) ) break;
	}
	// now implicit bits
	for ( long i = 0; i < n ; i++ ){
		QueryTerm *qt = &m_qterms[i];
		// make it explicit bit at least
		qt->m_implicitBits = qt->m_explicitBit;
		if ( qt->m_isRequired ) continue;
		// synonym?
		if ( qt->m_synonymOf )
			qt->m_implicitBits |= qt->m_synonymOf->m_explicitBit;
		// skip if not bigram
		if ( ! qt->m_isPhrase ) continue;
		// get sides
		QueryTerm *t1 = qt->m_leftPhraseTerm;
		QueryTerm *t2 = qt->m_rightPhraseTerm;
		if ( ! t1 || ! t2 ) continue;
		qt->m_implicitBits |= t1->m_explicitBit;
		qt->m_implicitBits |= t2->m_explicitBit;
	}



	// . for query 'to be or not to be shakespeare'
	//   give big bonus for 'ornot' and 'notto' bigram terms because
	//   the single terms 'or' and 'to' are ignored and because
	//   'to be or not to be' is a wikipedia phrase
	// . on 8/20/2012 i took 'not' off the query stop word list.
	// . now give a big bonus for bigrams whose two terms are in the
	//   same wikipedia phrase and one and only one of the terms in
	//   the bigram is a query stop word
	// . in general 'ornot' is considered a "synonym" of 'not' and
	//   gets hit with a .90 score factor, but that should never
	//   happen, it should be 1.00 and in this special case it should
	//   be 1.20
	// . so for 'time enough for love' the phrase term "enough for"
	//   gets its m_isWikiHalfStopBigram set AND that phrase term
	//   is a synonym term of the single word term "enough" and is treated
	//   as such in the Posdb.cpp logic.
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// assume not!
		qt->m_isWikiHalfStopBigram = 0;
		// don't require if negative
		if ( qt->m_termSign == '-' ) continue;
		// only check bigrams here
		if ( ! qt->m_isPhrase ) continue;
		// get the query word that starts this phrase
		QueryWord *qw1 = qt->m_qword;
		// must be in a wikiphrase
		if ( qw1->m_wikiPhraseId <= 0 ) continue;
		// what query word # is that?
		long qwn = qw1 - m_qwords;
		// get the next alnum word after that
		// assume its the last word in our bigram phrase
		QueryWord *qw2 = &m_qwords[qwn+2];
		// must be in same wikiphrase
		if ( qw2->m_wikiPhraseId != qw1->m_wikiPhraseId ) continue;
		// if both query stop words, should have been handled above
		// we need one to be a query stop word and the other not
		// for this algo
		if ( qw1->m_isQueryStopWord && qw2->m_isQueryStopWord )
			continue;
		// skip if neither is a query stop word
		if ( ! qw1->m_isQueryStopWord&& ! qw2->m_isQueryStopWord )
			continue;
		// one must be a stop word i guess
		// so for 'the time machine' we do not count 'time machine'
		// as a halfstopwikibigram
		if ( ! qw1->m_isQueryStopWord && ! qw2->m_isQueryStopWord )
			continue;
		// don't require it, if query is 'the tigers' accept
		// just 'tigers' but give a bonus for 'the tigers' in
		// the document.
		//qt->m_isRequired = true;
		// count them
		//m_numRequired++;
		// special flag
		qt->m_isWikiHalfStopBigram = true;
	}
	
	return true;
}

/*
// . add in compound terms
// . set m_componentCodes appropriately
void Query::addCompoundTerms ( ) {
	// loop through possible starting points of sequences of the same ebit
	for (long i = 0 ; i < m_numTerms - 1 ; i++ ) {
		// break if too many already
		if ( m_numTerms >= MAX_QUERY_TERMS ) break;
		// if already processed, skip it
		if ( m_componentCodes[i] != -2 ) continue;
		// get ebit of the ith query term
		qvec_t ebit = m_qterms[i].m_explicitBit;
		// skip if 0, it is ignored because it breeched limit of 15
		if ( ebit == 0 ) continue;

		// skip if next term's ebit is different
		//if ( ebit != m_qterms[i+1].m_explicitBit ) continue;
		// skip if not UOR'd because it could just be a repeat term
		//if ( ! m_qterms[i+1].m_isUORed ) continue;

		// all UORed terms have m_isOURed set now
		// because UORed terms are not necessarily in order
		// (first phrases, then words)
		if ( ! m_qterms[i].m_isUORed ) continue;
		// the termid of the compound list
		long long id = 0LL;
		// store compound terms last
		long n = m_numTerms;
		// sum of termfreqs
		//long long sum = 0;
		// we got a UOR'd list, see whose involved
		long j ;
		long numUORComponents = 0;
		char *beg = NULL;
		char *end = NULL;
		for ( j = 0; j < m_numTerms ; j++ ) {
			// if term does not have our ebit, break out
			if ( ebit != m_qterms[j].m_explicitBit ) continue;
			// otherwise, make this term point to the compound term
			m_componentCodes[j] = n;
			// an integrate its termid into the compound termid
			id = hash64 ( m_qterms[j].m_termId , id ) &TERMID_MASK;
			// add in the term frequency (aka popularity)
			//sum += m_termFreqs[j];
			// keep track so IndexTable::alloc() can get it
			m_numComponents++;
			numUORComponents++;
			
			// get phrase UOR term right
			long a = j;
			long b = j;
// 			if (m_qterms[j].m_qword->m_leftPhraseStart >= 0){
// 				a = m_qterms[j].m_qword->m_leftPhraseStart;
// 				b++;
// 			}
			char *newBeg = m_qterms[a].m_term; 
			// had to add check for newBeg being null
			// (because of -O2 ???)
			if (!beg || (newBeg && newBeg < beg)) 
				beg = newBeg;
			char *newEnd = m_qterms[b].m_term 
				+ m_qterms[b].m_termLen;
			if (!end || newEnd > end)
				end = newEnd;
		}
		if (!numUORComponents) continue;
		// copy it
		memcpy ( &m_qterms[n] , &m_qterms[i] , sizeof(QueryTerm) );
		// get term's length
		//char *beg = m_qterms[i].m_term;
		//char *end = m_qterms[j-1].m_term + m_qterms[j-1].m_termLen;
		m_qterms[n].m_term    = beg;
		m_qterms[n].m_termLen = end - beg;
		// set its id
		m_qterms[n].m_termId    = id;
		// this array too!
		m_termIds[n] = id;
		m_qterms[n].m_rawTermId = 0LL;
		m_qterms[n].m_isQueryStopWord = false;
		m_componentCodes[n]  = -1; // code for a compound termid is -1
		//m_termFreqs     [n]  = sum;
		m_termSigns     [n]  = '\0';
		// inc the total term count
		m_numTerms++;
	}
}
*/

// -1 means compound, -2 means unset, >= 0 means component
bool Query::isCompoundTerm ( long i ) {
	return ( m_componentCodes[i] == -1 );
}

bool Query::setQWords ( char boolFlag , 
			bool keepAllSingles ,
			Words &words ,
			Phrases &phrases ) {

	// . break query up into Words and phrases
	// . because we now deal with boolean queries, we make parentheses
	//   their own separate Word, so tell "words" we're setting a query
	//Words words;
	if ( ! words.set ( m_buf , m_bufLen,
			    TITLEREC_CURRENT_VERSION, true, true ) )
		return log("query: Had error parsing query: %s.",
			   mstrerror(g_errno));
	long numWords = words.getNumWords();
	// truncate it
	if ( numWords > MAX_QUERY_WORDS ) {
		log("query: Had %li words. Max is %li. Truncating.",
		    numWords,(long)MAX_QUERY_WORDS);
		numWords = MAX_QUERY_WORDS;
		m_truncated = true;
	}
	m_numWords = numWords;
	// alloc the mem if we need to (mdw left off here)
	long need = m_numWords * sizeof(QueryWord);
	// sanity check
	if ( m_qwords || m_qwordsAllocSize ) { char *xx = NULL; *xx = 0; }
	// point m_qwords to our generic buffer if it will fit
	//	if ( need < GBUF_SIZE ) {
	if ( m_gnext + need < m_gbuf + GBUF_SIZE ) {
		m_qwords = (QueryWord *)m_gnext;
		m_gnext += need;
	}
	// otherwise, we must allocate memory for it
	else {
		m_qwords = (QueryWord *)mmalloc ( need , "Query4" );
		if ( ! m_qwords ) 
			return log("query: Could not allocate mem for query.");
		m_qwordsAllocSize = need;
	}

	// is all alpha chars in query in upper case? caps lock on?
	bool allUpper = true;
	char *p    = m_buf;
	char *pend = m_buf + m_bufLen;
	for ( ; p < pend ; p += getUtf8CharSize(p) )
		if ( is_alpha_utf8 ( p ) && ! is_upper_utf8 ( p ) ) {
			allUpper = false; break; }

	// . come back here from below when we detect dat query is not boolean
	// . we need to redo the bits cuz they may have been messed with below
	// redo:
	// field code we are in
	char  fieldCode = 0;
	char  fieldSign = 0;
	char *field     = NULL;
	long  fieldLen  = 0;
	// keep track of the start of different chunks of quotes
	long quoteStart = -1;
	bool inQuotes   = false;
	//bool inVQuotes   = false;
	char quoteSign  = 0;
	// the current little sign
	char wordSign   = 0;
	// when reading first word in link: ... field we skip the following
	// words until we hit a space because we hash them all together
	bool ignoreTilSpace = false;
	// assume we're NOT a boolean query
	m_isBoolean = false;
	// used to not respect the bool operator if it is the first word
	bool firstWord = true;

	// the query processing is broken into 3 stages.

	// . STAGE #1
	// . reset all query words to default
	//   set all m_ignoreWord and m_ignorePhrase to IGNORE_DEFAULT
	// . set m_isFieldName, m_fieldCode and m_quoteStart for query words. 
	//   no field names in quotes. +title:"hey there". 
	//   set m_quoteStart to -1 if not in quotes. 
	// . if quotes immediately follow field code's ':' then distribute
	//   the field code to all words in the quotes
	// . distribute +/- signs across quotes and fields to m_wordSigns.
	//   support -title:"hey there".
	// . set m_quoteStart to -1 if only one alnum word is 
	//   in quotes, what's the point of that?
	// . set boolean op codes (m_opcode). cannot be in quotes. 
	//   cannot have a field code. cannot have a word sign (+/-).
	// . set m_wordId of FIELD_LINK, _URL, _SITE, _IP  fields.
	//   m_wordId of first should be hash of the whole field value.
	//   only set its m_ignoreWord to 0, keep it's m_ignorePhrase to DEF.
	// . set m_ignore of non-op codes, non-fieldname, alnum words to 0.
	// . set m_wordId of each non-ignored alnum word.

	// . STAGE #2
	// . customize Bits class:
	//   first alnum word can start phrase.
	//   first alnum word in quotes (m_quoteStart >= 0 ) can start phrase.
	//   connected on the right but not on the left.. can start phrase.
	//   no pair across any double quote
	//   no pair across ".." --- UNLESS in quotes!
	//   no pair across any change of field code.
	//   field names may not be part of any phrase or paired across.
	//   boolean ops may not be part of any phrase or paired across.
	//   ignored words may not be part of any phrase or paired across.

	// . STAGE #3
	// . set phrases class w/ custom Bits class mods.
	// . set m_phraseId and m_rawPhraseId of all QueryWords. if phraseId
	//   is not 0 (phrase exists) then set m_ignorePhrase to 0.
	// . set m_leftConnected, m_rightConnected. word you are connecting
	//   to must not be ignored. (no field names or op codes).
	//   ensure you are in a phrase with the connected word, too, to
	//   really be connected.
	// . set m_leftPhraseStart and m_rightPhraseEnd for all
	//   m_inQuotePhrase is not needed since if m_quoteStart is >= 0
	//   we MUST be in a quoted phrase!
	// . if word is Connected then set m_ignoreWord to IGNORE_CONNECTED.
	//   set his m_phraseSign to m_wordSign (if not 0) or '*' (if it is 0).
	//   m_wordSign may have inherited quote or field sign.
	// . if word's m_quoteStart is >= 0 set m_ignoreWord to IGNORE_QUOTED
	//   set his m_phraseSign to m_wordSign (if not 0) or '*' (if it is 0)
	//   m_wordSign may have inherited quote or field sign.
	// . if one word in a phrase is negative, then set m_phraseSign to '-'

	// set the Bits used for making phrases from the Words class
	Bits bits;
	if ( ! bits.set ( &words, TITLEREC_CURRENT_VERSION , 0 ))
		return log("query: Had error processing query: %s.",
			   mstrerror(g_errno));

	long userWeight       = 1;
	char userType         = 'r';
	long userWeightPhrase = 1;
	char userTypePhrase   = 'r';
	long ignorei          = -1;

	// assume we contain no pipe operator
	long pi = -1;

	// loop over all words, these QueryWords are 1-1 with "words"
	for ( long i = 0 ; i < numWords && i < MAX_QUERY_WORDS ; i++ ) {
		// convenience var, these are 1-1 with "words"
		QueryWord *qw = &m_qwords[i];
		// set to defaults?
		memset ( qw , 0 , sizeof(QueryWord) );
		// but quotestart should be -1
		qw->m_quoteStart = -1;
		qw->m_leftPhraseStart = -1;
		// assume QueryWord is ignored by default
		qw->m_ignoreWord   = IGNORE_DEFAULT;
		qw->m_ignorePhrase = IGNORE_DEFAULT;

		// get word as a string
		//char *w    = words.getWord(i);
		//long  wlen = words.getWordLen(i);
		qw->m_word    = words.getWord(i);
		qw->m_wordLen = words.getWordLen(i);
		qw->m_isPunct = words.isPunct(i);
		char *w   = words.getWord(i);
		long wlen = words.getWordLen(i);
		// assume it is a query weight operator
		qw->m_queryOp = true;
		// ignore it? (this is for query weight operators)
		if ( i <= ignorei ) continue;
		// deal with pipe operators
		if ( wlen == 5 &&
		     w[0]=='P'&&w[1]=='i'&&w[2]=='i'&&w[3]=='P'&&w[4]=='E') {
			pi = i;
			qw->m_opcode = OP_PIPE;
			continue;
		}
		// is it the bracket operator?
		// " LeFtB 113 rp RiGhB "
		if ( wlen == 5 &&
		     w[0]=='L'&&w[1]=='e'&&w[2]=='F'&&w[3]=='t'&&w[4]=='B'&& 
		     i+4 < numWords ) {
			// s MUST point to a number
			char *s = words.getWord(i+2);
			long slen = words.getWordLen(i+2);
			// if no number, it must be
			// " leFtB RiGhB " or " leFtB p RiGhB "
			if ( ! is_digit(s[0]) ) {
				// phrase weight reset
				if ( s[0] == 'p' ) {
					userWeightPhrase = 1;
					userTypePhrase   = 'r';
					ignorei = i + 4;
				}
				// word reset
				else {
					userWeight = 1;
					userType   = 'r';
					ignorei = i + 2;
				}
				continue;
			}
			// get the number
			long val = atol2 (s, slen);
			// s2 MUST point to the a,r,ap,rp string
			char *s2 = words.getWord(i+4);
			// is it a phrase?
			if ( s2[1] == 'p' ) {
				userWeightPhrase = val;
				userTypePhrase   = s2[0]; // a or r
			}
			else {
				userWeight = val;
				userType   = s2[0]; // a or r
			}
			// ignore all following words up and inc. i+6
			ignorei = i + 6;
			continue;
		}
					
		// assign score weight, if any for this guy
		qw->m_userWeight       = userWeight       ;
		qw->m_userType         = userType         ;
		qw->m_userWeightPhrase = userWeightPhrase ;
		qw->m_userTypePhrase   = userTypePhrase   ;
		qw->m_queryOp          = false;
		// does word #i have a space in it? that will cancel fieldCode
		// if we were in a field
		bool endField = false;
		if ( words.hasSpace(i) && ! inQuotes ) endField = true;
		// TODO: fix title:" hey there" (space in quotes is ok)
		// if there's a quote before the first space then
		// it's ok!!!
		if ( endField ) {
			char *s = words.m_words[i];
			char *send = s + words.m_wordLens[i];
			for ( ; s < send ; s++ ) {
				// if the space is inside the quotes then it
				// doesn't count!
				if ( *s == '\"' ) { endField = false; break;}
				if ( is_wspace_a(*s) ) break;
			}
		}
		// cancel the field if we hit a space (not in quotes)
		if ( endField ) {
			// cancel the field
			fieldCode = 0;
			fieldLen  = 0;
			field     = NULL;
			// we no longer have to ignore for link: et al
			ignoreTilSpace = false;
		}
		// . maintain inQuotes and quoteStart
		// . quoteStart is the word # that starts the current quote
		long nq = words.getNumQuotes(i) ;

		if ( nq > 0 ) { // && ! ignoreQuotes ) {
			// toggle quotes if we need to
			if ( nq & 0x01 ) inQuotes   = ! inQuotes;
			// set quote sign to sign before the quote
			if ( inQuotes ) {
				quoteSign = '\0';
				for ( char *p = w + wlen - 1 ; p > w ; p--){
					if ( *p != '\"' ) continue;
					if ( *(p-1) == '-' ) quoteSign = '-';
					if ( *(p-1) == '+' ) quoteSign = '+';
					break;
				}
			}
			// . quoteStart is the word # the quotes started at
			// . it is -1 if not in quotes
			// . now we set it to the alnum word AFTER us!!
			if   ( inQuotes && i+1< numWords ) quoteStart =  i+1;
			else                               quoteStart = -1;
		}
		//log(LOG_DEBUG, "Query: nq: %ld inQuotes: %d,quoteStart: %ld",
		//    nq, inQuotes, quoteStart);
		// does word #i have a space in it? that will cancel fieldCode
		// if we were in a field
		// TODO: fix title:" hey there" (space in quotes is ok)
		bool cancelField = false;
		if ( words.hasSpace(i) && ! inQuotes ) cancelField = true;
		// BUT if we have a quote, and they just got turned off,
		// and the space is not after the quote, do not cancel field!
		if ( nq == 1 && cancelField ) {
			// if we hit the space BEFORE the quote, do NOT cancel
			// the field
			for ( char *p = w + wlen - 1 ; p > w ; p--) {
				// hey, we got the quote first, keep field
				if ( *p == '\"' ) {cancelField = false; break;}
				// otherwise, we got space first? cancel it!
				if ( is_wspace_a(*p) ) break;
			}
		}
		if ( cancelField ) {
			// cancel the field
			fieldCode = 0;
			fieldLen  = 0;
			field     = NULL;
			// we no longer have to ignore for link: et al
			ignoreTilSpace = false;
		}
		// skip if we should
		if ( ignoreTilSpace ){
			if (m_qwords[i-1].m_fieldCode){
				qw->m_fieldCode = m_qwords[i-1].m_fieldCode;
			}
			continue;
		}
		// . is this word potentially a field? 
		// . it cannot be another field name in a field
		if ( i < (m_numWords-2) &&
		     w[wlen]   == ':' && ! is_wspace_utf8(w+wlen+1) && 
		     //w[wlen+1] != '/' &&  // as in http://
		     (! is_punct_utf8(w+wlen+1) || w[wlen+1]=='\"' ||
		      // for gblatrange2:-106.940994to-106.361282
		      w[wlen+1]=='-') &&  
		     ! fieldCode      && ! inQuotes                ) {
			// field name may have started before though if it
			// was a compound field name containing hyphens,
			// underscores or periods
			long  j = i-1 ;
			while ( j > 0 && 
				((m_qwords[j].m_rawWordId != 0) ||
				 (  m_qwords[j].m_wordLen ==1 &&
				  ((m_qwords[j].m_word)[0]=='-' || 
				   (m_qwords[j].m_word)[0]=='_' || 
				   (m_qwords[j].m_word)[0]=='.'))))
			{
				j--;
			}
			if ( j < 0 ) { 
				//log(LOG_LOGIC,"query: query: bad "
				//"engineer."); 
				j = 0; }
			// advance j to a non-punct word
			while (words.isPunct(j)) j++;

			// ignore all of these words then, 
			// they're part of field name
			long tlen = 0;
			for ( long k = j ; k <= i ; k++ )
				tlen += words.getWordLen(k);
			// set field name to the compound name if it is
			field     = words.getWord (j);
			fieldLen  = tlen;
			if ( j == i ) fieldSign = wordSign;
			else          fieldSign = m_qwords[j].m_wordSign;
			// debug msg
			//char ttt[128];
			//memcpy ( ttt , field , fieldLen );
			//ttt[fieldLen] = '\0';
			//log("field name = %s", ttt);
			// . is it recognized field name,like "title" or "url"?
			// . does it officially end in a colon? incl. in hash?
			bool hasColon;
			fieldCode = getFieldCode (field, fieldLen, &hasColon) ;
			// only url,link,site,ip and suburl field names will
			// end a colon, due to historical fuck up
			//if ( hasColon ){ 
			//	fieldLen++;
			//}
			// reassign alias fields
			//Why??? -p
			//if ( fieldCode == FIELD_TYPE ) {
			//	field = "type" ; fieldLen = 4; }

			// if so, it does NOT get its own QueryWord,
			// but its sign can be inherited by its members
			if ( fieldCode ) {
				for ( long k = j ; k <= i ; k++ )
					m_qwords[k].m_ignoreWord = 
						IGNORE_FIELDNAME;
				continue;
			}
		}

		// what quote chunk are we in? this is 0 if we're not in quotes
		if ( inQuotes ) qw->m_quoteStart = quoteStart ;
		else            qw->m_quoteStart = -1;
		qw->m_inQuotes = inQuotes;
		// ptr to field, if any
		qw->m_fieldCode = fieldCode;
		// if we are a punct word, see if we end in a sign that can
		// be applied to the next word, a non-punct word
		if ( words.isPunct(i) ) {
			wordSign = w[wlen-1];
			if ( wordSign != '-' && wordSign != '+') wordSign = 0; 
			if ( wlen>1 &&!is_wspace_a (w[wlen-2]) ) wordSign = 0;
			if ( i > 0 && wlen == 1                ) wordSign = 0;
		}
		// assign quoteSign to wordSign if we just got into quotes
		//if ( nq > 0 && inQuotes ) quoteSign = wordSign;
		// don't add any QueryWord for a punctuation word
		if ( words.isPunct(i) ) continue;
		// what is the sign of our term? +, -, *, ...
		char mysign;
		if      ( fieldCode ) mysign = fieldSign;
		else if ( inQuotes  ) mysign = quoteSign;
		else                  mysign = wordSign;
		// are we doing default AND?
		//if ( forcePlus && ! *mysign ) mysign = '+';
		// store the sign
		qw->m_wordSign = mysign;
		// what quote chunk are we in? this is 0 if we're not in quotes
		if ( inQuotes ) qw->m_quoteStart = quoteStart ;
		else            qw->m_quoteStart = -1;
		// if we're the first alnum in this quote and
		// the next word has a quote, then we're just a single word
		// in quotes which is silly, so undo it. But we should
		// still inherit any quoteSign, however. Be sure to also
		// set m_inQuotes to false so Matches.cpp::matchWord() works.
		if ( i == quoteStart ) { // + 1 ) {
			if ( i + 1 >= numWords || words.getNumQuotes(i+1)>0 ) {
				qw->m_quoteStart = -1;
				qw->m_inQuotes   = false;
			}
		}
		// . get prefix hash of collection name and field
		// . but first convert field to lower case
		unsigned long long ph;
		long fflen = fieldLen;
		if ( fflen > 62 ) fflen = 62;
		char ff[64];
		to_lower3_a ( field , fflen , ff );
		//unsigned longlongph=getPrefixHash(m_coll,m_collLen,ff,fflen);
		//ph=getPrefixHash(NULL,0,ff,fflen);
		ph = hash64 ( ff , fflen );
		// map "intitle" map to "title"
		if ( fieldCode == FIELD_TITLE )
			ph = hash64 ( "title", 5 );
		// make "suburl" map to "inurl"
		if ( fieldCode == FIELD_SUBURL )
			ph = hash64 ( "inurl", 5 );
		// ptr to field, if any

		qw->m_fieldCode = fieldCode;
		// prefix hash
		qw->m_prefixHash = ph;
		// set this flag
		if ( fieldCode == FIELD_LINKS    ) m_hasLinksOperator = true;
		if ( fieldCode == FIELD_SITELINK ) m_hasLinksOperator = true;
		// if we're hashing a url:, link:, site: or ip: term, 
		// then we need to hash ALL up to the first space
		if ( fieldCode == FIELD_URL  || 
		     fieldCode == FIELD_EXT  || 
		     fieldCode == FIELD_LINK ||
		     fieldCode == FIELD_ILINK||
		     fieldCode == FIELD_SITELINK||
		     fieldCode == FIELD_LINKS||
		     fieldCode == FIELD_SITE ||
		     fieldCode == FIELD_IP   ||
		     fieldCode == FIELD_ISCLEAN ||
		     fieldCode == FIELD_QUOTA ||
		     fieldCode == FIELD_GBAD  ) {
			// find first space -- that terminates the field value
			char *end = 
				(words.m_words[words.m_numWords-1] +
				 words.m_wordLens[words.m_numWords-1]);
			while ( w+wlen < end && 
				! is_wspace_utf8(w+wlen) ) wlen++;
			// ignore following words until we hit a space
			ignoreTilSpace = true;
			// the hash
			unsigned long long wid = hash64 ( w , wlen, 0LL );
			// should we have normalized before hashing?
			if ( fieldCode == FIELD_URL ||
			     fieldCode == FIELD_LINK ||
			     fieldCode == FIELD_ILINK ||
			     fieldCode == FIELD_SITELINK ||
			     fieldCode == FIELD_LINKS ||
			     fieldCode == FIELD_SITE ) {
				Url url;
				// do we add www?
				bool addwww = false;
				if ( fieldCode == FIELD_LINK ) addwww = true;
				if ( fieldCode == FIELD_ILINK) addwww = true;
				if ( fieldCode == FIELD_LINKS) addwww = true;
				if ( fieldCode == FIELD_URL  ) addwww = true;
				if ( fieldCode == FIELD_SITELINK) 
					addwww = true;
				url.set ( w , wlen , addwww );
				char *site    = url.getHost();
				long  siteLen = url.getHostLen();
				if (fieldCode == FIELD_SITELINK)
					wid = hash64 ( site , siteLen );
				else
					wid = hash64 ( url.getUrl(), 
						       url.getUrlLen() );
			}
			//qw->m_wordId      = g_indexdb.getTermId ( ph , wid );
			// like we do it in XmlDoc.cpp's hashString()
			if ( ph ) qw->m_wordId = hash64h ( wid , ph );
			else      qw->m_wordId = wid;
			qw->m_rawWordId   = 0LL; // only for highlighting?
			qw->m_phraseId    = 0LL;
			qw->m_rawPhraseId = 0LL;
			qw->m_opcode      = 0;
			// definitely not a query stop word
			qw->m_isQueryStopWord = false;
			// do not ignore the wordId
			qw->m_ignoreWord = 0;
			// override the word length
			//qw->m_wordLen = ulen * 2;
			// we are the first word?
			firstWord = false;
			// we're done with this one
			continue;
		}
		

		char opcode = 0;
		// if query is all in upper case and we're doing boolean 
		// DETECT, then assume not boolean
		if ( allUpper && boolFlag == 2 ) boolFlag = 0;
		// . having the UOR opcode does not mean we are boolean because
		//   we want to keep it fast.
		// . we need to set this opcode so the UOR logic in setQTerms()
		//   works, because it checks the m_opcode value. otherwise
		//   Msg20 won't think we are a boolean query and set boolFlag
		//   to 0 when setting the query for summary generation and
		//   will not recognize the UOR word as being an operator
		if ( wlen==3 && w[0]=='U' && w[1]=='O' && w[2]=='R' &&
		     ! firstWord ) {
			opcode = OP_UOR; m_hasUOR = true; goto skipin; }
		// . is this word a boolean operator?
		// . cannot be in quotes or field
		if ( boolFlag >= 1 && ! inQuotes && ! fieldCode ) {
			// are we an operator?
			if      ( ! firstWord && wlen==2 && 
				  w[0]=='O' && w[1]=='R') 
				opcode = OP_OR;
			else if ( ! firstWord && wlen==3 && 
				  w[0]=='A' && w[1]=='N' && w[2]=='D') 
				opcode = OP_AND;
			else if ( ! firstWord && wlen==3 && 
				  w[0]=='N' && w[1]=='O' && w[2]=='T') 
				opcode = OP_NOT;
			else if ( wlen==5 && w[0]=='L' && w[1]=='e' &&
				  w[2]=='F' && w[3]=='t' && w[4]=='P' )
				opcode = OP_LEFTPAREN;
			else if ( wlen==5 && w[0]=='R' && w[1]=='i' &&
				  w[2]=='G' && w[3]=='h' && w[4]=='P' )
				opcode = OP_RIGHTPAREN;
		skipin:
			// if we are detecting if query is boolean or not AND
			// if we are not an operator and have more than 1 cap 
			// char then the turn off boolean
			//if ( boolFlag==2 &&!opcode &&wlen>1&&is_upper(w[1])){
			//	// turn boolean stuff off
			//	boolFlag = 0;
			//	// start again from the top with NO boolean
			//	goto redo;
			//}
			// no pair across or even include any boolean op phrs
			if ( opcode ) {
				bits.m_bits[i] &= ~D_CAN_START_PHRASE;
				bits.m_bits[i] &= ~D_CAN_PAIR_ACROSS;
				bits.m_bits[i] &= ~D_CAN_BE_IN_PHRASE;
				qw->m_ignoreWord = IGNORE_BOOLOP;
				qw->m_opcode     = opcode;
				if ( opcode == OP_LEFTPAREN  ) continue;
				if ( opcode == OP_RIGHTPAREN ) continue;
				// if this is uncommented all of our operators
				// become actual query terms (mdw)
				if ( opcode == OP_UOR        ) continue;
				// if you just have ANDs and ()'s that does
				// not make you a boolean query! we are bool
				// by default!!
				if ( opcode == OP_AND        ) continue;
				m_isBoolean = true;
				continue;
			}
		}

		// . add single-word term id
		// . this is computed by hash64AsciiLower() 
		// . but only hash64Lower_a if _HASHWITHACCENTS_ is true
		unsigned long long wid = 0LL;
		if (fieldCode == FIELD_CHARSET){
			// find first space -- that terminates the field value
			char* end = 
				(words.m_words[words.m_numWords-1] +
				 words.m_wordLens[words.m_numWords-1]);
			while ( w+wlen<end && 
				! is_wspace_utf8(w+wlen) ) wlen++;
			// ignore following words until we hit a space
			ignoreTilSpace = true;
			// the hash
			//wid = hash64 ( uw , ulen, 0LL );
			// convert to enum value
			short csenum = get_iana_charset(w,wlen);
			// convert back to string
			char astr[128];
			long alen = sprintf(astr, "%d", csenum);
			wid = hash64(astr, alen, 0LL);
		}
		else{
 			wid = words.getWordId(i);
		}
		qw->m_rawWordId = wid;
		// we now have a first word already set
		firstWord = false;
		// . are we a QUERY stop word?
		// . NEVER count as stop word if it's in all CAPS and 
		//   not all letters in the whole query is NOT in all CAPS
		// . It's probably an acronym
		if ( words.isUpper(i) && words.getWordLen(i)>1 && ! allUpper ){
			qw->m_isQueryStopWord = false;
			qw->m_isStopWord      = false;
		}
		else {
			qw->m_isQueryStopWord =::isQueryStopWord (w,wlen,wid);
			// . BUT, if it is a single letter contraction thing
			// . ninad: make this == 1 if in utf8! TODO!! it is!
			if ( wlen == 1 && w[-1] == '\'' )
				qw->m_isQueryStopWord = true;
			qw->m_isStopWord =::isStopWord (w,wlen,wid);
		}
		// . do not count as query stop word if it is the last in query
		// . like the query: 'baby names that start with j'
		if ( i + 2 > numWords ) qw->m_isQueryStopWord = false;
		// hash the termid
		//qw->m_wordId = g_indexdb.getTermId ( ph , wid ); 
		// like we do it in XmlDoc.cpp's hashString()
		if ( ph ) qw->m_wordId = hash64 ( wid , ph );
		else      qw->m_wordId = wid;
		// do not ignore the word
		qw->m_ignoreWord = 0;
	}

	// pipe those that should be piped
	for ( long i = 0 ; i < pi ; i++ ) m_qwords[i].m_piped = true;
	if ( pi >= 0 ) m_piped = true;

	// . set m_leftConnected and m_rightConnected
	// . we are connected to the first non-punct word on our left 
	//   if we are separated by a small $ of defined punctuation
	// . see getIsConnection() for that definition
	// . this allows us to just lookup the phrase for things like
	//   "cd-rom" rather than lookup "cd" , "rom" and "cd-rom"
	// . skip if prev word is IGNORE_BOOLOP, IGNORE_FIELDNAME or
	//   IGNORE_DEFAULT
	// . we have to set outside the main loop above since we check
	//   the m_ignoreWord member of the i+2nd word
	for ( long i = 0 ; i < numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord ) continue;
		if ( i + 2 < numWords && ! m_qwords[i+2].m_ignoreWord&&
		     isConnection(words.getWord(i+1),words.getWordLen(i+1)) )
			qw->m_rightConnected = true;
		if ( i - 2 >= 0 && ! m_qwords[i-2].m_ignoreWord && 
		     isConnection(words.getWord(i-1),words.getWordLen(i-1) ) )
			qw->m_leftConnected  = true;
	}
	
	// now modify the Bits class before generating phrases
	for ( long i = 0 ; i < numWords ; i++ ) {
		// get default bits
		unsigned char b = bits.m_bits[i];
		// allow pairing across anything by default
		b |= D_CAN_PAIR_ACROSS;
		// get Query Word
		QueryWord *qw = &m_qwords[i];
		// . skip if part of a query weight operator
		// . cannot be in a phrase, or anything
		if ( qw->m_queryOp && !qw->m_opcode) { 
			b = D_CAN_PAIR_ACROSS;
		}
		// is this word a sequence of punctuation and spaces?
		else if ( words.isPunct(i) ) {
			// pair across ANY punct, even double spaces by default
			b |= D_CAN_PAIR_ACROSS;
			// but do not pair across anything with a quote in it
			if ( words.getNumQuotes(i) >0) b &= ~D_CAN_PAIR_ACROSS;
			// continue if we're in quotes
			else if ( qw->m_quoteStart >= 0 ) goto next;
			// continue if we're in a field
			else if ( qw->m_fieldCode > 0 ) goto next;
			// if guy on left is in field, do not pair across
			if ( i > 0 && m_qwords[i-1].m_fieldCode > 0 )
				b &= ~D_CAN_PAIR_ACROSS;
			// or if guy on right in field
			if ( i +1 < numWords && m_qwords[i+1].m_fieldCode > 0 )
				b &= ~D_CAN_PAIR_ACROSS;
			// do not pair across ".." when not in quotes/field
			char *w    = words.getWord   (i);
			long  wlen = words.getWordLen(i);
			for ( long j = 0 ; j < wlen-1 ; j++ ) {
				if ( w[j  ]!='.' ) continue;
				if ( w[j+1]!='.' ) continue;
				b &= ~D_CAN_PAIR_ACROSS;
				break;
			}
		}
		else {
			// . not even capped query stop words can start phrase
			// . 'Mice And Men' is just one phrase then
			// . TODO: "12345678 it was rainy" 
			//   ("it" should start a phrase)
			//if ( qw->m_isQueryStopWord) b &= ~D_CAN_START_PHRASE;
			if ( qw->m_isStopWord ) b &= ~D_CAN_START_PHRASE;
			// . first alnum word can start phrase.
			// . example: 'the tigers'
			if ( i <= 1 ) b |= D_CAN_START_PHRASE;
			// first alnum word in quotes can start phrase.
			if ( qw->m_quoteStart == i ) // + 1 ) 
				b |= D_CAN_START_PHRASE;
			// . right connected but not left can start phrase
			// . example: 'buy a-rom' , 'buy i-phone'
			if ( qw->m_rightConnected && ! qw->m_leftConnected )
				b |= D_CAN_START_PHRASE;
			// . no field names, bool operators, cruft in fields
			//   can be any part of a phrase
			// . no pair across any change of field code
			// . 'girl title:boy' --> no "girl title" phrase!
			if ( qw->m_ignoreWord ) { //== IGNORE_FIELDNAME ) {
				b &= ~D_CAN_PAIR_ACROSS;
				b &= ~D_CAN_BE_IN_PHRASE;
				b &= ~D_CAN_START_PHRASE;
			}
			// . no boolean ops
			// . 'this OR that' --> no "this OR that" phrase
			if ( qw->m_opcode ) {
				b &= ~D_CAN_PAIR_ACROSS;
				b &= ~D_CAN_BE_IN_PHRASE;
			}
			if ( qw->m_wordSign == '-' && qw->m_quoteStart < 0) {
				b &= ~D_CAN_PAIR_ACROSS;
				b &= ~D_CAN_BE_IN_PHRASE;
			}

		}
	next:
		// set it back all tweaked
		bits.m_bits[i] = b;
	}

	// . now since we may have prevented pairing across certain things
	//   we need to set D_CAN_START_PHRASE for stop words whose left
	//   punct word can no longer be paired across
	// . "dancing in the rain" is fun --> will include phrase "is fun".
	// . title:"is it right"? --> will include phrase "is it"
	for ( long i = 1 ; i < numWords ; i++ ) {
		// no punct, alnum only
		if ( words.isPunct(i) ) continue;
		// skip if not a stop word
		if ( ! bits.m_bits[i] & D_IS_STOPWORD ) continue;
		// continue if you can still pair across prev punct word
		if ( bits.m_bits[i-1] & D_CAN_PAIR_ACROSS ) continue;
		// otherwise, we can now start a phrase
		bits.m_bits[i] |= D_CAN_START_PHRASE;
	}

	// a bogus spam class, all words have 0 for their spam probability
	//Spam spam;
	//spam.reset ( words.getNumWords() );

	// make the phrases from the words and the tweaked Bits class
	//Phrases phrases;
	if ( ! phrases.set ( &words , 
			     &bits  , 
			     //NULL   ,
			     true   ,  // use stop words?
			     false  , // use stems?
			     TITLEREC_CURRENT_VERSION,
			     0 /*niceness*/))//disallows HUGE phrases
		return false;

	long long *wids = words.getWordIds();

	// do phrases stuff
	for ( long i = 0 ; i < numWords ; i++ ) {
		// get the ith QueryWord
		QueryWord *qw = &m_qwords[i];
		// if word is ignored because it is opcode, or whatever,
		// it cannot start a phrase
		// THIS IS BROKEN
		//if ( qw->m_queryOp && qw->m_opcode == OP_PIPE){
		//	for (long j = i-1;j>=0;j--){
		//		if (!m_qwords[j].m_phraseId) continue;
		//		m_qwords[j].m_ignorePhrase = IGNORE_BOOLOP;
		//		break;
		//	}
		//	
		//}
		if ( qw->m_ignoreWord ) continue;
		if ( qw->m_fieldCode && qw->m_quoteStart < 0) continue;
		// get the first word # to our left that starts a phrase
		// of which we are a member
		qw->m_leftPhraseStart = -1;
		//long long tmp;
		for ( long j = i - 1 ; j >= 0 ; j-- ) {
			//if ( ! bits.isIndexable(j)     ) continue;
			if ( ! bits.canPairAcross(j+1) ) break;
			//if ( ! bits.canStartPhrase(j)  ) continue;
			if ( ! wids[j] ) continue;
			// phrases.getNumWordsInPhrase()
			//if( j + phrases.getMaxWordsInPhrase(j,&tmp)<i) break;
			qw->m_leftPhraseStart = j;
			// we can't pair across alnum words now, we just want bigrams
			if ( wids[j] ) break;
			//break;
			// now we do bigrams so only allow two words even
			// if they are stop words
			break;
		}
		// . is this word in a quoted phrase?
		// . the whole phrase must be in the same set of quotes
		// . if we're in a left phrase, he must be in our quotes
		if ( qw->m_leftPhraseStart >= 0 &&
		     qw->m_quoteStart      >= 0 &&
		     qw->m_leftPhraseStart >= qw->m_quoteStart ) 
			qw->m_inQuotedPhrase = true;
		// if we start a phrase, ensure next guy is in our quote
		if ( ! qw->m_ignorePhrase && i+1 < numWords &&
		     m_qwords[i+1].m_quoteStart >= 0 &&
		     m_qwords[i+1].m_quoteStart <= i )
			qw->m_inQuotedPhrase = true;
		// are we the first word in the quote?
		if ( i-1>=0 && qw->m_quoteStart == i )
			qw->m_inQuotedPhrase = true;
		// ignore single words that are in a quoted phrase
		if ( ! keepAllSingles && qw->m_inQuotedPhrase ) 
			qw->m_ignoreWord = IGNORE_QUOTED;

		// . get phrase info for this term
		// . a pid (phraseId)of 0 indicates it does not start a phrase
		// . raw phrase termId
		//unsigned long long pid = phrases.getPhraseId(i);
		unsigned long long pid = 0LL;
		// nwp is a REGULAR WORD COUNT!!
		long nwp = 0;
		if ( qw->m_inQuotedPhrase )
			// keep at a bigram for now... i'm not sure if we
			// will be indexing trigrams
			nwp = phrases.getMinWordsInPhrase(i,(long long *)&pid);
		// just get a two-word phrase term if not in quotes
		else
			nwp = phrases.getMinWordsInPhrase(i,(long long *)&pid);
		// store it
		qw->m_rawPhraseId = pid;
		// does word #i start a phrase?
		if ( pid != 0 ) {
			unsigned long long ph = qw->m_prefixHash ;
			// store the phrase id with coll/prefix
			//qw->m_phraseId = g_indexdb.getTermId ( ph , pid );
			// like we do it in XmlDoc.cpp's hashString()
			if ( ph ) qw->m_phraseId = hash64 ( pid , ph );
			else      qw->m_phraseId = pid;
			// how many regular words long is the bigram?
			long plen2; phrases.getPhrase ( i , &plen2 ,2);
			// the trigram?
			long plen3; phrases.getPhrase ( i , &plen3 ,3);
			// get just the bigram for now
			qw->m_phraseLen = plen2;
			// do not ignore the phrase, it's valid
			qw->m_ignorePhrase = 0;
			// set our rightPhraseEnd point
			//qw->m_rightPhraseEnd = i + phrases.getNumWords(i);
			// store left and right raw word ids 
			//long nwp = phrases.getNumWordsInPhrase(i);
			qw->m_rightRawWordId = m_qwords[i+nwp-1].m_rawWordId;
		}


		// . phrase sign is inherited from word's sign if it's a minus
		// . word sign is inherited from field, quote or right before
		//   the word
		// . that is, all words in -"to be or not" will have a '-' sign
		// . phraseId may or may not be 0 at this point
		if ( qw->m_wordSign == '-' ) qw->m_phraseSign = '-';
		// . dist word signs to others in the same connected string
		// . use "-cd-rom x-box" w/ no connector in between
		// . test queries:
		// . +cd-rom +x-box
		// . -cd-rom +x-box
		// . -m-o-n
		// . who was the first   (was is a query stop word)
		// . www.xxx.com
		// . welcome to har.com
		// . hezekiah walker the love family affair ii live at radio 
		//   city music hall
		// . fotostudio +m-o-n-a-r-t
		// . fotostudio -m-o-n-a-r-t
		// . i'm home
		if ( qw->m_leftConnected && qw->m_leftPhraseStart >= 0 )
			qw->m_wordSign = m_qwords[i-2].m_wordSign;
		// . if we connected to the alnum word on our right then
		//   soft require the phrase (i.e. treat like a single term)
		// . example: cd-rom or www.xxx.com
		// . 'welcome to har.com' should get a '*' for "har.com" sign
		if ( qw->m_rightConnected ) {
			if ( qw->m_wordSign) qw->m_phraseSign = qw->m_wordSign;
			else                 qw->m_phraseSign = '*';
		}
		// . if we're in quotes then any phrase we have should be
		//   soft required (i.e. treated like a single term)
		// . we do not allow phrases in queries to pair across
		//   quotes. See where we tweak the Bits class above.
		if ( qw->m_quoteStart >= 0 ) {
			//if (qw->m_wordSign)qw->m_phraseSign = qw->m_wordSign;
			//else                 qw->m_phraseSign = '*';
			qw->m_phraseSign = '*';
		}

		// . if we are the last word in a phrase that consists of all
		//   PLAIN stop words then make the phrase have a '*'
		// . 'to be or not to be .. test' (cannot pair across "..")
		// . don't use QUERY stop words cuz of "who was the first?" qry
		if ( pid ) {
			long nw = phrases.getNumWordsInPhrase2(i);
			long j;
			for ( j = i ; j < i + nw ; j++ ) {
				// skip punct
				if ( words.isPunct(j)         ) continue;
				// break out if not a stop word
				if ( ! bits.isStopWord(j)     ) break;
				// break out if has a term sign
				if (   m_qwords[j].m_wordSign ) break;
			}
			// if everybody in phrase #i was a signless stopword
			// and the phrase was signless, make it have a '*' sign
			if ( j >= i + nw && m_qwords[i].m_phraseSign == '\0' ) 
				m_qwords[i].m_phraseSign = '*';
			// . if a constituent has a - sign, then the whole 
			//   phrase becomes negative, too
			// . fixes 'apple -computer' truncation problem
			for ( long j = i ; j < i + nw ; j++ )
				if ( m_qwords[j].m_wordSign == '-' )
					qw->m_phraseSign = '-';
		}

		// . ignore unsigned QUERY stop words that are not yet ignored 
		//   and are in unignored phrases
		// . 'who was the first taiwanese president' should not get 
		//   "who was" term sign changed to '*' because "was" is a
		//   QUERY stop word. So ignore singles query stop words
		//   in phrases now
		if ( //! keepAllSingles && 
		     (qw->m_isQueryStopWord && !m_isBoolean) &&
		     m_useQueryStopWords &&
		     ! qw->m_fieldCode &&
		     // fix 'the tigers'
		     //(qw->m_leftPhraseStart >= 0 || qw->m_phraseId > 0 ) && 
		     ! qw->m_wordSign && 
		     ! qw->m_ignoreWord )
			qw->m_ignoreWord = IGNORE_QSTOP;

		// . ignore word if connected to right or left alnum word
		// . we will be replaced by a phrase(s)
		// . do not worry about keepAllSingles because we turn
		//   this into a phrase below!
		// . if ( ! keepAllSingles &&
		if ( ( qw->m_leftConnected || qw->m_rightConnected ) )
			qw->m_ignoreWord = IGNORE_CONNECTED;
		// . ignore and/or between quoted phrases, save user from 
		//   themselves (they meant AND/OR)
		if ( ! keepAllSingles && qw->m_isQueryStopWord &&
		     ! qw->m_fieldCode &&
		     m_useQueryStopWords &&
		     ! qw->m_phraseId && ! qw->m_inQuotes &&
		     ((qw->m_wordId == 255176654160863LL) ||
		      (qw->m_wordId ==  46196171999655LL))        )
			qw->m_ignoreWord = IGNORE_QSTOP;
		// . ignore repeated single words and phrases
		// . look at the old termIds for this, too
		// . should ignore 2nd 'time' in 'time after time' then
		// . but boolean queries often need to repeat terms

		// . NEW - words much be same sign and not in different
		// . quoted phrases to be ignored -partap
		m_hasDupWords = false;
		if ( ! m_isBoolean && !qw->m_ignoreWord ) {
			for ( long j = 0 ; j < i ; j++ ) {
				if ( m_qwords[j].m_ignoreWord   ) continue;
				if ( m_qwords[j].m_wordId == qw->m_wordId &&
				     m_qwords[j].m_wordSign ==qw->m_wordSign &&
				     (!keepAllSingles || 
				      (m_qwords[j].m_quoteStart 
				       == qw->m_quoteStart))){
					qw->m_ignoreWord = IGNORE_REPEAT;
					m_hasDupWords = true;
				}
			}
		}
		if ( ! m_isBoolean && !qw->m_ignorePhrase ) {
			// ignore repeated phrases too!
			for ( long j = 0 ; j < i ; j++ ) {
				if ( m_qwords[j].m_ignorePhrase ) continue;
				if ( m_qwords[j].m_phraseId == qw->m_phraseId &&
				     m_qwords[j].m_phraseSign 
				     == qw->m_phraseSign)
					qw->m_ignorePhrase = IGNORE_REPEAT;
			}
		}
	}

	// treat strongly connected phrases like cd-rom and 3.2.0.3 as being
	// in quotes for the most part, therefore, set m_quoteStart for them
	long j;
	long qs = -1;
	for ( j = 0 ; j < numWords ; j++ ) {
		// skip all but strongly connected words
		if ( m_qwords[j].m_ignoreWord != IGNORE_CONNECTED &&
		     // must also be non punct word OR a space
		     ( !words.isPunct(j) || words.m_words[j][0] == ' ' ) ) {
			// break the "quote", if any
			qs = -1; continue; }
		// if he is punctuation and qs is -1, skip him,
		// punctuation words can no longer start a quote
		if ( words.isPunct(j) && qs == -1 ) continue;
		// uningore him if we should
		if ( keepAllSingles ) m_qwords[j].m_ignoreWord = 0;
		// if already in quotes, don't bother!
		if ( m_qwords[j].m_quoteStart >= 0 ) continue;
		// remember him
		if ( qs == -1 ) qs = j;
		// he starts the phrase
		m_qwords[j].m_quoteStart = qs;
		// force him into a quoted phrase
		m_qwords[j].m_inQuotes   = true;
		//m_qwords[j].m_inQuotedPhrase = true;
	}

	// . if we only have one quoted query then force its sign to be '+'
	// . '"get the phrase" the' --> +"get the phrase" (last the is ignored)
	// . "time enough for love" --> +"time enough" +"enough for love"
	// . if all unignored words are in the same set of quotes then change
	//   all '*' (soft-required) phrase signs to '+'
	for ( j= 0 ; j < numWords ; j++ ) {
		if ( words.isPunct(j)) continue;
		if ( m_qwords[j].m_quoteStart < 0 ) break;
		if ( m_qwords[j].m_ignoreWord ) continue;
		if ( j < 2 ) continue;
		if ( m_qwords[j-2].m_quoteStart != m_qwords[j].m_quoteStart )
			break;
	}
	if ( j >= numWords ) {
		for ( j= 0 ; j < numWords ; j++ ) {
			if ( m_qwords[j].m_phraseSign == '*' )
				m_qwords[j].m_phraseSign = '+';
		}
	}
		
	// . force a plus on any site: or ip: query terms
	// . also disable site clustering if we have either of these terms
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord ) continue;
		if ( qw->m_wordSign   ) continue;
		if ( qw->m_fieldCode != FIELD_SITE &&
		     qw->m_fieldCode != FIELD_IP     ) continue;
		qw->m_wordSign = '+';
	}

	// now check phrase terms. if you do a search in quotes like 
	// "directions and nearby" it will now generate two phrases:
	// "directions and nearby" and "and nearby", so stop "and nearby"
	/*
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignorePhrase ) continue;
		if ( ! qw->m_phraseId   ) continue;
		// skip if we start this phrase
		if ( qw->m_quoteStart == i ) continue;
		// . skip if are not a phrase stop word that is paired across
		// . not now, we support 3,4 and 5 word phrases...
		//if ( ! qw->m_isStopWord ) continue;
		// however, we some quoted phrases are more than 5 words
		// TODO: fix this!!!
		// ok, nuke this term otherwise
		qw->m_ignorePhrase = IGNORE_DEFAULT;
	}
	*/

	// . if one or more of a phrase's constituent terms exceeded 
	//   term #MAX_QUERY_TERMS then we should also soft require that phrase
	// . fixes 'hezekiah walker the love family affair ii live at 
	//          radio city music hall'
	// . how many non-ignored phrases?
	long count = 0;
	for ( long i = 0 ; i < m_numWords ; i++ ) {	
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignorePhrase ) continue;
		if ( ! qw->m_phraseId   ) continue;
		count++;
	}
	for ( long i = 0 ; i < numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		// count non-ignored words
		if ( qw->m_ignoreWord ) continue;
		// if under limit, continue
		if ( count++ < MAX_QUERY_TERMS ) continue;
		// . otherwise, ignore
		// . if we set this for our UOR'ed terms from SearchInput.cpp's
		//   UOR'ed facebook interests then it causes us to get no results!
		//   so make sure that MAX_QUERY_TERMS is big enough with respect to
		//   the opCount in SearchInput.cpp
		qw->m_ignoreWord = IGNORE_BREECH;
		// left phrase should get a '*'
		long left = qw->m_leftPhraseStart;
		if ( left >= 0 && ! m_qwords[left].m_phraseSign )
			m_qwords[left].m_phraseSign = '*';
		// our phrase should get a '*'
		if ( qw->m_phraseId && ! qw->m_phraseSign )
			qw->m_phraseSign = '*';
	}

	// . fix the 'x -50a' query so it returns results
	// . how many non-negative, non-ignored words/phrases do we have?
	count = 0;
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord      ) continue;
		if ( qw->m_wordSign == '-' ) continue;
		count++;
	}
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignorePhrase      ) continue;
		if ( qw->m_phraseSign == '-' ) continue;
		if ( qw->m_phraseId == 0LL   ) continue;
		count++;
	}
	// if everybody is ignored or negative UNignore first query stop word
	if ( count == 0 ) {
		for ( long i = 0 ; i < m_numWords ; i++ ) {
			QueryWord *qw = &m_qwords[i];
			if ( qw->m_ignoreWord != IGNORE_QSTOP ) continue;
			qw->m_ignoreWord = 0;
			count++;
			break;
		}
	}

	// . count ignored WORDS for logging stats
	// . do not IGNORE_DEFAULT though, that doesn't really count
	//m_numIgnored = 0;
	//for ( long i = 0 ; i < m_numWords ; i++ ) {
	//	if ( ! m_qwords[i].m_ignoreWord                 ) continue;
	//	if ( m_qwords[i].m_ignoreWord == IGNORE_DEFAULT ) continue;
	//	m_numIgnored++;
	//}

	quoteStart = -1;
	long quoteEnd = -1;
	// set m_quoteENd
	for ( long i = m_numWords - 1 ; i >= 0 ; i-- ) {
		// get ith word
		QueryWord *qw = &m_qwords[i];
		// skip if ignored
		if ( qw->m_ignoreWord ) continue;
		// skip if not in quotes
		if ( qw->m_quoteStart < 0 ) continue;
		// if match previous guy...
		if ( qw->m_quoteStart == quoteStart ) {
			// inherit the end
			qw->m_quoteEnd = quoteEnd;
			// all done
			continue;
		}
		// ok, we are the end then
		quoteEnd   = i;
		quoteStart = qw->m_quoteStart;
	}		


	long wkid = 0;
	long upTo = -1;
	long wk_start;
	long wk_nwk;
	//long long *wids = words.getWordIds();
	//
	// set the wiki phrase ids
	//
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		// get ith word
		QueryWord *qw = &m_qwords[i];
		// in a phrase from before?
		if ( i < upTo ) {
			qw->m_wikiPhraseId = wkid;
			qw->m_wikiPhraseStart = wk_start;
			qw->m_numWordsInWikiPhrase = wk_nwk;
			continue;
		}
		// assume none
		qw->m_wikiPhraseId = 0;
		// skip if punct
		if ( ! wids[i] ) continue;
		// get word
		long nwk ;
		nwk = g_wiki.getNumWordsInWikiPhrase ( i , &words );
		// bail if none
		if ( nwk <= 1 ) continue;
		// save these too
		wk_start = i;
		wk_nwk = nwk;
		// inc it
		wkid++;
		// store it
		qw->m_wikiPhraseId = wkid;
		qw->m_wikiPhraseStart = wk_start;
		qw->m_numWordsInWikiPhrase = wk_nwk;
		// set loop parm
		upTo = i + nwk;
	}

	// all done
	return true;
}

// return -1 if does not exist in query, otherwise return the query word num
long Query::getWordNum ( long long wordId ) { 
	// skip if punct or whatever
	if ( wordId == 0LL || wordId == -1LL ) return -1;
	for ( long i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		// the non-raw word id includes a hash with "0", which
		// signifies an empty field term
		if ( qw->m_rawWordId == wordId ) return i;
	}
	// otherwise, not found
	return -1;
}

//static TermTable  s_table;
static HashTableX s_table;
static bool       s_isInitialized = false;

// 3rd field = m_hasColon
struct QueryField g_fields[] = {
	{"url", FIELD_URL, true,"Match the exact url. Example: url:www.gigablast.com/addurl.htm"},
	{"ext", FIELD_EXT, true,"Match the url extension. Example: ext:htm or ext:mpeg to find urls ending in .htm or .mpeg respectively."},
	{"link", FIELD_LINK, true,"Match pages that link to the given url. Example: link:www.gigablast.com will return all pages linking to the www.gigablast.com page."},
	{"links", FIELD_LINKS, true,"Same as link:."},
	{"ilink", FIELD_ILINK, true,"Similar to above."},
	{"sitelink", FIELD_SITELINK, true,"Matches all pages that link to the given site. Example:sitelink:www.gigablast.com matches all pages that link to some page on the www.gigablast.com site."},
	{"site", FIELD_SITE, true,"Matches all pages from the given site. Example: site:www.gigablast.com will return all the pages on the gigablast site"},
	{"coll", FIELD_COLL, true,"Not sure if this works."},
	{"ip", FIELD_IP, true,"Matches all pages with the given ip. Example:1.2.3.4 will match all pages whose urls have that IP address."},
	{"inurl", FIELD_SUBURL, true,"Matches all pages that have the given terms in the url. Example inurl:water will match all pages whose url has the word water in it, but the word must be delineated by punctuation."},
	{"suburl", FIELD_SUBURL, true,"Same as inurl."},
	{"intitle", FIELD_TITLE, false,"Matches all pages that have pages that have the given term in their title. Example: title:web returns all pages that have the word web in their title."},
	{"title", FIELD_TITLE, false,"Same as intitle:"},
	{"isclean", FIELD_ISCLEAN, true,"Matches all pages that are deemed non-offensive and safe for children."},
	{"gbrss", FIELD_GBRSS, true,"Matches all pages that are rss feeds."},
	//{"gbruleset",FIELD_GBRULESET, true,"Obsolete."},
	{"type", FIELD_TYPE, false,"Matches all pages of the specified file type. Example: type:pdf will match pdf documents, regardless of their file extension."},
	{"filetype", FIELD_TYPE, false,"Same as type:"},
	{"gbtag*", FIELD_TAG, false,"Matches all pages whose tag named * have the specified value. Example: gbtagingoogle:1 matches all pages that have a value of 1 for their ingoogle tag in tagdb."},
	{"zip", FIELD_ZIP, false,"Matches all pages that have the specified zip code in their meta zip code tag. Not to be used with events."},
	{"zipcode", FIELD_ZIP, false,"Same as zip:"},
	//{"range", FIELD_RANGE, false,""}, // obsolete, datedb replaced
	{"charset", FIELD_CHARSET, false,"Matches all pages in the given character set."},
	{"urlhash",FIELD_URLHASH, false,""},
	{"urlhashdiv10",FIELD_URLHASHDIV10, false,""},
	{"urlhashdiv100",FIELD_URLHASHDIV100, false,""},
	{"gblang",FIELD_GBLANG,false,"Matches all pages in the given language. Examples: gblang:en gblang:fr gblang:de"},
	{"gbquality",FIELD_GBQUALITY,true,""},
	{"gblinktextin",FIELD_LINKTEXTIN,true,""},
	{"gblinktextout",FIELD_LINKTEXTOUT,true,""},
	{"gbkeyword",FIELD_KEYWORD,true,""},
	{"gbcharset", FIELD_CHARSET, false,""},
	{"gbpathdepth", FIELD_GBOTHER, false,"the path depth of the url"},
	{"gbhasfilename", FIELD_GBOTHER, false,""},
	{"gbiscgi", FIELD_GBOTHER, false,""},
	{"gbhasext", FIELD_GBOTHER, false,""},
	{"gbsubmiturl", FIELD_GBOTHER, false,""},

	{"qdom", FIELD_QUOTA, false,""},
	{"qhost", FIELD_QUOTA, false,""},
	{"gbtagvector", FIELD_GBTAGVECTOR, false,""},

	{"gbgigabitvector", FIELD_GBGIGABITVECTOR, false,""},
	{"gbsamplevector", FIELD_GBSAMPLEVECTOR, false,""},
	{"gbcountry",FIELD_GBCOUNTRY,false,""},
	{"gbad",FIELD_GBAD,false,""},


	{"gbsectionhash"            ,FIELD_GBSECTIONHASH,false,"Internal use only."},


	{"gbduphash"                ,FIELD_GBOTHER,false,"Internal use only."},
	{"gbsitetemplate"           ,FIELD_GBOTHER,false,"Internal use only."},
	{"gboutlinkedtitle"         ,FIELD_GBOTHER,false,"gboutlinkedtitle:0 and gboutlinkedtitle:1 matches events whose title is not in and in a hyperlink, respectively."},
	{"gbisaggregator"           ,FIELD_GBOTHER,false,"gbisaggregator:0|1 depending on if the event came from an event aggregator website, like eviesays.com."},
	{"gbdeduped"                ,FIELD_GBOTHER,false,""},

	{"gbinjected", FIELD_GBOTHER,false,"Was the event injected?."},

	//{"gbstartrange",FIELD_GBSTARTRANGE,false,""},
	//{"gbendrange",FIELD_GBENDRANGE,false,""},

	{"gbpermalink",FIELD_GBPERMALINK,false,""},
	{"gbcsenum",FIELD_GBCSENUM,false,""},
	{"gbdocid",FIELD_GBDOCID,false,"restrict results to this docid"}
	
};

void resetQuery ( ) {
	s_table.reset();
}



long getNumFieldCodes ( ) { 
	return (long)sizeof(g_fields) / (long)sizeof(QueryField); 
}

static bool initFieldTable(){

	if ( ! s_isInitialized ) {
		// set up the hash table
		if ( ! s_table.set ( 8 , 4 , 255,NULL,0,false,0,"qryfldtbl" ) )
			return log("build: Could not init table of "
					   "query fields.");
		// now add in all the stop words
		long n = getNumFieldCodes();
		for ( long i = 0 ; i < n ; i++ ) {
			long long h = hash64b ( g_fields[i].text );
			// store the entity index in the hash table as score
			if ( ! s_table.addTerm ( &h, i+1 ) ) return false;
		}
		s_isInitialized = true;
	} 
	return true;
}


char getFieldCode ( char *s , long len , bool *hasColon ) {
	// default
	if (hasColon) *hasColon = false;

	if (!initFieldTable()) return FIELD_GENERIC;
	long long h = hash64Lower_a(s, len );//>> 1) ;
	long i = (long) s_table.getScore ( &h ) ;
	if (i==0) return FIELD_GENERIC;
	//if (hasColon) *hasColon = g_fields[i-1].hasColon ; 
	return g_fields[i-1].field;
}

char getFieldCode2 ( char *s , long len , bool *hasColon ) {
	// default
	if (hasColon) *hasColon = false;

	if (!initFieldTable()) return FIELD_GENERIC;
	// subtract the colon for matching
	if ( s[len-1]==':') len--;
	long long h = hash64 (s , len , 0LL );
	long i = (long) s_table.getScore ( &h ) ;
	if (i==0) return FIELD_GENERIC;
	//if (hasColon) *hasColon = g_fields[i-1].hasColon ; 
	return g_fields[i-1].field;
}

char getFieldCode3 ( long long h64 ) {
	if (!initFieldTable()) return FIELD_GENERIC;
	// subtract the colon for matching
	long i = (long) s_table.getScore ( &h64 ) ;
	if (i==0) return FIELD_GENERIC;
	//if (hasColon) *hasColon = g_fields[i-1].hasColon ; 
	return g_fields[i-1].field;
}


// guaranteed to be punctuation
bool Query::isConnection ( char *s , long len ) {
	if ( len == 1 ) {
		switch (*s) {
			// . only allow apostrophe if it's NOT a 's
			// . so contractions are ok, and names too
		case '\'': 
			// no, i think we should require it. google seems to,
			// and msn and yahoo do. 'john's room -"john's" gives
			// no result son yahoo and msn.
			return true;
			if ( *(s+1) !='s' ) return true;
			return false;
		case ':': return true;
		case '-': return true;
		case '.': return true;
		case '@': return true;
		case '#': return true;
		case '/': return true;
		case '_': return true;
		case '&': return true;
		case '=': return true;
		case '\\': return true;
		default: return false;
		}
		return false;
	}
	//if ( len == 3 && s[0]==' ' && s[1]=='&' && s[2]==' ' ) return true;
	if ( len == 3 && s[0]==':' && s[1]=='/' && s[2]=='/' ) return true;
	return false;
}

void Query::printQueryTerms(){
	for (long i=0;i<m_numTerms;i++){
		char c = getTermSign(i);
		char tt[512];
		long ttlen = getTermLen(i);
		if ( ttlen > 254 ) ttlen = 254;
		if ( ttlen < 0   ) ttlen = 0;
		// this is utf8
		memcpy ( tt , getTerm(i) , ttlen );
		tt[ttlen]='\0';
		if ( c == '\0' ) c = ' ';
		logf(LOG_DEBUG, "query: Query Term #%ld "
		     "phr=%li termId=%llu rawTermId=%llu"
		     " sign=%c "
		     "ebit=0x%0llx "
		     "impBits=0x%0llx "
		     "hc=%li "
		     "component=%li "
		     "otermLen=%li "
		     "term=%s ",
		     i,
		     (long)isPhrase (i) ,
		     getTermId      (i) ,
		     getRawTermId   (i) ,
		     c ,
		     (long long)m_qterms[i].m_explicitBit  ,
		     (long long)m_qterms[i].m_implicitBits ,
		     (long) m_qterms[i].m_hardCount ,
		     m_componentCodes[i],
		     getTermLen(i),
		     tt                        );
	}
	
}


////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
//////////   ONLY BOOLEAN STUFF BELOW HERE  /////////////
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
bool  Query::testBoolean(qvec_t bits, qvec_t bitmask){
	if (!m_isBoolean) return false;
	Expression *e = &m_expressions [ 0 ];
	// find top-level expression
	while (e->m_parent && e != e->m_parent) e = e->m_parent;
	return e->isTruth(bits, bitmask);
	
}
void  Query::printBooleanTree(){
	if (!m_isBoolean) return;
	Expression *e = &m_expressions [ 0 ];
	// find top-level expression
	while (e->m_parent && e != e->m_parent) e = e->m_parent;
	SafeBuf sbuf(1024);
	e->print(&sbuf);
	logf(LOG_DEBUG, "query: Boolean Query: %s", sbuf.getBufStart());	
}

// . also sets the m_underNOT member of each QueryTerm, too!!
// . returns false and sets g_errno on error, true otherwise
bool Query::setBooleanOperands ( ) {
	// we're done if we're not boolean
	if ( ! m_isBoolean ) return true;

	if ( m_truncated ) {
		g_errno = ETOOMANYOPERANDS;
		return log("query: Maximum number of bool operands "
			   "exceeded (%ld).",m_numTerms);
	}

	// alloc the mem if we need to (mdw left off here) 
	//long need = (m_numWords/3) * sizeof(Expression);
	// illegitmate bool expressions breech the buffer
	long need = (m_numWords) * sizeof(Expression);
	// sanity check
	if ( m_expressions || m_expressionsAllocSize ) { 
		char *xx = NULL; *xx = 0; }
	// point m_qwords to our generic buffer if it will fit
	if ( m_gnext + need < m_gbuf + GBUF_SIZE ) {
		m_expressions = (Expression *)m_gnext;
		m_gnext += need;
	}
	// otherwise, we must allocate memory for it
	else {
		m_expressions = (Expression *)mmalloc ( need , "Query3" );
		if ( ! m_expressions ) return log("query: Could not allocate "
						  "expressions for query.");
		m_expressionsAllocSize = need;
	}

	// otherwise, we need to set the boolean Expression classes now
	// so we can determine which terms are UNDER the influence of
	// NOT operators so IndexReadInfo.cpp can read in the WHOLE termlist
	// for those terms. (like it would if they had a '-' m_termSign)
	Expression *e = &m_expressions [ 0 ];
	m_numExpressions = 1;
	// . set the expression recursively
	// . just setting this will not set the m_hasNOT members of each 
	//   QueryTerm
	long status = e->set ( 0           , // first word #
				m_numWords  , // last  word #
				0           , // parser position
				this        , // array of QueryWords
				0              ,// level
				NULL, NULL,  // parent, leftchild
			       false ,  // has NOT?
			       false ); // under NOT?
	if ( status < 0 ) {
		g_errno = ETOOMANYOPERANDS;
		return log("query: Maximum number of bool operands "
			   "(%li) exceeded.",(long)MAX_OPERANDS);
	}
	while (e->m_parent) {
		if (e == e->m_parent) {
			g_errno = EBADREQUEST;
			return log(LOG_WARN, "query: expression is own parent: "
				   "%s", m_orig);
		}
		e = e->m_parent;
	}

	//log(LOG_DEBUG, "query: set %li operands",
	//    m_numOperands);
	if (g_conf.m_logDebugQuery) {
		SafeBuf sbuf(1024);
		e->print(&sbuf);
		log(LOG_DEBUG, "query: Boolean Query: %s", sbuf.getBufStart());
	}

	// . get all the terms that are UNDER a NOT operator in some fashion
	// . these bits are 1-1 with m_qterms[]
	qvec_t notBits = e->getNOTBits( false );
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
		if ( m_qterms[i].m_explicitBit & notBits )
			m_qterms[i].m_underNOT = true;
		else
			m_qterms[i].m_underNOT = false;
	}
	return true;
}

// . returns -1 on bad query error
// . returns word AFTER the last word in our operand
long Operand::set ( long a , long b , QueryWord *qwords , long level ,
		    bool underNOT ) {
	// clear these
	m_termBits         = 0;
	m_hasNOT           = false;

	//m_hardRequiredBits = 0;
	// . parse out the operands and OR in their term bits
	// . the boy AND girl --> (the AND boy) AND girl
	// . "the boy toy" AND girl --> "the boy" AND "boy toy" AND girl
	// . cd-rom AND buy --> "cd-rom" AND buy
	// . phraseSign will not be 0 if its important (in quotes, cd-rom,...)
	for ( long i = a ; i < b ; i++ ) {
		// get the QUERY word
		QueryWord *qw = &qwords[i];
		// set the parenthetical level of the word
		qw->m_level = level;
		// set this
		qw->m_underNOT = underNOT;
		// skip punct
		if ( ! qw->isAlphaWord() ) {
			// if it is a parens, bail!
			if ( qw->m_opcode == OP_LEFTPAREN ) return i ;
			if ( qw->m_opcode == OP_RIGHTPAREN ) return i ;
			// otherwise, skip this punct and get next word
			else continue;
		}
		// bail if op code, return PUNCT word # before it
		if ( qw->m_opcode ) return i ;

		
		if ( qw->m_wordSign == '-' || qw->m_phraseSign == '-'){
			if (i == a) {
				m_hasNOT = true;
			}
			else {
				if (!m_hasNOT) return i;
			}
			
		}
		else if (i>a && m_hasNOT) return i;


		// . does it have an unsigned phrase? or in phrase term bits
		// . might have a phrase that's not a QueryTerm because
		//   query is too long
		if ( qw->m_phraseId && qw->m_queryPhraseTerm &&
		     qw->m_phraseSign ) {
			qvec_t e =qw->m_queryPhraseTerm->m_explicitBit;
			//if (qw->m_phraseSign == '+') m_hardRequiredBits |= e;
			m_termBits |= e;
		}
		// why would it be ignored? oh... if like cd-rom or in quotes
		if ( qw->m_ignoreWord ) continue;
		// . OR in the word term bits
		// . might be a word that's not a QueryTerm because
		//   query is too long
		if ( qw->m_queryWordTerm ) {
			qvec_t e = qw->m_queryWordTerm->m_explicitBit;
			//if (qw->m_phraseSign == '+') m_hardRequiredBits |= e;
			m_termBits |= e;
		}
	}
	return b;
}

// . returns -1 on bad query error
// . returns next word to parse (after expression) on success
// . "*globalNumOperands" is how many expressions/operands are being used
//   in the global "expressions" and "operands" array

// . new: organize query into sum of products normal form, ie:
// . (a) OR (b AND c AND d) OR (e AND f)

unsigned char precedence[] = {
	0, // term
	4, // OR
	3, // AND
	2, // NOT
	1, // LEFTP
	1, // RIGHTP
	3, // UOR
	5, // PIPE
}; 

long Expression::set (long start, 
		       long end, 
		       long pos, // current parsing position
		       class Query      *q,
		       long              level, 
		       class Expression *parent, 
		       class Expression *leftChild,
		       bool hasNOT ,
		       bool underNOT ) {
	m_start = start;
	m_end = end;
	m_opcode = 0;
	m_operand = NULL;
	m_numChildren = 0;
	m_hasNOT = hasNOT;
	m_parent = parent;
	uint8_t curOp = 0;

	QueryWord  *qwords        =  q->m_qwords;
	Expression *o_expressions =  q->m_expressions;
	Operand    *o_operands    =  q->m_operands;
	long       *o_numOperands = &q->m_numOperands;
	long       *o_numExpressions = &q->m_numExpressions;
	long maxExpressions       =  q->m_numWords;
		

	// Lets really try to catch this
	if (m_parent == this) {
		//log(LOG_WARN, "query: Warning, setting expression "
		//    "parent to self");
		char *xx = NULL; *xx = 0;
	}

	//set initial args
	if (leftChild) {
		leftChild->m_parent = this;
		m_children[0] = leftChild;
		m_numChildren = 1;
	}
	hasNOT = false;
	for ( long i=pos ; i<end ; i++ ){
		QueryWord * qw = &qwords[i];
		// set this
		qw->m_underNOT = underNOT;
		// set leaf node
		if (!qw->m_opcode && qw->isAlphaWord()){
			if (i > m_start) goto setChildExpr;
			// if we maxxed out, error out
			if ( *o_numOperands >= MAX_OPERANDS ) return -1;
			Operand *op = &o_operands [ *o_numOperands ];
			*o_numOperands = *o_numOperands + 1;
			// . return ptr to next word for us to parse
			// . subtract once since for loop will inc it
			i = op->set ( i , end , qwords , level , underNOT );
			if ( i < 0 ) return -1;
			m_operand = op;
			goto endExpr;
		}
		if (qw->m_opcode == OP_NOT){
			hasNOT = !hasNOT;
			underNOT = hasNOT;
			continue;
		}
		else if (qw->m_opcode == OP_LEFTPAREN){
			if (i == m_start) i++;
			goto setChildExpr;			
		}
		else if (qw->m_opcode == OP_RIGHTPAREN){
			goto endExpr;
		}
		else if (qw->m_opcode) {
			int delta = 0;
			curOp = qw->m_opcode;
			if (m_numChildren == 1)
				m_opcode = curOp;

			if (m_numChildren > 1 && curOp != m_opcode) {

			  delta = (int)precedence[curOp] -
					(int)precedence[m_opcode];
			}
			
			if (delta > 0){
				goto endExpr;
			}
		        if (delta < 0){
				// set a subexpression conataining the 
				// last operand we found as the first 
				goto setChildExpr2;
			}
		}
		continue;
	endExpr:
		//log(LOG_DEBUG, "query: set Expr [%ld, %ld), opcode: %d",
		//    a, i, curOp);
		// if we've matched parens, go to next word
		// but if we have an extra right paren, don't crash
		if (qw->m_opcode == OP_RIGHTPAREN &&
		    (qwords[m_start].m_opcode == OP_LEFTPAREN ||
		     m_start == 0)) 
			i++;

		m_end = i;
		// We have an extra open paren
		if (qwords[m_start].m_opcode == OP_LEFTPAREN &&
		    qw->m_opcode != OP_RIGHTPAREN) 
			goto setParentExpr;
		// we are top-level expr, but there is more to parse
		if (!m_parent && i < end-1) 
			goto setParentExpr;
		// just return
		return i;
		// add a parent expression with this one as the left child 
	setParentExpr:
		{
			if ( *o_numExpressions >= maxExpressions ) return -1;
			//if (qw->m_opcode == OP_RIGHTPAREN) i++;
			Expression *e = &o_expressions[*o_numExpressions];
			*o_numExpressions = *o_numExpressions + 1;
			i = e->set ( m_start , end ,i, q , 
				     level+1, 
				     m_parent,
				     this, 
				     false ,
				     underNOT ) ;
			return i;
		}

		// add a child expression
	setChildExpr:
		{
			if ( *o_numExpressions >= maxExpressions ) return -1;

			Expression *e = &o_expressions[*o_numExpressions];
			*o_numExpressions = *o_numExpressions + 1;
			i = e->set ( i , end , i, q , 
				     level+1, 
				     this, NULL, hasNOT , 
				     underNOT ) -1;
			if ( i < 0 ) return -1;
			
			// trim needless parens 
			while (e->m_numChildren == 1) {
				hasNOT = e->m_hasNOT;
				e = e->m_children[0];
				if (hasNOT) e->m_hasNOT = ! e->m_hasNOT;
			}
			hasNOT = false;
			//cull empty expressions
			if (e->m_numChildren < 1 &&
			    e->m_operand == NULL) continue;

			if (m_numChildren >= MAX_OPERANDS) return -1;
			// add good expressions
			m_children [ m_numChildren] = e;
			m_numChildren++;
			if (m_numChildren > 1 && m_opcode == 0)
				m_opcode = OP_AND; // default AND
			continue;
		}

		// we need to make the last operand we passed 
		// be the first operand of a subexpression
	setChildExpr2:
		{
			// remove the last expression from our list
			Expression *ce = m_children[m_numChildren-1];

			m_numChildren--;


			if ( *o_numExpressions >= maxExpressions ) return -1;

			Expression *e = &o_expressions[*o_numExpressions];
			*o_numExpressions = *o_numExpressions + 1;
			i = e->set ( ce->m_start , end , i, q , 
				     level+1, 
				     this, ce, 
				     false , 
				     underNOT ) -1;
			ce->m_parent = e;
			if ( i < 0 ) return -1;

			if (m_numChildren >= MAX_OPERANDS) return -1;
			m_children [ m_numChildren ] = e;

			hasNOT = false;
			m_numChildren++;
			continue;
		}
	}
	return end;
}


// . "bits" are 1-1 with the query terms in Query::m_qterms[] array
bool Expression::isTruth ( qvec_t bits, qvec_t mask ) {
	//bool op1 = false ; // set to false so compiler shuts up
	//bool op2 ;
	//bool accumulator = false;
	//bool hadOR       = false;
	bool result = false;

	// leaf node
	if (m_operand){
		result = m_operand->isTruth(bits, mask);
		// handle masked terms better.. don't apply NOT operator
		if (!(m_operand->m_termBits & mask)) return true;
	}
	else if (m_numChildren == 1){
		result = m_children[0]->isTruth(bits, mask);
	}
	else if (m_opcode == OP_OR || m_opcode == OP_UOR) {
		for ( long i=0 ; i<m_numChildren ; i++ ) {
			result = result || m_children[i]->isTruth(bits, mask);
			if (result) goto done;
		}
	}
	else if (m_opcode == OP_AND || m_opcode == OP_PIPE){
		result = true;
		for (long i = 0 ; i < m_numChildren ; i++ ) {
			result = result && m_children[i]->isTruth(bits, mask);
			if (!result) goto done;
		}
	}

done :
	if (m_hasNOT) return !result;
	else return result;
}

// . "bits" are 1-1 with the query terms in Query::m_qterms[] array
// . hasNOT is true if there's a NOT just to the left of this WHOLE expressions
//   ourside the parens
qvec_t Expression::getNOTBits ( bool hasNOT ) {
	qvec_t notBits = 0;
// 	for ( long i = 0 ; i < m_numOperands ; i++ ) {
// 		// get value of the ith operand, be it plain or an expression
// 		if ( m_operands[i]  ) {
// 			if ( m_hasNOT[i] || hasNOT ) 
// 				notBits |= m_operands[i]->m_termBits;
// 		}
// 		else
// 			notBits |= m_expressions[i]->getNOTBits (m_hasNOT[i]);
// 	}
	// success, all operand pairs were true
	return notBits;
}

// print boolean expression for debug purposes
void Expression::print(SafeBuf *sbuf) {
	if (m_hasNOT) sbuf->safePrintf("NOT ");
	if (m_operand){
		m_operand->print(sbuf);
		return;
	}
	sbuf->safePrintf("(");
	for (long i=0; i < m_numChildren ; i++) {
		m_children[i]->print(sbuf);
		
		if (i >= m_numChildren-1) break;
		switch (m_opcode) {
		case OP_OR:   sbuf->safePrintf(" OR "  ); break;
		case OP_AND:  sbuf->safePrintf(" AND " ); break;
		case OP_UOR:  sbuf->safePrintf(" UOR " ); break;
		case OP_PIPE: sbuf->safePrintf(" PIPE "); break;
		}
	}
	sbuf->safePrintf(")");

}

void Operand::print(SafeBuf *sbuf) {
// 	long shift = 0;
// 	while (m_termBits >> shift) shift++;
// 	sbuf->safePrintf("%i", 1<<(shift-1));
	if (m_hasNOT) sbuf->safePrintf("NOT 0x%lx", (long)m_termBits);
	else sbuf->safePrintf("0x%lx", (long)m_termBits);
}

// if any one query term is split, msg3a has to split the query
bool Query::isSplit() {
	for(long i = 0; i < m_numTerms; i++) 
		if(m_qterms[i].isSplit()) return true;
	return false;
}

bool QueryTerm::isSplit() {
	if(!m_fieldCode) return true;
	if(m_fieldCode == FIELD_QUOTA)           return false;
	if(m_fieldCode == FIELD_GBTAGVECTOR)     return false;
	if(m_fieldCode == FIELD_GBGIGABITVECTOR) return false;
	if(m_fieldCode == FIELD_GBSAMPLEVECTOR)  return false;
	if(m_fieldCode == FIELD_GBSECTIONHASH)  return false;
	return true;
}



// hash of all the query terms
long long Query::getQueryHash() {
	long long qh = 0LL;
	for ( long i = 0 ; i < m_numTerms ; i++ ) 
		qh = hash64 ( m_termIds[i] , qh );
	return qh;
}
