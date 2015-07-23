// Matt Wells, copyright Jul 2001

// . gets xhtml filtered into plain text
// . parses plain text into Words
// . gets rawTermIds from the query
// . uses Matches class to find words that match rawTermIds
// . for each term in query, find the line with that term and the most
//   other matching terms, and print that line

// . modifications...
// . exclude title from the plain text (call xml->getText() twice?)
// . find up to X lines
// . find phrases by setting the Phrases class as well
// . score lines by termfreqs of terms you highlight in the line
// . highlight terms in order of their termFreq, lowest first!
// . remove junk from start/end of summary (no back-to-back punct)
// . stop summary line on space, not non-alnum (no breaking on apostrophe)
// . don't highlight stop words????

#ifndef _SUMMARY_H_
#define _SUMMARY_H_

#include "gb-include.h"
#include "Unicode.h"
#include "matches2.h"
#include "Query.h"
#include "Xml.h"
#include "Mem.h"
//#include "LinkInfo.h" // BIG HACK support
#include "Words.h"
#include "Bits.h"
#include "Pos.h"
#include "Matches.h"
#include "HashTableT.h"
//#include "Places.h"
#include "Domains.h"
#include "CountryCode.h"

#define MAX_SUMMARY_LEN 1024*20
#define MAX_SUMMARY_EXCERPTS 1024
#define MAX_SUMMARY_LOCS 16

class Summary {

 public:

	Summary();
	~Summary();
	void reset();

	// . like above but flattens the xml for you then calls the above
	// . returns false and sets errno on error
	bool set ( class Xml   *xml                ,
		   class Query *q                  ,
		   int64_t   *termFreqs          ,
		   bool         doStemming         ,
		   int32_t         maxSummaryLen      , 
		   int32_t         maxNumLines        ,
		   int32_t         maxNumCharsPerLine ,
		   //int32_t         bigSampleRadius    ,
		   //int32_t         bigSampleMaxLen    ,
		   bool         ratInSummary = false ,
		   class Url   *f = NULL      );
		   //bool         excludeAnchText = false,
		   //bool         hackFixWords = false,
		   //bool         hackFixPhrases = false ) ;

	// this should eventually replace set()
	bool set2 ( class Xml      *xml                ,
		    class Words    *words              ,
		    class Bits     *bits               ,
		    class Sections *sections           ,
		    class Pos      *pos                ,
		    class Query    *q                  ,
		    int64_t      *termFreqs          ,
		    float          *affWeights         , // 1-1 with qterms
		    //char           *coll               ,
		    //int32_t            collLen            ,
		    bool            doStemming         ,
		    int32_t            maxSummaryLen      , 
		    int32_t            numDisplayLines    ,
		    int32_t            maxNumLines        ,
		    int32_t            maxNumCharsPerLine ,
		    //int32_t            bigSampleRadius    ,
		    //int32_t            bigSampleMaxLen    ,
		    bool            ratInSummary       ,
		    //TitleRec       *tr                 ,
		    class Url      *f ,
		    //bool            allowPunctInPhrase = true,
		    //bool            excludeLinkText    = false,
		    //bool            excludeMetaText    = false,
		    //bool            hackFixWords       = false,
		    //bool            hackFixPhrases     = false,
		    //float          *queryProximityScore= NULL ,
		    class Matches  *matches            = NULL ,
		    char           *titleBuf           = NULL ,
		    int32_t            titleBufLen        = 0    );


	// this is NULL terminated
	char *getSummary    ( ) { return m_summary;    };
	int32_t  getSummaryLen ( ) { return m_summaryLen; };

	// me = "max excerpts". we truncate the summary if we need to.
	// XmlDoc.cpp::getSummary(), likes to request more excerpts than are 
	// actually displayed so it has a bigger summary for deduping purposes.
	int32_t  getSummaryLen ( int32_t me ) ;

	// for related topics.. sample surrounding the query terms
	//char *getBigSampleBuf ( ) { return m_buf; };
	//int32_t  getBigSampleLen ( ) { return m_bufLen; };

	void truncateSummaryForExcerpts ( int32_t  numExcerpts  ,
					  int32_t  maxSummaryLen,
					  char *dmozSumms    ,
					  int32_t *dmozSummLens ,
					  int32_t  numCatids    ,
					  bool *sumFromDmoz  );
	
	//float getDiversity() {return m_diversity;}
	//float getProximityScore() { return m_proximityScore; };

	// for places in summary
	/*
	bool      scanForLocations      ( );
	int32_t      getNumSummaryLocs     ( ) {
		return m_summaryLocs.length()/sizeof(uint64_t); };
	int32_t      getSummaryLocsSize    ( ) {
	        return m_summaryLocs.length(); }
	uint64_t *getSummaryLocs        ( ) {
		return (uint64_t *)m_summaryLocs.getBufStart(); };
	int32_t      getSummaryLocsPopsSize( ) {
	        return m_summaryLocsPops.length(); }
	int32_t     *getSummaryLocsPops    ( ) {
		return (int32_t *)m_summaryLocsPops.getBufStart(); };
	*/
	
	// private:

