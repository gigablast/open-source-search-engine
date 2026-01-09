// Matt Wells, copyright Aug 2005

#ifndef _TITLE_H_
#define _TITLE_H_
#include "SafeBuf.h"
#include "Query.h"
//#include "CollectionRec.h"

#define TITLE_LOCAL_SIZE 2048

class LinkInfo;

class Title {

 public:

	Title();
	~Title();
	void reset();

	// . set m_title to the title of the document represented by "xd"
	// . if getHardTitle is true will always use the title in the <title>
	//   tag, but if that is not present, will try dmoz titles before
	//   resorting to trying to guess a title from the document content
	//   or incoming link text.
	// . uses the following:
	// .   title tag
	// .   dmoz title
	// .   meta title tag
	// .   incoming link text
	// .   <hX> tags at the top of the scored content
	// .   blurbs in bold at the top of the scored content
	// . removes starting/trailing punctuation when necessary
	// . does not consult words with scores of 0 (unless a meta tag)
	// . maxTitleChars is in chars (a utf16 char is 2 bytes or more)
	// . maxTitleChars of -1 means no max
	bool setTitle ( class XmlDoc   *xd            ,
			class Xml      *xml           ,
			class Words    *words         , 
			class Sections *sections      ,
			class Pos      *pos           ,
			int32_t            maxTitleChars ,
			int32_t            maxTitleWords ,
			SafeBuf *pbuf,
			Query          *q , // = NULL,
			CollectionRec  *cr , // = NULL ,
			int32_t niceness );


	char *getTitle     ( ) { return m_title; };
	int32_t  getTitleLen ( ) { return m_titleBytes; }; // does NOT include \0


	bool copyTitle ( class Words *words, class Pos *pos,
			 int32_t  t0, int32_t  t1 , class Sections *sections );

	//int32_t getTitleScore( Words *w, int32_t t0, int32_t t1, int32_t *numFoundQTerms,
	//		    int32_t *alphaWordCount = NULL );
	
	float getSimilarity ( Words  *w1 , int32_t i0 , int32_t i1 ,
			      Words  *w2 , int32_t t0 , int32_t t1 );

	//static int cmpInLinkerScore(const void *A, const void *B);

	char *m_title;
	int32_t  m_titleBytes; // in bytes. does NOT include \0
	int32_t  m_titleAllocSize;
	char  m_localBuf [ TITLE_LOCAL_SIZE ];
	bool  m_htmlEncoded;
	char  m_niceness;

	// For weighting title scores with query terms
	Query *m_query;


	int32_t  m_maxTitleChars;
	int32_t  m_maxTitleWords;

	int32_t m_titleTagStart ;
	int32_t m_titleTagEnd   ;

 private:

	bool setTitle4 ( class XmlDoc   *xd            ,
			 class Xml      *xml           ,
			 class Words    *words         , 
			 class Sections *sections      , 
			 class Pos      *pos           ,
			 int32_t            maxTitleChars ,
			 int32_t            maxTitleWords ,
			 SafeBuf        *pbuf          ,
			 Query          *q             ,
			 CollectionRec  *cr            );

};

#endif
