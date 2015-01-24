#include "gb-include.h"

#include "Phrases.h"
#include "Mem.h"

Phrases::Phrases ( ) {
	m_buf = NULL;
	//m_phraseScores = NULL;
	m_phraseSpam   = NULL;
	//m_phraseIds    = NULL;
}

Phrases::~Phrases ( ) {
	reset();
}

void Phrases::reset() {
	if ( m_buf && m_buf != m_localBuf )
		mfree ( m_buf , m_bufSize , "Phrases" );
	m_buf = NULL;
	//m_phraseScores = NULL;
	m_phraseSpam   = NULL;
	//m_phraseIds    = NULL;
}

// initialize this token array with the string, "s" of length, "len".
bool Phrases::set( Words    *words, 
		   Bits     *bits ,
		   bool      useStopWords , 
		   bool      useStems     ,
		   int32_t      titleRecVersion,
		   int32_t      niceness) {
	// reset in case being re-used
	reset();
	// always reset this
	//m_phraseScores = NULL;
	m_phraseSpam   = NULL;
	//m_phraseIds    = NULL;
	// now we never use stop words and we just index two-word phrases
	// so that a search for "get a" in quotes will match a doc that has
	// the phrase "get a clue". it might impact performance, but it should
	// be insignificant... but we need to have this level of precision.
	// ok -- but what about 'kick a ball'. we might not have that phrase
	// in the results for "kick a" AND "a ball"!! so we really need to
	// index "kick a ball" as well as "kick a" and "a ball". i don't think
	// that will cause too much bloat.
	//useStopWords = false;
	// ensure we have words
	if ( ! words ) return true;
	// set the words' scores array, m_wordScores
	//if ( scores ) m_wordScores = scores->m_scores;
	//else          m_wordScores = NULL;
	// . we have one phrase per word
	// . a phrase #n is "empty" if spam[n] == PSKIP
	m_numPhrases = words->getNumWords();

	// replaces scores
	//m_sections    = m_sections;
	//m_sectionPtrs = NULL;
	//if ( m_sections ) m_sectionPtrs = m_sections->m_sectionPtrs;

	// how much mem do we need?
	//int32_t need = (18+1+(3+8*3)) * m_numPhrases;
	int32_t need = m_numPhrases * (8+8+1+1+1);
	//if ( m_wordScores ) need += 4 * m_numPhrases;

	// alloc if we need to
	if ( need > PHRASE_BUF_SIZE ) 
		m_buf = (char *)mmalloc ( need , "Phrases" );
	else
		m_buf = m_localBuf;

	if ( ! m_buf ) 
		return log("query: Phrases::set: %s",mstrerror(g_errno));
	m_bufSize = need;
	// set up arrays
	char *p = m_buf;
	//m_phraseIds      = (int64_t *)p ; p += m_numPhrases * 8;
	// phrase not using stop words
	m_phraseIds2     = (int64_t *)p ; p += m_numPhrases * 8;
	m_phraseIds3     = (int64_t *)p ; p += m_numPhrases * 8;
	//m_phraseIds4     = (int64_t *)p ; p += m_numPhrases * 8;
	//m_phraseIds5     = (int64_t *)p ; p += m_numPhrases * 8;
	//m_stripPhraseIds = (int64_t *)p ; p += m_numPhrases * 8;
	//if ( m_wordScores ) {
	//	m_phraseScores  = (int32_t  *)p ;
	//	p += m_numPhrases * 4;
	//}
	m_phraseSpam    = (unsigned char *)p ; p += m_numPhrases * 1;
	//m_numWordsTotal = (unsigned char *)p ; p += m_numPhrases * 1;
	m_numWordsTotal2= (unsigned char *)p ; p += m_numPhrases * 1;
	m_numWordsTotal3= (unsigned char *)p ; p += m_numPhrases * 1;
	//m_numWordsTotal4= (unsigned char *)p ; p += m_numPhrases * 1;
	//m_numWordsTotal5= (unsigned char *)p ; p += m_numPhrases * 1;

	// sanity
	if ( p != m_buf + need ) { char *xx=NULL;*xx=0; }

	// clear this
	//memset ( m_numWordsTotal , 0 , m_numPhrases );

	memset ( m_numWordsTotal2 , 0 , m_numPhrases );
	memset ( m_numWordsTotal3 , 0 , m_numPhrases );
	//memset ( m_numWordsTotal4 , 0 , m_numPhrases );
	//memset ( m_numWordsTotal5 , 0 , m_numPhrases );
	
	// point to this info while we parse
	m_words        = words;
	m_wptrs        = words->getWords();
	m_wlens        = words->getWordLens();
	m_wids         = words->getWordIds();
	m_bits         = bits;
	m_useStopWords = useStopWords;
	m_useStems     = useStems;
	// we now are dependent on this
	m_titleRecVersion = titleRecVersion;
	// . set the phrases
	// . sets m_phraseIds [i]
	// . sets m_phraseSpam[i] to PSKIP if NO phrase exists
	for ( int32_t i = 0 ; i < words->getNumWords() ; i++ ) {
		if ( ! m_wids[i] ) continue;
		setPhrase ( i , niceness);
	}
	// success
	return true;
}

