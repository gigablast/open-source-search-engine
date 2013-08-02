#include "gb-include.h"

#include "Words.h"
#include "Phrases.h" // for isInPhrase() for hashWordIffNotInPhrase
#include "Unicode.h" // getUtf8CharSize()
#include "StopWords.h"
#include "Speller.h"
#include "HashTableX.h"
#include "Sections.h"
#include "XmlNode.h" // getTagLen()

//static long printstring ( char *s , long len ) ;

Words::Words ( ) {
	m_buf = NULL;
	m_bufSize = 0;
	reset();
}
Words::~Words ( ) {
	reset();
}
void Words::reset ( ) {
	m_numWords = 0;
	m_numAlnumWords = 0;
	m_xml = NULL;
	m_preCount = 0;
	if ( m_buf && m_buf != m_localBuf && m_buf != m_localBuf2 )
		mfree ( m_buf , m_bufSize , "Words" );
	m_buf = NULL;
	m_bufSize = 0;
	m_tagIds = NULL;
	m_s = NULL;
	m_numTags = 0;
	m_hasTags = false;
	m_localBuf2 = NULL;
	m_localBufSize2 = 0;
}

bool Words::set ( char *s, long slen, long version, 
		  bool computeWordIds,
		  long niceness) {
	// bail if nothing
	if ( ! s || slen == 0 ) {
		m_numWords = 0;
		m_numAlnumWords = 0;
		return true;
	}

	char c = s[slen];
	if ( c != '\0' ) s[slen]='\0';
	bool status = set ( s , version, computeWordIds , niceness );
	if ( c != '\0' ) s[slen] = c;
	return status;
}

// a quickie
// this url gives a m_preCount that is too low. why?
// http://go.tfol.com/163/speed.asp
long countWords ( char *p , long plen , long niceness ) {
	char *pend  = p + plen;
	long  count = 1;
 loop:

	// sequence of punct
	for  ( ; p < pend && ! is_alnum_utf8 (p) ; p += getUtf8CharSize(p) ) {
		// breathe
		QUICKPOLL ( niceness );
		// in case being set from xml tags, count as words now
		if ( *p=='<') count++; 
	}
	count++;

	// sequence of alnum
	for  ( ; p < pend && is_alnum_utf8 (p) ; p += getUtf8CharSize(p) )
		// breathe
		QUICKPOLL ( niceness );

	count++;

	if ( p < pend ) goto loop;
	// some extra for good meaure
	return count+10;
}

long countWords ( char *p , long niceness ) {
	long  count = 1;
 loop:

	// sequence of punct
	for  ( ; *p && ! is_alnum_utf8 (p) ; p += getUtf8CharSize(p) ) {
		// breathe
		QUICKPOLL ( niceness );
		// in case being set from xml tags, count as words now
		if ( *p=='<') count++; 
	}
	count++;

	// sequence of alnum
	for  ( ; *p && is_alnum_utf8 (p) ; p += getUtf8CharSize(p) )
		// breathe
		QUICKPOLL ( niceness );

	count++;

	if ( *p ) goto loop;
	// some extra for good meaure
	return count+10;
}

static bool s_tested = false;

bool Words::set ( Xml *xml, 
		  bool computeWordIds , 
		  long niceness ,
		  long node1 ,
		  long node2 ) {
	// prevent setting with the same string
	if ( m_xml == xml ) { char *xx=NULL;*xx=0; }
	reset();
	m_xml = xml;
	m_version = xml->getVersion();
	//m_version = xml->getVersion();

	// quick test
	if ( ! s_tested ) {
		// only do once
		s_tested = true;
		// set c to a curling quote in unicode
		long c = 0x201c; // 0x235e;
		// encode it into utf8
		char dst[5];
		// point to it
		char *p = dst;
		// put space in there
		*p++ = ' ';
		// "numBytes" is how many bytes it stored into 'dst"
		long numBytes = utf8Encode ( c , p );
		// must be 2 bytes i guess
		if ( numBytes != 3 ) { char *xx=NULL; *xx=0; }
		// check it
		long size = getUtf8CharSize(p);
		if ( size != 3 ) { char *xx=NULL; *xx=0; }
		// is that punct
		if ( ! is_punct_utf8 ( p ) ) { char *xx=NULL;*xx=0; }
		// make sure can pair across
		//unsigned char bits = getPunctuationBits  ( dst , 4 );
		// must be able to pair across
		//if ( ! ( bits & D_CAN_PAIR_ACROSS ) ) { char *xx=NULL;*xx=0;}
	}

	// if xml is empty, bail
	if   ( ! xml->getContent() ) return true;

	long numNodes = xml->getNumNodes();
	if ( numNodes <= 0 ) return true;

	// . can be given a range, if node2 is -1 that means all!
	// . range is half-open: [node1, node2)
	if ( node2 < 0 ) node2 = numNodes;
	// sanity check
	if ( node1 > node2 ) { char *xx=NULL;*xx=0; }
	char *start = xml->getNode(node1);
	char *end   = xml->getNode(node2-1) + xml->getNodeLen(node2-1);
	long  size  = end - start;

	m_preCount = countWords( start , size , niceness );

	// allocate based on the approximate count
	if ( ! allocateWordBuffers(m_preCount, true)) return false;
	
	// are we done?
	for ( long k = node1 ; k < node2 && m_numWords < m_preCount ; k++ ){
		// get the kth node
		char *node    = xml->getNode   (k);
		long  nodeLen = xml->getNodeLen(k);
		// is the kth node a tag?
		if ( ! xml->isTag(k) ) {
			char c = node[nodeLen];
			node[nodeLen] = '\0';
			addWords(node,nodeLen,computeWordIds,niceness);
			node[nodeLen] = c;
			continue;
		}
		// it is a tag
		m_words    [m_numWords] = node;
		m_wordLens [m_numWords] = nodeLen;
		m_tagIds   [m_numWords] = xml->getNodeId(k);
		m_wordIds  [m_numWords] = 0LL;
		m_nodes    [m_numWords] = k;
		// we have less than 127 HTML tags, so set 
		// the high bit for back tags
		if ( xml->isBackTag(k)) {
			m_tagIds[m_numWords] |= BACKBIT;
		}
		//log(LOG_DEBUG, "Words: Word %ld: got tag %s%s (%d)", 
		//    m_numWords,
		//    isBackTag(m_numWords)?"/":"",
		//    g_nodes[getTagId(m_numWords)].m_nodeName,
		//    getTagId(m_numWords));
		
		m_numWords++;
		// used by XmlDoc.cpp
		m_numTags++;
		continue;
	}
	return true;
}

