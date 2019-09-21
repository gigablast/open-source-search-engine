#include "gb-include.h"

#include "Matches.h"
#include "Titledb.h"  // for getting total # of docs in db
#include "StopWords.h"
#include "Phrases.h"
#include "Title.h"
#include "CountryCode.h"
#include "Domains.h"
#include "Sections.h"
#include "XmlDoc.h"

//#define DEBUG_MATCHES 1


// TODO: have Matches set itself from all the meta tags, titles, link text,
//       neighborhoods and body. then proximity algo can utilize that info
//       as well as the summary generator, Summary.cpp. right now prox algo
//       was setting all those different classes itself.

// TODO: toss m_tscores. make Summary::getBestWindow() just use its the
//       scores array itself. just access it with Match::m_queryWordNum.

Matches::Matches ( ) {
	m_detectSubPhrases = false;
	m_numMatchGroups = 0;
	m_qwordFlags = NULL;
	m_qwordAllocSize = 0;
	reset();
}
Matches::~Matches( ) { reset(); }
void Matches::reset   ( ) { 
	reset2();
	if ( m_qwordFlags && m_qwordFlags != (mf_t *)m_tmpBuf ) {
		mfree ( m_qwordFlags , m_qwordAllocSize , "mmqw" );
		m_qwordFlags = NULL;
	}
	//m_explicitsMatched = 0;
	//m_matchableRequiredBits = 0;
	//m_hasAllQueryTerms = false;
	//m_matchesQuery = false;
}

void Matches::reset2() {
	m_numMatches = 0;
	//m_maxNQT     = -1;
	m_numAlnums  = 0;
	// free all the classes' buffers
	for ( int32_t i = 0 ; i < m_numMatchGroups ; i++ ) {
		m_wordsArray   [i].reset();
		//m_sectionsArray[i].reset();
		m_posArray     [i].reset();
		m_bitsArray    [i].reset();
	}
	m_numMatchGroups = 0;
}

bool Matches::isMatchableTerm ( QueryTerm *qt ) { // , int32_t i ) {
	// . skip if negative sign
	// . no, we need to match negative words/phrases now so we can
	//   big hack them out...
	//if ( qw->m_wordSign == '-'                     ) return false;
	QueryWord *qw = qt->m_qword;
	// not derived from  a query word? how?
	if ( ! qw ) return false;
	if ( qw->m_ignoreWord == IGNORE_DEFAULT        ) return false;
	if ( qw->m_ignoreWord == IGNORE_FIELDNAME      ) return false;
	if ( qw->m_ignoreWord == IGNORE_BOOLOP         ) return false;
	// stop words in 'all the king's men' query need to be highlighted
	//if ( qw->m_isQueryStopWord && ! qw->m_inQuotes ) return false;
	//if ( qw->m_isStopWord      && ! qw->m_inQuotes ) return false;
	// take this out for now so we highlight for title: terms
	if ( qw->m_fieldCode && qw->m_fieldCode != FIELD_TITLE ) return false;
	// what word # are we?
	int32_t qwn = qw - m_q->m_qwords;
	// do not include if in a quote and does not start it!!
	//if ( qw->m_inQuotes && i-1 != qw->m_quoteStart ) return false;
	if ( qw->m_quoteStart >= 0 && qw->m_quoteStart != qwn ) return false;
	// if query is too long, a query word can be truncated!
	// this happens for some words if they are ignored, too!
	if ( ! qw->m_queryWordTerm && ! qw->m_queryPhraseTerm ) return false;
	// after a NOT operator?
	if ( qw->m_underNOT ) 
		return false;
	// in a field?
	//if ( qw->m_fieldCode != fieldCode ) continue;
	// skip if a query stop word w/o a sign and ignored
	//if ( q->m_isStopWord[i] &&
	//     q->m_termSigns[i] == '\0' &&
	//     q->m_ignore[i] ) continue;
	return true;
}

// a QueryMatch is a quote in the query or a single word.
class QueryMatch {
public:
	// range in Query::m_qwords [m_a,m_b]
	int32_t m_a;
	int32_t m_b;
	int32_t m_score; // lowest of the term freqs
};

void Matches::setQuery ( Query *q ) { 
	//int32_t    qtableScores   [ MAX_QUERY_TERMS * 2 ];
	reset();
	// save it
	m_q       = q;
	//m_tscores = tscores; // scores, 1-1 with query terms
	//m_numNegTerms = 0; 
	//m_explicitsMatched = 0;
	// clear this vector
	//memset ( m_foundTermVector , 0 , m_q->getNumTerms() );

	//memset ( m_foundNegTermVector, 0, m_q->getNumTerms() );

	if ( m_qwordFlags ) { char *xx=NULL;*xx=0; }

	int32_t need = m_q->m_numWords * sizeof(mf_t) ;
	m_qwordAllocSize = need;
	if ( need < 128 ) 
		m_qwordFlags = (mf_t *)m_tmpBuf;
	else
		m_qwordFlags = (mf_t *)mmalloc ( need , "mmqf" );

	if ( ! m_qwordFlags ) {
		log("matches: alloc failed for query %s",q->m_orig);
		return;
	}

	// this is word based. these are each 1 byte
	memset ( m_qwordFlags  , 0 , m_q->m_numWords * sizeof(mf_t));

	// # of WORDS in the query
	int32_t nqt = m_q->m_numTerms;

	// how many query words do we have that can be matched?
	int32_t numToMatch = 0;
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		// rest this
		//m_qwordFlags[i] = 0;
		// get query word #i
		//QueryWord *qw = &m_q->m_qwords[i];
		QueryTerm *qt = &m_q->m_qterms[i];
		// skip if ignored *in certain ways only*
		if ( ! isMatchableTerm ( qt ) ) {
			//if( (qw->m_wordSign == '-') && !qw->m_fieldCode )
			//	m_numNegTerms++;
			continue;
		}
		// count it
		numToMatch++;
		// don't breach. MDW: i made this >= from > (2/11/09)
		if ( numToMatch < MAX_QUERY_WORDS_TO_MATCH ) continue;
		// note it
		log("matches: hit %"INT32" max query words to match limit",
		    (int32_t)MAX_QUERY_WORDS_TO_MATCH);
		break;
	}

	// fix a core the hack way for now!
	if ( numToMatch < 256 ) numToMatch = 256;

	// keep number of slots in hash table a power of two for fast hashing
	m_numSlots = getHighestLitBitValue ( (uint32_t)(numToMatch * 3));
	// make the hash mask
	uint32_t mask = m_numSlots - 1;
	int32_t          n;
	// sanity check
	if ( m_numSlots > MAX_QUERY_WORDS_TO_MATCH * 3 ) {
		char *xx = NULL; *xx = 0; }

	// clear hash table
	memset ( m_qtableIds   , 0 , m_numSlots * 8 );
	memset ( m_qtableFlags , 0 , m_numSlots     );
	//memset ( m_qtableNegIds, 0 , m_numNegTerms  );

	// alternate colors for highlighting
	int32_t colorNum = 0;

	//int32_t negIds = 0;
	// . hash all the query terms into the hash table
	// . the term's score should be 100 for a very rare term,
	//   and 1 for a stop word.
	//m_maxNQT = nqt;
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		// get query word #i
		//QueryWord *qw = &m_q->m_qwords[i];
		QueryTerm *qt = &m_q->m_qterms[i];
		// skip if ignored *in certain ways only*
		if ( ! isMatchableTerm ( qt ) ) {
			//if( (qw->m_wordSign == '-') && !qw->m_fieldCode )
			//	m_qtableNegIds[negIds++] = qw->m_rawWordId;
			continue;
		}
		// get the word it is from
		QueryWord *qw = qt->m_qword;
		// get word #
		int32_t qwn = qw - q->m_qwords;
		// assign color # for term highlighting with different colors
		qw->m_colorNum = colorNum++;
		// do not overfill table
		if ( colorNum > MAX_QUERY_WORDS_TO_MATCH ) {
			//m_maxNQT = nqt; 
			break; 
		}
		// this should be equivalent to the word id
		int64_t qid = qt->m_rawTermId;//qw->m_rawWordId;

		// but NOT for 'cheatcodes.com'
		if ( qt->m_isPhrase ) qid = qw->m_rawWordId;

		// if its a multi-word synonym, like "new jersey" we must
		// index the individual words... or compute the phrase ids
		// for all the words in the doc. right now the qid is
		// the phrase hash for this guy i think...
		if ( qt->m_synonymOf && qt->m_numAlnumWordsInSynonym == 2 )
			qid = qt->m_synWids0;

		// put in hash table
		n = ((uint32_t)qid) & mask;
		// chain to an empty slot
		while ( m_qtableIds[n] && m_qtableIds[n] != qid ) 
			if ( ++n >= m_numSlots ) n = 0;
		// . if already occupied, do not overwrite this, keep this
		//   first word, the other is often ignored as IGNORE_REPEAT
		// . what word # in the query are we. save this.
		if ( ! m_qtableIds[n] ) m_qtableWordNums[n] = qwn;
		// store it
		m_qtableIds[n] = qid;
		// in quotes? this term may appear multiple times in the
		// query, in some cases in quotes, and in some cases not.
		// we need to know either way for logic below.
		if ( qw->m_inQuotes ) m_qtableFlags[n] |= 0x02;
		else                  m_qtableFlags[n] |= 0x01;

		// this is basically a quoted synonym
		if ( qt->m_numAlnumWordsInSynonym == 2 )
			m_qtableFlags[n] |=  0x08;

		//QueryTerm *qt = qw->m_queryWordTerm;
		if ( qt && qt->m_termSign == '+' ) m_qtableFlags[n] |= 0x04;

		//
		// if query has e-mail, then index phrase id "email" so
		// it matches "email" in the doc.
		// we need this for the 'cheat codes' query as well so it
		// highlights 'cheatcodes'
		//
		int64_t pid = qw->m_rawPhraseId;
		if ( pid == 0 ) continue;
		// put in hash table
		n = ((uint32_t)pid) & mask;
		// chain to an empty slot
		while ( m_qtableIds[n] && m_qtableIds[n] != pid ) 
			if ( ++n >= m_numSlots ) n = 0;
		// this too?
		if ( ! m_qtableIds[n] ) m_qtableWordNums[n] = qwn;
		// store it
		m_qtableIds[n] = pid;

	}

	/*
	// set what bits we need to match
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		// get it
		QueryTerm *qt = &m_q->m_qterms[i];			
		// get its explicit bit
		qvec_t ebit = qt->m_explicitBit;
		// must be a required term
		if ( (m_q->m_matchRequiredBits & ebit) == 0 ) continue;
		// we only check for certain fields in this logic right now
		bool skip = true;
		// if no field, must match it
		if ( qt->m_fieldCode == 0               ) skip = false;
		if ( qt->m_fieldCode == FIELD_GBLANG    ) skip = false;
		if ( qt->m_fieldCode == FIELD_GBCOUNTRY ) skip = false;
		if ( qt->m_fieldCode == FIELD_SITE      ) skip = false;
		if ( qt->m_fieldCode == FIELD_IP        ) skip = false;
		if ( qt->m_fieldCode == FIELD_URL       ) skip = false;
		if ( skip ) continue;

		// we need this ebit
		m_matchableRequiredBits |= ebit;
	}
	*/
}

