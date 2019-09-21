#include "gb-include.h"

#include "HttpServer.h"
#include "Pages.h"
#include "Collectiondb.h"
#include "HashTable.h"
#include "Stats.h"
#include "Users.h"
#include "XmlDoc.h" // gbzip
#include "UdpServer.h"
#include "Proxy.h"
#include "PageCrawlBot.h"

// a global class extern'd in .h file
HttpServer g_httpServer;

// this is defined in PageEvents.cpp
//bool sendPageSiteMap ( TcpSocket *s , HttpRequest *r ) ;
//bool sendPageApi ( TcpSocket *s , HttpRequest *r ) ;
bool sendPageAnalyze ( TcpSocket *s , HttpRequest *r ) ;
bool sendPagePretty ( TcpSocket *s , HttpRequest *r , char *filename ,
		      char *tabName ) ;

// we get like 100k submissions a day!!!
static HashTable s_htable;
static bool      s_init = false;
static int32_t      s_lastTime = 0;

// declare our C functions
static void requestHandlerWrapper ( TcpSocket *s ) ;
static void cleanUp               ( void *state , TcpSocket *s ) ;
static void getMsgPieceWrapper    ( int fd , void *state /*sockDesc*/ );
static void getSSLMsgPieceWrapper ( int fd , void *state /*sockDesc*/ );
// we now use the socket descriptor as state info for TcpServer instead of
// the TcpSocket ptr in case it got destroyed
static int32_t getMsgPiece           ( TcpSocket *s );
static void gotDocWrapper         ( void *state, TcpSocket *s );
static void handleRequestfd       ( UdpSlot *slot , int32_t niceness ) ;

//bool sendPageAbout ( TcpSocket *s , HttpRequest *r , char *path ) ;

static int32_t s_numOutgoingSockets = 0;

// reset the tcp servers
void HttpServer::reset() {
	m_tcp.reset();
	m_ssltcp.reset();
	s_htable.reset();
	s_numOutgoingSockets = 0;
}

bool HttpServer::init ( int16_t port,
			int16_t sslPort,
			void handlerWrapper( TcpSocket *s )) {
	// our mime table that maps a file extension to a content type
	HttpMime mm;
	if ( ! mm.init() ) return false;
	// make it essentially infinite
	//m_maxOpenSockets = 1000000;

	//well, not infinite
	//m_maxOpenSockets = g_conf.m_httpMaxSockets;
	
	m_uncompressedBytes = m_bytesDownloaded = 1;

	//if we haven't been given the handlerwrapper, use default
	//used only by proxy right now
	// qatest sets up a client-only httpserver, so don't set a 
	// handlerWrapper if no listening port
	if (!handlerWrapper && (port || sslPort))
		handlerWrapper = requestHandlerWrapper;

	if ( ! g_udpServer.registerHandler ( 0xfd , handleRequestfd ) )
		return false;

	// set our base TcpServer class
	if ( ! m_tcp.init( *handlerWrapper       ,
			   getMsgSize                  ,
			   getMsgPiece                 ,
			   port                        ,
			   //&g_conf.m_httpMaxSockets     ) ) return false;
			   &g_conf.m_httpMaxSockets  ) ) return false;
	//g_conf.m_httpMaxReadBufSize , 
	//g_conf.m_httpMaxSendBufSize ) ) return false;
	// set our secure TcpServer class
	if ( ! m_ssltcp.init ( handlerWrapper,
			       getMsgSize,
			       getMsgPiece,
			       sslPort,
			       &g_conf.m_httpsMaxSockets,
			       true                    ) ) {
		// this was required for communicating with an email alert
		// web server, but no longer do i use them
		//return false;
		// don't break, just log and don't do SSL
		log ( "https: SSL Server Failed To Init, Continuing..." );
		m_ssltcp.reset();
	}
	// log an innocent msg
	log(LOG_INIT,"http: Listening on TCP port %i with sd=%i", 
	    port, m_tcp.m_sock );
	// log for https
	if (m_ssltcp.m_ready)
		log(LOG_INIT,"https: Listening on TCP port %i with sd=%i", 
	    	    sslPort, m_ssltcp.m_sock );

	return true;
}

// . get a url's document
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . IMPORTANT: we free doc/docLen, so NULLify s->m_readBuf if you want it
// . IMPORTANT: same goes for s->m_sendBuf
// . timeout in milliseconds since no read OR no write
// . proxyIp is 0 if we don't have one
bool HttpServer::getDoc ( char   *url      ,
			  int32_t    ip       ,
			  int32_t    offset   ,
			  int32_t    size     ,
			  time_t  ifModifiedSince ,
			  void   *state    ,
			  void   (* callback)( void *state , TcpSocket *s ) ,
			  int32_t    timeout  ,
			  int32_t    proxyIp  ,
			  int16_t   proxyPort,
			  int32_t    maxTextDocLen  ,
			  int32_t    maxOtherDocLen ,
			  char   *userAgent ,
			  //bool    respectDownloadLimit ,
			  char    *proto ,
			  bool     doPost ,
			  char    *cookie ,
			  char    *additionalHeader ,
			  char    *fullRequest ,
			  char    *postContent ,
			  char    *proxyUsernamePwdAuth ) { 
	// sanity
	if ( ip == -1 ) 
		log("http: you probably didn't mean to set ip=-1 did you? "
		    "try setting to 0.");

	// ignore if -1 as well
	if ( proxyIp == -1 ) proxyIp = 0;

	//log(LOG_WARN, "http: get doc %s", url->getUrl());
	// use the HttpRequest class
	HttpRequest r;
	// set default port
	int32_t defPort = 80;
	// check for a secured site
	TcpServer *tcp = &m_tcp;
	bool urlIsHttps = false;
	if ( url && strncasecmp(url, "https://", 8) == 0 ) {
		urlIsHttps = true;
		defPort = 443;
	}

	// if we are using gigablast as a squid proxy then the
	// "fullRequest" and the url will be like "CONNECT foo.com:443 HTT..."
	// and it is an https url, because we only use the CONNECT cmd for
	// downloading https urls over a proxy i think
	char *p = fullRequest;
	if ( p && strncmp(p,"CONNECT ",8)==0 )
		urlIsHttps = true;

	// if going through a proxy do not use the ssl server, it will
	// handle the encryption from itself to the host website. unfortunately
	// then the http requests/responses are unencrypted from the
	// proxy to us here.
	if ( urlIsHttps && ! proxyIp ) {
		if (!m_ssltcp.m_ready) {
			// TODO: set an error here
			log("https: Trying to get HTTPS site when SSL "
			    "TcpServer not ready: %s",url);
			g_errno = ESSLNOTREADY;
			return true;
		}
		tcp = &m_ssltcp;
	}

	int32_t pcLen = 0;
	if ( postContent ) pcLen = gbstrlen(postContent);

	char *req = NULL;
	int32_t reqSize;

	// if downloading an *httpS* url we have to send a 
	// CONNECT www.abc.com:443 HTTP/1.0\r\n\r\n request first
	// and get back a connection established reply, before we can
	// send the actual encrypted http stuff.
	bool useHttpTunnel = ( proxyIp && urlIsHttps );

	int32_t  hostLen ;
	int32_t  port = defPort;
	char *host = NULL;
	if ( ! ip || useHttpTunnel ) 
		host = getHostFast ( url , &hostLen , &port );

	// this returns false and sets g_errno on error
	if ( ! fullRequest ) {
		if ( ! r.set ( url , offset , size , ifModifiedSince ,
			       userAgent , proto , doPost , cookie ,
			       // pass in proxyIp because if it is a
			       // request being sent to a proxy we have to
			       // say "GET http://www.xyz.com/" the full
			       // url, not just a relative path.
			       additionalHeader , pcLen , proxyIp ,
			       proxyUsernamePwdAuth ) ) {
			log("http: http req error: %s",mstrerror(g_errno));
			// TODO: ensure we close the socket on this error!
			return true;
		}
		if ( g_conf.m_logDebugTcp )
			log("archive: %s",r.m_reqBuf.getBufStart());
		reqSize = r.getRequestLen();
		int32_t need = reqSize + pcLen;
		// if we are requesting an HTTPS url through a proxy then
		// this will prepend a
		// CONNECT www.abc.com:443 HTTP/1.0\r\n\r\n
		// CONNECT www.gigablast.com:443 HTTP/1.0\r\n\r\n
		// to the actual mime and we have to detect that
		// below and just send that to the proxy. when the proxy
		// sends back "HTTP/1.0 200 Connection established\r\n\r\n"
		// we use ssl_write in tcpserver.cpp to send the original
		// encrypted http request.
		SafeBuf sb;
		if ( useHttpTunnel ) {
			sb.safePrintf("CONNECT ");
			sb.safeMemcpy ( host, hostLen );
			sb.safePrintf(":%"INT32" HTTP/1.0\r\n",port);
			// sb.safePrintf("Host: ");
			// sb.safeMemcpy ( host, hostLen );
			// sb.safePrintf("\r\n");
			// include proxy authentication info now
			if ( proxyUsernamePwdAuth && proxyUsernamePwdAuth[0] ){
				sb.safePrintf("Proxy-Authorization: Basic ");
				sb.base64Encode(proxyUsernamePwdAuth,
						gbstrlen(proxyUsernamePwdAuth)
						);
				sb.safePrintf("\r\n");
			}
			sb.safePrintf("\r\n");
			sb.nullTerm();
			need += sb.length();
		}
		req = (char *) mmalloc( need ,"HttpServer");
		char *p = req;
		if ( req && sb.length() ) {
			gbmemcpy ( p , sb.getBufStart() , sb.length() );
			p += sb.length();
		}
		if ( req ) {
			gbmemcpy ( p , r.getRequest() , reqSize );
			p += reqSize;
		}
		if ( req && pcLen ) {
			gbmemcpy ( p , postContent , pcLen );
			p += pcLen;
		}
		reqSize = p - req;
	}
	else {
		// does not contain \0 i guess
		reqSize = gbstrlen(fullRequest);
		req = (char *) mdup ( fullRequest , reqSize,"HttpServer");
	}

	// . get the request from the static buffer and dup it
	// . return true and set g_errno on error
	if ( ! req ) return true;


	// mdw23
	//if ( g_conf.m_logDebugSpider )
	// {
	// 	SafeBuf tmp;
	// 	tmp.safeMemcpy ( req , reqSize );
	// 	tmp.nullTerm();
	// 	log("spider: httprequest = %s", tmp.getBufStart() );
	// }


	// do we have an ip to send to? assume not
	if ( proxyIp ) { ip = proxyIp ; port = proxyPort; }

	// special NULL case
	if ( !state || !callback ) {
		// . send it away
		// . callback will be called on completion of transaction
		// . be sure to free "req/reqSize" in callback() somewhere
		if ( ip )
			return m_tcp.sendMsg ( ip             ,
				       port           ,
				       req            , 
				       reqSize        ,
				       reqSize        ,
				       reqSize        , // msgTotalSize
				       state          , 
				       callback       ,
				       timeout        ,
				       maxTextDocLen  ,
				       maxOtherDocLen );
		// otherwise pass the hostname
		return m_tcp.sendMsg ( host           ,
				       hostLen        ,
				       port           ,
				       req            , 
				       reqSize        ,
				       reqSize        ,
				       reqSize        , // msgTotalSize
				       state          , 
				       callback       ,
				       timeout        ,
				       maxTextDocLen  ,
				       maxOtherDocLen );
	}
	// if too many downloads already, return error
	//if ( respectDownloadLimit &&
	//     s_numOutgoingSockets >= g_conf.m_httpMaxDownloadSockets ||
	if ( s_numOutgoingSockets >= MAX_DOWNLOADS ) {
		mfree ( req, reqSize, "HttpServer" );
		g_errno = ETOOMANYDOWNLOADS;
		log("http: already have %"INT32" sockets downloading. Sending "
		    "back ETOOMANYDOWNLOADS.",(int32_t)MAX_DOWNLOADS);
		return true;
	}
	// increment usage
	PTRTYPE n = 0;
	while ( states[n] ) {
		n++;
		// should not happen...
		if ( n >= MAX_DOWNLOADS ) {
			mfree ( req, reqSize, "HttpServer" );
			g_errno = ETOOMANYDOWNLOADS;
			log("http: already have %"INT32" sockets downloading",
			    (int32_t)MAX_DOWNLOADS);
			return true;
		}
	}
	states   [n] = state;
	callbacks[n] = callback;
	s_numOutgoingSockets++;
	// debug
	log(LOG_DEBUG,"http: Getting doc with ip=%s state=%"PTRFMT" url=%s.",
	    iptoa(ip),(PTRTYPE)state,url);

	// . send it away
	// . callback will be called on completion of transaction
	// . be sure to free "req/reqSize" in callback() somewhere
	// . if using an http proxy, then ip should be valid here...
	if ( ip ) {
		if ( ! tcp->sendMsg (  ip             ,
				       port           ,
				       req            , 
				       reqSize        ,
				       reqSize        ,
				       reqSize        , // msgTotalSize
				       (void*)n       ,
				       gotDocWrapper  ,
				       timeout        ,
				       maxTextDocLen  ,
				       maxOtherDocLen ,
				       useHttpTunnel  ) )
			return false;
		// otherwise we didn't block
		states[n]    = NULL;
		callbacks[n] = NULL;
		s_numOutgoingSockets--;
		return true;
	}
	// otherwise pass the hostname
	if ( ! tcp->sendMsg (  host           ,
			       hostLen        ,
			       port           ,
			       req            , 
			       reqSize        ,
			       reqSize        ,
			       reqSize        , // msgTotalSize
			       (void*)n       ,
			       gotDocWrapper  ,
			       timeout        ,
			       maxTextDocLen  ,
			       maxOtherDocLen ) )
		return false;
	// otherwise we didn't block
	states[n]    = NULL;
	callbacks[n] = NULL;
	s_numOutgoingSockets--;
	return true;
}

// . get a url's document
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . IMPORTANT: we free doc/docLen, so NULLify s->m_readBuf if you want it
// . IMPORTANT: same goes for s->m_sendBuf
// . timeout in milliseconds since no read OR no write
// . proxyIp is 0 if we don't have one
bool HttpServer::getDoc ( int32_t ip,
			  int32_t port,
			  char *request,
			  int32_t requestLen,
			  void   *state    ,
			  void   (* callback)( void *state , TcpSocket *s ) ,
			  int32_t    timeout  ,
			  int32_t    maxTextDocLen  ,
			  int32_t    maxOtherDocLen ) {
			  //bool    respectDownloadLimit ) {
	TcpServer *tcp = &m_tcp;
	//stupid incoming request has 1024 bytes mostly, while we need to 
	//send exactly what was needed
	int32_t reqSize = gbstrlen ( request );
	char *req    = (char *) mdup ( request, reqSize,"HttpServer" );
	if ( ! req ) return true;

	// if too many downloads already, return error
	if ( s_numOutgoingSockets >= MAX_DOWNLOADS ) {
		mfree ( req, reqSize, "HttpServer" );
		g_errno = ETOOMANYDOWNLOADS;
		log("http: already have %"INT32" sockets downloading",
		    (int32_t)MAX_DOWNLOADS);
		return true;
	}

	// increment usage
	int32_t n = 0;
	while ( states[n] ) {
		n++;
		// should not happen...
		if ( n >= MAX_DOWNLOADS ) {
			mfree ( req, reqSize, "HttpServer" );
			g_errno = ETOOMANYDOWNLOADS;
			log("http: already have %"INT32" sockets downloading",
			    (int32_t)MAX_DOWNLOADS);
			return true;
		}
	}
	states   [n] = state;
	callbacks[n] = callback;
	s_numOutgoingSockets++;
	// debug
	log(LOG_DEBUG,"http: Getting doc with ip=%s state=%"PTRFMT". %s",
	    iptoa(ip),(PTRTYPE)state, req);
	// . send it away
	// . callback will be called on completion of transaction
	// . be sure to free "req/reqSize" in callback() somewhere

	// MDW: THIS IS RETURNING TRUE SOMEHOW w/o setting g_errno

	if ( ! tcp->sendMsg (  ip             ,
			       port           ,
			       req            , 
			       reqSize        ,
			       reqSize        ,
			       reqSize        , // msgTotalSize
			       (void*)(PTRTYPE)n       ,
			       gotDocWrapper  ,
			       timeout        ,
			       maxTextDocLen  ,
			       maxOtherDocLen ) )
		return false;
	// otherwise we didn't block
	states[n]    = NULL;
	callbacks[n] = NULL;
	s_numOutgoingSockets--;
	log("http: sendmsg returned true!: %s",mstrerror(g_errno));
	return true;
}


void gotDocWrapper ( void *state, TcpSocket *s ) {
	g_httpServer.gotDoc ( (int32_t)(PTRTYPE)state, s );
}

bool HttpServer::gotDoc ( int32_t n, TcpSocket *s ) {
	void *state = states[n];
	void (*callback)(void *state, TcpSocket *s) = callbacks[n];
	// debug
	log(LOG_DEBUG,"http: Got doc with state=%"PTRFMT".",(PTRTYPE)state);
	states[n]    = NULL;
	callbacks[n] = NULL;
	s_numOutgoingSockets--;
	//figure out if it came back zipped, unzip if so.
	//if(g_conf.m_gzipDownloads && !g_errno && s->m_readBuf)
	// now wikipedia force gzip on us regardless
	if( !g_errno && s->m_readBuf) {
		// this could set g_errno to EBADMIME or ECORRUPTHTTPGZIP
		s = unzipReply(s);
	}
	// callback 
	callback ( state, s );
	return true;

}

// . handle an incoming HTTP request
void requestHandlerWrapper ( TcpSocket *s ) {
	g_httpServer.requestHandler ( s );
}