bool Words::set11 ( char *s , char *send , long niceness ) {
	reset();
	m_version = TITLEREC_CURRENT_VERSION;
	m_s = s;
	// this will make addWords() scan for tags
	m_hasTags = true;
	// save it
	char saved = *send;
	// null term
	*send = '\0';
	// determine rough upper bound on number of words by counting
	// punct/alnum boundaries
	m_preCount = countWords ( s , niceness );
	// true = tagIds
	bool status = allocateWordBuffers(m_preCount,true);
	// deal with error now
	if ( ! status ) { *send = saved; return false; }
	// and set the words
	status = addWords(s,0x7fffffff, true, niceness );
	// bring it back
	*send = saved;
	// return error?
	return status;
}

bool Words::setxi ( char *s , char *buf, long bufSize, long niceness ) {
	// prevent setting with the same string
	if ( m_s == s ) { char *xx=NULL;*xx=0; }
	reset();
	m_version = TITLEREC_CURRENT_VERSION;
	// save for sanity check
	m_s = s;
	m_localBuf2 = buf;
	m_localBufSize2 = bufSize;
	// determine rough upper bound on number of words by counting
	// punct/alnum boundaries
	m_preCount = countWords ( s , niceness );
	if (!allocateWordBuffers(m_preCount)) return false;
	bool computeWordIds = true;
	return addWords(s,0x7fffffff, computeWordIds, niceness );
}

// . set words from a string
// . assume no HTML entities in the string "s"
// . s must be NULL terminated
// . NOTE: do not free "s" from under us cuz we reference it
// . break up the string ,"s", into "words".
// . doesn't do tags, only text nodes in "xml"
// . our definition of a word is as close to English as we can get it
// . BUT we also consider a string of punctuation characters to be a word
bool Words::set ( char *s , long version, 
		  bool computeWordIds ,
		  long niceness ) {

	// prevent setting with the same string
	if ( m_s == s ) { char *xx=NULL;*xx=0; }

	reset();
	m_version = version;
	// save for sanity check
	m_s = s;

	m_version = version;
	// determine rough upper bound on number of words by counting
	// punct/alnum boundaries
	m_preCount = countWords ( s , niceness );
	if (!allocateWordBuffers(m_preCount)) return false;
	
	return addWords(s,0x7fffffff, computeWordIds, niceness );
}

#include "XmlNode.h"