// . this was in Summary.cpp, but is more useful here
// . we can also use this to replace the proximity algo setup where it
//   fills in the matrix for title, link text, etc.
// . returns false and sets g_errno on error
bool Matches::set ( XmlDoc   *xd         ,
		    Words    *bodyWords  ,
		    //Synonyms *bodySynonyms,
		    Phrases  *bodyPhrases ,
		    Sections *bodySections ,
		    Bits     *bodyBits   ,
		    Pos      *bodyPos    ,
		    Xml      *bodyXml    ,
		    Title    *tt         ,
		    int32_t      niceness   ) {

	// don't reset query info!
	reset2();

	// sanity check
	if ( ! xd->m_docIdValid ) { char *xx=NULL;*xx=0; }

	// . first add all the matches in the body of the doc
	// . add it first since it will kick out early if too many matches
	//   and we get all the explicit bits matched
	if ( ! addMatches ( bodyWords ,
			    //bodySynonyms ,
			    bodyPhrases ,
			    bodySections ,
			    //addToMatches ,
			    bodyBits   ,
			    bodyPos    ,
			    0          , // fieldCode of words, 0 for no field
			    true       , // allowPunctInPhrase,
			    false      , // exclQTOnlyinAnchTxt,
			    0          , // qvec_t  reqMask      ,
			    0          , // qvec_t  negMask      ,
			    1          , // int32_t    diversityWeight,
			    xd->m_docId,
			    MF_BODY        ) )
		return false;

	// add the title in
	if ( ! addMatches ( tt->getTitle()      , 
			    tt->getTitleLen()  ,
			    MF_TITLEGEN         ,
			    xd->m_docId         ,
			    niceness            ))
		return false;

	// add in the url terms
	Url  *turl = xd->getFirstUrl();
	if ( ! addMatches ( turl->m_url ,
			    turl->m_ulen ,
			    MF_URL ,
			    xd->m_docId ,
			    niceness ) )
		return false;

	// also use the title from the title tag, because sometimes 
	// it does not equal "tt->getTitle()"
	int32_t  a     = tt->m_titleTagStart;
	int32_t  b     = tt->m_titleTagEnd;
	char *start = NULL;
	char *end   = NULL;
	if ( a >= 0 && b >= 0 && b>a ) {
		start = bodyWords->getWord(a);
		end   = bodyWords->getWord(b-1) + bodyWords->getWordLen(b-1);
		if ( ! addMatches ( start           ,
				    end - start     ,
				    MF_TITLETAG     ,
				    xd->m_docId     ,
				    niceness        ))
			return false;
	}

	// add in dmoz stuff
	char *dt     = xd->ptr_dmozTitles;
	char *ds     = xd->ptr_dmozSumms;
	int32_t  nd     = xd->size_catIds / 4;
	for ( int32_t i = 0 ; i < nd ; i++ ) {
		// sanity check
		if ( ! dt[0] ) break;
		// add each dmoz title
		if ( ! addMatches ( dt           ,
				    gbstrlen(dt)   ,
				    MF_DMOZTITLE ,
				    xd->m_docId  ,
				    niceness     ))
			return false;
		// skip
		dt += gbstrlen(dt) + 1;
		// sanity check
		if ( ! ds[0] ) break;
		// and the summary
		if ( ! addMatches ( ds           ,
				    gbstrlen(ds)   ,
				    MF_DMOZSUMM  ,
				    xd->m_docId  ,
				    niceness     ))
			return false;
		// skip
		ds += gbstrlen(ds) + 1;
	}

	// now add in the meta tags
	int32_t     n     = bodyXml->getNumNodes();
	XmlNode *nodes = bodyXml->getNodes();
	// find the first meta summary node
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// continue if not a meta tag
		if ( nodes[i].m_nodeId != 68 ) continue;
		// only get content for <meta name=..> not <meta http-equiv=..>
		int32_t tagLen;
		char *tag = bodyXml->getString ( i , "name" , &tagLen );
		// is it an accepted meta tag?
		int32_t flag = 0;
		if (tagLen== 7&&strncasecmp(tag,"keyword"    , 7)== 0)
			flag = MF_METAKEYW;
		if (tagLen== 7&&strncasecmp(tag,"summary"    , 7)== 0)
			flag = MF_METASUMM;
		if (tagLen== 8&&strncasecmp(tag,"keywords"   , 8)== 0)
			flag = MF_METAKEYW;
		if (tagLen==11&&strncasecmp(tag,"description",11)== 0)
			flag = MF_METADESC;
		if ( ! flag ) continue;
		// get the content
		int32_t len;
		char *s = bodyXml->getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) continue;
		// wordify
		if ( ! addMatches ( s                , 
				    len              ,
				    flag             ,
				    xd->m_docId      ,
				    niceness         ) )
			return false;
	}

	// . now the link text
	// . loop through each link text and it its matches
	LinkInfo *info = xd->getLinkInfo1();	
	// this is not the second pass, it is the first pass
	bool secondPass = false;
 loop:
	// loop through the Inlinks
	Inlink *k = NULL;
	for ( ; (k = info->getNextInlink(k)) ; ) {
		// does it have link text? skip if not.
		if ( k->size_linkText <= 1 ) continue;
		// set the flag, the type of match
		mf_t flags = MF_LINK;
		//if ( k->m_isAnomaly ) flags = MF_ALINK;
		// add it in
		if ( ! addMatches ( k->getLinkText() ,
				    k->size_linkText - 1 ,
				    flags                ,
				    xd->m_docId          ,
				    niceness             ))
			return false;

		// skip if no neighborhood text
		//if ( k->size_surroundingText <= 1 ) continue;

		// set flag for that
		flags = MF_HOOD;
		//if ( k->m_isAnomaly ) flags = MF_AHOOD;
		// add it in
		if ( ! addMatches ( k->getSurroundingText() ,
				    k->size_surroundingText - 1 ,
				    flags                       ,
				    xd->m_docId                 ,
				    niceness                    ))
			return false;

		// parse the rss up into xml
		Xml rxml;
		if ( ! k->setXmlFromRSS ( &rxml , niceness ) ) return false;

		// add rss description
		bool isHtmlEncoded;
		int32_t rdlen;
		char *rd = rxml.getRSSDescription ( &rdlen , &isHtmlEncoded );
		if ( ! addMatches ( rd               ,
				    rdlen            ,
				    MF_RSSDESC       ,
				    xd->m_docId      ,
				    niceness         ))
			return false;

		// add rss title
		int32_t rtlen;
		char *rt = rxml.getRSSTitle       ( &rtlen , &isHtmlEncoded );
		if ( ! addMatches ( rt               ,
				    rtlen            ,
				    MF_RSSTITLE      ,
				    xd->m_docId      ,
				    niceness         ))
			return false;
	}

	// now repeat for imported link text!
	if ( ! secondPass ) {
		// only do this once
		secondPass = true;
		// set it
		info = *xd->getLinkInfo2();
		if ( info ) goto loop;
	}

	/*
	// convenience
	Query *q = m_q;

	// any error we have will be this
	g_errno = EMISSINGQUERYTERMS;

	// . add in match bits from query!
	// . used for the BIG HACK
	for( int32_t i = 0; i < q->m_numTerms ; i++ ) {
		// get it
		QueryTerm *qt = &q->m_qterms[i];			

		bool   isNeg = qt->m_termSign == '-';
		qvec_t ebit  = qt->m_explicitBit;
		// save it
		int32_t fc = qt->m_fieldCode;
		// . length stops at space for fielded terms
		// . get word
		QueryWord *w = qt->m_qword;
		// get word index
		int32_t wi = w - q->m_qwords;
		// point to word
		char *qw = q->m_qwords[wi].m_word;
		// total length
		int32_t qwLen = 0;
		// keep including more words until not in field anymore
		for ( ; wi < q->m_numWords ; wi++ ) {
			if ( q->m_qwords[wi].m_fieldCode != fc ) break;
			// include its length
			qwLen += q->m_qwords[wi].m_wordLen;
		}
		if( !qw || !qwLen )
			return log( "query: Error, no query word found!" );
		char tmp[512];
		//int32_t tmpLen;
		//tmpLen = utf16ToUtf8( tmp, 512, qw, qwLen );
		int32_t tmpLen = qwLen;
		if ( tmpLen > 500 ) tmpLen = 500;
		gbmemcpy ( tmp , qw , tmpLen );
		tmp[tmpLen] = '\0';
		log(LOG_DEBUG,"query: term#=%"INT32" fieldLen=%"INT32":%s",i,tmpLen,tmp);

		if ( fc == FIELD_GBLANG ) {
			char lang = atoi( tmp );
			log( LOG_DEBUG, "query: TitleRec "
			     "Lang=%i", *xd->getLangId() );
			if( q->m_isBoolean ) {
				if (*xd->getLangId() == lang) 
					m_explicitsMatched |= ebit;
				continue;
			}
			if ( isNeg && (*xd->getLangId() == lang)){
				if( q->m_hasUOR ) continue;
				return log("query: Result contains "
					   "-gblang: term, filtering. "
					   " q=%s", q->m_orig);
			}
			else if(   !isNeg 
				   && (*xd->getLangId() != lang)){
				if( q->m_hasUOR ) continue;
				return log("query: Result is missing "
					   "gblang: term, filtering. "
					   "q=%s", q->m_orig);
			}
			else 
				m_explicitsMatched |= ebit;
		}

		else if ( fc == FIELD_GBCOUNTRY ) {
			unsigned char country ;
			country = g_countryCode.getIndexOfAbbr(tmp);
			log( LOG_DEBUG, "query: TitleRec "
			     "Country=%i", *xd->getCountryId() );
			if ( q->m_isBoolean ) {
				if ( *xd->getCountryId() == country) 
					m_explicitsMatched |= ebit;
				continue;
			}
			if ( isNeg && (*xd->getCountryId() == country)){
				if( q->m_hasUOR ) continue;
				return log("query: Result contains "
				    "-gbcountry: term, filtering. "
				    " q=%s", q->m_orig);
			}
			else if ( !isNeg  && (*xd->getCountryId() != country)){
				if( q->m_hasUOR ) continue;
				return log("query: Result is missing "
				    "gbcountry: term, filtering. "
				    "q=%s", q->m_orig);
			}
			else 
				m_explicitsMatched |= ebit;
		}
		else if( fc == FIELD_SITE ) {
			// . Site Colon Field Terms:
			//   1.) match tld first (if only tld)
			//   2.) match domain (contains tld) 
			//   3.) match host (sub-domain)
			//   4.) match path
			//   * 1 is the minimal specificity for
			//     a site: query.  2,3, and 4 are 
			//     only required if specified in 
			//     query
			bool  fail = false;
			Url  *turl   = xd->getFirstUrl();
			char *ttld   = turl->getTLD();
			int32_t  ttlen  = turl->getTLDLen();
			char *tdom   = turl->getDomain();
			int32_t  tdlen  = turl->getDomainLen();
			char *thost  = turl->getHost();
			int32_t  thlen  = turl->getHostLen();
			char *tpath  = turl->getPath();
			int32_t  tplen  = turl->getPathLen();
			//bool  hasWWW = turl->isHostWWW();
			log( LOG_DEBUG, "query: TitleRec "
			     "Site=%s", tdom );
			// . Check to see if site: is querying 
			//   only a TLD, then we can't put it
			//   into Url.
			if(isTLD(tmp, tmpLen)) {
				if(ttlen != tmpLen ||
				   strncmp(ttld, tmp, tmpLen))
					fail = true;
			}
			else {
				Url qurl;
				// false --> add www?
				qurl.set( tmp, tmpLen, false);//hasWWW );
				char *qdom  = qurl.getDomain();
				int32_t  qdlen = qurl.
					getDomainLen();
				char *qhost = qurl.getHost();
				int32_t  qhlen = qurl.getHostLen();
				char *qpath = qurl.getPath();
				int32_t  qplen = qurl.getPathLen();
				
				if(tdlen != qdlen || 
				   strncmp(tdom, qdom, qdlen))
					fail = true;
				if(!fail && 
				   qhlen != qdlen && 
				   (thlen != qhlen ||
				    strncmp(thost, 
					    qhost, qhlen)))
					fail = true;
				if(!fail && qplen > 1 && 
				   (tplen < qplen ||
				    strncmp(tpath, 
					    qpath, qplen)))
					fail = true;
			}
			if( q->m_isBoolean){
				if ( ! fail ) 
					m_explicitsMatched |= ebit;
				continue;
			}
		
			if( fail && !isNeg ){
				if( q->m_hasUOR ) continue;
				return log("query: Result is missing "
					   "site: term, filtering. " 
					   "q=%s", q->m_orig);
			}
			else if( !fail && isNeg ){
				if( q->m_hasUOR ) continue;
				return log("query: Result contains "
					   "-site: term, filtering. "
					   "q=%s", q->m_orig );
			}
			else 
				m_explicitsMatched |= ebit;
		}
		else if ( fc == FIELD_IP ) {
			int32_t  ip = *xd->getIp();
			char *oip = iptoa( ip );
			log(LOG_DEBUG, "query: TitleRec Ip=%s", oip );
			int32_t olen = gbstrlen(oip);
			bool matched = false;
			if (olen>=tmpLen && strncmp(oip,tmp,tmpLen)==0 )
				matched = true;
			if( q->m_isBoolean){
				if (matched) m_explicitsMatched |= ebit;
				continue;
			}
			if ( ! matched && ! isNeg ) {
				if( q->m_hasUOR ) continue;
				return log("query: Result is missing ip: term,"
					   " filtering. q=%s", q->m_orig );
			}
			else if ( matched && isNeg ) {
				if( q->m_hasUOR ) continue;
				return log("query: Result contains -ip: term, "
					   "filtering. q=%s", q->m_orig );
			}
			else 
				m_explicitsMatched |= ebit;
		}

		else if ( fc == FIELD_URL ) {
			char *url  = xd->getFirstUrl()->getUrl();
			int32_t  slen = xd->getFirstUrl()->getUrlLen();
			Url u;
			// do not force add the "www." cuz titleRec does not
			u.set( tmp, tmpLen, false );//true );
			char * qs  = u.getUrl();
			int32_t   qsl = u.getUrlLen();
			log( LOG_DEBUG, "query: TitleRec Url=%s",  url );
			if( qsl > slen ) qsl = slen;
			int32_t result = strncmp( url, qs, qsl );
			if( q->m_isBoolean){
				if (result) 
					m_explicitsMatched |= ebit;
				continue;
			}
			if( result && !isNeg ){
				if( q->m_hasUOR ) continue;
				return log("query: Result is missing "
					   "url: term, filtering. q=%s",
					   q->m_orig );
			}
			else if( !result && isNeg ){
				if( q->m_hasUOR ) continue;
				return log("query: Result contains "
					   "-url: term, filtering. "
					   "q=%s", q->m_orig );
			}
			else 
				m_explicitsMatched |= ebit;
		}

	}
	
	// clear just in case
	g_errno = 0;

	// what bits are not matchable
	qvec_t unmatchable = m_q->m_matchRequiredBits -m_matchableRequiredBits;
	// modify what we got
	qvec_t matched = m_explicitsMatched | unmatchable;
	// need to set Query::m_bmap before calling getBitScore()
	if ( ! m_q->m_bmapIsSet ) m_q->setBitMap();
	// if boolean, do the truth table
	int32_t bitScore = m_q->getBitScore ( matched );
	// assume we are missing some. if false, may still be in the results
	// if we have rat=0 (Require All Terms = false)
	m_hasAllQueryTerms = false;
	// assume not a match. if this is false big hack excludes from results
	m_matchesQuery = false;
	// see Query.h for these bits defined. do not include 0x80 because
	// we may not have any forced bits...
	if ( bitScore & (0x20|0x40) ) m_matchesQuery = true;
	// it may not have all the query terms because of rat=0
	if ( (matched & m_q->m_matchRequiredBits)== m_q->m_matchRequiredBits ){
		m_hasAllQueryTerms = true;
		m_matchesQuery     = true;
	}
	*/

	// that should be it
	return true;
}

