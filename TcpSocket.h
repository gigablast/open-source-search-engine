// . this is a virtual TCP socket (TcpSocket)
// . they are 1-1 with all socket descriptors
// . it's used to control the non-blocking polling etc. of the sockets
// . we also use it for re-using sockets w/o having to reconnect

#ifndef _TCPSOCKET_H_
#define _TCPSOCKET_H_

#include <sys/time.h>             // timeval data type
#include <openssl/ssl.h>

// . this specifies max # of bytes to do in a read() statement
// . we use stack space for the read's buffer so this is how much stack space
// . we usually copy the stack to a permanent malloc'ed buffer 
// . we read into this buf first to get the msg size (Content-Length: xxxx)
#define READ_CHUNK_SIZE (10*1024)

// . states of a non-blocking TcpSocket 
// . held by TcpSocket's m_sockState member variable
#define ST_AVAILABLE        0   // means it's connected but not being used
#define ST_CONNECTING       2
//#define ST_CLOSED           3
#define ST_READING          4
#define ST_WRITING          5
#define ST_NEEDS_CLOSE      6
#define ST_CLOSE_CALLED     7
#define ST_SSL_ACCEPT       8
#define ST_SSL_SHUTDOWN     9

#define TCP_READ_BUF_SIZE 1024

class TcpSocket {

 public:

	// some handy little thingies...
	bool isAvailable     ( ) { return ( m_sockState == ST_AVAILABLE  ); };
	bool isConnecting    ( ) { return ( m_sockState == ST_CONNECTING ); };
	//bool isClosed      ( ) { return ( m_sockState == ST_CLOSED     ); };
	bool isReading       ( ) { return ( m_sockState == ST_READING ||
					    m_sockState == ST_SSL_ACCEPT ); };
	bool isSending       ( ) { return ( m_sockState == ST_WRITING    ); };
	bool isReadingReply  ( ) { return ( isReading() && m_sendBuf); };
	bool isSendingReply  ( ) { return ( isSending() &&   m_readBuf); };
	bool isSendingRequest( ) { return ( isSending() && ! m_readBuf); };
	bool sendCompleted   ( ) { return ( m_totalSent == m_totalToSend ); };
	bool readCompleted   ( ) { return ( m_totalRead == m_totalToRead ); };

	void setTimeout   (long timeout ) { m_timeout = timeout; };


	// . call m_callback when on transcation completion, error or timeout
	// . m_sockState is the caller's state data
	void           (* m_callback )( void *state , TcpSocket *socket );
	void            *m_state;

	class TcpServer *m_this;

	int         m_sd;               // socket descriptor
	char       *m_hostname;         // may be NULL
 	long long   m_startTime;        // time the send/read started
	long long   m_lastActionTime;   // of send or receive or connect

	// m_ip is 0 on dns lookup error, -1 if not found
	long        m_ip;               // ip of connected host
	short       m_port;             // port of connected host
	char        m_sockState;        // see #defines above

	// userid that is logged in
	//long m_userId32;

	// . getMsgPiece() is called when we need more to send
	char       *m_sendBuf;
	long        m_sendBufSize;
	long        m_sendOffset;
	long        m_sendBufUsed; // how much of it is relevant data
	long        m_totalSent;   // bytes sent so far
	long        m_totalToSend;

	// NOTE: for now i've skipped allowing reception of LARGE msgs and
	//       thereby freezing putMsgPiece() for a while
	// . putMsgPiece() is called to flush m_readBuf (if > m_maxReadBufSize)
	char       *m_readBuf;        // might be NULL if unalloc'd
	long        m_readBufSize;    // size of alloc'd buffer, m_readBuf
	long        m_readOffset;     // next position to read into m_readBuf
	//long        m_storeOffset;  // how much of it is stored (putMsgPiece)
	long        m_totalRead;    // bytes read so far
	long        m_totalToRead;    // -1 means unknown
	//void       *m_readCallbackData; // maybe holds reception file handle

	//char        m_tmpBuf[TCP_READ_BUF_SIZE];

	char        m_waitingOnHandler;
	
	char        m_prefLevel;
	// is it in incoming request socket?
	char        m_isIncoming;

	// . is the tcp socket originating from a compression proxy?
	// . 0x01 means we need to compress the reply to send back to 
	//   a query compression proxy
	char        m_flags;

	// timeout (ms) relative to m_lastActionTime (last read or write)
	long        m_timeout;

	// . max bytes to read as a function of content type
	// . varies from collection to collection so you must specify it
	//   in call to HttpServer::getDoc()
	long        m_maxTextDocLen;  // if reading text/html or text/plain
	long        m_maxOtherDocLen; // if reading other doc types

	char        m_niceness;

	long m_shutdownStart;

	// SSL members
	SSL  *m_ssl;

	class UdpSlot *m_udpSlot;

	// used for debugging, PageResults.cpp sets this to the State0 ptr
	char *m_tmp;
};

#endif