bool Words::addWords(char *s,long nodeLen,bool computeWordIds, long niceness) {
	long  i = 0;
	long  j;
	//long  k = 0;
	long  wlen;
	//unsigned long e;
	//long  skip;
	long badCount = 0;

	bool hadApostrophe = false;

	UCScript oldScript = ucScriptCommon;
	UCScript saved;
	UCProps props;

 uptop:

	// bad utf8 can cause a breach
	if ( i >= nodeLen ) goto done;

	if ( ! s[i] ) goto done;

	if ( ! is_alnum_utf8(s+i) ) { // && m_numWords < m_preCount ) {

		if ( m_numWords >= m_preCount ) goto done;

		// tag?
		if ( s[i]=='<' && m_hasTags && isTagStart(s+i) ) {
			// get the tag id
			if ( s[i+1]=='/' ) {
				// skip over /
				m_tagIds [m_numWords] = ::getTagId(s+i+2);
				m_tagIds [m_numWords] |= BACKBIT;
			}
			else
				m_tagIds [m_numWords] = ::getTagId(s+i+1);
			// word start
			m_words    [m_numWords] = s + i;
			m_wordIds  [m_numWords] = 0LL;
			// skip till end
			long tagLen = getTagLen(s+i); // ,niceness);
			m_wordLens [m_numWords] = tagLen;
			m_numWords++;
			// advance
			i += tagLen;
			goto uptop;
		}

		// it is a punct word, find end of it
		char *start = s+i;
		//for (;s[i] && ! is_alnum_utf8(s+i);i+=getUtf8CharSize(s+i));
		for ( ; s[i] ; i += getUtf8CharSize(s+i)){
			// stop on < if we got tags
			if ( s[i] == '<' && m_hasTags ) break;
			// breathe
			QUICKPOLL(niceness);
			// if we are simple ascii, skip quickly
			if ( is_ascii(s[i]) ) {
				// accumulate NON-alnum chars
				if ( ! is_alnum_a(s[i]) ) continue;
				// update
				oldScript = ucScriptCommon;
				// otherwise, stop we got alnum
				break;
			}
			// if we are utf8 we stop on special props
			UChar32 c = utf8Decode ( s+i );
			// stop if word char
			if ( ! ucIsWordChar ( c ) ) continue;
			// update first though
			oldScript = ucGetScript ( c );
			// then stop
			break;
		}
		m_words        [ m_numWords  ] = start;
		m_wordLens     [ m_numWords  ] = s+i - start;
		m_wordIds      [ m_numWords  ] = 0LL;
		if (m_tagIds) m_tagIds[m_numWords] = 0;
		m_numWords++;
		goto uptop;
	}

	// get an alnum word
	j = i;
 again:
	//for ( ; is_alnum_utf8 (&s[i] ) ; i += getUtf8CharSize(s+i) );
	for ( ; s[i] ; i += getUtf8CharSize(s+i) ) {
		// breathe
		QUICKPOLL(niceness);
		// simple ascii?
		if ( is_ascii(s[i]) ) {
			// accumulate alnum chars
			if ( is_alnum_a(s[i]) ) continue;
			// update
			oldScript = ucScriptCommon;
			// otherwise, stop we got punct
			break;
		}
		// get the code point of the utf8 char
		UChar32 c = utf8Decode ( s+i );
		// get props
		props = ucProperties ( c );
		// good stuff?
		if ( props & (UC_IGNORABLE|UC_EXTEND) ) continue;
		// stop? if UC_WORCHAR is set, that means its an alnum
		if ( ! ( props & UC_WORDCHAR ) ) {
			// reset script between words
			oldScript = ucScriptCommon;
			break;
		}
		// save it
		saved = oldScript;
		// update here
		oldScript = ucGetScript(c);
		// treat ucScriptLatin (30) as common so we can have latin1
		// like char without breaking the word!
		if ( oldScript == ucScriptLatin ) oldScript = ucScriptCommon;
		// stop on this crap too i guess. like japanes chars?
		if ( props & ( UC_IDEOGRAPH | UC_HIRAGANA | UC_THAI ) ) {
			// include it
			i += getUtf8CharSize(s+i);
			// but stop
			break;
		}
		// script change?
		if ( saved != oldScript ) break;
	}
	
	// . java++, A++, C++ exception
	// . A+, C+, exception
	// . TODO: consider putting in Bits.cpp w/ D_CAN_BE_IN_PHRASE
	if ( s[i]=='+' ) {
		if ( s[i+1]=='+' && !is_alnum_utf8(&s[i+2]) ) i += 2;
		else if ( !is_alnum_utf8(&s[i+1]) ) i++;
	}
	// . c#, j#, ...
	if ( s[i]=='#' && !is_alnum_utf8(&s[i+1]) ) i++;
	
	// allow for words like we're dave's and i'm
	if(s[i]=='\''&&s[i+1]&&is_alnum_utf8(&s[i+1])&&!hadApostrophe){
		i++;
		hadApostrophe = true;
		goto again;
	}
	hadApostrophe = false;
	
	// get word length
	wlen = i - j;
	if ( m_numWords >= m_preCount ) goto done;
	m_words   [ m_numWords  ] = &s[j];
	m_wordLens[ m_numWords  ] = wlen;
	// . Lars says it's better to leave the accented chars intact
	// . google agrees
	// . but what about "re'sume"?
	if ( computeWordIds ) {
		long long h = hash64Lower_utf8(&s[j],wlen);
		m_wordIds [m_numWords] = h;
		// until we get an accent removal algo, comment this
		// out and possibly use the query synonym pipeline
		// to search without accents. MDW
		//long long h2 = hash64AsciiLowerE(&s[j],wlen);
		//if ( h2 != h ) m_stripWordIds [m_numWords] = h2;
		//else           m_stripWordIds [m_numWords] = 0LL;
		//m_stripWordIds[m_numWords] = 0;
	}
	if (m_tagIds) m_tagIds[m_numWords] = 0;
	m_numWords++;
	m_numAlnumWords++;
	// break on \0 or MAX_WORDS
	//if ( ! s[i] ) goto done;
	// get a punct word
	goto uptop;
	/*
	  j = i;
	  // delineate the "punctuation" word
	  for ( ; s[i] && !is_alnum_utf8(&s[i]);i+=getUtf8CharSize(s+i));
	  // bad utf8 could cause us to breach the node, so watch out!
	  if ( i > nodeLen ) {
	  badCount++;
	  i = nodeLen;
	  }
	  // get word length
	  wlen = i - j;
	  if ( m_numWords >= m_preCount ) goto done;
	  m_words        [m_numWords  ] = &s[j];
	  m_wordLens     [m_numWords  ] = wlen;
	  m_wordIds      [m_numWords  ] = 0LL;
	  if (m_tagIds) m_tagIds[m_numWords] = 0;
	  m_numWords++;
	*/

 done:
	// bad programming warning
	if ( m_numWords > m_preCount ) {
		log(LOG_LOGIC,
		    "build: words: set: Fix counting routine.");
		char *xx = NULL; *xx = 0;
	}
	// compute total length
	if ( m_numWords <= 0 ) m_totalLen = 0;
	else m_totalLen = m_words[m_numWords-1] - s + m_wordLens[m_numWords-1];

	if ( badCount )
		log("words: had %li bad utf8 chars",badCount);

	return true;
}

