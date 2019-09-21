#include "gb-include.h"

#include <limits>

#include "Query.h"
//#include "Indexdb.h" // g_indexdb.getTruncationLimit() g_indexdb.getTermId()
#include "Words.h"
#include "Bits.h"
#include "Phrases.h"
#include "Url.h"
#include "Clusterdb.h" // g_clusterdb.getNumGlobalRecs()
#include "StopWords.h" // isQueryStopWord()
#include "Sections.h"
#include "Msg1.h"
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
	m_numWords = 0;
	//m_expressions = NULL;
	m_qwordsAllocSize      = 0;
	//m_expressionsAllocSize = 0;
	m_qwords               = NULL;
	m_numTerms = 0;
	m_containingParent = NULL;
	m_st0Ptr = NULL;
	// we have to manually call this because Query::constructor()
	// might have been called explicitly
	//for ( int32_t i = 0 ; i < MAX_QUERY_TERMS ; i++ )
	//	m_qterms[i].constructor();
	//m_expressions          = NULL;
	reset ( );
}

void Query::destructor ( ) {
	reset();
}

Query::~Query ( ) {
	reset ( );
}

void Query::reset ( ) {

	// if Query::constructor() was called explicitly then we have to
	// call destructors explicitly as well...
	// essentially call QueryTerm::reset() on each query term
	for ( long i = 0 ; i < m_numTerms ; i++ ) {
	 	// get it
		QueryTerm *qt = &m_qterms[i];
		HashTableX *ht = &qt->m_facetHashTable;
		// debug note
		// log("results: free fhtqt of %"PTRFMT" for q=%"PTRFMT 
		//     " st0=%"PTRFMT,
		//     (PTRTYPE)ht->m_buf,(PTRTYPE)this,(PTRTYPE)m_st0Ptr);
		ht->reset();
		qt->m_facetIndexBuf.purge();
	}

	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		qw->destructor();
	}

	m_stackBuf.purge();
	m_qterms = NULL;

	m_sb.purge();
	m_osb.purge();
	m_docIdRestriction = 0LL;
	m_groupThatHasDocId = NULL;
	//m_bufLen      = 0;
	m_origLen     = 0;
	m_numWords    = 0;
	//m_numOperands = 0;
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
	//if ( m_expressionsAllocSize )
	//	mfree ( m_expressions , m_expressionsAllocSize , "Query3" );
	if ( m_qwordsAllocSize )
		mfree ( m_qwords      , m_qwordsAllocSize      , "Query4" );
	//m_expressionsAllocSize = 0;
	m_qwordsAllocSize      = 0;
	m_qwords               = NULL;
	//m_expressions          = NULL;
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
		   //int32_t  queryLen     ,
		   //char *coll         , 
		   //int32_t  collLen      ,
		   //char  boolFlag     ,
		   //bool  keepAllSingles ,
		   // need language for doing synonyms
		   uint8_t  langId ,
		   char     queryExpansion ,
		   bool     useQueryStopWords ,
		   int32_t  maxQueryTerms  ) {

	m_langId = langId;
	m_useQueryStopWords = useQueryStopWords;
	// fix summary rerank and highlighting.
	bool keepAllSingles = true;

	m_maxQueryTerms = maxQueryTerms;

	// assume  boolean auto-detect.
	char boolFlag = 2;

	// come back up here if we changed our boolean minds
	// top:

	reset();

	if ( ! query ) return true;

	// set to 256 for synonyms?
	//m_maxQueryTerms = 256;
	m_queryExpansion = queryExpansion;

	int32_t queryLen = gbstrlen(query);
	// override this to 32 at least for now
	//if ( m_maxQueryTerms < 32 ) m_maxQueryTerms = 32;
	// save collection info
	//m_coll    = coll;
	//m_collLen = collLen;
	// truncate query if too big
	if ( queryLen >= ABS_MAX_QUERY_LEN ) {
		log("query: Query length of %"INT32" must be "
		    "less than %"INT32". "
		    "Truncating.",queryLen,(int32_t)ABS_MAX_QUERY_LEN);
		queryLen = ABS_MAX_QUERY_LEN - 1;
		m_truncated = true;
	}
	// save original query
	m_osb.setBuf ( m_otmpBuf , 128 , 0 , false );
	m_osb.setLabel ("oqbuf" );
	m_osb.reserve ( queryLen + 1 );
	m_osb.safeMemcpy ( query , queryLen );
	m_osb.nullTerm ();
	
	//m_origLen = queryLen;
	//gbmemcpy ( m_orig , query , queryLen );
	//m_orig [ m_origLen ] = '\0';

	m_orig = m_osb.getBufStart();
	m_origLen = m_osb.getLength();

	log(LOG_DEBUG, "query: set called = %s", m_orig);

	char *q = query;
	// see if it should be boolean...
	for ( int32_t i = 0 ; i < queryLen ; i++ ) {
		// but if bool flag is 0 that means it is NOT boolean!
		// it must be one for autodetection. so do not autodetect
		// unless this is 2.
		if ( boolFlag != 2 ) break;
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

	// if we did not set the flag to 1 set it to 0. force to non-bool
	if ( boolFlag == 2 ) boolFlag = 0;
	
	// come back up here if we find no bool operators but had ()'s
	// top:
	// reset anything that was allocated... in case we're being 
	// called from below... m_qwords may have been allocated in call
	// to setQWords() below
	// NO! this resets m_origLen to 0!!! not to mention other member vars
	// that were set somewhere above!!! i moved top: label above!
	//reset();

	// reserve some space, guessing how much we'd need
	m_sb.setBuf(m_tmpBuf3,128,0,false);
	m_sb.setLabel("qrystk");
	int32_t need = queryLen * 2 + 32;
	if ( ! m_sb.reserve ( need ) ) 
		return false;

	// convenience ptr
	//char *p    = m_buf;
	//char *pend = m_buf + MAX_QUERY_LEN;
	bool inQuotesFlag = false;
	// . copy query into m_buf
	// . translate ( and ) to special query operators so Words class
	//   can parse them as their own word to make parsing bool queries ez
	//   for parsing out the boolean operators in setBitScoresBoolean()
	for ( int32_t i = 0 ; i < queryLen ; i++ ) {

		// gotta count quotes! we ignore operators in quotes
		// so you can search for diffbotUri:"article|0|123456"
		if ( query[i] == '\"' ) inQuotesFlag = !inQuotesFlag;

		if ( inQuotesFlag ) {
			//*p = query [i];
			//p++;
			m_sb.pushChar(query[i]);
			continue;
		}

		// dst buf must be big enough
		// if ( p + 8 >= pend ) {
		// 	g_errno = EBUFTOOSMALL;
		// 	return log(LOG_LOGIC,"query: query: query too big.");
		// }
		// translate ( and )
		if ( boolFlag == 1 && query[i] == '(' ) {
			//gbmemcpy ( p , " LeFtP " , 7 ); p += 7;
			m_sb.safeMemcpy ( " LeFtP " , 7 );
			continue;
		}
		if ( boolFlag == 1 && query[i] == ')' ) {
			//gbmemcpy ( p , " RiGhP " , 7 ); p += 7;
			m_sb.safeMemcpy ( " RiGhP " , 7 );
			continue;
		}
		if ( query[i] == '|' ) {
			//gbmemcpy ( p , " PiiPE " , 7 ); p += 7;
			m_sb.safeMemcpy ( " PiiPE " , 7 );
			continue;
		}
		// translate [#a] [#r] [#ap] [#rp] [] [p] to operators
		if ( query[i] == '[' && is_digit(query[i+1])) {
			int32_t j = i+2;
			int32_t val = atol ( &query[i+1] );
			while ( is_digit(query[j]) ) j++;
			char c = query[j];
			if ( (c == 'a' || c == 'r') && query[j+1]==']' ) {
				//sprintf ( p , " LeFtB %"INT32" %c RiGhB ",
				m_sb.safePrintf(" LeFtB %"INT32" %c RiGhB ",
					  val,c);
				//p += gbstrlen(p);
				i = j + 1;
				continue;
			}
			else if ( (c == 'a' || c == 'r') && 
				  query[j+1]=='p' && query[j+2]==']') {
				//sprintf ( p , " LeFtB %"INT32" %cp RiGhB ",
				m_sb.safePrintf(" LeFtB %"INT32" %cp RiGhB ",
				val,c);
				//p += gbstrlen(p);
				i = j + 2;
				continue;
			}
		}
		if ( query[i] == '[' && query[i+1] == ']' ) {
			//sprintf ( p , " LeFtB RiGhB ");
			//p += gbstrlen(p);
			m_sb.safePrintf ( " LeFtB RiGhB ");
			i = i + 1;
			continue;
		}
		if ( query[i] == '[' && query[i+1] == 'p' && query[i+2]==']') {
			//sprintf ( p , " LeFtB RiGhB ");
			//p += gbstrlen(p);
			m_sb.safePrintf ( " LeFtB RiGhB ");
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
		// *p = query [i];
		// p++;
		m_sb.pushChar ( query[i] );
	}
	// NULL terminate
	//*p = '\0';
	m_sb.nullTerm();
	// debug statement
	//log(LOG_DEBUG,"Query: Got new query=%s",tempBuf);
	//printf("query: query: Got new query=%s\n",tempBuf);

	// set length
	//m_bufLen = p - m_buf;

	//m_buf = m_sb.getBufStart();
	//m_bufLen = m_sb.length();

	Words words;
	Phrases phrases;

	// set m_qwords[] array from m_buf
	if ( ! setQWords ( boolFlag , keepAllSingles , words , phrases ) ) 
		return false;
	//log(LOG_DEBUG, "Query: QWords set");
	// did we have any boolean operators
	/*
	char found  = 0;
	char parens = 0;
	if ( boolFlag == 1 ) {
		for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
			char *w    = m_qwords[i].m_word;
			int32_t  wlen = m_qwords[i].m_wordLen;
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
	*/

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
	//if ( m_isBoolean )
	//	if ( ! setBooleanOperands() ) return false;

	// disable stuff for site:, ip: and url: queries
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
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
		else if ( qw->m_fieldCode == FIELD_SUBURL2 )
			m_hasSubUrlField = true;
	}

	// set m_docIdRestriction if a term is gbdocid:
	for ( int32_t i = 0 ; i < m_numTerms && ! m_isBoolean ; i++ ) {
		// get it
		QueryTerm *qt = &m_qterms[i];
		// gbdocid:?
		if ( qt->m_fieldCode != FIELD_GBDOCID ) continue;
		// get docid
		char *ds = m_qterms[i].m_term + 8;
		m_docIdRestriction = atoll(ds);
		//uint32_t gid;
		uint32_t shard = getShardNumFromDocId(m_docIdRestriction);
		//gid = g_hostdb.getGroupIdFromDocId(m_docIdRestriction);
		//m_groupThatHasDocId = g_hostdb.getGroup(gid);
		m_groupThatHasDocId = g_hostdb.getShard ( shard );
		break;
	}

	// . keep it simple for now
	// . we limit to MAX_EXRESSIONS to like 10 now i guess
	if ( m_isBoolean ) {
		m_numExpressions = 1;
		if ( ! m_expressions[0].addExpression ( 0 , 
						      m_numWords ,
						      this , // Query
						      0 ) ) // level
			// return false with g_errno set on error
			return false;
	}


	// . if it is not truncated, no need to use hard counts
	// . comment this line and the next one out for testing hard counts
	if ( ! m_truncated ) return true;
	// if got truncated AND under the HARD max, nothing we can do, it
	// got cut off due to m_maxQueryTerms limit in Parms.cpp
	if ( m_numTerms < (int32_t)MAX_EXPLICIT_BITS ) return true;
	// if they just hit the admin's ceiling, there's nothing we can do
	if ( m_numTerms >= m_maxQueryTerms ) return true;
	// a temp log message
	log(LOG_DEBUG,"query: Encountered %"INT32" query terms.",m_numTerms);

	// otherwise, we're below m_maxQueryTerms BUT above MAX_QUERY_TERMS
	// so we can use hard counts to get more power...

	// . use the hard count for excessive query terms to save explicit bits
	// . just look for operands on the first level that are not OR'ed
	char redo = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
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
		for ( int32_t j=i+1 ; m_isBoolean && j<m_numWords; j++ ) {
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

	// . set the query terms again if we have a int32_t query
	// . if QueryWords has m_hardCount set, ensure the explicit bit is 0
	// . non-quoted phrases that contain a "required" single word should
	//   themselves have 0 for their implicit bits, BUT 0x8000 for their
	//   explicit bit
	if ( ! setQTerms ( words , phrases ) )
		return false;


	// a temp log message
	//log(LOG_DEBUG,"query: Compressed to %"INT32" query terms, %"INT32" hard. "
	//    "(nt=%"INT32")",
	//     m_numExplicitBits,m_numTerms-m_numExplicitBits,m_numTerms);

	//if ( ! m_isBoolean ) return true;

	// free cuz it was already set
	//if ( m_expressionsAllocSize ) 
	//	mfree(m_expressions,m_expressionsAllocSize , "Query" );
	//m_expressionsAllocSize = 0;
	//m_expressions = NULL;

	// also set the boolean stuff again too!
	//if ( ! setBooleanOperands() ) return false;

	return true;
}

/*
// count how many so PageResults will know if he should offer
// a default OR alternative search if no more results for
// the default AND (rat=1)
int32_t Query::getNumRequired ( ) {
	if ( m_numRequired >= 0 ) return m_numRequired;
	m_numRequired = 0;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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

	//int32_t shift = 0;
	// . set m_qptrs/m_qtermIds/m_qbits 
	// . use one bit position for each phraseId and wordId
	// . first set phrases
	int32_t n = 0;
	// what is the max value for "shift"?
	int32_t max = (int32_t)MAX_EXPLICIT_BITS;
	if ( max > m_maxQueryTerms ) max = m_maxQueryTerms;

	// count phrases first for allocating
	int32_t nqt = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw  = &m_qwords[i];
		// skip if ignored... mdw...
		if ( ! qw->m_phraseId ) continue;
		if (   qw->m_ignorePhrase ) continue; // could be a repeat
		// none if weight is absolute zero
		if ( qw->m_userWeightPhrase == 0   && 
		     qw->m_userTypePhrase   == 'a'  ) continue;
		nqt++;
	}
	// count single terms
	for ( int32_t i = 0 ; i < m_numWords; i++ ) {
		QueryWord *qw  = &m_qwords[i];
 		if ( qw->m_ignoreWord && 
 		     qw->m_ignoreWord != IGNORE_QSTOP) continue;
		// ignore if in quotes and part of phrase, watch out
		// for things like "word", a single word in quotes.
		if ( qw->m_quoteStart >= 0 && qw->m_phraseId ) continue;
		// if we are not start of quote and NOT in a phrase we
		// must be the tailing word i guess.
		// fixes '"john smith" -"bob dole"' from having
		// smith and dole as query terms.
		if ( qw->m_quoteStart >= 0 && qw->m_quoteStart != i )
			continue;
		// ignore if weight is absolute zero
		if ( qw->m_userWeight == 0   && 
		     qw->m_userType   == 'a'  ) continue;
		nqt++;
	}
	// thirdly, count synonyms
	Synonyms syn;
	int32_t sn = 0;
	if ( m_queryExpansion ) sn = m_numWords;
	int64_t to = hash64n("to",0LL);
	for ( int32_t i = 0 ; i < sn ; i++ ) {
		// get query word
		QueryWord *qw  = &m_qwords[i];
		// skip if in quotes, we will not get synonyms for it
		if ( qw->m_inQuotes ) continue;
		// skip if has plus sign in front
		if ( qw->m_wordSign == '+' ) continue;
		// not '-' either i guess
		if ( qw->m_wordSign == '-' ) continue;
		// no url: stuff, maybe only title
		if ( qw->m_fieldCode &&
		     qw->m_fieldCode != FIELD_TITLE &&
		     qw->m_fieldCode != FIELD_GENERIC )
			continue;
		// skip if ignored like a stopword (stop to->too)
		//if ( qw->m_ignoreWord ) continue;
		// ignore title: etc. words, they are field names
		if ( qw->m_ignoreWord == IGNORE_FIELDNAME ) continue;
		// ignore boolean operators
		if ( qw->m_ignoreWord ) continue;// IGNORE_BOOLOP
		// no, hurts 'Greencastle IN economic development'
		if ( qw->m_wordId == to ) continue;
		// single letters...
		if ( qw->m_wordLen == 1 ) continue;
		// set the synonyms for this word
		char tmpBuf [ TMPSYNBUFSIZE ];
		int32_t naids = syn.getSynonyms ( &words ,
					       i ,
						  // language of the query.
						  // 0 means unknown. if this
						  // is 0 we sample synonyms
						  // from all languages.
						  m_langId , 
					       tmpBuf ,
					       0 ); // m_niceness );
		// if no synonyms, all done
		if ( naids <= 0 ) continue;
		nqt += naids;
	}

	m_numTermsUntruncated = nqt;

	if ( nqt > m_maxQueryTerms ) nqt = m_maxQueryTerms;

	// allocate the stack buf
	if ( nqt ) {
		int32_t need = nqt * sizeof(QueryTerm) ;
		if ( ! m_stackBuf.reserve ( need ) )
			return false;
		m_stackBuf.setLabel("stkbuf3");
		char *pp = m_stackBuf.getBufStart();
		m_qterms = (QueryTerm *)pp;
		pp += sizeof(QueryTerm);
		if ( pp > m_stackBuf.getBufEnd() ) { char *xx=NULL;*xx=0; }
	}

	// call constructor on each one here
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		QueryTerm *qt = &m_qterms[i];
		qt->constructor();
	}


	// count phrase terms
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		// break out if no more explicit bits!
		/*
		if ( shift >= max ) {
			log("query: Query1 has more than %"INT32" unique terms. "
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
		if ( n >= ABS_MAX_QUERY_TERMS ) {
			log("query: lost query phrase terms to max term "
			    "limit of %"INT32"",(int32_t)ABS_MAX_QUERY_TERMS );
			break;
		}
		if ( n >= m_maxQueryTerms ) {
			log("query: lost query phrase terms to max term cr "
			    "limit of %"INT32"",(int32_t)m_maxQueryTerms);
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
		qt->m_term      = NULL;
		qt->m_termLen   = 0;
		qt->m_langIdBitsValid = false;
		qt->m_langIdBits      = 0;
		// assume not a repeat of another query term (set below)
		qt->m_repeat    = false;
		// stop word? no, we're a phrase term
		qt->m_isQueryStopWord = false;
		// change in both places
		qt->m_termId    = qw->m_phraseId & TERMID_MASK;
		//m_termIds[n]    = qw->m_phraseId & TERMID_MASK;
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
			//m_termSigns[n] = '\0';
		}
		// if not boolean, ensure to change signs in both places
		else {
			qt->m_termSign  = qw->m_phraseSign;
			//m_termSigns[n]  = qw->m_phraseSign;
		}
		//
		// INSERT UOR LOGIC HERE
		//
// 		int32_t pw = i-1;
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
		//qt->m_underNOT = false;
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
		//gbmemcpy ( tmp , qt->m_term , qt->m_termLen );
		//tmp [ qt->m_termLen ] = 0;
		//logf(LOG_DEBUG,"got term %s (%"INT32")",tmp,qt->m_termLen);
		// otherwise, add it
		n++;
	}

	// now if we have enough room, do the singles
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		// break out if no more explicit bits!
		/*
		if ( shift >= max ) {
			logf(LOG_DEBUG,
			     "query: Query2 has more than %"INT32" unique terms. "
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
		//if ( qw->m_quoteStart >= 0 ) continue;
		// ignore if in quotes and part of phrase, watch out
		// for things like "word", a single word in quotes.
		if ( qw->m_quoteStart >= 0 && qw->m_phraseId ) continue;

		// if we are not start of quote and NOT in a phrase we
		// must be the tailing word i guess.
		// fixes '"john smith" -"bob dole"' from having
		// smith and dole as query terms.
		if ( qw->m_quoteStart >= 0 && qw->m_quoteStart != i )
			continue;

		// ignore if weight is absolute zero
		if ( qw->m_userWeight == 0   && 
		     qw->m_userType   == 'a'  ) continue;

		// stop breach
		if ( n >= ABS_MAX_QUERY_TERMS ) {
			log("query: lost query terms to max term "
			    "limit of %"INT32"",(int32_t)ABS_MAX_QUERY_TERMS );
			break;
		}
		if ( n >= m_maxQueryTerms ) {
			log("query: lost query terms to max term cr "
			    "limit of %"INT32"",(int32_t)m_maxQueryTerms);
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
		//m_termIds[n]    = qw->m_wordId & TERMID_MASK;
		qt->m_rawTermId = qw->m_rawWordId;
		// assume explicit bit is 0
		qt->m_explicitBit = 0;
		qt->m_matchesExplicitBits = 0;
		//log(LOG_DEBUG, "Setting query phrase term id %d: %lld raw: %lld", n, m_termIds[n], qt->m_rawTermId);
		// boolean queries are not allowed term signs
		if ( m_isBoolean ) {
			qt->m_termSign = '\0';
			//m_termSigns[n] = '\0';
			// boolean fix for "health OR +sports" because
			// the + there means exact word match, no synonyms.
			if ( qw->m_wordSign == '+' ) {
				qt->m_termSign  = qw->m_wordSign;
				//m_termSigns[n]  = qw->m_wordSign;
			}
		}
		// if not boolean, ensure to change signs in both places
		else {
			qt->m_termSign  = qw->m_wordSign;
			//m_termSigns[n]  = qw->m_wordSign;
		}
		// get previous text word
		//int32_t pw = i - 2;
 		int32_t pw = i-1;
// 		// . back up until word that contains quote if in a quoted 
// 		//   phrase
// 		// . UOR can only support two word phrases really...
 		if (m_qwords[i].m_quoteStart >= 0)
			pw = m_qwords[i].m_quoteStart ;
		if ( pw > 0 ) pw--;

 		// back two more if field
		int32_t fieldStart=-1;
		int32_t fieldLen=0;

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


		// skip if it is punct. fixes queries like
		// "(this OR that)" from including '(' or from including
		// a space.
		if ( fieldStart >-1 &&
		     m_qwords[fieldStart].m_isPunct && 
		     fieldStart+1<m_numWords )
			fieldStart++;

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
			// fix for query
			// text:""  foo bar   ""
			if ( pw-1 < i ) {
				log("query: bad query %s",m_orig);
				g_errno = EMALFORMEDQUERY;
				return false;
			}
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
// 		int32_t pn = qw->m_leftPhraseStart;
// 		// convert word to its phrase QueryTerm ptr, if any
// 		QueryTerm *tt = NULL;
// 		if ( pn >= 0 ) tt = m_qwords[pn].m_queryPhraseTerm;
// 		if ( tt      ) tt->m_implicitBits |= qt->m_explicitBit;
// 		// . there might be some phrase term that actually contains
// 		//   the same word as we are, but a different occurence
// 		// . like '"knowledge management" AND NOT management' query
// 		for ( int32_t j = 0 ; j < i ; j++ ) {
// 			// must be our same wordId (same word, different occ.)
// 			QueryWord *qw2 = &m_qwords[j];
// 			if ( qw2->m_wordId != qw->m_wordId ) continue;
// 			// get first word in the phrase that jth word is in
// 			int32_t pn2 = qw2->m_leftPhraseStart;
// 			if ( pn2 < 0 ) continue;
// 			// he implies us!
// 			QueryTerm *tt2 = m_qwords[pn2].m_queryPhraseTerm;
// 			if ( tt2 ) tt2->m_implicitBits |= qt->m_explicitBit;
// 			break;
// 		}
		// assume not under a NOT bool op
		//qt->m_underNOT = false;
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
		//gbmemcpy ( tmp , qt->m_term , qt->m_termLen );
		//tmp [ qt->m_termLen ] = 0;
		//logf(LOG_DEBUG,"got term %s (%"INT32")",tmp,qt->m_termLen);
		n++;
	}
	



	// now handle the explicit bits
	// moved out of separate phrase and singleton loops
	// for phrase UOR support

	/*
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {

		// break out if no more explicit bits!
// 		if ( shift >= max ) {
// 			log("query: Query has more than %"INT32" unique terms. "
// 			    "Truncating.",max);
// 			m_truncated = true;
// 			break; 
// 		}

		int32_t pw;
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
			int32_t quoteStart = m_qwords[i].m_quoteStart;
			for (int32_t j=quoteStart;j<m_numWords;j++){
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
			int32_t quoteStart = m_qwords[pw].m_quoteStart;
			for (int32_t j=quoteStart;j<m_numWords;j++){
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
// 		int32_t pn = qw->m_leftPhraseStart;
// 		// convert word to its phrase QueryTerm ptr, if any
// 		QueryTerm *tt = NULL;
// 		if ( pn >= 0 ) tt = m_qwords[pn].m_queryPhraseTerm;
// 		if ( tt      ) tt->m_implicitBits |= qt->m_explicitBit;
// 		// . there might be some phrase term that actually contains
// 		//   the same word as we are, but a different occurence
// 		// . like '"knowledge management" AND NOT management' query
// 		for ( int32_t j = 0 ; j < i ; j++ ) {
// 			// must be our same wordId (same word, different occ.)
// 			//QueryWord *qw2 = m_qterms[j].m_qword;
// 			QueryWord *qw2 = &m_qwords[j];
// 			if ( qw2->m_wordId != qw->m_wordId ) continue;
// 			// get first word in the phrase that jth word is in
// 			int32_t pn2 = qw2->m_leftPhraseStart;
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
	for ( int32_t i = 0; i < n ; i++ ){
 		// break out if no more explicit bits!
 		if ( shift >= max ) {
 			logf(LOG_DEBUG,
			    "query: Query4 has more than %"INT32" unique terms. "
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
		for (int32_t j=0;j<i;j++){
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
	for ( int32_t i = 0; i < n ; i++ ){
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
	for (int32_t i = 0; i < m_numWords ; i++ ){
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
		int32_t pn = qw->m_leftPhraseStart;
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
		for ( int32_t j = 0 ; j < m_numWords ; j++ ) {
			// must be our same wordId (same word, different occ.)
			QueryWord *qw2 = &m_qwords[j];
			if ( qw2->m_wordId != qw->m_wordId ) continue;
			// get first word in the phrase that jth word is in
			int32_t pn2 = qw2->m_leftPhraseStart;
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
	for (int32_t i = 0; i < m_numTerms; i++) {
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
		for ( int32_t k = m_synTerm ; k < m_numTerms ; k++ ) {
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
	// loop over all words in query and process its synonyms list
	//if ( m_langId != langUnknown && m_queryExpansion ) 
	// if lang is "xx" unknown we still do synonyms it just does
	// a loop over all languages starting with english
	// if ( m_queryExpansion ) 
	// 	sn = m_numWords;

	//int64_t to = hash64n("to",0LL);

	for ( int32_t i = 0 ; i < sn ; i++ ) {
		// get query word
		QueryWord *qw  = &m_qwords[i];
		// skip if in quotes, we will not get synonyms for it
		if ( qw->m_inQuotes ) continue;
		// skip if has plus sign in front
		if ( qw->m_wordSign == '+' ) continue;
		// not '-' either i guess
		if ( qw->m_wordSign == '-' ) continue;
		// no url: stuff, maybe only title
		if ( qw->m_fieldCode &&
		     qw->m_fieldCode != FIELD_TITLE &&
		     qw->m_fieldCode != FIELD_GENERIC )
			continue;
		// skip if ignored like a stopword (stop to->too)
		//if ( qw->m_ignoreWord ) continue;
		// ignore title: etc. words, they are field names
		if ( qw->m_ignoreWord == IGNORE_FIELDNAME ) continue;
		// ignore boolean operators
		if ( qw->m_ignoreWord ) continue;// IGNORE_BOOLOP
		// no, hurts 'Greencastle IN economic development'
		if ( qw->m_wordId == to ) continue;
		// single letters...
		if ( qw->m_wordLen == 1 ) continue;
		// set the synonyms for this word
		char tmpBuf [ TMPSYNBUFSIZE ];
		int32_t naids = syn.getSynonyms ( &words ,
					       i ,
						  // language of the query.
						  // 0 means unknown. if this
						  // is 0 we sample synonyms
						  // from all languages.
						  m_langId , 
					       tmpBuf ,
					       0 ); // m_niceness );
		// if no synonyms, all done
		if ( naids <= 0 ) continue;
		// sanity
		if ( naids > MAX_SYNS ) { char *xx=NULL;*xx=0; }
		// now make the buffer to hold them for us
		qw->m_synWordBuf.setLabel("qswbuf");
		qw->m_synWordBuf.safeMemcpy ( &syn.m_synWordBuf );
		// get the term for this word
		QueryTerm *origTerm = qw->m_queryWordTerm;
		// loop over synonyms for word #i now
		for ( int32_t j = 0 ; j < naids ; j++ ) {
			// stop breach
			if ( n >= ABS_MAX_QUERY_TERMS ) {
				log("query: lost synonyms due to max term "
				    "limit of %"INT32"",
				    (int32_t)ABS_MAX_QUERY_TERMS );
				break;
			}
			// this happens for 'da da da'
			if ( ! origTerm ) continue;

			if ( n >= m_maxQueryTerms ) {
				log("query: lost synonyms due to max cr term "
				    "limit of %"INT32"",
				    (int32_t)m_maxQueryTerms);
				break;
			}

			// add that query term
			QueryTerm *qt   = &m_qterms[n];
			qt->m_qword     = qw; // NULL;
			qt->m_piped     = qw->m_piped;
			qt->m_isPhrase  = false ;
			qt->m_isUORed   = false;
			qt->m_UORedTerm = NULL;
			qt->m_langIdBits = 0;
			// synonym of this term...
			qt->m_synonymOf = origTerm;
			// nuke this crap since it was done above and we
			// missed out!
			qt->m_inPhrase           = 0;
			qt->m_rightPhraseTermNum = -1;
			qt->m_leftPhraseTermNum  = -1;
			qt->m_rightPhraseTerm    = NULL;
			qt->m_leftPhraseTerm     = NULL;
			// need this for displaying language of syn in
			// the json/xml feed in PageResults.cpp
			qt->m_langIdBitsValid = true;
			int langId = syn.m_langIds[j];
			uint64_t langBit = (uint64_t)1 << langId;
			if ( langId >= 64 ) langBit = 0;
			qt->m_langIdBits |= langBit;
			// need this for Matches.cpp
			qt->m_synWids0 = syn.m_wids0[j];
			qt->m_synWids1 = syn.m_wids1[j];
			int32_t na        = syn.m_numAlnumWords[j];
			// how many words were in the base we used to
			// get the synonym. i.e. if the base is "new jersey"
			// then it's 2! and the synonym "nj" has one alnum
			// word.
			int32_t ba        = syn.m_numAlnumWordsInBase[j];
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
			int64_t wid = syn.m_aids[j];
			// might be in a title: field or something
			if ( qw->m_prefixHash ) {
				int64_t ph = qw->m_prefixHash;
				wid= hash64h(wid,ph);
			}
			qt->m_termId    = wid & TERMID_MASK;
			//m_termIds[n]    = wid & TERMID_MASK;
			qt->m_rawTermId = syn.m_aids[j];
			// assume explicit bit is 0
			qt->m_explicitBit = 0;
			qt->m_matchesExplicitBits = 0;
			// boolean queries are not allowed term signs
			if ( m_isBoolean ) {
				qt->m_termSign = '\0';
				//m_termSigns[n] = '\0';
				// boolean fix for "health OR +sports" because
				// the + there means exact word match, no syns
				if ( qw->m_wordSign == '+' ) {
					qt->m_termSign  = qw->m_wordSign;
					//m_termSigns[n]  = qw->m_wordSign;
				}
			}
			// if not bool, ensure to change signs in both places
			else {
				qt->m_termSign  = qw->m_wordSign;
				//m_termSigns[n]  = qw->m_wordSign;
			}
			// do not use an explicit bit up if we got a hard count
			qt->m_hardCount = qw->m_hardCount;
			//qw->m_queryWordTerm   = qt;
			// IndexTable.cpp uses this one
			qt->m_inQuotes  = qw->m_inQuotes;
			// usually this is right
			char *ptr = syn.m_termPtrs[j];
			// buf if it is NULL that means we transformed the
			// word by like removing accent marks and stored
			// it in m_synWordBuf, as opposed to just pointing
			// to a line in memory of wiktionary-buf.txt.
			if ( ! ptr ) {
				int32_t off = syn.m_termOffs[j];
				if ( off < 0 ) { 
					char *xx=NULL;*xx=0; }
				if ( off > qw->m_synWordBuf.length() ) {
					char *xx=NULL;*xx=0; }
				// use QueryWord::m_synWordBuf which should
				// be persistent and not disappear like
				// syn.m_synWordBuf.
				ptr = qw->m_synWordBuf.getBufStart() + off;
			}
			// point to the string itself that is the word
			qt->m_term     = ptr;
			qt->m_termLen  = syn.m_termLens[j];
			// qt->m_term     = syn.m_termPtrs[j];
			// reset our implicit bits to 0
			qt->m_implicitBits = 0;
			// assume not under a NOT bool op
			//qt->m_underNOT = false;
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
	
	if ( n > ABS_MAX_QUERY_TERMS ) { char *xx=NULL;*xx=0; }


	// count them for doing number of combos
	//m_numExplicitBits = shift;

	// . repeated terms have the same termbits!!
	// . this is only for bool queries since regular queries ignore
	//   repeated terms in setWords()
	// . we need to support: "trains AND (perl OR python) NOT python"
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// BUT NOT IF in a UOR'd list!!! Metalincs bug...
		if ( m_qterms[i].m_isUORed ) continue;
		// that didn't seem to fix it right, for dup terms that
		// are the FIRST term in a UOR sequence... they don't seem
		// to have m_isUORed set
		if ( m_hasUOR ) continue;
		for ( int32_t j = 0 ; j < i ; j++ ) {
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
	int32_t nw = m_numWords;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not a range: query term
		if ( m_qwords[i].m_fieldCode != FIELD_RANGE ) continue;
		// loop over all our associates (in same parens level) to
		// get the OR of all the explicit bits
		qvec_t allBits = 0;
		for ( int32_t j=i;j<nw &&m_qwords[j].m_opcode!=OP_RIGHTPAREN;j++){
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
		for ( int32_t j=i;j<nw &&m_qwords[j].m_opcode!=OP_RIGHTPAREN;j++){
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
		//m_termSigns[0]         = '*';
	}

	// . or bits into the m_implicitBits member of phrase QueryTerms that
	//   represent the consitutent words
	// . loop over each 
	//m_numTerms = n2;

	// . how many of the terms are non fielded singletons?
	// . this is just for support of the BIG HACK in Summary.cpp
	/*
	m_numTermsSpecial = 0;
	for ( int32_t i = 0 ; i < n ; i++ ) {
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
	//for( int32_t i = 0 ; i < m_numTerms ; i++ ) m_componentCodes[i] = -2;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		QueryTerm *qt = &m_qterms[i];
		qt->m_componentCode = -2;
	}
	m_numComponents = 0;

	// . now set m_phrasePart for Summary.cpp's hackfix filter
	// . only set this for the non-phrase terms, since keepAllSingles is
	//   set to true when setting the Query for Summary.cpp::set in order
	//   to match the singles
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// set it up
		m_qterms[i].m_matchesExplicitBits = m_qterms[i].m_explicitBit;
		// or in the repeats
		for ( int32_t j = 0 ; j < m_numTerms ; j++ ) {
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
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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
		int32_t qwn = qw1 - m_qwords;
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
	int32_t shift = 0;
	m_requiredBits = 0;
	for ( int32_t i = 0; i < n ; i++ ){
		QueryTerm *qt = &m_qterms[i];
		qt->m_explicitBit = 0;
		if ( ! qt->m_isRequired ) continue;
		// negative terms are "negative required", but we ignore here
		if ( qt->m_termSign == '-' ) continue;
		qt->m_explicitBit = 1<<shift;
		m_requiredBits |= qt->m_explicitBit;
		shift++;
		if ( shift >= (int32_t)(sizeof(qvec_t)*8) ) break;
	}
	// now implicit bits
	for ( int32_t i = 0; i < n ; i++ ){
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
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
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
		int32_t qwn = qw1 - m_qwords;
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
	for (int32_t i = 0 ; i < m_numTerms - 1 ; i++ ) {
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
		int64_t id = 0LL;
		// store compound terms last
		int32_t n = m_numTerms;
		// sum of termfreqs
		//int64_t sum = 0;
		// we got a UOR'd list, see whose involved
		int32_t j ;
		int32_t numUORComponents = 0;
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
			int32_t a = j;
			int32_t b = j;
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
		gbmemcpy ( &m_qterms[n] , &m_qterms[i] , sizeof(QueryTerm) );
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
bool Query::isCompoundTerm ( int32_t i ) {
	//return ( m_componentCodes[i] == -1 );
	if ( i >= m_numTerms ) return false;
	QueryTerm *qt = &m_qterms[i];
	return ( qt->m_componentCode == -1 );
}

bool Query::setQWords ( char boolFlag , 
			bool keepAllSingles ,
			Words &words ,
			Phrases &phrases ) {

	// . break query up into Words and phrases
	// . because we now deal with boolean queries, we make parentheses
	//   their own separate Word, so tell "words" we're setting a query
	//Words words;
	if ( ! words.set ( m_sb.getBufStart() , m_sb.length() ,
			   //buf , m_bufLen,
			    TITLEREC_CURRENT_VERSION, true, true ) )
		return log("query: Had error parsing query: %s.",
			   mstrerror(g_errno));
	int32_t numWords = words.getNumWords();
	// truncate it
	if ( numWords > ABS_MAX_QUERY_WORDS ) {
		log("query: Had %"INT32" words. Max is %"INT32". Truncating.",
		    numWords,(int32_t)ABS_MAX_QUERY_WORDS);
		numWords = ABS_MAX_QUERY_WORDS;
		m_truncated = true;
	}
	m_numWords = numWords;
	// alloc the mem if we need to (mdw left off here)
	int32_t need = m_numWords * sizeof(QueryWord);
	// sanity check
	if ( m_qwords || m_qwordsAllocSize ) { char *xx = NULL; *xx = 0; }
	// point m_qwords to our generic buffer if it will fit
	//	if ( need < GBUF_SIZE ) {
	if ( m_gnext + need < m_gbuf + GBUF_SIZE && 
	     // it can wrap so watch out with this:
	     need < GBUF_SIZE ) {
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
	// reset safebuf in there
	for ( int32_t i = 0 ; i < m_numWords ; i++ )
		m_qwords[i].constructor();

	// is all alpha chars in query in upper case? caps lock on?
	bool allUpper = true;
	char *p    = m_sb.getBufStart();//m_buf;
	char *pend = m_sb.getBuf(); // m_buf + m_bufLen;
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
	int32_t  fieldLen  = 0;
	// keep track of the start of different chunks of quotes
	int32_t quoteStart = -1;
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

	int32_t userWeight       = 1;
	char userType         = 'r';
	int32_t userWeightPhrase = 1;
	char userTypePhrase   = 'r';
	int32_t ignorei          = -1;

	// assume we contain no pipe operator
	int32_t pi = -1;

	int32_t posNum = 0;
	char *ignoreTill = NULL;

	// loop over all words, these QueryWords are 1-1 with "words"
	for ( int32_t i = 0 ; i < numWords && i < ABS_MAX_QUERY_WORDS ; i++ ) {
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
		qw->m_ignoreWordInBoolQuery = false;
		qw->m_wordNum = i;
		// get word as a string
		//char *w    = words.getWord(i);
		//int32_t  wlen = words.getWordLen(i);
		qw->m_word    = words.getWord(i);
		qw->m_wordLen = words.getWordLen(i);
		qw->m_isPunct = words.isPunct(i);

		qw->m_posNum = posNum;

		// count 1 unit for it
		posNum++;

		// we ignore the facet value range list...
		if ( ignoreTill && qw->m_word < ignoreTill ) 
			continue;

		// . we duplicated this code from XmlDoc.cpp's
		//   getWordPosVec() function
		if ( qw->m_isPunct ) { // ! wids[i] ) {
			char *wp = qw->m_word;
			int32_t  wplen = qw->m_wordLen;
			// simple space or sequence of just white space
			if ( words.isSpaces(i) ) 
				posNum += 0;
			// 'cd-rom'
			else if ( wp[0]=='-' && wplen==1 ) 
				posNum += 0;
			// 'mr. x'
			else if ( wp[0]=='.' && words.isSpaces2(i,1)) 
				posNum += 0;
			// animal (dog)
			else 
				posNum++;
		}

		char *w   = words.getWord(i);
		int32_t wlen = words.getWordLen(i);
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
		// [133.0r]
		// is it the bracket operator?
		// " LeFtB 113 rp RiGhB "
		if ( wlen == 5 &&
		     w[0]=='L'&&w[1]=='e'&&w[2]=='F'&&w[3]=='t'&&w[4]=='B'&& 
		     i+4 < numWords ) {
			// s MUST point to a number
			char *s = words.getWord(i+2);
			int32_t slen = words.getWordLen(i+2);
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
			float fval = atof2 (s, slen);
			// s2 MUST point to the a,r,ap,rp string
			char *s2 = words.getWord(i+4);
			// is it a phrase?
			if ( s2[1] == 'p' ) {
				userWeightPhrase = fval;
				userTypePhrase   = s2[0]; // a or r
			}
			else {
				userWeight = fval;
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
		int32_t nq = words.getNumQuotes(i) ;

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
		//log(LOG_DEBUG, "Query: nq: %"INT32" inQuotes: %d,quoteStart: %"INT32"",
		//    nq, inQuotes, quoteStart);
		// does word #i have a space in it? that will cancel fieldCode
		// if we were in a field
		// TODO: fix title:" hey there" (space in quotes is ok)
		bool cancelField = false;
		if ( words.hasSpace(i) && ! inQuotes ) cancelField = true;
		// fix title:"foo bar" "another quote" so "another quote"
		// is not in the title: field
		if ( words.hasSpace(i) && inQuotes && nq>= 2 ) 
			cancelField = true;

		// likewise for gbsortby operators watch out for boolean
		// operators at the end of the field. we also check for 
		// parens below when computing the hash of the value.
		if ( (fieldCode == FIELD_GBSORTBYINT ||
		      fieldCode == FIELD_GBSORTBYFLOAT ) &&
		     ( w[0] == '(' || w[0] == ')' ) )
			cancelField = true;

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
			int32_t  j = i-1 ;
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
			int32_t tlen = 0;
			for ( int32_t k = j ; k <= i ; k++ )
				tlen += words.getWordLen(k);
			// set field name to the compound name if it is
			field     = words.getWord (j);
			fieldLen  = tlen;
			if ( j == i ) fieldSign = wordSign;
			else          fieldSign = m_qwords[j].m_wordSign;
			// debug msg
			//char ttt[128];
			//gbmemcpy ( ttt , field , fieldLen );
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
				for ( int32_t k = j ; k <= i ; k++ )
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
		// MDW: don't undo it because we do not want to get synonyms
		// of terms in quotes. 7/15/2015
		// if ( i == quoteStart ) { // + 1 ) {
		// 	if ( i + 1 >= numWords || words.getNumQuotes(i+1)>0 ) {
		// 		qw->m_quoteStart = -1;
		// 		qw->m_inQuotes   = false;
		// 	}
		// }
		// . get prefix hash of collection name and field
		// . but first convert field to lower case
		uint64_t ph;
		int32_t fflen = fieldLen;
		if ( fflen > 62 ) fflen = 62;
		char ff[64];
		to_lower3_a ( field , fflen , ff );
		//uint32_tint32_tph=getPrefixHash(m_coll,m_collLen,ff,fflen);
		//ph=getPrefixHash(NULL,0,ff,fflen);
		ph = hash64 ( ff , fflen );
		// map "intitle" map to "title"
		if ( fieldCode == FIELD_TITLE )
			ph = hash64 ( "title", 5 );
		// make "suburl" map to "inurl"
		if ( fieldCode == FIELD_SUBURL )
			ph = hash64 ( "inurl", 5 );

		// fix for filetype:pdf queries
		if ( fieldCode == FIELD_TYPE )
			ph = hash64 ("type",4);

		// these are range constraints on the gbsortby: termlist
		// which sorts numbers in a field from low to high
		if ( fieldCode == FIELD_GBNUMBERMIN )
			ph = hash64 ("gbsortby", 8);
		if ( fieldCode == FIELD_GBNUMBERMAX )
			ph = hash64 ("gbsortby", 8);
		if ( fieldCode == FIELD_GBNUMBEREQUALFLOAT )
			ph = hash64 ("gbsortby", 8);

		// fix for gbsortbyfloat:product.price
		if ( fieldCode == FIELD_GBSORTBYFLOAT )
			ph = hash64 ("gbsortby", 8);

		if ( fieldCode == FIELD_GBNUMBERMININT )
			ph = hash64 ("gbsortbyint", 11);
		if ( fieldCode == FIELD_GBNUMBERMAXINT )
			ph = hash64 ("gbsortbyint", 11);
		if ( fieldCode == FIELD_GBNUMBEREQUALINT )
			ph = hash64 ("gbsortbyint", 11);


		// really just like the gbfacetstr operator but we do not
		// display the facets, instead we try to match the provided
		// facet value exactly, case sensitvely.
		// NOT any more because termlist is too big and we need it
		// to be fast for diffbot.
		//if ( fieldCode == FIELD_GBFIELDMATCH )
		//	ph = hash64 ("gbfacetstr", 10);


		if ( fieldCode == FIELD_GBFACETFLOAT )
			ph = hash64 ("gbsortby",8);
		if ( fieldCode == FIELD_GBFACETINT )
			ph = hash64 ("gbsortbyint",11);

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
		     fieldCode == FIELD_GBPARENTURL ||
		     fieldCode == FIELD_EXT  || 
		     fieldCode == FIELD_LINK ||
		     fieldCode == FIELD_ILINK||
		     fieldCode == FIELD_SITELINK||
		     fieldCode == FIELD_LINKS||
		     fieldCode == FIELD_SITE ||
		     fieldCode == FIELD_IP   ||
		     fieldCode == FIELD_ISCLEAN ||
		     fieldCode == FIELD_QUOTA ||
		     fieldCode == FIELD_GBSORTBYFLOAT ||
		     fieldCode == FIELD_GBREVSORTBYFLOAT ||
		     // gbmin:price:1.23
		     fieldCode == FIELD_GBNUMBERMIN ||
		     fieldCode == FIELD_GBNUMBERMAX ||
		     fieldCode == FIELD_GBNUMBEREQUALFLOAT ||

		     fieldCode == FIELD_GBSORTBYINT ||
		     fieldCode == FIELD_GBREVSORTBYINT ||
		     fieldCode == FIELD_GBNUMBERMININT ||
		     fieldCode == FIELD_GBNUMBERMAXINT ||
		     fieldCode == FIELD_GBNUMBEREQUALINT ||
		     fieldCode == FIELD_GBFACETSTR ||
		     fieldCode == FIELD_GBFACETINT ||
		     fieldCode == FIELD_GBFACETFLOAT ||
		     fieldCode == FIELD_GBFIELDMATCH ||

		     fieldCode == FIELD_GBAD  ) {
			// . find 1st space -- that terminates the field value
			// . make "end" point to the end of the entire query
			char *end = 
				(words.m_words[words.m_numWords-1] +
				 words.m_wordLens[words.m_numWords-1]);
			// use this for gbmin:price:1.99 etc.
			int32_t firstColonLen = -1;
			int32_t lastColonLen = -1;
			int32_t colonCount = 0;
			int32_t firstComma = -1;
			// are we a facet term?
			bool isFacetNumTerm = false;
			if ( fieldCode == FIELD_GBFACETINT   ) 
				isFacetNumTerm = true;
			if ( fieldCode == FIELD_GBFACETFLOAT ) 
				isFacetNumTerm = true;
			// "w" points to the first alnumword after the field,
			// so for site:xyz.com "w" points to the 'x' and wlen 
			// would be 3 in that case sinze xyz is a word of 3 
			// chars. so advance
			// wlen until we hit a space.
			while ( w + wlen < end ) {
				// stop at first white space
				if ( is_wspace_utf8(w+wlen) ) break;
				// in case of gbmin:price:1.99 record first ':'
				if ( w[wlen]==':' ) {
					lastColonLen = wlen;
					if ( firstColonLen == -1 )
						firstColonLen = wlen;
					colonCount++;
				}
				// fix "gbsortbyint:date)"
				// these are used as boolean operators
				// so do not include them in the value.
				// we also did this above to set cancelField
				// to true.
				if ( w[wlen] == '(' || w[wlen] == ')' )
					break;
				// hit a comma in something like
				// gbfacetfloat:price,0-1,1-2.5,2.5-10
				if ( w[wlen]==',' && 
				     isFacetNumTerm && 
				     firstComma == -1 )
					firstComma = wlen;

				wlen++;
			}
			// ignore following words until we hit a space
			ignoreTilSpace = true;
			// the hash. keep it case insensitive. only
			// the fieldmatch stuff should be case-sensitive. 
			// this may change later.
			uint64_t wid = hash64Lower_utf8 ( w , wlen, 0LL );

			//
			// BEGIN FACET RANGE LISTS
			//
			qw->m_numFacetRanges = 0;
			// for gbfacetfloat:price,0-1,1-2.5,... just hash price
			if ( firstComma > 0 &&
			     ( fieldCode == FIELD_GBFACETINT ||
			       fieldCode == FIELD_GBFACETFLOAT ) )
				// hash the "price" not the following range lst
				// crap, since this uses the gbsortby: 
				// termlists it is NOT case-sensitive
				wid = hash64Lower_utf8 ( w , firstComma );
			// now store the range list so we can 
			// fill up the buckets below
			char *s = w + firstComma + 1;
			char *send = w + wlen;
			int32_t nr = 0;
			for ( ; s <send && fieldCode == FIELD_GBFACETINT;){
				// must be a digit or . or - or *
				if ( ! is_digit(s[0]) &&
				     s[0] != '.' &&
				     s[0] != '-' &&
                                     s[0] != '*')
					break;
				char *sav = s;
				// skip to hyphen
				for ( ; s < send && *s != '-' ; s++ );
				// stop if not hyphen
				if ( *s != '-' ) break;

                                // If the first character is a hyphen, check
                                // if its part of a negative number. If it is,
                                // don't consider it a hyphen
                                if ( sav == s && is_digit(s[1]) ) {
                                  // Read the entire negative number
                                  char *s2 = s + 1;
                                  for ( ; s2 < send && is_digit(s2[0]); s2++);
                                  // If there's a hyphen after the negative
                                  // number, use that as the hyphen separator
                                  if ( *s2 == '-' ) s = s2;
                                }

				// skip hyphen
				s++;
				// must be a digit or . or - or *
				if ( ! is_digit(s[0]) &&
				     s[0] != '.' &&
				     s[0] != '-' &&
                                     s[0] != '*')
					break;
				// if under max, add it
				if ( nr < MAX_FACET_RANGES ) {
				     if (sav[0] == '*')
                                       qw->m_facetRangeIntA [nr] =
                                         std::numeric_limits<int>::min();
                                     else
				       qw->m_facetRangeIntA [nr] = atoll(sav);

                                     if (s[0] == '*')
                                       qw->m_facetRangeIntB [nr] =
                                         std::numeric_limits<int>::max();
                                     else
				       qw->m_facetRangeIntB [nr] = atoll(s);
				     qw->m_numFacetRanges = ++nr;
				}
				// skip to comma or end
				for ( ; s < send && *s != ',' ; s++ );
				// skip that
				if ( *s != ',' ) break;
				// SKIP COMMA
				s++;
				// ignore till. does not included s
				ignoreTill = s;
			}
			for ( ; s <send && fieldCode==FIELD_GBFACETFLOAT;){
				// must be a digit or . or - or *
				if ( ! is_digit(s[0]) &&
				     s[0] != '.' &&
				     s[0] != '-' &&
                                     s[0] != '*')
					break;
				char *sav = s;
				// skip to hyphen
				for ( ; s < send && *s != '-' ; s++ );
				// stop if not hyphen
				if ( *s != '-' ) break;

                                // If the first character is a hyphen, check
                                // if its part of a negative number. If it is,
                                // don't consider it a hyphen
                                if ( sav == s && (is_digit(s[1]) ||
						  (s[1] == '.' &&
						   s + 2 < send &&
						   is_digit(s[2]))) ) {
                                  // Read the entire negative number
                                  char *s2 = s + 1;
                                  for ( ; s2 < send &&
					  (is_digit(s2[0]) || s2[0] == '.'); s2++);
                                  // If there's a hyphen after the negative
                                  // number, use that as the hyphen separator
                                  if ( *s2 == '-' ) s = s2;
                                }
 
				// save that
				char *cma = s;
				// skip hyphen
				s++;
				// must be a digit or . or - or *
				if ( ! is_digit(s[0]) &&
				     s[0] != '.' &&
				     s[0] != '-' &&
                                     s[0] != '*')
					break;
				// save that
				char *sav2 = s;
				// advance to comma etc.
				for ( ; s < send && *s != ',' ; s++ );
				char *cma2 = s;
				// if under max, add it
				if ( nr < MAX_FACET_RANGES ) {
				  if (sav[0] == '*')
                                    // min() is min positive value for float, so
				    // we want -max() instead
                                    qw->m_facetRangeFloatA [nr] =
                                      -std::numeric_limits<float>::max();
                                  else
				    qw->m_facetRangeFloatA [nr] =atof2(sav,cma-sav);

                                  if (sav2[0] == '*')
                                    qw->m_facetRangeFloatB [nr] =
                                      std::numeric_limits<float>::max();
                                  else
				    qw->m_facetRangeFloatB [nr] =atof2(sav2,cma2-sav2);
				  qw->m_numFacetRanges = ++nr;
				}
				// skip that
				if ( *s != ',' ) break;
				// SKIP COMMA
				s++;
				// ignore till. does not included s
				ignoreTill = s;
			}

			//
			// END FACET RANGE LISTS
			//
			     
			// i've decided not to make 
			// gbsortby:products.offerPrice 
			// gbmin:price:1.23 case insensitive
			// too late... we have to support what we have
			if ( fieldCode == FIELD_GBSORTBYFLOAT ||
			     fieldCode == FIELD_GBREVSORTBYFLOAT ||
			     fieldCode == FIELD_GBSORTBYINT ||
			     fieldCode == FIELD_GBREVSORTBYINT ) {
				wid = hash64Lower_utf8 ( w , wlen , 0LL );
				// do not include this word as part of
				// any boolean expression, so
				// Expression::isTruth() will ignore it and we
				// fix '(A OR B) gbsortby:offperice' query
				qw->m_ignoreWordInBoolQuery = true;
			}

			// this seems case sensitive now, gbfacetstr:humanLang
			if ( fieldCode == FIELD_GBFACETSTR ) {
				wid = hash64 ( w , wlen , 0LL );
			}

			if ( fieldCode == FIELD_GBFIELDMATCH ) {
				// hash the json field name. (i.e. tag.uri)
				// make it case sensitive as 
				// seen in XmlDoc.cpp::hashFacet2().
				// the other fields are hashed in 
				// XmlDoc.cpp::hashNumber3().
				// CASE SENSITIVE!!!!
				wid = hash64 ( w , firstColonLen , 0LL);
				// if it is like
				// gbfieldmatch:tag.uri:"http://xyz.com/poo"
				// then we should hash the string into
				// an int just like how the field value would
				// be hashed when adding gbfacetstr: terms
				// in XmlDoc.cpp:hashFacet2(). the hash of
				// the tag.uri field, for example, is set
				// in hashFacet1() and set to "val32". so
				// hash it just like that does here.
				char *a = w + firstColonLen + 1;
				// . skip over colon at start
				if ( a[0] == ':' ) a++;
				// . skip over quotes at start/end
				bool inQuotes = false;
				if ( a[0] == '\"' ) {
					inQuotes = true;
					a++;
				}
				// end of field
				char *b = a;
				// if not in quotes advance until
				// we hit whitespace
				char cs;
				for ( ; ! inQuotes && *b ; b += cs ) {
					cs = getUtf8CharSize(b);
					if ( is_wspace_utf8(b) ) break;
				}
				// if in quotes, go until we hit quote
				for ( ; inQuotes && *b != '\"';b++);
				// now hash that up. this must be 64 bit
				// to match in XmlDoc.cpp::hashFieldMatch()
				uint64_t val64 = hash64 ( a , b-a );
				// make a composite of tag.uri and http://...
				// just like XmlDoc.cpp::hashFacet2() does
				wid = hash64 ( val64 , wid );
			}

			// gbmin:price:1.23
			if ( lastColonLen>0 &&
			     ( fieldCode == FIELD_GBNUMBERMIN ||
			       fieldCode == FIELD_GBNUMBERMAX ||
			       fieldCode == FIELD_GBNUMBEREQUALFLOAT ||
			       fieldCode == FIELD_GBNUMBEREQUALINT ||
			       fieldCode == FIELD_GBNUMBERMININT ||
			       fieldCode == FIELD_GBNUMBERMAXINT ) ) {

				// record the field
				wid = hash64Lower_utf8(w,lastColonLen , 0LL );

				// fix gbminint:gbfacetstr:gbxpath...:165004297
				if ( colonCount == 2 ) {
					int64_t wid1;
					int64_t wid2;
					char *a = w;
					char *b = w + firstColonLen;
					wid1 = hash64Lower_utf8(a,b-a);
					a = w + firstColonLen+1;
					b = w + lastColonLen;
					wid2 = hash64Lower_utf8(a,b-a);
					// keep prefix as 2nd arg to this
					wid = hash64 ( wid2 , wid1 );
					// we need this for it to work
					ph = 0LL;
				}
				// and also the floating point after that
				qw->m_float = atof ( w + lastColonLen + 1 );
				qw->m_int = (int32_t)atoll( w +lastColonLen+1);
			}


			// should we have normalized before hashing?
			if ( fieldCode == FIELD_URL ||
			     fieldCode == FIELD_GBPARENTURL ||
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
				if ( fieldCode == FIELD_GBPARENTURL ) 
					addwww = true;
				if ( fieldCode == FIELD_SITELINK) 
					addwww = true;
				url.set ( w , wlen , addwww );
				char *site    = url.getHost();
				int32_t  siteLen = url.getHostLen();
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
		uint64_t wid = 0LL;
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
			int16_t csenum = get_iana_charset(w,wlen);
			// convert back to string
			char astr[128];
			int32_t alen = sprintf(astr, "%d", csenum);
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
			qw->m_isQueryStopWord =::isQueryStopWord (w,wlen,wid,
								  m_langId);
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
	for ( int32_t i = 0 ; i < pi ; i++ ) m_qwords[i].m_piped = true;
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
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
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
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
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
			int32_t  wlen = words.getWordLen(i);
			for ( int32_t j = 0 ; j < wlen-1 ; j++ ) {
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
	for ( int32_t i = 1 ; i < numWords ; i++ ) {
		// no punct, alnum only
		if ( words.isPunct(i) ) continue;
		// skip if not a stop word
		if ( ! (bits.m_bits[i] & D_IS_STOPWORD) ) continue;
		// continue if you can still pair across prev punct word
		if ( bits.m_bits[i-1] & D_CAN_PAIR_ACROSS ) continue;
		// otherwise, we can now start a phrase
		bits.m_bits[i] |= D_CAN_START_PHRASE;
	}

	// a bogus spam class, all words have 0 for their spam probability
	//Spam spam;
	//spam.reset ( words.getNumWords() );


	// treat strongly connected phrases like cd-rom and 3.2.0.3 as being
	// in quotes for the most part, therefore, set m_quoteStart for them
	int32_t j;
	int32_t qs = -1;
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

	// fix for tags.uri:http://foo.com/bar so it works like
	// tags.uri:"http://foo.com/bar" like it should
	int32_t first = -1;
	for ( j = 0 ; j < numWords ; j++ ) {
		// stop when we hit spaces
		if ( words.hasSpace(j) ) {
			first = -1;
			continue;
		}
		// skip if not in field
		if ( ! m_qwords[j].m_fieldCode ) continue;
		// must be in a generic field, the other fields like site:
		// will be messed up by this logic
		if ( m_qwords[j].m_fieldCode != FIELD_GENERIC ) continue;
		// first alnumword in field?
		if ( first == -1 ) {
			// must be alnum
			if ( m_qwords[j].m_isPunct ) continue;
			// must have punct then another alnum word
			if ( j+2 >= numWords ) break;
			// spaces screw it up
			if ( words.hasSpace(j+1) ) continue;
			// then an alnum word after
			first = j;
		}
		// we are in fake quoted phrase
		m_qwords[j].m_inQuotes = true;
		m_qwords[j].m_quoteStart = first;
	}



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

	int64_t *wids = words.getWordIds();

	// do phrases stuff
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
		// get the ith QueryWord
		QueryWord *qw = &m_qwords[i];
		// if word is ignored because it is opcode, or whatever,
		// it cannot start a phrase
		// THIS IS BROKEN
		//if ( qw->m_queryOp && qw->m_opcode == OP_PIPE){
		//	for (int32_t j = i-1;j>=0;j--){
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
		//int64_t tmp;
		for ( int32_t j = i - 1 ; j >= 0 ; j-- ) {
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
		//uint64_t pid = phrases.getPhraseId(i);
		uint64_t pid = 0LL;
		// nwp is a REGULAR WORD COUNT!!
		int32_t nwp = 0;
		if ( qw->m_inQuotedPhrase )
			// keep at a bigram for now... i'm not sure if we
			// will be indexing trigrams
			nwp = phrases.getMinWordsInPhrase(i,(int64_t *)&pid);
		// just get a two-word phrase term if not in quotes
		else
			nwp = phrases.getMinWordsInPhrase(i,(int64_t *)&pid);
		// store it
		qw->m_rawPhraseId = pid;
		// does word #i start a phrase?
		if ( pid != 0 ) {
			uint64_t ph = qw->m_prefixHash ;
			// store the phrase id with coll/prefix
			//qw->m_phraseId = g_indexdb.getTermId ( ph , pid );
			// like we do it in XmlDoc.cpp's hashString()
			if ( ph ) qw->m_phraseId = hash64 ( pid , ph );
			else      qw->m_phraseId = pid;
			// how many regular words int32_t is the bigram?
			int32_t plen2; phrases.getPhrase ( i , &plen2 ,2);
			// the trigram?
			int32_t plen3; phrases.getPhrase ( i , &plen3 ,3);
			// get just the bigram for now
			qw->m_phraseLen = plen2;
			// do not ignore the phrase, it's valid
			qw->m_ignorePhrase = 0;
			// set our rightPhraseEnd point
			//qw->m_rightPhraseEnd = i + phrases.getNumWords(i);
			// leave it as 0 if it got truncated i guess by the
			// MAX_QUERY_WORDS of 320
			qw->m_rightRawWordId = 0LL;
			// store left and right raw word ids 
			int32_t ni = i + nwp - 1;
			if ( ni < m_numWords )
				qw->m_rightRawWordId=m_qwords[ni].m_rawWordId;
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
			int32_t nw = phrases.getNumWordsInPhrase2(i);
			int32_t j;
			// search up to this far
			int32_t maxj = i + nw;
			// but not past our truncated limit
			if ( maxj > ABS_MAX_QUERY_WORDS ) 
				maxj = ABS_MAX_QUERY_WORDS;

			for ( j = i ; j < maxj ; j++ ) {
				// skip punct
				if ( words.isPunct(j)         ) continue;
				// break out if not a stop word
				if ( ! bits.isStopWord(j)     ) break;
				// break out if has a term sign
				if (   m_qwords[j].m_wordSign ) break;
			}
			// if everybody in phrase #i was a signless stopword
			// and the phrase was signless, make it have a '*' sign
			if ( j >= maxj && m_qwords[i].m_phraseSign == '\0' ) 
				m_qwords[i].m_phraseSign = '*';
			// . if a constituent has a - sign, then the whole 
			//   phrase becomes negative, too
			// . fixes 'apple -computer' truncation problem
			for ( int32_t j = i ; j < maxj ; j++ )
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
		// . MDW: no longer do this. but we should consider them
		//   wikibigrams for proximity weighting
		// if ( ( qw->m_leftConnected || qw->m_rightConnected ) )
		// 	qw->m_ignoreWord = IGNORE_CONNECTED;
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
			for ( int32_t j = 0 ; j < i ; j++ ) {
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
			for ( int32_t j = 0 ; j < i ; j++ ) {
				if ( m_qwords[j].m_ignorePhrase ) continue;
				if ( m_qwords[j].m_phraseId == qw->m_phraseId &&
				     m_qwords[j].m_phraseSign 
				     == qw->m_phraseSign)
					qw->m_ignorePhrase = IGNORE_REPEAT;
			}
		}
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
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
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
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
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
	int32_t count = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {	
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignorePhrase ) continue;
		if ( ! qw->m_phraseId   ) continue;
		count++;
	}
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		// count non-ignored words
		if ( qw->m_ignoreWord ) continue;
		// if under limit, continue
		if ( count++ < ABS_MAX_QUERY_TERMS ) continue;
		// . otherwise, ignore
		// . if we set this for our UOR'ed terms from SearchInput.cpp's
		//   UOR'ed facebook interests then it causes us to get no results!
		//   so make sure that MAX_QUERY_TERMS is big enough with respect to
		//   the opCount in SearchInput.cpp
		qw->m_ignoreWord = IGNORE_BREECH;
		// left phrase should get a '*'
		int32_t left = qw->m_leftPhraseStart;
		if ( left >= 0 && ! m_qwords[left].m_phraseSign )
			m_qwords[left].m_phraseSign = '*';
		// our phrase should get a '*'
		if ( qw->m_phraseId && ! qw->m_phraseSign )
			qw->m_phraseSign = '*';
	}

	// . fix the 'x -50a' query so it returns results
	// . how many non-negative, non-ignored words/phrases do we have?
	count = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord      ) continue;
		if ( qw->m_wordSign == '-' ) continue;
		count++;
	}
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignorePhrase      ) continue;
		if ( qw->m_phraseSign == '-' ) continue;
		if ( qw->m_phraseId == 0LL   ) continue;
		count++;
	}
	// if everybody is ignored or negative UNignore first query stop word
	if ( count == 0 ) {
		for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
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
	//for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
	//	if ( ! m_qwords[i].m_ignoreWord                 ) continue;
	//	if ( m_qwords[i].m_ignoreWord == IGNORE_DEFAULT ) continue;
	//	m_numIgnored++;
	//}

	quoteStart = -1;
	int32_t quoteEnd = -1;
	// set m_quoteENd
	for ( int32_t i = m_numWords - 1 ; i >= 0 ; i-- ) {
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


	int32_t wkid = 0;
	int32_t upTo = -1;
	int32_t wk_start;
	int32_t wk_nwk;
	//int64_t *wids = words.getWordIds();
	//
	// set the wiki phrase ids
	//
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
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
		int32_t nwk ;
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


	// consider terms strongly connected like wikipedia title phrases
	for ( int32_t i = 0 ; i + 2 < m_numWords ; i++ ) {
		// get ith word
		QueryWord *qw1 = &m_qwords[i];
		// must not already be in a wikiphrase
		//if ( qw1->m_wikiPhraseId > 0 ) continue;
		// what query word # is that?
		int32_t qwn = qw1 - m_qwords;
		// get the next alnum word after that
		// assume its the last word in our bigram phrase
		QueryWord *qw2 = &m_qwords[qwn+2];
		// must be in same wikiphrase
		if ( qw2->m_wikiPhraseId > 0 ) continue;

		// if there is a strong connector like the . in 'dmoz.org'
		// then consider it a wiki bigram too
		if ( ! qw1->m_rightConnected ) continue;
		if ( ! qw2->m_leftConnected  ) continue;

		// fix 'rdf.org.dumps' so org.dumps gets same
		// wikiphraseid as rdf.org
		int id;
		if ( qw1->m_wikiPhraseId ) id = qw1->m_wikiPhraseId;
		else id = ++wkid;

		// store it
		qw1->m_wikiPhraseId = id;
		qw1->m_wikiPhraseStart = i;
		qw1->m_numWordsInWikiPhrase = 2;

		qw2->m_wikiPhraseId = id;
		qw2->m_wikiPhraseStart = i;
		qw2->m_numWordsInWikiPhrase = 2;
	}

	// all done
	return true;
}

// return -1 if does not exist in query, otherwise return the query word num
int32_t Query::getWordNum ( int64_t wordId ) { 
	// skip if punct or whatever
	if ( wordId == 0LL || wordId == -1LL ) return -1;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
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

	{"gbfieldmatch",
	 FIELD_GBFIELDMATCH,
	 true,
	 "gbfieldmatch:strings.vendor:\"My Vendor Inc.\"",
	 "Matches all the meta tag or JSON or XML fields that have "
	 "the name \"strings.vendor\" and contain the exactly provided "
	 "value, in this case, <i>My Vendor Inc.</i>. This is CASE "
	 "SENSITIVE and includes punctuation, so it's exact match. In "
	 "general, it should be a very short termlist, so it should be fast.",
	 "Advanced Query Operators",
	 QTF_BEGINNEWTABLE },

	{"url", 
	 FIELD_URL, 
	 true,
	 "url:www.abc.com/page.html",
	 "Matches the page with that exact url. Uses the first url, not "
	 "the url it redirects to, if any." , 
	 NULL,
	 0 },

	{"ext", 
	 FIELD_EXT, 
	 true,
	 "ext:doc",
	 "Match documents whose url ends in the <i>.doc</i> file extension.",
	 NULL,
	 0 },


	{"link", 
	 FIELD_LINK, 
	 true,
	 "link:www.gigablast.com/foo.html",
	 "Matches all the documents that have a link to "
	 "http://www.gigablast.com/foobar.html",
	 NULL,
	 0 },

	//{"links", FIELD_LINKS, true,"Same as link:."},
	//{"ilink", FIELD_ILINK, true,"Similar to above."},


	{"sitelink", 
	 FIELD_SITELINK, 
	 true,
	 "sitelink:abc.foobar.com",
	 "Matches all documents that link to any page on the "
	 "<i>abc.foobar.com</i> site.",
	 NULL,
	 0 },

	{"site", 
	 FIELD_SITE, 
	 true,
	 "site:mysite.com",
	 "Matches all documents on the mysite.com domain.",
	 NULL,
	 0 },

	{"site", 
	 FIELD_SITE, 
	 true,
	 "site:www.mysite.com/dir1/dir2/",
	 "Matches all documents whose url starts with "
	 "www.mysite.com/dir1/dir2/",
	 NULL,
	 QTF_DUP },


	//{"coll", FIELD_COLL, true,"Not sure if this works."},
	{"ip", 
	 FIELD_IP, 
	 true,
	 "ip:1.2.3.4",
	 "Matches all documents whose IP is 1.2.3.4.",
	 NULL,
	 0 },


	{"ip", 
	 FIELD_IP, 
	 true,
	 "ip:1.2.3",
	 "Matches all documents whose IP STARTS with 1.2.3.",
	 NULL,
	 QTF_DUP },


	{"inurl", 
	 FIELD_SUBURL, 
	 true,
	 "inurl:dog",
	 "Matches all documents that have the word dog in their url, like "
	 "http://www.mysite.com/dog/food.html. However will not match "
	 "http://www.mysite.com/dogfood.html because it is not an "
	 "individual word. It must be delineated by punctuation.",
	 NULL,
	 0 },


	{"suburl", 
	 FIELD_SUBURL, 
	 true,
	 "suburl:dog",
	 "Same as inurl.",
	 NULL,
	0},

	{"intitle", 
	 FIELD_TITLE, 
	 false,
	 "title:cat",
	 "Matches all the documents that have the word cat in their "
	 "title.",
	 NULL,
	 0 },


	{"intitle", 
	 FIELD_TITLE, 
	 false,
	 "title:\"cat food\"",
	 "Matches all the documents that have the phrase \"cat food\" "
	 "in their title.",
	 NULL,
	 QTF_DUP },
	

	{"title", 
	 FIELD_TITLE, 
	 false,
	 "title:cat",
	 "Same as intitle:",
	 NULL,
	0},


	//{"isclean", FIELD_ISCLEAN, true,"Matches all pages that are deemed non-offensive and safe for children."},


	{"gbinrss", 
	 FIELD_GBRSS, 
	 true,
	 "gbinrss:1",
	 "Matches all documents that are in RSS feeds. Likewise, use "
	 "<i>gbinrss:0</i> to match all documents that are NOT in RSS feeds.",
	 NULL,
	 0},


	{"type", 
	 FIELD_TYPE, 
	 false,
	 "type:json",
	 "Matches all documents that are in JSON format. "
	 "Other possible types include "
	 "<i>html, text, xml, pdf, doc, xls, ppt, ps, css, json, status.</i> "
	 "<i>status</i> matches special documents that are stored every time "
	 "a url is spidered so you can see all the spider attempts and when "
	 "they occurred as well as the outcome.",
	 NULL,
	 0},

	{"filetype", 
	 FIELD_TYPE, 
	 false,
	 "filetype:json",
	 "Same as type: above.",
	 NULL,
	0},

	{"gbisadult",
	 FIELD_GENERIC,
	 false,
	 "gbisadult:1",
	 "Matches all documents that have been detected as adult documents "
	 "and may be unsuitable for children. Likewise, use "
	 "<i>gbisadult:0</i> to match all documents that were NOT detected "
	 "as adult documents.",
	 NULL,
	 0},

	{"gbimage",
	 FIELD_URL,
	 false,
	 "gbimage:site.com/image.jpg",
	 "Matches all documents that contain the specified image.",
	 NULL,
	 0},

	{"gbhasthumbnail",
	 FIELD_GENERIC,
	 false,
	 "gbhasthumbnail:1",
	 "Matches all documents for which Gigablast detected a thumbnail. "
	 "Likewise use <i>gbhasthumbnail:0</i> to match all documents that "
	 "do not have thumbnails.",
	 NULL,
	 0},


	{"gbtag*", 
	 FIELD_TAG, 
	 false,
	 "gbtag*",
	 "Matches all documents whose tag named * have the specified value "
	 "in the tagdb entry for the url. Example: gbtagsitenuminlinks:2 "
	 "matches all documents that have 2 qualified "
	 "inlinks pointing to their site "
	 "based on the tagdb record. You can also provide your own "
	 "tags in addition to the tags already present. See the <i>tagdb</i> "
	 "menu for more information.",
	 NULL,
	0},


	{"gbzipcode", 
	 FIELD_ZIP, 
	 false,
	 "gbzip:90210",
	 "Matches all documents that have the specified zip code "
	 "in their meta zip code tag.",
	 NULL,
	 0},

	{"gbcharset", 
	 FIELD_CHARSET, 
	 false,
	 "gbcharset:windows-1252",
	 "Matches all documents originally in the Windows-1252 charset. "
	 "Available character sets are listed in the <i>iana_charset.cpp</i> "
	 "file in the open source distribution. There are a lot. Some "
	 "more popular ones are: <i>us, latin1, iso-8859-1, csascii, ascii, "
	 "latin2, latin3, latin4, greek, utf-8, shift_jis.",
	 NULL,
	 0},


	// this just complicates things for now, so comment out
	//{"urlhash",FIELD_URLHASH, false,""},
	//{"urlhashdiv10",FIELD_URLHASHDIV10, false,""},
	//{"urlhashdiv100",FIELD_URLHASHDIV100, false,""},

	{"gblang",
	 FIELD_GBLANG,
	 false,
	 "gblang:de",
	 "Matches all documents in german. "
	 "The supported language abbreviations "
	 "are at the bottom of the <a href=/admin/filters>url filters</a> "
	 "page. Some more "
	 "common ones are <i>gblang:en, gblang:es, gblang:fr, "
	 // need quotes for this one!!
	 "gblang:\"zh_cn\"</i> (note the quotes for zh_cn!).",
	 NULL,
	 0},

	//{"gbquality",FIELD_GBQUALITY,true,""},
	//{"gblinktextin",FIELD_LINKTEXTIN,true,""},
	//{"gblinktextout",FIELD_LINKTEXTOUT,true,""},
	//{"gbkeyword",FIELD_KEYWORD,true,""},
	//{"gbcharset", FIELD_CHARSET, false,""},

	{"gbpathdepth", 
	 FIELD_GBOTHER, 
	 false,
	 "gbpathdepth:3",
	 "Matches all documents whose url has 3 path components to it like "
	 "http://somedomain.com/dir1/dir2/dir3/foo.html",
	 NULL,
	 0},


	{"gbhopcount", 
	 FIELD_GBOTHER, 
	 false,
	 "gbhopcount:2",
	 "Matches all documents that are a minimum of two link hops away "
	 "from a root url.",
	 NULL,
	 0},


	{"gbhasfilename", 
	 FIELD_GBOTHER, 
	 false,
	 "gbhasfilename:1",
	 "Matches all documents whose url ends in a filename like "
	 "<i>http://somedomain.com/dir1/myfile</i> and not "
	 "<i>http://somedomain.com/dir1/dir2/</i>. Likewise, use "
	 "<i>gbhasfilename:0</i> to match all the documents that do not "
	 "have a filename in their url.",
	 NULL,
	 0},


	{"gbiscgi", 
	 FIELD_GBOTHER, 
	 false,
	 "gbiscgi:1",
	 "Matches all documents that have a question mark in their url. "
	 "Likewise gbiscgi:0 matches all documents that do not.",
	 NULL,
	0},


	{"gbhasext", 
	 FIELD_GBOTHER, 
	 false,
	 "gbhasext:1",
	 "Matches all documents that have a file extension in their url. "
	 "Likewise, <i>gbhasext:0</i> matches all documents that do not have "
	 "a file extension in their url.",
	 NULL,
	0},

	{"gbsubmiturl", 
	 FIELD_GBOTHER, 
	 false,
	 "gbsubmiturl:domain.com/process.php",
	 "Matches all documents that have a form that submits to the "
	 "specified url.",
	 NULL,
	0},


	// diffbot only
	{"gbparenturl", 
	 FIELD_GBPARENTURL, 
	 true,
	 "gbparenturl:www.xyz.com/abc.html",
	 "Diffbot only. Match the json urls that "
	 "were extract from this parent url. Example: "
	 "gbparenturl:www.gigablast.com/addurl.htm",
	 NULL,
	 0},

	{"gbcountry",
	 FIELD_GBCOUNTRY,
	 false,
	 "gbcountry:us",
	 "Matches documents determined by Gigablast to be from the United "
	 "States. See the country abbreviations in the CountryCode.cpp "
	 "open source distribution. Some more popular examples include: "
	 "de, fr, uk, ca, cn.",
	 NULL,
	 0} ,

// mdw

	{"gbpermalink",
	 FIELD_GBPERMALINK,
	 false,
	 "gbpermalink:1",
	 "Matches documents that are permalinks. Use <i>gbpermalink:0</i> "
	 "to match documents that are NOT permalinks.",
	 NULL,
	0},

	{"gbdocid",
	 FIELD_GBDOCID,
	 false,
	 "gbdocid:123456",
	 "Matches the document with the docid 123456",
	 NULL,
	 0},



	//
	// for content type CT_STATUS documents (Spider status docs)
	//



	//{"qdom", FIELD_QUOTA, false,""},
	//{"qhost", FIELD_QUOTA, false,""},


	{"gbsortbyfloat", 
	 FIELD_GBSORTBYFLOAT, 
	 false,
	 "cameras gbsortbyfloat:price",
	 "Sort all documents that "
	 "contain 'camera' by price. <i>price</i> can be a root JSON field or "
	 "in a meta tag, or in an xml &lt;price&gt; tag.", 
	 "Numeric Field Query Operators",
	 QTF_BEGINNEWTABLE },


	{"gbsortbyfloat", 
	 FIELD_GBSORTBYFLOAT, 
	 false,
	 "cameras gbsortbyfloat:product.price",
	 "Sort all documents that "
	 "contain 'camera' by price. <i>price</i> can be in a JSON document "
	 "like "
	 "<i>{ \"product\":{\"price\":1500.00}} "
	 "</i> or, alternatively, an XML document like <i>"
	 "&lt;product&gt;&lt;price&gt;1500.00&lt;/price&gt;&lt;/product&gt;"
	 "</i>", 
	 NULL,
	 0 },


	{"gbrevsortbyfloat", 
	 FIELD_GBREVSORTBYFLOAT, 
	 false,
	 "cameras gbrevsortbyfloat:product.price",
	 "Like above example but sorted with highest prices on top.",
	 NULL,
	 0 },


	{"gbsortby", 
	 FIELD_GBSORTBYFLOAT, 
	 false,
	 "dog gbsortbyint:gbdocspiderdate",
	 "Sort the documents that contain 'dog' by "
	 "the date they were last spidered, with the newest "
	 "on top.",
	 NULL,
	 QTF_HIDE},

	{"gbrevsortby", 
	 FIELD_GBREVSORTBYFLOAT, 
	 false,
	 "dog gbrevsortbyint:gbdocspiderdate",
	 "Sort the documents that contain 'dog' by "
	 "the date they were last spidered, but with the "
	 "oldest on top.",
	 NULL,
	 QTF_HIDE},




	{"gbsortbyint", 
	 FIELD_GBSORTBYINT, 
	 false,
	 "pilots gbsortbyint:employees",
	 "Sort all documents that "
	 "contain 'pilots' by employees. "
	 "<i>employees</i> can be a root JSON field or "
	 "in a meta tag, or in an xml &lt;price&gt; tag. The value it "
	 "contains is interpreted as a 32-bit integer.", 
	 NULL,
	 0 },


	{"gbsortbyint", 
	 FIELD_GBSORTBYINT, 
	 false,
	 "gbsortbyint:gbdocspiderdate",
	 "Sort all documents by the date they were spidered/downloaded.",
	 NULL,
	 0},


	{"gbsortbyint", 
	 FIELD_GBSORTBYINT, 
	 false,
	 "gbsortbyint:company.employees",
	 "Sort all documents by employees. Documents can contain "
	 "<i>employees</i> in a JSON document "
	 "like "
	 "<i>{ \"product\":{\"price\":1500.00}} "
	 "</i> or, alternatively, an XML document like <i>"
	 "&lt;product&gt;&lt;price&gt;1500.00&lt;/price&gt;&lt;/product&gt;"
	 "</i>", 
	 NULL,
	 0 },

	{"gbsortbyint", 
	 FIELD_GBSORTBYINT, 
	 false,
	 "gbsortbyint:gbsitenuminlinks",
	 "Sort all documents by the number of distinct inlinks the "
	 "document's site has.",
	 NULL,
	 0 },


	{"gbrevsortbyint", 
	 FIELD_GBREVSORTBYINT, 
	 false,
	 "gbrevsortbyint:gbdocspiderdate",
	 "Sort all documents by the date they were spidered/downloaded "
	 "but with the oldest on top.",
	 NULL,
	 0},



	// gbmin:price:1.23

	{"gbminfloat", 
	 FIELD_GBNUMBERMIN, 
	 false,
	 "cameras gbminfloat:price:109.99",
	 "Matches all documents that "
	 "contain 'camera' or 'cameras' and have a price of at least 109.99. "
	 "<i>price</i> can be a root JSON field or "
	 "in a meta tag name <i>price</i>, or in an xml &lt;price&gt; tag.", 
	 NULL,
	 0 },


	{"gbminfloat", 
	 FIELD_GBNUMBERMIN, 
	 false,
	 "cameras gbminfloat:product.price:109.99",
	 "Matches all documents that "
	 "contain 'camera' or 'cameras' and have a price of at least 109.99 "
	 "in a JSON document like "
	 "<i>{ \"product\":{\"price\":1500.00}} "
	 "</i> or, alternatively, an XML document like <i>"
	 "&lt;product&gt;&lt;price&gt;1500.00&lt;/price&gt;&lt;/product&gt;"
	 "</i>", 
	 NULL,
	 0 },


	// alias we need to bury
	{"gbmin", 
	 FIELD_GBNUMBERMIN, 
	 false,
	 "",
	 "",
	 NULL,
	 QTF_HIDE},



	{"gbmaxfloat", 
	 FIELD_GBNUMBERMAX, 
	 false,
	 "cameras gbmaxfloat:price:109.99",
	 "Like the gbminfloat examples above, but is an upper bound.",
	 NULL,
	 0 },



	{"gbequalfloat", 
	 FIELD_GBNUMBEREQUALFLOAT, 
	 false,
	 "gbequalfloat:product.price:1.23",
	 "Similar to gbminfloat and gbmaxfloat but is an equality constraint.",
	 NULL,
	 0 },



	{"gbmax", 
	 FIELD_GBNUMBERMAX, 
	 false,
	 "",
	 "",
	 NULL,
	 QTF_HIDE},



	{"gbminint", 
	 FIELD_GBNUMBERMININT, 
	 false,
	 "gbminint:gbspiderdate:1391749680",
	 "Matches all documents with a spider timestamp of at least "
	 "1391749680. Use this as opposed th gbminfloat when you need "
	 "32 bits of integer precision.",
	 NULL,
	 0},


	{"gbmaxint", 
	 FIELD_GBNUMBERMAXINT, 
	 false,
	 "gbmaxint:company.employees:20",
	 "Matches all companies with 20 or less employees "
	 "in a JSON document like "
	 "<i>{ \"company\":{\"employees\":13}} "
	 "</i> or, alternatively, an XML document like <i>"
	 "&lt;company&gt;&lt;employees&gt;13&lt;/employees&gt;"
	 "&lt;/company&gt;"
	 "</i>", 
	 NULL,
	 0},


	{"gbequalint", 
	 FIELD_GBNUMBEREQUALINT, 
	 false,
	 "gbequalint:company.employees:13",
	 "Similar to gbminint and gbmaxint but is an equality constraint.",
	 NULL,
	 0},


	{"gbdocspiderdate",
	 FIELD_GENERIC,
	 false,
	 "gbdocspiderdate:1400081479",
	 "Matches documents that have "
	 "that spider date timestamp (UTC). "
	 //"Does not include the "
	 //"special spider status documents. "
	 "This is the time the document "
	 "completed downloading.",
	 "Date Related Query Operators",
	 QTF_BEGINNEWTABLE},


	{"gbspiderdate",
	 FIELD_GENERIC,
	 false,
	 "gbspiderdate:1400081479",
	 "Like above.",
	 //, but DOES include the special spider status documents.",
	 NULL,
	 0},

	{"gbdocindexdate",
	 FIELD_GENERIC,
	 false,
	 "gbdocindexdate:1400081479",
	 "Like above, but is the time the document was last indexed. "
	 "This time is "
	 "slightly greater than or equal to the spider date.",//Does not "
	 //"include the special spider status documents.",
	 NULL,
	 0},


	{"gbindexdate",
	 FIELD_GENERIC,
	 false,
	 "gbindexdate:1400081479",
	 "Like above.",//, but it does include the special spider status "
	 //"documents.",
	 NULL,
	 0},

	// {"gbreplyspiderdate",FIELD_GENERIC,false,
	//  "Example: gbspiderdate:1400081479 will return spider log "
	//  "results that have "
	//  "that spider date timestamp (UTC)"},

	{"gbfacetstr", 
	 FIELD_GBFACETSTR, 
	 false,
	 "gbfacetstr:color",
	 "Returns facets in "
	 "the search results "
	 "by their color field. <i>color</i> is case INsensitive.",
	 "Facet Related Query Operators",
	 QTF_BEGINNEWTABLE},


	{"gbfacetstr", 
	 FIELD_GBFACETSTR, 
	 false,
	 "gbfacetstr:product.color",
	 "Returns facets in "
	 "the color field in a JSON document like "
	 "<i>{ \"product\":{\"color\":\"red\"}} "
	 "</i> or, alternatively, an XML document like <i>"
	 "&lt;product&gt;&lt;color&gt;red&lt;/price&gt;&lt;/product&gt;"
	 "</i>. <i>product.color</i> is case INsensitive.", 
	 NULL,
	 0},

	{"gbfacetstr", 
	 FIELD_GBFACETSTR, 
	 false,
	 "gbfacetstr:gbtagsite cat",
	 "Returns facets from the site names of all pages "
	 "that contain the word 'cat' or 'cats', etc. <i>gbtagsite</i> is case insensitive."
	 ,
	 NULL,
	 0},

	{"gbfacetint", FIELD_GBFACETINT, false,
	 "gbfacetint:product.cores",
	 "Returns facets in "
	 "of the <i>cores</i> field in a JSON document like "
	 "<i>{ \"product\":{\"cores\":10}} "
	 "</i> or, alternatively, an XML document like <i>"
	 "&lt;product&gt;&lt;cores&gt;10&lt;/price&gt;&lt;/product&gt;"
	 "</i>. <i>product.cores</i> is case INsensitive.", 
	 NULL,
	 0},

	{"gbfacetint", FIELD_GBFACETINT, false,
	 "gbfacetint:gbhopcount",
	 "Returns facets in "
	 "of the <i>gbhopcount</i> field over the documents so you can "
	 "search the distribution of hopcounts over the index. <i>gbhopcount</i> is "
	 "case INsensitive.",
	 NULL,
	 0},

	{"gbfacetint", FIELD_GBFACETINT, false,
	 "gbfacetint:gbtagsitenuminlinks",
	 "Returns facets in "
	 "of the <i>sitenuminlinks</i> field for the tag <i>sitenuminlinks</i>"
	 "in the tag for each site. Any numeric tag in tagdb can be "
	 "facetizeed "
	 "in this manner so you can add your own facets this way on a per "
	 "site or per url basis by making tagdb entries. Case Insensitive.",
	 NULL,
	 0},


	{"gbfacetint", FIELD_GBFACETINT, false,
	 "gbfacetint:size,0-10,10-20,30-100,100-200,200-1000,1000-10000",
	 "Returns facets in "
	 "of the <i>size</i> field (either in json, field or a meta tag) "
	 "and cluster the results into the specified ranges. <i>size</i> is "
	 "case INsensitive.",
	 NULL,
	 0},

	{"gbfacetint", FIELD_GBFACETINT, false,
	 "gbfacetint:gbsitenuminlinks",
	 "Returns facets based on # of site inlinks the site of each "
	 "result has. <i>gbsitenuminlinks</i> is case INsensitive.",
	 NULL,
	 0},

	{"gbfacetfloat", FIELD_GBFACETFLOAT, false,
	 "gbfacetfloat:product.weight",
	 "Returns facets "
	 "of the <i>weight</i> field in a JSON document like "
	 "<i>{ \"product\":{\"weight\":1.45}} "
	 "</i> or, alternatively, an XML document like <i>"
	 "&lt;product&gt;&lt;weight&gt;1.45&lt;/price&gt;&lt;/product&gt;"
	 "</i>. <i>product.weight</i> is case INsensitive.", 
	 NULL,
	 0},

	{"gbfacetfloat", FIELD_GBFACETFLOAT, false,
	 "gbfacetfloat:product.price,0-1.5,1.5-5,5.0-20,20-100.0",
	 "Similar to above but cluster the pricess into the specified ranges. "
	 "<i>product.price</i> is case insensitive.",
	 NULL,
	 0},



	//
	// spider status docs queries
	//

	{"gbssUrl",
	 FIELD_GENERIC,
	 false,
	 "gbssUrl:com",
	 "Query the url of a spider status document.",
	 "Spider Status Documents", // title
	 QTF_BEGINNEWTABLE},


	{"gbssFinalRedirectUrl",
	 FIELD_GENERIC,
	 false,
	 "gbssFinalRedirectUrl:abc.com/page2.html",
	 "Query on the last url redirect to, if any.",
	 NULL, // title
	 0},

	{"gbssStatusCode",
	 FIELD_GENERIC,
	 false,
	 "gbssStatusCode:0",
	 "Query on the status code of the index attempt. 0 means no error.",
	 NULL,
	 0},

	{"gbssStatusMsg",
	 FIELD_GENERIC,
	 false,
	 "gbssStatusMsg:\"Tcp timed\"",
	 "Like gbssStatusCode but a textual representation.",
	 NULL,
	 0},

	{"gbssHttpStatus",
	 FIELD_GENERIC,
	 false,
	 "gbssHttpStatus:200",
	 "Query on the HTTP status returned from the web server.",
	 NULL,
	 0},

	{"gbssWasIndexed",
	 FIELD_GENERIC,
	 false,
	 "gbssWasIndexed:0",
	 "Was the document in the index before attempting to index? Use 0 "
	 " or 1 to find all documents that were not or were, respectively.",
	 NULL,
	 0},

	{"gbssIsDiffbotObject",
	 FIELD_GENERIC,
	 false,
	 "gbssIsDiffbotObject:1",
	 "This field is only present if the document was an object from "
	 "a diffbot reply. Use gbssIsDiffbotObject:0 to find the non-diffbot "
	 "objects.",
	 NULL,
	 0},

	{"gbssAgeInIndex",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssAgeInIndex",
	 "If the document was in the index at the time we attempted to "
	 "reindex it, how long has it been since it was last indexed?",
	 NULL,
	 0},

	{"gbssDomain",
	 FIELD_GENERIC,
	 false,
	 "gbssDomain:yahoo.com",
	 "Query on the domain of the url.",
	 NULL,
	 0},

	{"gbssSubdomain",
	 FIELD_GENERIC,
	 false,
	 "gbssSubdomain:www.yahoo.com",
	 "Query on the subdomain of the url.",
	 NULL,
	 0},

	{"gbssNumRedirects",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssNumRedirects",
	 "Query on the number of times the url redirect when attempting to "
	 "index it.",
	 NULL,
	 0},

	{"gbssDocId",
	 FIELD_GENERIC,
	 false,
	 "gbssDocId:1234567",
	 "Show all the spider status docs for the document with this docId.",
	 NULL,
	 0},

	{"gbssHopCount",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssHopCount",
	 "Query on the hop count of the document.",
	 NULL,
	 0},

	{"gbssCrawlRound",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssCrawlRound",
	 "Query on the crawl round number.",
	 NULL,
	 0},

	{"gbssDupOfDocId",
	 FIELD_GENERIC,
	 false,
	 "gbssDupOfDocId:123456",
	 "Show all the documents that were considered dups of this docId.",
	 NULL,
	 0},

	{"gbssPrevTotalNumIndexAttempts",
	 FIELD_GENERIC,
	 false,
	 "gbssPrevTotalNumIndexAttempts:1",
	 "Before this index attempt, how many attempts were there?",
	 NULL,
	 0},

	{"gbssPrevTotalNumIndexSuccesses",
	 FIELD_GENERIC,
	 false,
	 "gbssPrevTotalNumIndexSuccesses:1",
	 "Before this index attempt, how many successful attempts were there?",
	 NULL,
	 0},

	{"gbssPrevTotalNumIndexFailures",
	 FIELD_GENERIC,
	 false,
	 "gbssPrevTotalNumIndexFailures:1",
	 "Before this index attempt, how many failed attempts were there?",
	 NULL,
	 0},

	{"gbssFirstIndexed",
	 FIELD_GENERIC,
	 false,
	 "gbrevsortbyint:gbssFirsIndexed",
	 "The date in utc that the document was first indexed.",
	 NULL,
	 0},

	{"gbssContentHash32",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssContentHash32",
	 "The hash of the document content, excluding dates and times. Used "
	 "internally for deduping.",
	 NULL,
	 0},

	{"gbssDownloadDurationMS",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDownloadDurationMS",
	 "How long it took in millisecons to download the document.",
	 NULL,
	 0},

	{"gbssDownloadStartTime",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDownloadStartTime",
	 "When the download started, in seconds since the epoch, UTC.",
	 NULL,
	 0},

	{"gbssDownloadEndTime",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDownloadEndTime",
	 "When the download ended, in seconds since the epoch, UTC.",
	 NULL,
	 0},

	{"gbssUsedRobotsTxt",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssUsedRobotsTxt",
	 "This is 0 or 1 depending on if robots.txt was not obeyed or obeyed, "
	 "respectively.",
	 NULL,
	 0},

	{"gbssConsecutiveErrors",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssConsecutiveErrors",
	 "For the last set of indexing attempts how many were errors?",
	 NULL,
	 0},

	{"gbssIp",
	 FIELD_GENERIC,
	 false,
	 "gbssIp:1.2.3.4",
	 "The IP address of the document being indexed. Is 0.0.0.0 "
	 "if unknown.",
	 NULL,
	 0},

	{"gbssIpLookupTimeMS",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssIpLookupTimeMS",
	 "How long it took to lookup the IP of the document. Might have been "
	 "in the cache.",
	 NULL,
	 0},

	{"gbssSiteNumInlinks",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssSiteNumInlinks",
	 "How many good inlinks the document's site had.",
	 NULL,
	 0},

	{"gbssSiteRank",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssSiteRank",
	 "The site rank of the document. Based directly "
	 "on the number of inlinks the site had.",
	 NULL,
	 0},

	{"gbssContentInjected",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssContentInjected",
	 "This is 0 or 1 if the content was not injected or injected, "
	 "respectively.",
	 NULL,
	 0},

	{"gbssPercentContentChanged",
	 FIELD_GENERIC,
	 false,
	 "gbfacetfloat:gbssPercentContentChanged",
	 "A float between 0 and 100, inclusive. Represents how much "
	 "the document has changed since the last time we indexed it. This is "
	 "only valid if the document was successfully indexed this time."
	 "respectively.",
	 NULL,
	 0},

	{"gbssSpiderPriority",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssSpiderPriority",
	 "The spider priority, from 0 to 127, inclusive, of the document "
	 "according to the url filters table.",
	 NULL,
	 0},

	{"gbssMatchingUrlFilter",
	 FIELD_GENERIC,
	 false,
	 "gbfacetstr:gbssMatchingUrlFilter",
	 "The url filter expression the document matched.",
	 NULL,
	 0},

	{"gbssLanguage",
	 FIELD_GENERIC,
	 false,
	 "gbfacetstr:gbssLanguage",
	 "The language of the document. If document was empty or not "
	 "downloaded then this will not be present. Uses xx to mean "
	 "unknown language. Uses the language abbreviations found at the "
	 "bottom of the url filters page.",
	 NULL,
	 0},

	{"gbssContentType",
	 FIELD_GENERIC,
	 false,
	 "gbfacetstr:gbssContentType",
	 "The content type of the document. Like html, xml, json, pdf, etc. "
	 "This field is not present if unknown.",
	 NULL,
	 0},

	{"gbssContentLen",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssContentLen",
	 "The content length of the document. 0 if empty or not downloaded.",
	 NULL,
	 0},

	{"gbssCrawlDelayMS",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssCrawlDelay",
	 "The crawl delay according to the robots.txt of the document. "
	 "This is -1 if not specified in the robots.txt or not found.",
	 NULL,
	 0},

	{"gbssSentToDiffbotThisTime",
	 FIELD_GENERIC,
	 false,
	 "gbssSentToDiffbotThisTime:1",
	 "Was the document's url sent to diffbot for processing this time "
	 "of spidering the url?",
	 NULL,
	 0},

	{"gbssSentToDiffbotAtSomeTime",
	 FIELD_GENERIC,
	 false,
	 "gbssSentToDiffbotAtSomeTime:1",
	 "Was the document's url sent to diffbot for processing, either this "
	 "time or some time before?",
	 NULL,
	 0},

	{"gbssDiffbotReplyCode",
	 FIELD_GENERIC,
	 false,
	 "gbssDiffbotReplyCode:0",
	 "The reply received from diffbot. 0 means success, otherwise, it "
	 "indicates an error code.",
	 NULL,
	 0},

	{"gbssDiffbotReplyMsg",
	 FIELD_GENERIC,
	 false,
	 "gbfacetstr:gbssDiffbotReplyMsg:0",
	 "The reply received from diffbot represented in text.",
	 NULL,
	 0},

	{"gbssDiffbotReplyLen",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDiffbotReplyLen",
	 "The length of the reply received from diffbot.",
	 NULL,
	 0},

	{"gbssDiffbotReplyResponseTimeMS",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDiffbotReplyResponseTimeMS",
	 "The time in milliseconds it took to get a reply from diffbot.",
	 NULL,
	 0},

	{"gbssDiffbotReplyRetries",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssDiffbotReplyRetries",
	 "The number of times we had to resend the request to diffbot "
	 "because diffbot returned a 504 gateway timed out error.",
	 NULL,
	 0},

	{"gbssDiffbotReplyNumObjects",
	 FIELD_GENERIC,
	 false,
	 "gbfacetint:gbssDiffbotReplyNumObjects",
	 "The number of JSON objects diffbot excavated from the provided url.",
	 NULL,
	 0},


	/*
	{"gbstatus",
	 FIELD_GENERIC,
	 false,
	 "gbstatus:0",
	 "Matches all special spider status documents that spidered "
	 "their url successfully. Replace <i>0</i> with other numeric error "
	 "codes to get the other outcomes.",
	 "Spider Status Documents", // title
	 QTF_BEGINNEWTABLE},

	
	{"gbstatusmsg",
	 FIELD_GENERIC,
	 false,
	 "gbstatusmsg:tcp",
	 "Matches all special spider status documents that had a status "
	 "message containing the word <i>tcp</i> like in "
	 "<i>TCP Timed Out</i>. Similarly, gbstatus:success, "
	 "gbstatus:\"robots.txt\" are other possibilities.",
	 NULL,
	 0},


	{"url2", 
	 FIELD_URL, 
	 true,
	 "url2:www.abc.com/page.html",
	 "Matches the <i>Spider Status</i> documents for the specified url. "
	 "These special documents "
	 "let you know exactly when the url was attempted to be "
	 "spidered and the outcome.",
	 NULL,
	 0 },

	{"site2", 
	 FIELD_SITE, 
	 true,
	 "site2:mysite.com",
	 "Matches all the special spider status documents on the "
	 "mysite.com domain.",
	 NULL,
	 0 },


	{"ip2", 
	 FIELD_IP, 
	 true,
	 "ip2:1.2.3.4",
	 "Matches all the special spider status "
	 "documents whose IP is 1.2.3.4.",
	 NULL,
	 0 },

	{"inurl2", 
	 FIELD_SUBURL2, 
	 true,
	 "inurl2:dog",
	 "Matches all the special spider status "
	 "documents that have the word dog in their url, like "
	 "http://www.mysite.com/dog/food.html. However will not match "
	 "http://www.mysite.com/dogfood.html because it is not an "
	 "individual word. It must be delineated by punctuation.",
	 NULL,
	 0 },


	{"gbpathdepth2", 
	 FIELD_GBOTHER, 
	 false,
	 "gbpathdepth2:2",
	 "Similar to gbpathdepth: described above but for special "
	 "spider status documents.",
	 NULL,
	 0},

	{"gbhopcount2", 
	 FIELD_GBOTHER, 
	 false,
	 "gbhopcount2:3",
	 "Similar to gbhopcount: described above but for special "
	 "spider status documents.",
	 NULL,
	 0},


	{"gbhasfilename2", 
	 FIELD_GBOTHER, 
	 false,
	 "gbhasfilename2:1",
	 "Similar to gbhasfilename: described above but for special "
	 "spider status documents.",
	 NULL,
	 0},

	{"gbiscgi2",
	 FIELD_GBOTHER, 
	 false,
	 "gbiscgi2:1",
	 "Similar to gbiscgi: described above but for special "
	 "spider status documents.",
	 NULL,
	 0},

	{"gbhasext2",
	 FIELD_GBOTHER, 
	 false,
	 "gbhasext2:1",
	 "Similar to gbhasext: described above but for special "
	 "spider status documents.",
	 NULL,
	 0},
	*/


	// they don't need to know about this
	{"gbad",FIELD_GBAD,false,"","",NULL,QTF_HIDE},
	{"gbtagvector", FIELD_GBTAGVECTOR, false,"","",NULL,QTF_HIDE},
	{"gbgigabitvector", FIELD_GBGIGABITVECTOR, false,"","",NULL,QTF_HIDE},
	{"gbsamplevector", FIELD_GBSAMPLEVECTOR, false,"","",NULL,QTF_HIDE},
	{"gbcontenthash", FIELD_GBCONTENTHASH, false,"","",NULL,QTF_HIDE},
	{"gbduphash"  ,FIELD_GBOTHER,false,"","",NULL,QTF_HIDE},
	// call it field url to hash all up to the first space
	{"gbsitetemplate"           ,FIELD_URL,false,"","",NULL,QTF_HIDE}

	//{"gbcsenum",FIELD_GBCSENUM,false,""},
	//{"gboutlinkedtitle"         ,FIELD_GBOTHER,false,"gboutlinkedtitle:0 and gboutlinkedtitle:1 matches events whose title is not in and in a hyperlink, respectively."},
	//{"gbisaggregator"           ,FIELD_GBOTHER,false,"gbisaggregator:0|1 depending on if the event came from an event aggregator website, like eviesays.com."},
	//{"gbdeduped"                ,FIELD_GBOTHER,false,""},

	//{"gbinjected", FIELD_GBOTHER,false,"Was the document injected?."},


	
};

void resetQuery ( ) {
	s_table.reset();
}



int32_t getNumFieldCodes ( ) { 
	return (int32_t)sizeof(g_fields) / (int32_t)sizeof(QueryField); 
}

static bool initFieldTable(){

	if ( ! s_isInitialized ) {
		// set up the hash table
		if ( ! s_table.set ( 8 , 4 , 255,NULL,0,false,0,"qryfldtbl" ) )
			return log("build: Could not init table of "
					   "query fields.");
		// now add in all the stop words
		int32_t n = getNumFieldCodes();
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// skip if dup
			//if ( g_fields[i].m_flag & QTF_DUP ) continue;
			int64_t h = hash64b ( g_fields[i].text );
			// if already in there it is a dup
			if ( s_table.isInTable ( &h ) ) continue;
			// store the entity index in the hash table as score
			if ( ! s_table.addTerm ( &h, i+1 ) ) return false;
		}
		s_isInitialized = true;
	} 
	return true;
}


