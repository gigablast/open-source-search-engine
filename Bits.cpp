#include "gb-include.h"

#include "Bits.h"
#include "StopWords.h"
#include "fctypes.h"
#include "Abbreviations.h"
#include "Mem.h"

Bits::Bits() {
	m_bits = NULL;
	m_swbits = NULL;
}

Bits::~Bits() {
	reset();
}

void Bits::reset() {
	if ( m_bits && m_needsFree ) // (char *)m_bits != m_localBuf )
		mfree ( m_bits , m_bitsSize , "Bits" );
	if ( m_swbits && m_needsFree )
		mfree ( m_swbits , m_swbitsSize , "Bits" );
	m_bits = NULL;
	m_swbits = NULL;
	m_inLinkBitsSet = false;
	m_inUrlBitsSet = false;
}

// . set bits for each word
// . these bits are used for phrasing and by spam detector
// . returns false and sets errno on error
bool Bits::set ( Words *words , char titleRecVersion , int32_t niceness ,
		 char *buf , int32_t bufSize ) {
	reset();
	// save words so printBits works
	m_words = words;
	// save for convenience/speed
	m_titleRecVersion = titleRecVersion;
	m_niceness        = niceness;
	// how many words?
	int32_t numBits = words->getNumWords();
	// how much space do we need?
	int32_t need = numBits * sizeof(wbit_t);
	// assume no malloc
	m_needsFree = false;

	// use local buf?
	if ( need < BITS_LOCALBUFSIZE ) m_bits = (wbit_t *)m_localBuf;
	// use provided buf?
	else if ( need < bufSize ) m_bits = (wbit_t *)buf;
	// i guess need to malloc
	else {
		m_bitsSize = need;
		m_bits = (wbit_t *)mmalloc ( need , "Bits1" );
		m_needsFree = true;
	}
	if ( ! m_bits ) return log("build: Could not allocate "
				   "Bits table used to parse words: "
				   "%s",
				   mstrerror(g_errno));

	// breathe
	QUICKPOLL ( m_niceness );

	// sometimes the next bits are dependent on the previous bits.
	wbit_t prevBits  = 0;

	nodeid_t *tagIds = words->getTagIds();
	char **w         = words->getWords();
	int64_t *wids  = words->getWordIds();
	char **wptrs     = words->getWords();

	int64_t prevWid = 0LL;

	//int32_t  *wlens     = words->getWordLens();
	int32_t   brcount   = 0;

	wbit_t bits;
	bool isInSentence = false;

	for ( int32_t i = 0 ; i < numBits ; i++ ) {
		// get the word text and it's length
		//char         *s    = words->getWord    ( i );
		//int32_t          slen = words->getWordLen ( i );
		//wbit_t bits;

		// breathe
		QUICKPOLL ( m_niceness );

		if ( tagIds && tagIds[i] ) {
			// int16_tcut
			nodeid_t tid = tagIds[i] & BACKBITCOMP;
			// count the <br>s, we can't pair across more than 1
			if ( g_nodes[tid].m_isBreaking ) 
				bits = 0;
			// can only pair across one <br> tag, not two
			else if ( tid == TAG_BR ) { //tagIds[i] == 20 ){// <br>
				if ( brcount > 0 ) bits = 0;
				else { brcount++; bits = D_CAN_PAIR_ACROSS; }
			}
			else bits = D_CAN_PAIR_ACROSS;
		}

		// just skip if ignored from a 0 score
		//else if ( scores && scores[i] <= 0 ) {
		//	bits = 0;
		//}
		else if ( is_alnum_utf8 ( w[i]+0 )) {
			bits=getAlnumBits(i,prevBits);
			brcount = 0;
		}
		else {
			// . just allow anything now!
			// . the curved quote in utf8 is 3 bytes long and with
			//   a space before it, was causing issues here!
			bits= D_CAN_PAIR_ACROSS;
			//bits = getPunctuationBits(w[i],wlens[i]);
		}
		// now everybody has a period before them since i don't
		// want "project S" to phrase to "projects" or
		// "the rapist" to phrase to "therapist"
		bits |= D_CAN_PERIOD_PRECEED;
		// i commented this out cuz we ALWAYS put a period between now
		// if this word is following a "/", "." or "/~" then it can
		// be period preceeded in a phrase
		//if ( i > 1 && (s[-1]=='/' || s[-1]=='.') && is_alnum(s[-2])) 
		//	bits |= D_CAN_PERIOD_PRECEED;
		//if ( i > 2 &&  s[-1]=='~' && s[-2]=='/'  && is_alnum(s[-3])) 
		//	bits |= D_CAN_PERIOD_PRECEED;
		// remember our bits.
		m_bits [ i ] = bits;
		// these bits will be the previous bits the next time around.
		prevBits = bits; //m_bits [ i - 1 ];

		/////////////////////////
		//
		// . identify which tags and punct words break a sentence
		// . Sections.cpp uses this to carve out sentence sections
		//
		/////////////////////////

		// a word never breaks a sentence
		if ( wids[i] ) {
			isInSentence = true;
			prevWid = wids[i];
			continue;
		}

		// if not in a sentence, just keep going
		if ( ! isInSentence ) continue;

		// if punct it breaks unless it is a comma, semicolon,
		// colon, space, etc.
		if ( ! tagIds || ! tagIds[i] ) {
			// not a break if no period right there
			if ( wptrs[i][0] != '.' &&
			     wptrs[i][0] != '!' &&
			     wptrs[i][0] != '?' )
				continue;
			// if an alnum char follows the ., it is ok
			// probably a hostname or ip or phone #
			if ( is_alnum_utf8(wptrs[i]+1) ) continue;
			// if abbreviation before we are ok too
			if ( wptrs[i][0]=='.' && isAbbr(prevWid) ) continue;
			// otherwise, break that sentence
			m_bits[i] |= D_BREAKS_SENTENCE;
			// stop it
			isInSentence = false;
			// keep going
			continue;
		}

		// skip non breaking tags like font
		if ( ! isBreakingTagId(tagIds[i]) ) continue;

		// now we assume br tags break sentences until we can figure 
		// out if the page is microsoft front page or not.
		m_bits[i] |= D_BREAKS_SENTENCE;
		// stop it
		isInSentence = false;


		//
		// pick the longest line in a hard section which ends in
		// a period and contains a br tag.  then any line that
		// is 80%+ of that line's number of chars is also a line
		// where the br should not terminate it as a sentence.
		// ?????
	}

	return true;
}