// common to Unicode and ISO-8859-1
bool Words::allocateWordBuffers(long count, bool tagIds) {
	// alloc if we need to (added 4 more for m_nodes[])
	long wordSize = 20;
	if ( tagIds ) wordSize += sizeof(nodeid_t);
	m_bufSize = wordSize * count;
	if(m_bufSize < 0) return log("build: word count overflow %li "
				     "bytes wordSize=%li count=%li.",
				     m_bufSize, wordSize, count);
	if ( m_bufSize <= m_localBufSize2 && m_localBuf2 ) {
		m_buf = m_localBuf2;
	}
	else if ( m_bufSize <= WORDS_LOCALBUFSIZE ) {
		m_buf = m_localBuf;
	}
	else {
		m_buf = (char *)mmalloc ( m_bufSize , "Words" );
		if ( ! m_buf ) return log("build: Could not allocate %li "
					  "bytes for parsing document.",
					  m_bufSize);
	}

	// set ptrs
	char *p = m_buf;
	m_words    = (char     **)p ;
	p += sizeof(char*) * count;
	m_wordLens = (long      *)p ;
	p += sizeof(long)* count;
	m_wordIds  = (long long *)p ;
	p += sizeof (long long) * count;
	//m_stripWordIds  = (long long *)p ;
	//p += sizeof (long long) * count;
	m_nodes = (long *)p;
	p += sizeof(long) * count;

	if (tagIds) {
		m_tagIds = (nodeid_t*) p;
		p += sizeof(nodeid_t) * count;
	}

	return true;
}

void Words::print( ) {
	for (long i=0;i<m_numWords;i++) {
		printWord(i);
		printf("\n");
	}
}

void Words::printWord ( long i ) {
	fprintf(stderr,"#%05li ",i);
	fprintf(stderr,"%020llu ",m_wordIds[i]);
	// print the word
	printstring(m_words[i],m_wordLens[i]);
	//if (m_spam.m_spam[i]!=0)
	//	printf("[%i]",m_spam.m_spam[i]);
}

long printstring ( char *s , long len ) {
	// filter out \n's and \r's
	long olen = 0;
	for ( long i = 0 ; i < len && olen < 17 ; i++ ) {
		if ( s[i] == '\n' || s[i] =='\r' ) continue;
		olen++;
		fprintf(stderr,"%c",s[i]);
	}
	if ( olen == 17 ) fprintf(stderr,"...");
	//while ( olen < 20 ) { fprintf(stderr," "); olen++; }
	return olen;
}

