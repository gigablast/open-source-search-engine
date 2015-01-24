// Matt Wells, copyright Jun 2001

// . each word has several bits of information we like to keep track of
// . these bits are used for making phrases in Phrases.h
// . also used by spam detector in Spam.h
// . TODO: rename this class to PhraseBits
// . TODO: separate words in phrases w/ period OR space so a search for
//   "chicken.rib" gives you the renderman file, not a recipe or something

#ifndef _BITS_H_
#define _BITS_H_

#include "Words.h"

// . here's the bit define's:
// . used for phrasing 
// . no punctuation or "big" numbers can be in a phrase
#define D_CAN_BE_IN_PHRASE      0x0001 
// is this word a stop word?
#define D_IS_STOPWORD           0x0002 
// . used for phrasing 
// . stop words can have a period preceeding them in the phrase
// . words preceeded by "/" , "." or "/~" can have a period preceed them
#define D_CAN_PERIOD_PRECEED    0x0004 
// same as above (can we hash this word???)
//#define D_IS_INDEXABLE        0x08 
// this means the word is in a verified address (bit set in Address.cpp)
#define D_IS_IN_ADDRESS         0x0008
// . used for phrasing
// . stop words can only start a phrase if prev word could not "pair across"
#define D_CAN_START_PHRASE      0x0010 
// . used for phrasing 
// . can we continue forming our phrase after this word?
// . some puntuation words and all stop words can be paired across
#define D_CAN_PAIR_ACROSS       0x0020 
// it it capitalized?
#define D_IS_CAP                0x0040
// is it in a date?
#define D_IS_IN_DATE            0x0080
// is it in a street name. set by Address.cpp code.
#define D_IS_IN_STREET          0x0100
#define D_BREAKS_SENTENCE       0x0200
// set by Sections.cpp::setMenu() function
#define D_IN_LINK               0x0400
// in the place name part of an address?
#define D_IS_IN_VERIFIED_ADDRESS_NAME    0x0800
// allow for dows for texasdrums.org, so TUESDAYS is set with this and
// we can keep it as part of the sentence and not split on the colon
//#define D_IS_IN_DATE_2        0x1000
// this is so we can still set EV_HASTITLEBYVOTES if a tod date is in the
// title, all other dates are no-no!
#define D_IS_DAYNUM             0x1000
// for setting event titles in Events.cpp
#define D_GENERIC_WORD          0x2000
#define D_CRUFTY                0x4000
#define D_IS_NUM                        0x00008000
#define D_IS_IN_UNVERIFIED_ADDRESS_NAME 0x00010000
#define D_IS_IN_URL                     0x00020000
// like D_IS_TOD above
#define D_IS_MONTH                      0x00040000
#define D_IS_HEX_NUM                    0x00080000
//
// the bits below here are used for Summary.cpp when calling 
// Bits::setForSummary()
//

// . is this word a strong connector?
// . used by Summary.cpp so we don't split strongly connected things
// . right now, just single character punctuation that is not a space
// . i don't want to split possessive words at the apostrophe, or split
//   ip addresses at the period, etc. applies to unicode as well.
#define D_IS_STRONG_CONNECTOR   0x0001
// . does it start a sentence? 
// . if our summary excerpt starts with this then it will get bonus points
#define D_STARTS_SENTENCE       0x0002
// . or does it start a sentence fragment, like after a comma or something
// . the summary excerpt will get *some* bonus points for this
#define D_STARTS_FRAG           0x0004
// . does this word have a quote right before it?
#define D_IN_QUOTES             0x0008
// more bits so we can get rid of Summary::setSummaryScores() so that
// Summary::getBestWindow() just uses these bits to score the window now
#define D_IN_TITLE              0x0010
#define D_IN_PARENS             0x0020
#define D_IN_HYPERLINK          0x0040
#define D_IN_BOLDORITALICS      0x0080
#define D_IN_LIST               0x0100
#define D_IN_SUP                0x0200
#define D_IN_PARAGRAPH          0x0400
#define D_IN_BLOCKQUOTE         0x0800
// for Summary.cpp
#define D_USED                  0x1000

//
// end summary bits
//

#define BITS_LOCALBUFSIZE 20

// Words class bits. the most common case
typedef uint32_t wbit_t;

// summary bits used for doing summaries at query time
typedef uint16_t swbit_t;

// . used by SimpleQuery.cpp
// . this isn't used for phrasing, it's just so a doc that has the same
//   # of query terms as another, but also one query stop word, won't be
//   ranked above the other doc just because of that
//#define D_IS_QUERY_STOPWORD     0x40

class Bits {

 public:

	Bits();
	~Bits();

	bool set2 ( Words *words, int32_t niceness ) {
		return set ( words,TITLEREC_CURRENT_VERSION,niceness); };

	// . returns false and sets errno on error
	bool set ( Words *words , 
		   char titleRecVersion ,
		   int32_t niceness ,
		   // provide it with a buffer to prevent a malloc
		   char         *buf    = NULL ,
		   int32_t          bufSize= 0    );

	bool setForSummary ( Words *words ,
			     // provide it with a buffer to prevent a malloc
			     char         *buf    = NULL ,
			     int32_t          bufSize= 0    );

	void reset();

	bool isStopWord      (int32_t i) {return m_bits[i]&D_IS_STOPWORD;};
	bool canBeInPhrase   (int32_t i) {return m_bits[i]&D_CAN_BE_IN_PHRASE;};
	bool canStartPhrase  (int32_t i) {return m_bits[i]&D_CAN_START_PHRASE;};
	bool canPeriodPreceed(int32_t i) {return m_bits[i]&D_CAN_PERIOD_PRECEED;};
	bool canPairAcross   (int32_t i) {return m_bits[i]&D_CAN_PAIR_ACROSS;};
	//bool isIndexable     (int32_t i) {return m_bits[i]&D_IS_INDEXABLE;};
	bool isCap           (int32_t i) {return m_bits[i]&D_IS_CAP;};
	void printBits ( );
	void printBit  ( int32_t i );

	void setInLinkBits ( class Sections *ss ) ;
	void setInUrlBits  ( int32_t niceness );

	bool m_inLinkBitsSet;
	bool m_inUrlBitsSet;

	//char m_localBuf [MAX_WORDS*10];
	char m_localBuf [ BITS_LOCALBUFSIZE ];

	// leave public so Query.cpp can tweak this
	wbit_t *m_bits ;
	int32_t    m_bitsSize;

	int32_t m_niceness;

	// . wordbits
	// . used only by setForSummary() now to avoid having to update a
	//   lot of code
	swbit_t *m_swbits;
	int32_t     m_swbitsSize;

 private:

	Words        *m_words;

	char m_titleRecVersion;

	bool m_needsFree;

	// get bits for the ith word
	wbit_t getAlnumBits ( int32_t i , wbit_t prevBits );

	// get bits for the ith word
	wbit_t getPunctuationBits  ( char *s , int32_t slen ) ;
};

#endif
