// Matt Wells, copyright Jul 2001

// . highlights the terms in Query "q" in "xml" and puts results in m_buf

#include "Words.h"
#include "Query.h"
#include "Matches.h"
#include "Xml.h"
#include "Url.h"

#ifndef _HIGHLIGHT_H_
#define _HIGHLIGHT_H_

class Highlight {

 public:

	// . content is an html/xml doc
	// . we highlight Query "q" in "xml" as best as we can
	// . store highlighted text into "buf"
	// . return length stored into "buf"
	long set ( char        *buf          ,
		   long         bufLen       ,
		   char        *content      ,
		   long         contentLen   , 
		   char         docLangId    ,
		   Query       *q            ,
		   bool         doStemming   ,
		   bool         useAnchors   , // = false ,
		   const char  *baseUrl      , // = NULL  ,
		   const char  *frontTag     , // = NULL  ,
		   const char  *backTag      , // = NULL  ,
		   long         fieldCode    , // = 0     ,
		   long         niceness    ) ;
	
	long set ( char        *buf        ,
		   long         bufLen     ,
		   Words       *words      ,
		   Matches     *matches    ,
		   bool         doStemming ,
		   bool         useAnchors = false ,
		   const char  *baseUrl    = NULL  ,
		   const char  *frontTag   = NULL  ,
		   const char  *backTag    = NULL  ,
		   long         fieldCode  = 0     ,
		   Query       *q	   = NULL  ) ;

	long getNumMatches() { return m_numMatches; }

 private:

	bool highlightWords ( Words *words , Matches *m , Query *q=NULL );

	// null terminate and store the highlighted content in m_buf
	char    *m_buf ;
	long     m_bufLen;
	char    *m_bufPtr;
	char    *m_bufEnd;

	//Words    m_words;
	Matches  m_matches;
	//Xml     *m_xml;
	const char    *m_frontTag;
	const char    *m_backTag;
	long     m_frontTagLen;
	long     m_backTagLen;
	bool     m_doStemming;

	bool     m_useAnchors;  // click and scroll technology for cached pages
	long     m_anchorCounts [ MAX_QUERY_TERMS ];
	const char    *m_baseUrl;

	long m_numMatches;
	
	// so we don't repeat the same buf overflow error msg a gazillion times
	bool     m_didErrMsg;
	// . field code of the text excerpt to highlight
	// . only query terms with this fieldCode will be highlighted
	long     m_fieldCode;
};

#endif