char getFieldCode ( char *s , int32_t len , bool *hasColon ) {
	// default
	if (hasColon) *hasColon = false;

	if (!initFieldTable()) return FIELD_GENERIC;
	int64_t h = hash64Lower_a(s, len );//>> 1) ;
	int32_t i = (int32_t) s_table.getScore ( &h ) ;
	if (i==0) return FIELD_GENERIC;
	//if (hasColon) *hasColon = g_fields[i-1].hasColon ; 
	return g_fields[i-1].field;
}

char getFieldCode2 ( char *s , int32_t len , bool *hasColon ) {
	// default
	if (hasColon) *hasColon = false;

	if (!initFieldTable()) return FIELD_GENERIC;
	// subtract the colon for matching
	if ( s[len-1]==':') len--;
	int64_t h = hash64 (s , len , 0LL );
	int32_t i = (int32_t) s_table.getScore ( &h ) ;
	if (i==0) return FIELD_GENERIC;
	//if (hasColon) *hasColon = g_fields[i-1].hasColon ; 
	return g_fields[i-1].field;
}

char getFieldCode3 ( int64_t h64 ) {
	if (!initFieldTable()) return FIELD_GENERIC;
	// subtract the colon for matching
	int32_t i = (int32_t) s_table.getScore ( &h64 ) ;
	if (i==0) return FIELD_GENERIC;
	//if (hasColon) *hasColon = g_fields[i-1].hasColon ; 
	return g_fields[i-1].field;
}


