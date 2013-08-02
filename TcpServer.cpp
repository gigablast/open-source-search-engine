#include "gb-include.h"

#include "TcpServer.h"
#include "Stats.h"
#include "Profiler.h"
#include "PingServer.h"
//#include "AutoBan.h"

// . TODO: deleting nodes from under Loop::callCallbacks is dangerous!!

static void gotTcpServerIpWrapper           ( void *state , long ip ) ;
static void readSocketWrapper      ( int sd , void *state ) ;
static void writeSocketWrapper     ( int sd , void *state ) ;
static void readTimeoutPollWrapper ( int sd , void *state ) ;
static void acceptSocketWrapper    ( int sd , void *state ) ;
static void timePollWrapper        ( int fd , void *state ) ;

static void logSSLError(SSL *ssl, int ret) {
	switch (SSL_get_error(ssl, ret)) {
	case SSL_ERROR_NONE:
		log("net: ssl: No Error.");
		break;
	case SSL_ERROR_ZERO_RETURN:
		log ("net: ssl: Error: Zero Return");
		break;
	case SSL_ERROR_WANT_READ:
		log ("net: ssl: Error: Want Read");
		break;
	case SSL_ERROR_WANT_WRITE:
		log ("net: ssl: Error: Want Write");
		break;
	case SSL_ERROR_WANT_CONNECT:
		log ("net: ssl: Error: Want Connect");
		break;
	//case SSL_ERROR_WANT_ACCEPT:
	//	log ("net: ssl: Error: Want Accept");
	//	break;
	case SSL_ERROR_WANT_X509_LOOKUP:
		log ("net: ssl: Error: Want X509 Lookup");
		break;
	case SSL_ERROR_SYSCALL:
		log ("net: ssl: Error: Syscall");
		break;
	case SSL_ERROR_SSL:
		log ("net: ssl: Error: SSL");
		break;
	}
}

// free all TcpSockets and their bufs
void TcpServer::reset() {
	// set not ready
	m_ready = false;
	// clean up the sockets
	for ( long i = 0 ; i < MAX_TCP_SOCKS ; i++ ) {
		TcpSocket *s = m_tcpSockets[i];
		if ( ! s ) continue;
		destroySocket ( s );
	}
	// do we got a valid listen socket?
	if ( m_sock < 0 ) return;
	// if so, stop listening, may block
	close ( m_sock );
	// shutdown SSL
	if (m_useSSL && m_ctx) {
		SSL_CTX_free(m_ctx);
		// clean up the SSL crap
		ERR_free_strings();
		ERR_remove_state(0);
		m_ctx = NULL;
	}
}

static void sigpipe_handle(int x) {
}