/*
// for g_indexdb.getTermId()
#include "Indexdb.h" 

// . hash all the words into "table"
// . NOTE: we append ":" to the prefixes for you, if one is not there already
bool Words::hash ( TermTable      *table          , 
		   Spam           *spam           ,
		   //Scores       *scores         ,
		   Weights        *weights        ,
		   unsigned long   baseScore      ,
		   unsigned long   maxScore       ,
		   long long       startHash      ,
		   char           *prefix1        ,
		   long            prefixLen1     ,
		   char           *prefix2        ,
		   long            prefixLen2     ,
		   bool            useStems       , 
		   bool            hashUniqueOnly ,
		   long            version        , // titleRecVersion ,
		   class Phrases  *phrases        ,
		   bool            hashWordIffNotInPhrase ,
		   long            niceness       ) {
	//if (g_pbuf) g_pbufPtr+=sprintf(g_pbufPtr,"<b>Words::hash()</b><br>");
	// don't hash if score is 0 or less.
	if ( baseScore <= 0 ) return true;

	// is the table storing the terms as strings, too? 
	// used by PageParser.cpp
	SafeBuf *pbuf = table->getParserBuf();

	// each word has a score (spam modified)
	long score;
	long score2;
	// the score from the Scores class
	long *wscores = NULL;
	long  norm    = DW; // NORM_WORD_SCORE;
	//if ( scores ) wscores = scores->m_scores;
	// point to word weights over score if we got them
	if ( weights ) {
		wscores = weights->m_ww;
		// set to default weight, DW, defined in Weights.h
		norm    = DW;
	}
	// the hash of each word
	long long h;
	// now hash each form of each word
	for (long i = 0 ; i < m_numWords; i++ ) {
		// don't hash punct words
		//if (m_isUnicode || m_version >= 67){
		//if (!ucIsWordChar(((UChar*)m_words[i])[0])) continue;
		if (!m_wordIds[i]) continue;
		
		// . if the word is not in a phrase and 
		//   "hashWordOnlyIfNotInPhrase" is true then don't hash it
		// . this is used in LinkInfo::hash() to hash link text
		if ( hashWordIffNotInPhrase && phrases->isInPhrase(i) )
			continue;
		// assume words has the baseScore
		score = baseScore;
		// modify score based on score vector... like Spam class
		// but top score is XXX. the score vector weights words in
		// different sections of the documents differently. sections
		// that have lots of unhyperlinked text weight highly. this
		// is used to strip out menus, etc. used to get articles for
		// the news collection.
		if ( wscores ) {
			// ignore word completely if score is 0
			if ( wscores[i] == 0 ) continue;
			// scale the final score if we should
			if ( wscores[i] != norm ) { // NORM_WORD_SCORE ) {
				// . we use -1 to mean to index with minimal 
				//   score but also to mean that it is not 
				//   visible
				// . used for things in <marquee> and <select> 
				// . see Scores.cpp
				//if ( wscores[i] == -1 ) score = 1;
				//score = (score * wscores[i]) >> 10;
				// TODO: can this wrap?
				score = (score * wscores[i]) / norm;
				// never decrease all the way to 0
				if (  score <= 0 ) score = 1;
			}
		}
		QUICKPOLL(niceness);
		// . hash the startHash with the wordId for this word
		// . we must mask it before adding it to the table because
		//   this table is also used to hash IndexLists into that come
		//   from LinkInfo classes (incoming link text). And when
		//   those IndexLists are hashed they used masked termIds.
		//   So we should too...
		//h = hash64 ( startHash , m_wordIds[i] ) & TERMID_MASK;
		h = g_indexdb.getTermId ( startHash , m_wordIds[i] ) ;
		//if (m_isUnicode && 
		//    (((UChar*)m_words[i])[0] == '1' ||
		//     ((UChar*)m_words[i])[0] == 's')){
		//		printf("Words::hash: starthash %lli prefix2 \"
		//               %10s\" wordId "
		//	       "(%lli) termId: (%lli) ", 
		//	       startHash, prefix2, m_wordIds[i], h);
		//	ucDebug(m_words[i], m_wordLens[i]);
		//}
		// . modify word's score based on the spam probability
		// . don't hash it if it's heavily spammed (spam of 100%)
		if ( spam && spam->getSpam(i) ) {
			score = score - (score*spam->getSpam(i)) / 100;
			if (  score <= 0 ) continue;
		}
		//if ( version >= 36 ) {
		score2 = score >> 1;
		if (score2 <= 0) score2 = 1;
		//}
		//else
		//	score2 = score;
		// debug, show the score for 'york'
		//if ( h == 25718418790376LL ) {
		//	long ww = -1;
		//	if ( wscores ) ww = wscores[i];
		//	logf(LOG_DEBUG,"build: adding %li for sex, wscore=%li "
		//	     "baseScore=%li",
		//	     score,ww,baseScore);
		//}
		
		// if we don't have to print out the parser info then
		// do not supply the term string to the table
		if ( ! pbuf ) {
			if ( ! table->addTerm(h,score,maxScore,hashUniqueOnly,
					      m_version ))
				return false;
			continue;
		}

		// . keep tabs on what we hash into the table if we need to
		// . store the term into term table
		long slen;
		char *s = table->storeTerm ( m_words[i], 
					     m_wordLens[i] ,
					     prefix1   , prefixLen1    ,
					     prefix2   , prefixLen2    , 
					     true, &slen);
		if(s == NULL) {
			g_errno = ENOMEM;
			return false;
		}
		if ( ! table->addTerm(h,score,maxScore,hashUniqueOnly,
				      m_version,s,slen))
			return false;			

		// sanity check
		//if ( h == 262515731587173LL ) {
		//	long nn = table->getScoreFromTermId ( h );
		//	logf(LOG_DEBUG,"build: score now %li",nn);
		//}
	}
	// return now if we don't have to print out spam info to parser buf
	if ( ! pbuf ) return true;
	if ( ! spam && ! weights ) return true;//scores ) return true;
	// new line for parser buf
	*pbuf += '\n';
	// print page as normal
	//char m_printTags = false;
	// print out each word and it's spam value, if we have spammed words!
	long i;
	for ( i = 0 ; i < m_numWords; i++ ) {
		// get the score, default it to 100
		long score  = 100;
		// phrase weight
		long pscore = 100;
		// NORM_WORD_SCORE is 128 last time i checked, this allows for
		// us to do fast integer operations with the resolution of a 
		// float
		//if ( scores  ) 
		//	score = (100 * scores->getScore(i)) / NORM_WORD_SCORE;
		if ( weights ) {
			// DW is the default word weight
			score  = (100 *weights->m_ww[i]    ) / DW;
			pscore = (100 *weights->m_pw[i]    ) / DW;
		}

		//if (m_wordIds[i] && (!scores || scores->getScore(i) > 0)){
		// show tags unrendered
		if ( ! pbuf->m_renderHtml && m_wordIds[i] ) {
			if (spam->getSpam(i) ) {
				pbuf->safePrintf("<span class=\"spam\">"
						 "<strike>");
			}
			else{
				pbuf->safePrintf("<span class=\"token\">");
			}
		}
		else if ( ! pbuf->m_renderHtml && m_tagIds && m_tagIds[i] ) {
			if (m_tagIds[i] == TAG_COMMENT) 
				pbuf->safePrintf("<span class=\"gbcomment\">");
			else
				pbuf->safePrintf("<span class=\"gbtag\">");
		}
		
		for ( long j = 0 ; j < m_wordLens[i] ; j++ ) {
			UChar32 c = (unsigned char)m_words[i][j];
			// print the tag au natural if we should
			if ( pbuf->m_renderHtml ) { // ! m_printTags ) {
				c = fixWindows1252(c);
				pbuf->utf32Encode(c);
				continue;
			}
			if (c == '<'){
				pbuf->safePrintf("&lt;");
			}
			else if (c == '>'){
				pbuf->safePrintf("&gt;");
			}
			else if (c == '&'){
				pbuf->safePrintf("&amp;");
			}
			else{
				c = fixWindows1252(c);
				pbuf->utf32Encode(c);
			}
		}


		if ((m_tagIds && m_tagIds[i]) || ! m_wordIds[i] ) {
			if ( pscore != 0 ) {
				//long tt=((long)scores->getScore(i)*100)/
				//NORM_WORD_SCORE;
				//long tt = 0;
				//if(scores) tt = scores->getScore(i);
				//else tt = score;
				//tt = score;
				//if ( tt == 0 ) tt = 1;
				//pbuf->safePrintf("<font size=-7 color=red>"
				//		 "%li</font>",
				//		 pscore);
				//if ( scores )
				//	pbuf->safePrintf(
				//		 "<font size=-7 color=green>"
				//		 "%li</font>",
				//		 scores->m_scores[i]);
				pbuf->safePrintf("<font size=-7>#%li</font>",i);
			}
			if ( ! pbuf->m_renderHtml ) // ! m_printTags )
				pbuf->safePrintf("</span>\n");
		}
		//if (m_wordIds[i] && (!scores || scores->getScore(i) > 0) ){
		if (m_wordIds[i] ) { // && score ) {
			if ( m_wordIds[i] && spam->getSpam(i) ) {
				pbuf->safePrintf("</strike>[%li]",
					(long)spam->getSpam(i));
			}
			//if (m_wordIds[i] && (!scores || scores->getScore(i) 
			// > 0) ){
			//if(scores && scores->getScore(i) != NORM_WORD_SCORE){
			//if ( score != 0 || pscore != 0 ) {
			//long tt=((long)scores->getScore(i)*100)/
			//NORM_WORD_SCORE;
			long tt = 0;
			//if(scores) tt = scores->getScore(i);
			//else tt = score;
			tt = score;
			if ( tt == 0 ) tt = 1;
			pbuf->safePrintf("<font size=-7 color=red>"
					 "%li/%li</font>",
					 score,pscore);
			//if ( scores )
			//	pbuf->safePrintf("<font size=-7 color=green>"
			//			 "%li</font>",
			//			 (long)scores->m_scores[i]);
			pbuf->safePrintf("<font size=-7>#%li</font>",i);
			//}
			if ( ! pbuf->m_renderHtml ) // ! m_printTags )
				pbuf->safePrintf("</span>\n");
		}
	}
	// end with a <br>
	pbuf->safePrintf ( "<br><br><br>" );
	if ( i >= m_numWords ) return true;
	// otherwise print a msg if breaked out
	pbuf->safePrintf("<br><b>... out of memory</b><br>");
	return true;
}
*/