// guaranteed to be punctuation
bool Query::isConnection ( char *s , int32_t len ) {
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
	for (int32_t i=0;i<m_numTerms;i++){
		char c = getTermSign(i);
		char tt[512];
		int32_t ttlen = getTermLen(i);
		if ( ttlen > 254 ) ttlen = 254;
		if ( ttlen < 0   ) ttlen = 0;
		// this is utf8
		gbmemcpy ( tt , getTerm(i) , ttlen );
		tt[ttlen]='\0';
		if ( c == '\0' ) c = ' ';
		logf(LOG_DEBUG, "query: Query Term #%"INT32" "
		     "phr=%"INT32" termId=%"UINT64" rawTermId=%"UINT64""
		     " sign=%c "
		     "ebit=0x%0"XINT64" "
		     "impBits=0x%0"XINT64" "
		     "hc=%"INT32" "
		     "component=%"INT32" "
		     "otermLen=%"INT32" "
		     "term=%s ",
		     i,
		     (int32_t)isPhrase (i) ,
		     getTermId      (i) ,
		     getRawTermId   (i) ,
		     c ,
		     (int64_t)m_qterms[i].m_explicitBit  ,
		     (int64_t)m_qterms[i].m_implicitBits ,
		     (int32_t) m_qterms[i].m_hardCount ,
		     m_qterms[i].m_componentCode,
		     getTermLen(i),
		     tt                        );
	}
	
}


