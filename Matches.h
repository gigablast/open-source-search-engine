// Matt Wells, copyright Jul 2001

#ifndef _MATCHES_H_
#define _MATCHES_H_

#include "Query.h"
#include "Words.h"
#include "Xml.h"
#include "HashTableT.h"
#include "Pos.h"
#include "Bits.h"

// do not hash more than this many query words into the hash table
#define MAX_QUERY_WORDS_TO_MATCH 1000

// . i upped this from 500 to 3000 to better support the BIG HACK
//   getting 3000 matches slows down the summary generator a lot.
// . i raised MAX_MATCHES to 3000 for huge UOR queries made in SearchInput.cpp
//   from facebook interests
#define MAX_MATCHES              3000
#define MAX_MATCHES_FOR_BIG_HACK 3000

#define MAX_MATCHGROUPS 300

typedef int32_t mf_t;

// . values for Match::m_flags
// . dictates the "match group" that the match belongs to
#define MF_TITLEGEN                   0x0001 // in generated title?
#define MF_TITLETAG                   0x0002
#define MF_LINK                       0x0004 // in non-anomalous link text
//#define MF_ALINK                      0x0008 // in anomalous link text
#define MF_HOOD                       0x0010 // in non-anomalous neighborhood
//#define MF_AHOOD                      0x0020 // in anomalous neighborhood
#define MF_BODY                       0x0040 // in body
#define MF_METASUMM                   0x0080 // in meta summary
#define MF_METADESC                   0x0100 // in meta description
#define MF_METAKEYW                   0x0200 // in meta keywords
#define MF_DMOZTITLE                  0x0400 
#define MF_DMOZSUMM                   0x0800 
#define MF_RSSTITLE                   0x1000 
#define MF_RSSDESC                    0x2000 
#define MF_URL                        0x4000  // in url
#define MF_SYNONYM                    0x8000

class Match {
 public:
	// word # we match in the document using "m_words" below
	int32_t m_wordNum;
	// # of words in this match, like if we match a phrase
	// we have > 1 words in the match
	int32_t m_numWords;
	// word # we match in the query
	int32_t m_qwordNum;
	// # of query words we match if we are a phrase, otherwise
	// this is 1
	int32_t m_numQWords;
	// . used for highlighting under different colors (Highlight.cpp)
	// . words in the same quote should use the same highlight color
	int32_t m_colorNum;

	// . max score of all contained query terms in this match
	// . uses provided m_tscores[] array to get scores of the query terms
	//int32_t m_score;

	// "match group" or type of match. i.e. MF_TITLETAG, MF_METASUMM, ...
	mf_t m_flags;

	// improve summary generation parms
	int32_t m_dist;
	//bool m_crossedSection;

	// . for convenience, these four class ptrs are used by Summary.cpp
	// . m_wordNum is relative to this "words" class (and scores,bits,pos)
	class Words    *m_words;
	class Sections *m_sections;
	class Bits     *m_bits;
	class Pos      *m_pos;
};

class Matches {

 public:

	void setQuery ( Query *q );//, int32_t *tscores = NULL ) ;

	bool set ( class XmlDoc   *xd           ,
		   class Words    *bodyWords    ,
		   //class Synonyms *bodySynonyms ,
		   class Phrases  *bodyPhrases ,
		   class Sections *bodySections ,
		   class Bits     *bodyBits     ,
		   class Pos      *bodyPos      ,
		   class Xml      *xml          ,
		   class Title    *tt           ,
		   int32_t            niceness     ) ;

	bool addMatches ( char      *s         ,
			  int32_t       slen      ,
			  mf_t       flags     ,
			  int64_t  docId     ,
			  int32_t       niceness  ) ;

	// . this sets the m_matches[] array
	// . m_matches[i] is -1 if it matches no term in the query
	// . m_matches[i] is X if it matches term #X in the query
	// . returns false and sets errno on error
	bool addMatches ( Words    *words                      ,
			  //class Synonyms *syn           = NULL ,
			  class Phrases  *phrases       = NULL ,
			  Sections *sections            = NULL ,
			  Bits     *bits                = NULL ,
			  Pos      *pos                 = NULL ,
			  int32_t      fieldCode           = 0    , // wrds,0=none
			  bool      allowPunctInPhrase  = true ,
			  bool      exclQTOnlyInAnchTxt = false,
			  qvec_t    reqMask             = 0    ,
			  qvec_t    negMask             = 0    ,
			  int32_t      diversityWeight     = 1    ,
			  int64_t docId               = 0    ,
			  mf_t      flags               = 0    );

	// this is NULL terminated
	//char getTermNum  ( int32_t i ) { return m_termNums[i]; };
	//char getTermNum2 ( int32_t i ) { return m_termNums2[i]; };

	// get the whole array
	//char *getTermNums ( ) { return m_termNums; };
	//char *getPhraseNums ( ) { return m_phraseNums; };

	// how many words matched a rawTermId?
	int32_t getNumMatches ( ) { return m_numMatches; };

	//void clearBitsMatched( void       ) { m_explicitBitsMatched = 0x00; };
	//void addBitMatched   ( qvec_t bit ) { m_explicitBitsMatched |= bit; };
	//qvec_t getBitsMatched( void       ) { return m_explicitBitsMatched; };

