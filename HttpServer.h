// Copyright Matt Wells Nov 2000

// . derived from TcpServer
// . fill in our own getMsgSize () -- looks for Content-Length:xxx
// . fill in our own getMsgPiece() -- looks on disk
// . fill in our own putMsgPiece() -- ??? for spidering big files!

// . all the shit is just a generic non-blocking i/o system
// . move data from one file/mem to another file/mem that might be remote
// 

//TODO: handle SIG_PIPEs!! use sigaction() ...

//TODO: first packet should have some file in it, not just MIME hdr (avoid TCP delayed ACKS)

// TODO: what's TCP_CORK??? it delays sending a packet until it's full
//       which improves performance quite a bit. unsetting TCP_CORK flushes it.
// TODO: investigate sendfile() (copies data between file descriptors)

#ifndef _HTTPSERVER_H_
#define _HTTPSERVER_H_

//#define BGCOLOR "89e3A9" // green
#define BGCOLOR "ffffff" // white
//#define BGCOLOR "d0cfc0" // gray
//#define BGCOLOR "d0d0d9"   // blue gray
//#define BGCOLOR "d0cfd0" // gray
//#define BGCOLOR "d6ced6" // bluish gray
#define MAX_DOWNLOADS (MAX_TCP_SOCKS-50)

#include "TcpServer.h"
#include "Url.h"
#include "HttpRequest.h"          // for parsing/forming HTTP requests
#include "HttpMime.h"

#define DEFAULT_HTTP_PROTO "HTTP/1.0"
// prevent HTTP STATUS 206
// not acceptable response by using 1.1
// instead of 1.0 for www.mindanews.com.
// keep-alive is controlled by the client/spider so should be ok
// to not support it.
// MDW: crap, we don't support chunked transfer encoding so until we do
// we have to use 1.0
// Transfer-Encoding: chunked\r\n
//#define DEFAULT_SPIDER_HTTP_PROTO "HTTP/1.1"
#define DEFAULT_SPIDER_HTTP_PROTO "HTTP/1.0"

//this is for low priority requests which come in while we are
//in a quickpoll
#define MAX_REQUEST_QUEUE 128
struct QueuedRequest {
	HttpRequest  m_r;
	TcpSocket   *m_s;
	int32_t         m_page;
};

typedef void (*tcp_callback_t)(void *, TcpSocket *);
int32_t getMsgSize ( char *buf , int32_t bufSize , TcpSocket *s );

bool sendPageAddEvent ( TcpSocket *s , HttpRequest *r );

class HttpServer {

 public:

	// reset the tcp server
	void reset();

	// returns false if initialization was unsuccessful
	bool init ( int16_t port,
		    int16_t sslPort ,
		    void handlerWrapper ( TcpSocket *s) = NULL);

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . supports partial gets with "offset" and "size"
	// . IMPORTANT: we free read/send bufs of TcpSocket after callback
	// . IMPORTANT: if you don't like this set s->m_read/sendBuf to NULL
	//              in your callback function
	// . NOTE: this should always block unless errno is set
	// . the TcpSocket's callbackData is a file ptr
	// . replies MUST fit in memory (we have NOT implemented putMsgPiece())
	// . uses the HTTP partial GET command if size is > 0
	// . uses regular GET if size is -1
	// . otherwise uses the HTTP HEAD command
	// . the document will be in the s->m_readBuf/s->m_bytesRead of "s"
	// . use Mime class to help parse the readBuf
	// . timeout is in milliseconds since last read OR write
	// . this now ensures that the read content is NULL terminated!
	bool getDoc ( char   *url      , // Url    *url      ,
		      int32_t    ip       ,
		      int32_t    offset   ,
		      int32_t    size     ,
		      time_t  ifModifiedSince ,
		      void   *state    ,
		      void   (* callback) ( void *state , TcpSocket *s ) ,
		      int32_t    timeout  , // 60*1000 
		      int32_t    proxyIp  ,
		      int16_t   proxyPort,
		      int32_t    maxTextDocLen  ,
		      int32_t    maxOtherDocLen ,
		      char   *userAgent = NULL ,
		      //bool    respectDownloadLimit = false ,
		      // . say HTTP/1.1 instead of 1.0 so we can communicate
		      //   with room alert...
		      // . we do not support 1.1 that is why you should always
		      //   use 1.0
		      char   *proto = DEFAULT_HTTP_PROTO , // "HTTP/1.0" ,
		      bool    doPost = false ,
		      char   *cookie = NULL ,
		      char *additionalHeader = NULL , // does not include \r\n
		      // specify your own mime and post data here...
		      char *fullRequest = NULL ,
		      char *postContent = NULL ,
		      char *proxyUsernamePwdAuth = NULL );

	bool getDoc ( int32_t ip,
		      int32_t port,
		      char *request,
		      int32_t requestLen,
		      void   *state    ,
		      void   (* callback)( void *state , TcpSocket *s ) ,
		      int32_t    timeout  ,
		      int32_t    maxTextDocLen  ,
		      int32_t    maxOtherDocLen );
		      //bool    respectDownloadLimit = false );

