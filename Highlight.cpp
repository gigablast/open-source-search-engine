#include "gb-include.h"

#include "Highlight.h"
#include "Titledb.h" // TITLEREC_CURRENT_VERSION
#include "Phrases.h"
#include "Synonyms.h"
#include "XmlDoc.h"


// use different front tags for matching different term #'s
static char *s_frontTags[] = {
	/* . old style tags
	"<b style=color:black;background-color:#ffff66>" ,
	"<b style=color:black;background-color:#A0FFFF>" ,
	"<b style=color:black;background-color:#99ff99>" ,
	"<b style=color:black;background-color:#ff9999>" ,
	"<b style=color:black;background-color:#ff66ff>" ,
	"<b style=color:white;background-color:#880000>" ,
	"<b style=color:white;background-color:#00aa00>" ,
	"<b style=color:white;background-color:#886800>" ,
	"<b style=color:white;background-color:#004699>" ,
	"<b style=color:white;background-color:#990099>" 
	*/
	"<span class=\"gbcnst gbcnst00\">" ,
	"<span class=\"gbcnst gbcnst01\">" ,
	"<span class=\"gbcnst gbcnst02\">" ,
	"<span class=\"gbcnst gbcnst03\">" ,
	"<span class=\"gbcnst gbcnst04\">" ,
	"<span class=\"gbcnst gbcnst05\">" ,
	"<span class=\"gbcnst gbcnst06\">" ,
	"<span class=\"gbcnst gbcnst07\">" ,
	"<span class=\"gbcnst gbcnst08\">" ,
	"<span class=\"gbcnst gbcnst09\">" 
};
//int32_t s_frontTagLen=gbstrlen("<b style=color:black;background-color:#990099>");
int32_t s_frontTagLen=gbstrlen("<span class=\"gbcnst gbcnst00\">");

// include the css style sheet for the highlight tags
//static char *s_styleSheetInc =  "<link rel=\"stylesheet\" type=\"text/css\" "
//			"href=\"~/cachedpagestyles.css\">";
//int32_t s_styleSheetIncLen = gbstrlen( s_styleSheetInc );

static char *s_styleSheet =
"<style type=\"text/css\">"
"span.gbcns{font-weight:600}"
"span.gbcnst00{color:black;background-color:#ffff66}"
"span.gbcnst01{color:black;background-color:#a0ffff}"
"span.gbcnst02{color:black;background-color:#99ff99}"
"span.gbcnst03{color:black;background-color:#ff9999}"
"span.gbcnst04{color:black;background-color:#ff66ff}"
"span.gbcnst05{color:white;background-color:#880000}"
"span.gbcnst06{color:white;background-color:#00aa00}"
"span.gbcnst07{color:white;background-color:#886800}"
"span.gbcnst08{color:white;background-color:#004699}"
"span.gbcnst09{color:white;background-color:#990099}"
"span.gbcnst00x{color:white;background-color:black;border:2px solid #ffff66}"
"span.gbcnst01x{color:white;background-color:black;border:2px solid #a0ffff}"
"span.gbcnst02x{color:white;background-color:black;border:2px solid #99ff99}"
"span.gbcnst03x{color:white;background-color:black;border:2px solid #ff9999}"
"span.gbcnst04x{color:white;background-color:black;border:2px solid #ff66ff}"
"span.gbcnst05x{color:white;background-color:black;border:2px solid #880000}"
"span.gbcnst06x{color:white;background-color:black;border:2px solid #00aa00}"
"span.gbcnst07x{color:white;background-color:black;border:2px solid #886800}"
"span.gbcnst08x{color:white;background-color:black;border:2px solid #004699}"
"span.gbcnst09x{color:white;background-color:black;border:2px solid #990099}"
"</style>";
int32_t s_styleSheetLen = gbstrlen( s_styleSheet );

//buffer for writing term list items
char s_termList[1024];