bool Matches::addMatches ( char      *s         ,
			   int32_t       slen      ,
			   mf_t       flags     ,
			   int64_t  docId     ,
			   int32_t       niceness  ) {

	// . do not breach
	// . happens a lot with a lot of link info text
	if ( m_numMatchGroups >= MAX_MATCHGROUPS ) {
		// . log it
		// . often we have a ton of inlink text!!
		//log("matches: could not add matches1 for docid=%"INT64" because "
		//    "already have %"INT32" matchgroups",docId,
		//    (int32_t)MAX_MATCHGROUPS);
		return true;
	}

	// get some new ptrs for this match group
	Words    *wp = &m_wordsArray    [ m_numMatchGroups ];
	//Sections *sp = &m_sectionsArray [ m_numMatchGroups ];
	Sections *sp = NULL;
	Bits     *bp = &m_bitsArray     [ m_numMatchGroups ];
	Pos      *pb = &m_posArray      [ m_numMatchGroups ];

	// set the words class for this match group
	if ( ! wp->set ( s                        ,
			 slen                     , // in bytes
			 TITLEREC_CURRENT_VERSION ,
			 true                     , // computeIds?
			 niceness                 ))
		return false;

	// scores vector
	//if ( ! sp->set ( wp , TITLEREC_CURRENT_VERSION , false ) )
	//	return false;
	// bits vector
	if ( ! bp->setForSummary ( wp ) )
		return false;
	// position vector
	if ( ! pb->set ( wp , sp ) )
		return false;
	// record the start
	int32_t startNumMatches = m_numMatches;
	// sometimes it returns true w/o incrementing this
	int32_t n = m_numMatchGroups;
	// . add all the Match classes from this match group
	// . this increments m_numMatchGroups on success
	bool status = addMatches ( wp   ,
				   //NULL , // synonyms
				   NULL , // phrases
				   sp   ,
				   //true , // addToMatches
				   bp   , // bits
				   pb   , // pos
				   0    , // fieldCode
				   true , // allowPunctInPhrase?
				   false , // excludeQTOnlyInAnchTxt?
				   0     , // reqMask
				   0     , // negMask
				   1     , // diversityWeight
				   docId ,
				   flags );// docId

	// if this matchgroup had some, matches, then keep it
	if ( m_numMatches > startNumMatches ) return status;
	// otherwise, reset it, useless
	wp->reset();
	if ( sp ) sp->reset();
	bp->reset();
	pb->reset();
	// do not decrement the counter if we never incremented it
	if ( n == m_numMatchGroups ) return status;
	// ok, remove it
	m_numMatchGroups--;
	return status;
}

