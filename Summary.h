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
#include "matches.h"
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
		   long long   *termFreqs          ,
		   bool         doStemming         ,
		   long         maxSummaryLen      , 
		   long         maxNumLines        ,
		   long         maxNumCharsPerLine ,
		   //long         bigSampleRadius    ,
		   //long         bigSampleMaxLen    ,
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
		    long long      *termFreqs          ,
		    float          *affWeights         , // 1-1 with qterms
		    //char           *coll               ,
		    //long            collLen            ,
		    bool            doStemming         ,
		    long            maxSummaryLen      , 
		    long            maxNumLines        ,
		    long            maxNumCharsPerLine ,
		    //long            bigSampleRadius    ,
		    //long            bigSampleMaxLen    ,
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
		    long            titleBufLen        = 0    );


	// this is NULL terminated
	char *getSummary    ( ) { return m_summary;    };
	long  getSummaryLen ( ) { return m_summaryLen; };

	// me = "max excerpts". we truncate the summary if we need to.
	// XmlDoc.cpp::getSummary(), likes to request more excerpts than are 
	// actually displayed so it has a bigger summary for deduping purposes.
	long  getSummaryLen ( long me ) ;

	// for related topics.. sample surrounding the query terms
	//char *getBigSampleBuf ( ) { return m_buf; };
	//long  getBigSampleLen ( ) { return m_bufLen; };

	void truncateSummaryForExcerpts ( long  numExcerpts  ,
					  long  maxSummaryLen,
					  char *dmozSumms    ,
					  long *dmozSummLens ,
					  long  numCatids    ,
					  bool *sumFromDmoz  );
	
	//float getDiversity() {return m_diversity;}
	//float getProximityScore() { return m_proximityScore; };

	// for places in summary
	/*
	bool      scanForLocations      ( );
	long      getNumSummaryLocs     ( ) {
		return m_summaryLocs.length()/sizeof(uint64_t); };
	long      getSummaryLocsSize    ( ) {
	        return m_summaryLocs.length(); }
	uint64_t *getSummaryLocs        ( ) {
		return (uint64_t *)m_summaryLocs.getBufStart(); };
	long      getSummaryLocsPopsSize( ) {
	        return m_summaryLocsPops.length(); }
	long     *getSummaryLocsPops    ( ) {
		return (long *)m_summaryLocsPops.getBufStart(); };
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
		   long       docLen             ,
		   Query     *q                  ,
		   long long *termFreqs          ,
		   bool       doStemming         ,
		   long       maxSummaryLen      , 
		   long       maxNumLines        ,
		   long       maxNumCharsPerLine ,
		   //long       bigSampleRadius    ,
		   //long       bigSampleMaxLen    ,
		   //long      *bigSampleLen       ,
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
				 //long      bigSampleRadius,
				 long      maxSummaryLen );

	void setSummaryScores ( class Matches *matches   , 
				//Words         *words     , 
				//Scores        *scores    ,  
				//Pos           *pos       ,
				//long           numNeedles,
				//Needle        *needles   ,
				Query         *q         ,
				float         *phraseAffWeights ,
				//long          *docSummaryScore,
				//long          *queryInSectionScore,
				long           commentStart );

	long long getBestWindow ( class Matches *matches ,
				  long           mn      ,
				  long          *lasta   ,
				  long          *besta   ,
				  long          *bestb   ,
				  char          *gotIt   ,
				  char          *retired ,
				  long           maxExcerptLen );
				  //long           numFindableQWords,
				  //char          *represented,
				  //long          *foundNew );

	void reduceQueryScores   ( class Matches *matches, 
				   long m, long a, long b ) ;
	void reduceScoreForWords ( class Matches *matches, long qtn ) ;

	// a wrapper basically for the set0 below
	bool set0 ( char *doc, long docLen, Query *q, class Msg20Request *mr);

	// . the old string based summary generator -- ULTRA FAST!
	// . resurrected from /gb/datil2-release.git/src/Summary.cpp
	// . returns false with g_errno set on error
	bool set1 ( char      *doc                ,
		    long       docLen             ,
		    Query     *q                  ,
		    long       maxSummaryLen      ,
		    long       maxNumLines        ,
		    long       maxNumCharsPerLine ,
		    long       bigSampleRadius    ,
		    long       bigSampleMaxLen    ,
		    long      *bigSampleLen       ,
		    char      *foundTermVector    ,
		    long long *termFreqs          ) ;

	// null terminate and store the summary here.
	char  m_summary      [ MAX_SUMMARY_LEN ];
	long  m_summaryLen;
	long  m_summaryExcerptLen [ MAX_SUMMARY_EXCERPTS ];
	long  m_numExcerpts;
	bool  m_isNormalized;
	// hold the big sample here
	//char *m_buf;
	//long  m_bufMaxLen;
	//long  m_bufLen;
	//bool  m_freeBuf;
        //char  m_localBuf[10032];

	long  m_maxNumCharsPerLine;

	long m_titleVersion;

	// ptr to the query
	Query     *m_q;

	// query scores
	//long *m_qscores;

	// pub date list offsets
	bool m_useDateLists;
	bool m_exclDateList;
	long m_begPubDateList;
	long m_endPubDateList;

	//float m_diversity;

	//float m_proximityScore;

	char *m_bitScoresBuf;
	long  m_bitScoresBufSize;
	float m_wordWeights[MAX_QUERY_WORDS];

	char    m_summaryLocBuf[MAX_SUMMARY_LOCS*sizeof(uint64_t)];
	SafeBuf m_summaryLocs;
	char    m_summaryLocPopsBuf[MAX_SUMMARY_LOCS*sizeof(long)];
	SafeBuf m_summaryLocsPops;
};

#endif