////////////////////////////////////////////////////////////
// 
// the new faster words setter. 
// old was taking 346 cycles per word
//
////////////////////////////////////////////////////////////

bool Words::set2 ( Xml *xml, 
		   bool computeWordIds ,
		   long niceness) {
	reset();
	m_xml = xml;
	m_version = xml->getVersion();
	m_version = xml->getVersion();
	register char *p = (char *)xml->getContent();
	if ( *p ) p++;
	register long x = 0;
 ploop:
	//if ( is_alnum(*(p-1)) ^ is_alnum(*p) ) x++;
	//if ( is_alnum(*p ) ) x++;
	//x += g_map_is_alpha[*p] ;
	if ( is_alnum_utf8(p) ) x++;
	//if ( isalnum(*p) ) x++;
	//if ( g_map_is_alpha[*p] ) x++;
	//x++;
	p++;
	if ( *p ) goto ploop;

	m_preCount = x;
	m_preCount = xml->getContentLen() / 2;
	//if ( m_preCount > 9000 ) m_preCount = 9000;
	//m_preCount = 9000;

	if (!allocateWordBuffers(m_preCount, true)) return false;
	
	long numNodes = xml->getNumNodes();
	// are we done?
	for ( long k = 0 ; k < numNodes && m_numWords < m_preCount ; k++ ) {
		// get the kth node
		char *node    = xml->getNode   (k);
		long  nodeLen = xml->getNodeLen(k);
		// is the kth node a tag?
		if ( xml->isTag(k) ) {
			m_words         [m_numWords] = node;
			m_wordLens      [m_numWords] = nodeLen;
			m_tagIds        [m_numWords] = xml->getNodeId(k);
			m_wordIds       [m_numWords] = 0LL;
			m_nodes         [m_numWords] = k;
			// we have less than 127 HTML tags, so set 
			// the high bit for back tags
			if ( xml->isBackTag(k)) {
				m_tagIds[m_numWords] |= BACKBIT;
			}

			//log(LOG_DEBUG, "Words: Word %ld: got tag %s%s (%d)", 
			//    m_numWords,
			//    isBackTag(m_numWords)?"/":"",
			//    g_nodes[getTagId(m_numWords)].m_nodeName,
			//    getTagId(m_numWords));

			m_numWords++;
			// used by XmlDoc.cpp
			m_numTags++;
			continue;
		}
		// otherwise it's a text node
		char c = node[nodeLen];
		node[nodeLen] = '\0';
		addWords(node, nodeLen,computeWordIds, niceness);
		node[nodeLen] = c;
	}
	return true;
}

long Words::isFloat  ( long n, float& f) {
	char buf[128];
	char *p = buf;
	long offset = 0;
	while(isPunct(n+offset) && 
	      !(m_words[n+offset][0] == '.' || 
		m_words[n+offset][0] == '-')) offset++;

	while(isPunct(n+offset) && 
	      !(m_words[n+offset][0] == '.' || 
		m_words[n+offset][0] == '-')) offset++;


	memcpy(buf, getWord(n), getWordLen(n));
	buf[getWordLen(n)] = '\0';
	log(LOG_WARN, "trying to get %s %li", buf, offset);
	

	if(isNum(n)) {
		if(1 + n < m_numWords && 
		   isPunct(n+1) && m_words[n+1][0] == '.') {
			if(2 + n < m_numWords && isNum(n+2)) {
				memcpy(p, m_words[n], m_wordLens[n]);
				p += m_wordLens[n];
				memcpy(p, ".", 1);
				p++;
				memcpy(p, m_words[n+2], m_wordLens[n+2]);
				f = atof(buf);
				return 3 + offset;
			}
			else {
				return offset;
			}
		} else if(n > 0 && isPunct(n-1) && m_wordLens[n-1] > 0 &&
			  (m_words[n-1][m_wordLens[n-1]-1] == '.' ||
			   m_words[n-1][m_wordLens[n-1]-1] == '-')) {
			//hmm, we just skipped the period as punct?
			sprintf(buf, "0.%s",m_words[n]);
			f = atof(buf);
			return 1 + offset;
		}
		else {
			f = atof(m_words[n]);
			return 1 + offset;
		}
	}

	//does this have a period in front?
	if(isPunct(n) && (m_words[n][0] == '.' || m_words[n][0] == '-')) {
		if(1 + n < m_numWords && isNum(n+1)) {
			memcpy(p, m_words[n], m_wordLens[n]);
			p += m_wordLens[n];
			memcpy(p, m_words[n+1], m_wordLens[n+1]);
			f = atof(buf);
			return 2 + offset;
		}
	}
	return offset;
}