// . return length stored into "buf"
// . content must be NULL terminated
// . if "useAnchors" is true we do click and scroll
// . if "isQueryTerms" is true, we do typical anchors in a special way
int32_t Highlight::set ( SafeBuf *sb,
		      //char        *buf          ,
		      //int32_t         bufLen       ,
		      char        *content      ,
		      int32_t         contentLen   ,
		      // primary language of the document (for synonyms)
		      char         docLangId    ,
		      Query       *q            ,
		      bool         doStemming   ,
		      bool         useAnchors   ,
		      const char  *baseUrl      ,
		      const char  *frontTag     ,
		      const char  *backTag      ,
		      int32_t         fieldCode    ,
		      int32_t         niceness      ) {

	Words words;
	if ( ! words.set ( content      , 
			   contentLen   , 
			   TITLEREC_CURRENT_VERSION,
			   true         , // computeId
			   true         ) ) // has html entites?
		return -1;

	int32_t version = TITLEREC_CURRENT_VERSION;

	Bits bits;
	if ( ! bits.set (&words,version,niceness) ) return -1;

	Phrases phrases;
	if ( !phrases.set(&words,&bits,true,false,version,niceness))return -1;

	//SafeBuf langBuf;
	//if ( !setLangVec ( &words , &langBuf , niceness )) return 0;
	//uint8_t *langVec = (uint8_t *)langBuf.getBufStart();

	// make synonyms
	//Synonyms syns;
	//if(!syns.set(&words,NULL,docLangId,&phrases,niceness,NULL)) return 0;

	Matches matches;
	matches.setQuery ( q );

	if ( ! matches.addMatches ( &words , &phrases ) ) return -1;

	// store
	m_numMatches = matches.getNumMatches();

	return set ( sb , 
		     //buf         ,
		     //bufLen      , 
		     &words      ,
		     &matches    ,
		     doStemming  ,
		     useAnchors  ,
		     baseUrl     ,
		     frontTag    ,
		     backTag     ,
		     fieldCode   ,
		     q		 );
}

// New version
int32_t Highlight::set ( SafeBuf *sb ,
		      //char        *buf        ,
		      //int32_t         bufLen     ,
		      Words       *words      ,
		      Matches     *matches    ,
		      bool         doStemming ,
		      bool         useAnchors ,
		      const char  *baseUrl    ,
		      const char  *frontTag   ,
		      const char  *backTag    ,
		      int32_t         fieldCode  ,
		      Query	  *q	      ) {
	// save stuff
	m_frontTag    = frontTag;
	m_backTag     = backTag;
	m_doStemming  = doStemming;
	m_didErrMsg   = false;
	m_fieldCode   = fieldCode;
	// should we do click and scroll
	m_useAnchors  = useAnchors;
	m_baseUrl     = baseUrl;
	// . set the anchor counts to 1000*i+1 for each possible query term num
	// . yes, i know, why +1? because we're assuming the query terms
	//   have been highlighted before us 
	//for ( int32_t i = 0 ; i < MAX_QUERY_TERMS ; i++ ) 
	//	m_anchorCounts[i] = 1000*i + 1;
	// set lengths of provided front/back highlight tags
	if ( m_frontTag ) m_frontTagLen = gbstrlen ( frontTag );
	if ( m_backTag  ) m_backTagLen  = gbstrlen ( backTag  );
	// point to buffer to store highlighted text into
	//m_buf    = buf;
	//m_bufLen = bufLen;
	//m_bufPtr = buf;
	m_sb = sb;

	// label it
	m_sb->setLabel ("highw");

	// save room for terminating \0
	//m_bufEnd = m_buf + m_bufLen - 1;

	if ( ! highlightWords ( words, matches, q ) ) return -1;

	// null terminate
	//*m_bufPtr = '\0';
	m_sb->nullTerm();
	// return the length
	return m_sb->length();//m_bufPtr - m_buf;
}