// . add the phrase that starts with the ith word
// . "read Of Mice and Men" should make 3 phrases:
// . read.ofmice
// . ofmice
// . mice.andmen
void Phrases::setPhrase ( int32_t i, int32_t niceness ) {
	// . if the ith word cannot start a phrase then we have no phrase
	// . we indicate NULL phrasesIds with a spam of PSKIP
	// . we now index all regardless! we want to be able to search
	//   for "a thing" or something. so do it!
	//if ( ! m_bits->canStartPhrase ( i ) ) {
	//	m_phraseSpam[i] = PSKIP; 
	//	m_phraseIds [i] = 0LL;
	//	return;
	//}

	// MDW: now Weights.cpp should encompass all this logic
	// or if score <= 0, set in Scores.cpp
	//if ( m_wordScores && m_wordScores[i] <= 0 ) {
	//	m_phraseSpam[i] = PSKIP; 
	//	m_phraseIds [i] = 0LL;
	//	return;
	//}

	// hash of the phrase
	int64_t h   = 0LL; 
	// the hash of the two-word phrase (now we do 3,4 and 5 word phrases)
	int64_t h2  = 0LL; 
	int64_t h3  = 0LL; 
	//int64_t h4  = 0LL; 
	//int64_t h5  = 0LL; 
	// reset
	unsigned char pos = 0;
	// now look for other tokens that should follow the ith token
	int32_t          nw               = m_words->getNumWords();
	int32_t          numWordsInPhrase = 1;
	// use the min spam from all words in the phrase as the spam for phrase
	char minSpam = -1;
	// we need to hash "1 / 8" differently from "1.8" from "1,000" etc.
	char isNum = is_digit(m_wptrs[i][0]);
	// min score
	//int32_t minScore ;
	//if ( m_wordScores ) minScore = m_wordScores[i];
	// if i is not a stop word, it can set the min spam initially
	//if ( ! m_bits->isStopWord(i) &&m_spam ) minSpam = m_spam->getSpam(i);
	// do not include punct/tag words in the m_numWordsTotal[j] count
	// of the total words in the phrase. these are just usesless tails.
	int32_t lastWordj = -1;
	// loop over following words
	int32_t j;
	bool hasHyphen ;
	bool hasStopWord2 ;

	// . NOTE: a token can start a phrase but NOT be in it. 
	// . like a large number for example.
	// . wordId is the lower ascii hash of the ith word
	// . NO... this is allowing the query operator PiiPe to start
	//   a phrase but not be in it, then the phrase id ends up just
	//   being the following word's id. causing the synonyms code to
	//   give a synonym which it should not un Synonyms::set()
	if ( ! m_bits->canBeInPhrase(i) )
		// so indeed, skip it then
		goto nophrase;

	//h = hash64 ( h, m_words->getWordId(i));
	h = m_wids[i];
	// set position
	pos = (unsigned char)m_wlens[i];
	//if (m_words->getStripWordId(i)) 
	//	h2 = hash64 ( h2, m_words->getStripWordId(i));
	//else h2 = h;

	hasHyphen = false;
	hasStopWord2 = m_bits->isStopWord(i);
	// this makes it true now too
	//if ( m_wlens[i] <= 2 ) hasStopWord = true;

	for ( j = i + 1 ; j < nw ; j++ ) {
		QUICKPOLL(niceness);

		// . do not allow more than 32 alnum/punct "words" in a phrase
		// . this prevents phrases with 100,000 words from slowing
		//   us down. would put us in a huge double-nested for loop
		if ( j > i + 32 ) goto nophrase;
		// deal with punct words
		if ( ! m_wids[j] ) {
			// if we cannot pair across word j then break
			if ( ! m_bits->canPairAcross (j) ) break;
			// does it have a hyphen?
			if (j==i+1 && m_words->hasChar(j,'-')) hasHyphen=true;
			/*
			// "D & B" --> dandb
			if (j==i+1 && m_words->hasChar(j,'&')) {
				// set this
				hasStopWord = true;
				// insert "and"
				int32_t conti=pos;
				h = hash64Lower_utf8_cont("and",3,h,&conti);
				pos=conti;
				// the two-word phrase, set it if we need to
				h2 = h;
				m_numWordsTotal2[i] = j-i+1;
			}
			*/
			continue;
		}
		// . if this word can not be in a phrase then continue our 
		//   search for a word that can
		// . no punctuation can be in a phrase currently (++?)
		//if ( m_bits->canBeInPhrase (j) ) {
		//}

		// keep this set right
		//if (m_bits->isStopWord(j)||m_wlens[j]<=2) hasStopWord = true;
		//if ( m_bits->isStopWord(j) ) hasStopWord = true;

		// record lastWordj to indicate that word #j was a true word
		lastWordj = j;
		// . stop words should have a 0 spam value so don't count those
		// . added by mdw in march 2002
		/*
		if ( ! m_bits->isStopWord(j) && m_spam ) {
			// maintain the min spam
			char spam  = m_spam->getSpam ( j );
			if ( minSpam == -1 || spam < minSpam ) minSpam = spam;
			// . min weight from score vector
			// . normal score here is 256, not 128, so shift
			//   down 3 to normalize it relatively
			//if ( m_wordScores && (m_wordScores[j]>>3)<minScore) 
			//	minScore = m_wordScores[j]>>3;
			//if ( m_wordScores && m_wordScores[j] < minScore ) 
			//	minScore = m_wordScores[j];
		}
		*/
		// if word #j can be in phrase then incorporate it's hash
		if ( m_bits->canBeInPhrase (j) ) {
			// continue the hash
		        //unsigned char *p= (unsigned char *)m_wptrs[j];
			//unsigned char *pend = p + m_wlens[j];
			//for ( ; p < pend ; p++ ) 
			//	h ^= g_hashtab[pos++][*p];

			int32_t conti = pos;

			// . get the punctuation mark separting two numbers
			// . use space if can't find one
			// . 1/234 1,234 1.234 10/11 "1 234" 1-5
			//if (isNum && j==i + 2 && is_digit(m_wptrs[j][0]) ) {
			//	// get punct mark
			//	char c = m_wptrs[i+1][0];
			//	// if space try next
			//	if(c==' '&&m_wlens[i+1]>1) c=m_wptrs[i+1][1];
			//	// treat comma as nothing
			//	if ( c==',' ) c='\0';
			//	// treat / and . and - as they are, everything
			//	// else should be treated as a space
			//	else if(c!='/'&&c !='.'&& c!='-'&&c!=':')c=' ';
			//	// incorporate into hash if c is there
			//	if (c)h=hash64Lower_utf8_cont(&c,1,h,&conti);
			//}

			// hash the jth word into the hash
			h = hash64Lower_utf8_cont(m_wptrs[j], 
						  m_wlens[j],
						  h,
						  &conti );
			pos = conti;
			//h = hash64 ( h , m_words->getWordId (j) );
			//if (m_words->getStripWordId(j)) 
			//	h2 = hash64 ( h2, m_words->getStripWordId(j));
			//else h2 = hash64(h2, m_words->getWordId(j));
			numWordsInPhrase++;

			// N-word phrases?
			if ( numWordsInPhrase == 2 ) { // h != h2 ) {
				h2 = h;
				m_numWordsTotal2[i] = j-i+1;
				if ( m_bits->isStopWord(j) ) 
					hasStopWord2 = true;
				continue;
			}
			if ( numWordsInPhrase == 3 ) {
				h3 = h;
				m_numWordsTotal3[i] = j-i+1;
				//continue;
				break;
			}
			/*
			if ( numWordsInPhrase == 4 ) {
				h4 = h;
				m_numWordsTotal4[i] = j-i+1;
				continue;
			}
			if ( numWordsInPhrase == 5 ) {
				h5 = h;
				m_numWordsTotal5[i] = j-i+1;
				continue;
			}
			*/
		}
		// if we cannot pair across word j then break
		if ( ! m_bits->canPairAcross (j) ) break;
		// keep chugging?
		if ( numWordsInPhrase >= 5 ) {
			// if we're not using stop words then break
			if ( ! m_useStopWords ) break;
			// if it's not a stop word then break
			if ( ! m_bits->isStopWord (j) ) break;
		}
		// otherwise, get the next word
	}
	// if we had no phrase then use 0 as id (need 2+ words to be a pharse)
	if ( numWordsInPhrase <= 1 ) { 
	nophrase:
		m_phraseSpam[i]      = PSKIP; 
		//m_phraseIds [i]      = 0LL; 
		m_phraseIds2[i]      = 0LL; 
		m_phraseIds3[i]      = 0LL; 
		//m_stripPhraseIds [i] = 0LL; 
		//m_numWordsTotal[i]   = 0;
		m_numWordsTotal2[i]   = 0;
		m_numWordsTotal3[i]   = 0;
		return;
	}
	// don't jump the edge
	//if ( j >= nw ) j = nw - 1;
	// sanity check
	if ( lastWordj == -1 ) { char *xx = NULL; *xx = 0; }
	// set the phrase length (from word #i upto & including word #j)
	//m_numWordsTotal[i] = j - i + 1;
	//m_numWordsTotal [i] = lastWordj - i + 1;
	// sanity check
	if ( lastWordj - i + 1 > 255 ) { char *xx=NULL;*xx=0; }
	// set the phrase spam
	if ( minSpam == -1 ) minSpam = 0;
	m_phraseSpam[i] = minSpam;
	// return the phraseId
	//m_phraseIds [i] = h;
	// hyphen between numbers does not count (so 1-2 != 12)
	if ( isNum ) hasHyphen = false;
	// . the two word phrase id
	// . "cd rom"    -> cdrom
	// . "fly paper" -> flypaper
	// . "i-phone"   -> iphone
	// . "e-mail"    -> email
	if ( hasHyphen || ! hasStopWord2 ) {
		//m_phraseIds [i] = h;
		m_phraseIds2[i] = h2;
	}
	// . "st. and"    !-> stand
	// . "the rapist" !-> therapist
	else {
		//m_phraseIds [i] = h  ^ 0x768867;
		m_phraseIds2[i] = h2 ^ 0x768867;
	}
	// forget hyphen logic for these
	m_phraseIds3[i] = h3;
	//m_phraseIds4[i] = h4;
	//m_phraseIds5[i] = h5;

	//if ( h != h2 ) m_stripPhraseIds[i] = h2;
	//else m_stripPhraseIds[i] = 0LL;
		
	// the score weight, if any
	//if ( m_phraseScores ) m_phraseScores [i] = minScore;
	// sanity check
	//if(m_phraseScores && minScore == 0x7fffffff ) {char *xx =NULL;*xx=0;}
	// debug msg
	//char *w = m_words->getWord(i) ;
	//int32_t  wlen = m_words->getWordLen(i) ; 
	//for ( int32_t k = 0 ; k < wlen ; k++ )
	//	fprintf(stderr,"%c",w[k]);
	//fprintf(stderr,"--> hash=%"UINT64"\n",(uint64_t)h);
}

