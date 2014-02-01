// Copyright Matt Wells Nov 2000

// . TODO: don't keep alloc'ing TcpSockets, re-use "empty" TcpSockets from 
//         an array of them (grow array if needed) (do same for UdpServer)

// . used as client AND server
// . used to do non-blocking sends/recieves of messages using TCP/IP
// . re-uses sockets (keep alive) in server AND client directions
// . uses custom dns client (Dns.h) for non-blocking domain name lookups
// . a callback can be specified for each TcpSocket
// . a pointer to custom callback data can also be specified for each socket
// . that callback is called on connect/read/write/dnsLookup TIMEOUT or ERROR
// . callback is also called on reception of REPLY
// . receptions of REQUESTS are received thru TcpSocket*TcpServer::read(int sd)
// . if a msg being received is too big we make call(s) to putMsgPiece() to
//   empty the receive buffer when it gets full (m_maxReadBufSize)

#ifndef _TCPSERVER_H_
#define _TCPSERVER_H_

#include <sys/time.h>             // time()
#include <sys/types.h>            // setsockopt()
#include <sys/socket.h>           // setsockopt()
#include <netinet/tcp.h>          // TCP_CORK and SOL_TCP (linux only!)
#include <openssl/ssl.h>          // for ssl stuff
#include <openssl/crypto.h>
#include <openssl/err.h>
#include "Mem.h"     // for mem routines
#include "MsgC.h"           // for udp-only, non-blocking dns lookups
#include "TcpSocket.h"            
#include "Loop.h"      // g_loop.registerRead/WriteCallback()

// raised from 5k to 15k in case we are a spider compression proxy
#define MAX_TCP_SOCKS 15000

extern bool g_isYippy;

class TcpServer {

	friend class HttpServer;

 public:

	// free all TcpSockets and their bufs
	void reset();

	// . constructor
	TcpServer() { m_port = -1; m_sock = -1; m_useSSL = false; m_ctx = NULL; };

	// . creates a tcp socket which listens on port "port"
	// . will close unused sockets to ensure we stay under "maxSockets"
	// . receiving msgs bigger then "16k" will result in a call to
	//   TcpSocket::putMsgPiece(), if not NULL
	// . we call "requestHandler" when an incoming request arrives
	// . IMPORTANT: requestHandler MUST call sendMsg(s,...) eventually
	// . getMsgSize is called to get the total size of an incoming msg
	// . it returns -1 if it doesn't yet know
	bool init ( void (* requestHandler)(TcpSocket *s) ,
		    long (* getMsgSize    )(char *msg , long msgBytesRead,
					    TcpSocket *s ),
		    long (* getMsgPiece   )(TcpSocket *s ),
		    short     port                        , 
		    long     *maxSocketsPtr = NULL        , //MAX_TCP_SOCS def
		    bool      useSSL = false              );
		    //long      maxReadBufSize = 128*1024  , 
		    //long      maxSendBufSize = 128*1024  );

	bool testBind ( unsigned short port ) ;

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . uses g_dns to find the ip then calls sendRequest() below
	// . callback is called on completion of TRANSACTION
	// . that is, when both m_sendBuf and m_readBuf have been filled
	// . callback is also called on error
	// . default timeout of 60 secs of no read OR no write
	bool sendMsg ( char  *hostname ,
		       long   hostnameLen ,
		       short  port     ,
		       char  *sendBuf  ,
		       long   sendBufSize ,
		       long   sendBufUsed ,
		       long   msgTotalSize ,
		       void  *state    ,
		       void  (* callback )( void *state, TcpSocket *s ) ,
		       long   timeout   , // 60*1000 
		       long   maxTextDocLen ,  // -1 for no max
		       long   maxOtherDocLen );

	bool sendMsg ( char *url ,
		       char  *sendBuf  ,
		       long   sendBufSize ,
		       long   sendBufUsed ,
		       long   msgTotalSize ,
		       void  *state    ,
		       void  (* callback )( void *state, TcpSocket *s ) ,
		       long   timeout   , // 60*1000 
		       long   maxTextDocLen ,  // -1 for no max
		       long   maxOtherDocLen );
		       

	bool sendChunk ( class TcpSocket *s ,
			 class SafeBuf *sb ,
			 void *state ,
			 // call this function when done sending this chunk
			 // so that it can read another chunk and call 
			 // sendChunk() again.
			 void (* doneSendingWrapper)( void *state,TcpSocket *),
			 bool lastChunk );

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . use this for sending a msg to another host
	
