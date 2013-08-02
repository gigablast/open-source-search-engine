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

typedef long mf_t;

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
	long m_wordNum;
	// # of words in this match, like if we match a phrase
	// we have > 1 words in the match
	long m_numWords;
	// word # we match in the query
	long m_qwordNum;
	// # of query words we match if we are a phrase, otherwise
	// this is 1
	long m_numQWords;
	// . used for highlighting under different colors (Highlight.cpp)
	// . words in the same quote should use the same highlight color
	long m_colorNum;

	// . max score of all contained query terms in this match
	// . uses provided m_tscores[] array to get scores of the query terms
	//long m_score;

	// "match group" or type of match. i.e. MF_TITLETAG, MF_METASUMM, ...
	mf_t m_flags;

	// improve summary generation parms
	long m_dist;
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

	void setQuery ( Query *q );//, long *tscores = NULL ) ;

	bool set ( class XmlDoc   *xd           ,
		   class Words    *bodyWords    ,
		   //class Synonyms *bodySynonyms ,
		   class Phrases  *bodyPhrases ,
		   class Sections *bodySections ,
		   class Bits     *bodyBits     ,
		   class Pos      *bodyPos      ,
		   class Xml      *xml          ,
		   class Title    *tt           ,
		   long            niceness     ) ;

	bool addMatches ( char      *s         ,
			  long       slen      ,
			  mf_t       flags     ,
			  long long  docId     ,
			  long       niceness  ) ;

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
			  long      fieldCode           = 0    , // wrds,0=none
			  bool      allowPunctInPhrase  = true ,
			  bool      exclQTOnlyInAnchTxt = false,
			  qvec_t    reqMask             = 0    ,
			  qvec_t    negMask             = 0    ,
			  long      diversityWeight     = 1    ,
			  long long docId               = 0    ,
			  mf_t      flags               = 0    );

	// this is NULL terminated
	//char getTermNum  ( long i ) { return m_termNums[i]; };
	//char getTermNum2 ( long i ) { return m_termNums2[i]; };

	// get the whole array
	//char *getTermNums ( ) { return m_termNums; };
	//char *getPhraseNums ( ) { return m_phraseNums; };

	// how many words matched a rawTermId?
	long getNumMatches ( ) { return m_numMatches; };

	//void clearBitsMatched( void       ) { m_explicitBitsMatched = 0x00; };
	//void addBitMatched   ( qvec_t bit ) { m_explicitBitsMatched |= bit; };
	//qvec_t getBitsMatched( void       ) { return m_explicitBitsMatched; };

	// set m_termNums from a to b to 0
	//void clearMatches ( long a , long b );

	// janitorial stuff
	Matches ( ) ;
	~Matches( ) ;
	void reset ( ) ;

	// BIG HACK support
	//long getTermsFound ( bool *hadPhrases , bool *hadWords );
	unsigned long getTermsFound2(bool *hadPhrases, bool *hadWords);
	//bool negTermsFound ( );

	// used internally and by PageGet.cpp
	bool isMatchableTerm ( class QueryTerm *qt );//, long i );

	// used internally
	long getNumWordsInMatch ( Words *words     ,
				  long   wn        , 
				  long   n         , 
				  long  *numQWords ,
				  long  *qwn       ,
				  bool   allowPunctInPhrase = true ) ;

	// how many words matched a rawTermId?
	Match  m_matches[MAX_MATCHES_FOR_BIG_HACK];
	long   m_numMatches;
	//long   m_numNegTerms;

	// found term vector, 1-1 with m_q->m_qterms[]
	//char   m_foundTermVector    [ MAX_QUERY_TERMS ];
	//char   m_foundNegTermVector [ MAX_QUERY_TERMS ];
	qvec_t m_explicitBitsMatched;

	// . hash query word ids into a small hash table
	// . we use this to see what words in the document are query terms
	long long m_qtableIds      [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	long      m_qtableWordNums [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	char      m_qtableFlags    [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	//long long m_qtableNegIds   [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	long      m_numSlots;
	Query    *m_q;
	long      m_maxNQW;
	long     *m_tscores;
	long      m_numAlnums;

	// . 1-1 with Query::m_qwords[] array of QWords
	// . shows the match flags for that query word
	mf_t      m_qwordFlags[MAX_QUERY_WORDS];

	//stuff for detecting whether a match is part of a larger phrase
	void setSubPhraseDetection();
	void detectSubPhrase(Words* w, 
			     long matchWordNum,
			     long numMatchedWords,
			     long queryWordNum,
			     long diversityWeight );
	float getDiversity();

	bool m_detectSubPhrases;
	long m_leftDiversity;
	long m_rightDiversity;

	static const long m_htSize = 128 * (sizeof(long long) + sizeof(long));
	char m_subPhraseBuf[2 * m_htSize];
	HashTableT<long long, long> m_pre;
	HashTableT<long long, long> m_post;

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
	long      m_numMatchGroups;

	Words    m_wordsArray    [MAX_MATCHGROUPS];
	Bits     m_bitsArray     [MAX_MATCHGROUPS];
	Pos      m_posArray      [MAX_MATCHGROUPS];

	long long *m_pids2;
	long long *m_pids3;
	long long *m_pids4;
	long long *m_pids5;

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

	long getStoredSize();
	long serialize(char *buf, long buflen);
	long deserialize(char *buf, long buflen);

	long m_numMatches;
	unsigned char m_queryWords[MAX_MATCHES];
	long m_matchOffsets[MAX_MATCHES];
	// keep track of breaks between matches
	long m_numBreaks;
	long m_breakOffsets[MAX_MATCHES];
	// and total number of alnums in the document
	long m_numAlnums;
};
#endif