////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
//////////   ONLY BOOLEAN STUFF BELOW HERE  /////////////
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
bool  Query::testBoolean( unsigned char *bits ,int32_t vecSize){//qvec_t bitmask){
	if (!m_isBoolean) return false;
	Expression *e = &m_expressions [ 0 ];
	// find top-level expression
	//while (e->m_parent && e != e->m_parent) e = e->m_parent;
	return e->isTruth(bits,vecSize);//, bitmask);
	
}
void  Query::printBooleanTree(){
	if (!m_isBoolean) return;
	//Expression *e = &m_expressions [ 0 ];
	// find top-level expression
	//while (e->m_parent && e != e->m_parent) e = e->m_parent;
	//SafeBuf sbuf(1024,"botree");
	//e->print(&sbuf);
	//logf(LOG_DEBUG, "query: Boolean Query: %s", sbuf.getBufStart());
}
/*
// . also sets the m_underNOT member of each QueryTerm, too!!
// . returns false and sets g_errno on error, true otherwise
bool Query::setBooleanOperands ( ) {
	// we're done if we're not boolean
	if ( ! m_isBoolean ) return true;

	if ( m_truncated ) {
		g_errno = ETOOMANYOPERANDS;
		return log("query: Maximum number of bool operands "
			   "exceeded (%"INT32").",m_numTerms);
	}

	// set the QueryWord::m_opBit member of each query word.
	// so if  you have a query like 'A B OR C' then you need
	// to have both A and B if you don't have C. so every word
	// unless its an operator needs its own bit. quoted phrases
	// may present a problem down the road we'll have to deal with.
	int32_t opNum = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		// skip if field, opcode, punct. etc.
		if ( m_qwords[i].m_ignoreWord ) continue;
		// assign it a # i guess
		m_qwords[i].m_opNum = opNum++;
	}
	

	// alloc the mem if we need to (mdw left off here) 
	//int32_t need = (m_numWords/3) * sizeof(Expression);
	// illegitmate bool expressions breech the buffer
	int32_t need = (m_numWords) * sizeof(Expression);
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
	int32_t status = e->add ( 0           , // first word #
			       m_numWords  , // last  word #
			       this        , // array of QueryWords
			       0              ,// level
			       false );  // has NOT?
	if ( status < 0 ) {
		g_errno = ETOOMANYOPERANDS;
		return log("query: Maximum number of bool operands "
			   "(%"INT32") exceeded.",(int32_t)MAX_OPERANDS);
	}
	while (e->m_parent) {
		if (e == e->m_parent) {
			g_errno = EBADREQUEST;
			return log(LOG_WARN, "query: expression is own parent: "
				   "%s", m_orig);
		}
		e = e->m_parent;
	}

	//log(LOG_DEBUG, "query: set %"INT32" operands",
	//    m_numOperands);
	if (g_conf.m_logDebugQuery) {
		SafeBuf sbuf(1024);
		e->print(&sbuf);
		log(LOG_DEBUG, "query: Boolean Query: %s", sbuf.getBufStart());
	}

	// . get all the terms that are UNDER a NOT operator in some fashion
	// . these bits are 1-1 with m_qterms[]
	*/
	/*
	qvec_t notBits = e->getNOTBits( false );
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		if ( m_qterms[i].m_explicitBit & notBits )
			m_qterms[i].m_underNOT = true;
		else
			m_qterms[i].m_underNOT = false;
	}
	*/