// . a udp handler wrapper 
// . the proxy sends us udp packets with msgtype = 0xfd ("forward")
void handleRequestfd ( UdpSlot *slot , int32_t niceness ) {
	// if we are proxy, that is just wrong! a proxy does not send
	// this msg to another proxy, only to the flock
	// no! now a compression proxy will forward a query to a regular
	// proxy so that the search result pages can be compressed to
	// save bandwidth so we can serve APN's queries over lobonet
	// which is only 2Mbps.
	//if ( g_proxy.isCompressionProxy() ) { char *xx=NULL;*xx=0; }
	if ( g_hostdb.m_myHost->m_type==HT_QCPROXY) {char *xx=NULL;*xx=0;}

	// if niceness is 0, use the higher priority udpServer
	//UdpServer *us = &g_udpServer;
	// get the request
	char *request      = slot->m_readBuf;
	int32_t  requestSize  = slot->m_readBufSize;
	int32_t  requestAlloc = slot->m_readBufMaxSize;
	// sanity check, must at least contain \0 and ip (5 bytes total)
	if ( requestSize < 5 ) { char *xx=NULL;*xx=0; }
	// make a fake TcpSocket
	TcpSocket *s = (TcpSocket *)mcalloc(sizeof(TcpSocket),"tcpudp");
	// this sucks
	if ( ! s ) {
		log("http: could not allocate for TcpSocket. Out of memory.");
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}
	// HACK: Proxy.cpp crammed the real ip into the end of the request
	s->m_ip          = *(int32_t *)(request+requestSize-4);
	// callee will free this buffer...
	s->m_readBuf     = request;
	// actually remove the ending \0 as well as 4 byte ip
	s->m_readOffset  = requestSize - 5;
	s->m_readBufSize = requestAlloc;
	s->m_this        = &g_httpServer.m_tcp;
	// HACK: let TcpServer know to send a UDP reply, not a tcp reply!
	s->m_udpSlot     = slot;
	// . let the TcpSocket free that readBuf when we free the TcpSocket,
	//   there in TcpServer.cpp::sendMsg()
	// . PageDirectory.cpp actually realloc() the TcpSocket::m_readBuf
	//   so we have to be careful not to let UdpServer free it in the
	//   udpSlot because it will have been reallocated by PageDirectory.cpp
	slot->m_readBuf = NULL;

	// HACK: this is used as a unique identifier for registering callbacks
	// so let's set the high bit here to avoid conflicting with normal
	// TCP socket descriptors that might be reading the same file! But
	// now we are not allowing proxy to forward regular file requests, 
	// so hopefully, we won't even use this hacked m_sd.
	s->m_sd          = 1234567;//(int32_t)slot | 0x80000000;

	// if we are a proxy receiving a request from a compression proxy
	// then use the proxy handler function
	if ( g_proxy.isProxy() ) {
		// this should call g_httpServer.sendDynamicPage() which
		// should compress the 0xfd reply to be sent back to the
		// compression proxy
		g_proxy.handleRequest ( s );
		return;
	}

	// log this out on gk144 to see why dropping
	if ( g_conf.m_logDebugBuild )
		log("fd: handling request transid=%"INT32" %s", 
		    slot->m_transId, request );

	// ultimately, Tcp::sendMsg() should be called which will free "s"
	g_httpServer.requestHandler ( s );
}

// . if this returns false "s" will be destroyed 
// . if request is not GET or HEAD we send an HTTP 400 error code
// . ALWAYS calls m_tcp.sendMsg ( s ,... )
// . may block on something before calling that however
// . NEVER calls m_tcp.destroySocket ( s )
// . One Exception: sendErrorReply() can call it if cannot form error reply
void HttpServer::requestHandler ( TcpSocket *s ) {
	// debug msg
	//log("got request, readBufUsed=%i",s->m_readOffset);
	// parse the http request
	HttpRequest r;

	// debug
	/*
	unsigned char foo[1024];
	unsigned char *pp = foo;
	pp += sprintf ( (char *)pp,"GET /search?qcs=iso-8859-1&k0c=107207&code=1M9VNT6&spell=1&ns=2&nrt=0&rat=0&sc=1&DR=1&qh=0&bq2&q=");
	//pp += sprintf ( (char *)pp,"GET /search?k0c=107207&code=1M9VNT6&spell=1&ns=2&nrt=0&rat=0&sc=1&DR=1&qh=0&bq2&q=");

	static char ddd[] = {
		0xc3, 0x83, 0xc6, 0x92, 0xc3, 0xa2, 0xe2, 0x80, 0x9e, 0xc2, 
		0xa2, 0xc3, 0x83, 0xc2, 0xa2, 0xc3, 0xa2, 0xe2, 0x80, 0x9a, 
		0xc2, 0xac, 0xc3, 0x82, 0xc2, 0xa6, 0xc3, 0x83, 0xc6, 0x92, 
		0xc3, 0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 0x83, 0xe2, 
		0x80, 0x9a, 0xc3, 0x82, 0xc2, 0x81, 0xc3, 0x83, 0xc6, 0x92, 
		0xc3, 0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 0x83, 0xc2, 
		
		0xa2, 0xc3, 0xa2, 0xe2, 0x80, 0x9a, 0xc2, 0xac, 0xc3, 0x82, 
		0xc2, 0xa1, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0xa2, 0xe2, 0x80, 
		0x9e, 0xc2, 0xa2, 0xc3, 0x83, 0xe2, 0x80, 0xb9, 0xc3, 0xa2, 
		0xe2, 0x82, 0xac, 0xc2, 0xa0, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 
		0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 0x83, 0xc2, 0xa2, 
		0xc3, 0xa2, 0xe2, 0x80, 0x9a, 0xc2, 0xac, 0xc3, 0x82, 0xc2, 
		0xa6, 0x20, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0x8b, 0xc5, 0x93, 
		0xc3, 0x83, 0xe2, 0x80, 0x9a, 0xc3, 0x82, 0xc2, 0xa7, 0xc3, 
		0x83, 0xc6, 0x92, 0xc3, 0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 
		0xc3, 0x83, 0xc2, 0xa2, 0xc3, 0xa2, 0xe2, 0x80, 0x9a, 0xc2, 
		0xac, 0xc3, 0x85, 0xc2, 0xbe, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 
		0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 0x83, 0xc2, 0xa2, 
		0xc3, 0xa2, 0xe2, 0x80, 0x9a, 0xc2, 0xac, 0xc3, 0x82, 0xc2, 
		0xa6, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0xa2, 0xe2, 0x80, 0x9e, 
		0xc2, 0xa2, 0xc3, 0x83, 0xc2, 0xa2, 0xc3, 0xa2, 0xe2, 0x80, 
		0x9a, 0xc2, 0xac, 0xc3, 0x82, 0xc2, 0xa0, 0xc3, 0x83, 0xc6, 
		0x92, 0xc3, 0x8b, 0xc5, 0x93, 0xc3, 0x83, 0xe2, 0x80, 0x9a, 
		0xc3, 0x82, 0xc2, 0xb8, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0xa2, 
		0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 0x83, 0xe2, 0x80, 0xb9, 
		0xc3, 0xa2, 0xe2, 0x82, 0xac, 0xc2, 0xa0, 0xc3, 0x83, 0xc6, 
		0x92, 0xc3, 0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 0x83, 
		0xc2, 0xa2, 0xc3, 0xa2, 0xe2, 0x80, 0x9a, 0xc2, 0xac, 0xc3, 
		0x82, 0xc2, 0xa6, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0x8b, 0xc5, 
		0x93, 0xc3, 0x83, 0xe2, 0x80, 0x9a, 0xc3, 0x82, 0xc2, 0xa9, 
		0x20, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0x8b, 0xc5, 0x93, 0xc3, 
		0x83, 0xe2, 0x80, 0x9a, 0xc3, 0x82, 0xc2, 0xa7, 0xc3, 0x83, 
		0xc6, 0x92, 0xc3, 0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 
		0x83, 0xc2, 0xa2, 0xc3, 0xa2, 0xe2, 0x80, 0x9a, 0xc2, 0xac, 
		0xc3, 0x85, 0xc2, 0xbe, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0x8b, 
		0xc5, 0x93, 0xc3, 0x83, 0xe2, 0x80, 0x9a, 0xc3, 0x82, 0xc2, 
		0xa8, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0xa2, 0xe2, 0x80, 0x9e, 
		0xc2, 0xa2, 0xc3, 0x83, 0xe2, 0x80, 0xa6, 0xc3, 0x82, 0xc2, 
		0xa0, 0xc3, 0x83, 0xc6, 0x92, 0xc3, 0x8b, 0xc5, 0x93, 0xc3, 
		0x83, 0xe2, 0x80, 0x9a, 0xc3, 0x82, 0xc2, 0xa6, 0xc3, 0x83, 
		0xc6, 0x92, 0xc3, 0xa2, 0xe2, 0x80, 0x9e, 0xc2, 0xa2, 0xc3, 
		0x83, 0xe2, 0x80, 0xa6, 0xc3, 0x82, 0xc2, 0xa0, 0xc3, 0x83, 
		0xc6, 0x92, 0xc3, 0x8b, 0xc5, 0x93, 0xc3, 0x83, 0xe2, 0x80, 
		0x9a, 0xc3, 0x82, 0xc2, 0xa9, 0x00, 0x00, 0xda, 0xda, 0xda, 
		0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 
		0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 
		0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0xda, 0x74, 
		0x65, 0x73, 0x2c, 0x20, 0x68, 0x59, 0x00, 0x00, 0x00, 0xac, 
		0xed, 0x3b, 0x09, 0xac, 0xed, 0x3b, 0x09, 0x78, 0x51, 0xa7, 
		0x24, 0xf8, 0xd0, 0xa7, 0x24, 0x00, 0x00, 0x00, 0x00, 0x0a, 
		0x00};

	for ( int32_t i = 0 ; i < 435 ; i++ ) {
		//	again:
		*pp = ddd[i]; // rand() % 256;
		//if ( *pp < 0x80 ) goto again;
		pp++;
	}
	*pp = 0;
	*/

	// . since we own the data, we'll free readBuf on r's destruction
	// . this returns false and sets g_errno on error
	// . but it should still set m_request to the readBuf to delete it
	//   so don't worry about memory leaking s's readBuf
	// . if the request is bad then return an HTTP error code
	// . this should copy the request into it's own local buffer
	// . we now pass "s" so we can get the src ip/port to see if
	//   it's from lenny
	bool status = r.set ( s->m_readBuf , s->m_readOffset , s ) ;

	//bool status = r.set ( (char *)foo , pp - foo , s ) ;
	// is this the admin
	//bool isAdmin       = g_collectiondb.isAdmin ( &r , s );
	// i guess assume MASTER admin...
	//bool isAdmin = g_users.hasPermission ( &r , PAGE_MASTER , s );
	bool isAdmin = r.isLocal();
	// never proxy admin pages for security reasons
	if ( s->m_udpSlot ) isAdmin = false;
	//bool isIpInNetwork = g_hostdb.isIpInNetwork ( s->m_ip );
	// . if host does not allow http requests (except for admin) then bail
	// . used to prevent seo/email spammers from querying other machines
	//   and getting around our seo robot protection
	//if ( ! g_conf.m_httpServerEnabled  ) {
	//&& ! isAdmin  &&
	     // quick hack so we can add ips to the connect ips list in
	     // the master controls security table
	  //   ! g_conf.isConnectIp ( s->m_ip ) ) {
	//	log("query: Returning 403 Forbidden. HttpServer is disabled "
	//	    "in the master controls. ip=%s",iptoa(s->m_ip));
	//	sendErrorReply ( s , 403 , "Forbidden" );
	//	return;
	//}

	// get the server this socket uses
	TcpServer *tcp = s->m_this;
	// get the max sockets that can be opened at any one time
	int32_t max;
	if ( tcp == &m_ssltcp ) max = g_conf.m_httpsMaxSockets;
	else                    max = g_conf.m_httpMaxSockets;
	// just a safety catch
	if ( max < 2 ) max = 2;

	// limit injects to less sockets than the max, so the administrator
	// and regular queries will take precedence
	int32_t imax = max - 10;
	if ( imax < 10 ) imax = max - 1;
	if ( imax <  2 ) imax = 2;
	if ( strncmp ( s->m_readBuf , "GET /inject" , 11 ) == 0 ) {
		// reset "max" to something smaller
		max = imax;
		// do not consider injects to be coming from admin ever
		isAdmin = false;
	}

	// enforce open connections here
	//if ( used >= g_conf.m_httpMaxSockets + 10 ) {
	//	log("query: Too many sockets open for ip=%s. Destroying.",
	//	    iptoa(s->m_ip));
	//	m_tcp.destroySocket ( s ); 
	//	return; 
	//}

	// enforce the open socket quota iff not admin and not from intranet
	if ( ! isAdmin && tcp->m_numIncomingUsed >= max && 
	     // make sure request is not from proxy
	     ! s->m_udpSlot &&
	     !tcp->closeLeastUsed()) {
		static int32_t s_last = 0;
		static int32_t s_count = 0;
		int32_t now = getTimeLocal();
		if ( now - s_last < 5 ) 
			s_count++;
		else {
			log("query: Too many sockets open. Sending 500 "
			    "http status code to %s. (msgslogged=%"INT32")",
			    iptoa(s->m_ip),s_count);
			s_count = 0;
			s_last = now;
		}
		sendErrorReply ( s , 500 , "Too many sockets open."); 
		// count as a failed query so we send an email alert if too
		// many of these happen
		g_stats.m_closedSockets++;
		return; 
	}

	// . read Buf should be freed on s's recycling/destruction in TcpServer
	// . always free the readBuf since TcpServer does not
	// . be sure not to set s->m_readBuf to NULL because it's used by
	//   TcpServer to determine if we're sending/reading a request/reply
	// mfree ( s->m_readBuf , s->m_readBufSize );
	// set status to false if it's not a HEAD or GET request
	//if ( ! r.isGETRequest() && ! r.isHEADRequest() ) status = false;
	// if the HttpRequest was bogus come here
	if ( ! status ) {
		// log a bad request
		log("http: Got bad request from %s: %s",
		    iptoa(s->m_ip),mstrerror(g_errno));
		// cancel the g_errno, we'll send a BAD REQUEST reply to them
		g_errno = 0;
		// . this returns false if blocked, true otherwise
		// . this sets g_errno on error
		// . this will destroy(s) if cannot malloc send buffer
		sendErrorReply ( s , 400, "Bad Request" );
		return;
	}

	// ok, we got an authenticated proxy request
	if ( r.m_isSquidProxyRequest ) {
		processSquidProxyRequest ( s , &r );
		return;
	}

	// log the request iff filename does not end in .gif .jpg .
	char *f     = r.getFilename();
	int32_t  flen  = r.getFilenameLen();
	bool  isGif = ( f && flen >= 4 && strncmp(&f[flen-4],".gif",4) == 0 );
	bool  isJpg = ( f && flen >= 4 && strncmp(&f[flen-4],".jpg",4) == 0 );
	bool  isBmp = ( f && flen >= 4 && strncmp(&f[flen-4],".bmp",4) == 0 );
	bool  isPng = ( f && flen >= 4 && strncmp(&f[flen-4],".png",4) == 0 );
	bool  isIco = ( f && flen >= 4 && strncmp(&f[flen-4],".ico",4) == 0 );
	bool  isPic = (isGif | isJpg | isBmp | isPng || isIco);
	// get time format: 7/23/1971 10:45:32
	// . crap this cores if we use getTimeGlobal() and we are not synced
	//   with host #0, so just use local time i guess in that case
	time_t tt ;
	if ( isClockInSync() ) tt = getTimeGlobal();
	else                   tt = getTimeLocal();
	struct tm *timeStruct = localtime ( &tt );
	char buf[64];
	strftime ( buf , 100 , "%b %d %T", timeStruct);
	// save ip in case "s" gets destroyed
	int32_t ip = s->m_ip;
	// . likewise, set cgi buf up here, too
	// . if it is a post request, log the posted data, too
	/*
	char cgi[20058];
	cgi[0] = '\0';
	if ( r.isPOSTRequest() ) {
		int32_t  plen = r.m_cgiBufLen;
		if (  plen >= 20052 ) plen = 20052;
		char *pp1 = cgi ;
		char *pp2 = r.m_cgiBuf;
		// . when parsing cgi parms, HttpRequest converts the 
		//   &'s to \0's so it can avoid having to malloc a 
		//   separate m_cgiBuf
		// . now it also converts ='s to 0's, so flip flop back
		//   and forth
		char dd = '=';
		for ( int32_t i = 0 ; i < plen ; i++ , pp1++, pp2++ ) {
			if ( *pp2 == '\0' ) { 
				*pp1 = dd;
				if ( dd == '=' ) dd = '&';
				else             dd = '=';
				continue;
			}
			if ( *pp2 == ' ' ) *pp1 = '+';
			else               *pp1 = *pp2;
		}
		if ( r.m_cgiBufLen >= 20052 ) {
			pp1[0]='.'; pp1[1]='.'; pp1[2]='.'; pp1 += 3; }
		*pp1 = '\0';
	}
	*/

	//get this value before we send the reply, because r can be 
	//destroyed when we send.
	int32_t dontLog = r.getLong("dontlog",0);
	// turn off for now
	//dontLog = 0;

	// !!!!
	// TcpServer::sendMsg() may free s->m_readBuf if doing udp forwarding
	// !!!!
	char stackMem[1024];
	int32_t maxLen = s->m_readOffset;
	if ( maxLen > 1020 ) maxLen = 1020;
	gbmemcpy(stackMem,s->m_readBuf,maxLen);
	stackMem[maxLen] = '\0';

	// . sendReply returns false if blocked, true otherwise
	// . sets g_errno on error
	// . this calls sendErrorReply (404) if file does not exist
	// . g_msg is a ptr to a message like " (perm denied)" for ex. and
	//   it should be set by PageAddUrl.cpp, PageResults.cpp, etc.
	g_msg = "";
	sendReply ( s , &r , isAdmin) ;

	// log the request down here now so we can include 
	// "(permission denied)" on the line if we should
	if ( ! isPic ) {
		// what url refered user to this one?
		char *ref = r.getReferer();
		// skip over http:// in the referer
		if ( strncasecmp ( ref , "http://" , 7 ) == 0 ) ref += 7;

		// fix cookie for logging
		/*
		char cbuf[5000];
		char *p  = r.m_cookiePtr;
		int32_t  plen = r.m_cookieLen;
		if ( plen >= 4998 ) plen = 4998;
		char *pend = r.m_cookiePtr + plen;
		char *dst = cbuf;
		for ( ; p < pend ; p++ ) {
			*dst = *p;
			if ( ! *p ) *dst = ';';
			dst++;
		}
		*dst = '\0';
		*/

		// store the page access request in accessdb
		//g_accessdb.addAccess ( &r , ip );

		// if autobanned and we should not log, return now
		if ( g_msg && ! g_conf.m_logAutobannedQueries && 
		     strstr(g_msg,"autoban") ) { 
		}
		else if(isAdmin && dontLog) {
			//dont log if they ask us not to.
		}
		// . log the request
		// . electric fence (efence) seg faults here on iptoa() for
		//   some strange reason
		else if ( g_conf.m_logHttpRequests ) // && ! cgi[0] ) 
			logf (LOG_INFO,"http: %s %s %s %s "
			      //"cookie=\"%s\" "
			      //"%s "
			      "%s",
			      buf,iptoa(ip),
			      // can't use s->m_readBuf[] here because
			      // might have called TcpServer::sendMsg() which
			      // might have freed it if doing udp forwarding.
			      // can't use r.getRequest() because it inserts
			      // \0's in there for cgi parm parsing.
			      stackMem,
			      //s->m_readBuf,//r.getRequest(),
			      ref,
			      //r.m_cookiePtr,
			      //r.getUserAgent(),
			      g_msg);
		/*
		else if ( g_conf.m_logHttpRequests ) 
			logf (LOG_INFO,"http: %s %s %s %s %s "
			      //"cookie=\"%s\" "
			      //"%s "
			      "%s",
			      buf,iptoa(ip),
			      s->m_readBuf,//r.getRequest(),
			      cgi,
			      ref,
			      //cbuf,//r.m_cookiePtr,
			      //r.getUserAgent(),
			      g_msg);
		*/
	}

	// if no error, we completed w/o blocking so return
	//if ( ! g_errno ) return;
	// if g_errno was set then send an error msg
	//return sendErrorReply ( s, 500 , mstrerror(g_errno) );
}

