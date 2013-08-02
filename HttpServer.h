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

//this is for low priority requests which come in while we are
//in a quickpoll
#define MAX_REQUEST_QUEUE 128
struct QueuedRequest {
	HttpRequest  m_r;
	TcpSocket   *m_s;
	long         m_page;
};

typedef void (*tcp_callback_t)(void *, TcpSocket *);
long getMsgSize ( char *buf , long bufSize , TcpSocket *s );

bool sendPageAddEvent ( TcpSocket *s , HttpRequest *r );

class HttpServer {

 public:

	// reset the tcp server
	void reset();

	// returns false if initialization was unsuccessful
	bool init ( short port,
		    short sslPort ,
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
		      long    ip       ,
		      long    offset   ,
		      long    size     ,
		      time_t  ifModifiedSince ,
		      void   *state    ,
		      void   (* callback) ( void *state , TcpSocket *s ) ,
		      long    timeout  , // 60*1000 
		      long    proxyIp  ,
		      short   proxyPort,
		      long    maxTextDocLen  ,
		      long    maxOtherDocLen ,
		      char   *userAgent = NULL ,
		      //bool    respectDownloadLimit = false ,
		      // . say HTTP/1.1 instead of 1.0 so we can communicate
		      //   with room alert...
		      // . we do not support 1.1 that is why you should always
		      //   use 1.0
		      char   *proto = "HTTP/1.0" ,
		      bool    doPost = false ,
		      char   *cookie = NULL );

	bool getDoc ( long ip,
		      long port,
		      char *request,
		      long requestLen,
		      void   *state    ,
		      void   (* callback)( void *state , TcpSocket *s ) ,
		      long    timeout  ,
		      long    maxTextDocLen  ,
		      long    maxOtherDocLen );
		      //bool    respectDownloadLimit = false );

	bool gotDoc ( long n , TcpSocket *s );

	// just make a request with size set to 0 and it'll do a HEAD request
	/*
	bool getMime ( char  *url       ,
		       long   timeout   ,
		       long   proxyIp   ,
		       short  proxyPort ,
		       void  *state     ,
		       void  (* callback) ( void *state , TcpSocket *s )) {
		return getDoc (url,0,0,0,state,callback,
			       timeout,proxyIp,proxyPort,-1,-1); };
	*/

	// . this is public so requestHandlerWrapper() can call it
	// . if it returns false "s" will be destroyed w/o a reply
	void requestHandler ( TcpSocket *s );

	// send an error reply, like "HTTP/1.1 404 Not Found"
	bool sendErrorReply ( TcpSocket *s , long error , char *errmsg ,
			      long *bytesSent = NULL ); 
	// send a "prettier" error reply, formatted in XML if necessary
	bool sendQueryErrorReply ( TcpSocket *s , long error , char *errmsg,
				   long rawFormat, int errnum, 
				   char *content=NULL); 
	

	// these are for stopping annoying seo bots
	void getKey ( long *key, char *kname, 
		      char *q , long qlen , long now , long s , long n ) ;
	void getKeys ( long *key1, long *key2, char *kname1, char *kname2,
		       char *q , long qlen , long now , long s , long n ) ;
	bool hasPermission ( long ip , HttpRequest *r , 
			     char *q , long qlen , long s , long n ) ;

	// . used by the HttpPageX.h classes after making their dynamic content
	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . a cacheTime of -2 means browser should not cache when user
	//   is clicking forward or hitting back button OR anytime -- no cache!
	// . a cacheTime of -1 means browser should not cache when user
	//   is clicking forward, but caching when clicking back button is ok
	// . a cacheTime of  0 tells browser to use local caching rules
	bool sendDynamicPage  ( TcpSocket *s , char *page , long pageLen ,
				long cacheTime = -1 , bool POSTReply = false ,
				char *contentType = NULL,
				long httpStatus = -1,
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

	long m_maxOpenSockets;

	//for content-encoding: gzip, we unzip the reply and edit the
	//header to reflect the new size and encoding 
	TcpSocket *unzipReply(TcpSocket* s);
	
	float getCompressionRatio()
	{return (float)m_uncompressedBytes/m_bytesDownloaded;}



	//this is for low priority requests which come in while we are
	//in a quickpoll
	bool addToQueue(TcpSocket *s, HttpRequest *r, long page);
	bool callQueuedPages();



	// private:

	// like above but you supply the ip
	bool sendRequest ( long   ip       ,
			   short  port     ,
			   char  *request  ,
			   void  *state    ,
			   void (* callback) ( void *state , TcpSocket *s ));

	// go ahead and start sending the file ("path") over the socket
	bool sendReply ( TcpSocket *s , HttpRequest *r , bool isAdmin);

	bool sendReply2 ( char *mime, 
			  long  mimeLen ,
			  char *content  ,
			  long  contentLen ,
			  TcpSocket *s ,
			  bool alreadyCompressed = false ,
			  HttpRequest *hr = NULL) ;

	void *states[MAX_DOWNLOADS];
	tcp_callback_t callbacks[MAX_DOWNLOADS];

	long m_bytesDownloaded;
	long m_uncompressedBytes;

	//QueuedRequest m_requestQueue[MAX_REQUEST_QUEUE];
	//long          m_lastSlotUsed;

};

extern class HttpServer g_httpServer;

#endif


