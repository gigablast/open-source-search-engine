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
	int32_t  m_origDX;
	int32_t  m_origDY;
	int32_t  m_dx;
	int32_t  m_dy;
	int32_t  m_urlSize;
	int32_t  m_dataSize;
	char  m_buf[];
	char *getUrl() { return m_buf; };
	char *getData() { return m_buf + m_urlSize; };
	int32_t  getDataSize() { return m_dataSize; };
	int32_t  getSize () { return sizeof(ThumbnailInfo)+m_urlSize+m_dataSize;};

	// make sure neither the x or y side is > maxSize
	bool printThumbnailInHtml ( SafeBuf *sb , 
				    int32_t maxWidth,
				    int32_t maxHeight,
				    bool printLink ,
				    int32_t *newdx ,
				    char *style = NULL ,
				    char format = FORMAT_HTML ) ;
};

// XmlDoc::ptr_imgData is a ThumbnailArray
class ThumbnailArray {
 public:
	// 1st byte if format version
	char m_version;
	// # of thumbs
	int32_t m_numThumbnails;
	// list of ThumbnailInfos
	char m_buf[];

	int32_t getNumThumbnails() { return m_numThumbnails;};

	ThumbnailInfo *getThumbnailInfo ( int32_t x ) {
		if ( x >= m_numThumbnails ) return NULL;
		char *p = m_buf;
		for ( int32_t i = 0 ; i < m_numThumbnails ; i++ ) {
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
	//bool hash ( int32_t trVersion ,
	//	    class Xml       *xml             ,
	//	    class Url *url ,
	//	    class TermTable *table ,
	//	    int32_t        score );

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
			    int32_t  siteLen  ,
			    int64_t docId ,
			    class XmlDoc *xd ,
			    collnum_t collnum,
			    //char **statusPtr ,
			    int32_t hopCount,
			    void   *state ,
			    void   (*callback)(void *state) );

	//char *getImageData    () { return m_imgData; };
	//int32_t  getImageDataSize() { return m_imgDataSize; };
	//int32_t  getImageType    () { return m_imageType; };

	SafeBuf m_imageBuf;
	bool m_imageBufValid;
	int32_t m_phase;

	bool gotTermFreq();
	bool launchRequests();
	void gotTermList();
	bool downloadImages();



	bool getImageIp();
	bool downloadImage();
	bool makeThumb();

	char *getImageUrl ( int32_t j , int32_t *urlLen ) ;

	//bool gotImage ( );
	void thumbStart_r ( bool amThread );

	int32_t  m_i;
	int32_t  m_j;

	class XmlDoc *m_xd;

	// callback information
	void  *m_state  ;
	void (* m_callback)(void *state );

	int32_t m_xysize;

	bool      m_setCalled;
	int32_t      m_errno;
	int32_t      m_hadError;
	bool      m_stopDownloading;
	//char    **m_statusPtr;
	char      m_statusBuf[128];
	collnum_t m_collnum;

	int64_t   m_docId;
	IndexList   m_list;

	int32_t m_latestIp;
	MsgC m_msgc;
	Url m_imageUrl;

	int32_t      m_numImages;
	int32_t      m_imageNodes[MAX_IMAGES];
	// termids for doing gbimage:<url> lookups for uniqueness
	int64_t m_termIds   [MAX_IMAGES];
	// for the msg0 lookup, did we have an error?
	int32_t      m_errors    [MAX_IMAGES];

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
	int32_t  m_httpStatus;
	// ptr to the image as downloaded
	char *m_imgData;
	int32_t  m_imgDataSize;
	int32_t  m_imgType;

	// udp slot buffer
	char *m_imgReply;
	int32_t  m_imgReplyLen;      // how many bytes the image is
	int32_t  m_imgReplyMaxLen;   // allocated for the image
	int32_t  m_dx;             // width of image in pixels
	int32_t  m_dy;             // height of image in pixels
	bool  m_thumbnailValid; // is it a valid thumbnail image

	// we store the thumbnail into m_imgBuf, overwriting the original img
	int32_t  m_thumbnailSize;
	// the thumbnail dimensions
	int32_t  m_tdx;
	int32_t  m_tdy;
};

#endif