	// set m_termNums from a to b to 0
	//void clearMatches ( int32_t a , int32_t b );

	// janitorial stuff
	Matches ( ) ;
	~Matches( ) ;
	void reset ( ) ;
	void reset2 ( ) ;

	// BIG HACK support
	//int32_t getTermsFound ( bool *hadPhrases , bool *hadWords );
	uint32_t getTermsFound2(bool *hadPhrases, bool *hadWords);
	//bool negTermsFound ( );
	bool docHasQueryTerms(int32_t totalInlinks);

	// used internally and by PageGet.cpp
	bool isMatchableTerm ( class QueryTerm *qt );//, int32_t i );

	// used internally
	int32_t getNumWordsInMatch ( Words *words     ,
				  int32_t   wn        , 
				  int32_t   n         , 
				  int32_t  *numQWords ,
				  int32_t  *qwn       ,
				  bool   allowPunctInPhrase = true ) ;

	// how many words matched a rawTermId?
	Match  m_matches[MAX_MATCHES_FOR_BIG_HACK];
	int32_t   m_numMatches;
	//int32_t   m_numNegTerms;

	// found term vector, 1-1 with m_q->m_qterms[]
	//char   m_foundTermVector    [ MAX_QUERY_TERMS ];
	//char   m_foundNegTermVector [ MAX_QUERY_TERMS ];
	qvec_t m_explicitBitsMatched;

	// . hash query word ids into a small hash table
	// . we use this to see what words in the document are query terms
	int64_t m_qtableIds      [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	int32_t      m_qtableWordNums [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	char      m_qtableFlags    [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	//int64_t m_qtableNegIds   [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	int32_t      m_numSlots;
	Query    *m_q;
	int32_t      m_maxNQW;
	int32_t     *m_tscores;
	int32_t      m_numAlnums;

	// . 1-1 with Query::m_qwords[] array of QWords
	// . shows the match flags for that query word
	//mf_t      m_qwordFlags[MAX_QUERY_WORDS];
	mf_t     *m_qwordFlags;
	int32_t m_qwordAllocSize;
	char m_tmpBuf[128];

	//stuff for detecting whether a match is part of a larger phrase
	void setSubPhraseDetection();
	void detectSubPhrase(Words* w, 
			     int32_t matchWordNum,
			     int32_t numMatchedWords,
			     int32_t queryWordNum,
			     int32_t diversityWeight );
	float getDiversity();

	bool m_detectSubPhrases;
	int32_t m_leftDiversity;
	int32_t m_rightDiversity;

	static const int32_t m_htSize = 128 * (sizeof(int64_t) + sizeof(int32_t));
	char m_subPhraseBuf[2 * m_htSize];
	HashTableT<int64_t, int32_t> m_pre;
	HashTableT<int64_t, int32_t> m_post;

	// . one words/scores/bits/pos/flags class per "match group"
	// . match groups examples = body, a single link text, a meta tag, etc.
	// . match groups are basically disjoint chunks of text information
	// . the document body (web page) is considered a single match group
	// . a single link text is considered a match group
	// . a single meta summary tag is a match group, ...
	Words    *m_wordsPtr    [MAX_MATCHGROUPS];
	Sections *m_sectionsPtr [MAX_MATCHGROUPS];
	Bits     *m_bitsPtr     [MAX_MATCHGROUPS];
	Pos      *m_posPtr      [MAX_MATCHGROUPS];
	mf_t      m_flags       [MAX_MATCHGROUPS];
	int32_t      m_numMatchGroups;

	Words    m_wordsArray    [MAX_MATCHGROUPS];
	Bits     m_bitsArray     [MAX_MATCHGROUPS];
	Pos      m_posArray      [MAX_MATCHGROUPS];

	int64_t *m_pids2;
	int64_t *m_pids3;
	int64_t *m_pids4;
	int64_t *m_pids5;

	// MDW: i am hoping we do not need this, it is really only useful
	// for the words in the body of the document!!
	//Sections m_sectionsArray [MAX_MATCHGROUPS];

	bool getMatchGroup ( mf_t       matchFlag ,
			     Words    **wp        ,
			     Pos      **pp        ,
			     Sections **sp        );

	//qvec_t m_explicitsMatched;
	//qvec_t m_matchableRequiredBits;
	// this may be false and still be included in the results if rat=0
	//bool   m_hasAllQueryTerms;
	// if this is false big hack will exclude it from results
	//bool   m_matchesQuery;
};

#define OFFSET_BYTES 1
#define OFFSET_WORDS 2

// smaller class for attaching to Msg20
class MatchOffsets{
public:
	MatchOffsets();
	~MatchOffsets();
	void reset();
	bool set(Xml *xml, Words *words, Matches *matches, 
		 unsigned char offsetType);

	int32_t getStoredSize();
	int32_t serialize(char *buf, int32_t buflen);
	int32_t deserialize(char *buf, int32_t buflen);

	int32_t m_numMatches;
	unsigned char m_queryWords[MAX_MATCHES];
	int32_t m_matchOffsets[MAX_MATCHES];
	// keep track of breaks between matches
	int32_t m_numBreaks;
	int32_t m_breakOffsets[MAX_MATCHES];
	// and total number of alnums in the document
	int32_t m_numAlnums;
};
#endif
