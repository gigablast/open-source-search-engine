// Matt Wells, copyright Jul 2001

// . used to parse XML/HTML (romantic char set) into words
// . TODO: ensure WordType m_types[] array is only 1 byte per entry
// . ??? a word should end at any non-alnum ??? then using phrasing for "tim's"

#ifndef _WORDS_H_
#define _WORDS_H_

// . we can have up to about 16,000 words per doc
// . can be big since we never use threads
// . this was 1024*32 but using librt's aio_read somehow enforces a 500k
//   stack onto us, even when we're not in a thread per se!!
//#define MAX_WORDS (1024*16)
// now keep this small and malloc if we need more... save some stack
#define MAX_WORDS (1024)

// Leaving MAX_WORDS alone because other classes use it...
// We use LOCALBUFSIZE now for allocation here now
//#define LOCALBUFSIZE (MAX_WORDS*16)

// now Matches.h has 300 Words classes handy... try to do away with this
// make sure it does not slow us down!!
#define WORDS_LOCALBUFSIZE 80

// an upper bound really
long countWords ( char *p , long niceness ) ;
long countWords ( char *p , long plen , long niceness );

long printstring ( char *s , long len ) ;

char *getFieldValue ( char *s ,long  slen, char *field , long *valueLen ) ;

unsigned char getCharacterLanguage ( char *utf8Char ) ;


//#include "TermTable.h"  // used in hash()
#include "Xml.h"
#include "SafeBuf.h"
#include "StopWords.h"
#include "fctypes.h"
#include "Titledb.h"

#define NUM_LANGUAGE_SAMPLES 1000

//#define TITLEREC_CURRENT_VERSION 114

// this bit is set in the tag id to indicate a back tag
#define BACKBIT     ((nodeid_t)0x8000)
#define BACKBITCOMP ((nodeid_t)0x7fff)

class Words {

 public:

	// . set words from a string
	// . s must be NULL terminated
	// . NOTE: we never own the data
	// . there is typically no html in "s"
	// . html tags are NOT parsed out
	bool set ( char *s , 
		   long version , // = TITLEREC_CURRENT_VERSION , 
		   bool computeIds , // = true ,
		   long niceness ); // = 0);

	// assume TITLEREC_CURRENT_VERSION and computeIds is true
	bool set9 ( char *s , long niceness ) {
		return set ( s , TITLEREC_CURRENT_VERSION, true , niceness);};

	bool setxi ( char *s , char *buf, long bufSize, long niceness ) ;

	bool setx ( char *s , long slen , long niceness ) {
		return set ( s,slen,TITLEREC_CURRENT_VERSION,true,niceness);};

	bool set11 ( char *s , char *send , long niceness ) ;

	// . similar to above
	// . but we temporarily stick a \0 @ s[slen] for parsing purposes
	bool set ( char *s , long slen , long version, 
		   bool computeIds ,
		   long niceness = 0);

	bool set3 ( char *s ) {return set(s,TITLEREC_CURRENT_VERSION,true,0);};

	// . new function to set directly from an Xml, rather than extracting
	//   text first
	// . use range (node1,node2] and if node2 is -1 that means the last one
	bool      set ( Xml *xml, 
			bool computeIds , 
			long niceness = 0 , 
			long node1    = 0 ,
			long node2    = -1 );

	// trying to make it faster
	bool      set2 ( Xml *xml, bool computeIds , long niceness = 0);

	// . if score == 0  then use spam modified score
	// . each non-spammy occurence of a word adds "baseScore" to it's score
	// . keep baseScore pretty high in case reduced by spamming
	// . typically i use 100 as the baseScore to preserve fractions
	/*
	bool hash ( TermTable      *table       ,
		    class Spam     *spam        ,
		    class Weights  *weights     ,
		    unsigned long   baseScore   ,
		    unsigned long   maxScore    ,
		    long long       startHash   ,
		    char           *prefix1     ,
		    long            prefixLen1  ,
		    char           *prefix2     ,
		    long            prefixLen2  ,
		    bool            useStems    ,
		    bool            hashUniqueOnly ,
		    long            titleRecVersion ,
		    class Phrases  *phrases                ,//= NULL  ,
		    bool            hashWordIffNotInPhrase ,//= false,
		    long            niceness               );//= 0);
	*/

	inline bool addWords(char* s, long nodeLen,
			     bool computeIds, long niceness);

	// get the spam modified score of the ith word (baseScore is the 
	// score if the word is not spammed)
	long      getNumWords      (        ) const { return m_numWords;   };
	long      getNumAlnumWords (        ) const { return m_numAlnumWords;};
	char     *getWord          ( long n ) const { return m_words   [n];};
	long      getWordLen       ( long n ) const { return m_wordLens[n];};