bool Matches::getMatchGroup ( mf_t       matchFlag ,
			      Words    **wp        ,
			      Pos      **pp        ,
			      Sections **sp        ) {

	for ( int32_t i = 0 ; i < m_numMatchGroups ; i++ ) {
		// must be the type we want
		if ( m_flags[i] != matchFlag ) continue;
		// get it
		*wp = &m_wordsArray    [i];
		*pp = &m_posArray      [i];
		//*sp = &m_sectionsArray [i];
		*sp = NULL;
		return true;
	}
	// not found
	return false;
}

// . TODO: support stemming later. each word should then have multiple ids.
// . add to our m_matches[] array iff addToMatches is true, otherwise we just
//   set the m_foundTermVector for doing the BIG HACK described in Summary.cpp
bool Matches::addMatches ( Words    *words               ,
			   //Synonyms *syn                 ,
			   Phrases  *phrases             ,
			   Sections *sections            ,
			   Bits     *bits                ,
			   Pos      *pos                 ,
			   int32_t      fieldCode           , // of words,0=none
			   bool      allowPunctInPhrase  ,
			   bool      exclQTOnlyinAnchTxt ,
			   qvec_t    reqMask             ,
			   qvec_t    negMask             ,
			   int32_t      diversityWeight     ,
			   int64_t docId               ,
			   mf_t      flags               ) { 

	// if no query term, bail.
	if ( m_numSlots <= 0 ) return true;

	// . do not breach
	// . happens a lot with a lot of link info text
	if ( m_numMatchGroups >= MAX_MATCHGROUPS ) {
		// . log it
		// . often we have a ton of inlink text!!
		//log("matches: could not add matches2 for docid=%"INT64" because "
		//    "already have %"INT32" matchgroups",docId,
		//    (int32_t)MAX_MATCHGROUPS);
		return true;
	}

	// int16_tcut
	Section *sp = NULL;
	if ( sections ) sp = sections->m_sections;

	// we've added a lot of matches, if we don't need anymore
	// to confirm the big hack then break out
	//if ( m_numMatches >= MAX_MATCHES &&
	//     ( m_explicitsMatched & m_matchableRequiredBits ) ) 
	//	return true;

	mf_t eflag = 0;

	// set the ptrs
	m_wordsPtr    [ m_numMatchGroups ] = words;
	m_sectionsPtr [ m_numMatchGroups ] = sections;
	m_bitsPtr     [ m_numMatchGroups ] = bits;
	m_posPtr      [ m_numMatchGroups ] = pos;
	m_flags       [ m_numMatchGroups ] = flags;
	m_numMatchGroups++;

	int64_t *pids = NULL;
	if ( phrases ) pids = phrases->getPhraseIds2();

	// set convenience vars
	uint32_t  mask    = m_numSlots - 1;
	int64_t     *wids    = words->getWordIds();
	int32_t          *wlens   = words->getWordLens();
	char         **wptrs   = words->getWords();
	// swids = word ids where accent marks, etc. are stripped 
	//int64_t     *swids   = words->getStripWordIds();
	nodeid_t      *tids    = words->getTagIds();
	int32_t           nw      = words->m_numWords;
	//int32_t          *wscores = NULL;
	//if ( scores )  wscores = scores->m_scores;
	int32_t           n;//,n2 ;
	int32_t           matchStack = 0;
	int64_t      nextMatchWordIdMustBeThis = 0;
	int32_t           nextMatchWordPos = 0;
	int32_t           lasti   = -3;
	//bool           inAnchTag = false;

	int32_t dist = 0;

	// . every tag increments "dist" by a value
	// . rather than use a switch/case statement, which does a binary
	//   lookup thing which is really slow, let's use a 256 bucket table
	//   for constant lookup, rather than log(N).
	static char   s_tableInit = false;
	static int8_t s_tab[512];
	if ( getNumXmlNodes() > 512 ) { char *xx=NULL;*xx=0; }
	for ( int32_t i = 0 ; ! s_tableInit && i < 128 ; i++ ) {
		char step = 0;
		if ( i == TAG_TR    ) step = 2;
		if ( i == TAG_P     ) step = 10;
		if ( i == TAG_HR    ) step = 10;
		if ( i == TAG_H1    ) step = 10;
		if ( i == TAG_H2    ) step = 10;
		if ( i == TAG_H3    ) step = 10;
		if ( i == TAG_H4    ) step = 10;
		if ( i == TAG_H5    ) step = 10;
		if ( i == TAG_H6    ) step = 10;
		if ( i == TAG_TABLE ) step = 30;
		if ( i == TAG_BLOCKQUOTE ) step = 10;
		// default
		if ( step == 0 ) {
			if ( g_nodes[i].m_isBreaking ) step = 10;
			else                           step = 1;
		}
		// account for both the back and the front tags
		s_tab[i     ] = step;
		//s_tab[i|0x80] = step;
	}
	s_tableInit = true;

	// google seems to index SEC_MARQUEE so i took that out of here
	int32_t badFlags =SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_IN_TITLE;

	//int32_t anum;
	//int64_t *aids;
	//int32_t j;
	int32_t qwn;
	int32_t numQWords;
	int32_t numWords;

	//
	// . set m_matches[] array
	// . loop over all words in the document
	//
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		//if      (tids && (tids[i]            ) == TAG_A) 
		//	inAnchTag = true;
		//else if (tids && (tids[i]&BACKBITCOMP) == TAG_A) 
		//	inAnchTag = false;

		// for each word increment distance
		dist++;

		//if ( addToMatches && tids && tids[i] ){
		if ( tids && tids[i] ){
			int32_t tid = tids[i] & BACKBITCOMP;
			// accumulate distance
			dist += s_tab[tid];
			// monitor boundaries so that the proximity algo
			// knows when two matches are separated by such tags
			// MDW: isn't the "dist" good enough for this?????
			// let's try just using "dist" then.
			// "crossedSection" is hereby replaced by "dist".
			//if ( s_tab[tid]
			// tagIds don't have wids and are skipped
			continue;
		}

		// skip if wid is 0, it is not an alnum word then
		if ( ! wids[i] ) {
			// and extra unit if it starts with \n i guess
			if ( words->m_words[i][0] == '\n' ) dist++;
			//	dist += words->m_wordLens[i] / 3;
			continue;
		}

		// count the number of alnum words
		m_numAlnums++;

		// clear this
		eflag = 0;

		// . zero score words cannot match query terms either
		// . BUT if score is -1 that means it is in a <select> or
		//   a <marquee> tag (see Scores.cpp)
		// . FIX: neg word terms cannot be in quotes!!
		//for( int32_t j = 0; j < m_numNegTerms; j++ ) {
		//	if(     wids[i] == m_qtableNegIds[j] 
		//	    || swids[i] == m_qtableNegIds[j] ) 
		//		m_foundNegTermVector[j] = 1;
		//}

		// NO NO, a score of -1 means in a select tag, and
		// we do index that!! so only skip if wscores is 0 now.
		// -1 means in script, style, select or marquee. it is
		// indexed but with very little weight... this is really
		// a hack in Scores.cpp and should be fixed.
		// in Scores.cpp we set even the select tag stuff to -1...
		//if ( wscores && wscores[i] == -1 ) continue;
		if ( sp && (sp->m_flags & badFlags) ) continue;


		// . does it match a query term?
		// . hash to the slot in the hash table
		n = ((uint32_t)wids[i]) & mask;
		//n2 = swids[i]?((uint32_t)swids[i]) & mask:n;
	chain1:
		// skip if slot is empty (doesn't match query term)
		//if ( ! m_qtableIds[n] && ! m_qtableIds[n2]) continue;
		if ( ! m_qtableIds[n] ) goto tryPhrase;
		// otherwise chain
		if ( (m_qtableIds[n] != wids[i]) ) {
			if ( m_qtableIds[n] && ++n >= m_numSlots ) n = 0;
			goto chain1;
		}
		// we got one!
		goto gotMatch;


		//
		// fix so we hihglight "woman's" when query term is "woman"
		// for 'spiritual books for women' query
		// 
	tryPhrase:
		// try without 's if it had it
		if ( wlens[i] >= 3 && 
		     wptrs[i][wlens[i]-2] == '\'' &&
		     to_lower_a(wptrs[i][wlens[i]-1]) == 's' ) {
			// move 's from word hash... very tricky
			int64_t nwid = wids[i];
			// undo hash64Lower_utf8 in hash.h
			nwid ^= g_hashtab[wlens[i]-1][(uint8_t)'s'];
			nwid ^= g_hashtab[wlens[i]-2][(uint8_t)'\''];
			n = ((uint32_t)nwid) & mask;
		chain2:
			if ( ! m_qtableIds[n] ) goto tryPhrase2;
			if ( (m_qtableIds[n] != nwid) ) {
				if ( m_qtableIds[n] && ++n >= m_numSlots ) n=0;
				goto chain2;
			}
			qwn = m_qtableWordNums[n];
			numWords = 1;
			numQWords = 1;
			// we got one!
			goto gotMatch2;
		}

	tryPhrase2:
		// try phrase first
		if ( pids && pids[i] ) {
			n = ((uint32_t)pids[i]) & mask;
		chain3:
			if ( ! m_qtableIds[n] ) continue;
			if ( (m_qtableIds[n] != pids[i]) ) {
				if ( m_qtableIds[n] && ++n >= m_numSlots)n = 0;
				goto chain3;
			}
			// what query word # do we match?
			qwn = m_qtableWordNums[n];
			// get that query word #
			QueryWord *qw = &m_q->m_qwords[qwn];
			// . do we match it as a single word?
			// . did they search for "bluetribe" ...?
			if ( qw->m_rawWordId == pids[i] ) {
				// set our # of words basically to 3
				numWords = 3;
				// matching a single query word
				numQWords = 1;
				// got a match
				goto gotMatch2;
			}
			if ( qw->m_phraseId == pids[i] ) {
				// might match more if we had more query
				// terms in the quote
				numWords = getNumWordsInMatch( words, 
							       i, 
							       n, 
							       &numQWords, 
							       &qwn, 
							 allowPunctInPhrase );
				// this is 0 if we were an unmatched quote
				if ( numWords <= 0 ) continue;
				// we matched a bigram in the document
				//numWords = 3;
				// i guess we matched the query phrase bigram
				//numQWords = 3;
				// got a match
				goto gotMatch2;
			}
			// otherwise we are matching a query phrase id
			log("matches: wtf? query word not matched for "
			    "highlighting... strange.");
			// assume one word for now
			numWords = 1;
			numQWords = 1;
			goto gotMatch2;
			//char *xx=NULL;*xx=0;
		}

		//
		// shucks, no match
		//
		continue;

	gotMatch:
		// what query word # do we match?
		qwn = m_qtableWordNums[n];
		

		// . how many words are in this match?
		// . it may match a single word or a phrase or both
		// . this will be 1 for just matching a single word, and 
		//   multiple words for quotes/phrases. The number of words
		//   in both cases will included unmatched punctuation words
		//   and tags in between matching words.
		numQWords = 0;
		numWords = getNumWordsInMatch( words, i, n, &numQWords, 
					       &qwn, allowPunctInPhrase );
		// this is 0 if we were an unmatched quote
		if ( numWords <= 0 ) continue;

	gotMatch2:
		// get query word
		QueryWord *qw = &m_q->m_qwords[qwn];
		// point to next word in the query
		QueryWord *nq = NULL;
		if ( qwn+2 < m_q->m_numWords ) nq = &m_q->m_qwords[qwn+2];

		// . if only one word matches and its a stop word, make sure
		//   it's next to the correct words in the query
		// . if phraseId is 0, that means we do not start a phrase,
		//   because stop words can start phrases if they are the
		//   first word, are capitalized, or have breaking punct before
		//   them.
		if ( numWords == 1 && 
		     ! qw->m_inQuotes &&
		     m_q->m_numWords > 2 &&
		     qw->m_wordSign == '\0' &&
		     (nq && nq->m_wordId) && // no field names can follow
		     //(qw->m_isQueryStopWord || qw->m_isStopWord ) ) {
		     // we no longer consider single alnum chars to be
		     // query stop words as stated in StopWords.cpp to fix
		     // the query 'j. w. eagan'
		     qw->m_isQueryStopWord ) {
			// if stop word does not start a phrase in the query 
			// then he must have a matched word before him in the
			// document. if he doesn't then do not count as a match
			if ( qw->m_phraseId == 0LL && i-2 != lasti ) {
				// peel off anybody before us
				m_numMatches -= matchStack;
				if ( m_numMatches < 0 ) m_numMatches = 0;
				// don't forget to reset the match stack
				matchStack = 0;	

				/*
				//
				// count him at least for big hack though
				//
				// incorporate the explicit bit of this term
				QueryTerm *qt = qw->m_queryWordTerm;
				// are we in quotes?
				if ( ! qt ) qt = qw->m_queryPhraseTerm;
				// record it as matched. this is used for the 
				// BIG HACK
				if ( qt ) m_explicitsMatched |= 
				      qt->m_explicitBit | qt->m_implicitBits;
				//
				// done BIG HACK fix
				//
				*/

				continue; 
			}
			// if we already have a match stack, we must
			// be in nextMatchWordPos
			if ( matchStack && nextMatchWordPos != i ) {
				// peel off anybody before us 
				m_numMatches -= matchStack;
				if ( m_numMatches < 0 ) m_numMatches = 0;
				// don't forget to reset the match stack
				matchStack = 0;	
				//continue; 
			}
			// if the phraseId is 0 and the previous word
			// is a match, then we're ok, but put us on a stack
			// so if we lose a match, we'll be erased
			QueryWord *nq = &m_q->m_qwords[qwn+2];
			// next match is only required if next word in query
			// is indeed valid.
			if ( nq->m_wordId && nq->m_fieldCode == 0 ) {
				nextMatchWordIdMustBeThis = nq->m_rawWordId;
				nextMatchWordPos          = i + 2;
				matchStack++;
			}
		}
		else if ( matchStack ) {
			// if the last word matched was a stop word, we have to
			// match otherwise we have to remove the whole stack.
			if ( qw->m_rawWordId != nextMatchWordIdMustBeThis ||
			     i                > nextMatchWordPos ) {
				m_numMatches -= matchStack;
				// ensure we never go negative like for 
				// www.experian.com query
				if ( m_numMatches < 0 ) m_numMatches = 0;
			}
			// always reset this here if we're not a stop word
			matchStack = 0;
		}

		// record word # of last match
		lasti = i;

		// . we MUST map the QueryWords to their respective QueryTerms
		// . that is done already pretty much in Query.cpp
		// . this allows us to set our m_foundTermVector[] as well as
		//   compute the termFreq for our matching quote
		// . MDW: WHAT IS THIS?????
		/*
		//int64_t  max = -1;
		for ( int32_t j = qwn ; j < qwn + numQWords && 
			      // if the word is repeated twice in two different
			      // phrases, qwn sometimes ends up in the later,
			      // phrase which may have less words in it than
			      // the other, so check for breech here
			      j < m_q->m_numWords ; j++ ) {
			// get the ith query word
			QueryWord *qw = &m_q->m_qwords[j];
			// does it have a query word or phrase term?
			QueryTerm *qt1 = qw->m_queryWordTerm ;
			QueryTerm *qt2 = qw->m_queryPhraseTerm;
			int32_t qtn1 = -1;
			int32_t qtn2 = -1;
			if ( qt1 ) qtn1 = qt1 - m_q->m_qterms;
			if ( qt2 ) qtn2 = qt2 - m_q->m_qterms;

			// we must match X words to match the phrase!
			if ( numWords <= 1 ) qt2 = NULL;

			// MDW: why do this here instead of below where we
			// actually add the Match?
			if ( qt1 &&
			     !(exclQTOnlyinAnchTxt && inAnchTag) )
			   m_explicitsMatched |= qt1->m_matchesExplicitBits;
			if ( qt2 && 
			     !(exclQTOnlyinAnchTxt && inAnchTag) ) {
			   m_explicitsMatched |= qt2->m_matchesExplicitBits;
			   m_explicitsMatched |= qt2->m_implicitBits;
			}
			// . set the score
			// . MDW: these scores are set in Summary.cpp based on
			//   tf, etc. i think it should handle this , not us
			if ( ! m_tscores           ) continue;
			if ( max == -1 && qt1           ) max=m_tscores[qtn1];
			if ( max == -1 && qt2           ) max=m_tscores[qtn2];
			if ( qt1 && m_tscores[qtn1]>max ) max=m_tscores[qtn1];
			if ( qt2 && m_tscores[qtn2]>max ) max=m_tscores[qtn2];
		}
		*/

		if(m_detectSubPhrases) 
			detectSubPhrase(words, i, numWords, qwn, 
					diversityWeight);
		// . if not adding to m_matches, keep going
		// . MDW: why wouldn't we add to the matches array?
		//if ( ! addToMatches ) continue;
		// don't store it in our m_matches array if the max is negative
		// i.e. we matched a '-' unwanted word
		/*
		if ( max < -1 ) {
			log("query: found neg word in doc! should be taken "
			    "care of in summary and doc should not be "
			    "displayed! query=%s docId=%"INT64"",
			    m_q->m_orig, docId);
			return false;
		}
		// sanity check
		if ( m_tscores && max == -1 ) { 
			g_errno = EBADENGINEER;
			log("query: bad matches error. fix me! query=%s "
			    "docId=%"INT64"", m_q->m_orig, docId);
			return false;
			char *xx = NULL; *xx = 0; 
		}
		*/
		// otherwise, store it in our m_matches[] array
		Match *m = &m_matches[m_numMatches];
		// use the max score of all query terms we contain as our score
		//if ( max >= 0 ) m->m_score = max;
		// the word # in the doc, and how many of 'em are in the match
		m->m_wordNum  = i;
		m->m_numWords = numWords;
		// the word # in the query, and how many of 'em we match
		m->m_qwordNum  = qwn;
		m->m_numQWords = numQWords;
		// get the first query word # of this match
		//QueryWord *qw = &m_q->m_qwords[qwn];
		qw = &m_q->m_qwords[qwn];
		// get its color. for highlighting under different colors.
		m->m_colorNum = qw->m_colorNum;
		// sanity check
		if ( m->m_colorNum < 0 ) { char *xx = NULL; *xx = 0; }
		// convenience, used by Summary.cpp
		m->m_words    = words;
		m->m_sections = sections;
		m->m_bits     = bits;
		m->m_pos      = pos;
		m->m_dist     = dist;
		m->m_flags    = flags | eflag ;
		// this is used by the proximity algorithm in Summary.cpp
		//m->m_crossedSection = false;
		// add to our vector. we want to know where each QueryWord
		// is. i.e. in the title, link text, meta tag, etc. so
		// the proximity algo in Summary.cpp can use that info.
		m_qwordFlags[qwn] |= flags;

		// loop over the query words in the match and add in all
		// their explicit bits. fixes www.gmail.com query which
		// matches query words, and we assume it is in quotes...
		/*
		for ( int32_t qi = qwn ; qi < qwn + numQWords ; qi++ ) {
			// get it
			QueryWord *ww = &m_q->m_qwords[qi];
			// incorporate the explicit bit of this term
			QueryTerm *qt = ww->m_queryWordTerm;
			// are we in quotes?
			if ( ! qt ) qt = ww->m_queryPhraseTerm;
			// record it as matched. this is used for the BIG HACK
			if ( qt ) m_explicitsMatched |= 
				      qt->m_explicitBit | qt->m_implicitBits;
		}
		*/

		// advance
		m_numMatches++;
		// i think we use "dist" for the proximity algo now, but what
		// was it used for before?
		//dist = 0;
		// reset stack
		// no! we need to be able to pop off this match if it
		// requires the next query term to follow it, like in the
		// case of a query stop word...
		//matchStack = 0;
		// we get atleast MAX_MATCHES
		if ( m_numMatches < MAX_MATCHES ) continue;
		// we've added a lot of matches, if we don't need anymore
		// to confirm the big hack then break out
		//if ( m_explicitsMatched & m_matchableRequiredBits ) {
		//	log(LOG_DEBUG,
		//	    "query: found all query terms for big hack after "
		//	    "%"INT32" matches. docId=%"INT64"", m_numMatches, docId);
		//	break;
		//}
		//bool hadPhrases ;
		//bool hadWords   ;
		//int32_t matchedBits = getTermsFound2 (&hadPhrases,&hadWords);
		//if ( (matchedBits & reqMask) == reqMask && 
		//     !(matchedBits & negMask) ) {
		//	log("query: found all query terms for big hack after "
		//	    "%"INT32" matches. docId=%"INT64"", m_numMatches, docId);
		//	break;
		//}
		// don't breech MAX_MATCHES_FOR_BIG_HACK
		if ( m_numMatches < MAX_MATCHES_FOR_BIG_HACK ) continue;
		
		log("query: Exceed match buffer of %"INT32" matches. docId=%"INT64"",
		    (int32_t)MAX_MATCHES_FOR_BIG_HACK, docId);
		break;
	}

	// peel off anybody before us
	m_numMatches -= matchStack;
	if ( m_numMatches < 0 ) m_numMatches = 0;

	return true;
}

