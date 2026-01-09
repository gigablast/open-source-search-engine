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
		   int32_t            titleRecVersion           ,
		   // this is true to zero-out terms in the menus, otherwise
		   // we assign them a minimal score of 1
		   bool            eliminateMenus            ,
		   // provide it with a buffer to prevent a malloc
		   char           *buf               = NULL  ,
		   int32_t            bufSize           = 0     ,
		   int32_t            minIndexableWords = -1    );

	//char  m_localBuf [ MAX_WORDS*8*10 ];
	char  m_localBuf[SCORES_LOCALBUFSIZE];
	char *m_buf;
	int32_t  m_bufSize;
	bool  m_needsFree;

 private:

	// returns false and sets g_errno on error
	bool set ( class Words    *words                   ,
		   class Sections *sections                ,
		   int32_t            titleRecVersion         ,
		   bool            scoreBySection          ,
		   bool            indexContentSectionOnly ,
		   int32_t            minSectionScore         ,
		   int32_t            minAvgWordScore         ,
		   int32_t            minIndexableWords       ,
		   // these are for weighting top part of news articles
		   int32_t            numTopWords             ,
		   float           topWordsWeight          ,
		   float           topSentenceWeight       ,
		   int32_t            maxWordsInSentence      ,
		   char           *buf     = NULL          ,
		   int32_t            bufSize = 0             ) ;

 public:
	
	int32_t getMemUsed () { return m_bufSize; };

	int32_t getScore ( int32_t i ) { return m_scores[i]; };

	// private:

	bool setScoresBySection ( class Words *words ,
				  bool         indexContentSectionOnly ,
				  int32_t         minSectionScore         ,
				  int32_t         minAvgWordScore         );

	// percent to weight word scores by... actually from 0 to 128
	// for speed reasons
	int32_t *m_scores;
	//int32_t *m_rerankScores;

	// these are printed out by PageParser.cpp in TermTable.cpp
	bool   m_scoreBySection          ;
	bool   m_indexContentSectionOnly ;
	int32_t   m_minSectionScore         ;
	int32_t   m_minAvgWordScore         ;
	int32_t   m_minIndexableWords       ;
	int32_t   m_numTopWords             ;
	float  m_topWordsWeight          ;
	float  m_topSentenceWeight       ;
	int32_t   m_maxWordsInSentence      ;
};

#endif