/*
// it's better to hardcode this so we never lose it!
bool sendPageRobotsTxt ( TcpSocket *s , HttpRequest *r ) {
	SafeBuf sb;
	sb.safePrintf ( "User-Agent: *\n"
			"Disallow: *\n"
			//"Disallow: /search?makewidget=1\n"
			//"Disallow: /search?clockset=\n"
			"\n"
			);
	// this should copy it since sb is on stack
	return g_httpServer.sendDynamicPage ( s ,
					      sb.getBufStart(),
					      sb.m_length ,
					      0 ,
					      "text/html");
}
*/

bool endsWith(char *haystack, int haystackLen, char *needle, int needleLen) {
    return haystackLen >= needleLen && !strncmp(haystack + haystackLen - needleLen, needle, needleLen);
}

#include "Pages.h" // sendPageAPI, printApiForPage()

// . reply to a GET (including partial get) or HEAD request
// . HEAD just returns the MIME header for the file requested
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . it calls TcpServer::sendMsg(s,...) 
// .   with a File * as the callback data
// .   and with cleanUp() as the callback function
// . if sendMsg(s,...) blocks this cleanUp() will be called before the
//   socket gets recycled or destroyed (on error)
bool HttpServer::sendReply ( TcpSocket  *s , HttpRequest *r , bool isAdmin) {

	// get the server this socket uses
	TcpServer *tcp = s->m_this;
	// if there is a redir=http:// blah in the request then redirect
	int32_t redirLen = r->getRedirLen() ;
	char *redir   = NULL;

	// . we may be serving multiple hostnames
	// . www.gigablast.com, gigablast.com, www.inifinte.info,
	//   infinite.info, www.microdemocracy.com
	// . get the host: field from the MIME
	// . should be NULL terminated
	char *h  = r->getHost();

	if(redirLen > 0) redir = r->getRedir();
	else if (!isAdmin && 
		 *g_conf.m_redirect != '\0' &&
		 // was "raw"
		 r->getLong("xml", -1) == -1 &&
		 // do not redirect a 'gb proxy stop' request away,
		 // which POSTS cast=0&save=1. that is done from the
		 // command line, and for some reason it is not isAdmin
		 r->getLong("save", -1) != -1 &&
		 r->getString("site")  == NULL &&
		 r->getString("sites") == NULL) {
		//direct all non-raw, non admin traffic away.
		redir = g_conf.m_redirect;
		redirLen = gbstrlen(g_conf.m_redirect);
	}

	char *hdom = h;
	if ( strncasecmp(hdom,"www.",4) == 0 ) hdom += 4;
	// auto redirect eventguru.com to www.eventguru.com so cookies
	// are consistent
	if ( ! redir && 
	     ( strcasecmp ( h    , "eventguru.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbit.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbits.com" ) == 0 ||
	       strcasecmp ( hdom , "flurpit.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbot.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbits.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbyte.com" ) == 0 ||
	       strcasecmp ( hdom , "eventstereo.com" ) == 0 ||
	       strcasecmp ( hdom , "eventcrier.com" ) == 0 ||
	       strcasecmp ( hdom , "eventwidget.com" ) == 0 ) ) {
		redir = "http://www.eventguru.com/";
		redirLen = gbstrlen(redir);
	}


	if ( redirLen > 0 ) {
		// . generate our mime header
		// . see http://www.vbip.com/winsock/winsock_http_08_01.asp
		HttpMime m;
		m.makeRedirMime (redir,redirLen);
		// the query compress proxy, qcproxy, should handle this
		// on its level... but we will support ZET anyway
		return sendReply2 ( m.getMime(),m.getMimeLen(), NULL,0,s);
	}

	// . get info about the file requested
	// . use a "size" of -1 for the WHOLE file
	// . a non GET request should use a "size" of 0 (like HEAD)
	char *path    = r->getFilename();
	int32_t  pathLen = r->getFilenameLen();
	// paths with ..'s are from hackers!
	for ( char *p = path ; *p ; p++ )
		if ( *p == '.' && *(p+1) == '.' )
			return sendErrorReply(s,404,"bad request");

	// dump urls or json objects or pages? 
	// "GET /crawlbot/downloadurls"
	// "GET /crawlbot/downloadobjects"
	// "GET /crawlbot/downloadpages"
	if ( strncmp ( path , "/crawlbot/download/" ,19 ) == 0 ||
	     // add 4 to length of needle to account for /vXX.
	     // GET /v3/crawl/download/
	     (pathLen >= 20 && strnstr(path, "/crawl/download/", 20)) ||
	     (pathLen >= 19 && strnstr(path, "/bulk/download/", 19)) )
		return sendBackDump ( s , r );

	// "GET /download/mycoll_urls.csv"
	if ( strncmp ( path , "/download/", 10 ) == 0 )
		return sendBackDump ( s , r );

	if ( strncmp ( path , "/gbiaitem/" , 10 ) == 0 ) {
		SafeBuf cmd;
		char *iaItem = path + 10;
		char c = iaItem[pathLen];
		iaItem[pathLen] = '\0';
		// iaItem is like "webgroup-20100422114008-00011"
		// print out the warc files as if they were urls
		// so we can spider them through the spider pipeline as-is.
		// this hack only works on internet archive servers
		// that have the '/home/mwells/ia' obviously
		cmd.safePrintf("/home/mwells/ia list %s --glob='*arc.gz' | "
			       "awk '{print \"<a "
			       "href=http://archive.org/download/"
			       "%s/\"$1\">\"$1\"</a><br>\"}' > ./tmpiaout"
			       //, g_hostdb.m_dir
			       ,iaItem
			       ,iaItem
			       );
		iaItem[pathLen] = c;
		log("system: %s",cmd.getBufStart());
		gbsystem ( cmd.getBufStart() );
		SafeBuf sb;
		sb.safePrintf("<title>%s</title>\n<br>\n",iaItem);
		sb.load ( "./tmpiaout" );
		// remove those pesky ^M guys. i guess ia is windows based.
		sb.safeReplace3("\r","");
		//log("system: output(%"INT32"=%s",sb.getBufStart(),
		//sb.length());
		return g_httpServer.sendDynamicPage(s,
						    sb.getBufStart(),
						    sb.length(),
						    0, false, 
						    "text/html",
						    -1, NULL,
						    "UTF-8");
	}
		

	// . is it a diffbot api request, like "GET /api/*"
	// . ie "/api/startcrawl" or "/api/stopcrawl" etc.?
	//if ( strncmp ( path , "/api/" , 5 ) == 0 )
	//	// this will call g_httpServer.sendDynamicPage() to send
	//	// back the reply when it is done generating the reply.
	//	// this function is in Diffbot.cpp.
	//	return handleDiffbotRequest ( s , r );


	// for adding to browser list of search engines
	if ( strncmp ( path, "/eventguru.xml", 14 ) == 0 )  {
		SafeBuf sb(256);
		sb.safePrintf(
			      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\" xmlns:moz=\"http://www.mozilla.org/2006/browser/search/\">\n"
			      "<ShortName>Event Guru</ShortName>\n"
			      "<Description>Event Guru</Description>\n"
			      "<InputEncoding>UTF-8</InputEncoding>\n"
			      "<SyndicationRight>limited</SyndicationRight>\n"
			      "<Image width=\"16\" height=\"16\" type=\"image/x-icon\">http://www.eventguru.com/favicon.ico</Image>\n"
			      "<Url type=\"text/html\" method=\"GET\" template=\"http://www.eventguru.com/?q={searchTerms}\"/>\n"
			      //"<Url type=\"application/x-suggestions+json\" template=\"http://www.eventguru.com/autocomplete?term={searchTerms}&lang={language?}&form=opensearch\"/>\n"
			      "<Url type=\"application/opensearchdescription+xml\" rel=\"self\" template=\"http://www.eventguru.com/eventguru.xml\"/>\n"
			      "<moz:SearchForm>http://www.eventguru.com/</moz:SearchForm>\n"
			      "</OpenSearchDescription>\n"
			      );
		return g_httpServer.sendDynamicPage(s,
						    sb.getBufStart(),
						    sb.length(),
						    0, false, 
						    "text/xml",
						    -1, NULL,
						    "UTF-8");
	}


	// if it's gigablast.com, www.gigablast.com, ... do int16_tcut
	bool isGigablast = false;
	if ( strcasecmp ( h , "www.gigablast.com" ) == 0 ) isGigablast = true;
	if ( strcasecmp ( h , "gigablast.com"     ) == 0 ) isGigablast = true;
	//bool isNewSite = false;
	//if ( gb_strcasestr ( h , "eventwidget.com"   ) ) isNewSite = true;
	//if ( gb_strcasestr ( h , "eventguru.com"     ) ) isNewSite = true;
	//isNewSite = true;

	// get the dynamic page number, is -1 if not a dynamic page
	int32_t n = g_pages.getDynamicPageNumber ( r );

	int32_t niceness = g_pages.getNiceness(n);
	niceness = r->getLong("niceness", niceness);
	// save it
	s->m_niceness = niceness;

	// the new cached page format. for twitter.
	if ( ! strncmp ( path , "/?id=" , 5 ) ) n = PAGE_RESULTS;
	if ( strncmp(path,"/crawlbot",9) == 0 ) n = PAGE_CRAWLBOT;
	if (endsWith(path, pathLen, "/crawl", 6)) n = PAGE_CRAWLBOT;
	if (endsWith(path, pathLen, "/bulk", 5)) n = PAGE_CRAWLBOT;
	if (endsWith(path, pathLen, "/search", 6)) n = PAGE_RESULTS;

	bool isProxy = g_proxy.isProxy();
	// . prevent coring
	// . we got a request for
	//   POST /%3Fid%3D19941846627.3030756329856867809/trackback
	//   which ended up calling sendPageEvents() on the proxy!!
	if ( isProxy && ( n == PAGE_RESULTS || n == PAGE_ROOT ) )
		n = -1;

	//////////
	//
	// if they say &showinput=1 on any page we show the input parms
	//
	//////////
	char format = r->getReplyFormat();
	int32_t show = r->getLong("showinput",0);
	WebPage *wp = g_pages.getPage(n);
	if ( wp && (wp->m_pgflags & PG_NOAPI) ) show = false;
	if ( show ) {
		SafeBuf sb;
		//CollectionRec *cr = g_collectiondb.getRec ( r );
		//printApiForPage ( &sb , n , cr );
		// xml/json header
		char *res = NULL;
		if ( format == FORMAT_XML )
			res = "<response>\n"
				"\t<statusCode>0</statusCode>\n"
				"\t<statusMsg>Success</statusMsg>\n";
		if ( format == FORMAT_JSON )
			res = "{ \"response\":{\n"
				"\t\"statusCode\":0,\n"
				"\t\"statusMsg\":\"Success\",\n";
		if ( res )
			sb.safeStrcpy ( res );
		//
		// this is it
		//
		g_parms.printParmTable ( &sb , s , r );
		// xml/json tail
		if ( format == FORMAT_XML )
			res = "</response>\n";
		if ( format == FORMAT_JSON )
			res = "\t}\n}\n";
		if ( res )
			sb.safeStrcpy ( res );
		char *ct = "text/html";
		if ( format == FORMAT_JSON ) ct = "application/json";
		if ( format == FORMAT_XML  ) ct = "text/xml";
		return g_httpServer.sendDynamicPage(s,
						    sb.getBufStart(),
						    sb.length(),
						    0, false, 
						    ct,
						    -1, NULL,
						    "UTF-8");
	}



	// map to new events page if we are eventguru.com
	//if ( isNewSite && ( n == PAGE_RESULTS || n == PAGE_ROOT ) )
	//	return sendPageEvents ( s , r );

	if ( n == PAGE_RESULTS ) // || n == PAGE_ROOT )
		return sendPageResults ( s , r );

	// . redirect eventwidget.com search traffic to results
	// . i think this is a zombie bot click farm!
	// . i did a check and its pretty worthless!!
	//if ( ! strncmp ( path ,"/search.cfm", pathLen ) )
	//	return sendPageEvents ( s , r );		
	
	// let's try without this for now
	//if ( g_loop.m_inQuickPoll && niceness ) {
	//	//log(LOG_WARN, "saving request for later. %"INT32" %"INT32" %"INT32"",
	//	//	    (int32_t)s, (int32_t)r, n);
	//	addToQueue(s, r, n);
	//	if(g_errno) 
	//		return g_httpServer.sendErrorReply(s,505,
	//						   mstrerror(g_errno));
	//	return true;
	//}
	//g_loop.canQuickPoll(niceness);

	// for flurbit.com old layout
	//if ( ! strncmp ( path ,"/addevent", pathLen ) ) {
	//	if ( ! isNewSite ) return sendPageAddEvent2 ( s , r );
	//	else               return sendPageAddEvent  ( s , r );
	//}

	// prints out stats for widgetmasters so they can see the traffic
	// they sent to us...
	//if ( ! strncmp ( path ,"/account", pathLen ) ) {
	//	return sendPageAccount ( s , r );
	//}


	// . if not dynamic this will be -1
	// . sendDynamicReply() returns false if blocked, true otherwise
	// . it sets g_errno on error
	if ( n >= 0 ) return g_pages.sendDynamicReply ( s , r , n );


	//if ( ! strncmp ( path ,"/widget.html", pathLen ) )
	//	return sendPageWidget ( s , r );

	// use a standard robots. do not allow someone to forget to have
	// that file in place!! let google hit us now that browse.html
	// is dynamic and the urls seem static so they should be digested
	// by google bot
	//if ( ! strncmp ( path ,"/robots.txt", pathLen ) )
	//	return sendPageRobotsTxt ( s , r );

	//if ( ! strncmp ( path ,"/sitemap.xml", pathLen ) )
	//	return sendPageSiteMap ( s , r );

	//if ( ! isNewSite ) {
	//	// for the old flurbit layout
	//	if ( ! strncmp ( path ,"/browse.html", pathLen ) )
	//		return sendPageBrowse ( s , r );
	//}

	// comment out for old flurbit layout
	//if ( ! strncmp ( path ,"/help.html", pathLen ) )
	//	return sendPageAbout ( s , r , path );

	//if ( ! strncmp ( path ,"/adv.html", pathLen ) )
	//	return sendPageAdvanced ( s , r );

	//if ( ! strncmp ( path ,"/about.html", pathLen ) )
	//	return sendPageAbout ( s , r );

	if ( ! strncmp ( path ,"/help.html", pathLen ) )
		return sendPageHelp ( s , r );
	if ( ! strncmp ( path ,"/syntax.html", pathLen ) )
		return sendPageHelp ( s , r );

	if ( ! strncmp ( path ,"/widgets.html", pathLen ) )
		return sendPageWidgets ( s , r );

	if ( ! strncmp ( path ,"/adv.html", pathLen ) )
		return sendPagePretty ( s , r,"adv.html","advanced");

	// who uses gigablast?
	if ( ! strncmp ( path ,"/users.html", pathLen ) )
		return sendPagePretty ( s , r,"users.html","users"); // special

	if ( ! strncmp ( path ,"/about.html", pathLen ) )
		return sendPagePretty ( s , r , "about.html","about" );

	// decorate the plain html page, news.html, with our nav chrome
	if ( ! strncmp ( path ,"/blog.html", pathLen ) )
		return sendPagePretty ( s , r , "blog.html", "blog");

	// decorate the plain html page with our nav chrome
	if ( ! strncmp ( path ,"/searchfeed.html", pathLen ) )
		return sendPagePretty ( s , r , "searchfeed.html", "feed");


	// decorate the plain html page, rants.html, with our nav chrome
	if ( ! strncmp ( path ,"/faq.html", pathLen ) )
		return sendPagePretty ( s , r , "faq.html", "faq");
	if ( ! strncmp ( path ,"/admin.html", pathLen ) )
		return sendPagePretty ( s , r , "faq.html", "faq");

	// decorate the plain html pages with our nav chrome
	if ( ! strncmp ( path ,"/developer.html", pathLen ) )
		return sendPagePretty ( s , r , "developer.html", "developer");
	if ( ! strncmp ( path ,"/compare.html", pathLen ) )
		return sendPagePretty ( s , r , "compare.html", "compare");

	if ( ! strncmp ( path ,"/contact.html", pathLen ) )
		return sendPagePretty ( s , r , "contact.html", "contact");

	if ( ! strncmp ( path ,"/bio.html", pathLen ) )
		return sendPagePretty ( s , r , "bio.html", "bio");

	if ( ! strncmp ( path ,"/appliance.html", pathLen ) )
		return sendPagePretty ( s , r , "appliance.html", "appliance");


	if ( ! strncmp ( path ,"/api.html", pathLen ) )
		return sendPageAPI ( s , r  );
	if ( ! strncmp ( path ,"/api", pathLen ) )
		return sendPageAPI ( s , r  );

	if ( ! strncmp ( path ,"/print", pathLen ) )
		return sendPageAnalyze ( s , r  );

	// proxy should handle all regular file requests itself! that is
	// generally faster i think, and, besides, sending pieces of a big
	// file one at a time using our encapsulation method won't work! so
	// put a sanity check in here to make sure! also i am not convinced
	// that we set s->m_sd to something that won't conflict with real
	// TcpSocket descriptors (see m_sd above)
	if ( s->m_udpSlot ) { 
		log("http: proxy should have handled this request");
		// i've seen this core once on a GET /logo-small.png request
		// and i am not sure why... so let it slide...
		//char *xx=NULL;*xx=0; }
	}

	// . where do they want us to start sending from in the file
	// . 0 is the default, if none specified
	int32_t  offset      = r->getFileOffset();
	// make sure offset is positive
	if ( offset < 0 ) offset = 0;
	// . how many bytes do they want of the file?
	// . this returns -1 for ALL of file
	int32_t  bytesWanted = r->getFileSize();   
	// create a file based on this path
	File *f ;
	try { f = new (File) ; }
	// return true and set g_errno if couldn't make a new File class
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("HttpServer: new(%"INT32"): %s",
		    (int32_t)sizeof(File),mstrerror(g_errno));
		return sendErrorReply(s,500,mstrerror(g_errno)); 
	}
	mnew ( f, sizeof(File), "HttpServer");
	// note that 
	if ( g_conf.m_logDebugTcp )
		log("tcp: new filestate=0x%"PTRFMT"",(PTRTYPE)f);
	// don't honor HUGE requests
	if ( pathLen > 100 ) {
		if ( g_conf.m_logDebugTcp )
			log("tcp: deleting filestate=0x%"PTRFMT" [1]",
			    (PTRTYPE)f);
		mdelete ( f, sizeof(File), "HttpServer");
		delete (f);
		g_errno = EBADREQUEST;
		return sendErrorReply(s,500,"request url path too big");
	}
	// set the filepath/name
	char fullPath[512];
	bool isQAFile = false;
	// if it's gigablast.com, www.gigablast.com, ... do int16_tcut
	//if ( strcasecmp ( h , "www.gigablast.com" ) == 0 ) goto skip;
	//if ( strcasecmp ( h , "gigablast.com"     ) == 0 ) goto skip;
	if ( isGigablast ) goto skip;
	// otherwise, look for special host
	sprintf(fullPath,"%s/%s/%s", g_hostdb.m_httpRootDir, h, path );
	// set filename to full path
	f->set ( fullPath );
	// if f does not exist then try w/o the host in the path
	if ( f->doesExist() <= 0 ) {
		// skip to here if we're the gigablast host
	skip:
		// use default page if does not exist under host-specific path
		if (pathLen == 11 && strncmp ( path , "/index.html" ,11 ) ==0){
			if ( g_conf.m_logDebugTcp )
				log("tcp: deleting filestate=0x%"PTRFMT" [2]",
				    (PTRTYPE)f);
			mdelete ( f, sizeof(File), "HttpServer");
			delete (f);
			return g_pages.sendDynamicReply ( s , r , PAGE_ROOT );
		}
		// otherwise, use default html dir
		sprintf(fullPath,"%s/%s", g_hostdb.m_httpRootDir , path );

		// special hack for /qa/content.* stuff, do not use /html/
		if ( strncmp(path,"/qa/",4) == 0 ) {
			isQAFile = true;
			sprintf(fullPath,"%s%s", g_hostdb.m_dir, path );
		}

		// now retrieve the file
		f->set ( fullPath );
	}		
	// if f STILL does not exist (or error) then send a 404
	if ( f->doesExist() <= 0 ) {
		if ( g_conf.m_logDebugTcp )
			log("tcp: deleting filestate=0x%"PTRFMT" [3]",
			    (PTRTYPE)f);
		mdelete ( f, sizeof(File), "HttpServer");
		delete (f);
		return sendErrorReply ( s , 404 , "Not Found" );
	}
	// when was this file last modified
	time_t lastModified = f->getLastModifiedTime();
	int32_t   fileSize     = f->getFileSize();
	// . assume we're sending the whole file 
	// . this changes if it's a partial GET
	int32_t   bytesToSend  = fileSize;
	// . bytesWanted is positive if the request specified it
	// . ensure it's not bigger than the fileSize itself
	if ( bytesWanted >= 0 ) {
		// truncate "bytesWanted" if we need to
		if ( offset + bytesWanted > fileSize ) 
			bytesToSend = fileSize - offset;
		else    bytesToSend = bytesWanted;
	}
	// . only try to open "f" is bytesToSend is positive
	// . return true and set g_errno if:
	// . could not open it for reading!
	// . or could not set it to non-blocking
	// . set g_errno on error
	if ( bytesToSend > 0  &&  ! f->open(O_RDONLY | O_NONBLOCK | O_ASYNC)) {
		if ( g_conf.m_logDebugTcp )
			log("tcp: deleting filestate=0x%"PTRFMT" [4]",
			    (PTRTYPE)f);
		mdelete ( f, sizeof(File), "HttpServer");
		delete (f); 
		return sendErrorReply ( s , 404 , "Not Found" );
	}
	// are we sending partial content?
	bool   partialContent = false;
	if ( offset > 0 || bytesToSend != fileSize ) partialContent = true;
	// . use the file extension to determine content type
	// . default to html if no extension
	char *ext = f->getExtension();
	// . generate our mime header
	// . see http://www.vbip.com/winsock/winsock_http_08_01.asp
	// . this will do a "partial content" mime if offset!=0||size!=-1
	HttpMime m;
	// . the first "0" to makeMime means to use the browser cache rules
	// . it is the time to cache the page (0 means let browser decide)
	// . tell browser to cache all non-dynamic files we have for a day
	int32_t ct = 0; // 60*60*24;
	// hmmm... chrome seems not to cache the little icons! so cache
	// for two hours.
	ct = 2*3600;
	// but not the performance graph!
	if ( strncmp(path,"/diskGraph",10) == 0 ) ct = 0;

	// if no extension assume charset utf8
	char *charset = NULL;
	if ( ! ext || ext[0] == 0 ) charset = "utf-8";

	if ( isQAFile ) ext = "txt";

	if ( partialContent )
		m.makeMime (fileSize,ct,lastModified,offset,bytesToSend,ext,
			    false,NULL,charset,-1,NULL);
	else	m.makeMime (fileSize,ct,lastModified,0     ,-1         ,ext,
			    false,NULL,charset,-1,NULL);
	// sanity check, compression not supported for files
	if ( s->m_readBuf[0] == 'Z' ) { 
		int32_t len = s->m_readOffset;
		// if it's null terminated then log it
		if ( ! s->m_readBuf[len] )
			log("http: got ZET request and am not proxy: %s",
			    s->m_readBuf);
		// otherwise, do not log the request
		else
			log("http: got ZET request and am not proxy");
		// bail
		if ( g_conf.m_logDebugTcp )
			log("tcp: deleting filestate=0x%"PTRFMT" [5]",
			    (PTRTYPE)f);
		mdelete ( f, sizeof(File), "HttpServer");
		delete (f); 
		g_errno = EBADREQUEST;
		return sendErrorReply(s,500,mstrerror(g_errno));
	}
	// . move the reply to a send buffer
	// . don't make sendBuf bigger than g_httpMaxSendBufSize
	int32_t mimeLen     = m.getMimeLen();
	int32_t sendBufSize = mimeLen + bytesToSend;
	if ( sendBufSize > g_conf.m_httpMaxSendBufSize ) 
		sendBufSize = g_conf.m_httpMaxSendBufSize;
	char *sendBuf    = (char *) mmalloc ( sendBufSize ,"HttpServer" );
	if ( ! sendBuf ) { 
		if ( g_conf.m_logDebugTcp )
			log("tcp: deleting filestate=0x%"PTRFMT" [6]",
			    (PTRTYPE)f);
		mdelete ( f, sizeof(File), "HttpServer");
		delete (f); 
		return sendErrorReply(s,500,mstrerror(g_errno));
	}
	gbmemcpy ( sendBuf , m.getMime() , mimeLen );
	// save sd
	int sd = s->m_sd;
	// . send it away
	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . this will destroy "s" on error
	// . "f" is the callback data
	// . it's passed to cleanUp() on completion/error before socket
	//   is recycled/destroyed
	// . this will call getMsgPiece() to fill up sendBuf from file
	int32_t totalToSend = mimeLen + bytesToSend;

	//s->m_state = NULL; // do we need this? yes, cuz s is NULL for cleanUp
	if ( s && s->m_state == f ) s->m_state = NULL;

	//if ( ! m_tcp.sendMsg ( s           , 
	if (  ! tcp->sendMsg ( s           , 
			       sendBuf     ,
			       sendBufSize ,
			       mimeLen     ,
			       totalToSend ,
			       f           , // callback data for getMsgPiece()
			       cleanUp     ) ) return false;

	// . otherwise sendMsg() blocked, so return false
	// . just return false if we don't need to read from f
	// (mdw) if ( bytesToSend <= 0 ) return false;
	// . if it did not block , free our stuff
	// . on error "s" should have been destroyed and g_errno set
	// . unregister our handler on f
	// . return true cuz we did not block at all
	//delete (f);
	// . tcp server may NOT have called cleanup if the socket closed on us
	//   but it may have called getMsgPiece(), thus registering the fd
	// . if we do not unregister but just delete f it will cause a segfault
	// . if the above returned true then it MUST have freed our socket,
	//   UNLESS s->m_waitingonHandler was true, which should not be the
	//   case, as it is only set to true in TcpServer::readSocketWrapper()
	//   which should never be called by TcpServer::sendMsg() above.
	//   so let cleanUp know it is no longer valid
	if ( ! f->calledOpen() ) f->open( O_RDONLY );
	int fd = f->getfd();
	cleanUp ( f , NULL/*TcpSocket */ );
	// . AND we need to do this ourselves here
	// . do it SILENTLY so not message is logged if fd not registered
	if (tcp->m_useSSL)
		g_loop.unregisterReadCallback ( fd,(void *)(PTRTYPE)(sd),
						getSSLMsgPieceWrapper , 
						true /*silent?*/);
	else
		g_loop.unregisterReadCallback ( fd,(void *)(PTRTYPE)(sd),
						getMsgPieceWrapper , 
						true /*silent?*/);

	// TcpServer will free sendBuf on s's recycling/destruction
	// mfree ( sendBuf , sendBufSize );
	return true;
}