// . port will be incremented if already in use
// . use 1 socket for receiving and sending
// . requestHandler() is called when we read a request on "s"
// . getMsgSize() is called when we read a PACKET(s) on "s" to get the size of
//   the entire request beforehand for allocation purposes
// . getMsgSize() must return -1 if it cannot determine the size of the request
bool TcpServer::init ( void (* requestHandler)(TcpSocket *s) ,
		       long (* getMsgSize    )(char *msg , long msgBytesRead,
					       TcpSocket *s ),
		       long (* getMsgPiece   )(TcpSocket *s ),
		       short      port           , 
		       long      *maxSocketsPtr  ,
		       bool       useSSL         ) {
		       //long       maxReadBufSize , 
		       //long       maxSendBufSize ) {
	// don't be ready until we succeed
	m_ready = false;
	m_doReadRateTimeouts = true;
	// store the handlers
	m_requestHandler = requestHandler;
	m_getMsgSize     = getMsgSize;
	m_getMsgPiece    = getMsgPiece;
	// init the sockets array to hold our TcpSockets
	memset ( m_tcpSockets , 0 , sizeof(TcpSocket *) * MAX_TCP_SOCKS );
	// clear the actual tcp sockets array
	memset ( m_actualSockets , 0 , sizeof(TcpSocket)* MAX_TCP_SOCKS );
	m_lastFilled = 0;
	m_numUsed    = 0;
	// remember our port
	m_port = port;
	// point to dummy if we need to
	m_dummy = MAX_TCP_SOCKS;
	if ( ! maxSocketsPtr                 ) maxSocketsPtr = &m_dummy;
	if (  *maxSocketsPtr > MAX_TCP_SOCKS ) maxSocketsPtr = &m_dummy;
	// we can only have this many sockets open at any one time
	m_maxSocketsPtr = maxSocketsPtr;
	// set the useSSL flag
	m_useSSL = useSSL;

	// claim sd 0 so it is not used
	static FILE *s_stdinSock ;
	static bool  s_openned = false;
	if ( ! s_openned ) {
		s_stdinSock = fopen ( "stdin" , "r" );
		// sanity - make sure 0 was opened as stdin!
		if ( s_stdinSock != NULL ) { 
			log("tcp: stdinSock = %li != 0", (long)s_stdinSock);
			char *xx=NULL;*xx=0;
		}
		s_openned = true;
	}

	// can't exceed hard limit
	//if ( m_maxSockets > MAX_TCP_SOCKS ) m_maxSockets = MAX_TCP_SOCKS;
	// how many bytes we can read into memory before calling putMsgPiece()
	//m_maxReadBufSize     = maxReadBufSize;
	// how many bytes we can hold to send at one time
	//m_maxSendBufSize     = maxSendBufSize;
	// sockaddr_in provides interface to sockaddr
	struct sockaddr_in name; 
	// parm
	int options;
	// if port is -1 don't set up a listening socket
	if ( m_port == -1 || m_port == 0 ) goto skipServer;
	// . set up our connection listening socket
	// . sets g_errno and returns -1 on error
 retry13:
	m_sock  = socket ( AF_INET, SOCK_STREAM , 0 );
	//if ( m_sock == 0 ) log ( "tcp: socket1 gave sd=0");
	while ( m_sock == 0 ) {
		int newSock = socket ( AF_INET, SOCK_STREAM, 0 );
		log ( "tcp: socket gave sd=0, reopenning1 to sd=%i", newSock );
		//::close(m_sock);
		m_sock = newSock;
	}
	if (m_sock < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry13;
		// copy errno to g_errno
		g_errno = errno;
		return log("tcp: Failed to create socket for "
		   	"listening :%s.",mstrerror(g_errno));
	}
	// reset it all just to be safe
	bzero((char *)&name, sizeof(name));
	name.sin_family      = AF_INET;
	name.sin_addr.s_addr = 0; /*INADDR_ANY;*/
	name.sin_port        = htons(port);
	// . we want to re-use port it if we need to restart
	// . sets g_errno and returns -1 on error
 retry14:
	options = 1;
	if(setsockopt(m_sock,SOL_SOCKET,SO_REUSEADDR,&options,sizeof(options))){
		// valgrind
		if ( errno == EINTR ) goto retry14;
		g_errno = errno;
		return false;
	}
 retry15:
	// bind this name to the socket
	if ( bind ( m_sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
		// valgrind
		if ( errno == EINTR ) goto retry15;
		// copy errno to g_errno
		g_errno = errno;
		//if ( g_errno == EINVAL ) { port++; goto again; }
		close ( m_sock );
		return log("tcp: Failed to bind socket on port %li: %s.",
			   (long)port,mstrerror(g_errno));
	}
retry16:
	// now listen for connections
	if (listen ( m_sock , 128 ) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry16;
		// copy errno to g_errno
		g_errno = errno;
		close ( m_sock );
		return log("tcp: Failed to listen on socket: %s.",
		   	mstrerror(g_errno));
	}

	// setup SSL
	if (m_useSSL) {
		// init SSL
  		// older ssl does not use "const". depends on the include files
#if OPENSSL_VERSION_NUMBER <= 0x009080bfL
		SSL_METHOD *meth = NULL;
#else
		const SSL_METHOD *meth = NULL;
#endif
		SSL_library_init();
		SSL_load_error_strings();
		//SSLeay_add_all_algorithms();
		//SSLeay_add_ssl_algorithms();
		signal(SIGPIPE, sigpipe_handle);
		meth = SSLv23_method();
		m_ctx = SSL_CTX_new(meth);
		// get the certificate location
		char sslCertificate[256];
		sprintf(sslCertificate, "%sgb.pem", g_hostdb.m_dir);
		//char sslBundleFilename[256];
		//sprintf(sslBundleFilename, "%sgd_bundle.crt",g_hostdb.m_dir);
		log(LOG_INFO, "https: Reading SSL certificate from: %s", 
		    sslCertificate);
		// Load the keys
		if (m_ctx == NULL)
			return log("ssl: Failed to set up an SSL context\n");
		if (!SSL_CTX_use_certificate_chain_file ( m_ctx,
							  sslCertificate ) )
			return log("ssl: Failed to read certificate file");
		//if ( !SSL_CTX_add_extra_chain_cert ( m_ctx, 
		//				       sslBundleFilename ) )
		//	return log("ssl: Failed to add extra "
		//		   "certificate chain");
		if (!SSL_CTX_use_PrivateKey_file ( m_ctx,
						   sslCertificate,
						   SSL_FILETYPE_PEM ) )
			return log("ssl: Failed to read Private Key File");
		if (!SSL_CTX_load_verify_locations( m_ctx,
						    sslCertificate,
						    0 ) )
			return log("ssl: Failed to read Certificate");
	}
	// . register this fd with the Loop class
	// . this will make it nonBlocking and sigio based 
	// . when m_sock is ready for reading Loop calls acceptSocketWrapper()
	// . this also makes m_sock nonBlocking, etc...
	// . this returns false and sets g_errno if it couldn't register
	// . we do our accept and connect callbacks like a write
	// . accept/connects generate both POLLIN and POLLOUT bands @ same time
	// . use a niceness of 0 so traffic from our server to a browser takes
	//   precedence over spider traffic
	if ( ! g_loop.registerReadCallback (m_sock,this,acceptSocketWrapper,0))
		return false;
	// if port is -1 we skip listening to a socket
 skipServer:
	// . register to receives wake up calls every 500ms so we can
	//   check for TcpSockets that have timed out
	// . check every 500ms now since we have timeout of 1000ms for ads
	if ( ! g_loop.registerSleepCallback (500,this,readTimeoutPollWrapper,0))
		return false;
	if ( ! g_loop.registerSleepCallback (30*1000,this,timePollWrapper,0))
		return false;
	// return true on success
	m_ready = true;
	return true;
}

// this wrapper is called every 15 ms by the Loop class
void timePollWrapper ( int fd , void *state ) { 
	TcpServer *THIS  = (TcpServer *)state;
	if ( g_inSigHandler ) return;
	// close ANY socket that is just reading and OVER 60 secs idle
	THIS->closeLeastUsed( 60 );
}

bool TcpServer::testBind ( unsigned short port ) {
	// assign port for the test
	m_port = port;
	// sockaddr_in provides interface to sockaddr
	struct sockaddr_in name; 
	// parm
	int options;
	// if port is -1 don't set up a listening socket
	if ( m_port == -1 || m_port == 0 ) return true;
	// . set up our connection listening socket
	// . sets g_errno and returns -1 on error
 retry17:
	m_sock  = socket ( AF_INET, SOCK_STREAM , 0 );
	//if ( m_sock == 0 ) log ( "tcp: socket2 gave sd=0");
	while ( m_sock == 0 ) {
		int newSock = socket ( AF_INET, SOCK_STREAM, 0 );
		log ( "tcp: socket gave sd=0, reopenning2 to sd=%i", newSock );
		//::close(m_sock);
		m_sock = newSock;
	}
	if (m_sock < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry17;
		// copy errno to g_errno
		g_errno = errno;
		return log("tcp: Failed to create socket for "
		   	"listening :%s.",mstrerror(g_errno));
	}
	// reset it all just to be safe
	bzero((char *)&name, sizeof(name));
	name.sin_family      = AF_INET;
	name.sin_addr.s_addr = 0; /*INADDR_ANY;*/
	name.sin_port        = htons(port);
	// . we want to re-use port it if we need to restart
	// . sets g_errno and returns -1 on error
 retry18:
	options = 1;
	if(setsockopt(m_sock,SOL_SOCKET,SO_REUSEADDR,&options,
		      sizeof(options))){
		// valgrind
		if ( errno == EINTR ) goto retry18;
		g_errno = errno;
		return false;
	}
retry19:
	// bind this name to the socket
	if ( bind ( m_sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
		// valgrind
		if ( errno == EINTR ) goto retry19;
		// copy errno to g_errno
		g_errno = errno;
		//if ( g_errno == EINVAL ) { port++; goto again; }
		close ( m_sock );
		return log("tcp: Failed to bind socket: %s.",
		   	mstrerror(g_errno));
	}
	close ( m_sock );
	return true;
}


// . we use this temp structure to hold our state while we call g_dns
//   to translate a hostname to an ip 
// . make this into a class now so m_msgc's constructor gets called
class TcpState {
public:
	char       m_hostname[256];
	short      m_port;
	char      *m_sendBuf;
	long       m_sendBufSize;
	long       m_sendBufUsed;
	long       m_msgTotalSize;
	TcpServer *m_this;
	void      *m_state ;
	void    (* m_callback ) ( void *state , TcpSocket *s ) ;
	long       m_timeout;
	long       m_maxTextDocLen;
	long       m_maxOtherDocLen;
	long       m_ip;
	MsgC       m_msgc;
};

bool TcpServer::sendMsg ( char  *url ,
			  char  *sendBuf  ,
			  long   sendBufSize ,
			  long   sendBufUsed ,
			  long   msgTotalSize ,
			  void  *state    ,
			  void  (* callback )( void *state , TcpSocket *s ) ,
			  long   timeout ,
			  long   maxTextDocLen ,  // -1 for no max
			  long   maxOtherDocLen ) {

	Url u;
	u.set ( url );
	char *host = u.getHost();
	long  hostLen = u.getHostLen();
	long  port = u.getPort();
	return sendMsg ( host ,
			 hostLen ,
			 port ,
			 sendBuf ,
			 sendBufSize ,
			 sendBufUsed ,
			 msgTotalSize ,
			 state ,
			 callback ,
			 timeout ,
			 maxTextDocLen ,
			 maxOtherDocLen );
}


// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we do not copy "sendBuf"
// . you must free sendBuf when we call your callback
// . if this returns true you must free sendBuf then
// . if "msgTotalSize" > "sendBufUsed" we should be notified by a routine
//   like HttpServer::getMsgPiece() by having him load the sendBuf and
//   call g_loop.callCallbacks(sd) or something
// . those bytes should be stored in m_sendBuf, but not overwrite what 
//   has not been sent yet
bool TcpServer::sendMsg ( char  *hostname ,
			  long   hostnameLen ,
			  short  port     ,
			  char  *sendBuf  ,
			  long   sendBufSize ,
			  long   sendBufUsed ,
			  long   msgTotalSize ,
			  void  *state    ,
			  void  (* callback )( void *state , TcpSocket *s ) ,
			  long   timeout ,
			  long   maxTextDocLen ,  // -1 for no max
			  long   maxOtherDocLen ) {
	// a quickie
	char *h    = hostname;
	long  hlen = hostnameLen;
	// make sure hostname not too big
	if ( hlen >= 254 ) { 
		g_errno = EBUFTOOSMALL; 
		log(LOG_LOGIC,"tcp: tcpserver: sendMsg: hostname length is "
		    "too big. it's %li, max is 254." , hostnameLen );
		mfree ( sendBuf , sendBufSize ,"TcpServer");
		return true; 
	}
	// . make a state for calling dns server
	// . TODO: speed up by checking dns cache first
	// . TODO: use a TcpSocket structure instead of TcpState to hold this
	// . return true and set g_errno on error
	// . malloc() should set g_errno on error
	//TcpState *tst=(TcpState *) mmalloc (sizeof(TcpState),"TcpServerTS");
	TcpState *tst;
	try { tst = new (TcpState); }
	// bail on failure
	catch ( ... ) { 
		mfree(sendBuf,sendBufSize,"TcpServer"); 
		return true;
	}
	// register this mem with g_mem
	mnew ( tst , sizeof(TcpState) , "TcpServer" );
	// fill up our temporary state structure
	memcpy ( tst->m_hostname , h , hlen );
	// NULL terminate the hostname in tst
	tst->m_hostname [ hlen ] = '\0';
	// set the other members of tst
	tst->m_port           = port;
	tst->m_sendBuf        = sendBuf;
	tst->m_sendBufSize    = sendBufSize;
	tst->m_sendBufUsed    = sendBufUsed;
	tst->m_msgTotalSize   = msgTotalSize;
	tst->m_state          = state;
	tst->m_callback       = callback;
	tst->m_this           = this;
	tst->m_timeout        = timeout;
	tst->m_maxTextDocLen  = maxTextDocLen;
	tst->m_maxOtherDocLen = maxOtherDocLen;

	//very bad. was passing this local var ptr to msgc which blocks!
	//	long ip;
	tst->m_ip = 0;

	// debug
	log(LOG_DEBUG,"tcp: Getting IP for %s using msgc.",
	    tst->m_hostname );
	long status;
	// if no hosts we are being called by monitor.cpp
	if ( g_hostdb.m_numHosts == 0 ||
	     // or if we are spider proxy...
	     g_hostdb.m_myHost->m_isProxy )
		status = g_dns.getIp ( h ,
				       hlen ,
				       &(tst->m_ip),
				       tst ,
				       gotTcpServerIpWrapper );
	// . this returns false if blocks, true otherwise
	// . it also sets g_errno on error
	// . seems like this single msgc's multicast was being shared by
	//   the multiple calls, too... use a private msgc now
	else 
		status = tst->m_msgc.getIp(h,hlen,&(tst->m_ip),tst,
					   gotTcpServerIpWrapper);
	// return false if blocked
	if ( status == 0 ) return false;
	// . gotIp() returns false if blocked, true otherwise
	// . sets g_errno on error
	return gotTcpServerIp ( tst , tst->m_ip ) ;
}

// called by Dns class when ip (or g_errno) is ready
void gotTcpServerIpWrapper ( void *state , long ip ) {
	// our state ptr ptrs to a TcpState struct
	TcpState *tst  = (TcpState *) state;
	// save the callback and state since gotIp frees tst
	void  *tststate = tst->m_state;
	void  (* tstcallback )( void *state , TcpSocket *s );
	tstcallback = tst->m_callback;
	// get ptr to our tcp server
	TcpServer *THIS = tst->m_this;
	// get ip
	//long ip = tst->m_ip;
	// . call gotIp()
	// . return if it blocked (returned false)
	if ( ! THIS->gotTcpServerIp ( tst , ip ) ) return;
	// . tstcallback can be NULL if caller did not care about the reply
	// . now it the transmission was completed w/o further blocking
	// . call the callback
	// . g_errno may be set
	// . we have no TcpSocket at this point, so use NULL
	if ( tstcallback ) tstcallback ( tststate , NULL );
}


// . returns false if TRANSACTION blocked, true otherwise
// . sets g_errno on error
bool TcpServer::gotTcpServerIp ( TcpState *tst , long ip ) {
	// debug
	log(LOG_DEBUG,"tcp: Got ip of %s for %s err=%s.", 
	    iptoa(ip),tst->m_hostname , mstrerror(g_errno) );
	// set g_errno if unable to get ip for this hostname
	if ( ip == 0 ) g_errno = EBADIP;
	// free "ts" and return true on error
	if ( g_errno   ) {
		// we are responsible for freeing the send buffer
		mfree ( tst->m_sendBuf , tst->m_sendBufSize ,"TcpServer");
		//mfree(tst,sizeof(TcpState),"TcpServer"); 
		mdelete ( tst , sizeof(TcpState) , "TcpServer" );
		delete ( tst ) ;
		return true; 
	}
	// . now call the ip-based sendMsg()
	// . this return false if blocked, true otherwise
	// . it also sets g_errno on error
	bool status =  sendMsg ( ip                  ,
				 tst->m_port         ,
				 tst->m_sendBuf      ,
				 tst->m_sendBufSize  ,
				 tst->m_sendBufUsed  ,
				 tst->m_msgTotalSize ,
				 tst->m_state        , 
				 tst->m_callback     ,
				 tst->m_timeout      ,
				 tst->m_maxTextDocLen  ,
				 tst->m_maxOtherDocLen );
	//mfree ( tst , sizeof(TcpState),"TcpServer");
	mdelete ( tst , sizeof(TcpState) , "TcpServer" );
	delete ( tst ) ;
	// return false if this send blocked
	if ( ! status ) return false;
	// if no error then we've blocked on waiting for the reply
	if ( ! g_errno  ) return false;
	// otherwise, return true on error
	return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . NOTE: should not be called by user since does not copy "msg"
// . NOTE: we do not copy "msg" so keep it on your stack
bool TcpServer::sendMsg ( long   ip       ,
			  short  port     ,
			  char  *sendBuf  ,
			  long   sendBufSize ,
			  long   sendBufUsed ,
			  long   msgTotalSize ,
			  void  *state    ,
			  void  (* callback )( void *state, TcpSocket *s) ,
			  long   timeout ,
			  long   maxTextDocLen ,  // -1 for no max
			  long   maxOtherDocLen ) {

	// debug
	log(LOG_DEBUG,"tcp: Getting doc for ip=%s.", iptoa(ip));
	// . get an unused socket that's pre-connected to this ip/port
	// . returns NULL if it can't
	TcpSocket *s = getAvailableSocket ( ip , port );
	// . sendMsg(...) returns false if blocked, true otherwise
	// . it also sets g_errno on error
	if ( s ) return sendMsg ( s            ,
				  sendBuf      ,
				  sendBufSize  ,
				  sendBufUsed  ,
				  msgTotalSize ,
				  state        ,
				  callback     ,
				  timeout      ,
				  maxTextDocLen ,  // -1 for no max
				  maxOtherDocLen );
	// . otherwise, create a new socket
	// . returns NULL and sets g_errno on error
	// . adds socket to array for us and sets the fd non-blocking, etc.
	s = getNewSocket ( );
	// return true if s is NULL and g_errno was set by getNewSocket()
	if ( ! s ) { mfree ( sendBuf , sendBufSize,"TcpServer"); return true; }
	// set up the new TcpSocket for connecting
	s->m_state            = state;
	s->m_callback         = callback;
	s->m_this             = this;
	s->m_ip               = ip;
	s->m_port             = port;
	s->m_sockState        = ST_CONNECTING;
	s->m_sendBuf          = sendBuf;
	s->m_sendBufSize      = sendBufSize;
	s->m_sendBufUsed      = sendBufUsed;
	s->m_totalToSend      = msgTotalSize;
	s->m_sendOffset       = 0;
	s->m_totalSent        = 0;
	s->m_waitingOnHandler = false;
	s->m_timeout          = timeout;
	s->m_maxTextDocLen    = maxTextDocLen ;
	s->m_maxOtherDocLen   = maxOtherDocLen ;
	s->m_ssl              = NULL;
	s->m_udpSlot          = NULL;
	// . call the connect routine to try to connect it asap
	// . this does not block however
	// . this returns false if blocked, true otherwise
	// . it also sets g_errno on error
	// . it should destroy socket on error
	// . TODO: ensure this always blocks, otherwise we must redo this code
	connectSocket ( s ) ;
	// . destroy s on error and return true since we did not block
	// . this will close the socket descriptor and make the callback
	if ( g_errno ) { destroySocket ( s ); return true; }
	// . we're blocking on the reply so return false always
	// . reply can't be gotten until readSocket() is called
	return false;
}

// . returns false if TRANSACTION blocked, true otherwise
// . sets g_errno on error
// . destroys socket, "s",  on error
// . recycles socket, "s", on done writing and reading
// . "s" must be a pre-connected (available) TcpSocket
// . this is called by m_requestHander() to send a reply
// . this is called by sendMsg(ip,...) above to send a request
bool TcpServer::sendMsg ( TcpSocket *s            , 
			  char      *sendBuf      ,
			  long       sendBufSize  ,
			  long       sendBufUsed  ,
			  long       msgTotalSize ,
			  void      *state        ,
			  void     (* callback)(void *state,TcpSocket *s) ,
			  long       timeout      ,
			  long       maxTextDocLen ,  // -1 for no max
			  long       maxOtherDocLen ) {
	//reset any previous g_errno so we don't think it was our call to write
	g_errno = 0;

	// HACK: the proxy encapsulates http requests in udp datagrams with
	//       msgtype 0xfd. so do a udp reply in that case to the proxy.
	if ( s->m_udpSlot ) {
		g_udpServer.sendReply_ass ( sendBuf      ,
					    sendBufUsed  ,
					    sendBuf      ,
					    sendBufSize  ,
					    s->m_udpSlot ,
					    timeout      , // timeout?
					    state        ,
					    NULL         );// callback
		// we now free the read buffer here since PageDirectory.cpp
		// might have reallocated it.
		if ( s->m_readBuf ) 
			mfree (s->m_readBuf, s->m_readBufSize,"TcpUdp");
		// free it! we allocated in HttpServer.cpp handleRequestfd()
		mfree ( s , sizeof(TcpSocket) , "tcpudp" );
		// assume did not block
		return true;
	}

	// reset the parms in the pre-connected TcpSocket, "s"
	s->m_state            = state;
	s->m_callback         = callback;
	// ensure the correct TcpServer
	if (s->m_this != this) {
		log("tcpserver: Socket comming into incorrect TcpServer!");
		char *xx = NULL; *xx = 0;
	}
	s->m_this             = this;
	//	s->m_ip               = ip;
	//	s->m_port             = port;
	s->m_sockState        = ST_WRITING;
	s->m_sendBuf          = sendBuf;
	s->m_sendBufSize      = sendBufSize;
	s->m_sendBufUsed      = sendBufUsed;
	s->m_totalToSend      = msgTotalSize;
	s->m_sendOffset       = 0;
	s->m_totalSent        = 0;
	s->m_waitingOnHandler = false;
	s->m_timeout          = timeout;
	s->m_maxTextDocLen    = maxTextDocLen ;
	s->m_maxOtherDocLen   = maxOtherDocLen ;
	// . try to send immediately
	// . returns false if blocked, true otherwise
	// . sets g_errno on error (and returns true)
	if ( ! writeSocket ( s ) ) return false;
	// . the write completed writing a REPLY OR REQUEST
	// . or g_errno was set
	// . do not make callbacks if it did not block
	//	makeCallback ( s );
	// . destroy the socket on error
	// . this will also unregister all our callbacks for the socket
	// . TODO: deleting nodes from under Loop::callCallbacks is dangerous!!
	if      ( g_errno      ) { destroySocket ( s ); return true; }
	// reset the socket iff it was a reply that we finished writing
	// hmmm else if ( s->m_readBuf ) { recycleSocket ( s ); return true; }
	// we can't close it here any more for some reason the browser truncates
	// the content we transmit otherwise... i've tried SO_LINGER and couldnt get
	// that to work...
	if ( s->m_readBuf ) { s->m_sockState = ST_NEEDS_CLOSE; return true; }
	// we're blocking on the reply (readBuf is empty)
	return false;
}

// . TcpSockets are 1-1 with socket descriptors
// . returns NULL if no available sockets w/ this ip/port were found
TcpSocket *TcpServer::getAvailableSocket ( long ip, short port ) {
	// . search for an available socket already connected to our ip/port
	for ( long i = 0 ; i <= m_lastFilled ; i++ ) {
		TcpSocket *s = m_tcpSockets[i];
		if ( ! s               ) continue;
		if ( s->m_ip   != ip   ) continue;
		if ( s->m_port != port ) continue;
		if ( ! s->isAvailable()) continue;
		// reset the start time
		s->m_startTime      = gettimeofdayInMilliseconds();
		s->m_lastActionTime = gettimeofdayInMilliseconds();
		s->m_shutdownStart  = 0;
		// debug msg
		//log("........... TcpServer found available sock "
		//"%li\n",i);
		return s;
	}
	// return NULL if none pre-connected and available for this ip/port
	return NULL;
}

static long s_lastTime = 0;

// . gets a new TcpSocket
// . returns NULL and set g_errno on error
// . sets socket to non-blocking and sets up signal generation (SIGMINRT)
//   so Loop class can handle the signals and route to our handlers
TcpSocket *TcpServer::getNewSocket ( ) {
	// . if outta sd's we close least used socket first
	// . if they're all in use set g_errno and return NULL
	if ( m_numIncomingUsed >= *m_maxSocketsPtr ) 
		if ( ! closeLeastUsed () ){
			// note it in the log
			long now = getTimeLocal();
			static long s_last = 0;
			static long s_count = 0;
			if ( now - s_last < 5 ) 
				s_count++;
			else {
				log("tcp: Out of sockets. Max sockets = %li. "
				    "(msgslogged=%li)",
				    *m_maxSocketsPtr,s_count);
				s_count = 0;
				s_last = now;
			}
			// another stat
			g_stats.m_closedSockets++;
			g_errno = EOUTOFSOCKETS; 
			// send email alert
			g_pingServer.sendEmailMsg ( &s_lastTime ,
						    "out of sockets on https");
			return NULL;
		}

 retry4:
	// now make a new socket descriptor
	int sd = socket ( AF_INET , SOCK_STREAM , 0 ) ;

	if ( g_conf.m_logDebugTcp )
		logf (LOG_DEBUG,"tcp: ...... created new socket sd=%li",
		      (long)sd);

	//if ( sd == 0 ) log ( "tcp: socket3 gave sd=0");
	while ( sd == 0 ) {
		errno = 0;
		int newSock = socket ( AF_INET, SOCK_STREAM, 0 );
		log ( "tcp: socket gave sd=0, reopenning3 to sd=%i", newSock );
		//::close(sd);
		sd = newSock;
	}
	if ( sd >= MAX_NUM_FDS ) {
		log("tcp: using statically linked libc that only supports "
		    "an fd of up to %li, but got an fd = %li. fd_set is "
		    "only geared for 1024 bits of file descriptors for "
		    "doing poll() in Loop.cpp",
		    (long)MAX_NUM_FDS,(long)sd);
		char *xx=NULL;*xx=0; 
	}
	// return NULL and set g_errno on failure
	if ( sd <  0 ) {
		// valgrind. interrupted system call
		if ( errno == EINTR ) goto retry4;
		// copy errno to g_errno
		g_errno = errno;
		log("tcp: Failed to create new socket: %s.",
		    mstrerror(g_errno));
		log("tcp: numopensocks = %li",m_numUsed);
		log("tcp: try editing /etc/security/limits.conf and "
		    "restarting in fresh shell.");
		log("tcp: try using multiple spider compression proxies on "
		    "same server.");
		return NULL;
	}

	// ssl debug
	//log("tcp: open socket fd=%i",m_sock);

	// . create a new TcpSocket around this socket descriptor
	// . returns NULL and sets g_errno on error
	// . use a maximum niceness for spidering
	TcpSocket *s = wrapSocket ( sd , MAX_NICENESS , false /*incoming?*/) ;
	// . close sd on failure 
	// . TODO: ensure this blocks even if sd was set nonblock by wrapSock()
	if ( ! s ) { 
		if ( sd == 0 ) log("tcp: closing1 sd of 0");
		if ( ::close(sd) == -1 )
			log("tcp: close2(%li) = %s",(long)sd,mstrerror(errno));
		return NULL; 
	}
	// return it on success
	return s;
}	

TcpSocket *TcpServer::getSocket  ( int sd ) {
	TcpSocket *s = m_tcpSockets[sd];
	if ( s ) return s;
	log(LOG_LOGIC,"tcp: tcpserver: getSocket: sd=%i has no TcpSocket.",sd);
	return NULL;
}

// . returns NULL and sets g_errno on error, true otherwise
// . make a TcpSocket around "sd", a socket descriptor
// . makes the socket non-blocking and sets up signal catching
// . Loop class will receives signals and call the handlers we register with
// . the Loop class
// . NOTE: it's up to the caller to fill in the details of the TcpSocket!
TcpSocket *TcpServer::wrapSocket ( int sd , long niceness , bool isIncoming ) {
	// debug
	//logf(LOG_DEBUG,"tcp: wrapsocket sd=%li",sd);
	// refuse to wrap it if too many used already
	//log(LOG_WARN, "incoming socket %li incoming %li %li %li", sd, (long)isIncoming,
	//    m_numIncomingUsed , *m_maxSocketsPtr);
	if ( isIncoming && m_numIncomingUsed >= *m_maxSocketsPtr )
		if ( ! closeLeastUsed () ) {
			// note it in the log
			long now = getTimeLocal();
			static long s_last = 0;
			static long s_count = 0;
			if ( now - s_last < 5 ) 
				s_count++;
			else {
				log("tcp: Out of sockets. Max sockets = %li. "
				    "(msgslogged=%li)[2]",
				    *m_maxSocketsPtr,s_count);
				s_count = 0;
				s_last = now;
			}
			// another stat
			g_stats.m_closedSockets++;
			g_errno = EOUTOFSOCKETS; 
			// send email alert
			g_pingServer.sendEmailMsg ( &s_lastTime ,
						    "out of sockets on https");
			return NULL;
		}
	// sanity check
	if ( sd < 0 || sd >= MAX_TCP_SOCKS ) {
		log(LOG_LOGIC,"tcp: Got bad sd of %li.",(long)sd);
		// another stat
		g_stats.m_closedSockets++;
		g_errno = EOUTOFSOCKETS; 
		// send email alert
		g_pingServer.sendEmailMsg ( &s_lastTime ,
					    "out of sockets on https2");
		return NULL;
	}
	// alloc a new TcpSocket
	//TcpSocket *s=(TcpSocket *) mcalloc (sizeof (TcpSocket),"TcpServerC");
	//if ( ! s ) return NULL;
	TcpSocket *s = &m_actualSockets[sd];
	// . sanity check, it should be clear always! it means "in use" or not
	// . this has happened a few times lately...
	if ( s->m_startTime != 0 ) {
		log(LOG_LOGIC,"tcp: sd of %li is already in use.",(long)sd);
		g_stats.m_closedSockets++;
		g_errno = EOUTOFSOCKETS;
		if ( sd == 0 ) log("tcp: closing2 sd of 0");
		if ( ::close(sd) == -1 )
			log("tcp: close3(%li) = %s",(long)sd,mstrerror(errno));
		// send email alert
		g_pingServer.sendEmailMsg ( &s_lastTime ,
					    "out of sockets on https3");
		//sleep(10000);
		return NULL;
	}
	// clear it
	memset ( s , 0 , sizeof(TcpSocket) );
	// store sd in our TcpSocket
	s->m_sd = sd;
	// store the last action time as now (used for timeout'ing sockets)
	s->m_startTime      = gettimeofdayInMilliseconds();
	s->m_shutdownStart  = 0;
	// just make sure this is not 0 because we use it to mean "in use"
	if ( s->m_startTime == 0 ) s->m_startTime = 1;
	s->m_lastActionTime = s->m_startTime;
	// set if it's incoming connection or not
	s->m_isIncoming = isIncoming;
	// . a 30 sec timeout, we don't want slow guys using all our sockets
	// . they could easily flood us anyway though
	// . we need to wait possibly a few minutes for a large inject of
	//   100's of MBs to finish, so make it 10 minutes
	//s->m_timeout = 30*1000;
	//s->m_timeout = 10*60*1000;
	// we have code that closes the sockets when it needs to i think
	// so let's go to 100 minutes so we can deal with reranked queries
	// (Msg3b) that take like an hour.
	s->m_timeout = 1000*60*1000;
	// a temp thang
	int parm;
	// . TODO: make sure this sd will NEVER exist!!
	// . throw our TcpSocket into the array
	// . this returns -1 on error, otherwise >= 0 of the node #
	m_tcpSockets [ sd ] = s ;
	if ( sd > m_lastFilled ) m_lastFilled = sd;
	m_numUsed++;
	// count connections to us separately for limiting to m_maxSockets
	if ( isIncoming ) m_numIncomingUsed++;
	// . we should also set TCP_CORK
	// . NOTE: we must unset this when we've written out the last bytes
	//         to the send buffer
 retry:
	parm = 1;
	if ( setsockopt ( sd , SOL_TCP , TCP_CORK , &parm , sizeof(int)) < 0) {
		// valgrind
		if ( errno == EINTR ) goto retry;
		// copy errno to g_errno
		g_errno = errno;
		log("tcp: Failed to set TCP_CORK on socket: setsockopt: %s.",
		    mstrerror(g_errno));
		goto hadError;
	}
	// try this to fix bug of not sending all data to browser
	//struct linger ggg;
	//ggg.l_onoff = 1; // non-zero to linger on close
	//ggg.l_linger = 1000; // time to linger
	//if ( setsockopt ( sd , SOL_SOCKET , SO_LINGER , &ggg , sizeof(ggg)) < 0) {
	//	// copy errno to g_errno
	//	g_errno = errno;
	//	log("tcp: Failed to set SO_LINGER on socket: setsockopt: %s.",
	//	    mstrerror(g_errno));
	//	goto hadError;
	//}
	
	// save this in here too
	s->m_niceness = niceness;
	// . now we must successfully register it
	// . this also sets the sock to nonblocking, etc...
	// . TODO: we'd have to set timestamps in Loop to check for timeou
	// . use niceness levels of 0 so this server-to-browser traffic takes
	//   precedence over spider traffic
	if (!g_loop.registerReadCallback (sd,this,readSocketWrapper,niceness))
		goto hadError;
	if(!g_loop.registerWriteCallback(sd,this,writeSocketWrapper,niceness)){
		g_loop.unregisterReadCallback(sd,this , readSocketWrapper  );
		goto hadError;
	}
	// return "s" on success
	return s;
	// otherwise, free "s" and return NULL
 hadError:
	log("tcp: Had error preparing socket: %s.",mstrerror(g_errno));
	m_tcpSockets [ sd ] = NULL;
	// clear it, this means no longer in use
	s->m_startTime = 0LL;
	//mfree ( s , sizeof(TcpSocket) ,"TcpServer" ); 
	// uncount
	m_numUsed--;
	if ( isIncoming ) m_numIncomingUsed--;
	return NULL; 
}

// . if maxIdleTime > 0 we close all sockets idle "maxIdleTime" seconds
// . if maxIdleTime > 0 we may not close ANY sockets
// . if maxIdleTime <= 0 then we ALWAYS close the least used
bool TcpServer::closeLeastUsed ( long maxIdleTime ) {
	//log(LOG_WARN, "closing. %li used!", m_numUsed);
	unsigned long times   [MAX_TCP_SOCKS];
	short         indices [MAX_TCP_SOCKS];
	unsigned char numSocks[MAX_TCP_SOCKS];
	memset(times   , 0xff, sizeof(long)  * MAX_TCP_SOCKS);
	memset(indices , 0,    sizeof(short) * MAX_TCP_SOCKS);
	memset(numSocks, 0,    sizeof(char)  * MAX_TCP_SOCKS);
	long numSocksMask = MAX_TCP_SOCKS - 1;

	short         biggestHogNdx = -1;
	unsigned char biggestHogNum = 0;

	long long nowms;
	if ( maxIdleTime > 0 ) nowms = gettimeofdayInMilliseconds();
	// conver it to milliseconds
	long long maxms ;
	if ( maxIdleTime > 0 ) maxms = maxIdleTime * 1000;
	
	for ( long i = 0 ; i <= m_lastFilled ; i++ ) {
		TcpSocket *s = m_tcpSockets[i];
		if ( ! s ) continue;

		//don't close stuff that gigablast is working on.
		if(!(s->isReading()|| s->isAvailable())) continue;
		// . chose either an available socket or a non paying
		// . customer: ...lousy cheapskates...
		// . prefLevel is set by autoban once we find a valid code.
		if ( ! s->isAvailable()  && 
		     !(s->m_isIncoming && s->m_prefLevel == 0) ) continue;
		// if we were given a VALID maxIdleTime, close any socket
		// past that idle time
		if ( maxIdleTime > 0 ) {
			// keep chugging if socket is <= the max
			if ( nowms - s->m_lastActionTime <= maxms ) continue;
			// log it
			log(LOG_INFO,"tcp: closing socket. ip=%s. "
			    "idle time was %lli ms > %lli ms",
			    iptoa(s->m_ip),nowms-s->m_lastActionTime,maxms);
			// set g_errno? i guess to zero
			g_errno = 0;
			// otherwise destroy the socket
			makeCallback ( s );
			destroySocket ( s );
			continue;
		}
		long index = s->m_ip & numSocksMask;
		if(++numSocks[index] > biggestHogNum) {
			biggestHogNum = numSocks[index];
			biggestHogNdx = index;
		}
		if(times[index] < (unsigned long)s->m_lastActionTime) continue;
		times[index] = (unsigned long)s->m_lastActionTime;
		indices[index] = i;
	}
	// if everything was in use we're SOL
	if ( biggestHogNdx == -1 ) return false;
	// get the socket we're closing
	TcpSocket *s = m_tcpSockets[ indices[biggestHogNdx] ];
	log(LOG_WARN, "tcp: closing least used! sd=%li idle=%llims", 
	    (long)s->m_sd, nowms - s->m_lastActionTime);
	// set g_errno? i guess to zero
	g_errno = 0;
	// call the callback of the socket we're destroying (if exists)
	makeCallback ( s );
	// this frees and removes TcpSocket from the array
	destroySocket ( s );
	// send email alert
	//g_pingServer.sendEmailMsg ( &s_lastTime ,
	//			    "out of sockets on https5");
	// return true cuz we closed the least-used socket
	return true;
}



// // . close the least use TcpSocket that is in an "available" state
// // . "available" means not being used but still connected
// // . return false if we could not close any cuz they're all used
// bool TcpServer::closeLeastUsed ( ) {
// 	// . see who hasn't been used in the longest time
// 	// . only check the available sockets (m_state == ST_AVAILABLE)
// 	long long minTime = (long long) 0x7fffffffffffffffLL;
// 	long      mini    = -1;

// 	for ( long i = 0 ; i <= m_lastFilled ; i++ ) {
// 		TcpSocket *s = m_tcpSockets[i];
// 		if ( ! s ) continue;
// 		if ( ! s->isAvailable() && 
// 		     !(s->m_isIncoming && s->m_prefLevel == 0) ) continue;
// 		if ( s->m_lastActionTime > minTime ) continue;

// 		mini    = i;
// 		minTime = s->m_lastActionTime;
// 	}
// 	// if everything was in use we're SOL
// 	if ( mini == -1 ) return false;
// 	// get the socket we're closing
// 	TcpSocket *s = m_tcpSockets[mini];
// 	// call the callback of the socket we're destroying (if exists)
// 	makeCallback ( s );
// 	// this frees and removes TcpSocket from the array
// 	destroySocket ( s );
// 	// return true cuz we closed the least-used socket
// 	return true;
// }

// . this is called by Loop::gotSig() when "sd" is ready for reading
// . we registered it with Loop::registerReadCallback(sd) in wrapSocket()
// . g_errno will be set by Loop if there was a kinda socket reset error
void readSocketWrapper ( int sd , void *state ) {
	// debug msg
	// log("........... TcpServer::readSocketWrapper\n");
	// extract our this ptr
	TcpServer *THIS = (TcpServer *)state;
	// get a TcpSocket from sd
	TcpSocket *s = THIS->getSocket ( sd );
	// . return if does not exist
	// . TODO: will data to be read remain on queue?
	if ( ! s ) return ;
	// doing an ssl accept?
	if ( s->m_sockState == ST_SSL_ACCEPT ) {
		// try to complete SSL_accept() function
		if ( ! THIS->sslAccept ( s ) ) {
			THIS->makeCallback  ( s );
			THIS->destroySocket ( s ); 
			return ;
		}
		// if still not done return..
		if ( s->m_sockState == ST_SSL_ACCEPT ) return;
	}
	// doing an ssl_shutdown?
	if ( s->m_sockState == ST_SSL_SHUTDOWN ) {
		THIS->destroySocket ( s );
		return;
	}
	// . if this socket was connecting than call connectSocket()
	// . it returns false if blocked,true otherwise and sets g_errno on err
	if ( s->isConnecting() ) {
		// returns -1 on error and sets g_errno,0 if blocked, 1 success
		long status = THIS->connectSocket(s) ;
		if ( status == 0 ) return;
		// now try to send on it
		if ( status == 1 ) status = THIS->writeSocket ( s );
		// destroy socket and call callback on connect error
		if ( status == -1 ) {
			// i saw 
			// ssl: Error on Connect
			// ssl: Error: Syscall 
			// from this with g_errno not set
			if ( ! g_errno ) { char *xx=NULL;*xx=0; }
			THIS->makeCallback  ( s );
			THIS->destroySocket ( s ); 
			return ;
		}
		if ( status != 1 ) return ;
		// now try to read the reply
		//log("calling readSocket now\n");
	}
	// . readSocket() returns -1 on error and sets g_errno
	// . if socket was closed on the other end this returns -1 but does 
	//   NOT set g_errno
	// . otherwise, returns 0 if blocked, 1 if completed
	long status = THIS->readSocket ( s ) ;
	// . destroy socket immediately on error or if other end closed
	// . this will also unregister all our callbacks for the socket
	// . TODO: deleting nodes from under Loop::callCallbacks is dangerous!!
	if ( status == -1 ) {
		// g_errno is not set if it just read 0 bytes
		//if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		THIS->makeCallback  ( s );
		THIS->destroySocket ( s ); 
		return;
	}
	// if we blocked then return
	if ( status == 0 ) return;
	// enter here if we finished reading a reply
	//if ( s->m_sendBuf || s->isClosed() ) {
	if ( s->m_sendBuf ) {
		// i guess ok
		g_errno = 0;
		// callback must free all m_sendBuf/m_readBuf in TcpSocket
		THIS->makeCallback ( s );
		// . if the socket was closed by remote side we destroy it
		// . if the readBuf was maxed out we destroy so keep-alives
		//   don't keep messed up
		// . this is wrong if the size we read happened to exactly
		//   be out m_maxReadBufSize, but oh well, no big deal
		//if      ( s->isClosed () )                   
		//	THIS->destroySocket ( s );
		//else if ( s->m_readBufSize >= THIS->m_maxReadBufSize ) 
		//	THIS->destroySocket ( s );
		//else    
		//	THIS->recycleSocket ( s );
		THIS->destroySocket ( s );		
		return;
	}
	// set the socket's state to writing now (how about WAITINGTOWRITE?)
	s->m_sockState = ST_WRITING;
	// tell 'em socket has called the handler
	s->m_waitingOnHandler = true;
	// . TODO: ensure timeout is set on s in case requestHandler does not
	//   send on it so it will close in due time
	// . call the request handler to handle it
	// . this should have been specified in TcpServer::init()
	// . IMPORTANT: this handler MUST call sendMsg(s,...) to send a reply
	THIS->m_requestHandler ( s ) ;
}	

// . returns -1 on error and sets g_errno, 0 if blocked, 1 if completed
// . now it also returns -1 if other end closed on us (no more ST_CLOSED state)
//   but it does not set g_errno in that case
// . tries to read some data from the socket "s"
long TcpServer::readSocket ( TcpSocket *s ) {
	// . otherwise, it's a normal read of normal data (request or reply)
	// . if we got some shit to read but shouldn't be reading someone is
	//   fucking with us so throw the shit away... it could be an attack...
	if ( ! s->isReading() && ! s->isAvailable() ) {
		if ( g_conf.m_logDebugTcp ) 
			log(LOG_DEBUG,"tcp: readsocket: socket %i not in "
			"read/available mode... trying a write.",s->m_sd );
		//long status = writeSocket ( s );
		//return status;
		return 0;
	}
	// set our state to reading in case we were ST_AVAILABLE state
	s->m_sockState = ST_READING;
	// . TODO: support the reception of large messages
	// . alloc a buffer to read the reply/request
	// . will grow dynamically if it's not enough
	if ( ! s->m_readBuf ) {
		// . if our sendBuf is non-NULL we're getting a big reply
		// . otherwise it's a small request
		long size ;
		if ( s->m_sendBuf ) size = 64*1024 ;
		else                size = TCP_READ_BUF_SIZE; // 1024;
		// alloc space only if we need to now
		// this might be causing, problems, so i took this out
		//if ( size <= TCP_READ_BUF_SIZE )
		//	s->m_readBuf = s->m_tmpBuf;
		//else
		s->m_readBuf = (char *) mmalloc ( size ,"TcpServer");
		// if not able to allocate initial buffer then bail w/ g_errno
		if ( ! s->m_readBuf ) return -1;
		// otherwise, set it's size
		s->m_readBufSize = size;
		// first char is a \0
		s->m_readBuf[0] = '\0';
	}
 loop:
	// . determine how many bytes we have AVAILable for storing into:
	// . leave room to store a \0 so html docs always have it, -1
	// . ALSO leave room for 4 bytes at the end so Proxy.cpp can store the
	//   sender ip address in there
	// . see HttpServer.cpp::sendDynamicPage()
	long avail = s->m_readBufSize  - s->m_readOffset - 1 - 4;

	// do the read
	int n;
	if (m_useSSL)
		n = SSL_read ( s->m_ssl, s->m_readBuf + s->m_readOffset, avail );
	else
		n = ::read ( s->m_sd, s->m_readBuf + s->m_readOffset, avail );

	// deal with errors
	if ( n < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto loop;
		// copy errno to g_errno
		g_errno = errno;
		if ( g_errno == EAGAIN || g_errno == 0 ||
				g_errno == EILSEQ) { g_errno = 0; return 0; }
		log("tcp: Failed to read on socket: %s.", mstrerror(g_errno));
		return -1;
	}
	// debug msg
	//log(".......... TcpServer read %i bytes on %i\n",n,s->m_sd);
	// . if we read 0 bytes then that signals the end of the connection
	// . doesn't this only apply to reading replies and not requests???
	// . MDW: add "&& s->m_sendBuf to it"
	// . just return -1 WITHOUT setting g_errno
	if ( n == 0 ) return -1; // { s->m_sockState = ST_CLOSED; return 1; }
	// update counts
	s->m_totalRead  += n;
	s->m_readOffset += n;
	// NULL terminate after each read
	if ( avail >= 0 ) s->m_readBuf [ s->m_readOffset ] = '\0';
	// update last action time stamp
	s->m_lastActionTime = gettimeofdayInMilliseconds();
	// . if we don't know yet, try to determine the total msg size
	// . it will try to set s->m_totalToRead
	// . it will look for the end of the mime on requests and look for
	//   the the mime's content-len: field on replies
	// . it should look for content-len: on post requests as well
	// . it sets it to -1 if incoming msg size is still unknown
	if ( s->m_totalToRead <= 0 && ! setTotalToRead ( s ) ) {
		log(LOG_LOGIC,"tcp: readSocket3: wierd error.");
		return -1;
	}
	// . keep reading until we block
	// . mdw: loop back if we can read more
	// . obsoleted: just return false if we're NOT yet done
	// . NOTE: loop even if we read all to read, cuz we might need to
	//         read a 0 byte packet (a close) iff we're reading a reply
	if ( s->m_totalToRead <= 0  ||
	     s->m_readOffset  <  s->m_totalToRead ) goto loop; // return 0;
	// . if it was a reply, keep looping until we read 0 byte packet
	//   since we no longer support keep-alive
	// . NO! i think the linksys befsr81 nat/dsl router is blocking
	//   some FINs so we never get that freakin 0 byte packet, so
	//   let's force the close ourselves
	// . unfortunately, if content-len is not specified in the returned
	//   Http MIME then this is not going to fix the lost FIN problem
	// . shit, this doesn't help 404 pages because they don't have
	//   a content-length: field a lot of the time
	//if ( s->m_sendBuf ) goto loop;
	// otherwise, we read all we needed to so return 1
	return 1;
}

// . ensures our readBuf is big enough to handle the incoming msg
// . if not, it reallocs it to make bigger
// . returns false and sets g_errno on error
// . calls m_getMsgSize to look for Content-Length: on replies, \n on requests
//   for the HTTP protocol at least
bool TcpServer::setTotalToRead ( TcpSocket *s ) {
	// . at "close connection" bit is sent in the last tcp packet
	//   for http servers that use "Connection: close"
	// . we only get the POLLHUP event when we try to write on it, however
	// . therefore check for this bit here
	// . get tcp socket state
	// . see /usr/include/linux/tcp.h for more TCP states and info,...
	// . TODO: should we destroy the socket in this case?
	/*
	struct tcp_info info  ;
	socklen_t       infoSize = sizeof(tcp_info);
	getsockopt ( s->m_sd , SOL_TCP , TCP_INFO, &info, &infoSize );
	if ( info.tcpi_state == TCP_CLOSE_WAIT ) {
		// if we got the close signal then we've read it all!
		s->m_totalToRead = s->m_readOffset;
		return true;
	}
	*/
	// . parse out the msgSize, -1 means unknown
	// . NOTE: getMsgSize() may return less than the actual reply size if 
	//   it decides we should truncate the document!
	long size = m_getMsgSize ( s->m_readBuf , s->m_readOffset , s );
	// set total to read if we know it
	if ( size > 0 ) s->m_totalToRead = size;
	// if size is unknown ensure we have at least 10k of extra space
	if ( size == -1 ) size = s->m_readOffset + 10*1024;
	// . if it's smaller than our current buffer don't worry
	// . we need to make sure to store a \0 at end of the read...
	//if ( size <= s->m_readBufSize ) return true;
	if ( size < s->m_readBufSize ) return true;
	// adjust so we can include our \0 at the end of the m_readBuf
	size += 1;
	// and for the proxy ip!!
	// (See HttpServer.cpp::sendDynamicPage())
	size += 4;
	// prepare for realloc if we're point to s->m_tmpBuf
	//char *tmp = NULL;
	char *newBuf = NULL;
	/*
	if ( s->m_readBuf == s->m_tmpBuf ) {
		log(LOG_LOGIC,"tcp: This should not have been called.");
		sleep(10000);
		tmp = s->m_tmpBuf;
		//s->m_readBuf = NULL;
		newBuf = (char *)mmalloc(size,"TcpServerR2");
		// copy over from tmpBuf if we have to
		if ( newBuf ) memcpy (newBuf, s->m_readBuf, s->m_readBufSize);
	}
	// otherwise, it's bigger than our 10k buffer and we gotta realloc
	else 
	*/
	newBuf = (char * ) mrealloc(s->m_readBuf,s->m_readBufSize,size,
				    "TcpServerR");
	if ( ! newBuf )
		return log("tcp: Failed to reallocate from %li to %li "
				   "bytes to read from socket.",
				   s->m_readBufSize,size);
	// set the new buffer
	s->m_readBuf       =  newBuf;
	s->m_readBufSize   =  size;
	return true;
}

// . this is called by Loop::gotSig() when "sd" is ready for reading
// . we registered it with Loop::registerReadCallback(sd)
// . g_errno will be set by Loop if there was a kinda socket reset error
// . we call this when socket is connected, too
void writeSocketWrapper ( int sd , void *state ) {
	// debug msg
	// log("........... TcpServer::writeSocketWrapper\n");
	TcpServer *THIS = (TcpServer *)state;
	// get the TcpSocket for this socket descriptor
	TcpSocket *s = THIS->getSocket ( sd );
	if ( ! s ) { 
		if ( g_conf.m_logDebugTcp )
			log(LOG_DEBUG,"tcp: writesocketwrapper: "
			"Socket descriptor %i not found.", sd ); 
		return; 
	}
	// doing an ssl_shutdown?
	if ( s->m_sockState == ST_SSL_SHUTDOWN ) {
		THIS->destroySocket ( s );
		return;
	}
	// . if loop notified us of an error on this socket then destroy it
	// . like -- pollhup, socket closed
	if ( g_errno == ESOCKETCLOSED ) { 
		// note the ip now too
		long long nowms = gettimeofdayInMilliseconds();
		if ( g_conf.m_logDebugTcp )
		     log(LOG_INFO,"tcp: sock closed. ip=%s. idle for %lli ms.",
			 iptoa(s->m_ip),nowms-s->m_lastActionTime);
		// . some http servers close socket as end of transmission
		// . so it's not really an g_errno
		g_errno = 0;
		THIS->makeCallback ( s );
		THIS->destroySocket ( s ); 
		return; 
	}
	if ( s->m_sockState == ST_NEEDS_CLOSE ) {
		THIS->destroySocket ( s ); 
		return; 
	}
	// . if this socket was connecting than call connectSocket()
	// . it returns false if blocked,true otherwise and sets g_errno on err
	if ( s->isConnecting() ) {
		// returns -1 on error and sets g_errno,0 if blocked, 1 success
		long status = THIS->connectSocket(s) ;
		// if connection had an error, bail, g_errno should be set
		if ( status == -1 ) {
			if ( ! g_errno ) { char *xx=NULL;*xx=0; }
			THIS->makeCallback ( s );
			THIS->destroySocket ( s ); 
		}
		// return on coonection error or if still trying to connect
		if ( status != 1 ) return;
		// now try to send on it
	}
	// if socket has nothing to send yet cuz we're waiting, wait...
	if ( s->m_sendBufUsed == 0 ) return;
	// . writeSocket returns false if blocked, true otherwise
	// . it also sets g_errno on errro
	// . don't call it if we have g_errno set, however
	long status = THIS->writeSocket ( s ) ;
	// return if it blocked
	if ( status == 0 ) return;
	// if write finished, but we're not done reading return
	if ( status == 1  &&  ! s->m_readBuf ) return;
	// good?
	g_errno = 0;
	// otherwise, call callback on done reading or error
	THIS->makeCallback ( s );
	// . destroy the socket on error, recycle on transaction completion
	// . this will also unregister all our callbacks for the socket
	if      ( status == -1 ) THIS->destroySocket ( s );
	else                     THIS->recycleSocket ( s );
}	

// . returns -1 on error and sets g_errno, 0 if blocked, 1 if completed
// . called by writeSocketWrapper() which is called by Loop::gotSig() when it
//   gets a signal that "sd" is ready for writing
// . also called by sendMsg() to immediately initiate sending a msg
long TcpServer::writeSocket ( TcpSocket *s ) {
	// skip if socket not in send state (nothing needs to be sent)
	if ( ! s->isSending() ) { 
		if ( g_conf.m_logDebugTcp )
			log(LOG_DEBUG,"tcp: writeSocket: socket %i not in "
			    "write mode... trying a read",s->m_sd );
		return 0;
		//long status = readSocket ( s );
		//return status; //-1; 
	}
 loop:
	// send some stuff
	long toSend = s->m_sendBufUsed - s->m_sendOffset;
	// get a ptr to the msg piece to send
	char *msg = s->m_sendBuf;
	if ( ! msg ) return 1;
	// debug msg
	if ( g_conf.m_logDebugTcp )
		logf(LOG_DEBUG,"tcp: writeSocket: writing %li bytes",toSend);
	// send this piece
	int n;
 retry10:
	if (m_useSSL)
		n = SSL_write ( s->m_ssl, msg + s->m_sendOffset, toSend );
	else
		n = ::send ( s->m_sd , msg + s->m_sendOffset , toSend , 0 );
	// cancel harmless errors, return -1 on severe ones
	if ( n < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry10;
		// copy errno to g_errno
		g_errno = errno;
		// i saw errno to be 0 after logging
		// ssl: Error on Connect
		// ssl: Error: Syscall 
		// and then calling THIS->writeSocket() and thereby causing
		// a core... so check g_errno here.
		// actually for m_useSSL it does not set errno...
		if ( ! g_errno && m_useSSL ) g_errno = ESSLERROR;
		if ( g_errno != EAGAIN ) return -1;
		g_errno = 0; 
		// debug msg
		//log("........... TcpServer write blocked on %i\n",
		//s->m_sd);
		return 0; 
	}
	// debug msg
	if ( g_conf.m_logDebugTcp )
		log("........... TcpServer wrote %i bytes on %i\n",
		    n,s->m_sd);
	// return 0 if we blocked on this write
	if ( n == 0 ) return 0;
	// update last action time stamp
	s->m_lastActionTime = gettimeofdayInMilliseconds();
	// update count
	s->m_totalSent  += n;
	s->m_sendOffset += n;
	// . if we sent less than we tried to send then block
	// . we should be notified via sig/callback when we can send the rest
	if ( n < toSend ) return 0;
	// . we sent all we were asked to, but our sendBuf may need a refill
	// . call this routine to refill it
	if ( s->m_totalSent  < s->m_totalToSend ) {
		// note that
		if ( g_conf.m_logDebugTcp )
			log(".... Tcpserver: only sent fraction. looping.");
		// . refill the sendBuf
		// . this might set m_sendBufUsed, m_sendBufOffset, ...
		// . it may also block in which case nothing will be changed
		// . it returns # of new bytes read
		// . it returns -1 on error
		if ( m_getMsgPiece ( s ) == -1 ) {
			log("tcp: Had error getting data to send: %s.",
			    mstrerror(g_errno));
			return -1;
		}
		// . now loop to send the refilled data
		// . if m_getMsgPiece() blocked on the read we still won't have
		//   anything to send and it should have registered itself
		//   to get ready-to-read signals and it will give us a
		//   ready-to-send signal when it's read something into the
		//   sendBuf for "s" (calls g_loop.callCallbacks(s->m_sd))
		goto loop;
	}
	// if we made it here we sent the whole thing
	// . uncork sd so write buf gets flushed
	// . return false and set g_errno on error
	// . sd should be destroyed
	int parm = 0;
 retry11:
	if ( setsockopt (s->m_sd,SOL_TCP,TCP_CORK,&parm,sizeof(int)) < 0) {
		// valgrind
		if ( errno == EINTR ) goto retry11;
		// copy errno to g_errno
		g_errno = errno;
		log("tcp: Failed to set TCP_CORK option on socket: %s.",
		    strerror(g_errno));
		return -1;
	}
	// if we completed sending a REQUEST then change state to 
	// "reading" and return true
	if ( s->isSendingRequest() ) {
		s->m_sockState = ST_READING;
		return 1 ;
	}
	// . otherwise, we finished sending a reply
	// . our caller should call recycleSocket ( s ) to keep it alive
	return true ;
} 

// . returns -1 on error and sets g_errno, 0 if blocked, 1 if completed
// . called by readSocketWrapper() when socket is ready for reading but it's 
//   state is ST_CONNECTING
long TcpServer::connectSocket ( TcpSocket *s ) {
	// if this socket is not in connecting state (ST_CONNECTING) then ret
	//	if ( ! s->isConnecting() ) return true;
	// now we have a connect just starting or already in progress
	struct sockaddr_in to;
	to.sin_family = AF_INET;
	// our ip's are always in network order, but ports are in host order
	to.sin_addr.s_addr =  s->m_ip;
	to.sin_port        = htons ((unsigned short)( s->m_port));
	bzero ( &(to.sin_zero) , 8 ); // TODO: bzero too slow?
	if ( g_conf.m_logDebugTcp )
		log("........... TcpServer connecting %i to %s port %i\n",
		    s->m_sd,iptoa(s->m_ip), s->m_port );
 retry3:
	// connect to the socket. This should be non-blocking!
	if ( ::connect ( s->m_sd, (sockaddr *)&to, sizeof(to) ) == 0 ) {
		// debug msg
		if ( g_conf.m_logDebugTcp )
			log("........... TcpServer connected %i to %s "
			    "port %i\n", s->m_sd, iptoa(s->m_ip), s->m_port );
		// hey it was successful!
		goto connected;
	}
	// valgrind. interrupted system call?
	if ( errno == EINTR ) goto retry3;
	// copy errno to g_errno
	g_errno = errno;
	// hey! it's alrady connected!
	if ( g_errno == EALREADY    ) {
		// debug msg
		if ( g_conf.m_logDebugTcp )
			log("........... TcpServer already connected %i to "
			    "%s port %i\n", s->m_sd, iptoa(s->m_ip),s->m_port);
		g_errno = 0;
		goto connected;
	}
	// we blocked with the EINPROGRESS g_errno
	if ( g_errno == EINPROGRESS ) { g_errno = 0; return 0; }
	// return -1 on real error
	if ( g_conf.m_logDebugTcp )
		log(LOG_INFO,"tcp: Failed to connect socket: %s, %s:%li", 
		    mstrerror(g_errno), iptoa(s->m_ip), (long)s->m_port);
	return -1;

connected:
	// change state so this doesn't get called again
	s->m_sockState = ST_WRITING;
	// connect ssl
	if (m_useSSL) {
		int r;
		s->m_ssl = SSL_new(m_ctx);
		SSL_set_fd(s->m_ssl, s->m_sd);
		SSL_set_connect_state(s->m_ssl);
		r = SSL_connect(s->m_ssl);
		if (!s->m_ssl) {
			log("ssl: SSL is NULL after connect.");
			char *xx = NULL; *xx = 0;
		}
		if (r <= 0) {
			int sslError = SSL_get_error(s->m_ssl, r);
			if ( sslError != SSL_ERROR_WANT_READ &&
			     sslError != SSL_ERROR_WANT_WRITE &&
			     sslError != SSL_ERROR_NONE ) {
				logSSLError(s->m_ssl, r);
				log("net: ssl: Error on Connect. ip=%s",
				    iptoa(s->m_ip));
				g_errno = ESSLERROR;
				// crap, if we return 1 here then
				// it will call THIS->writeSocket() which
				// will return -1 and not set g_errno
				return -1;
			}
		}
	}
	return 1;
}

// . call this on read/write/connect errors
// . g_errno MUST be set before this is called
// . calls the callback governing "s" if it has one
void TcpServer::destroySocket ( TcpSocket *s ) {
	if ( ! s ) return ;
	// sanity check
	if ( s->m_udpSlot ) { char *xx=NULL;*xx = 0; }
	// . you cannot destroy socket's who have called a handler and the
	//   handler is still in progress, because when he's got a reply ready
	//   he expects this TcpSocket to still be there
	// . if this is the case we, the client probably closed his connection
	//   before we could generate a reply to send to him
	if ( s->m_waitingOnHandler ) return;
	// log it if g_errno not set
	if ( g_errno ) 
		log("tcp: Destroying tcp socket because of error: %s. sd=%i. "
		    "state=%i.", mstrerror(g_errno),s->m_sd,s->m_sockState);
	// the socket descriptor
	int sd = s->m_sd;
	// debug msg
	if ( g_conf.m_logDebugTcp )
		logf(LOG_DEBUG,"tcp: ...... TcpServer closing sock %i\n",sd);
	// make it blocking for the close for testing
	//int flags = fcntl ( sd , F_GETFL );
	//flags &= ~O_NONBLOCK;
	//fcntl ( sd , F_SETFL , flags );
	if ( sd == 0 ) log("tcp: closing3 sd of 0");

	// remove all queued signals from Loop for this fd
	if (m_useSSL && s->m_ssl) {
		/*
	retry23:
		errno = 0;
		// shit, this blocks?
		int ret = SSL_shutdown(s->m_ssl);
		// ssl debug!
		//log("ssl: ssl_shutdown returned %i (errno=%i/%s) [fd=%i]",
		//    ret,errno,mstrerror(errno),sd);
		// did it get interrupted?
		if ( ret < 0 && errno == EINTR ) goto retry23;
		// set "saved" to errno if it had a bad return value
		//long saved = 0; if ( ret < 0 ) saved = errno;
		// sslerr is "2" if it is SSL_ERROR_WANT_READ and 3 for WRITE
		int sslerr = 0;
		if ( ret < 0 ) sslerr = SSL_get_error(s->m_ssl, ret);
		// if we need to call it again, set this flag...
		if ( // . 0 means to call it again to complete handshaking
		     // . ret==1 means it is ALL done!
		     ret == 0 ||
		     // this means it blocked... waiting on communication
		     (ret == -1 && errno == EAGAIN)
		     //saved == SSL_ERROR_WANT_READ ||
		     //saved == SSL_ERROR_WANT_WRITE ||
		     //saved == EAGAIN
		     //err == SSL_ERROR_WANT_READ ||
		     //err == SSL_ERROR_WANT_WRITE 
		     ) {
			// ssl debug!
			//log("ssl: ssl_shutdown did not complete fd=%i "
			//    "(sslerr=%i)",sd,sslerr);
			s->m_sockState = ST_SSL_SHUTDOWN;
			// for time outs...
			long now = getTimeLocal();
			// TODO: if we are almost out of sockets then force
			// close this without waiting 4 seconds lest we be
			// susceptible to a DOS attack
			if ( s->m_shutdownStart == 0 )
				s->m_shutdownStart = now;
			// only wait if it hasn't been more than 4 seconds
			if ( now - s->m_shutdownStart < 4 )
				return;
			// otherwise, force close the ssl socket
			log("ssl: ssl_shutdown timed out fd=%i "
			    "(start=%li now=%li)",sd,s->m_shutdownStart,now);
			//return;
		}
		*/
		SSL_free(s->m_ssl);
	}

	// ssl debug!
	//log("tcp: closing fd=%i",sd);

	// TODO: does this block or what?
	long cret = ::close ( sd );
	if ( cret != 0 ) // == -1 ) 
		log("tcp: close(%li) = %li = %s",
		    (long)sd,cret,mstrerror(errno));
	// a 2nd close? it should return -1 with errno set!
	//long cret2 = ::close ( sd );
	//if ( cret2 != -1 )
	//	log("tcp: double close was required fd=%li",(long)sd);
	// flag it
	s->m_sockState = ST_CLOSE_CALLED;
	//::close ( 0 );
	//fdatasync(sd);


	// caller should call makeCallback, not us since we might not
	// have blocked, in which case should not be calling the callback
	//	makeCallback ( s );
	// pretend we're trying to salvage it to free the send/read bufs
	//	recycleSocket ( s );
	// do not try to free m_tmpBuf
	//if ( s->m_readBuf == s->m_tmpBuf ) s->m_readBuf = NULL;
	//if ( s->m_sendBuf == s->m_tmpBuf ) s->m_sendBuf = NULL;
	// always free read/send buffers
	if ( s->m_readBuf ) mfree (s->m_readBuf, s->m_readBufSize,"TcpServer");
	// always free the sendBuf 
	if ( s->m_sendBuf ) mfree (s->m_sendBuf, s->m_sendBufSize,"TcpServer");
	// unregister it with Loop so we don't get any calls about it
	g_loop.unregisterWriteCallback ( sd , this , writeSocketWrapper );
	g_loop.unregisterReadCallback  ( sd , this , readSocketWrapper  );
	// debug msg
	//log("unregistering sd=%li",sd);
	// discount if it was an incoming connection
	if ( s->m_isIncoming ) m_numIncomingUsed--;
	// clear it, this means no longer in use
	s->m_startTime = 0LL;
	// free TcpSocket from the array
	//mfree ( s , sizeof(TcpSocket) ,"TcpServer");
	m_tcpSockets [ sd ] = NULL;
	// one less used
	m_numUsed--;
	// reset m_lastFilled
	if ( sd == m_lastFilled ) {
		sd--;
		while ( sd > 0  && !m_tcpSockets[sd] ) sd--;
		m_lastFilled = sd;
	}
}

// . try to make the socket available for another transaction
// . if the socket was initiated by remote host then this makes us seem like
//   a keep alive server, and we're open for reading...
// . if the socket was connected by us then we're hoping the remote host
//   supports keep alives...
void TcpServer::recycleSocket ( TcpSocket *s ) {
	// mdw... this now just destroys, baby, no more keep-alives
	destroySocket ( s );
	return;
	// do not try to free m_tmpBuf
	//if ( s->m_readBuf == s->m_tmpBuf ) s->m_readBuf = NULL;
	//if ( s->m_sendBuf == s->m_tmpBuf ) s->m_sendBuf = NULL;
	// always free read/send buffers
	if ( s->m_readBuf ) mfree (s->m_readBuf, s->m_readBufSize,"TcpServer");
	// always free the sendBuf 
	if ( s->m_sendBuf ) mfree (s->m_sendBuf, s->m_sendBufSize,"TcpServer");
	// hey! there shouldn't be any should there? TODO! figure out.
	// debug msg
	//log("........... TcpServer recycling sock #%i\n",s->m_sd);
	//if ( s->m_state ) log("TcpServer::recycleSocket: panic-callerData");
	// NULLify all data in TcpSocket, except ip/port
	s->m_callback          = NULL;
	s->m_state             = NULL;
	s->m_sendBuf           = NULL;
	s->m_sendBufSize       = 0;
	s->m_sendOffset        = 0;
	s->m_totalSent         = 0;
	s->m_totalToSend       = 0;
	s->m_readBuf           = NULL;
	s->m_readBufSize       = 0;
	s->m_readOffset        = 0;
	s->m_totalRead         = 0;
	s->m_totalToRead       = 0;
	//s->m_timeout           = 60*1000;
	s->m_timeout           = 10*60*1000;
	s->m_udpSlot           = NULL;
	// keep it alive for other dialogs
	s->m_sockState         = ST_AVAILABLE;
	s->m_startTime         = gettimeofdayInMilliseconds();
	s->m_waitingOnHandler  = false;
	s->m_shutdownStart     = 0;
}

// . called by Loop::runLoop() every one second
void readTimeoutPollWrapper ( int sd , void *state ) {
	TcpServer *THIS = (TcpServer *)state;
	THIS->readTimeoutPoll();
}

// . called by readTimeoutPollWrapper() every 1 second
void TcpServer::readTimeoutPoll ( ) {
	// get the time now in seconds
	long long now = gettimeofdayInMilliseconds();
	// send the msg that is mostly caught up with it's acks first.
	// "ackWait" is how many more acks we need to complete the transmission
	for ( long i = 0 ; i <= m_lastFilled ; i++ ) {
		// get the TcpSocket for socket descriptor #i
		TcpSocket *s = m_tcpSockets[i];
		if ( ! s ) continue;
		// if in a high niceness callback we can only serve 
		// low niceness (0) sockets at this point. because we might
		// do a double callback on a socket that have niceness 1...
		if ( g_loop.m_inQuickPoll &&  s->m_niceness != 0 ) continue;
		// close if need be. we added this delayed closing logic because
		// the transmission was getting truncated somehow, and i even tried
		// the SO_LINGER crap to no avail. so this is basically our own linger
		// algorithm...
		if ( s->m_sockState == ST_NEEDS_CLOSE &&
		     // give it 500ms
		     now - s->m_lastActionTime >= 500 ) {
			destroySocket ( s );
			continue;
		}
		// . if he is sending, that sticks too, so try it!
		// . or if we're connecting to him...
		if ( s->isSending() || s->isConnecting() ) {
			writeSocketWrapper ( s->m_sd , this );
			s = m_tcpSockets[i];
			if ( ! s ) continue;
		}
		// . seems like we don't always get the ready-for-read signal
		// . HACK: this fixes the problem, albeit not the best way
		// . or if he's connecting to us...
		if ( s->isReading() || s->isConnecting() ) {
			readSocketWrapper ( s->m_sd , this );
			s = m_tcpSockets[i];
			if ( ! s ) continue;
		}
		// continue if socket not in an active state
		if ( ! s->isReading   () && 
		     ! s->isConnecting() &&
		     ! s->isSending   ()    ) continue;
		// . if the transmission time out then makeCallback() will
		//   make the callback and then unconditionally delete 
		//   the UdpSlot
		// . go back to top because delete might have shrunk table
		// see if socket is now closed
		//struct tcp_info info;
		//socklen_t  size     = sizeof(tcp_info);
		//getsockopt ( s->m_sd , SOL_TCP , TCP_INFO, &info, &size );
		//log("fd=%i,info=%hhx\n",s->m_sd,info.tcpi_state);
		// fix system clock advanced
		if ( s->m_lastActionTime > now ) s->m_lastActionTime = now ;

		// how long since we started...
		long long total = now - s->m_startTime;
		// if it has been a minute or more, and averaging less than
		// 20 bytes per second, time it out. otherwise we end up 
		bool timeOut = false;
		// BUT make sure we sent them a request. i.e. we are spidering
		// and they haven't gotten back to us yet...
		if ( total > 60000 && s->m_sendBufSize > 0 &&
		     m_doReadRateTimeouts &&
		     s->m_sockState == ST_READING ) {
			// calculate "Bytes per second"
			float Bps=(float)s->m_readOffset/((float)total)/1000.0;
			// timeout if too low
			if ( Bps < 20.0 ) {
				timeOut = true;
				log("tcp: Read rate too low. Timing out. "
				    "Bps=%li ip=%s", (long)Bps,iptoa(s->m_ip));
			}
		}				


		// if we read something and are now generating a reply to
		// write back to the browser, wait a long time, because
		// the seo tools can take several minutes!
		if ( s->m_sockState == ST_WRITING && s->m_sendBufSize == 0 )
			continue;

		// if the transmission time out then makeCallback() will
		// make the callback and then unconditionally delete theUdpSlot
		// go back to top because delete might have shrunk table.
		long long elapsed = now - s->m_lastActionTime;
		if ( ! timeOut && elapsed < s->m_timeout) continue;

		//log("tcp: timeout=%li fd=%li",sockTimeout,s->m_sd);

		// uncomment this if you want to close a socket if they havent 
		// finished reading in 10 seconds
		//     &&
		//     !(s->isReading() && s->m_isIncoming && elapsed > 10000))
		//	continue;

		// set g_errno to timeout error just for this callback
		g_errno = ETCPTIMEDOUT;
		// call the callback since they blocked for sure
		makeCallback ( s );
		// nuke the transaction socket/slot
		destroySocket ( s );
		// reset g_errno so we can continue
		g_errno = 0;
	}
}

// . sd should be m_sock
// . this is called by Loop::gotSig() when m_sock is ready for reading
void acceptSocketWrapper ( int sd , void *state ) {
	TcpServer *THIS = (TcpServer *)state;
	long long startTimer = gettimeofdayInMilliseconds();
 loop:
	// . returns true if read completed, false otherwise
	// . sets g_errno on error
	// . this will call ::close(sd) on error
	TcpSocket *s = THIS->acceptSocket ( );
	// . destroy the socket on error
	// . this will also unregister all our callbacks for the socket
	// . TODO: deleting nodes from under Loop::callCallbacks is dangerous!!
	//	if ( g_errno ) THIS->destroySocket ( sd );
	// . return true since we don't want to be removed from Loop's loop
	//	return true;

	// just return if nothing to accept
	if ( ! s ) return;
	// . i put this here because if i have a debug breakpoint before
	//   this m_sd gets registered we'll miss out on some read signals
	// . and if we miss those signals we won't read from sd then!
	readSocketWrapper ( s->m_sd , state );
	// keep looping until we have no more accepts on the queue
	if(gettimeofdayInMilliseconds() - startTimer > 15) return;
	goto loop;
}

// . this is called when m_sock, our listener, is ready for reading
// . returns the TcpSocket
// . returns NULL if did not accept it
// . sets g_errno on error
TcpSocket *TcpServer::acceptSocket ( ) {
	// get the new socket descriptor, "newsd"
	struct sockaddr_in name; 
	unsigned int       nameLen = sizeof(sockaddr);
 retry12:
	int newsd = accept ( m_sock , (sockaddr *)&name , &nameLen );
	// valgrind
	if ( newsd < 0 && errno == EINTR ) goto retry12;
	// assume none
	g_errno = 0;
	// copy errno to g_errno
	if ( newsd < 0 ) g_errno = errno;
	// ignore harmless errors
	if ( g_errno == EAGAIN ) { g_errno = 0; return NULL; }
	if ( g_errno == EILSEQ ) { g_errno = 0; return NULL; }

	if ( g_conf.m_logDebugTcp ) 
		logf(LOG_DEBUG,"tcp: ...... accepted sd=%li",(long)newsd);

	// ssl debug!
	//log("tcp: accept returned fd=%i",newsd);

	if ( newsd < 0 ) {
		log("TcpServer::acceptSocket:%s",mstrerror(g_errno));
		// too many open files (i can't find the #define for the error).
		if(g_errno == 24) {
			if(closeLeastUsed()) return acceptSocket();
		}
		return NULL;
	}
	// i think this is zero to finish a non-blocking socket close?
	if ( newsd == 0 ) {
		log("tcp: accept gave sd = 0, strange, that's stdin! "
		    "allowing to pass through for now.");
		//long nn = ::send ( 0,"hey",3,0);
		//log("tcp: send = %li",nn);
		//return NULL;
		// so calling close(0) seems to really close it...???
		//if ( ::close (0) == -1 )
		//	log("tcp: close3(%li) = %s",(long)0,mstrerror(errno));
		//goto loop;
		//return NULL;
	}

	// ban assholes
	//if(g_autoBan.isBanned(name.sin_addr.s_addr)) return NULL;
	//if ( (long)name.sin_addr.s_addr == atoip ("194.205.122.42",14) ) {
		//log("banned ip=%s", iptoa(name.sin_addr.s_addr));
	//	close(newsd); 
	//	return NULL;
	//}

	// . wrap a new TcpSocket around "newsd"
	// . on error wrapSocket() will call ::close(newsd) for you
	// . wrapSocket() also registers callbacks for newsd
	// . use a niceness of 0 so this takes priority over spider traffic
	TcpSocket *s = wrapSocket ( newsd , 0 , true /*incoming?*/ );
	// should just close newsd if we couldn't wrap it
	if ( ! s ) { 
		//log("tcp: wrapsocket returned null fd=%i",newsd);
		if ( newsd == 0 ) log("tcp: closing sd of 0");
		if ( ::close(newsd)== -1 )
			log("tcp: close2(%li) = %s",
			    (long)newsd,mstrerror(errno));
		return NULL; 
	}


	// set the ssl
	s->m_ssl = NULL;//ssl;
	// set the ip/port/state
	s->m_ip        = name.sin_addr.s_addr;
	s->m_port      = name.sin_port;
	s->m_sockState = ST_READING;
	s->m_this      = this;
	s->m_udpSlot   = NULL;

	if ( ! m_useSSL ) return s;

	// the wrapSocket() function above set our socket to
	// non-blocking... but we still need to call SSL_accept()
	s->m_sockState = ST_SSL_ACCEPT;
	if ( sslAccept ( s ) ) return s;
	// critical error of some sort? then destroy socket.
	//makeCallback  ( s );
	destroySocket ( s ); 
	return NULL;
}

// returns false on critical error in which case "s" should be destroyed
bool TcpServer::sslAccept ( TcpSocket *s ) {

	long newsd = s->m_sd;

	// build the ssl
	if ( ! s->m_ssl ) {
		SSL *ssl = NULL;
		//log("ssl: SSL_new");
		ssl = SSL_new(m_ctx);
		//log("ssl: SSL_set_fd %li",(long)newsd);
		SSL_set_fd(ssl, newsd);
		//log("ssl: SSL_set_accept_state");
		SSL_set_accept_state(ssl);
		//g_loop.setNonBlocking ( newsd, s->m_niceness );
		s->m_ssl = ssl;
		// wtf?
		if ( ! ssl ) {
			log("tcp: sslAccept had null ssl");
			return false;
		}
	}

	//log("ssl: SSL_accept %li",newsd);
 retry19:
	// javier put this in here, but it was not non-blocking!!!
	int r = SSL_accept(s->m_ssl);
	// did it block?
	if ( r < 0 && errno == EINTR ) goto retry19;
	// copy errno to g_errno
	if ( r < 0 ) g_errno = errno;
	// ignore harmless errors
	if ( g_errno == SSL_ERROR_WANT_READ ||
	     g_errno == SSL_ERROR_WANT_WRITE ||
	     g_errno == EAGAIN ) {
		//log("ssl: SSL_accept blocked %li",newsd);
		return true;
	}
	// any other?
	if ( g_errno ) {
		log("tcp: sslAccept: %s",mstrerror(g_errno));
		// too many open files?
		//if ( g_errno == 24 ) {
		//	if(closeLeastUsed()) return acceptSocket();
		//}
		return false;
	}

	// log this so we can monitor if we get too many of these per second
	// because they take like 10ms each on sp1!!! mdw
	log("ssl: SSL_accept (~10ms) completed %li",newsd);
	// ok, we got it
	s->m_sockState = ST_READING;
	return true;
}

// . NOTE: caller must free s->m_sendBuf/m_readBuf -- we don't do it at all
void TcpServer::makeCallback ( TcpSocket * s ) {
	if ( ! s->m_callback ) return;
	// record times for profiler
	//long address = (long)s->m_callback;
// 	unsigned long long start ;
// 	unsigned long long statStart,statEnd;
	//if ( g_conf.m_profilingEnabled ) {
// 		start = gettimeofdayInMillisecondsLocal();
// 		statStart=gettimeofdayInMilliseconds();
	//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
	//}
	//g_loop.startBlockedCpuTimer();	
	s->m_callback ( s->m_state , s );
	//if ( g_conf.m_profilingEnabled ) {
	//	if(!g_profiler.endTimer(address,__PRETTY_FUNCTION__))
	//		log(LOG_WARN,"admin: Couldn't add the fn %li",
	//		    (long)address);
	//}
}

// . cancel the transaction that had this state
// . g_errno should be set to ECANCELLED
void TcpServer::cancel ( void *state ) {
			 //void (*callback)(void *state, TcpSocket *s ) ) {
	for ( long i = 0 ; i <= m_lastFilled ; i++ ) {
		// get the TcpSocket for socket descriptor #i
		TcpSocket *s = m_tcpSockets[i];
		if ( ! s ) continue;
		if ( s->m_state != state ) continue;
		// set this before callback?
		g_errno = ECANCELLED;
		//     s->m_callback != callback ) continue;
		makeCallback  ( s );
		destroySocket ( s );
	}
}