#include "Sections.h"

void Bits::setInLinkBits ( Sections *ss ) {

	if ( m_inLinkBitsSet ) return;
	m_inLinkBitsSet = true;
	if ( ss->m_numSections == 0 ) return;
	// sets bits for Bits.cpp for D_IN_LINK for each ALNUM word
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// set boundaries
		int32_t a = si->m_a;
		int32_t b = si->m_b;
		for ( int32_t i = a ; i < b ; i++ )
			m_bits[i] |= D_IN_LINK;
	}
}	

void Bits::setInUrlBits ( int32_t niceness ) {
	if ( m_inUrlBitsSet ) return;
	m_inUrlBitsSet = true;
	nodeid_t *tids  = m_words->getTagIds();
	int64_t *wids = m_words->getWordIds();
	char **wptrs    = m_words->getWords();
	int32_t nw = m_words->getNumWords();
	for ( int32_t i = 0 ; i < nw; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// look for protocol
		if ( wids[i] ) continue;
		if ( tids[i] ) continue;
		if ( wptrs[i][0] != ':' ) continue;
		if ( wptrs[i][1] != '/' ) continue;
		if ( wptrs[i][2] != '/' ) continue;
		// set them up
		if ( i<= 0 ) continue;
		// scan for end of it. stop at tag or space
		int32_t j = i - 1;
		for ( ; j < nw ; j++ ) {
			// breathe
			QUICKPOLL(niceness);
			// check if end
			if ( m_words->hasSpace(j) ) break;
			// or tag
			if ( tids[j] )
			     //tids[j] != TAG_B && 
			     //tids[j] != (TAG_B|BACKBIT) ) 
				break;
			// include it
		        m_bits[j] |= D_IS_IN_URL;
		}
		// avoid inifinite loop with this if conditional statement
		if ( j > i ) i = j;
	}
}

void Bits::printBits ( ) {
	for ( int32_t i = 0 ; i < m_words->getNumWords(); i++ ) {
		m_words->printWord(i);
		fprintf(stderr," ");
		printBit(i);
		fprintf(stderr,"\n");
	}
}

void Bits::printBit ( int32_t i ) {
	if (m_bits[i]&D_CAN_BE_IN_PHRASE ) fprintf(stderr," canBeInPhrse");
	else                               fprintf(stderr,"             ");
	if (m_bits[i]&D_IS_STOPWORD ) fprintf(stderr," stopword");
	else                          fprintf(stderr,"         ");
	if (m_bits[i]&D_CAN_PERIOD_PRECEED)fprintf(stderr," periodCanPreceed");
	else                               fprintf(stderr,"                 ");
	//if (m_bits[i]&D_IS_INDEXABLE) fprintf(stderr," indexable");
	//else                          fprintf(stderr,"          ");
	if (m_bits[i]&D_CAN_START_PHRASE) fprintf(stderr," canStartPhrase");
	else                              fprintf(stderr,"               ");
	if (m_bits[i]&D_CAN_PAIR_ACROSS ) fprintf(stderr," canPairAcross");
	else                              fprintf(stderr,"              ");
}