bool HttpServer::sendReply2 ( char *mime, 
			      int32_t  mimeLen ,
			      char *content  ,
			      int32_t  contentLen ,
			      TcpSocket *s ,
			      bool alreadyCompressed ,
			      HttpRequest *hr ) {

	char *rb = s->m_readBuf;
	// int16_tcut
	int32_t ht = g_hostdb.m_myHost->m_type;
	// get the server this socket uses
	TcpServer *tcp = s->m_this;
	// . move the reply to a send buffer
	// . don't make sendBuf bigger than g_httpMaxSendBufSize
	int32_t sendBufSize = mimeLen + contentLen;
	// if we are a regular proxy and this is compressed, just forward it
	//if ( (ht & HT_PROXY) && *rb == 'Z' && alreadyCompressed ) {
	if ( alreadyCompressed ) {
		sendBufSize = contentLen;
		//if ( mimeLen ) { char *xx=NULL;*xx=0; }
	}
	// what the hell is up with this???
	//if ( sendBufSize > g_conf.m_httpMaxSendBufSize ) 
	//	sendBufSize = g_conf.m_httpMaxSendBufSize;
	char *sendBuf    = (char *) mmalloc (sendBufSize,"HttpServer");
	// the alloc size is synonymous at this point
	int32_t sendBufAlloc = sendBufSize;
	//if ( ! sendBuf ) 
	//	return sendErrorReply(s,500,mstrerror(g_errno));
	// . this is the ONLY situation in which we destroy "s" ourselves
	// . that is, when we cannot even transmit the server-side error info
	if ( ! sendBuf ) { 
		log("http: Failed to allocate %"INT32" bytes for send "
		    "buffer. Send failed.",sendBufSize);
		// set this so it gets destroyed
		s->m_waitingOnHandler = false;
		tcp->destroySocket ( s );
		return true;
	}

	// we swap out the GET for a ZET
	bool doCompression = ( rb[0] == 'Z' );
	// qtproxy never compresses a reply
	if ( ht & HT_QCPROXY ) doCompression = false;
	// . only grunts do the compression now to prevent proxy overload
	// . but if we are originating the msg ourselves, then we should
	//   indeed do compres
	//if ( ! ( ht & HT_GRUNT) && ! originates ) doCompression = false;
	if ( alreadyCompressed ) doCompression = false;
	// p is a moving ptr into "sendBuf"
	unsigned char *p    = (unsigned char *)sendBuf;
	unsigned char *pend = (unsigned char *)sendBuf + sendBufAlloc;

	// if we are fielding a request for a qcproxy's 0xfd request,
	// then compress the reply before sending back.
	if ( doCompression ) {
		// store uncompressed size1 and size1
		*(int32_t *)p = mimeLen + contentLen; p += 4;
		// bookmarks
		int32_t *saved1 = (int32_t *)p; p += 4;
		int32_t *saved2 = (int32_t *)p; p += 4;
		uint32_t  used1 = pend - p;
		int err1 = gbcompress ( p , &used1, 
					(unsigned char *)mime,mimeLen );
		if ( mimeLen && err1 != Z_OK )
			log("http: error compressing mime reply.");
		p += used1;
		// update bookmark
		*saved1 = used1;
		// then store the page content
		uint32_t used2 = 0;
		if ( contentLen > 0 ) {
			// pass in avail buf space
			used2 = pend - p;
			// this will set used2 to what we used to compress it
			int err2 = gbcompress(p,
					      &used2,
					      (unsigned char *)content,
					      contentLen);
			if ( err2 != Z_OK )
				log("http: error compressing content reply.");
		}
		p += used2;
		// update bookmark
		*saved2 = used2;
		// note it
		//logf(LOG_DEBUG,"http: compressing. from %"INT32" to %"INT32"",
		//     mimeLen+contentLen,(int32_t)(((char *)p)-sendBuf));
		// change size
		sendBufSize = (char *)p - sendBuf;
	}
	// if we are a proxy, and not a compression proxy, then just forward
	// the blob as-is if it is a "ZET" (GET-compressed=ZET)
	else if ( (ht & HT_PROXY) && (*rb == 'Z') ) {
		gbmemcpy ( sendBuf , content, contentLen );
		// sanity check
		if ( sendBufSize != contentLen ) { char *xx=NULL;*xx=0; }
		// note it
		//logf(LOG_DEBUG,"http: forwarding. pageLen=%"INT32"",contentLen);
	}
	else {
		// copy mime into sendBuf first
		gbmemcpy ( p , mime , mimeLen );
		p += mimeLen;
		// then the page
		gbmemcpy ( p , content, contentLen );
		p += contentLen;
		// sanity check
		if ( sendBufSize != contentLen+mimeLen) { char *xx=NULL;*xx=0;}
	}

	// . store the login/logout links after <body> tag
	// . only proxy should provide a non-null hr right now
	/*
	if ( hr ) {
		int32_t newReplySize;
		char *newReply = g_proxy.storeLoginBar ( sendBuf, 
							 sendBufSize, 
							 sendBufAlloc,
							 mimeLen,
							 &newReplySize,
							 hr );
		// different? no, we free it in storeloginbar
		//if ( newReply != sendBuf )
		//	mfree ( sendBuf , sendBufSize ,"sbufa" );
		// and do it
		sendBuf      = newReply;
		sendBufSize  = newReplySize;
		sendBufAlloc = newReplySize;
	}
	*/

	// . send it away
	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . this will destroy "s" on error
	// . "f" is the callback data
	// . it's passed to cleanUp() on completion/error before socket
	//   is recycled/destroyed
	// . this will call getMsgPiece() to fill up sendBuf from file
	if (  ! tcp->sendMsg ( s           , 
			       sendBuf     ,
			       sendBufAlloc ,
			       sendBufSize ,
			       sendBufSize ,
			       NULL        ,   // data for callback
			       NULL        ) ) // callback
		return false;
	// it didn't block or there was an error
	return true;
}

// . this is called when our reply has been sent or on error
// . "s" should be recycled/destroyed by TcpServer after we return
// . this recycling/desutruction will free s's read/send bufs
// . this should have been called by TcpServer::makeCallback()
void cleanUp ( void *state , TcpSocket *s ) {
	// free the send buffer
	//mfree ( s->m_sendBuf , s->m_sendBufSize );
	//mfree ( s->m_readBuf , s->m_readBufSize );
	//s->m_sendBuf     = NULL;
	//s->m_sendBufSize = 0;
	File *f = (File *) state;
	if ( ! f ) return;
	int32_t fd = -1;
	// debug msg
	//log("HttpServer: unregistering file fd: %i", f->getfd());
	// unregister f from getting callbacks (might not be registerd)
	if ( s ) {
		// When reading from a slow disk, socket gets closed before the
		// file and sets its descriptor to be negative, so make sure this 
		// is positive so we can find the callback.
		int32_t socketDescriptor = s->m_sd;
		if (socketDescriptor < 0) socketDescriptor *= -1;
		// set this
		fd = f->getfd();
		// get the server this socket uses
		TcpServer *tcp = s->m_this;
		// do it SILENTLY so not message is logged if fd not registered
		if (tcp->m_useSSL)
			g_loop.unregisterReadCallback ( fd,//f->getfd(),
						    (void *)(PTRTYPE)(socketDescriptor),
							getSSLMsgPieceWrapper,
							true );
		else
			g_loop.unregisterReadCallback ( fd,//f->getfd(),
						    (void *)(PTRTYPE)(socketDescriptor),
							getMsgPieceWrapper , 
							true );
	}
	if ( g_conf.m_logDebugTcp )
		log("tcp: deleting filestate=0x%"PTRFMT" fd=%"INT32" [7] "
		    "s=0x%"PTRFMT"", (PTRTYPE)f,fd,(PTRTYPE)s);
	// this should also close f
	mdelete ( f, sizeof(File), "HttpServer");
	delete (f);
	// . i guess this is the file state!?!?!
	// . it seems the socket sometimes is not destroyed when we return
	//   and we get a sig hangup and call this again!! so make this NULL
	if ( s && s->m_state == f ) s->m_state = NULL;
}

bool HttpServer::sendSuccessReply ( GigablastRequest *gr , char *addMsg ) {
	return sendSuccessReply ( gr->m_socket ,
				  gr->m_hr.getReplyFormat() ,
				  addMsg );
}