	// . if a reply is expected then you should specify the "done" callback
	//   which will be called on complete reception of the reply
	// . after completion/done this will call reseTcpSocket()
	// . upon successful transmision of "msg" we shift socket into readMode
	// . default timeout of 60 secs of no read OR no write
	bool sendMsg ( long   ip      ,
		       short  port    ,
		       char  *sendBuf  ,
		       long   sendBufSize ,
		       long   sendBufUsed ,
		       long   msgTotalSize ,
		       void   *state   ,
		       void  (* callback )( void *state , TcpSocket *s ) ,
		       long   timeout   , // 60*1000 
		       long   maxTextDocLen ,  // -1 for no max
		       long   maxOtherDocLen );

	// . send request over an available (pre-connected) TcpSocket
	// . destroys the socket on error
	// . calls callback on completion of transaction
	// . default timeout of 60 secs of no read OR no write
	bool sendMsg ( TcpSocket *s            , 
		       char      *sendBuf  ,
		       long       sendBufSize ,
		       long       sendBufUsed ,
		       long       msgTotalSize ,
		       void      *state        ,
		       void      (* callback )( void *state , TcpSocket *s ),
		       long       timeout = 60*1000 ,
		       long       maxTextDocLen = -1 , // -1 for no max
		       long       maxOtherDocLen = -1 );

	// . the following public funcs are public so C wrappers can call them
	// . you should not call them

	// . we use this as a callback called by DnsServer
	// . gotta keep this public cuz getIpWrapper() calls it
	// . returns true if didn't block when sending msg (using sendRequest)
	bool       gotTcpServerIp ( class TcpState *tst , long ip );

	// get a TcpSocket from a socket descriptor
	TcpSocket *getSocket          ( int sd ) ;

	long       readSocket         ( TcpSocket *s );
	long       writeSocket        ( TcpSocket *s );
	void       readTimeoutPoll    ( ) ;

	TcpSocket *acceptSocket       ( ) ;
	bool sslAccept ( class TcpSocket *s ) ;

	// keep public so PageResults can call if had error getting query results
	void       destroySocket      ( TcpSocket *s ) ;

	// calls s->m_callback ( s->m_state , s )
	void       makeCallback       ( TcpSocket * s ) ;

	void       recycleSocket      ( TcpSocket *s ) ;

	// only wrappers should call this 
	long       connectSocket      ( TcpSocket *s ) ;

	// set from init() to handle incoming requests
	void (* m_requestHandler)(TcpSocket *s) ;

	// cancel the transaction that had this state
	void cancel ( void *state );
	           // void (*callback)(void *state, TcpSocket *s) ) ;

	// private:

	TcpSocket *getAvailableSocket ( long ip, short port ) ;
	TcpSocket *getNewSocket       ( ) ;
	TcpSocket *wrapSocket         ( int sd , long niceness, bool incoming);
	bool       closeLeastUsed     ( long maxIdleTime = -1 ) ;
	bool       setTotalToRead     ( TcpSocket *s ) ;

	// . we call this to try to figure out the size of the WHOLE msg
	//   being read so that we might pre-allocate memory for it
	// . overriden for different protocols
        // . this is called upon reception of every packet
        //   of the msg being read until a non-negative msg size is returned.
        //   this is used to avoid doing excessive reallocs and extract the
        //   reply size from things like "Content-Length: xxx" so we can do
        //   one alloc() and forget about having to do more...
	// . up to 128 bytes of the reply can be stored in a static buffer
	//   contained in TcpSocket, until we need to alloc...
	//	virtual long getMsgSize ( char *buf , long bufSize ) ;

	int         m_sock ;  // for listening for connections
	short       m_port ;  // for listening for connections

	// handlers set in the init() routine

	// ptrs to our TcpSockets 1-1 w/ real sockets
	TcpSocket *m_tcpSockets [ MAX_TCP_SOCKS ];
	long       m_lastFilled;
	long       m_numUsed;
	// # used for incoming connections
	long       m_numIncomingUsed;
	// let's have them all pre-allocated, it's only ~1.1MB...
	TcpSocket  m_actualSockets [ MAX_TCP_SOCKS ];


	// . how many socket descriptors can we use simultaneously?
	// . just applies to incoming sockets
	long       m_dummy;
	long      *m_maxSocketsPtr;
       
	bool m_doReadRateTimeouts;

	//MsgC m_msgc;
	
	// . we don't wanna use too much memory reading/sending a big msg
	//   so we have a limit on our buffer sizes (set with set())
	//long        m_maxReadBufSize;
	//long        m_maxSendBufSize;

	// these callbacks should be set in init()
	long (* m_getMsgSize    )(char *msg , long msgBytesRead, TcpSocket *s);
	long (* m_getMsgPiece   )(TcpSocket *s );

	// flag to specify SSL or not
	bool m_useSSL;

	// SSL members
	SSL_CTX *m_ctx;

	// ready to go or not
	bool m_ready;
};

#endif