	//long long getNextWid ( long i , long toscan , long niceness ) {
	//	long max = i + toscan;
	//	if ( max > m_numWords ) max = m_numWords;
	//	for ( ; i < max ; i++ ) {
	//		QUICKPOLL(niceness);
	//		if ( m_wids[i] ) return m_wids[i];
	//	}
	//	return 0LL;
	//};

	// . size of string from word #a up to and NOT including word #b
	// . "b" can be m_numWords to mean up to the end of the doc
	long      getStringSize    ( long a , long b ) {
		// do not let it exceed this
		if ( b >= m_numWords ) b = m_numWords;
		// pedal it back. we might equal a then. which is ok, that
		// means to just return the length of word #a then
		b--;
		if ( b <  a ) return 0;
		if ( a <  0 ) return 0;
		long size = m_words[b] - m_words[a];
		// add in size of word #b
		size += m_wordLens[b];
		return size;
	};

	long getWordAt ( char *charPos ); // long charPos );
	// . CAUTION: don't call this for punct "words"... it's bogus for them
	// . this is only for alnum "words"
	long long getWordId        ( long n ) const { return m_wordIds [n];};

	bool isStopWord ( long n ) {
		return ::isStopWord(m_words   [n],
				    m_wordLens[n],
				    m_wordIds [n]);
	}

	bool isQueryStopWord ( long n ) {
		return ::isQueryStopWord(m_words   [n],
					 m_wordLens[n],
					 m_wordIds [n]);
	}


	// . how many quotes in the nth word?
	// . how many plusses in the nth word?
	// . used exclusively by Query class for parsing query syntax
	long      getNumQuotes     ( long n ) {
		long count = 0;
		for ( long i = 0 ; i < m_wordLens[n] ; i++ )
			if ( m_words[n][i] == '\"' ) count++;
		return count; };

	long      getNumPlusses    ( long n );

	// . do we have a ' ' 't' '\n' or '\r' in this word?
	// . caller should not call this is isPunct(n) is false, pointless.
	
	bool      hasSpace         ( long n ) {
		for ( long i = 0 ; i < m_wordLens[n] ; i++ )
			if ( is_wspace_utf8(&m_words[n][i]) ) return true;
		return false; 
	};

	bool      hasChar         ( long n , char c ) const {
		for ( long i = 0 ; i < m_wordLens[n] ; i++ )
			if ( m_words[n][i] == c ) return true;
		return false; 
	};

	bool      hasDigit        ( long n ) const {
		for ( long i = 0 ; i < m_wordLens[n] ; i++ )
			if ( is_digit(m_words[n][i]) ) return true;
		return false; 
	};

	// this doesn't really work for utf8!!!
	bool      hasAlpha        ( long n ) const {
		for ( long i = 0 ; i < m_wordLens[n] ; i++ )
			if ( is_alpha_a(m_words[n][i]) ) return true;
		return false; 
	};

	bool      isSpaces        ( long n ) {
		for ( long i = 0 ; i < m_wordLens[n] ; i++ )
			if ( ! is_wspace_utf8(&m_words[n][i]) ) return false;
		return true;
	};

	bool      isSpaces2       ( long n , long starti ) {
		for ( long i = starti ; i < m_wordLens[n] ; i++ )
			if ( ! is_wspace_utf8(&m_words[n][i]) ) return false;
		return true;
	};

	//bool      isSpacesOrComma  ( long n ) {
	//	for ( long i = 0 ; i < m_wordLens[n] ; i++ )
	//		if ( ! is_wspace_utf8(&m_words[n][i]) &&
	//		     m_words[n][i]!=',' ) return false;
	//	return true;
	//};

	//if this is set from xml, every word is either a word or an xml node 
	nodeid_t getTagId(long n) {
		if ( ! m_tagIds ) return 0;
		return ( m_tagIds[n] & BACKBITCOMP );
	};
	bool    isBackTag(long n) {
		if ( ! m_tagIds ) return false;
		if ( m_tagIds[n] & BACKBIT ) return true;
		return false;
	};
	bool    isBackTagId ( nodeid_t tid ) {
		if ( tid & BACKBIT ) return true;
		return false;
	};

	// CAUTION!!!
	//
	// "BACKBIT" is set in the tagid  of m_tagIds[] to indicate the tag is
	// a "back tag" as opposed to a "front tag". i.e. </a> vs. <a>
	// respectively. so mask it out by doing "& BACKBITCOMP" if you just
	// want the pure tagid!!!!
	//
	// CAUTION!!!
	nodeid_t   *getTagIds  () { return m_tagIds; };
	char      **getWords   () { return m_words; };
	char      **getWordPtrs() { return m_words; };
	long       *getWordLens() { return m_wordLens; };
	long long  *getWordIds () { return m_wordIds; };
	// 2 types of "words": punctuation and alnum
	// isPunct() will return true on tags, too, so they are "punct"
	bool      isPunct  ( long n ) const { return m_wordIds[n] == 0;};
	bool      isAlnum  ( long n ) const { return m_wordIds[n] != 0;};
	bool      isAlpha  ( long n ) const { 
		if ( m_wordIds[n] == 0LL ) return false;
		if ( isNum ( n )         ) return false;
		return true;
	};