bool HttpServer::sendSuccessReply ( TcpSocket *s , char format, char *addMsg) {
	// get time in secs since epoch
	time_t now ;
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();
	// . buffer for the MIME request and brief html err msg
	// . NOTE: ctime appends a \n to the time, so we don't need to
	char msg[1524];
	SafeBuf sb(msg,1524,0,false);

	char *tt = asctime(gmtime ( &now ));
	tt [ gbstrlen(tt) - 1 ] = '\0';

	char *ct = "text/html";
	if ( format == FORMAT_XML  ) ct = "text/xml";
	if ( format == FORMAT_JSON ) ct = "application/json";

	char cbuf[1024];
	SafeBuf cb(cbuf,1024,0,false);

	if ( format != FORMAT_XML && format != FORMAT_JSON )
		cb.safePrintf("<html><b>Success</b></html>");

	if ( format == FORMAT_XML ) {
		cb.safePrintf("<response>\n"
			      "\t<statusCode>0</statusCode>\n"
			      "\t<statusMsg><![CDATA[Success]]>"
			      "</statusMsg>\n");
	}

	if ( format == FORMAT_JSON ) {
		cb.safePrintf("{\"response\":{\n"
			      "\t\"statusCode\":0,\n"
			      "\t\"statusMsg\":\"Success\",\n" );
	}

	if ( addMsg )
		cb.safeStrcpy(addMsg);


	if ( format == FORMAT_XML ) {
		cb.safePrintf("</response>\n");
	}

	if ( format == FORMAT_JSON ) {
		// erase trailing ,\n
		cb.m_length -= 2;
		cb.safePrintf("\n"
			      "}\n"
			      "}\n");
	}


	sb.safePrintf(
		      "HTTP/1.0 200 (OK)\r\n"
		      "Content-Length: %"INT32"\r\n"
		      "Connection: Close\r\n"
		      "Content-Type: %s\r\n"
		      "Date: %s UTC\r\n\r\n"
		      , cb.length()
		      , ct
		      , tt );

	sb.safeMemcpy ( &cb );

	// use this new function that will compress the reply now if the
	// request was a ZET instead of a GET
	return sendReply2 ( sb.getBufStart(), sb.length() , NULL , 0 , s );
}

bool HttpServer::sendErrorReply ( GigablastRequest *gr ) {

	int32_t error = g_errno;
	char *errmsg = mstrerror(error);

	time_t now ;//= getTimeGlobal();
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();

	int32_t format = gr->m_hr.getReplyFormat();
	char msg[1524];
	SafeBuf sb(msg,1524,0,false);
	char *tt = asctime(gmtime ( &now ));
	tt [ gbstrlen(tt) - 1 ] = '\0';

	char *ct = "text/html";
	if ( format == FORMAT_XML  ) ct = "text/xml";
	if ( format == FORMAT_JSON ) ct = "application/json";

	SafeBuf xb;

	if ( format != FORMAT_XML && format != FORMAT_JSON )
		xb.safePrintf("<html><b>Error = %s</b></html>",errmsg );

	if ( format == FORMAT_XML ) {
		xb.safePrintf("<response>\n"
			      "\t<statusCode>%"INT32"</statusCode>\n"
			      "\t<statusMsg><![CDATA[", error );
		xb.cdataEncode(errmsg );
		xb.safePrintf("]]></statusMsg>\n"
			      "</response>\n");
	}

	if ( format == FORMAT_JSON ) {
		xb.safePrintf("{\"response\":{\n"
			      "\t\"statusCode\":%"INT32",\n"
			      "\t\"statusMsg\":\"", error );
		xb.jsonEncode(errmsg );
		xb.safePrintf("\"\n"
			      "}\n"
			      "}\n");
	}

	sb.safePrintf(
		      "HTTP/1.0 %"INT32" (%s)\r\n"
		      "Content-Length: %"INT32"\r\n"
		      "Connection: Close\r\n"
		      "Content-Type: %s\r\n"
		      "Date: %s UTC\r\n\r\n"
		      ,
		      error  ,
		      errmsg ,

		      xb.length(),

		      ct ,
		      tt ); // ctime ( &now ) ,


	sb.safeMemcpy ( &xb );

	// use this new function that will compress the reply now if the
	// request was a ZET instead of a GET
	return sendReply2 ( sb.getBufStart(),sb.length(),NULL,0,gr->m_socket );
}

// . send an error reply, like "HTTP/1.1 404 Not Found"
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool HttpServer::sendErrorReply ( TcpSocket *s , int32_t error , char *errmsg ,
				  int32_t *bytesSent ) {
	if ( bytesSent ) *bytesSent = 0;
	// clear g_errno so the send goes through
	g_errno = 0;

	// sanity check
	if ( strncasecmp(errmsg,"Success",7)==0 ) {char*xx=NULL;*xx=0;}

	// get time in secs since epoch
	time_t now ;//= getTimeGlobal();
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();

	// this kinda sucks that we have to do it twice...
	HttpRequest hr;
	hr.set ( s->m_readBuf , s->m_readOffset , s ) ;
	char format = hr.getReplyFormat();

	// . buffer for the MIME request and brief html err msg
	// . NOTE: ctime appends a \n to the time, so we don't need to
	char msg[1524];
	SafeBuf sb(msg,1524,0,false);
	// if it's a 404, redirect to home page
	/*
	if ( error == 404 ) 
		sprintf ( msg ,
			  "HTTP/1.0 301 Moved\r\n"
			  "Content-length: 0\r\n"
			  "Connection: Close\r\n"
			  "Location: http://www.gigablast.com/\r\n"
			  "Date: %s\r\n\r\n",
			  ctime ( &now ) );
	else 
	*/
	char *tt = asctime(gmtime ( &now ));
	tt [ gbstrlen(tt) - 1 ] = '\0';

	char *ct = "text/html";
	if ( format == FORMAT_XML  ) ct = "text/xml";
	if ( format == FORMAT_JSON ) ct = "application/json";

	SafeBuf xb;

	if ( format != FORMAT_XML && format != FORMAT_JSON )
		xb.safePrintf("<html><b>Error = %s</b></html>",errmsg );

	if ( format == FORMAT_XML ) {
		xb.safePrintf("<response>\n"
			      "\t<statusCode>%"INT32"</statusCode>\n"
			      "\t<statusMsg><![CDATA[", error );
		xb.cdataEncode(errmsg );
		xb.safePrintf("]]></statusMsg>\n"
			      "</response>\n");
	}

	if ( format == FORMAT_JSON ) {
		xb.safePrintf("{\"response\":{\n"
			      "\t\"statusCode\":%"INT32",\n"
			      "\t\"statusMsg\":\"", error );
		xb.jsonEncode(errmsg );
		xb.safePrintf("\"\n"
			      "}\n"
			      "}\n");
	}

	sb.safePrintf(
		      "HTTP/1.0 %"INT32" (%s)\r\n"
		      "Content-Length: %"INT32"\r\n"
		      "Connection: Close\r\n"
		      "Content-Type: %s\r\n"
		      "Date: %s UTC\r\n\r\n"
		      ,
		      error  ,
		      errmsg ,

		      xb.length(),

		      ct ,
		      tt ); // ctime ( &now ) ,


	sb.safeMemcpy ( &xb );

	// . move the reply to a send buffer
	// . don't make sendBuf bigger than g_conf.m_httpMaxSendBufSize
	//int32_t msgSize    = gbstrlen ( msg );
	// record it
	if ( bytesSent ) *bytesSent = sb.length();//sendBufSize;
	// use this new function that will compress the reply now if the
	// request was a ZET instead of a GET mdw
	return sendReply2 ( sb.getBufStart() , sb.length() , NULL , 0 , s );

	/*
	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . this will destroy s on error
	//if ( ! m_tcp.sendMsg ( s           , 
	if (  ! tcp->sendMsg ( s           , 
			       sendBuf     ,
			       sendBufSize , 
			       sendBufSize , // sendBufUsed
			       sendBufSize , // msgTotalSize (total to send)
			       NULL        , // callbackData passed 2 cleanUp()
			       cleanUp     ) )
		return false;
	// . TcpServer will free bufs on recycle/destruction
	// . free all if sendReply() did not block
	// mfree ( sendBuf , sendBufSize );
	return true;
	*/
}
bool HttpServer::sendQueryErrorReply( TcpSocket *s , int32_t error , 
				      char *errmsg, 
				      //int32_t  rawFormat, 
				      char format ,
				      int errnum, char *content) {

	// just use this for now. it detects the format already...
	return sendErrorReply ( s,error,errmsg,NULL);

	/*
	// clear g_errno so the send goes through
	g_errno = 0;
	// get time in secs since epoch
	time_t now = getTime();
	// . buffer for the MIME request and brief html err msg
	// . NOTE: ctime appends a \n to the time, so we don't need to
	char msg[2048];
	char *tt = asctime(gmtime ( &now ));
	tt [ gbstrlen(tt) - 1 ] = '\0';
	// fix empty strings
	if (!content) content = "";

	// sanity check
	if ( strncasecmp(errmsg,"Success",7)==0 ) {char*xx=NULL;*xx=0;}

	if ( format == FORMAT_HTML ) {
		// Page content
		char cbuf[1024];
		sprintf (cbuf, 
			 "<html><head><title>Gigablast Error</title></head>"
			 "<body><b>Error = %s</b><br><br>"
			 "%s"
			 "</body></html>\n", 
			 errmsg, content);
		// Header and content prepared for sending
		sprintf ( msg , 
			  "HTTP/1.0 %"INT32" (%s)\r\n"
			  "Content-Length: %i\r\n"
			  "Connection: Close\r\n"
			  "Content-type: text/html; charset=utf-8\r\n"
			  "Date: %s UTC\r\n\r\n"
			  "%s",
			  error  ,
			  errmsg ,
			  gbstrlen(cbuf),
			  tt , 
			  cbuf );
	}
	// XML error msg.  This needs to contain information that is useful to 
	// an xml parser (and not break it with invalid syntax)
	else{ 
		// Page content
		char cbuf[1024];
		sprintf (cbuf, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			 "<error status=\"%d\" msg=\"%s\">"
			 "%s"
			 "</error>\n",
			 (int)error, errmsg, content);
		sprintf ( msg , 
			  "HTTP/1.0 %"INT32" (%s)\r\n"
			  "Content-Length: %i\r\n"
			  "Connection: Close\r\n"
			  "Content-type: text/xml; charset=utf-8\r\n"
			  "Date: %s\r\n\r\n"
			  "%s",
			  error  ,
			  errmsg ,
			  gbstrlen(cbuf),
			  tt , // ctime ( &now ) ,
			  cbuf );

	}
	// . move the reply to a send buffer
	// . don't make sendBuf bigger than g_conf.m_httpMaxSendBufSize
	int32_t msgSize    = gbstrlen ( msg );

	return sendReply2 ( msg , msgSize , NULL , 0 , s );
	*/

	/*
	int32_t sendBufSize = msgSize;
	char *sendBuf    ;
	//if ( sendBufSize <= TCP_READ_BUF_SIZE )
	//	sendBuf = s->m_tmpBuf;
	//else 
	sendBuf = (char *) mmalloc ( sendBufSize , "HttpServer");
	// . this is the ONLY situation in which we destroy "s" ourselves
	// . that is, when we cannot even transmit the server-side error info
	if ( ! sendBuf ) { 
		log("http: Failed to allocate %"INT32" bytes for send "
		    "buffer. Send failed.",sendBufSize);
		// set this so it gets destroyed
		s->m_waitingOnHandler = false;
		//m_tcp.destroySocket ( s );
		tcp->destroySocket ( s );
		return true;
	}
	gbmemcpy ( sendBuf , msg , msgSize );
	// erase g_errno for sending
	g_errno = 0;
	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . this will destroy s on error
	//if ( ! m_tcp.sendMsg ( s           , 
	if (  ! tcp->sendMsg ( s           , 
			       sendBuf     ,
			       sendBufSize , 
			       sendBufSize , // sendBufUsed
			       sendBufSize , // msgTotalSize (total to send)
			       NULL        , // callbackData passed 2 cleanUp()
			       cleanUp     ) )
		return false;
	// . TcpServer will free bufs on recycle/destruction
	// . free all if sendReply() did not block
	// mfree ( sendBuf , sendBufSize );
	return true;
	*/
}

// . this is called by the Loop class
// . getMsgPiece() is called by TcpServer cuz we set it in TcpServer::init()
void getMsgPieceWrapper ( int fd , void *state ) {
	// NOTE: this socket 's' may have been closed/destroyed,
	// so let's use the fd on the tcpSocket
	//TcpSocket *s  = (TcpSocket *) state;
	int32_t sd = (int32_t)(PTRTYPE) state;
 loop:
	// ensure Socket has not been destroyed by callCallbacks()
	TcpSocket *s = g_httpServer.m_tcp.m_tcpSockets[sd] ;
	// return if it has been destroyed (cleanUp() should have been called)
	if ( ! s ) return;
	// read some file into the m_sendBuf of s
	int32_t n = getMsgPiece ( s );
	// return if nothing was read
	if ( n == 0 ) return;
	// . now either n is positive, in which case we read some
	// . or n is negative and g_errno is set
	// . send a ready-to-write signal on s->m_sd
	// . g_errno may be set in which case TcpServer::writeSocketWrapper()
	//   will destroy s and call s's callback, cleanUp()
	g_loop.callCallbacks_ass ( false /*for reading?*/, sd );
	// break the loop if n is -1, that means error in getMsgPiece()
	if ( n < 0 ) {
		log(LOG_LOGIC,"http: getMsgPiece returned -1.");
		return;
	}
	// keep reading more from file and sending it as int32_t as file didn't
	// block
	goto loop;
}

// . this is called by the Loop class
// . getMsgPiece() is called by TcpServer cuz we set it in TcpServer::init()
void getSSLMsgPieceWrapper ( int fd , void *state ) {
	// NOTE: this socket 's' may have been closed/destroyed,
	// so let's use the fd on the tcpSocket
	//TcpSocket *s  = (TcpSocket *) state;
	int32_t sd = (int32_t)(PTRTYPE) state;
 loop:
	// ensure Socket has not been destroyed by callCallbacks()
	TcpSocket *s = g_httpServer.m_ssltcp.m_tcpSockets[sd] ;
	// return if it has been destroyed (cleanUp() should have been called)
	if ( ! s ) return;
	// read some file into the m_sendBuf of s
	int32_t n = getMsgPiece ( s );
	// return if nothing was read
	if ( n == 0 ) return;
	// . now either n is positive, in which case we read some
	// . or n is negative and g_errno is set
	// . send a ready-to-write signal on s->m_sd
	// . g_errno may be set in which case TcpServer::writeSocketWrapper()
	//   will destroy s and call s's callback, cleanUp()
	g_loop.callCallbacks_ass ( false /*for reading?*/, sd );
	// keep reading more from file and sending it as int32_t as file didn't
	// block
	goto loop;
}

// . returns number of new bytes read
// . returns -1 on error and sets g_errno
// . if this gets called then the maxReadBufSize (128k?) was exceeded
// . this is called by g_loop when "f" is ready for reading and is called
//   by TcpServer::writeSocket() when it needs more of the file to send it
int32_t getMsgPiece ( TcpSocket *s ) {
	// get the server this socket uses
	TcpServer *tcp = s->m_this;
	//CallbackData *cd = (CallbackData *) s->m_callbackData;
	File *f = (File *) s->m_state;
	// . sendReply() above should have opened this file
	// . this will be NULL if we get a signal on this file before
	//   TcpServer::sendMsg() was called to send the MIME reply
	// . in that case just return 0
	if ( ! f ) {
		g_errno = EBADENGINEER;
		return log(LOG_LOGIC,"http: No file ptr.") - 1;
	}
	// if the we've sent the entire buffer already we should reset it
	if ( s->m_sendBufUsed == s->m_sendOffset ) {
		s->m_sendBufUsed = 0;
		s->m_sendOffset  = 0;
	}
	// how much can we read into the sendBuf?
	int32_t avail = s->m_sendBufSize - s->m_sendBufUsed;
	// where do we read it into
	char *buf  = s->m_sendBuf     + s->m_sendBufUsed;
	// get current read offset of f
	int32_t pos = f->getCurrentPos();
	// now do a non-blocking read from the file
	int32_t n = f->read ( buf , avail , -1/*current read offset*/ );
	// cancel EAGAIN errors (not really errors)
	if ( g_errno == EAGAIN ) { g_errno = 0; n = 0; }
	// return -1 on real read errors
	if ( n < 0 ) return -1;
	// mark how much sendBuf is now used
	s->m_sendBufUsed += n;
	// how much do we still have to send?
	int32_t needToSend      = s->m_totalToSend - s->m_totalSent;
	// how much is left in buffer to send
	int32_t inBufferToSend  = s->m_sendBufUsed - s->m_sendOffset;
	// if we read all we need from disk then no need to register 
	// since we did not block this time
	if ( needToSend == inBufferToSend ) {
		// do a quick filter of google.com to gxxgle.com to
		// prevent those god damned google map api pop ups
		char *p = s->m_sendBuf;
		char *pend = p + s->m_sendBufUsed;
		// skip if not a doc.234567 filename format
		if ( ! gb_strcasestr(f->getFilename(),"/doc." ) ) p = pend;
		// do the replace
		for ( ; p < pend ; p++ ) {
			if ( strncasecmp(p,"google",6)) continue;
			// replace it
			p[1]='x';
			p[2]='x';
		}
		return n;
	}
	// if the current read offset of f was not 0 then no need to register
	// because we should have already registered last call
	if ( pos != 0 ) return n;
	// debug msg
	log(LOG_DEBUG,"http: Register file fd: %i.",f->getfd());
	// . TODO: if we read less than we need, register a callback for f HERE
	// . this means we block, and cleanUp should be called to unregister it
	// . this also makes f non-blocking
	if (tcp->m_useSSL) {
		if ( g_loop.registerReadCallback ( f->getfd(), 
						   (void *)(PTRTYPE)(s->m_sd),
						   getSSLMsgPieceWrapper ,
						   s->m_niceness ) )
			return n;
	}
	else {
		if ( g_loop.registerReadCallback ( f->getfd(), 
						   (void *)(PTRTYPE)(s->m_sd),
						   getMsgPieceWrapper ,
						   s->m_niceness ) )
			return n;
	}
	// . TODO: deal with this better
	// . if the register failed then panic
	log("http: Registration failed.");
	exit ( -1 );
}