// . word #i in the doc matches slot #n in the hash table
int32_t Matches::getNumWordsInMatch ( Words *words     ,
				   int32_t   wn        , 
				   int32_t   n         , 
				   int32_t  *numQWords ,
				   int32_t  *qwn       ,
				   bool   allowPunctInPhrase ) {

	// is it a two-word synonym?
	if ( m_qtableFlags[n] & 0x08 ) {
		// get the word following this
		int64_t wid2 = 0LL;
		if ( wn+2 < words->m_numWords ) wid2 = words->m_wordIds[wn+2];
		// scan the synonyms...
		int64_t *wids = words->m_wordIds;
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			QueryTerm *qt = &m_q->m_qterms[k];
			if ( ! qt->m_synonymOf ) continue;
			if ( qt->m_synWids0 != wids[wn] ) continue;
			if ( qt->m_synWids1 != wid2 ) continue;
			*numQWords = 3; 
			return 3;
		}
	}

	// save the first word in the doc that we match first
	int32_t wn0 = wn;

	// CAUTION: the query "business development center" (in quotes)
	// would match a doc with "business development" and 
	// "development center" as two separate phrases.

	// if query word never appears in quotes, it's a single word match
	if ( ! (m_qtableFlags[n] & 0x02) ) { *numQWords = 1; return 1; }

	// get word ids array for the doc
	int64_t  *wids   = words->getWordIds();
	//int64_t  *swids  = words->getStripWordIds();
	char      **ws     = words->getWords();
	int32_t       *wl     = words->getWordLens();
	//the word we match in the query appears in quotes in the query
	int32_t k     = -1;
	int32_t count = 0;
	int32_t nw    = words->m_numWords;

	// loop through all the quotes in the query and find
	// which one we match, if any. we will have to advance the
	// query word and doc word simultaneously and make sure they
	// match as we advance. 
	int32_t nqw = m_q->m_numWords;
	// do not look through more words than were hashed, wastes time
	//if ( nqw >= m_maxNQW && m_maxNQW > 0 ) nqw = m_maxNQW;
	int32_t j;
	for ( j = 0 ; j < nqw ; j++ ) {
		// get ith query word
		QueryWord *qw = &m_q->m_qwords[j];
		if ( !qw->m_rawWordId ) continue;
		// query word must match wid of first word in quote
		if ( (qw->m_rawWordId != wids[wn]) ) continue;
		//     (qw->m_rawWordId != swids[wn])) continue;
		// skip if in field
		// . we were doing an intitle:"fight club" query and
		//   needed to match that in the title...
		//if ( qw->m_fieldCode         ) continue;
		// query word must be in quotes
		if ( ! qw->m_inQuotes        ) continue;
		// skip it if it does NOT start the quote. quoteStart
		// is actually the query word # that contains the quote
		//if ( qw->m_quoteStart != j-1 ) continue;
		// not any more it isn't...
		if ( qw->m_quoteStart != j ) continue;
		// save the first word # in the query of the quote
		k = j; // -1;
		// count number of words we match in the quote, we've
		// already matched the first one
		count = 0;
	subloop:
		// query word must match wid of first word in phrase
		if ( (qw->m_rawWordId != wids[wn]) ) {
		//     (qw->m_rawWordId != swids[wn])) {
			// reset and try another quote in the query
			count = 0;
			wn    = wn0;
			continue;
		}
		// up the count of query words matched in the quote
		count++;
		// ADVANCE QUERY WORD
		j++;
		// if no more, we got a match
		if ( j >= nqw ) break;
		// skip punct words
		if ( m_q->m_qwords[j].m_isPunct ) j++;
		// if no more, we got a match
		if ( j >= nqw ) break;
		// now we should point to the next query word in quote
		qw = &m_q->m_qwords[j];
		// if not in quotes, we're done, we got a match
		if ( ! qw->m_inQuotes      ) break;
		// or if in a different set of quotes, we got a match
		if ( qw->m_quoteStart != k ) break;
		// . ADVANCE DOCUMENT WORD
		// . tags and punctuation words have 0 for their wid
		for ( wn++ ; wn < nw ; wn++ ) {
			// . if NO PUNCT, IN QUOTES, AND word id is zero
			//   then check for punctuation
			if(!allowPunctInPhrase && qw->m_inQuotes && !wids[wn]) {
				// . check if its a space [0x20, 0x00]
				if( (wl[wn] == 2) && (ws[wn][0] == ' ') ) 
					continue;
				// . if the length is greater than a space
				else if( wl[wn] > 2 ) {
					// . increment until we find no space
					// . increment by 2 since its utf16
					for( int32_t i = 0; i < wl[wn]; i+=2 )
						// . if its not a space, its punc
						if( ws[wn][i] != ' ' ) {
							count=0; break;
						}
					// . if count is 0, punc found break
					if( count == 0 ) break;
				}
				// . otherwise its solo punc, set count and break
				else { count=0; break; }
			}
			// . we incremented to a new word break and check
			if ( wids[wn] ) break;
		}
		// there was a following query word in the quote
		// so there must be a following word, if not, continue
		// to try to find another quote in the query we match
		if ( wn >= nw ) { 
			// reset and try another quote in the query
			count = 0; 
			wn    = wn0;
			continue;
		}
		// see if the next word and query term match
		goto subloop;
	}

	// if we did not match any quote in the query
	// check if we did match a single word. e.g.
	// Hello World "HelloWorld" "Hello World Example"
	if ( count <= 0 ) {
		if ( m_qtableFlags[n] & 0x01 ) {
			*numQWords = 1;
			// we did match a single word. m_qtableWordNums[n] may
			// not be pointing to the right qword. Set it to a 
			// qword that is the single word
			for ( j = 0 ; j < nqw ; j++ ) {
				// get ith query word
				QueryWord *qw = &m_q->m_qwords[j];
				if ( !qw->m_rawWordId ) continue;
				// query word must match wid of word
				if ( (qw->m_rawWordId != wids[wn]) ) continue;
				//   (qw->m_rawWordId != swids[wn])) continue;
				// skip if in field
				// . fix intitle:"fight club"
				//if ( qw->m_fieldCode         ) continue;
				// query word must NOT be in quotes
				if ( qw->m_inQuotes        ) continue;
				*qwn = j;
			}
			return 1;
		}
		else
			return 0;
	}
	// sanity check
	if ( k < 0 ) { char *xx = NULL; *xx = 0; }
	// skip punct words
	if ( j-1>=0 && m_q->m_qwords[j-1].m_isPunct ) j--;
	// . ok, we got a quote match
	// . it had this man query words in it
	//*numQWords = j - (k+1);
	*numQWords = j - k;
	// fix the start word
	*qwn = k ;
	if (m_q->m_qwords[k].m_isPunct) *qwn = k+1;

	return wn - wn0 + 1;
}