/*
	return true;
}
*/
/*
// . returns -1 on bad query error
// . returns word AFTER the last word in our operand
int32_t Operand::set ( int32_t a , int32_t b , QueryWord *qwords , int32_t level ,
		    bool underNOT ) {
	// clear these
	//m_termBits         = 0;
	memset(m_opBits,0,MAX_OVEC_SIZE);

	m_hasNOT           = false;

	//m_hardRequiredBits = 0;
	// . parse out the operands and OR in their term bits
	// . the boy AND girl --> (the AND boy) AND girl
	// . "the boy toy" AND girl --> "the boy" AND "boy toy" AND girl
	// . cd-rom AND buy --> "cd-rom" AND buy
	// . phraseSign will not be 0 if its important (in quotes, cd-rom,...)
	for ( int32_t i = a ; i < b ; i++ ) {
		// get the QUERY word
		QueryWord *qw = &qwords[i];
		// set the parenthetical level of the word
		qw->m_level = level;
		// set this
		//qw->m_underNOT = underNOT;
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
			//qvec_t e =qw->m_queryPhraseTerm->m_explicitBit;
			//if (qw->m_phraseSign == '+') m_hardRequiredBits |= e;
			//m_termBits |= e;
			int32_t byte = qw->m_opNum / 8;
			int32_t mask = 1<<(qw->m_opNum % 8);
			if ( byte < MAX_OVEC_SIZE ) m_opBits[byte] |= mask;
		}
		// why would it be ignored? oh... if like cd-rom or in quotes
		if ( qw->m_ignoreWord ) continue;
		// . OR in the word term bits
		// . might be a word that's not a QueryTerm because
		//   query is too long
		if ( qw->m_queryWordTerm ) {
			//qvec_t e = qw->m_queryWordTerm->m_explicitBit;
			//if (qw->m_phraseSign == '+') m_hardRequiredBits |= e;
			//m_termBits |= e;
			int32_t byte = qw->m_opNum / 8;
			int32_t mask = 1<<(qw->m_opNum % 8);
			if ( byte < MAX_OVEC_SIZE ) m_opBits[byte] |= mask;
		}
	}
	return b;
}
*/

