// Matt Wells, copyright Sep 2001

// . class to parse and form HTTP requests

#ifndef _HTTPREQUEST_H_
#define _HTTPREQUEST_H_

// . allow for up to 256 cgi fields
// . this was stopping us from having more than about 253 banned ips, so i
//   raised it to 600
//#define MAX_CGI_PARMS 600
// . new prioirty controls has 128 rows!!
#define MAX_CGI_PARMS 1400

// for getting a file from http server
#define MAX_HTTP_FILENAME_LEN 1024

// i raised this from 1.3k to 5.3k so we can log the full request better
//#define MAX_REQ_LEN (1024*5+300)
//#define MAX_REQ_LEN (8024*5+300)

// keep it small now that we use m_reqBuf
//#define MAX_REQ_LEN (1024)

#include "SafeBuf.h"
#include "Mem.h"       // mdup
#include "Url.h"       // Url class
#include "TcpSocket.h"

class HttpRequest {

 public:

	// . form an HTTP request 
	// . use size 0 for HEAD requests
	// . use size -1 for GET whole doc requests
	// . fill in your own offset/size for partial GET requests
	// . returns false and sets errno on error
	bool set ( char *url , long offset = 0 , long size = -1 ,
		   time_t ifModifiedSince = 0 , char *userAgent = NULL ,
		   char *proto = "HTTP/1.0" ,
		   bool doPost = false ,
		   char *cookie = NULL );

	// use this
	SafeBuf m_reqBuf;
	bool    m_reqBufValid;

	// get the request length
	long getRequestLen() { return m_reqBuf.length(); };//m_bufLen; };

	// . get the outgoing request we made by calling set() above
	// . OR get the first line of an incoming request
	char *getRequest  () { 
		if ( m_reqBufValid ) return m_reqBuf.getBufStart();
		else return NULL;
		//return m_buf;
	};

	// get the referer field of the MIME header
	char *getReferer () { return m_ref; };

	// this is NULL terminated too
	char *getUserAgent () { return m_userAgent; };

	// just does a simply memcpy() operation, since it should be pointing
	// into the TcpSocket's buffer which is safe until after reply is sent
	// . returns false and sets g_errno on error, true otherwise
	bool copy ( class HttpRequest *r ) ;

	// . the url being reuqested
	// . removes &code= facebook cruft
	bool getCurrentUrl ( SafeBuf &cu );
	bool getCurrentUrlPath ( SafeBuf &cup );

	// . parse an incoming request
	// . returns false and set errno on error
	// . may alloc mem for m_cgiBuf to hold cgi vars from GET or POST op
	bool set ( char *req , long reqSize , TcpSocket *s );

	// is it this type of request?
	bool isGETRequest  () { return (m_requestType == 0); };
	bool isHEADRequest () { return (m_requestType == 1); };
	bool isPOSTRequest () { return (m_requestType == 2); };

	char *getFilename    () { return m_filename; };
	long  getFilenameLen () { return m_filenameLen; };
	long  getFileOffset  () { return m_fileOffset; };
	long  getFileSize    () { return m_fileSize; };

	char *getHost        () { return m_host;    };
	long  getHostLen     () { return m_hostLen; };
	//bool  isLocal        () { return m_isLocal; };
	//bool  isAdmin        () { return m_isAdmin; };
	bool  isLocal        () { return m_isLocal; };

	// . the &ucontent= cgi var does not get its value decoded
	//   because it's already decoded
	// . this is so Mark doesn't have to url encode his injected content
	char *getUnencodedContent    ( ) { return m_ucontent; };
	long  getUnencodedContentLen ( ) { return m_ucontentLen; };
	
	// . for parsing the terms in a cgi url
	// . the returned string is NOT NULL terminated
	char      *getString   ( char *field, long *len = NULL,
				 char *defaultString = NULL , long *next=NULL);
	bool       getBool     ( char *field, bool defaultBool );
	long       getLong     ( char *field, long defaultLong           );
	long long  getLongLong ( char *field, long long defaultLongLong    );
	float      getFloat    ( char *field, double defaultFloat );
	double     getDouble   ( char *field, double defaultDouble );

	float      getFloatFromCookie    ( char *field, float def );
	long       getLongFromCookie    ( char *field, long def );
	long long  getLongLongFromCookie( char *field, long long def );
	bool       getBoolFromCookie    ( char *field, bool def );
	char      *getStringFromCookie  ( char *field, long *len = NULL,
					  char *defaultString = NULL , 
					  long *next=NULL);
	