static uint8_t s_findMaxIndex(long long *array, int num, int *wantmax = NULL) {
	if(!array || num < 2 || num > 255) return(0);
	long long max, oldmax;
	int idx = 0;
	max = oldmax = INT_MIN;
	for(int x = 0; x < num; x++) {
		if(array[x] >= max) {
			oldmax = max;
			max = array[x];
			idx = x;
		}
	}
	if(max == 0) return(0);
	if(max == oldmax) return(0);
	if(wantmax) *wantmax = max;
	return((uint8_t)idx);
}

//static bool s_isWordCap ( char *word , int len ) {
//	if ( ! is_upper_utf8 ( word ) ) return false;
//	long cs = getUtf8CharSize ( word );
//	if ( is_lower_utf8 ( &word[cs] ) ) return true;
//	return false;
//}

unsigned char Words::isBounded(int wordi) {
	if(wordi+1 < m_numWords &&
	   getWord(wordi)[getWordLen(wordi)] == '/' //||
	    //getWord(wordi)[getWordLen(wordi)] == '?'
	   )
		return(true);
	if(wordi+1 < m_numWords &&
	   (getWord(wordi)[getWordLen(wordi)] == '.' ||
	    getWord(wordi)[getWordLen(wordi)] == '?') &&
	   is_alnum_a(getWord(wordi)[getWordLen(wordi)+1]) )
		return(true);
	if(wordi > 0 &&
	   (getWord(wordi)[-1] == '/' ||
	    getWord(wordi)[-1] == '?'))
		return(true);

	return(false);
}

unsigned char getCharacterLanguage ( char *utf8Char ) {
	// romantic?
	char cs = getUtf8CharSize ( utf8Char );
	// can't say what language it is
	if ( cs == 1 ) return langUnknown;
	// convert to 32 bit unicode
	UChar32 c = utf8Decode ( utf8Char );
	UCScript us = ucGetScript ( c );
	// arabic? this also returns for persian!! fix?
	if ( us == ucScriptArabic ) 
		return langArabic;
	if ( us == ucScriptCyrillic )
		return langRussian;
	if ( us == ucScriptHebrew )
		return langHebrew;
	if ( us == ucScriptGreek )
		return langGreek;

	return langUnknown;
}

// returns -1 and sets g_errno on error, because 0 means langUnknown
long Words::getLanguage( Sections *sections ,
			 long maxSamples,
			 long niceness,
			 long *langScore) {
	// calculate scores if not given
	//Scores calcdScores;
	//if ( ! scores ) {
	//	if ( ! calcdScores.set( this,m_version,false ) )
	//		return -1;
	//	scores = &calcdScores;
	//}

	// . take a random sample of words and look them up in the
	//   language dictionary
	//HashTableT<long long, char> ht;
	HashTableX ht;
	long long langCount[MAX_LANGUAGES];
	long long langWorkArea[MAX_LANGUAGES];
	long numWords = m_numWords;
	//long skip = numWords/maxSamples;
	//if ( skip == 0 ) skip = 1;
	// reset the language count
	memset(langCount, 0, sizeof(long long)*MAX_LANGUAGES);
	// sample the words
	//long wordBase  = 0;
	long wordi     = 0;
	//if ( ! ht.set(maxSamples*1.5) ) return -1;
	if ( ! ht.set(8,1,maxSamples*8.0,NULL,0,false,niceness,"wordslang")) 
		return -1;
 
	// . avoid words in these bad sections
	// . google seems to index SEC_MARQUEE so i took that out of badFlags
	long badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT;
	// shortcuts
	long long *wids  = m_wordIds;
	long      *wlens = m_wordLens;
	char     **wptrs = m_words;

	//long langTotal = 0;
// 	log ( LOG_WARN, "xmldoc: Picking language from %li words with %li skip",
// 			numWords, skip );
	char numOne = 1;
	Section **sp = NULL;
	if ( sections ) sp = sections->m_sectionPtrs;
	// this means null too
	if ( sections && sections->m_numSections == 0 ) sp = NULL;

	long maxCount = 1000;

	while ( wordi < numWords ) {
		// breathe
		QUICKPOLL( niceness );
		// move to the next valid word
		if ( ! wids [wordi]     ) { wordi++; continue; }
		if (   wlens[wordi] < 2 ) { wordi++; continue; }
		// skip if in a bad section
		//long flags = sections->m_sectionPtrs[i]->m_flags;
		// meaning script section ,etc
		if ( sp && ( sp[wordi]->m_flags & badFlags ) ) {
			wordi++; continue; }
		// check the language
		//unsigned char lang = 0;

		// Skip if word is capitalized and not preceded by a tag
		//if(s_isWordCap(getWord(wordi), getWordLen(wordi)) &&
		//   wordi > 0 && !getTagId(wordi - 1)) {
		//	wordi++;
		//	continue;
		//}

		// Skip word if bounded by '/' or '?' might be in a URL
		if(isBounded(wordi)) {
			wordi++;
			continue;
		}

		// is it arabic? sometimes they are spammy pages and repeat
		// a few arabic words over and over again, so don't do deduping
		// with "ht" before checking this.
		char cl = getCharacterLanguage ( wptrs[wordi] );
		if ( cl ) {
		        langCount[(unsigned char)cl]++;
			wordi++;
			continue;
		}

		//if(ht.getSlot(m_wordIds[wordi]) !=-1) {
		if(!ht.isEmpty(&m_wordIds[wordi]) ) {
			wordi++;
			continue;
		}

		// If we can't add the word, it's not that bad.
		// Just gripe about it in the log.
		if(!ht.addKey(&m_wordIds[wordi], &numOne)) {
			log(LOG_WARN, "build: Could not add word to temporary "
			    "table, memory error?\n");
			g_errno = ENOMEM;
			return -1;
		}

		if ( maxCount-- <= 0 ) break;

		// No lang from charset, got a phrase, and 0 language does not have 
		// a score Order is very important!
		int foundone = 0;
		if ( // lang == 0 &&
		    // we seem to be missing hungarian and thai
		    g_speller.getPhraseLanguages(getWord(wordi),
						 getWordLen(wordi), 
						 langWorkArea) &&
		    // why must it have an "unknown score" of 0?
		    // allow -1... i don't know what that means!!
		    langWorkArea[0] <= 0) {
			
			int lasty = -1;
			for(int y = 1; y < MAX_LANGUAGES; y++) {
				if(langWorkArea[y] == 0) continue;
				langCount[y]++;
				long pop = langWorkArea[y];
				// negative means in an official dictionary
				if ( pop < 0 ) {
					pop *= -1;
					langCount[y] += 1;
				}
				// extra?
				if ( pop > 1000 )
					langCount[y] += 2;
				if ( pop > 10000 )
					langCount[y] += 2;
				lasty = y;
				foundone++;
			}
			// . if it can only belong to one language
			// . helps fix that fact that our unifiedDict is crummy
			//   and identifes some words as being in a lot of languages
			//   like "Pronto" as being in english and not giving
			//   the popularities correctly.
			if ( foundone == 1 )
				// give massive boost
				langCount[lasty] += 10;
		}
		// . try to skip unknown words without killing sample size
		// . we lack russian, hungarian and arabic in the unified
		//   dict, so try to do character detection for those langs.
		// . should prevent them from being detected as unknown
		//   langs and coming up for english search 'gigablast'
		if ( ! foundone ) {
			langCount[langUnknown]++;
			// do not count towards sample size
			maxCount++;
		}

		// skip to the next word
		//wordBase += skip;
		//if ( wordi < wordBase )
		//	wordi = wordBase;
		//else
		wordi++;
	}
	// punish unknown count in case a doc has a lot of proper names
	// or something
	//langCount[langUnknown] /= 2;
	// get the lang with the max score then
	int l = s_findMaxIndex(langCount, MAX_LANGUAGES);
	// if(langCount[l] < 15) return(langUnknown);
	if(langScore) *langScore = langCount[l];
	// return if known now
	return l;
}