// . returns -1 on bad query error
// . returns next word to parse (after expression) on success
// . "*globalNumOperands" is how many expressions/operands are being used
//   in the global "expressions" and "operands" array

// . new: organize query into sum of products normal form, ie:
// . (a) OR (b AND c AND d) OR (e AND f)

/*
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
*/

//#define TYPE_OPERAND 1
//#define TYPE_OPCODE 2
//#define TYPE_EXPRESSION 3


// return false and set g_errno on error
// returns how many words expression was
bool Expression::addExpression (int32_t start, 
				int32_t end, 
				class Query      *q,
				int32_t              level
				) {

	if ( level >= MAX_EXPRESSIONS ) { 
		g_errno = ETOOMANYPARENS;
		return false;
	}

	// the # of the first alnumpunct word in the expression
	m_expressionStartWord = start;
	// and the last one
	//m_end = end;
	//m_hasNOT = hasNOT;
	m_q = q;

	//m_cc = 0;

	int32_t i = m_expressionStartWord;

	// try to fix 
	// type:html AND ((site:xyz.com OR site:abc.com))
	// query where there are double parens
	m_hadOpCode = false;

	// "start" is the current alnumpunct word we are parsing out
	for ( ; i<end ; i++ ) {

		QueryWord *qwords = q->m_qwords;

		QueryWord * qw = &qwords[i];
		// set this
		//qw->m_underNOT = underNOT;

		// set leaf node if not an opcode like "AND" and not punct.
		if ( ! qw->m_opcode && qw->isAlphaWord()){
			//m_opSlots[m_cc] = i;
			//m_opTypes[m_cc] = TYPE_OPERAND;
			//qw->m_opBitNum = m_cc;
			continue;//goto endExpr; mdw
		}
		if (qw->m_opcode == OP_NOT){
			//hasNOT = !hasNOT;
			//underNOT = hasNOT;
			continue;
		}
		else if (qw->m_opcode == OP_LEFTPAREN){
			// this is expression
			// . it should advance "i" to end of expression
			// point to next...
			q->m_numExpressions++;
			// make a new one:
			Expression *e=&q->m_expressions[q->m_numExpressions-1];
			// now set it
			if ( ! e->addExpression ( i+1, // skip over (
						  end ,
						  q ,
						  level + 1)  )
				return false;
			// skip over it. pt to ')'
			i += e->m_numWordsInExpression;
			qw->m_expressionPtr = e;
			//m_opSlots[m_cc] = (int32_t)e;
			//m_opTypes[m_cc] = TYPE_EXPRESSION;
			//qw->m_opBitNum = m_cc;
		}
		else if (qw->m_opcode == OP_RIGHTPAREN){
			// return size i guess, include )
			m_numWordsInExpression = i - m_expressionStartWord+1;
			return true;
		}
		else if (qw->m_opcode) {
			// add that mdw
			//m_opSlots[m_cc] = qw->m_opcode;
			//m_opTypes[m_cc] = TYPE_OPCODE;
			//qw->m_opBitNum = m_cc;
			//m_cc++;
			m_hadOpCode = true;
			continue;
		}
		// white space?
		continue;
	}

	m_numWordsInExpression = i - m_expressionStartWord;

	return true;
}