/*
int32_t Matches::getTermsFound ( bool  *hadPhrases , 
			      bool  *hadWords   ) {
	*hadPhrases = true;
	*hadWords   = true;
	int32_t n      = m_q->getNumTerms();
	int32_t count  = 0;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// do not count query stop words if not in quotes
		//if ( m_q->m_qterms[i].m_isQueryStopWord &&
		//     ! m_q->m_qterms[i].m_inQuotes        ) 
		//	continue;
		if ( m_foundTermVector[i] ) { count++; continue; }
		// if we missed a phrase, flag it
		if ( m_q->m_qterms[i].m_inQuotes ) *hadPhrases = false;
		else                               *hadWords   = false;
	}
	return count;
}
*/

// new version for explicit bit mask
/*
uint32_t Matches::getTermsFound2(bool *hadPhrases, bool *hadWords) {
	*hadPhrases = true;
	*hadWords   = true;
	int32_t n      = m_q->getNumTerms();
	//int32_t count  = 0;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		QueryTerm *qt = &m_q->m_qterms[i];
		if (qt->m_fieldCode) continue;
		if (qt->m_isPhrase && qt->m_termSign == 0) continue;
		if ( m_explicitsMatched & qt->m_explicitBit ) continue;
		// if we missed a phrase, flag it
		if ( qt->m_inQuotes ) *hadPhrases = false;
		else                  *hadWords   = false;
	}
	return m_explicitsMatched;

}
*/