// . we call this to try to figure out the size of the WHOLE HTTP msg
//   being recvd so that we might pre-allocate memory for it
// . it could be an HTTP request or reply
// . this is called upon reception of every packet
//   of the msg being read until a non-negative msg size is returned
// . this is used to avoid doing excessive reallocs and extract the
//   reply size from things like "Content-Length: xxx" so we can do
//   one alloc() and forget about having to do more...
// . up to 128 bytes of the reply can be stored in a static buffer
//   contained in TcpSocket, until we need to alloc...
int32_t getMsgSize ( char *buf, int32_t bufSize, TcpSocket *s ) {
	// . if the msg ends in \r\n0\r\n\r\n that's an end delimeter
	// . this is part of HTTP/1.1's "chunked transfer encoding" thang
	/*
	if ( bufSize >= 7 ) {
		if ( buf [ bufSize - 1 ] == '\n' &&
		     buf [ bufSize - 2 ] == '\r' &&
		     buf [ bufSize - 3 ] == '\n' &&
		     buf [ bufSize - 4 ] == '\r' &&
		     buf [ bufSize - 5 ] == '0'  &&
		     buf [ bufSize - 6 ] == '\n' &&
		     buf [ bufSize - 7 ] == '\r'   )  
			return bufSize;
	}
	*/
	// make sure we have a \n\n or \n\c\n\c or \c\c
	int32_t i;
	int32_t mimeSize = 0;
	for ( i = 0 ; i < bufSize ; i++ ) {
		if ( buf[i] != '\r' && buf[i] != '\n' ) continue;
		// boundary check
		if ( i + 1 >= bufSize ) continue;
		// prepare for a smaller mime size
		mimeSize = i+2;
		// \r\r
		if ( buf[i  ] == '\r' && buf[i+1] == '\r' ) break;
		// \n\n
		if ( buf[i  ] == '\n' && buf[i+1] == '\n' ) break;
		// boundary check
		if ( i + 3 >= bufSize ) continue;
		// prepare for a larger mime size
		mimeSize = i+4;
		// \r\n\r\n
		if ( buf[i  ] == '\r' && buf[i+1] == '\n' &&
		     buf[i+2] == '\r' && buf[i+3] == '\n'  ) break;
		// \n\r\n\r
		if ( buf[i  ] == '\n' && buf[i+1] == '\r' &&
		     buf[i+2] == '\n' && buf[i+3] == '\r'  ) break;
	}
	// return -1 if could not find the end of the MIME
	if ( i == bufSize ) {
		// some mutha fuckas could send us a never ending mime!
		if ( bufSize < 20*1024 ) return -1;
		// in that case bitch about it
		log("http: Could not find end of HTTP MIME in at least "
		    "the first 20k. Truncating MIME to %"INT32" bytes.",
		     bufSize);
		return bufSize;
	}

	// if it is "HTTP/1.0 200 Connetion established\r\n\r\n" we are done
	// it is just a mime reply and no content. then we need to send
	// the https request, encrypted, to the proxy... because our tunnel
	// is now established
	if ( s->m_tunnelMode == 1 )
		return mimeSize;

	// . don't read more than this many bytes!
	// . this may change if we detect a Content-Type: field in the MIME
	int32_t max = s->m_maxTextDocLen + 10*1024;
	if ( s->m_maxTextDocLen == -1 ) max = 0x7fffffff;
	// hey, now, requests can have this to if they're POSTs
	// is it a reply?
	bool hasContent = ( bufSize>=4  && buf[0]=='H' && buf[1]=='T' && 
			                   buf[2]=='T' && buf[3]=='P' );
	// POSTs have content as well
	bool isPost = false;
	if ( ! hasContent && bufSize>=4 && buf[0]=='P' && buf[1]=='O' && 
	                                   buf[2]=='S' && buf[3]=='T' ) {
		hasContent = true;
		// don't allow posts of more than 100k
		max = 200*1024;
		// point to requested url path
		char *pp    = buf + 5;
		char *ppend = buf + bufSize;
		while ( pp < ppend && is_wspace_a(*pp) ) pp++;
		// if post is a /inject allow 10 megs
		if ( pp + 7 < ppend && strncmp ( pp ,"/inject" , 7 ) == 0 )
			max = 0x7fffffff; // maxOtherDocLen not available
		if ( pp + 13 < ppend && strncmp ( pp ,"/admin/inject" ,13)==0 )
			max = 0x7fffffff; // maxOtherDocLen not available
		// if post is a /cgi/12.cgi (tagdb) allow 10 megs
		//if ( pp + 11 < ppend && strncmp ( pp ,"/cgi/12.cgi",11)==0)
		if ( pp + 12 < ppend && strncmp ( pp ,"/admin/tagdb",12)==0)
			max = 0x7fffffff;;//10*1024*1024;
		if ( pp + 4 < ppend && strncmp ( pp ,"/vec",4)==0)
			max = 0x7fffffff;
		// /admin/basic etc
		if ( pp + 7 < ppend && strncmp ( pp ,"/admin/",7)==0)
			max = 0x7fffffff;
		// bulk job. /v2/bulk or /v3/crawl/download/token-name...
		if ( pp + 4 < ppend && strncmp ( pp ,"/v",2)==0 &&
		     // /v2/bulk
		     ( ( pp[4] == 'b' && pp[5] == 'u' ) ||
		       // /v19/bulk
		       ( pp[5] == 'b' && pp[6] == 'u' ) ||
		       // /vN/crawl
		       ( pp[4] == 'c' && pp[5] == 'r' ) ||
		       // /vNN/crawl
		       ( pp[5] == 'c' && pp[6] == 'r' ) ) )
			max = 0x7fffffff;
		// flag it as a post
		isPost = true;
	}
	// if has no content then it must end  in \n\r\n\r or \r\n\r\n
	if ( ! hasContent ) return bufSize;

	// look for a Content-Type: field because we now limit how much
	// we read based on this
	char *p          = buf;
	char *pend       = buf + i;
	// if it is pointless to partially download the doc then
	// set allOrNothing to true
	bool  allOrNothing = false;
	for ( ; p < pend ; p++ ) {
		if ( *p != 'c' && *p != 'C' ) continue;
		if ( p + 13 >= pend ) break;
		if ( strncasecmp ( p, "Content-Type:", 13 ) != 0 ) continue;
		p += 13; while ( p < pend && is_wspace_a(*p) ) p++;
		if ( p + 9 < pend && strncasecmp ( p,"text/html" , 9 )==0)
			continue;
		if ( p + 10 < pend && strncasecmp( p,"text/plain",10)==0)
			continue;
		// . we cannot parse partial PDFs cuz they have a table at the 
		//   end that gets cutoff
		// . but we can still index their url components and link
		//   text and it won't take up much space at all, so we might
		//   as well index that at least.
		if ( p + 15 < pend && strncasecmp( p,"application/pdf",15)==0)
			allOrNothing = true;
		if ( p + 15 < pend&&strncasecmp(p,"application/x-gzip",18)==0)
			allOrNothing = true;
		// adjust "max to read" if we don't have an html/plain doc
		// this can be pdf or xml etc.
		if ( ! isPost ) {
			max = s->m_maxOtherDocLen + 10*1024 ;
			if ( s->m_maxOtherDocLen == -1 ) max = 0x7fffffff;
			// overflow? we added 10k to it make sure did not
			// wrap around to a negative number
			if ( max<s->m_maxOtherDocLen && s->m_maxOtherDocLen>0 )
				max = s->m_maxOtherDocLen;
		}
	}

	// // if it is a warc or arc.gz allow it for now but we should
	// // only allow one spider at a time per host
	if ( s->m_sendBuf ) {
		char *p = s->m_sendBuf;
		char *pend = p + s->m_sendBufSize;
		if ( strncmp(p,"GET /",5) == 0 ) p += 4;
		// find end of url we are getting
		char *e = p;
		for ( ; *e && e < pend && ! is_wspace_a(*e) ; e++ );
		if ( e - 8 > p && strncmp(e-8,".warc.gz", 8 ) == 0 )
			max = 0x7fffffff;
		if ( e - 7 > p && strncmp(e-7, ".arc.gz", 7 ) == 0 )
			max = 0x7fffffff;
	}

	int32_t contentSize = 0;
	int32_t totalReplySize = 0;

	// now look for Content-Length in the mime
	int32_t j; for ( j = 0; j < i ; j++ ) {
		if ( buf[j] != 'c' && buf[j] != 'C' ) continue;
		if ( j + 16 >= i ) break;
		if ( strncasecmp ( &buf[j], "Content-Length:" , 15 ) != 0 )
			continue;
		contentSize = atol2 ( &buf[j+15] , i - (j+15) );
		totalReplySize = contentSize + mimeSize ;
		break;
	}

	// all-or-nothing filter
	if ( totalReplySize > max && allOrNothing ) {
		log(LOG_INFO,
		    "http: reply/request size of %"INT32" is larger "
		    "than limit of %"INT32". Cutoff documents "
		    "of this type are useless. "
		    "Abandoning.",totalReplySize,max);
		// do not read any more than what we have
		return bufSize;
	}
	// warn if we received a post that was truncated
	if ( totalReplySize > max && isPost ) {
		log("http: Truncated POST request from %"INT32" "
		    "to %"INT32" bytes. Increase \"max other/text doc "
		    "len\" in Spider Controls page to prevent this.",
		    totalReplySize,max);
	}
	// truncate the reply if we have to
	if ( totalReplySize > max ) {
		log("http: truncating reply of %"INT32" to %"INT32" bytes",
		    totalReplySize,max);
		totalReplySize = max;
	}
	// truncate if we need to
	if ( totalReplySize )
		return totalReplySize;

	// if it is a POST request with content but no content length...
	// we don't know how big it is...
	if ( isPost ) {
		log("http: Received large POST request without Content-Length "
		    "field. Assuming request size is %"INT32".",bufSize);
		return bufSize;
	}
	// . we only need to be read shit after the \r\n\r\n if it is a 200 OK
	//   HTTP status, right? so get the status
	// . this further fixes the problem of the befsr81 router presumably
	//   dropping TCP FIN packets. The other fix I did was to close the
	//   connection ourselves after Content-Length: bytes have been read.
	// . if first char we read is not H, stop reading
	if ( buf[0] != 'H' ) return bufSize;
	// . skip over till we hit a space
	// . only check the first 20 chars
	for ( i = 4 ; i < bufSize && i < 20 ; i++ ) if ( buf[i] == ' ' ) break;
	// skip additional spaces
	while ( i < bufSize && i < 20 && buf[i] == ' ' ) i++;
	// 3 digits must follow the space, otherwise, stop reading
	if ( i + 2 >= bufSize      ) return bufSize;
	if ( i + 2 >= 20           ) return bufSize;
	if ( ! is_digit ( buf[i] ) ) return bufSize;
	// . if not a 200 then we can return bufSize and not read anymore
	// . we get a lot of 404s here from getting robots.txt's
	if ( buf[i+0] != '2'  ) return bufSize;
	if ( buf[i+1] != '0'  ) return bufSize;
	if ( buf[i+2] != '0'  ) return bufSize;
	// . don't read more than total max
	// . http://autos.link2link.com/ was sending us an infinite amount
	//   of content (no content-length) very slowly
	if ( bufSize >= max ) return bufSize;
	// otherwise, it is a 200 so we need a content-length field or
	// we just have to wait for server to send us a FIN (close socket).
	// if it's not there return -1 for unknown, remote server should close
	// his connection on send completion then... sloppy
	return -1; 
}

// . this table is used to permit or deny access to the serps
// . by embedding a dynamic "key" in every serp url we can dramatically reduce
//   abuse by seo robots
// . the key is present in the <form> on every page with a query submission box
// . the key is dependent on the query (for getting next 10, etc) and the day.
// . a new key is generated every 12 hours.
// . only the current key and the last key will be valid.
// . the key on the home page use a NULL query, so it only changes once a day.
// . we allow any one IP block to access up to 10 serps per day without having
//   the correct key. This allows people with text boxes to gigablast to
//   continue to provide a good service.
// . we do not allow *any* accesses to s=10+ or n=10+ pages w/o a correct key.
// . we also allow any site searches to work without keys.
// . unfortunately, toolbars will not allow more than 10 searches per day.
// . byt seo robots will be limited to ten searches per day per IP block. Some
//   seos have quite a few IP blocks but this should be fairly rare.
// . if permission is denied, we should serve up a page with the query in
//   a text box so the user need only press submit and his key will be correct
//   and the serp will be served immediately. also, state that if he wants
//   to buy a search feed he can do so. AND to obey robots.txt, and people
//   not obeying it risk having their sites permanently banned from the index.
// . the only way a spammer can break this scheme is to download the homepage
//   once every day and parse out the key. it is fairly easy to do but most
//   spammers are using "search engine commando" and do not have that level
//   of programming ability.
// . to impede a spammer parsing the home page, the variable name of the 
//   key in the <form> changes every 12 hours as well. it should be
//   "k[0-9][a-z]" so it does not conflict with any other vars.
// . adv.html must be dynamically generated.
// . this will help us stop the email extractors too!! yay!
// . TODO: also allow banning by user agents...

// support up to 100,000 or so ips in the access table at one time
#define AT_SLOTS 1000*150

// . how many freebies per day?
// . an ip domain can do this many queries without a key
#define AT_FREEBIES 15

void HttpServer::getKey ( int32_t *key, char *kname, 
			   char *q , int32_t qlen , int32_t now , int32_t s , int32_t n ) {
	// temp debug
	//*key=0; kname[0]='k'; kname[1]='x'; kname[2]='x'; kname[3]=0;
	//return;
	getKeys ( key , NULL , kname , NULL , q , qlen , now , s , n );
}

// . get the correct key for the current time and query
// . first key is the most recent, key2 is the older one
// . PageResults should call this to embed the keys
void HttpServer::getKeys ( int32_t *key1, int32_t *key2, char *kname1, char *kname2,
			   char *q , int32_t qlen , int32_t now , int32_t s , int32_t n ) {
	// debug msg
	//log("q=%s qlen=%"INT32" now/32=%"INT32" s=%"INT32" n=%"INT32"",q,qlen,now/32,s,n);
	// new base key every 64 seconds
	int32_t now1 = now / 64;
	int32_t now2 = now1 - 1;
	// we don't know what query they will do ... so reset this to 0
	// unless they are doing a next 10
	if ( s == 0 ) qlen = 0;
	uint32_t h  = hash32 ( q , qlen );
	h = hash32h ( s , h );
	h = hash32h ( n , h );
	int32_t h1 = hash32h ( now1 , h );
	int32_t h2 = hash32h ( now2 , h );
	// get rid of pesky negative sign, and a few digits
	h1 &= 0x000fffff;
	h2 &= 0x000fffff;
	// set the keys first
	if ( key1 ) *key1 = h1;
	if ( key2 ) *key2 = h2;
	// key name changes based on time
	kname1[0] = 'k';
	kname1[1] = '0' + now1 % 10;
	kname1[2] = 'a' + now1 % 26;
	kname1[3] = '\0';
	// debug msg
	//log("kname1=%s v1=%"UINT32"",kname1,*key1);

	if ( ! kname2 ) return;
	kname2[0] = 'k';
	kname2[1] = '0' + now2 % 10;
	kname2[2] = 'a' + now2 % 26;
	kname2[3] = '\0';
}

// . returns false if permission is denied
// . HttpServer should log it as such, and the user should be presented with
//   a query submission page so he can get the right key by hitting "blast it"
// . q/qlen may be a 6 byte docid, not just a query.
bool HttpServer::hasPermission ( int32_t ip , HttpRequest *r , 
				 char *q , int32_t qlen , int32_t s , int32_t n ) {
	// time in seconds since epoch
	time_t now ;//= getTimeGlobal(); //time(NULL);
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();
	// get the keys name/value pairs that are acceptable
	int32_t key1 , key2 ;
	char kname1[4], kname2[4];
	// debug msg
	//log("get keys for permission: q=%s qlen=%"INT32"",q,qlen);
	getKeys (&key1,&key2,kname1,kname2,q,qlen,now,s,n);
	// what value where in the http request?
	int32_t v1 = r->getLong ( kname1 , 0 );
	int32_t v2 = r->getLong ( kname2 , 0 );

	// for this use just the domain
	//uint32_t h = iptop ( s->m_ip );
	uint32_t h = ipdom ( ip );

	// init the table
	if ( ! s_init ) {
		s_htable.set ( AT_SLOTS );
		s_init = true;
	}

	// do they match? if so, permission is granted
	if ( v1 == key1 || v2 == key2 ) {
		// only reset the count if s = 0 so bots don't reset it when
		// they do a Next 10
		if ( s != 0 ) return true;
		// are they in the table?
		int32_t slotNum = s_htable.getSlot ( h );
		// if so, reset freebie count
		if ( slotNum >= 0 ) s_htable.setValue ( slotNum , 1 );
		return true;
	}

	// debug msg
	//log("NO!!!! input--> kname1=%s kname2=%s v1=%"INT32" v2=%"INT32"",
	//   kname1,kname2,v1,v2);

	// . you always need key if you specifiy s= or n=, no freebies for that
	// . no, cuz they could wait a minute before hitting "Next 10" and pass
	//   in an expired key
	//if ( r->getLong ( "s" ,  0 ) !=  0 ) return false;
	//if ( r->getLong ( "n" , 10 ) != 10 ) return false;

	// . clean out table every 3 hours
	// . AT_FREEBIES allowed every 3 hours
	if ( now - s_lastTime > 3*60*60 ) {
		s_lastTime = now;
		s_htable.clear();
	}
	// . if table almost full clean out ALL slots
	// . TODO: just clean out oldest slots
	int32_t partial = (AT_SLOTS * 90) / 100 ;
	if ( s_htable.getNumSlotsUsed() > partial ) s_htable.clear ();
	// . how many times has this IP domain submitted?
	// . allow 10 times per day
	int32_t val = s_htable.getValue ( h );
	// if over 24hr limit then bail
	if ( val >= AT_FREEBIES ) return false;
	// otherwise, inc it
	val++;
	// add to table, will replace old values if already in there
	s_htable.addKey ( h , val );
	return true;
}