// . store phrase that starts with word #i into "printBuf"
// . return bytes stored in "printBuf"
char *Phrases::getPhrase ( int32_t i , int32_t *phrLen , int32_t npw ) {
	// return 0 if no phrase
	if ( m_phraseSpam[i] == PSKIP ) return NULL;
	// store the phrase in here
	static char buf[256];
	// . how many words, including punct words, are in phrase?
	// . this should never be 1 or less
	//int32_t  n     = m_numWordsTotal[i] ;
	int32_t  n ;
	if      ( npw == 2 ) n = m_numWordsTotal2[i] ;
	else if ( npw == 3 ) n = m_numWordsTotal3[i] ;
	else { char *xx=NULL; *xx=0; }
	//char *w1    = m_words->getWord(i);
	//char *w2    = m_words->getWord(i+n-1);
	//int32_t  wlen2 = m_words->getWordLen(i+n-1);
	//int32_t  plen  = ( w2 - w1 ) + wlen2;

	char *s     = buf;
	char *send  = buf + 255;
	for (int32_t w = i;w<i+n;w++){
		if (!m_words->isAlnum(w)){
			// skip spaces for now since we has altogether now
			*s++ = ' ';
			/*
			// . get the punctuation mark separting two numbers
			// . use space if can't find one
			// . 1/234 1,234 1.234 10/11 "1 234" 1-5
			if ( is_digit(m_wptrs[i][0]) && w == i + 1 && 
			     is_digit(m_wptrs[i+2][0]) ) {
				// get punct mark
				char c = m_wptrs[i+1][0];
				// if space try next
				if(c==' '&&m_wlens[i+1]>1) c=m_wptrs[i+1][1];
				// treat comma as nothing
				if ( c==',' ) continue;//c='\0';
				// treat / and . and - as they are, everything
				// else should be treated as a space
				else if(c!='/'&&c !='.'&& c!='-'&&c!=':')c=' ';
				// print that
				*s++ = c;
			}
			*/
			continue;
		}
		char *w1   = m_words->getWord(w);
		char *wend = w1 + m_words->getWordLen(w);
		for ( int32_t j = 0 ; j < m_words->getWordLen(w) && s<send ; j++){
			// write the lower case char from w1+j into "s"
			int32_t size = to_lower_utf8 ( s , send , w1 + j , wend );
			// advance
			j += size;
			s += size;
		}
	}
	// null terminate
	*s = '\0';
	// set length we wrote into "buf"
	*phrLen = s - buf;

	// return ptr to buf
	return buf;
}

