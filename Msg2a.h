//
// Copyright Gigablast, March 2005
// Author: Javier Olivares <jolivares@gigablast.com>
//
// Message to remake the catdb, filling it with dmoz category info
//

#ifndef _MSG2A_H_
#define _MSG2A_H_

#include "Url.h"
#include "Hostdb.h"
#include "Msg9b.h"

#define NUM_MINIMSG2AS 8

class Msg2a;

struct MiniMsg2a {
	//Multicast  m_mcast;
	long       m_index;
	Msg2a     *m_parent;
};

class Msg2a {
public:	
	Msg2a();
	~Msg2a();

	long fileRead ( int fileid, void *buf, size_t count );

	bool registerHandler ( );

	// main call to make catdb
	bool makeCatdb ( char  *coll,
			 long   collLen,
			 bool   updateFromNew,
			 void  *state,
			 void (*callback)(void *st) );

	//bool insertNextUrl ( long num );
	bool gotAllReplies();

	bool sendSwitchCatdbMsgs ( long num );

	// state
	void  *m_state;
	void (*m_callback)(void *st);
	// coll
	char *m_coll;
	long  m_collLen;

	// locals
	bool m_updateFromNew;
	long m_numUrls;
	long m_numUrlsSent;
	long m_numUrlsDone;

	// Msg9 buffer
	//Msg9 m_msg9s[NUM_MSG9S];
	Msg9b m_msg9b;

	// file stream
	//ifstream m_inStream;
	int m_inStream;

	// buffers
	char *m_urls;
	long  m_urlsBufferSize;
	long *m_catids;
	long  m_catidsBufferSize;
	unsigned char *m_numCatids;
	long  m_numNumCatids;
	long *m_updateIndexes;
	long  m_numUpdateIndexes;
	long  m_numRemoveUrls;

	// mini msg2as for switching to the updated catdb and categories
	MiniMsg2a m_miniMsg2as[NUM_MINIMSG2AS];
	long      m_msgsSent;
	long      m_msgsReplied;
	long      m_numMsgsToSend;
	char      m_msgData;
};

#endif
