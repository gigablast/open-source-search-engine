// Matt Wells, copyright Sep 2004

// . propagate config changes to all hosts via an http request
// . uses udp server
// . keeps sending forever to dead hosts


#ifndef _MSG28_H_
#define _MSG28_H_

#include "TcpSocket.h"
#include "HttpRequest.h"

class Msg28 {

 public:

	Msg28();
	~Msg28();

	bool massConfig ( char  *requestBuf              ,
			  void  *state                   ,
			  void (* callback) (void *state ) ) ;

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . sendToProxy only used to stop the proxy
	bool massConfig ( class TcpSocket *s, class HttpRequest *r , 
			  long hostId , 
			  void *state , void (* callback) (void *state) ,
			  bool ourselvesLast = true ,
			  bool sendToProxy = false ,
			  // this not -1 if specifying a range of docids
			  // in the closed interval, [hostId,hostId2]
			  long hostId2 = -1 );

	bool registerHandler ( ) ;

	bool doSendLoop ( );

	bool         m_ourselvesLast;
	void        *m_state;
	void      (* m_callback ) ( void *state );

	long  m_i;

	char *m_buf;
	long  m_bufSize;
	long  m_bufLen;

	long  m_numRequests;
	long  m_numReplies;
	long  m_numHosts;

	long  m_hostId;
	long  m_hostId2;

	bool  m_sendToProxy;
	long  m_sendTotal;

	bool  m_freeBuf;
};

#endif
