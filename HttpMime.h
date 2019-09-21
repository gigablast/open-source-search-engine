// Matt Wells, copyright Jun 2000

// . class to parse an html MIME header

#ifndef _HTTPMIME_H_
#define _HTTPMIME_H_

// convert text/html to CT_HTML for instance
// convert application/json to CT_JSON for instance
int32_t getContentTypeFromStr ( char *s ) ;

const char *extensionToContentTypeStr2 ( char *ext , int32_t elen ) ;

#include <time.h>

void   getTime    ( char *s , int *sec , int *min , int *hour ) ;
int32_t   getMonth   ( char *s ) ;
int32_t   getWeekday ( char *s ) ;
time_t atotime    ( char *s ) ;
time_t atotime1   ( char *s ) ;
time_t atotime2   ( char *s ) ;
time_t atotime3   ( char *s ) ;
time_t atotime4   ( char *s ) ;
time_t atotime5   ( char *s ) ;

// the various content types
#define CT_UNKNOWN 0
#define CT_HTML    1
#define CT_TEXT    2
#define CT_XML     3
#define CT_PDF     4
#define CT_DOC     5
#define CT_XLS     6
#define CT_PPT     7
#define CT_PS      8
// images
#define CT_GIF     9
#define CT_JPG    10
#define CT_PNG    11
#define CT_TIFF   12
#define CT_BMP    13
#define CT_JS     14
#define CT_CSS    15
#define CT_JSON   16
#define CT_IMAGE  17
#define CT_STATUS 18 // an internal type indicating spider reply
#define CT_GZ     19
#define CT_ARC    20
#define CT_WARC   21

#define ET_IDENTITY 0
#define ET_GZIP 1
#define ET_COMPRESS 2
#define ET_DEFLATE 3


extern char *g_contentTypeStrings[];

#include <time.h>   // time_t mktime()
#include "Url.h"

class HttpMime {

 public:

	bool init ( ) ;
	void reset() ;
	HttpMime   () ;

	// . returns false and sets errno if could not get a valid mime
	// . just copies bits and pieces so you can free "mime" whenever
	// . we need "url" to set m_locUrl if it's a relative redirect
	bool set ( char *httpReply , int32_t replyLen , Url *url );

	// these 2 sets are used to dress up a fake mime for passing
	// to TitleRec::set() called from Msg16 getDoc() routine
	void setLastModifiedDate ( time_t date ) { m_lastModifiedDate = date;};
	void setContentType      ( int32_t   t    ) { m_contentType      = t; };
	void setHttpStatus       ( int32_t status ) { m_status        = status; };

	// http status: 404, 200, etc.
	int32_t   getHttpStatus      () { return m_status;           };
	char  *getContent         () { return m_content; };
	int32_t   getContentLen      () { return m_contentLen;       };
	time_t getLastModifiedDate() { return m_lastModifiedDate; };
	int32_t   getContentType     () { return m_contentType;      };
	bool   isEmpty            () { return ( m_status == -1); };
	Url   *getLocationUrl     () { return &m_locUrl;    };
	char  *getCookie          () { return m_cookie; };
	int32_t   getCookieLen       () { return m_cookieLen; };

	// new stuff for Msg13.cpp to use
	char *getLocationField    () { return m_locationField; }
	int32_t  getLocationFieldLen () { return m_locationFieldLen; }

	// compute length of a possible mime starting at "buf"
	int32_t getMimeLen ( char *buf , int32_t bufLen , int32_t *boundaryLen ) ;

	// . used to create a mime
	// . if bytesToSend is < 0 that means send totalContentLen (all doc)
	// . if lastModified is 0 we take the current time and use that
	// . cacheTime is how long for browser to cache this page in seconds
	// . a cacheTime of -2 means do not cache at all
	// . a cacheTime of -1 means do not cache when moving forward,
	//   but when hitting the back button, serve cached results
	// . a cache time of 0 means use local caching rules
	// . any other cacheTime is an explicit time to cache the page for
	// . httpStatus of -1 means to auto determine
	void makeMime   ( int32_t    totalContentLen        , 
			  int32_t    cacheTime        =-1   , // -1-->noBackCache
			  time_t  lastModified     = 0   ,
			  int32_t    offset           = 0   , 
			  int32_t    bytesToSend      =-1   ,
			  char   *ext              = NULL,
			  bool    POSTReply        = false,
			  char   *contentType      = NULL ,
			  char   *charset          = NULL ,
			  int32_t    httpStatus       = -1   ,
			  char   *cookie           = NULL );

	// make a redirect mime
	void makeRedirMime ( char *redirUrl , int32_t redirUrlLen );

	bool addCookiesIntoBuffer ( class SafeBuf *sb ) ;

	char *getMime    ( ) { return m_buf; };
	// does this include the last \r\n\r\n? yes!
	int32_t  getMimeLen ( ) { return m_bufLen; };
	int32_t  getBoundaryLen ( ) { return m_boundaryLen; };

	char *getCharset    ( ) { return m_charset   ; };
	int32_t  getCharsetLen ( ) { return m_charsetLen; };

	int32_t  getContentEncoding () {return m_contentEncoding;}
	char *getContentEncodingPos() {return m_contentEncodingPos;}
	char *getContentLengthPos()      {return m_contentLengthPos;}
	char *getContentTypePos()      {return m_contentTypePos;}


	// private:

	// . sets m_status, m_contentLen , ...
	// . we need "url" to set m_locUrl if it's a relative redirect
	bool parse ( char *mime , int32_t mimeLen , Url *url );

	// converts a string contentType like "text/html" to a int32_t
	int32_t   getContentTypePrivate ( char *s ) ;

	// convert a file extension like "gif" to "images/gif"
	const char *getContentTypeFromExtension ( char *ext ) ;
	const char *getContentTypeFromExtension ( char *ext , int32_t elen ) ;

	// used for bz2, gz files
	const char *getContentEncodingFromExtension ( char *ext ) ;


	// these are set by calling set() above
	int32_t    m_status;
	char   *m_content;
	int32_t    m_contentLen;
	time_t  m_lastModifiedDate;
	int32_t    m_contentType;
	Url     m_locUrl;

	char *m_locationField    ;
	int32_t  m_locationFieldLen ;

	char *m_mimeStartPtr;

	// buf used to hold a mime we create
	char m_buf[1024];
	int32_t m_bufLen;


	int32_t    m_contentEncoding;
	char   *m_contentEncodingPos;
	char   *m_contentLengthPos;
	char   *m_contentTypePos;

	// the size of the terminating boundary, either 1 or 2 bytes.
	// just the last \n in the case of a \n\n or \r in the case
	// of a \r\r, but it is the full \r\n in the case of a last \r\n\r\n
	int32_t m_boundaryLen;

	// Content-Type: text/html;charset=euc-jp  // japanese (euc-jp)
	// Content-Type: text/html;charset=gb2312  // chinese (gb2312)
	char *m_charset;
	int32_t  m_charsetLen;

	char *m_firstCookie;

	char *m_cookie;
	int32_t  m_cookieLen;
};

#endif