// . if we're a stop word and previous word was an apostrophe
//   then set D_CAN_APOSTROPHE_PRECEED to true and PERIOD_PRECEED to false
wbit_t Bits::getAlnumBits ( int32_t i , wbit_t prevBits ) {

	char      *s   = m_words->getWord    ( i );
	int32_t       len = m_words->getWordLen ( i );
	int64_t  wid = m_words->getWordId  ( i );

	//if ( m_titleRecVersion < 36 && m_words->getStripWordId(i) )
	//	wid = m_words->getStripWordId(i);

	wbit_t bits = 0;

	// this is used by Weights.cpp
	if ( is_cap_utf8 ( s , len ) ) bits |= D_IS_CAP;

	// this is not case sensitive -- all non-stop words can start phrases
	if ( ! ::isStopWord ( s , len , wid ) )
	       return bits | D_CAN_BE_IN_PHRASE | D_CAN_START_PHRASE;

	bits |= 
		D_CAN_BE_IN_PHRASE   | 
		D_CAN_PAIR_ACROSS    | 
		D_IS_STOPWORD        | 
		D_CAN_PERIOD_PRECEED ;

	// stopwords preceeding an immediate hyphen (i-phone) can start phrases
	if ( s[len]=='-' && is_alnum_utf8(s+len+1) )
		return bits | D_CAN_START_PHRASE;

	// capitalized stop words can start phrases. ( kick Him in the *** )
	if ( is_upper_utf8(s) ) return bits | D_CAN_START_PHRASE;

	// if the previous word could not be paired across then
	// this stop word can start a phrase.  ( int16_t end.  it happened 
	// yesterday. )
	if ((prevBits & D_CAN_PAIR_ACROSS) == 0)
		return bits | D_CAN_START_PHRASE;

	// . the first alnum word can start a phrase as well
	// . prevBits may nto be zero if first word was punctuation
	if ( i <= 1 ) return bits | D_CAN_START_PHRASE;

	return bits;
}

