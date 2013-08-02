// Matt Wells, copyright Jul 2005

// . the Scores class is a vector to weight Words scores by
// . this was originally made to extract the news article from a web page
//   and discard the words in menu sections and other cruft.
// . words are weighted by the number of neighboring words in their "sections"
//   that are not in hyperlinks. 
// . "sections" are determined by table/tr/td/div/... etc tags
// . m_scores is 1-1 with the words in the supplied "words" class

#ifndef _SCORES_H_
#define _SCORES_H_

#include "Words.h"

// if you change this you must also change the shift logic in Phrases.cpp
// for setting the "minScore"
#define NORM_WORD_SCORE 128

#define SCORES_LOCALBUFSIZE 20

class Scores {

 public:

	Scores();
	~Scores();
	void reset();

	// if indexContentSectionsOnly is true, only the words in the most 
	// relevant scores will have positive scores, all other words are 
	// discarded.
	//bool set ( class Words *words , bool indexContentSectionsOnly );
	bool set ( class Words    *words                     ,
		   class Sections *sections                  ,
		   long            titleRecVersion           ,
		   // this is true to zero-out terms in the menus, otherwise
		   // we assign them a minimal score of 1
		   bool            eliminateMenus            ,
		   // provide it with a buffer to prevent a malloc
		   char           *buf               = NULL  ,
		   long            bufSize           = 0     ,
		   long            minIndexableWords = -1    );

	//char  m_localBuf [ MAX_WORDS*8*10 ];
	char  m_localBuf[SCORES_LOCALBUFSIZE];
	char *m_buf;
	long  m_bufSize;
	bool  m_needsFree;

 private:

	// returns false and sets g_errno on error
	bool set ( class Words    *words                   ,
		   class Sections *sections                ,
		   long            titleRecVersion         ,
		   bool            scoreBySection          ,
		   bool            indexContentSectionOnly ,
		   long            minSectionScore         ,
		   long            minAvgWordScore         ,
		   long            minIndexableWords       ,
		   // these are for weighting top part of news articles
		   long            numTopWords             ,
		   float           topWordsWeight          ,
		   float           topSentenceWeight       ,
		   long            maxWordsInSentence      ,
		   char           *buf     = NULL          ,
		   long            bufSize = 0             ) ;

 public:
	
	long getMemUsed () { return m_bufSize; };

	long getScore ( long i ) { return m_scores[i]; };

	// private:

	bool setScoresBySection ( class Words *words ,
				  bool         indexContentSectionOnly ,
				  long         minSectionScore         ,
				  long         minAvgWordScore         );

	// percent to weight word scores by... actually from 0 to 128
	// for speed reasons
	long *m_scores;
	//long *m_rerankScores;

	// these are printed out by PageParser.cpp in TermTable.cpp
	bool   m_scoreBySection          ;
	bool   m_indexContentSectionOnly ;
	long   m_minSectionScore         ;
	long   m_minAvgWordScore         ;
	long   m_minIndexableWords       ;
	long   m_numTopWords             ;
	float  m_topWordsWeight          ;
	float  m_topSentenceWeight       ;
	long   m_maxWordsInSentence      ;
};

#endif