/*
// . store phrase that starts with word #i into "printBuf"
// . return bytes stored in "printBuf"
char *Phrases::getNWordPhrase ( int32_t i , int32_t *phrLen , int32_t npw ) {
	// return 0 if no phrase
	if ( m_phraseSpam[i] == PSKIP ) return NULL;
	// store the phrase in here
	static char buf[512];
	// . how many words, including punct words, are in phrase?
	// . this should never be 1 or less
        int32_t  n     = m_numWordsTotal[i] ;
	char *dst   = buf;
	char *dend  = buf + 255;
	int32_t  count = 0;
	for (int32_t w = i; w<i+n && count<npw; w++ ) {
		// do not breach the buffer
		if ( dst + 4 >= dend ) break;
		// all non alnum chars are spaces now
		if ( ! m_words->isAlnum(w) ) { 
			// skip spaces for now since we has altogether now
			*dst++ = ' '; 
			continue; 
		}
		count++;
		char *w1   = m_words->getWord(w);
		int32_t  wlen = m_words->getWordLen(w);
		// store the word in lower case into "dst"
		to_lower_utf8 ( dst , dend , w1 , w1 + wlen );
		// advance destination cursor
		dst += wlen;
	}
	// null terminate
	*dst = '\0';
	// set length we wrote into "buf"
	*phrLen = dst - buf;
	// return ptr to buf
	return buf;
}
*/