// . called by functions that generate dynamic pages to send to client browser
// . send this page
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . cacheTime default is 0, which tells browser to use local caching rules
// . status should be 200 for all replies except POST which is 201
bool HttpServer::sendDynamicPage ( TcpSocket *s           ,
				   char      *page        ,
				   int32_t       pageLen     ,
				   int32_t       cacheTime   ,
				   bool       POSTReply   ,
				   char      *contentType ,
				   int32_t       httpStatus  ,
				   char      *cookie      ,
				   char      *charset      ,
				   HttpRequest *hr ) {
	// how big is the TOTAL page?
	int32_t contentLen = pageLen; // headerLen + pageLen + tailLen;
	// get the time for a mime
	time_t now ;//= getTimeGlobal();
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();
	// guess contentype
	char *ct = contentType;
	if ( ! ct ) {
		if ( page && pageLen > 10 && strncmp(page,"<?xml",5)==0)
			ct = "text/xml";
	}
	// make a mime for this contentLen (no partial send)
	HttpMime m;
	// . the default is a cacheTime of -1, which means NOT to cache at all
	// . if we don't supply a cacheTime (it's 0) browser caches for its
	//   own time (maybe 1 minute?)
	// . the page's mime will have "Pragma: no-cache\nExpiration: -1\n"
	m.makeMime ( contentLen  , 
		     cacheTime   , // 0-->local cache rules,-1 --> NEVER cache
		     now         , // last modified
		     0           , // offset
		     -1          , // bytes to send
		     NULL        , // extension of file we're sending
		     POSTReply   , // is this a post reply?
		     ct, // contentType ,
		     charset     , // charset
		     httpStatus  ,
		     cookie      );

	return sendReply2 ( m.getMime(),m.getMimeLen(),page,pageLen,s,
			    false, hr);


	/*
	// get mime length
	int32_t mimeLen = m.getMimeLen();
	// 0 content length for POST replies
	//if ( POSTReply ) { *page='X'; }; //contentLen = 0; pageLen = 0; }
	// get total bytes to send
	int32_t sendBufAlloc = mimeLen + contentLen;
	// int16_tcut
	int32_t ht = g_hostdb.m_myHost->m_type;
	// did requester want a compressed reply?
	char *rb = s->m_readBuf;
	// special forwarding case
	if ( (ht & HT_PROXY) && *rb == 'Z' ) sendBufAlloc = pageLen;
	// extra room?
	//if ( s->m_udpSlot ) sendBufSize += 12;
	// make a sendBuf
	char *sendBuf    ;
	//if ( sendBufSize <= TCP_READ_BUF_SIZE )
	//	sendBuf = s->m_tmpBuf;
	//else 
	sendBuf = (char *) mmalloc ( sendBufAlloc , "HttpServer2");
	// destroy s on mmalloc() error and return
	if ( ! sendBuf ) 
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	// p is a moving ptr into "sendBuf"
	unsigned char *p    = (unsigned char *)sendBuf;
	unsigned char *pend = (unsigned char *)sendBuf + sendBufAlloc;
	// by default assign size to what was allocated
	int32_t sendBufSize = sendBufAlloc;
	// we swap out the GET for a ZET
	bool doCompression = ( *rb == 'Z' );
	// only grunts do the compression now to prevent proxy overload
	if ( ! ( ht & HT_GRUNT) ) doCompression = false;

	// if we are fielding a request for a qcproxy's 0xfd request,
	// then compress the reply before sending back.
	if ( doCompression ) {
		// store uncompressed size1 and size1
		*(int32_t *)p = mimeLen + pageLen; p += 4;
		// bookmarks
		int32_t *saved1 = (int32_t *)p; p += 4;
		int32_t *saved2 = (int32_t *)p; p += 4;
		uint32_t  used1 = pend - p;
		int err1 = gbcompress ( p , &used1, 
					(unsigned char *)m.getMime(),mimeLen );
		if ( err1 != Z_OK )
			log("http: error compressing mime reply.");
		p += used1;
		// update bookmark
		*saved1 = used1;
		// then store the page content
		uint32_t used2 = pend - p;
		int err2 = gbcompress(p,&used2,(unsigned char *)page,pageLen );
		if ( err2 != Z_OK )
			log("http: error compressing content reply.");
		p += used2;
		// update bookmark
		*saved2 = used2;
		// note it
		logf(LOG_DEBUG,"http: compressing. after=%"INT32"",
		     (int32_t)(((char *)p)-sendBuf));
		// change size
		sendBufSize = (char *)p - sendBuf;
	}
	// if we are a proxy, and not a compression proxy, then just forward
	// the blob as-is if it is a "ZET" (GET-compressed=ZET)
	else if ( (ht & HT_PROXY) && *rb == 'Z' ) {
		gbmemcpy ( sendBuf , page , pageLen );
		// sanity check
		if ( sendBufSize != pageLen ) { char *xx=NULL;*xx=0; }
		// note it
		logf(LOG_DEBUG,"http: forwarding. pageLen=%"INT32"",pageLen);
	}
	else {
		// copy mime into sendBuf first
		gbmemcpy ( p , m.getMime() , mimeLen );
		p += mimeLen;
		// then the page
		gbmemcpy ( p , page , pageLen );
		p += pageLen;
		// sanity check
		if ( sendBufSize != pageLen+mimeLen ) { char *xx=NULL;*xx=0;}
	}

	// . send if off
	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	//if ( ! m_tcp.sendMsg ( s           , 
	if (  ! tcp->sendMsg ( s           , 
			       sendBuf     ,
			       sendBufAlloc,
			       sendBufSize ,
			       sendBufSize ,
			       NULL        , 
			       cleanUp     ) ) return false;
	// . free all if sendReply() did not block
	// . socket should have been reset() (destroyed on g_errno) byTcpServer
	// . TcpServer will free bufs on recycle/destruction
	// mfree ( sendBuf , sendBufSize );
	return true;
	*/
}


/*
bool HttpServer::addToQueue(TcpSocket *s, HttpRequest *r, int32_t page) {
	if(m_lastSlotUsed == MAX_REQUEST_QUEUE) {
		//not enough room to handle another request!
		g_errno = ETRYAGAIN;
		return false;
	}
	QueuedRequest* qr = &m_requestQueue[m_lastSlotUsed++];
	qr->m_s = s;
	qr->m_r.copy(r);
	qr->m_page = page;
	return true;
}


bool HttpServer::callQueuedPages() {
	if(m_lastSlotUsed == 0) return true;
	for(int32_t i = 0; i < m_lastSlotUsed; i++) {
		QueuedRequest* qr = &m_requestQueue[i];
		g_pages.sendDynamicReply (qr->m_s , &qr->m_r , qr->m_page);
	}
	m_lastSlotUsed = 0;
	return true;
}
*/

TcpSocket *HttpServer::unzipReply(TcpSocket* s) {
	//int64_t start = gettimeofdayInMilliseconds();

	HttpMime mime;
	if(!mime.set(s->m_readBuf,s->m_readOffset, NULL)) {
		g_errno = EBADMIME;
		return s;
	}

	// . return if not zipped,
	// . or sometimes we get an empty reply that claims its gzipped
	int32_t zipLen = s->m_readOffset - mime.getMimeLen();
	if(mime.getContentEncoding() != ET_GZIP ||
	   zipLen < (int)sizeof(gz_header)) { 
		m_bytesDownloaded += zipLen;
		m_uncompressedBytes += zipLen;
		return s;
	}

	//int32_t newSize = getGunzippedSize(s->m_readBuf, s->m_readOffset);
	int32_t newSize = *(int32_t*)(s->m_readBuf + s->m_readOffset - 4);

	if(newSize < 0 || newSize > 500*1024*1024) {
		log("http: got bad gzipped reply1 of size=%"INT32".",
		    newSize );
		g_errno = ECORRUPTHTTPGZIP;//CORRUPTDATA;//EBADREPLYSIZE;
		return s;
	}
	//if the content is just the gzip header and footer, then don't
	//bother unzipping
	if(newSize == 0) {
		s->m_readOffset = mime.getMimeLen();
		return s;
	}

	//make the buffer to hold the new header and uncompressed content
	int32_t need = mime.getMimeLen() + newSize + 64;
	char *newBuf = (char*)mmalloc(need, "HttpUnzip");
	if(!newBuf) {
		g_errno = ENOMEM;
		return s;
	}
	char *pnew = newBuf;
	char *mimeEnd = s->m_readBuf + mime.getMimeLen();

	// copy header to the new buffer, do it in two parts, since we
	// have to modify the encoding and content length as we go.
	// Basically we are unzipping the http reply into a new buffer here,
	// so we need to rewrite the Content-Length: and the 
	// Content-Encoding: http mime field values so they are no longer
	// "gzip" and use the uncompressed content-length.
	char *ptr1 = mime.getContentEncodingPos();
	char *ptr2 = mime.getContentLengthPos();
	char *ptr3 = NULL;

	// change the content type based on the extension before the
	// .gz extension since we are uncompressing it
	char *p = s->m_sendBuf + 4;
	char *pend = s->m_sendBuf + s->m_sendBufSize;
	const char *newCT = NULL;
	char *lastPeriod = NULL;
	// get the extension, if any, before the .gz
	for ( ; *p && ! is_wspace_a(*p) && p < pend ; p++ ) {
		if ( p[0] != '.' ) continue;
		if ( p[1] != 'g' ) { lastPeriod = p; continue; }
		if ( p[2] != 'z' ) { lastPeriod = p; continue; }
		if ( ! is_wspace_a(p[3]) ) { lastPeriod = p; continue; }
		// no prev?
		if ( ! lastPeriod ) break;
		// skip period
		lastPeriod++;
		// back up
		newCT = extensionToContentTypeStr2 (lastPeriod,p-lastPeriod);
		// this is NULL if the file extension is unrecognized
		if ( ! newCT ) break;
		// this should be like text/html or
		// WARC/html or something like that...
		ptr3 = mime.getContentTypePos();
		break;
	}

	// this was writing a number at the start of the mime and messing
	// up our squid proxy implementation. so take out. MDW 10/2/2014
	//if ( ! ptr1 )
	//	ptr1 = s->m_readBuf;


	char *src = s->m_readBuf;

	// sometimes they are missing Content-Length:

 subloop:

	char *nextMin = (char *)-1;
	if ( ptr1 && (ptr1 < nextMin || nextMin==(char *)-1)) nextMin = ptr1;
	if ( ptr2 && (ptr2 < nextMin || nextMin==(char *)-1)) nextMin = ptr2;
	if ( ptr3 && (ptr3 < nextMin || nextMin==(char *)-1)) nextMin = ptr3;

	// if all ptrs are NULL then copy the tail
	if ( nextMin == (char *)-1 ) nextMin = mimeEnd;

	// copy ptr1 to src
	gbmemcpy ( pnew, src, nextMin - src );
	pnew += nextMin - src;
	src  += nextMin - src;
	// store either the new content encoding or new length
	if ( nextMin == mime.getContentEncodingPos()) {
		pnew += sprintf(pnew, " identity");
		ptr1 = NULL;
	}
	else if ( nextMin == mime.getContentLengthPos() ) {
		pnew += sprintf(pnew, " %"INT32"",newSize);
		ptr2 = NULL;
	}
	else if ( nextMin == mime.getContentTypePos() ) {
		pnew += sprintf(pnew," %s",newCT);
		ptr3 = NULL;
	}

	// loop for more
	if ( nextMin < mimeEnd ) {
		// scan to \r\n at end of that line we replace
		while ( *src != '\r' && *src != '\n') src++;
		goto subloop;
	}


	// copy the rest
	// gbmemcpy ( pnew , src , mimeEnd - src );
	// pnew += mimeEnd - src;
	// src  += mimeEnd - src;


	// before restLen was negative because we were skipping over
	// leading \n's in the document body because the while loop above
	// was bad logic
	// if ( restLen < 0 || ! ptr1 ) {
	// 	log("http: got bad gzipped reply2 of size=%"INT32".",
	// 	    newSize );
	// 	mfree (newBuf, need, "HttpUnzipError");
	// 	g_errno = ECORRUPTHTTPGZIP;
	// 	return s;
	// }
		
	// gbmemcpy(pnew, pold, restLen);
 	// pold += restLen;
 	// pnew += restLen;

	uint32_t uncompressedLen = newSize;
	int32_t compressedLen = s->m_readOffset-mime.getMimeLen();

	int zipErr = gbuncompress((unsigned char*)pnew, &uncompressedLen,
				  (unsigned char*)src, 
				  compressedLen);
	

	if(zipErr != Z_OK ||
	   uncompressedLen != (uint32_t)newSize) {
		log("http: got gzipped unlikely reply of size=%"INT32".",
		    newSize );
		mfree (newBuf, need, "HttpUnzipError");
		g_errno = ECORRUPTHTTPGZIP;//EBADREPLYSIZE;
		return s;
	}
	mfree (s->m_readBuf, s->m_readBufSize, "HttpUnzip");
	pnew += uncompressedLen;
	if(pnew - newBuf > need - 2 ) {char *xx=NULL;*xx=0;}
	*pnew = '\0';
	//log("http: got compressed doc, %f:1 compressed "
	//"(%"INT32"/%"INT32"). took %"INT64" ms",
	//(float)uncompressedLen/compressedLen, 
	//uncompressedLen,compressedLen,
	//gettimeofdayInMilliseconds() - start);

	m_bytesDownloaded += compressedLen;
	m_uncompressedBytes += uncompressedLen;


	s->m_readBuf = newBuf;
	s->m_readOffset = pnew - newBuf;
	s->m_readBufSize = need;
	return s;
}


bool printFrontPageShell ( SafeBuf *sb , char *tabName , CollectionRec *cr ,
			   bool printGigablast );

bool sendPagePretty ( TcpSocket *s , 
		      HttpRequest *r , 
		      char *filename ,
		      char *tabName ) {

	SafeBuf sb;

	CollectionRec *cr = g_collectiondb.getRec ( r );

	// print the chrome
	printFrontPageShell ( &sb , tabName , cr , true ); //  -1=pagenum

	SafeBuf ff;
	ff.safePrintf("html/%s",filename);

	SafeBuf tmp;
	tmp.fillFromFile ( g_hostdb.m_dir , ff.getBufStart() );

	sb.safeStrcpy ( tmp.getBufStart() );


	// done
	sb.safePrintf("\n</html>");

	char *charset = "utf-8";
	char *ct = "text/html";
	g_httpServer.sendDynamicPage ( s      , 
				       sb.getBufStart(), 
				       sb.length(), 
				       25         , // cachetime in secs
				       // pick up key changes
				       // this was 0 before
				       false      , // POSTREply? 
				       ct         , // content type
				       -1         , // http status -1->200
				       NULL, // cookiePtr  ,
				       charset    );
	return true;
}	


