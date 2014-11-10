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
			  int32_t hostId , 
			  void *state , void (* callback) (void *state) ,
			  bool ourselvesLast = true ,
			  bool sendToProxy = false ,
			  // this not -1 if specifying a range of docids
			  // in the closed interval, [hostId,hostId2]
			  int32_t hostId2 = -1 );

	bool registerHandler ( ) ;

	bool doSendLoop ( );

	bool         m_ourselvesLast;
	void        *m_state;
	void      (* m_callback ) ( void *state );

	int32_t  m_i;

	char *m_buf;
	int32_t  m_bufSize;
	int32_t  m_bufLen;

	int32_t  m_numRequests;
	int32_t  m_numReplies;
	int32_t  m_numHosts;

	int32_t  m_hostId;
	int32_t  m_hostId2;

	bool  m_sendToProxy;
	int32_t  m_sendTotal;

	bool  m_freeBuf;
};

#endif