/*
char *Phrases::getStripPhrase ( int32_t i , int32_t *phrLen ) {
	// return 0 if no phrase
	if ( m_phraseSpam[i] == PSKIP ) return NULL;
	// store the phrase in here
	static char buf[512];
	// . how many words, including punct words, are in phrase?
	// . this should never be 1 or less
        int32_t  n     = m_numWordsTotal[i] ;
	//char *w1    = m_words->getWord(i);
	//char *w2    = m_words->getWord(i+n-1);
	//int32_t  wlen2 = m_words->getWordLen(i+n-1);
	//int32_t  plen  = ( w2 - w1 ) + wlen2;

	char *s     = buf;
	char *send  = buf + 255;
	for (int32_t w = i;w<i+n;w++){
		if (!m_words->isAlnum(w)){
			*s++ = ' ';
			continue;
		}
		char *w1 = m_words->getWord(w);

		for ( int32_t j = 0 ; j < m_words->getWordLen(w) && s<send ; j++){
			// write the lower case char from w1+j into "s"
			int32_t size = to_lower_ascii_utf8 ( s , send , w1 + j );
			// advance
			j += size;
			s += size;
		}
	}
	// null terminate
	*s = '\0';
	// set length we wrote into "buf"
	*phrLen = s - buf;
	
	// return ptr to buf
	return buf;
}
*/

