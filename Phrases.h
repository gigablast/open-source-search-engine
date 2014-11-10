// Matt Wells, copyright Jul 2001

// . generate phrases and store their hashes into m_phraseIds[] array
// . hash() will then hash the phraseIds into the TermTable (hashtable)
// . will it hash a word as a phrase if it's the only word? No, it will not.
//   it only hashes 2+ word phrases

#ifndef _PHRASES_H_
#define _PHRASES_H_

//#include "TermTable.h"
#include "Bits.h"
//#include "Spam.h"
//#include "Scores.h"
#include "Words.h"
//#include "Weights.h"

#define PHRASE_BUF_SIZE (MAX_WORDS * 14)

#define PSKIP 201

class Phrases {

 public:

	Phrases();
	~Phrases();
	void reset() ;

	bool set2 ( Words *words, Bits *bits , int32_t niceness ) {
		return set ( words,bits,true,false,TITLEREC_CURRENT_VERSION,
			     niceness); };

	// . set the hashes (m_phraseIds) of the phrases for these words
	// . a phraseSpam of PSKIP means word is not in a phrase
	// . "bits" describes the words in a phrasing context
	// . "spam" is % spam of each word (spam may be NULL)
	bool set ( Words    *words, 
		   Bits     *bits ,
		   //Spam     *spam ,
		   //Scores   *scores ,
		   bool      useStopWords ,
		   bool      useStems     ,
		   int32_t      titleRecVersion,
		   int32_t      niceness);

	//int64_t getPhraseId   ( int32_t n ) { return m_phraseIds [n]; };
	int64_t getPhraseId2  ( int32_t n ) { return m_phraseIds2[n]; };
	//int64_t *getPhraseIds (        ) { return m_phraseIds ; };
	int64_t *getPhraseIds2(        ) { return m_phraseIds2; };
	int64_t *getPhraseIds3(        ) { return m_phraseIds3; };
	//int64_t *getPhraseIds4(        ) { return m_phraseIds4; };
	//int64_t *getPhraseIds5(        ) { return m_phraseIds5; };

	//int64_t *getStripPhraseIds (      ) { return m_stripPhraseIds ; };
	//int64_t getStripPhraseId   ( int32_t n ) 
	//{ return m_stripPhraseIds [n]; };
	int32_t      getPhraseSpam ( int32_t n ) { return m_phraseSpam[n]; };
	bool      hasPhraseId   ( int32_t n ) { return (m_phraseSpam[n]!=PSKIP);};
	bool      startsAPhrase ( int32_t n ) { return (m_phraseSpam[n]!=PSKIP);};
	bool      isInPhrase    ( int32_t n ) ;
	// . often word #i is involved in 2 phrases
	// . m_phraseIds[i] only holds the one he starts
	// . this gets the one he's in the middle of or on the right of
	// . used by Query.cpp for phrase-forcing
	//int64_t getLeftPhraseId       ( int32_t i ) ;
	//int64_t getLeftStripPhraseId  ( int32_t i ) ;
	//int32_t      getLeftPhraseIndex    ( int32_t i ) ;

	// . each non-spammy occurence of phrase adds "baseScore" to it's score
	/*
	bool hash ( TermTable      *table       ,
		    Weights        *weightsPtr  ,
		    uint32_t   baseScore   ,
		    uint32_t   maxScore    ,
		    int64_t       startHash   ,
		    char           *prefix1     ,
		    int32_t            prefixLen1  ,
		    char           *prefix2     ,
		    int32_t            prefixLen2  ,
		    bool            hashUniqueOnly ,
		    int32_t            titleRecVersion,
		    int32_t            niceness = 0);
	*/

	// . store phrase that starts with word #i into "dest"
	// . we also NULL terminated it in "dest"
	// . return length
	char *getPhrase ( int32_t i , int32_t *phrLen , int32_t npw );
	//char *getNWordPhrase ( int32_t i , int32_t *phrLen , int32_t npw ) ;
	//char *getStripPhrase ( int32_t i , int32_t *phrLen );

	//int32_t  getNumWords         ( int32_t i ) { return m_numWordsTotal[i]; };
	//int32_t  getNumWordsInPhrase ( int32_t i ) { return m_numWordsTotal [i]; };
	int32_t  getNumWordsInPhrase2( int32_t i ) { return m_numWordsTotal2[i]; };

	int32_t  getMaxWordsInPhrase( int32_t i , int64_t *pid ) ;
	int32_t  getMinWordsInPhrase( int32_t i , int64_t *pid ) ;

	// . leave this public so SimpleQuery.cpp can mess with it
	// . called by Phrases::set() above for each i
	// . we set phraseSpam to 0 to 100% typically
	// . we set phraseSpam to PSKIP if word #i cannot start a phrase
	void setPhrase ( int32_t i ,
			 int32_t niceness);

	// private:

	char  m_localBuf [ PHRASE_BUF_SIZE ];

	char *m_buf;
	int32_t  m_bufSize;

	// . these are 1-1 with the words in the Words class
	// . phraseSpam is PSKIP if the phraseId is invalid
	//int64_t     *m_phraseIds  ;
	// the two word hash
	int64_t     *m_phraseIds2  ;
	int64_t     *m_phraseIds3  ;
	//int64_t     *m_phraseIds4  ;
	//int64_t     *m_phraseIds5  ;
	//int64_t     *m_stripPhraseIds  ;
	unsigned char *m_phraseSpam ;
	// . # words in phrase TOTAL (including punct words)
	// . used for printing
	// . used by SimpleQuery::getTermIds() for setting word ranges
	//   for phrases
	//unsigned char *m_numWordsTotal ;
	// for the two word phrases:
	unsigned char *m_numWordsTotal2 ;
	unsigned char *m_numWordsTotal3 ;
	//unsigned char *m_numWordsTotal4 ;
	//unsigned char *m_numWordsTotal5 ;
	int32_t           m_numPhrases; // should equal the # of words

	// placeholders to avoid passing to subroutine
	Words      *m_words;
	int64_t  *m_wids;
	char      **m_wptrs;
	int32_t       *m_wlens;

	Bits    *m_bits;
	bool     m_useStems;
	bool     m_useStopWords;
	int32_t     m_titleRecVersion;

	// replaces Scores
	//class Sections *m_sections;
	//class Section  *m_sectionPtrs;

	// word scores, set in Scores.cpp
	//int32_t    *m_wordScores;
	// the score of the phrase is the min of the scores of the words that
	// make up the phrase
	//int32_t    *m_phraseScores ;
};

#endif