/*
void Matches::setSubPhraseDetection() {
	char* pbuf = m_subPhraseBuf;
	m_pre.set(128, pbuf , m_htSize);
	pbuf += m_htSize;
	m_post.set(128, pbuf, m_htSize);
	m_detectSubPhrases = true;

	m_leftDiversity = 0;
	m_rightDiversity = 0;

	int64_t h = hash64LowerE("www",3);
	m_pre.addKey(h, LONG_MIN);
}
*/

void Matches::detectSubPhrase(Words* words, 
			      int32_t matchWordNum,
			      int32_t numMatchedWords,
			      int32_t queryWordNum ,
			      int32_t diversityWeight ) {

	int32_t       nw        = words->getNumWords();
	int64_t *wids      = words->getWordIds();

	// . Hash the preceding word
	int32_t prevWord = matchWordNum - 2;
	//skip entities
	while(prevWord > 0 && wids[prevWord] == 0) prevWord--;

	int64_t wid;
	int32_t slot;
	if(prevWord < 0 || wids[prevWord] == 0) {
		//word begins this section
		m_leftDiversity += diversityWeight;
	}
	else if(queryWordNum == 0 ||
		m_q->m_qwords[queryWordNum-1].m_rawWordId != wids[prevWord]) {
		//prev word is valid and is not prev query word
		wid = wids[prevWord];
		slot = m_pre.getSlot(wid);
		int32_t val;
		if(slot == -1) {
			m_pre.addKey(wid, 1);
			m_leftDiversity += diversityWeight;
		}
		else {
			val = m_pre.getValueFromSlot(slot);
			//our exempt words are negative
			if(val < 0) m_leftDiversity += diversityWeight;
		}
	}

	// . Hash the trailing word
	//n words + n-1 punctuation separators.
	int32_t nextWord = matchWordNum + 2 * numMatchedWords ;
	int32_t nextQueryWord = queryWordNum + numMatchedWords;


	//skip entities
	while(nextWord < nw && wids[nextWord] == 0) nextWord++;

	if(nextWord >= nw || wids[nextWord] == 0) { 
		//word ends this section
		m_rightDiversity += diversityWeight;
	}
	else if(nextQueryWord >= m_q->m_numWords ||
		m_q->m_qwords[nextQueryWord].m_rawWordId != wids[nextWord]) {
		//next word is valid and is not the next query word
		wid = wids[nextWord]; 
		slot = m_post.getSlot(wid);
		int32_t val;
		if(slot == -1) {
			m_post.addKey(wid, 1);
			m_rightDiversity += diversityWeight;
		}
		else {
			val = m_post.getValueFromSlot(slot);
			if(val < 0) m_rightDiversity += diversityWeight; 
		}
	}
}

