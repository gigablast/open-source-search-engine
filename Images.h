// Matt Wells, copyright Nov 2008

#ifndef _IMAGES_H_
#define _IMAGES_H_

#include "Msg0.h"
#include "Msg36.h"
#include "Msg13.h"
#include "IndexList.h"

#define MAX_IMAGES 500

class Images {

 public:

	Images();

	void reset();

	// . hash the candidates with a gbimage: prefix
	// . called by XmlDoc.cpp
	// . used by Image.cpp for determining uniqueness of image
	//bool hash ( long trVersion ,
	//	    class Xml       *xml             ,
	//	    class Url *url ,
	//	    class TermTable *table ,
	//	    long        score );

	// set the m_imageNodes[] array to the potential thumbnails
	void setCandidates ( class Url      *pageUrl , 
			     class Words    *words , 
			     class Xml      *xml ,
			     class Sections *sections );
	
	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "termFreq" should NOT be on the stack in case we block
	// . sets *termFreq to UPPER BOUND on # of records with that "termId"
	bool getThumbnail ( char *pageSite ,
			    long  siteLen  ,
			    long long docId ,
			    class XmlDoc *xd ,
			    char *coll ,
			    char **statusPtr ,
			    long hopCount,
			    void   *state ,
			    void   (*callback)(void *state) );

	char *getImageData    () { return m_imgData; };
	long  getImageDataSize() { return m_imgDataSize; };
	//long  getImageType    () { return m_imageType; };

	bool gotTermFreq();
	bool launchRequests();
	void gotTermList();
	bool downloadImages();
	bool gotImage ( );
	void thumbStart_r ( bool amThread );

	long  m_i;
	long  m_j;

	// callback information
	void  *m_state  ;
	void (* m_callback)(void *state );

	bool      m_setCalled;
	long      m_errno;
	long      m_hadError;
	bool      m_stopDownloading;
	char    **m_statusPtr;
	char      m_statusBuf[128];
	char     *m_coll;

	long long   m_docId;
	IndexList   m_list;

	long      m_numImages;
	long      m_imageNodes[MAX_IMAGES];
	// termids for doing gbimage:<url> lookups for uniqueness
	long      m_termIds   [MAX_IMAGES];
	// for the msg0 lookup, did we have an error?
	long      m_errors    [MAX_IMAGES];

	class Url      *m_pageUrl;
	class Xml      *m_xml;

	Msg13Request m_msg13Request;

	// . for getting # of permalinks from same hopcount/site
	// . we need at least 10 for the uniqueness test to be effective
	Msg36     m_msg36;
	
	// . for getting docids that have the image
	// . for the uniqueness test
	Msg0      m_msg0;

	Msg13     m_msg13;

	// download status
	long  m_httpStatus;
	// ptr to the image as downloaded
	char *m_imgData;
	long  m_imgDataSize;
	long  m_imgType;

	// udp slot buffer
	char *m_imgBuf;
	long  m_imgBufLen;      // how many bytes the image is
	long  m_imgBufMaxLen;   // allocated for the image
	long  m_dx;             // width of image in pixels
	long  m_dy;             // height of image in pixels
	bool  m_thumbnailValid; // is it a valid thumbnail image

	// we store the thumbnail into m_imgBuf, overwriting the original img
	long  m_thumbnailSize;
	// the thumbnail dimensions
	long  m_tdx;
	long  m_tdy;
};

#endif
