// Matt Wells, copyright Jun 2000

// . class to parse an html MIME header

#ifndef _HTTPMIME_H_
#define _HTTPMIME_H_

#include <time.h>

void   getTime    ( char *s , int *sec , int *min , int *hour ) ;
long   getMonth   ( char *s ) ;
long   getWeekday ( char *s ) ;
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
	bool set ( char *httpReply , long replyLen , Url *url );

	// these 2 sets are used to dress up a fake mime for passing
	// to TitleRec::set() called from Msg16 getDoc() routine
	void setLastModifiedDate ( time_t date ) { m_lastModifiedDate = date;};
	void setContentType      ( long   t    ) { m_contentType      = t; };
	void setHttpStatus       ( long status ) { m_status        = status; };

	// http status: 404, 200, etc.
	long   getHttpStatus      () { return m_status;           };
	char  *getContent         () { return m_content; };
	long   getContentLen      () { return m_contentLen;       };
	time_t getLastModifiedDate() { return m_lastModifiedDate; };
	long   getContentType     () { return m_contentType;      };
	bool   isEmpty            () { return ( m_status == -1); };
	Url   *getLocationUrl     () { return &m_locUrl;    };
	char  *getCookie          () { return m_cookie; };
	long   getCookieLen       () { return m_cookieLen; };

	// new stuff for Msg13.cpp to use
	char *getLocationField    () { return m_locationField; }
	long  getLocationFieldLen () { return m_locationFieldLen; }

	// compute length of a possible mime starting at "buf"
	long getMimeLen ( char *buf , long bufLen , long *boundaryLen ) ;

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
	void makeMime   ( long    totalContentLen        , 
			  long    cacheTime        =-1   , // -1-->noBackCache
			  time_t  lastModified     = 0   ,
			  long    offset           = 0   , 
			  long    bytesToSend      =-1   ,
			  char   *ext              = NULL,
			  bool    POSTReply        = false,
			  char   *contentType      = NULL ,
			  char   *charset          = NULL ,
			  long    httpStatus       = -1   ,
			  char   *cookie           = NULL );

	// make a redirect mime
	void makeRedirMime ( char *redirUrl , long redirUrlLen );

	char *getMime    ( ) { return m_buf; };
	// does this include the last \r\n\r\n? yes!
	long  getMimeLen ( ) { return m_bufLen; };
	long  getBoundaryLen ( ) { return m_boundaryLen; };

	char *getCharset    ( ) { return m_charset   ; };
	long  getCharsetLen ( ) { return m_charsetLen; };

	long  getContentEncoding () {return m_contentEncoding;}
	char *getContentEncodingPos() {return m_contentEncodingPos;}
	char *getContentLengthPos()      {return m_contentLengthPos;}


	// private:

	// . sets m_status, m_contentLen , ...
	// . we need "url" to set m_locUrl if it's a relative redirect
	bool parse ( char *mime , long mimeLen , Url *url );

	// converts a string contentType like "text/html" to a long
	long   getContentTypePrivate ( char *s ) ;

	// convert a file extension like "gif" to "images/gif"
	const char *getContentTypeFromExtension ( char *ext ) ;

	// used for bz2, gz files
	const char *getContentEncodingFromExtension ( char *ext ) ;


	// these are set by calling set() above
	long    m_status;
	char   *m_content;
	long    m_contentLen;
	time_t  m_lastModifiedDate;
	long    m_contentType;
	Url     m_locUrl;

	char *m_locationField    ;
	long  m_locationFieldLen ;

	
	// buf used to hold a mime we create
	char m_buf[1024];
	long m_bufLen;


	long    m_contentEncoding;
	char   *m_contentEncodingPos;
	char   *m_contentLengthPos;

	// the size of the terminating boundary, either 1 or 2 bytes.
	// just the last \n in the case of a \n\n or \r in the case
	// of a \r\r, but it is the full \r\n in the case of a last \r\n\r\n
	long m_boundaryLen;

	// Content-Type: text/html;charset=euc-jp  // japanese (euc-jp)
	// Content-Type: text/html;charset=gb2312  // chinese (gb2312)
	char *m_charset;
	long  m_charsetLen;

	char *m_cookie;
	long  m_cookieLen;
};

#endif