float Matches::getDiversity() {
	float retval = m_leftDiversity;
	if(m_rightDiversity < retval) retval = m_rightDiversity;
	//0 means we did not get a match, doc will be big hacked out
	//1 means not diverse at all
	if(retval <= 1) return 0; 
	return logf(retval);
}


/*
bool Matches::negTermsFound ( ) {

	for( int32_t i = 0; i < m_numNegTerms; i++ ) {
		if( m_foundNegTermVector[i] ) return true;
	}
	
	return false;
}
*/

bool Matches::docHasQueryTerms(int32_t totalInlinks) {
    // Loop through all matches keeping a count of query term matches 
    // from link text.
    // If a match is not from a link text max it out.
    // Tally up the matched terms vs number of matches
    // if only one or two link text matches out of > 10 then
    // return false indicating that the doc does not
    // have the term

    if(m_numMatches == 0) {
        // if there is no query and no matches then short circuit
        return true;
    }

    int32_t qterms = 1024;
    int32_t tmpBuf[qterms];
    int32_t *numMatches = tmpBuf;

    if(qterms < m_q->m_numTerms) {
        qterms = m_q->m_numTerms;
        numMatches = (int32_t *)mmalloc(qterms * sizeof(int32_t), 
                                        "matchesAnomaly");
    }
    memset(numMatches, 0, qterms * sizeof(int32_t));

    for ( int32_t i = 0 ; i < m_numMatches ; i++ ) {
        // get the match
        Match *m = &m_matches[i];
        if(m->m_flags & MF_LINK) {
            numMatches[m->m_qwordNum]++;
            continue;
        }
        numMatches[m->m_qwordNum] = m_numMatches;
    }


    // Assume the best, since we're really only after anomalous link text
    // at this point.
    bool hasTerms = true;
    int32_t nqt = m_q->m_numTerms;
    for ( int32_t i = 0 ; i < nqt ; i++ ) {
        QueryTerm *qt = &m_q->m_qterms[i];
        // For purposes of matching, we ignore all stop words
        if ( ! isMatchableTerm ( qt ) || qt->m_ignored || 
             (qt->m_isPhrase && !qt->m_isRequired)) {
            continue;
        }

        // get the word it is from
        QueryWord *qw = qt->m_qword;

        // It is a match if it matched something other than link text
        // or it matched at least 1 link text and there arent many link texts
        // or it matched more than 2 link texts and there are many link texts
        hasTerms &= ((numMatches[qw->m_wordNum] >= m_numMatches) ||  
                     (numMatches[qw->m_wordNum] > 0 && totalInlinks < 10) ||
                     (numMatches[qw->m_wordNum] > 2 && totalInlinks > 10));

    }

    if (numMatches != tmpBuf) {
        mfree(numMatches, qterms * sizeof(int32_t), "matchesAnomaly");
    }
    return hasTerms;
}





MatchOffsets::MatchOffsets() {
	reset();
}

MatchOffsets::~MatchOffsets() {

}

void MatchOffsets::reset() {
	m_numMatches = 0;
	m_numBreaks = 0;
	m_numAlnums = 0;
}

bool MatchOffsets::set(Xml * xml, Words *words, Matches *matches,
		       unsigned char offsetType) {
	//m_numMatches = matches->m_numMatches;
	m_numMatches = 0;
	m_numAlnums  = matches->m_numAlnums;
	if (offsetType == OFFSET_WORDS){
		for (int32_t i = 0; i < matches->m_numMatches ; i++ ) {
			m_queryWords[i] = matches->m_matches[i].m_qwordNum;
			m_matchOffsets[i] = matches->m_matches[i].m_wordNum;
			m_numMatches++;
			// look for breaking tags
			if (i == matches->m_numMatches-1) continue;
			for (int32_t j= matches->m_matches[i].m_wordNum+1;
			     j < matches->m_matches[i+1].m_wordNum;
			     j++){
				nodeid_t tag =words->m_tagIds[j] & BACKBITCOMP;
				if (!g_nodes[tag].m_isBreaking) 
					continue;
				m_breakOffsets[m_numBreaks++] = j;
				// only store the first one
				break;
			}
		}
	}
	else if ( offsetType == OFFSET_BYTES ){
		// Latin-1 offset
		for (int32_t i = 0; i < matches->m_numMatches ; i++ ) {
			int32_t wordOffset = matches->m_matches[i].m_wordNum;
			m_queryWords[i] = matches->m_matches[i].m_qwordNum;
			m_matchOffsets[i] = 
				words->m_words[wordOffset] - 
				words->m_words[0];
			m_numMatches++;
			// look for breaking tags
			if (i == matches->m_numMatches-1) 
				continue;
			for (int32_t j= matches->m_matches[i].m_wordNum+1;
			     j < matches->m_matches[i+1].m_wordNum;
			     j++){
				nodeid_t tag =words->m_tagIds[j] & BACKBITCOMP;
				if (g_nodes[tag].m_isBreaking) {
					m_breakOffsets[m_numBreaks++] = 
						words->m_words[j] - 
						words->m_words[0];

					// only store the first one
					break;
				}
			}
		}
	}
	return true;
}


int32_t MatchOffsets::getStoredSize() {
	return m_numMatches * 5 
		+ 4 //numMatches
		+ m_numBreaks * 4
		+ 4 //numBreaks
		+ 4 //totalsize
		+ 4;//numAlnums
}

int32_t MatchOffsets::serialize(char *buf, int32_t bufsize){
	//if (m_numMatches == 0 ) return 0;
	int32_t need = getStoredSize();
	if ( need > bufsize ) { 
		g_errno = EBUFTOOSMALL; 
		log(LOG_LOGIC,"query: matchoffsets: serialize Buf too small.");
		return -1; 
	}
	char *p = buf;
	*(int32_t*) p = need; p += 4;
	*(int32_t*) p = m_numMatches; p += 4;
	*(int32_t*) p = m_numAlnums; p += 4;
	gbmemcpy(p, m_queryWords, m_numMatches); p += m_numMatches;
	gbmemcpy(p, m_matchOffsets, m_numMatches*4); p += m_numMatches*4;
	*(int32_t*) p = m_numBreaks; p += 4;
	gbmemcpy(p, m_breakOffsets, m_numBreaks*4); p += m_numBreaks*4;
	
	return p - buf;
}

int32_t MatchOffsets::deserialize(char *buf, int32_t bufsize){
	//if (bufsize == 0 ) return 0;
	char *p = buf;
	int32_t need = *(int32_t*) p ; p += 4;
	if (bufsize < need) {
		g_errno = EBUFTOOSMALL; 
		log(LOG_LOGIC,"query: matchoffsets: deserialize "
		    "buf too small.");
		return -1; 
	}
	m_numMatches = *(int32_t*) p ; p += 4;
	m_numAlnums  = *(int32_t*) p ; p += 4;
	gbmemcpy(m_queryWords, p, m_numMatches); p += m_numMatches;
	gbmemcpy(m_matchOffsets, p, m_numMatches*4); p += m_numMatches*4;
	m_numBreaks = *(int32_t*) p ; p += 4;
	gbmemcpy(m_breakOffsets, p, m_numBreaks*4); p += m_numBreaks*4;
	
	return p - buf;
	
}