	long getAsLong ( long n ) {
		// skip if no digit
		if ( ! is_digit ( m_words[n][0] ) ) return -1;
		return atol2(m_words[n],m_wordLens[n]); 
	};


	bool      isNum    ( long n ) const { 
		if ( ! is_digit(m_words[n][0]) ) return false;
		char *p    = m_words[n];
		char *pend = p + m_wordLens[n];
		for (  ; p < pend ; p++ )
			if ( ! is_digit(*p) ) return false;
		return true;
	};

	bool      isHexNum    ( long n ) const { 
		if ( ! is_hex(m_words[n][0]) ) return false;
		char *p    = m_words[n];
		char *pend = p + m_wordLens[n];
		for (  ; p < pend ; p++ )
			if ( ! is_hex(*p) ) return false;
		return true;
	};

	// include &frac12's utf8 equivalent. used by Address.cpp
	bool      isNum2   ( long n ) const { 
		if ( ! is_digit(m_words[n][0]) ) return false;
		char *p    = m_words[n];
		char *pend = p + m_wordLens[n];
		for (  ; p < pend ; p++ ) {
			if ( is_digit(*p) ) continue;
			// this is frac14
			if ( p[0] == -62 && p[1] == -68 ) { p++; continue; }
			// might be that &frac12 char, 14, 34 utf8 chars
			if ( p[0] == -62 && p[1] == -67 ) { p++; continue; }
			// this is frac34
			if ( p[0] == -62 && p[1] == -66 ) { p++; continue; }
			return false;
		}
		return true;
	};

	// . used in SimpleQuery.cpp
	// . are all alpha char capitalized?
	bool      isUpper  ( long n ) {
		// skip if not alnum...
		if ( m_wordIds[n] == 0LL ) return false;
		char *p    = m_words[n];
		char *pend = p + m_wordLens[n];
		char  cs;
		for ( ; p < pend ; p += cs ) {
			cs = getUtf8CharSize ( p );
			if ( is_digit        ( *p ) ) continue;
			if ( is_lower_utf8   (  p ) ) return false;
		}
		return true;
	}

	bool isCapitalized ( long n ) {
		if ( ! is_alpha_utf8 ( m_words[n] ) ) return false;
		return is_upper_utf8 ( m_words[n] ) ;
	};

	//returns the number of words in the float.
	long      isFloat  ( long n, float& f);

	long      getTotalLen ( ) { return m_totalLen; };

	unsigned char isBounded(int wordi);
	 Words     ( );
	~Words     ( );
	void reset ( ); 

	void print ( );
	void printWord ( long i );

	//unsigned char getLanguage() { return langUnknown; }
	// returns -1 and sets g_errno on error
	long getLanguage ( class Sections *sections = NULL ,
			   long maxSamples = NUM_LANGUAGE_SAMPLES,
			   long niceness = 0,
			   long *langScore = NULL);

	long getMemUsed () { return m_bufSize; };

	char *getContent() { 
		if ( m_numWords == 0 ) return NULL;
		return m_words[0]; 
	};
	char *getContentEnd() { 
		if ( m_numWords == 0 ) return NULL;
		return m_words[m_numWords-1] + m_wordLens[m_numWords-1];
	};

	// private:

	bool allocateWordBuffers(long count, bool tagIds = false);
	
	char  m_localBuf [ WORDS_LOCALBUFSIZE ];

	char *m_localBuf2;
	long  m_localBufSize2;

	char *m_buf;
	long  m_bufSize;
        Xml  *m_xml ;  // if the class is set from xml, rather than a string

	long           m_preCount  ; // estimate of number of words in the doc
	char          **m_words    ;  // pointers to the word
	long           *m_wordLens ;  // length of each word
	long long      *m_wordIds  ;  // lower ascii hash of word
	long           *m_nodes    ;  // Xml.cpp node # (for tags only)
	nodeid_t       *m_tagIds   ;  // tag for xml "words"

 	long           m_numWords;      // # of words we have
	long           m_numAlnumWords;

	long           m_totalLen;  // of all words
	long           m_version; // titlerec version

	bool           m_hasTags;

	// sanity checkes for performance
	char *m_s;

	long m_numTags;
};

#endif
