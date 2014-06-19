// Matt Wells, copyright Nov 2008

#ifndef _IMAGES_H_
#define _IMAGES_H_

#include "Msg0.h"
#include "Msg36.h"
#include "Msg13.h"
#include "IndexList.h"
#include "MsgC.h"
#include "SafeBuf.h"
#include "HttpRequest.h" // FORMAT_HTML

#define MAX_IMAGES 500

// a single serialized thumbnail:
class ThumbnailInfo {
 public:
	long  m_origDX;
	long  m_origDY;
	long  m_dx;
	long  m_dy;
	long  m_urlSize;
	long  m_dataSize;
	char  m_buf[];
	char *getUrl() { return m_buf; };
	char *getData() { return m_buf + m_urlSize; };
	long  getDataSize() { return m_dataSize; };
	long  getSize () { return sizeof(ThumbnailInfo)+m_urlSize+m_dataSize;};

	// make sure neither the x or y side is > maxSize
	bool printThumbnailInHtml ( SafeBuf *sb , 
				    long maxWidth,
				    long maxHeight,
				    bool printLink ,
				    long *newdx ,
				    char *style = NULL ,
				    char format = FORMAT_HTML ) ;
};

// XmlDoc::ptr_imgData is a ThumbnailArray
class ThumbnailArray {
 public:
	// 1st byte if format version
	char m_version;
	// # of thumbs
	long m_numThumbnails;
	// list of ThumbnailInfos
	char m_buf[];

	long getNumThumbnails() { return m_numThumbnails;};

	ThumbnailInfo *getThumbnailInfo ( long x ) {
		if ( x >= m_numThumbnails ) return NULL;
		char *p = m_buf;
		for ( long i = 0 ; i < m_numThumbnails ; i++ ) {
			if ( i == x ) return (ThumbnailInfo *)p;
			ThumbnailInfo *ti = (ThumbnailInfo *)p;
			p += ti->getSize();
		}
		return NULL;
	};
};

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
			     class Sections *sections ,
			     class XmlDoc *xd );
	
	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "termFreq" should NOT be on the stack in case we block
	// . sets *termFreq to UPPER BOUND on # of records with that "termId"
	bool getThumbnail ( char *pageSite ,
			    long  siteLen  ,
			    long long docId ,
			    class XmlDoc *xd ,
			    collnum_t collnum,
			    //char **statusPtr ,
			    long hopCount,
			    void   *state ,
			    void   (*callback)(void *state) );

	//char *getImageData    () { return m_imgData; };
	//long  getImageDataSize() { return m_imgDataSize; };
	//long  getImageType    () { return m_imageType; };

	SafeBuf m_imageBuf;
	bool m_imageBufValid;
	long m_phase;

	bool gotTermFreq();
	bool launchRequests();
	void gotTermList();
	bool downloadImages();



	bool getImageIp();
	bool downloadImage();
	bool makeThumb();

	//bool gotImage ( );
	void thumbStart_r ( bool amThread );

	long  m_i;
	long  m_j;

	class XmlDoc *m_xd;

	// callback information
	void  *m_state  ;
	void (* m_callback)(void *state );

	bool      m_setCalled;
	long      m_errno;
	long      m_hadError;
	bool      m_stopDownloading;
	//char    **m_statusPtr;
	char      m_statusBuf[128];
	collnum_t m_collnum;

	long long   m_docId;
	IndexList   m_list;

	long m_latestIp;
	MsgC m_msgc;
	Url m_imageUrl;

	long      m_numImages;
	long      m_imageNodes[MAX_IMAGES];
	// termids for doing gbimage:<url> lookups for uniqueness
	long long m_termIds   [MAX_IMAGES];
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
	char *m_imgReply;
	long  m_imgReplyLen;      // how many bytes the image is
	long  m_imgReplyMaxLen;   // allocated for the image
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