	// . content is an html/xml doc
	// . we highlight "query" in "content" as best as we can
	// . returns false and sets errno on error
	// . CAUTION: this is destructive on "doc"
	// . stores bigSample into "doc" which should be "m_buf"
	//   and sets bytes stored into *bigSampleLen
	/*
	bool set ( char      *doc                ,
		   int32_t       docLen             ,
		   Query     *q                  ,
		   int64_t *termFreqs          ,
		   bool       doStemming         ,
		   int32_t       maxSummaryLen      , 
		   int32_t       maxNumLines        ,
		   int32_t       maxNumCharsPerLine ,
		   //int32_t       bigSampleRadius    ,
		   //int32_t       bigSampleMaxLen    ,
		   //int32_t      *bigSampleLen       ,
		   char      *foundTermVector    );
	*/
		
	// BIG HACK support
	//bool allQTermsFound( Query *q, TitleRec *tr, Xml *xml, 
	//		     class Matches *matches, 
	//		     qvec_t reqMask, qvec_t negMask,
	//		     bool excludeLinkText, 
	//		     bool excludeMetaText, 
	//		     bool allowPunctInPhrase );

	//////////////////////////////////////////////////////////////////
	//
	// THE NEW SUMMARY GENERATOR routines below here
	//
	//////////////////////////////////////////////////////////////////

	bool getDefaultSummary ( Xml      *xml,
				 Words    *words,
				 class Sections *sections ,
				 Pos      *pos,
				 //int32_t      bigSampleRadius,
				 int32_t      maxSummaryLen );

	void setSummaryScores ( class Matches *matches   , 
				//Words         *words     , 
				//Scores        *scores    ,  
				//Pos           *pos       ,
				//int32_t           numNeedles,
				//Needle        *needles   ,
				Query         *q         ,
				float         *phraseAffWeights ,
				//int32_t          *docSummaryScore,
				//int32_t          *queryInSectionScore,
				int32_t           commentStart );

	int64_t getBestWindow ( class Matches *matches ,
				  int32_t           mn      ,
				  int32_t          *lasta   ,
				  int32_t          *besta   ,
				  int32_t          *bestb   ,
				  char          *gotIt   ,
				  char          *retired ,
				  int32_t           maxExcerptLen );
				  //int32_t           numFindableQWords,
				  //char          *represented,
				  //int32_t          *foundNew );

	void reduceQueryScores   ( class Matches *matches, 
				   int32_t m, int32_t a, int32_t b ) ;
	void reduceScoreForWords ( class Matches *matches, int32_t qtn ) ;

	// a wrapper basically for the set0 below
	bool set0 ( char *doc, int32_t docLen, Query *q, class Msg20Request *mr);

	// . the old string based summary generator -- ULTRA FAST!
	// . resurrected from /gb/datil2-release.git/src/Summary.cpp
	// . returns false with g_errno set on error
	bool set1 ( char      *doc                ,
		    int32_t       docLen             ,
		    Query     *q                  ,
		    int32_t       maxSummaryLen      ,
		    int32_t       maxNumLines        ,
		    int32_t       maxNumCharsPerLine ,
		    int32_t       bigSampleRadius    ,
		    int32_t       bigSampleMaxLen    ,
		    int32_t      *bigSampleLen       ,
		    char      *foundTermVector    ,
		    int64_t *termFreqs          ) ;

	// null terminate and store the summary here.
	char  m_summary      [ MAX_SUMMARY_LEN ];
	int32_t  m_summaryLen;
	int32_t  m_summaryExcerptLen [ MAX_SUMMARY_EXCERPTS ];
	int32_t  m_numExcerpts;
	bool  m_isNormalized;
	// hold the big sample here
	//char *m_buf;
	//int32_t  m_bufMaxLen;
	//int32_t  m_bufLen;
	//bool  m_freeBuf;
        //char  m_localBuf[10032];

	// if getting more lines for deduping than we need for displaying,
	// how big is that part of the summary to display?
	int32_t m_numDisplayLines;
	int32_t m_displayLen;
	int32_t getSummaryDisplayLen() { return m_displayLen; }

	int32_t  m_maxNumCharsPerLine;

	int32_t m_titleVersion;

	// ptr to the query
	Query     *m_q;

	// query scores
	//int32_t *m_qscores;

	// pub date list offsets
	bool m_useDateLists;
	bool m_exclDateList;
	int32_t m_begPubDateList;
	int32_t m_endPubDateList;

	//float m_diversity;

	//float m_proximityScore;

	char *m_bitScoresBuf;
	int32_t  m_bitScoresBufSize;
	//float m_wordWeights[MAX_QUERY_WORDS];
	float *m_wordWeights;
	int32_t m_wordWeightSize;
	char m_tmpBuf[128];

	char *m_buf4;
	int32_t m_buf4Size;
	char m_tmpBuf4[128];

	char    m_summaryLocBuf[MAX_SUMMARY_LOCS*sizeof(uint64_t)];
	SafeBuf m_summaryLocs;
	char    m_summaryLocPopsBuf[MAX_SUMMARY_LOCS*sizeof(int32_t)];
	SafeBuf m_summaryLocsPops;
};

#endif