// TODO: fuckin' ms frontpage puts int32_t sequences of spaces
//       between words that are next to each other
wbit_t Bits::getPunctuationBits ( char *s , int32_t len ) {

	uint8_t cs;
	if ( len != 2 ) goto tryLen1;

	if (s[0]==',' && (s[1]=='\n' || s[1]==' ')) return D_CAN_PAIR_ACROSS;
	if (s[0]=='/' && s[1]=='~')                 return D_CAN_PAIR_ACROSS ;
	cs = getUtf8CharSize ( s );
	// allow double spaces for version 6 or more
	if ( is_wspace_utf8(s) && is_wspace_utf8(s+cs) ) 
		return D_CAN_PAIR_ACROSS;
	if (is_wspace_utf8(s+cs) && is_punct_utf8(s)) {
		// switch/case is slow b-tree thing! stop it!
		if ( s[0] == '?' ) return 0;
		if ( s[0] == ';' ) return 0;
		if ( s[0] == '{' ) return 0;
		if ( s[0] == '}' ) return 0;
		if ( s[0] == '<' ) return 0;
		if ( s[0] == '>' ) return 0;
		//switch ((wbit_t)s[0]) {
		//case '!': return D_CAN_PAIR_ACROSS; // "Yahoo! games"
		//case '.': return 0;  // initials!  "I. B. M."
		//UTF8?case 171: return 0;  // <<  left shift operator
		//UTF8?case 187: return 0;  // >>  right shift operator
		//UTF8?case 191: return 0;  // upsidedown question mark
		//UTF8?case 161: return 0;  // upsidedown exclamation point
		return D_CAN_PAIR_ACROSS;
	}
	if (is_wspace_utf8(s) && is_punct_utf8(s+cs)) {
		// switch/case is slow b-tree thing! stop it!
		if ( s[cs] == '?' ) return 0;
		if ( s[cs] == ';' ) return 0;
		if ( s[cs] == '{' ) return 0;
		if ( s[cs] == '}' ) return 0;
		if ( s[cs] == '<' ) return 0;
		if ( s[cs] == '>' ) return 0;
		if ( s[cs] == '!' ) return 0;
		//UTF8?case 171: return 0;  // <<  left shift operator
		//UTF8?case 187: return 0;  // >>  right shift operator
		//UTF8?case 191: return 0;  // upsidedown question mark
		//UTF8?case 161: return 0;  // upsidedown exclamation point
		return D_CAN_PAIR_ACROSS;
	}
	return 0;

 tryLen1:

	if (len != 1) goto tryLen3;

	// switch/case is slow b-tree thing! stop it!
	if ( s[0] == '?' ) return 0;
	if ( s[0] == ';' ) return 0;
	if ( s[0] == '{' ) return 0;
	if ( s[0] == '}' ) return 0;
	if ( s[0] == '<' ) return 0;
	if ( s[0] == '>' ) return 0;
	if ( s[0] == '!' ) return 0;
	//UTF8?case 171: return 0;  // <<  left shift operator
	//UTF8?case 187: return 0;  // >>  right shift operator
	//UTF8?case 191: return 0;  // upsidedown question mark
	//UTF8?case 161: return 0;  // upsidedown exclamation point
	return D_CAN_PAIR_ACROSS;

	// we can pair across:
	// "://"
	// " , "
	// " - "
	// " & "
	// " + "
 tryLen3:

	//
	// good place to check for ascii spaces...
	//

	// pair across any number of spaces, it will only show up as one
	// space in html and Microsoft Front Page separates lines by a 
	// bunch of spaces
	if ( is_wspace_a(s[0]) && is_wspace_a(s[1]) && is_wspace_a(s[2]) ) {
		int32_t k = 3;
		while ( k < len ) if ( ! is_wspace_a(s[k++] ) ) return 0;
		return D_CAN_PAIR_ACROSS;
	}
	if (len != 3) return 0;
	if (s[0]==':' && s[1]=='/'&&s[2]=='/')return D_CAN_PAIR_ACROSS;
	if ( is_wspace_a(s[0]) && is_wspace_a(s[2]) ) 
		switch (s[1]) {
		case ',': return D_CAN_PAIR_ACROSS;
		case '-': return D_CAN_PAIR_ACROSS;
		case '+': return D_CAN_PAIR_ACROSS;
		case '&': return D_CAN_PAIR_ACROSS;
		}
	return 0;
}

//
// Summary.cpp sets its own bits.
//

// this table maps a tagId to a #define'd bit from Bits.h which describes
// the format of the following text in the page. like bold or italics, etc.
nodeid_t s_bt [ 1000 ];