	bool gotDoc ( int32_t n , TcpSocket *s );

	// just make a request with size set to 0 and it'll do a HEAD request
	/*
	bool getMime ( char  *url       ,
		       int32_t   timeout   ,
		       int32_t   proxyIp   ,
		       int16_t  proxyPort ,
		       void  *state     ,
		       void  (* callback) ( void *state , TcpSocket *s )) {
		return getDoc (url,0,0,0,state,callback,
			       timeout,proxyIp,proxyPort,-1,-1); };
	*/

	// . this is public so requestHandlerWrapper() can call it
	// . if it returns false "s" will be destroyed w/o a reply
	void requestHandler ( TcpSocket *s );

	// send an error reply, like "HTTP/1.1 404 Not Found"
	bool sendErrorReply ( TcpSocket *s , int32_t error , char *errmsg ,
			      int32_t *bytesSent = NULL ); 
	bool sendErrorReply ( class GigablastRequest *gr );
	// xml and json uses this
	bool sendSuccessReply ( class GigablastRequest *gr,char *addMsg=NULL);
	bool sendSuccessReply (TcpSocket *s , char format , char *addMsg=NULL);
	// send a "prettier" error reply, formatted in XML if necessary
	bool sendQueryErrorReply ( TcpSocket *s , int32_t error , char *errmsg,
				   // FORMAT_HTML=0,FORMAT_XML,FORMAT_JSON
				   char format, int errnum, 
				   char *content=NULL); 
	

	// these are for stopping annoying seo bots
	void getKey ( int32_t *key, char *kname, 
		      char *q , int32_t qlen , int32_t now , int32_t s , int32_t n ) ;
	void getKeys ( int32_t *key1, int32_t *key2, char *kname1, char *kname2,
		       char *q , int32_t qlen , int32_t now , int32_t s , int32_t n ) ;
	bool hasPermission ( int32_t ip , HttpRequest *r , 
			     char *q , int32_t qlen , int32_t s , int32_t n ) ;

	// . used by the HttpPageX.h classes after making their dynamic content
	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . a cacheTime of -2 means browser should not cache when user
	//   is clicking forward or hitting back button OR anytime -- no cache!
	// . a cacheTime of -1 means browser should not cache when user
	//   is clicking forward, but caching when clicking back button is ok
	// . a cacheTime of  0 tells browser to use local caching rules
	bool sendDynamicPage  ( TcpSocket *s , char *page , int32_t pageLen ,
				int32_t cacheTime = -1 , bool POSTReply = false ,
				char *contentType = NULL,
				int32_t httpStatus = -1,
				char *cookie = NULL,
				char *charset = NULL ,
				HttpRequest *hr = NULL );

	// for PageSockets
	TcpServer *getTcp()    { return &m_tcp; };
	TcpServer *getSSLTcp() { return &m_ssltcp; };

	// we contain our own tcp server
	TcpServer m_tcp;
	TcpServer m_ssltcp;

	// cancel the transaction that had this state
	void cancel ( void *state ) {
		//void (*callback)(void *state, TcpSocket *s) ) {
		m_tcp.cancel ( state );//, callback );
	};

	int32_t m_maxOpenSockets;

	//for content-encoding: gzip, we unzip the reply and edit the
	//header to reflect the new size and encoding 
	TcpSocket *unzipReply(TcpSocket* s);
	
	float getCompressionRatio() {
		if ( m_bytesDownloaded )
			return (float)m_uncompressedBytes/m_bytesDownloaded;
		else
			return 0.0;
	};

	//this is for low priority requests which come in while we are
	//in a quickpoll
	bool addToQueue(TcpSocket *s, HttpRequest *r, int32_t page);
	bool callQueuedPages();

	bool processSquidProxyRequest ( TcpSocket *sock, HttpRequest *hr);

	// private:

	// like above but you supply the ip
	bool sendRequest ( int32_t   ip       ,
			   int16_t  port     ,
			   char  *request  ,
			   void  *state    ,
			   void (* callback) ( void *state , TcpSocket *s ));

	// go ahead and start sending the file ("path") over the socket
	bool sendReply ( TcpSocket *s , HttpRequest *r , bool isAdmin);

	bool sendReply2 ( char *mime, 
			  int32_t  mimeLen ,
			  char *content  ,
			  int32_t  contentLen ,
			  TcpSocket *s ,
			  bool alreadyCompressed = false ,
			  HttpRequest *hr = NULL) ;

	void *states[MAX_DOWNLOADS];
	tcp_callback_t callbacks[MAX_DOWNLOADS];

	int64_t m_bytesDownloaded;
	int64_t m_uncompressedBytes;

	//QueuedRequest m_requestQueue[MAX_REQUEST_QUEUE];
	//int32_t          m_lastSlotUsed;

};

extern class HttpServer g_httpServer;

#endif