	// are we a redir? if so return non-NULL
	char      *getRedir    ( ) { return m_redir;    };
	long       getRedirLen ( ) { return m_redirLen; };

	HttpRequest();
	HttpRequest( const HttpRequest &a );
	~HttpRequest();
	void reset();

	char *getPath    ( ) { return m_path; };
	long  getPathLen ( ) { return m_plen; };

	bool isMSIE ( ) { return m_isMSIE; };

	// private:

	// . get value of cgi "field" term in the requested filename
	// . you know GET /myfile.html?q=123&name=nathaniel
	char *getValue ( char *field , long *len=NULL, long *next=NULL) ;

	// get value of the ith field
	char *getValue ( long i, long *len = NULL);

	// get the ith cgi parameter name, return NULL if none
	long  getNumFields   ( ) { return m_numFields; };
	char *getField    ( long i ) {
		if ( i >= m_numFields ) return NULL; return m_fields[i]; };
	long  getFieldLen ( long i ) {
		if ( i >= m_numFields ) return 0   ; return m_fieldLens[i]; };

	// . s is a cgi string
	// . either the stuff after the '?' in a url
	// . or the content in a POST operation
	// . returns false and sets errno on error
	bool addCgi ( char *s , long slen );

	// . parse cgi field terms into m_fields,m_fieldLens,m_fieldValues
	// . "s" should point to cgi string right after the '?' if it exists
	// . s should have had all it's &'s replaced with /0's
	// . slen should include the last \0
	void parseFields ( char *s , long slen ) ;
	void parseFieldsMultipart ( char *s , long slen ) ;
	void addExtraParms(char *s, long slen);
	// . decodes "s/slen" and stores into "dest"
	// . returns the number of bytes stored into "dest"
	// . converts %3A, %2F, etc to their appropriate chars
	long decode ( char *dest , char *s , long slen );

	// 0 for GET, 1 for HEAD
	char  m_requestType;

	// we decode the filename into this buffer (no cgi)
	char  m_filename[MAX_HTTP_FILENAME_LEN];
	long  m_filenameLen;  // excludes ?cgistuff

	// virtual host in the Host: field of the mime
	char  m_host[256];
	long  m_hostLen;

	// are we coming from a local machine? 
	bool  m_isLocal;

	// is it the microsoft internet explorer browser?
	bool m_isMSIE;

	// does the connecting machine have admin privledges?
	//bool  m_isAdmin;

	// . decoded cgi data stored here 
	// . this just points into TcpSocket::m_readBuf
	// . now it points into m_reqBuf.m_buf[]
	char *m_cgiBuf       ;
	long  m_cgiBufLen    ;
	long  m_cgiBufMaxLen ;

	// partial GET file read info
	long  m_fileOffset;
	long  m_fileSize;

	// we use this buf to make requests from a url and to hold incoming
	// requests
	//char  m_buf[MAX_REQ_LEN];
	//long  m_bufLen;

	// . cgi field term info stored in here
	// . set by parseFields()
	char *m_fields      [ MAX_CGI_PARMS ];
	long  m_fieldLens   [ MAX_CGI_PARMS ];
	char *m_fieldValues [ MAX_CGI_PARMS ];
	long  m_numFields;
	//long  getNumCgiParms ( ) { return m_numFields; };
	//char *getCgiParm     ( long i , long *len ) { 
	//	*len = m_fieldLens[i]; return m_fields[i]; };
	//char *getCgiValue    ( long i ) { return m_fieldValues[i]; };

	long m_userIP;
	bool m_isSSL;

	// . ptr to the thing we're getting in the request
	// . used by PageAddUrl4.cpp
	char *m_path;
	long  m_plen;

	char  m_redir[128];
	long  m_redirLen;

	// referer, NULL terminated, from Referer: field in MIME
	char  m_ref [ 256 ];
	long  m_refLen;

	// NULL terminated User-Agent: field in MIME
	char  m_userAgent[128];

	// this points into m_cgiBuf
	char *m_ucontent;
	long  m_ucontentLen;

	// buffer for the cookie
	//char  m_cookieBuf[1024];
	//long  m_cookieBufLen;
	char *m_cookiePtr;
	long  m_cookieLen;

	char *m_metaCookie;

	// buffer for adding extra parms
	char *m_cgiBuf2;
	long  m_cgiBuf2Size;
};

#endif