/*
// for getTermId()
#include "Indexdb.h" 

// . hash all the words into "table"
bool Phrases::hash ( TermTable      *table          , 
		     Weights        *weightsPtr     ,
		     uint32_t   baseScore      ,
		     uint32_t   maxScore       ,
		     int64_t       startHash      ,
		     char           *prefix1        ,
		     int32_t            prefixLen1     ,
		     char           *prefix2        ,
		     int32_t            prefixLen2     ,
		     bool            hashUniqueOnly ,
		     int32_t            titleRecVersion,
		     int32_t            niceness       ) {

	// don't hash if score is 0 or less.
	if (baseScore <= 0) return true;

	// point to the phrase weights array, m_pw[]
	int32_t *weights = NULL;
	if ( weightsPtr ) weights = weightsPtr->m_pw;

	// is the table storing the terms as strings, too? used by 
	// PageParser.cpp
	SafeBuf *pbuf = table->getParserBuf();

	// . now add each phraseId to the index table
	// . TODO: might want to add w/ uniqueOnly on if spam is 100%
	uint32_t score;
	bool huo;
	for (int32_t i =0; i < m_numPhrases; i++) {
		// should we hash this phraseId only if it's not hashed yet?
		huo = hashUniqueOnly;
		// a phraseSpam of PSKIP means word #i does not start a phrase
		if ( m_phraseSpam[i] == PSKIP ) continue;
		// don't hash it if it's heavily spammed (spam of 100%)
		score = baseScore - ( baseScore * m_phraseSpam[i] ) / 100;
		// . use weights instead if we have them
		// . default weight should be 128!
		if ( weights ) {
			// skip if the weight is 0, we probably have menu 
			// eelimination technology turned on...
			if ( weights[i] == 0 ) continue;
			// . the old way:  we used a signed int32_t which could
			//   overflow before the divide and make artificially 
			//   high term scores
			//if(titleRecVersion < 85)
			//	score = (int32_t)((int32_t)score * weights[i]) / DW;
			//else    score = (score * weights[i]) / DW;
			score = (score * weights[i]) / DW;
		}

		// weight by score if we need to

		// if score is 0 because it's heavily spammed then we
		// should hash just enough to index the phrase
		if ( ! score ) { score = 1; huo = true; }
		// get the phrase hash (includes coll,field prefixes)
		int64_t h = g_indexdb.getTermId (startHash ,m_phraseIds[i]) ;
		
		//int64_t h2 = 0LL;
		//if (m_stripPhraseIds[i])
		//	h2 = g_indexdb.getTermId (startHash ,
		//				  m_stripPhraseIds[i]) ;
		int64_t h2 = g_indexdb.getTermId(startHash,m_phraseIds2[i]);
		// we must mask it before adding it to the table because
		// this table is also used to hash IndexLists into that come
		// from LinkInfo classes (incoming link text). And when
		// those IndexLists are hashed they used masked termIds.
		// So we should too...
		//h = h & TERMID_MASK;
		// add to table
		//int32_t score2;
		//if ( titleRecVersion >= 36 ) {
		//score2 = score >> 1;
		//if ( score2 <= 0 ) score2 = 1;
		//}
		//else
		//	score2 = score;

		QUICKPOLL(niceness);

		if ( ! pbuf ) {
			if ( ! table->addTerm ( h, score, maxScore, huo,
						titleRecVersion )) 
				return false;
			// hash the two-word phrase if h is not two words
			if ( h2 && h2 != h &&
			     ! table->addTerm ( h2, score, maxScore, 
						huo, titleRecVersion )) 
				return false;
			continue;
		}
		// add phrase as string to hash table if we need to as well
		int32_t  plen;
		char *p = getPhrase ( i , &plen );
		int32_t slen;
		//#if 1
		char *s = table->storeTerm ( p , plen ,
					     prefix1 , prefixLen1 ,
					     prefix2 , prefixLen2 ,true,&slen);
		//#else
		//char *s = table->storeTerm ( p , plen ,
		//			     "phrase" , 6 ,
		//		     prefix2 , prefixLen2 , true, &slen );
		//#endif
		if ( ! table->addTerm( h, score, maxScore, huo , 
				       titleRecVersion, s, slen ) )
			return false;	

		// if no strippable chars in phrase, we're done
		if ( ! h2 || h2 == h ) continue; 

		p = getTwoWordPhrase(i, &plen);

		s = table->storeTerm ( p , plen ,
				       prefix1 , prefixLen1 ,
				       prefix2 , prefixLen2 ,true,&slen);
		if ( ! table->addTerm( h2, score , maxScore, huo , 
				       titleRecVersion , s, slen ) )
			return false;	
	}
	// . TODO: print spam %'s for phrases!!!
	// . see Words.cpp for template code to do this
	return true;
}
*/

