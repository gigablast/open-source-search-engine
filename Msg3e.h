// . This message class determines if the parms have changed
//   while a host is down, and resyncs them if they have


#ifndef _MSG3E_H_
#define _MSG3E_H_

#include "UdpServer.h" // for sending/handling requests
#include "Parms.h" 

class Msg3e {

 public:

	// register our 0x3f handler function
	bool registerHandler ( );

	// see if parms have changed
	void checkForNewParms ( );

	// send checksum to host
	void sendChecksum( Host *h );

	// send parms to host
	bool sendParms( Host *h );

	// public so C wrapper can call
	void gotReply ( ) ;

	// callback information
	void  *m_state  ;
	void (* m_callback)(void *state );

	// request buffer is just 4 bytes
	char m_request[ 4 ]; 
	char *m_reply; 
	int32_t m_replySize;

	char m_goodReply;

	int32_t m_errno;
};

#endif
