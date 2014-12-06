// Zak Betz, copyright Gigablast Jan 2005

//Get stuff from remote hosts...
//


#ifndef _MSG1F_H_
#define _MSG1F_H_
#include "UdpSlot.h"
#include "UdpServer.h"
#include "TcpSocket.h"


class Msg1f {
 public:
        Msg1f();
        ~Msg1f();
	
	static bool init();

	static bool getLog(int32_t hostId, 
			   int32_t numBytes, 
			   void *state, 
			   void ( *callback) (void *state, UdpSlot* slot));

	
 private:
/* 	TcpSocket *m_socket; */
/* 	void *m_state; */
/* 	void ( *m_callback) (void *state)); */
};


#endif