// . word #n is in a phrase if he has [word][punct] or [punct][word]
//   before/after him and you can pair across the punct and include both
//   in a phrase
// . used by SimpleQuery class to see if a word is in a phrase or not
// . if it is then the query may choose not to represent the word by itself
bool Phrases::isInPhrase ( int32_t n ) {
	// returns true if we started a phrase (our phraseSpam is not PSKIP)
	if ( m_phraseSpam[n] != PSKIP ) return true;
	// . see if we were in a phrase started by a word before us
	// . this only words since stop words - whose previous word cannot be
	//   paired across - are able to start phrases
	if ( n < 2                        ) return false;
	if ( ! m_bits->canPairAcross(n-1) ) return false;
	if ( ! m_bits->canBeInPhrase(n-2) ) return false;
	return true;
}

/*
// . get the index of the word that starts this phrase
// . returns -1 if none...factored out for 
// . getLeftPhraseId and getLeftStripPhraseId
int32_t Phrases::getLeftPhraseIndex( int32_t i ) {
	// return 0 if we no words before us
	while ( i  > 0 ) {
		// check punct before
		i--;
		// can he be paired across
		if ( m_words->isPunct(i)){
			if ( ! m_bits->canPairAcross(i) ) return -1;
		}
		else{
			// if word before him not in a phrase, bail
			if ( ! isInPhrase ( i ) ) return -1;
			// can he start ?
			if ( ! m_bits->canStartPhrase ( i  ) ) continue;
			// yes he can
			return i;
		}
	}
	// none
	return -1;	
}
// . get the id of the phrase we are in that we do not start
// . returns 0 if none, even though 0 may be a valid phraseId!! TODO: fix

int64_t Phrases::getLeftPhraseId ( int32_t i ) {
	int32_t index = getLeftPhraseIndex(i);
	if ( index < 0 ) return 0LL;
	return getPhraseId(index);
}

int64_t Phrases::getLeftStripPhraseId ( int32_t i ) {
	int32_t index = getLeftPhraseIndex(i);
	if ( index < 0 ) return 0LL;
	return getStripPhraseId(index);
}
*/
int32_t Phrases::getMaxWordsInPhrase ( int32_t i , int64_t *pid ) { 

	*pid = 0LL;

	/*
	if ( m_numWordsTotal5[i] ) {
		*pid = m_phraseIds5[i];
		return m_numWordsTotal5[i];
	}

	if ( m_numWordsTotal4[i] ) {
		*pid = m_phraseIds4[i];
		return m_numWordsTotal4[i];
	}
	*/
	if ( m_numWordsTotal3[i] ) {
		*pid = m_phraseIds3[i];
		return m_numWordsTotal3[i];
	}

	if ( m_numWordsTotal2[i] ) {
		*pid = m_phraseIds2[i];
		return m_numWordsTotal2[i];
	}

	return 0;
}


int32_t Phrases::getMinWordsInPhrase ( int32_t i , int64_t *pid ) { 

	*pid = 0LL;

	if ( m_numWordsTotal2[i] ) {
		*pid = m_phraseIds2[i];
		return m_numWordsTotal2[i];
	}

	return 0;
}