// each bit is 1-1 with the explicit terms in the boolean query
bool Query::matchesBoolQuery ( unsigned char *bitVec , int32_t vecSize ) {
	return m_expressions[0].isTruth ( bitVec , vecSize );
}


bool isBitNumSet ( int32_t opBitNum, unsigned char *bitVec, int32_t vecSize ) {
	int32_t byte = opBitNum / 8;
	int32_t mask = 1<<(opBitNum % 8);
	if ( byte >= vecSize ) { char *xx=NULL;*xx=0; }
	return bitVec[byte] & mask;
}

// . "bits" are 1-1 with the query words in Query::m_qwords[] array
//   including ignored words and spaces i guess since Expression::add()
//   seems to do that.
bool Expression::isTruth ( unsigned char *bitVec ,int32_t vecSize ) {

	//
	// operand1 operand2 operator1 operand3 operator2 ....
	//

	// result: -1 means unknown at this point
	int32_t result = -1;

	char prevOpCode = 0;
	int32_t prevResult ;
	// result of current operand
	int32_t opResult = -1;

	int32_t i    =     m_expressionStartWord;
	int32_t iend = i + m_numWordsInExpression;

	bool hasNot = false;

	for ( ; i < iend ; i++ ) {

		QueryWord *qw = &m_q->m_qwords[i];

		// ignore parentheses, aren't real opcodes.
		// we just want OP_AND/OP_OR/OP_NOT
		int32_t opcode = qw->m_opcode;
		if ( opcode != OP_AND && 
		     opcode != OP_OR  && 
		     opcode != OP_NOT )
			opcode = 0;

		if ( opcode == OP_NOT ) {
			hasNot = true;
			continue;
		}


		// so operands are expressions as well
		Expression *e = (Expression *)qw->m_expressionPtr;
		if ( e ) {
			// save prev one. -1 means no prev.
			prevResult = opResult;
			// set new onw
			opResult = e->isTruth ( bitVec , vecSize );
			// skip over that expression. point to ')'
			i += e->m_numWordsInExpression;
			// flip?
			if ( hasNot ) {
				if ( opResult == 1 ) opResult = 0;
				else                 opResult = 1;
				hasNot = false;
			}
		}

		if ( opcode && ! e ) {
			prevOpCode = opcode;//m_opSlots[i];
			continue;
		}

		// simple operand
		if ( ! opcode && ! e ) {
			// for regular word operands
			// ignore it like a space?
			if ( qw->m_ignoreWord ) continue;
			// ignore gbsortby:offerprice in bool queries
			// at least for evaluating them
			if ( qw->m_ignoreWordInBoolQuery ) continue;
			// save old one
			prevResult = opResult;
			// convert word to term #
			QueryTerm *qt = qw->m_queryWordTerm;
			// fix title:"notre dame" AND NOT irish
			if ( ! qt ) qt = qw->m_queryPhraseTerm;
			if ( ! qt ) continue;
			// phrase terms are not required and therefore
			// do not have a v alid qt->m_bitNum set, so dont core
			if ( ! qt->m_isRequired ) continue;
			// . m_bitNum is set in Posdb.cpp when it sets its
			//   QueryTermInfo array
			// . it is basically the query term #
			// . see iff that bit is set in this docid's vec
			opResult = isBitNumSet ( qt->m_bitNum,bitVec,vecSize );
			// flip?
			if ( hasNot ) {
				if ( opResult == 1 ) opResult = 0;
				else                 opResult = 1;
				hasNot = false;
			}
		}

		// need two to tango. i.e. (true OR false)
		if ( prevResult == -1 ) continue;

		// if this is not the first time... we got two
		if ( prevOpCode == OP_AND ) {
			// if first operation we encount is A AND B then
			// default result to on. only allow an AND operation
			// to turn if off.
			if ( result == -1 ) result = true;
			if ( ! prevResult ) result = false;
			if ( !    opResult ) result = false;
		}
		else if ( prevOpCode == OP_OR ) {
			// if first operation we encount is A OR B then
			// default result to off
			if ( result == -1 ) result = false;
			if ( prevResult ) result = true;
			if (   opResult ) result = true;
		}
	}

	// if we never set result, then it was probably a single
	// argument expression like something in double parens like
	// ((site:xyz.com OR site:abc.com)). so set it to value of
	// first operand, opResult.
	if ( prevOpCode == 0 && result == -1 ) result = opResult;

	if ( result == -1 ) return true;
	if ( result ==  0 ) return false;
	return true;
}