// . set bits for each word
// . these bits are used for phrasing and by spam detector
// . returns false and sets errno on error
bool Bits::setForSummary ( Words *words , char *buf , int32_t bufSize ) {
	// clear the mem
	reset();

	// set our s_bt[] table
	bool s_init = false;
	if ( ! s_init ) {
		// only do this once
		s_init = true;
		// clear table
		if ( 1000 < getNumXmlNodes() ) { char *xx=NULL;*xx=0; }
		memset ( s_bt , 0 , 1000 * sizeof(nodeid_t) );
		// set just those that have bits #defined in Bits.h
		s_bt [ TAG_TITLE      ] = D_IN_TITLE;
		s_bt [ TAG_A          ] = D_IN_HYPERLINK;
		s_bt [ TAG_B          ] = D_IN_BOLDORITALICS;
		s_bt [ TAG_I          ] = D_IN_BOLDORITALICS;
		s_bt [ TAG_LI         ] = D_IN_LIST;
		s_bt [ TAG_SUP        ] = D_IN_SUP;
		s_bt [ TAG_P          ] = D_IN_PARAGRAPH;
		s_bt [ TAG_BLOCKQUOTE ] = D_IN_BLOCKQUOTE;
	}

	// save words so printBits works
	m_words = words;
	// save for convenience/speed
	//m_titleRecVersion = 0;
	// how many words?
	int32_t numBits = words->getNumWords();
	// how much space do we need?
	int32_t need = sizeof(swbit_t) * numBits;
	// assume no malloc
	m_needsFree = false;

	// use local buf?
	if ( need < BITS_LOCALBUFSIZE ) m_swbits = (swbit_t *)m_localBuf;
	// use provided buf?
	else if ( need < bufSize ) m_swbits = (swbit_t *)buf;
	// i guess need to malloc
	else {
		m_swbitsSize = need;
		m_swbits = (swbit_t *)mmalloc ( need , "BitsW" );
		m_needsFree = true;
	}
	if ( ! m_swbits ) return log("build: Could not allocate "
				     "Bits table used to parse words: "
				     "%s",
				     mstrerror(g_errno));

	// set 
	// D_STRONG_CONNECTOR
	// D_STARTS_SENTENCE
	// D_STARTS_FRAGMENT

	nodeid_t   *tagIds = words->getTagIds();
	char      **w      = words->getWords();
	int32_t       *wlens  = words->getWordLens();
	int64_t  *wids   = words->getWordIds();

	char          startSent = 1;
	char          startFrag = 1;
	char          inQuote   = 0;
	char          inParens  = 0;

	int32_t          wlen;
	char         *wp;

	// the ongoing accumulation flag we apply to each word
	swbit_t flags = 0;

	for ( int32_t i = 0 ; i < numBits ; i++ ) {
		// assume none are set
		m_swbits[i] = 0;
		// if a breaking tag, next guy can "start a sentence"
		if ( tagIds && tagIds[i] ) {
			// get the tag id minus the high "back bit"
			int32_t tid = tagIds[i] & BACKBITCOMP;
			// is it a "breaking tag"?
			if ( g_nodes[tid].m_isBreaking ) {
				startSent = 1;
				inQuote   = 0;
			}
			// adjust flags if we should
			if ( s_bt[tid] ) {
				if   ( tid != tagIds[i] ) flags &= ~s_bt[tid];
				else                      flags |=  s_bt[tid];
			}
			// apply flag
			m_swbits[i] |= flags;
			continue;
		}
		// if alnum, might start sentence or fragment
		if ( wids[i] ) {
			if ( startFrag ) {
				m_swbits[i] |= D_STARTS_FRAG   ; startFrag =0;}
			if ( startSent ) {
				m_swbits[i] |= D_STARTS_SENTENCE;startSent =0;}
			if ( inQuote ) {
				m_swbits[i] |= D_IN_QUOTES      ;inQuote = 0;}
			if ( inParens )
				m_swbits[i] |= D_IN_PARENS;
			// apply any other flags we got
			m_swbits[i] |= flags;
			continue;
		}
		// fast ptrs
		wlen = wlens[i];
		wp   = w    [i];
		
		// this is not 100%
		if      ( words->hasChar (i, '(' ) ) flags |=  D_IN_PARENS;
		else if ( words->hasChar (i, ')' ) ) flags &= ~D_IN_PARENS;

		// apply curent flags
		m_swbits[i] |= flags;


		// does it END in a quote?
		if      ( wp[wlen-1]=='\"' )
			inQuote = 1;
		else if ( wlen >= 6 &&
			  strncmp(wp,"&quot;",6)== 0 ) 
			inQuote = 1;
		
		// . but double spaces are not starters
		// . MDW: we kinda force ourselves to only use ascii spaceshere
		if ( wlen==2 && is_wspace_a(*wp)&&is_wspace_a(wp[1])) continue;
		// it can start a fragment if not a single space char
		if ( wlen!=1 || ! is_wspace_utf8(wp) )
			startFrag = 1;
		// ". " denotes end of sentence
		if ( wlen>=2 && wp[0]=='.' && is_wspace_utf8(wp+1)){
			// but not if preceeded by an initial
			if ( i>0 && wlens[i-1]==1 && wids[i-1] )
				continue;
			// ok, really the end of a sentence
			startSent = 1;
		}
		// are we a "strong connector", meaning that 
		// Summary.cpp should not split on us if possible
		
		// apostrophe html encoded?
		if ( wlen == 6 && strncmp(wp,"&#146;",6) == 0 ) {
			m_swbits[i] |= D_IS_STRONG_CONNECTOR;
			continue;
		}
		if ( wlen == 7 && strncmp(wp,"&#8217;",7) == 0 ) {
			m_swbits[i] |= D_IS_STRONG_CONNECTOR;
			continue;
		}
		
		// otherwise, strong connectors must be single char
		if ( wlen != 1 ) continue;
		// is it apostrophe? - & . * (M*A*S*H)
		char c = wp[0];
		if      ( c == '\'')m_swbits[i]|=D_IS_STRONG_CONNECTOR;
		else if ( c == '-' )m_swbits[i]|=D_IS_STRONG_CONNECTOR;
		else if ( c == '&' )m_swbits[i]|=D_IS_STRONG_CONNECTOR;
		else if ( c == '.' )m_swbits[i]|=D_IS_STRONG_CONNECTOR;
		else if ( c == '*' )m_swbits[i]|=D_IS_STRONG_CONNECTOR;
		else if ( c == '/' )m_swbits[i]|=D_IS_STRONG_CONNECTOR;
	}

	return true;
}