bool Highlight::highlightWords ( Words *words , Matches *m, Query *q ) {
	// get num of words
	int32_t numWords = words->getNumWords();
	// some convenience ptrs to word info
	char *w;
	int32_t  wlen;

	// length of our front tag should be constant
	int32_t frontTagLen ;
	if ( m_frontTag ) frontTagLen = m_frontTagLen;
	else              frontTagLen = s_frontTagLen;
	// set the back tag, should be constant
	const char *backTag ;
	int32_t  backTagLen;
	if ( m_backTag ) {
		backTag    = m_backTag;
		backTagLen = m_backTagLen;
	}
	else {
		//backTag = "</b>";
		//backTagLen = 4;
		backTag = "</span>";
		backTagLen = 7;
	}

	// set nexti to the word # of the first word that matches a query word
	int32_t nextm = -1;
	int32_t nexti = -1;
	if ( m->m_numMatches > 0 ) {
		nextm = 0;
		nexti = m->m_matches[0].m_wordNum;
	}

	int32_t backTagi = -1;
	bool inTitle  = false;
	bool endHead  = false;
	bool endHtml  = false;

	/*
	if (q) {
		log ("q.m_numWords: %d\nq.m_numTerms: %d\n",
		      q->m_numWords, q->m_numTerms);
	}
	*/

	for ( int32_t i = 0 ; i < numWords  ; i++ ) {
		// set word's info
		w    = words->getWord(i);
		wlen = words->getWordLen(i);
		endHead = false;
		endHtml = false;
		// bail now if out of room
		/*
		if ( m_bufPtr + MAX_URL_LEN + 1024 + wlen >= m_bufEnd ) {
			// don't spam the logs
			static int64_t s_lastTime = 0;
			int64_t now = gettimeofdayInMilliseconds();
			if ( now - s_lastTime < 1000 ) return true;
			log("query: Not enough buffer space to highlight "
			    "text. Ask Matt to fix.");
			s_lastTime = now;
			return true;
		}
		*/
		if ( (words->getTagId(i) ) == TAG_TITLE ) { //<TITLE>
			if ( words->isBackTag(i) ) inTitle = false;
			else inTitle = true;
#ifdef DEBUG_HIGHLIGHT
			char foo[32];
			strncpy(foo, w, 32);
			foo[wlen]='\0';
			printf("<title> at word %d: %s\n", i, foo);
#endif
		}
		else if ( (words->getTagId(i) ) == TAG_HTML ) { //<HTML>
			if (words->isBackTag(i) )
				endHtml = true;
		}
		else if ( (words->getTagId(i) ) == TAG_HEAD ) { //<HEAD>
			if (words->isBackTag(i) )
				endHead = true;
		}

		// match class ptr
		Match *mat = NULL;
		// This word is a match...see if we're gonna tag it
		// dont put the same tags around consecutive matches
		if ( i == nexti && ! inTitle && ! endHead && ! endHtml) {
			// get the match class for the match
			mat = &m->m_matches[nextm];
			// discontinue any current font tag we are in
			if ( i < backTagi ) {
				// push backtag ahead if needed
				if ( i + mat->m_numWords > backTagi )
					backTagi = i + mat->m_numWords;

				//gbmemcpy ( m_bufPtr , backTag , backTagLen );
				//m_bufPtr += backTagLen ;
				//backTagi = -1;
			}
			else {
				// now each match is the entire quote, so write the
				// fron tag right now
				const char *frontTag;
				if ( m_frontTag ) frontTag    = m_frontTag;
				//else frontTag = s_frontTags [ p[i] % 10];
				else frontTag =s_frontTags[mat->m_colorNum%10];
				// OK...this is UTF-8 output, and ASCII Text
				//strcpy ( m_bufPtr , frontTag );
				//m_bufPtr += frontTagLen;
				m_sb->safeStrcpy ( (char *)frontTag );
				//log(LOG_DEBUG, 
				//    "Highlight: starting phrase %d at word %d\n",
				//    p[i], i);
				
				// when to write the back tag? add the number of
				// words in the match to i.
				backTagi = i + mat->m_numWords;
			}
		}
		else if ( endHead ) {
			// include the tags style sheet immediately before
			// the closing </TITLE> tag
			//gbmemcpy( m_bufPtr, s_styleSheet, s_styleSheetLen );
			m_sb->safeMemcpy( s_styleSheet , s_styleSheetLen );
			//m_bufPtr += s_styleSheetLen;
		}
		//else if ( endHtml ) {
		//	;
		//}

		if ( i == nexti ) {
			// advance match
			nextm++;
			// set nexti to the word # of the next match
			if ( nextm < m->m_numMatches ) 
				nexti = m->m_matches[nextm].m_wordNum;
			else
				nexti = -1;
		}

#ifdef DEBUG_HIGHLIGHT
		if ( mat )
			log(LOG_DEBUG, 
			    "Highlight: word %d is in phrase %d, "
			    "intitle: %s, fronttag: %s, backtag: %s\n",
			    i,mat->m_colorNum, //p[i], 
			    inTitle?"true":"false",
			    doFrontTag?"true":"false",
			    doBackTag?"true":"false");
#endif
		// write the alnum word
		//m_bufPtr +=latin1ToUtf8(m_bufPtr, m_bufEnd-m_bufPtr,w, wlen);
		// everything is utf8 now
		//gbmemcpy ( m_bufPtr, w , wlen );
		//m_bufPtr += wlen;
		m_sb->safeMemcpy ( w , wlen );

		// back tag
		if ( i == backTagi-1 ) {
			// store the back tag
			//gbmemcpy ( m_bufPtr , backTag , backTagLen );
			//m_bufPtr += backTagLen ;
			m_sb->safeMemcpy ( (char *)backTag , backTagLen );
			//log(LOG_DEBUG, 
			//    "Highlight: ending phrase %d at word %d\n",
			//    p[i], i);
		}
	}
	return true;
}