/*
bool sendPageApi ( TcpSocket *s , HttpRequest *r ) {

	SafeBuf sb;

	sb.safePrintf(
		      "<html>"
		      "<span style=\"font-size:16px;\">"
		      "<br>"
		      "For developers that require the highest level "
		      "of control, "
		      "we can return search results in XML "
		      "format using a url like: "
		      "<a href=/?xml=1&q=test>"
		      "http://www.procog."
		      "com/?xml=1&q=test</a>"
		      "<br><br>"
		      "The only thing we request is that if you use "
		      "this then you please place a link like<br>"
		      "<i><u>"
		      "&lt;a href=http://www.procog.com/&gt;"
		      "Search Results&lt;/a&gt; powered by procog.com"
		      "</u></i>"
		      "<br>on the page that is displaying the events."
		      "<br><br>"
		      "<a href=#output>Jump to the XML Output "
		      "Description</a>"
		      "<br><br>"
		      "<table cellpadding=10>"
		      "<tr height=22px><td colspan=10 class=gradgreen>"
		      "<center><b>Input Parameters</b></center>"
		      "</td></tr>"
		      );
	sb.safePrintf ("<tr><td><b>Name</b></td>"
		       "<td><b>Value</b></td>"
		       "<td><b>Default</b></td>"
		       //"<td><b>Note</b></td>"
		       "<td><b>Description</b></td>"
		       "</tr>\n" );
	
	int32_t count = 0;
	// from SearchInput.cpp:
	for ( int32_t i = 0 ; i < g_parms.m_numParms ; i++ ) {
		//Parm *parm = g_parms.m_searchParms[i];
		Parm *parm = &g_parms.m_parms[i];
		// check if we should print it...
		if ( ! ( parm->m_flags & PF_API ) ) continue;
		//if ( parm->m_flags & PF_SUBMENU_HEADER ) continue;
		//if ( ! parm->m_flags ) continue;
		// print it
		//if ( ! parm->m_sparm ) continue;
		// use m_cgi if no m_scgi
		char *cgi = parm->m_cgi;
		if ( parm->m_scgi ) cgi = parm->m_scgi;
		// alternat bg color
		char *bgcolor = "";
		if ( ++count % 2 == 1 )
			bgcolor = " bgcolor=lightgray";
		// print the parm
		sb.safePrintf ( "<tr><td%s><b>%s</b></td>"
				"<td%s>", bgcolor,cgi,bgcolor );
		if ( parm->m_type == TYPE_BOOL2 ) 
			sb.safePrintf ( "0 or 1" );
		else if ( parm->m_type == TYPE_BOOL ) 
			sb.safePrintf ( "0 or 1" );
		else if ( parm->m_type == TYPE_CHAR ) 
			sb.safePrintf ( "CHAR" );
		else if ( parm->m_type == TYPE_CHAR2 ) 
			sb.safePrintf ( "CHAR" );
		else if ( parm->m_type == TYPE_FLOAT ) 
			sb.safePrintf ( "FLOAT" );
		else if ( parm->m_type == TYPE_IP ) 
			sb.safePrintf ( "IP" );
		else if ( parm->m_type == TYPE_LONG ) 
			sb.safePrintf ( "INT32" );
		else if ( parm->m_type == TYPE_LONG_LONG ) 
			sb.safePrintf ( "INT64" );
		else if ( parm->m_type == TYPE_STRING ) 
			sb.safePrintf ( "STRING" );
		else if ( parm->m_type == TYPE_STRINGBOX ) 
			sb.safePrintf ( "STRING" );
		else
			sb.safePrintf ( "OTHER" );
		
		char *def = parm->m_def;
		if ( ! def ) def = "";
		char *desc = parm->m_desc;
		if ( ! desc || ! desc[0] ) 
			desc = "Show events from this category.";
		sb.safePrintf ( "</td>"
				"<td%s>%s</td>" // default
				//"<td nowrap=1>%s</td>"
				"<td%s><font size=-1>%s</font>"
				"</td></tr>\n",
				//parm->m_title, 
				bgcolor,
				def,
				bgcolor,
				desc );
	}
	// close it up
	sb.safePrintf ( "</table><br><br>" );
	
	
	sb.safePrintf ( "<a name=output></a>");
	
	sb.safePrintf("</span>"
		      "<span style=\"font-size:14px;\">"
		      );
	
	
	sb.safePrintf("<table cellpadding=10>"
		      "<tr height=22px><td colspan=10 "
		      "class=gradorange>"
		      "<center><b>XML Output Description</b></center>"
		      "</td></tr>"
		      "<tr><td>"
		      );
	
	// . print out a sample in preformatted xml
	// . just use &xml=1&help=1 then each field will be
	//   described in line!!
	// . so we can use an iframe for this...
	// . or just do a query and paste it into here!!!
	char *xmlOutput = 
		
		"# The first xml tag is the response tag.\n"
		"<response>\n"
		"\n"
		
		"# This is the current time in UTC that was used for the "
		"query\n"
		"<currentTimeUTC>1332453466</currentTimeUTC>\n"
		"\n"

		"# This is the latitude and longitude location of the user.\n"
		"# Based on the user's IP address or her GPS coords\n"
		"# or based on the city derived from the entered location\n"
		"<userCityLat>35.0844879</userCityLat>\n"
		"<userCityLon>-106.6511383</userCityLon>\n"
		"\n"

		"# This is the lat/lon bounding box to which the search\n"
		"# was constrained.\n"
		"<boxLatCenter>35.0844955</boxLatCenter>\n"
		"<boxLonCenter>-106.6511383</boxLonCenter>\n"
		"<boxRadius>100.00</boxRadius>\n"
		"<boxMinLat>33.6352234</boxMinLat>\n"
		"<boxMaxLat>36.5337677</boxMaxLat>\n"
		"<boxMinLon>-108.1004105</boxMinLon>\n"
		"<boxMaxLon>-105.2018661</boxMaxLon>\n"
		"\n"

		"# This is how long the search took in milliseconds\n"
		"<responseTime>0</responseTime>\n"
		"\n"

		"# This is how many events are contained in this response\n"
		"<hits>20</hits>\n"
		"\n"

		"# This is '1' if more events follow or 0 if not\n"
		"<moreResultsFollow>0</moreResultsFollow>\n"
		"\n"

		"# This is the current time in the timezone of that of\n"
		"# the majority of the events in the contained herein\n"
		"<serpCurrentLocalTime>"
		"<![CDATA[ Thu Mar 22, 3:57 PM MDT ]]>"
		"</serpCurrentLocalTime>\n"
		"\n"
		
		"# This is the indicator of an event\n"
		"<result>\n"
		"\n"

		"# This is the title of the event\n"
		"<eventTitle>"
		"<![CDATA[ UNM'S Poli Sci 101: Pimping IS Easy! ]]>"
		"</eventTitle>\n"
		"\n"

		"# This is a 32-bit hash of the event's summary\n"
		"<eventSummaryHash32>328692420</eventSummaryHash32>\n"
		"\n"

		"# This is the summary of the event\n"
		"<eventDesc>\n"
		"\t<![CDATA["
		"\tNot just by cheering me on when I announce myself or the general radness I've come to expect from you, but the fact that there was a friggin' waiting list of teams to buy me drinks. All of you knew Vanilla Ice, which I suppose says something about the longevity of crap."
		"]]>\n"
		"<![CDATA["
		"Sorry Lauryn Hill, a white boy named Bob Van Winkle has stood the <font style=\"color:black;background-color:yellow\">test</font> of time better than you."
		"\t]]>\n"
		"</eventDesc>\n"
		"\n"

		"# This represents all the date intervals the event occurs\n"
		"# at over the next year or two. The two numbers in each\n"
		"# closed interval are UNIX timestamps in UTC.\n"
		"# There can be hundreds of intervals.\n"
		"<eventDateIntervalsUTC>"
		"<![CDATA["
		"[1332471600,1332471600],[1333076400,1333076400],[1333681200,1333681200],"
		"]]>"
		"</eventDateIntervalsUTC>\n"
		"\n"

		"# This is how long until the next occurence of this event\n"
		"<eventCountdown>"
		"<![CDATA[ in 5 hours 2 minutes on Thu, Mar 22 @ 9pm ]]>"
		"</eventCountdown>\n"
		"\n"

		"# This is the canonical time of the event\n"
		"<eventEnglishTime>"
		"<![CDATA[ every Thursday at 9 pm ]]>"
		"</eventEnglishTime>\n"
		"\n"

		"# This is the canonical time of the event, truncated, in\n"
		"# case it is a huge list of times.\n"
		"<eventEnglishTimeTruncated>"
		"<![CDATA[ every Thursday at 9 pm ]]>"
		"</eventEnglishTimeTruncated>\n"
		"\n"

		"# This is the url the event came from\n"
		"<url>"
		"<![CDATA["
		"http://www.geekswhodrink.com/index.cfm?event=client.page&pageid=90&contentid=2146"
		"\t]]>"
		"</url>\n"
		"\n"

		"# This is the url of the cached copy of that page\n"
		"<cachedUrl>"
		"<![CDATA["
		"/?id=229952262607.3596429002840667676"
		"]]>"
		"</cachedUrl>\n"
		"\n"

		"# This is the size of that web page in Kilobytes\n"
		"<size>63.0</size>\n"
		"\n"

		"# This is the numeric Document Identifier of that page\n"
		"<docId>229952262607</docId>\n"
		"\n"

		"# The timzone the event is in. Goes from -11 to 11.\n"
		"<eventTimeZone>-7</eventTimeZone>\n"
		"\n"

		"# Does the location the event is in use Daylight "
		"Savings Time?\n"
		"<eventCityUsesDST>1</eventCityUsesDST>\n"
		"\n"

		"# When is the next start time of the event in UTC?\n"
		"# If event is in progress, this is when it started\n"
		"<eventStartTimeUTC>1332457200</eventStartTimeUTC>\n"
		"\n"

		"# When is the next end time of ths event in UTC?\n"
		"<eventEndTimeUTC>1332464400</eventEndTimeUTC>\n"
		"\n"

		"# When is the next start monthday, month and year of the\n"
		"# event in UTC? If event is in progress, this is when it\n"
		"# started\n"
		"<eventStartMonthDay>22</eventStartMonthDay>\n"
		"<eventStartMonth>3</eventStartMonth>\n"
		"<eventStartYear>2012</eventStartYear>\n"
		"\n"

		"# How far away is the event in Manhattan distance?\n"
		"<drivingMilesAway>8.6</drivingMilesAway>\n"
		"\n"

		"# The address broken down into separate tags\n"
		"# The state here is always a two character abbreviation.\n"
		"# The country here is always a two character abbreviation.\n"
		"<eventVenue>"
		"<![CDATA[ Wendy's ]]>"
		"</eventVenue>\n"
		"<eventStreet>"
		"<![CDATA[ 410 Eubank Boulevard Northeast ]]>"
		"</eventStreet>\n"
		"<eventCity>"
		"<![CDATA[ Albuquerque ]]</b>"
		"</eventCity>\n"
		"<eventState>"
		"<![CDATA[ NM ]]>"
		"</eventState>\n"
		"<eventCountry>"
		"<![CDATA[ US ]]>"
		"</eventState>\n"
		"\n"


		"# The latitude and longitude of the event according to\n"
		"# The geocoder, if available.\n"
		"<eventGeocoderLat>35.0784380</eventGeocoderLat>\n"
		"<eventGeocoderLon>-106.5322530</eventGeocoderLon>\n"
		"\n"

		"# The latitude and longitude of the centroid of the city\n"
		"# that the event is taking place in, if available\n"
		"<eventCityLat>35.0844917</eventCityLat>\n"
		"<eventCityLon>-106.6511383</eventCityLon>\n"
		"\n"

		"# The final latitude and longitude of the event\n"
		"# You can count on this to be there.\n"
		"<eventBalloonLetter>A</eventBalloonLetter>\n"
		"<eventBalloonLat>35.0784380</eventBalloonLat>\n"
		"<eventBalloonLon>-106.5322530</eventBalloonLon>\n"
		"\n"

		"# The website the event came from and the 32-bit\n"
		"# hash of that site and its domain.\n"
		"<site>www.facebook.com</site>\n"
		"<siteHash32>1486592848</siteHash32>\n"
		"<domainHash32>2890457068</domainHash32>\n"
		"\n"

		"# The last time the event was spidered. A UNIX timestamp\n"
		"# in UTC.\n"
		"<spiderDate>1332346009</spiderDate>\n"
		"\n"

		"# The last time the event was successfully indexed\n"
		"# A UNIX timestamp in UTC.\n"
		"<indexDate>1332346009</indexDate>\n"
		"\n"

		"# The content type of the page the event was indexed from.\n"
		"# Values can be 'xml' or 'html'\n"
		"<contentType>xml</contentType>\n"
		"\n"

		"# The language of the page the event was on.\n"
		"# Values are the standard two-letter abbreviations, like\n"
		"# 'en' for english. It uses 'Unknown' if unknown.\n"
		"<language>"
		"<![CDATA[ Unknown ]]>"
		"</language>\n"
		"\n"

		"# That does it!\n"
		"</result>\n"

		"</response>\n";

	// reserve an extra 3k
	sb.reserve ( 25000 );
	
	// get ptr
	char *dst = sb.getBuf();
	char *src = xmlOutput;
	
	bool inBold = false;
	bool inFont = false;
	
	// copy into sb, but rewrite \n as <br> and
	// < as &lt; and > as &gt;
	for ( ; *src ; src++ ) {
		if ( *src == '#' ) {
			gbmemcpy ( dst,"<font color=gray>",17);
			dst += 17;
			inFont = true;
		}
		if ( *src == '<' ) {
			gbmemcpy ( dst , "&lt;",4);
			dst += 4;
			// boldify start tags
			//if ( src[1] != '/' && src[1] !='!' ) {
			//	gbmemcpy(dst,"<b>",3);
			//	dst += 3;
			//	inBold = true;
			//}
			continue;
		}
		else if ( *src == '>' ) {
			// end bold tags
			if ( inBold ) {
				gbmemcpy(dst,"</b>",4);
				dst += 4;
				inBold = false;
			}
			gbmemcpy ( dst , "&gt;",4);
			dst += 4;
			continue;
		}
		else if ( *src == '\n' ) {
			if ( inFont ) {
				gbmemcpy(dst,"</font>",7);
				dst += 7;
				inFont = false;
			}
			gbmemcpy ( dst , "<br>",4);
			dst += 4;
			continue;
		}
		// default
		*dst = *src;
		dst++;
	}
	// just in case
	*dst = '\0';
	// update sb length
	sb.m_length = dst - sb.getBufStart();
	
	
	sb.safePrintf("</td></tr></table>");
	sb.safePrintf ( "</span>" );
	sb.safePrintf ( "</html>" );

	char *charset = "utf-8";
	char *ct = "text/html";
	g_httpServer.sendDynamicPage ( s      , 
				       sb.getBufStart(), 
				       sb.length(), 
				       25         , // cachetime in secs
				       // pick up key changes
				       // this was 0 before
				       false      , // POSTREply? 
				       ct         , // content type
				       -1         , // http status -1->200
				       NULL, // cookiePtr  ,
				       charset    );
	return true;
}
*/

////////////////////
//
// support for Gigablast acting like a squid proxy
//
// handle requests like:
//
// GET http://www.xyz.com/
// Proxy-Authentication: Basic xxxxxx
//
/////////////////////

#define PADDING_SIZE 3000

// in honor of squid
class SquidState {
public:
	TcpSocket *m_sock;
	// store the ip here
	int32_t m_ip;
	// not really needed but saves a malloc
	DnsState m_dnsState;

	Msg13 m_msg13;
	Msg13Request m_request;
	// padding for large requests with cookies and shit so we can store
	// the request into m_request.m_url and have it overflow some.
	// IMPORTANT: this array declaration must directly follow m_request.
	char m_padding[PADDING_SIZE];
};

static void gotSquidProxiedUrlIp ( void *state , int32_t ip );

// did gigablast receive a request like:
// GET http://www.xyz.com/
// Proxy-authorization: Basic abcdefghij
//
// so we want to fetch that document using msg13 which will send the
// request to the appropriate host in the cluster based on the url's ip.
// that host will check its document cache for it and return that if there.
// otherwise, that host will download it.
// that host will also use the spider proxies (floaters) if specified,
// to route the request through the appropriate spider proxy.
bool HttpServer::processSquidProxyRequest ( TcpSocket *sock, HttpRequest *hr) {

	// debug note
	log("http: got squid proxy request from client at %s",
	    iptoa(sock->m_ip));

	// we need the actual ip of the requested url so we know
	// which host to send the msg13 request to. that way it can ensure
	// that we do not flood a particular IP with too many requests at once.

	// sanity
	if ( hr->m_squidProxiedUrlLen > MAX_URL_LEN )
		// what is ip lookup failure for proxy?
		return sendErrorReply ( sock,500,"Url too long (via Proxy)" );

	int32_t maxRequestLen = MAX_URL_LEN + PADDING_SIZE;
	if ( sock->m_readOffset >= maxRequestLen )
		// what is ip lookup failure for proxy?
		return sendErrorReply(sock,500,"Request too long (via Proxy)");

	SquidState *sqs;
	try { sqs = new (SquidState) ; }
	// return true and set g_errno if couldn't make a new File class
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("squid: new(%"INT32"): %s",
		    (int32_t)sizeof(SquidState),mstrerror(g_errno));
		return sendErrorReply(sock,500,mstrerror(g_errno)); 
	}
	mnew ( sqs, sizeof(SquidState), "squidst");

	// init
	sqs->m_ip = 0;
	sqs->m_sock = sock;

	// normalize the url. MAX_URL_LEN is an issue...
	Url url;
	url.set ( hr->m_squidProxiedUrl , hr->m_squidProxiedUrlLen );

	// get hostname for ip lookup
	char *host = url.getHost();
	int32_t hlen = url.getHostLen();

	// . return false if this blocked
	// . this returns true and sets g_errno on error
	if ( ! g_dns.getIp ( host,
			     hlen,
			     &sqs->m_ip,
			     sqs,
			     gotSquidProxiedUrlIp,
			     &sqs->m_dnsState) )
		return false;

	// error?
	if ( g_errno ) {
		mdelete ( sqs, sizeof(SquidState), "sqs");
		delete (sqs);
		// what is ip lookup failure for proxy?
		return sendErrorReply ( sock , 404 , "Not Found (via Proxy)" );
	}
	
	// must have been in cache using sqs->m_ip
	gotSquidProxiedUrlIp ( sqs , sqs->m_ip );
	
	// assume maybe incorrectly that this blocked! might have had the
	// page in cache as well...
	return false;
}

static void gotSquidProxiedContent ( void *state ) ;

void gotSquidProxiedUrlIp ( void *state , int32_t ip ) {
	SquidState *sqs = (SquidState *)state;

	// send the exact request. hide in the url buf i guess.
	TcpSocket *sock = sqs->m_sock;


	// if ip lookup failed... return 
	if ( ip == 0 || ip == -1 ) {
		mdelete ( sqs, sizeof(SquidState), "sqs");
		delete (sqs);
		// what is ip lookup failure for proxy?
		g_httpServer.sendErrorReply(sock,404,"Not Found (via Proxy)");
		return;
	}


	// pick the host to send the msg13 to now based on the ip
	Msg13Request *r = &sqs->m_request;
	
	// clear everything
	r->reset();

	r->m_urlIp = ip;

	// let msg13 know to just send the request in m_url
	r->m_isSquidProxiedUrl = true;

	// char *proxiedReqBuf = r->ptr_url;

	// // store into there
	// gbmemcpy ( proxiedReqBuf,
	// 	 sqs->m_sock->m_readBuf,
	// 	 // include +1 for the terminating \0
	// 	 sqs->m_sock->m_readOffset + 1);

	// send the whole http request mime to the msg13 handling host
	r->ptr_url = sqs->m_sock->m_readBuf;

	// include terminating \0. well it is already i think. see
	// Msg13Request::getSize(), so no need to add +1
	r->size_url = sqs->m_sock->m_readOffset + 1;

	// sanity
	if ( r->ptr_url && r->ptr_url[r->size_url-1] ) { char *xx=NULL;*xx=0;}

	// use urlip for this, it determines what host downloads it
	r->m_firstIp                = r->m_urlIp;
	// other stuff
	r->m_maxCacheAge            = 3600; // for page cache. 1hr.
	r->m_urlHash48              = 0LL;
	r->m_maxTextDocLen          = -1;//maxDownload;
	r->m_maxOtherDocLen         = -1;//maxDownload;
	r->m_forwardDownloadRequest = false;
	r->m_useTestCache           = false;
	r->m_spideredTime           = 0;
	r->m_ifModifiedSince        = 0;
	r->m_skipHammerCheck        = 0;
	// . this is -1 if unknown. none found in robots.txt or provided
	//   in the custom crawl parms.
	// . it should also be 0 for the robots.txt file itself
	// . since we are a proxy and have no idea, let's default to 0ms
	r->m_crawlDelayMS = 0;
	// let's time our crawl delay from the initiation of the download
	// not from the end of the download. this will make things a little
	// faster but could slam servers more.
	r->m_crawlDelayFromEnd = false;
	// new stuff
	r->m_contentHash32 = 0;
	// turn this off too
	r->m_attemptedIframeExpansion = false;
	// turn off
	r->m_useCompressionProxy = false;
	r->m_compressReply       = false;
	r->m_isCustomCrawl       = 0;

	// force the use of the floaters now otherwise they might not be used
	// if you have the floaters disabled on the 'proxy' page
	r->m_forceUseFloaters = 1;

	// log for now
	log("proxy: getting proxied content for req=%s",r->ptr_url);

	// isTestColl = false. return if blocked.
	if ( ! sqs->m_msg13.getDoc ( r, false ,sqs, gotSquidProxiedContent ) ) 
		return;

	// we did not block, send back the content
	gotSquidProxiedContent ( sqs );
}

//#include "PageInject.h" // Msg7

void gotSquidProxiedContent ( void *state ) {
	SquidState *sqs = (SquidState *)state;

	// send back the reply
	Msg13 *msg13 = &sqs->m_msg13;
	char *reply = msg13->m_replyBuf;
	int32_t  replySize = msg13->m_replyBufSize;
	int32_t replyAllocSize = msg13->m_replyBufAllocSize;

	TcpSocket *sock = sqs->m_sock;

	if ( ! reply ) {
		log("proxy: got empty reply from webserver. setting g_errno.");
		g_errno = EBADREPLY;
	}

	// if it timed out or something...
	if ( g_errno ) {
		log("proxy: proxy reply had error=%s",mstrerror(g_errno));
		mdelete ( sqs, sizeof(SquidState), "sqs");
		delete (sqs);
		// what is ip lookup failure for proxy?
		g_httpServer.sendErrorReply(sock,505,"Timed Out (via Proxy)");
		return;
	}


	// another debg log
	if ( g_conf.m_logDebugProxies ) {
		int32_t clen = 500;
		if ( clen > replySize ) clen = replySize -1;
		if ( clen < 0 ) clen = 0;
		char c = reply[clen];
		reply[clen]=0;
		log("proxy: got proxied reply=%s",reply);
		reply[clen]=c;
	}

	// don't let Msg13::reset() free it
	sqs->m_msg13.m_replyBuf = NULL;

	// sanity, this should be exact... since TcpServer.cpp needs that
	//if ( replySize != replyAllocSize ) { char *xx=NULL;*xx=0; }

	mdelete ( sqs, sizeof(SquidState), "sqs");
	delete  ( sqs );

	// . the reply should already have a mime at the top since we are
	//   acting like a squid proxy, send it back...
	// . this should free the reply when done
	TcpServer *tcp = &g_httpServer.m_tcp;
	tcp->sendMsg ( sock ,
		       reply ,  // sendbuf
		       replyAllocSize ,  // bufsize
		       replySize , // used
		       replySize , // msgtotalsize
		       NULL , // state
		       NULL ); // donesendingcallback
}

