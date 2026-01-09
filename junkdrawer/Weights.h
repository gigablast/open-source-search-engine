// Matt Wells, copyright Apr 2007

// . the Weights class defines word weights, m_ww[], and phrase weights, m_pw[]
// . it emphasizes important words/phrases and deemphasizes others

#ifndef _WEIGHTS_H_
#define _WEIGHTS_H_

// the default weight is 128
//#define DW 128
// had to increase resolution, so made this 32k
#define DW 32768

#define LOCAL_BUF_SIZE 10000

#define MAX_RULES 30

#include "Words.h"

// this is RULE #15 implemented (See Weights.cpp) and is also used
// by neighborhood.cpp to hash/index neighborhoods properly.
void getWordToPhraseRatioWeights ( int64_t         pid1 , // pre phrase
				   int64_t         wid1 ,
				   int64_t         pid2 ,
				   int64_t         wid2 , // post word
				   float            *ww   ,
				   float            *pw   ,
				   class HashTableX *tt1  ,
				   int32_t              titleRecVersion ) ;

class Weights {

 public:

	Weights();
	~Weights();
	void reset();

	bool set  ( class Words      *words              ,
		    class Phrases    *phrases            ,
		    class Bits       *bits               ,
		    class Sections   *sections           ,
		    class SafeBuf    *pbuf               ,
		    bool              eliminateMenus     ,
		    //class Scores   *scores             ,
		    //class LinkInfo *linkInfo           ,
		    //class LinkInfo *linkInfo2          ,
		    bool              isPreformattedText ,
		    int32_t              titleRecVersion    ,
		    int32_t              titleWeight        ,
		    int32_t              headerWeight       ,
		    class HashTableX *countTablePtr      ,
		    bool              isLinkText         ,
		    bool              isCountTable       ,
		    int32_t              siteNumInlinks     ,
		    int32_t              niceness           );
	
	bool set1 ( class Words    *words              ,
		    class Phrases  *phrases            ,
		    class Bits     *bits               ,
		    bool            isPreformattedText );
	
	bool set2 ( class Words    *words              ,
		    class Phrases  *phrases            ,
		    class Bits     *bits               );

	bool set3 ( Words *words , class Bits *bits ) ;

	bool set4 ( );

	//bool m_isRepeatSpammer;

 private:

	//int32_t getPunctPhraseWeight   ( int32_t i , char  *s , int32_t len ) ;
	//float getPunctWordWeight    ( char  *s , int32_t len ) ;

	void setPunctWeights ( int32_t k , char *s , int32_t len ) ;

	bool  setSpam           ( int32_t *profile, int32_t plen , int32_t numWords, 
				  unsigned char *spam );

	//int32_t  getProbSpam     ( int32_t *profile, int32_t plen , int32_t step      );
 
 public:

	class HashTableX *m_countTablePtr;

	bool       m_isLinkText;
	bool       m_isCountTable;
	int32_t       m_titleWeight;
	int32_t       m_headerWeight;
	int32_t       m_version;
	int32_t       m_nw;
	int64_t *m_wids;

	int32_t       m_niceness;

	int32_t *m_ww;
	int32_t *m_pw;

	// used for debug by XmlDoc::print()
	float *m_rvw;
	float *m_rvp;

	char *m_buf;
	int32_t  m_bufSize;
	char  m_localBuf[LOCAL_BUF_SIZE];

	class Words *m_words;
	class Bits  *m_bits;
	//int32_t         m_numRepeatSpam;
	int32_t         m_siteNumInlinks;
	//bool         m_totallySpammed;
};

extern float g_wtab[30][30];
extern float g_ptab[30][30];
/*
// . returns 0 to 100 , the probability of spam for this subprofile
// . a "profile" is an array of all the positions of a word in the document
// . a "position" is just the word #, like first word, word #8, etc...
// . we are passed a subprofile, "profile", of the actual profile
//   because some of the document may be more "spammy" than other parts
// . inlined to speed things up because this may be called multiple times
//   for each word in the document
// . if "step" is 1 we look at every       word position in the profile
// . if "step" is 2 we look at every other word position 
// . if "step" is 3 we look at every 3rd   word position, etc...
inline int32_t Weights::getProbSpam(int32_t *profile, int32_t plen, int32_t step) {

	// you can spam 2 or 1 letter words all you want to
	if ( plen <= 2 ) return 0;

	// if our step is bigger than the profile return 0
	if ( step == plen ) return 0;

	register int32_t avgSpacing, stdDevSpacing;
	int32_t d,dev=0;
	register int32_t i;
	
	for (int32_t j = 0; j < step; j++) {

		// find avg. of gaps between consecutive tokens in subprofile
		// TODO: isn't profile[i] < profile[i+1]??
		int32_t istop = plen-1;
		avgSpacing = 0;
		for (i=0; i < istop; i += step ) 
			avgSpacing += ( profile[i] - profile[i+1] ); 
		// there's 1 less spacing than positions in the profile
		// so we divide by plen-1
		avgSpacing = (avgSpacing * 256) / istop;

		// compute standard deviation of the gaps in this sequence
		stdDevSpacing = 0;
		for (i = 0 ; i < istop; i += step ) {
			d = (( profile[i] - profile[i+1]) * 256 ) - avgSpacing;
			if ( d < 0 ) stdDevSpacing -= d;
			else         stdDevSpacing += d;
		}

		// TODO: should we divide by istop-1 for stdDev??
		stdDevSpacing /= istop;

		// average of the stddevs for all sequences
		dev += stdDevSpacing;
	}

	dev /= step;
	
	// if the plen is big we should expect dev to be big
	// here's some interpolation points:
	// plen >=  2  and  dev<= 0.2  --> 100%
	// plen  =  7  and  dev = 1.0  --> 100%
	// plen  = 14  and  dev = 2.0  --> 100%
	// plen  = 21  and  dev = 3.0  --> 100%
	// plen  = 7   and  dev = 2.0  -->  50%

	// NOTE: dev has been multiplied by 256 to avoid using floats
	if ( dev <= 51.2 ) return 100;  // (.2 * 256)
	int32_t prob = ( (256*100/7) * plen ) / dev;

	if (prob>100) prob=100;

	return prob;

	//if (prob>=0) {
	//	int32_t i;
	//printf("dev=%i,plen=%i,nseq=%i,prob=%i----\n",dev,plen,step,prob);
	//	for (i=0;i<plen;i++)
	//		printf("%i#",profile[i]);
	//	printf("\n");
	//}
}
*/

#endif