// get the word index at the given character position 
long Words::getWordAt ( char *p ) { // long charPos ) {
	if ( ! p                  ) { char *xx=NULL;*xx=0; }
	if ( p <  m_words[0]      ) { char *xx=NULL;*xx=0; }
	if ( p >= getContentEnd() ) { char *xx=NULL;*xx=0; }
	
	long step = m_numWords / 2;
	long i = m_numWords / 2 ;

 loop:

	// divide it by 2 each time
	step >>= 1;
	// always at least one
	if ( step <= 0 ) step = 1;
	// is it a hit?
	if ( p >= m_words[i] && p < m_words[i] + m_wordLens[i] )
		return i;
	// compare
	if ( m_words[i] < p ) i += step;
	else                  i -= step;
	goto loop;
	return -1;
}


// . return the value of the specified "field" within this html tag, "s"
// . the case of "field" does not matter
char *getFieldValue ( char *s , 
		      long  slen ,
		      char *field , 
		      long *valueLen ) {
	// reset this to 0
	*valueLen = 0;
	// scan for the field name in our node
	long flen = gbstrlen(field);
	char inQuotes = '\0';
	long i;

	// make it sane
	if ( slen > 2000 ) slen = 2000;

	for ( i = 1; i + flen < slen ; i++ ) {
		// skip the field if it's quoted
		if ( inQuotes) {
			if (s[i] == inQuotes ) inQuotes = 0;
			continue;
		}
		// set inQuotes to the quote if we're in quotes
		if ( (s[i]=='\"' || s[i]=='\'')){ 
			inQuotes = s[i];
			continue;
		} 
		// if not in quote tag might end
		if ( s[i] == '>' && ! inQuotes ) return NULL;
		// a field name must be preceeded by non-alnum
		if ( is_alnum_a ( s[i-1] ) ) continue;
		// the first character of this field shout match field[0]
		if ( to_lower_a (s[i]) != to_lower_a(field[0] )) continue;
		// field just be immediately followed by an = or space
		if (s[i+flen]!='='&&!is_wspace_a(s[i+flen]))continue;
		// field names must match
		if ( strncasecmp ( &s[i], field, flen ) != 0 ) continue;
		// break cuz we got a match for our field name
		break;
	}
	
	
	// return NULL if no matching field
	if ( i + flen >= slen ) return NULL;

	// advance i over the fieldname so it pts to = or space
	i += flen;

	// advance i over spaces
	while ( i < slen && is_wspace_a ( s[i] ) ) i++;

	// advance over the equal sign, return NULL if does not exist
	if ( i < slen && s[i++] != '=' ) return NULL;

	// advance i over spaces after the equal sign
	while ( i < slen && is_wspace_a ( s[i] ) ) i++;
	
	// now parse out the value of this field (could be in quotes)
	inQuotes = '\0';

	// set inQuotes to the quote if we're in quotes
	if ( s[i]=='\"' || s[i]=='\'') inQuotes = s[i++]; 

	// mark this as the start of the value
	int start=i;

	// advance i until we hit a space, or we hit a that quote if inQuotes
	if (inQuotes) while (i<slen && s[i] != inQuotes ) i++;
	else while ( i<slen &&!is_wspace_a(s[i])&&s[i]!='>')i++;

	// set the length of the value
	*valueLen = i - start;

	// return a ptr to the value
	return s + start;
}