/*
// . "bits" are 1-1 with the query terms in Query::m_qterms[] array
// . hasNOT is true if there's a NOT just to the left of this WHOLE expressions
//   ourside the parens
qvec_t Expression::getNOTBits ( bool hasNOT ) {
	qvec_t notBits = 0;
// 	for ( int32_t i = 0 ; i < m_numOperands ; i++ ) {
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
*/

// print boolean expression for debug purposes
void Expression::print(SafeBuf *sbuf) {
	/*
	if (m_hasNOT) sbuf->safePrintf("NOT ");
	if (m_operand){
		m_operand->print(sbuf);
		return;
	}
	sbuf->safePrintf("(");
	for (int32_t i=0; i < m_numChildren ; i++) {
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
	*/
}

/*
void Operand::print(SafeBuf *sbuf) {
// 	int32_t shift = 0;
// 	while (m_termBits >> shift) shift++;
// 	sbuf->safePrintf("%i", 1<<(shift-1));
	if (m_hasNOT) sbuf->safePrintf("NOT 0x%"XINT64"",*(int64_t *)m_opBits);
	else sbuf->safePrintf("0x%"XINT64"", *(int64_t *)m_opBits);
}
*/

// if any one query term is split, msg3a has to split the query
bool Query::isSplit() {
	for(int32_t i = 0; i < m_numTerms; i++) 
		if(m_qterms[i].isSplit()) return true;
	return false;
}

void QueryTerm::constructor ( ) {
	m_facetHashTable.constructor(); // hashtablex
	m_facetIndexBuf.constructor(); // safebuf
	m_langIdBits = 0;
	m_langIdBitsValid = false;
	m_numDocsThatHaveFacet = 0;
}

bool QueryTerm::isSplit() {
	if(!m_fieldCode) return true;
	if(m_fieldCode == FIELD_QUOTA)           return false;
	if(m_fieldCode == FIELD_GBTAGVECTOR)     return false;
	if(m_fieldCode == FIELD_GBGIGABITVECTOR) return false;
	if(m_fieldCode == FIELD_GBSAMPLEVECTOR)  return false;
	if(m_fieldCode == FIELD_GBSECTIONHASH)  return false;
	if(m_fieldCode == FIELD_GBCONTENTHASH)  return false;
	return true;
}



// hash of all the query terms
int64_t Query::getQueryHash() {
	int64_t qh = 0LL;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ )  {
		QueryTerm *qt = &m_qterms[i];
		qh = hash64 ( qt->m_termId , qh );
	}
	return qh;
}

void QueryWord::constructor () {
	m_synWordBuf.constructor();
}

void QueryWord::destructor () {
	m_synWordBuf.purge();
}
