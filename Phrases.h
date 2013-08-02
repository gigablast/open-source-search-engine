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

	bool set2 ( Words *words, Bits *bits , long niceness ) {
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
		   long      titleRecVersion,
		   long      niceness);

	//long long getPhraseId   ( long n ) { return m_phraseIds [n]; };
	long long getPhraseId2  ( long n ) { return m_phraseIds2[n]; };
	//long long *getPhraseIds (        ) { return m_phraseIds ; };
	long long *getPhraseIds2(        ) { return m_phraseIds2; };
	long long *getPhraseIds3(        ) { return m_phraseIds3; };
	//long long *getPhraseIds4(        ) { return m_phraseIds4; };
	//long long *getPhraseIds5(        ) { return m_phraseIds5; };

	//long long *getStripPhraseIds (      ) { return m_stripPhraseIds ; };
	//long long getStripPhraseId   ( long n ) 
	//{ return m_stripPhraseIds [n]; };
	long      getPhraseSpam ( long n ) { return m_phraseSpam[n]; };
	bool      hasPhraseId   ( long n ) { return (m_phraseSpam[n]!=PSKIP);};
	bool      startsAPhrase ( long n ) { return (m_phraseSpam[n]!=PSKIP);};
	bool      isInPhrase    ( long n ) ;
	// . often word #i is involved in 2 phrases
	// . m_phraseIds[i] only holds the one he starts
	// . this gets the one he's in the middle of or on the right of
	// . used by Query.cpp for phrase-forcing
	//long long getLeftPhraseId       ( long i ) ;
	//long long getLeftStripPhraseId  ( long i ) ;
	//long      getLeftPhraseIndex    ( long i ) ;

	// . each non-spammy occurence of phrase adds "baseScore" to it's score
	/*
	bool hash ( TermTable      *table       ,
		    Weights        *weightsPtr  ,
		    unsigned long   baseScore   ,
		    unsigned long   maxScore    ,
		    long long       startHash   ,
		    char           *prefix1     ,
		    long            prefixLen1  ,
		    char           *prefix2     ,
		    long            prefixLen2  ,
		    bool            hashUniqueOnly ,
		    long            titleRecVersion,
		    long            niceness = 0);
	*/

	// . store phrase that starts with word #i into "dest"
	// . we also NULL terminated it in "dest"
	// . return length
	char *getPhrase ( long i , long *phrLen , long npw );
	//char *getNWordPhrase ( long i , long *phrLen , long npw ) ;
	//char *getStripPhrase ( long i , long *phrLen );

	//long  getNumWords         ( long i ) { return m_numWordsTotal[i]; };
	//long  getNumWordsInPhrase ( long i ) { return m_numWordsTotal [i]; };
	long  getNumWordsInPhrase2( long i ) { return m_numWordsTotal2[i]; };

	long  getMaxWordsInPhrase( long i , long long *pid ) ;
	long  getMinWordsInPhrase( long i , long long *pid ) ;

	// . leave this public so SimpleQuery.cpp can mess with it
	// . called by Phrases::set() above for each i
	// . we set phraseSpam to 0 to 100% typically
	// . we set phraseSpam to PSKIP if word #i cannot start a phrase
	void setPhrase ( long i ,
			 long niceness);

	// private:

	char  m_localBuf [ PHRASE_BUF_SIZE ];

	char *m_buf;
	long  m_bufSize;

	// . these are 1-1 with the words in the Words class
	// . phraseSpam is PSKIP if the phraseId is invalid
	//long long     *m_phraseIds  ;
	// the two word hash
	long long     *m_phraseIds2  ;
	long long     *m_phraseIds3  ;
	//long long     *m_phraseIds4  ;
	//long long     *m_phraseIds5  ;
	//long long     *m_stripPhraseIds  ;
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
	long           m_numPhrases; // should equal the # of words

	// placeholders to avoid passing to subroutine
	Words      *m_words;
	long long  *m_wids;
	char      **m_wptrs;
	long       *m_wlens;

	Bits    *m_bits;
	bool     m_useStems;
	bool     m_useStopWords;
	long     m_titleRecVersion;

	// replaces Scores
	//class Sections *m_sections;
	//class Section  *m_sectionPtrs;

	// word scores, set in Scores.cpp
	//long    *m_wordScores;
	// the score of the phrase is the min of the scores of the words that
	// make up the phrase
	//long    *m_phraseScores ;
};

#endif
