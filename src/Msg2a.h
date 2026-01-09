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
	int32_t       m_index;
	Msg2a     *m_parent;
};

class Msg2a {
public:	
	Msg2a();
	~Msg2a();

	int32_t fileRead ( int fileid, void *buf, size_t count );

	bool registerHandler ( );

	// main call to make catdb
	bool makeCatdb ( char  *coll,
			 int32_t   collLen,
			 bool   updateFromNew,
			 void  *state,
			 void (*callback)(void *st) );

	//bool insertNextUrl ( int32_t num );
	bool gotAllReplies();

	bool sendSwitchCatdbMsgs ( int32_t num );

	// state
	void  *m_state;
	void (*m_callback)(void *st);
	// coll
	char *m_coll;
	int32_t  m_collLen;

	// locals
	bool m_updateFromNew;
	int32_t m_numUrls;
	int32_t m_numUrlsSent;
	int32_t m_numUrlsDone;

	// Msg9 buffer
	//Msg9 m_msg9s[NUM_MSG9S];
	Msg9b m_msg9b;

	// file stream
	//ifstream m_inStream;
	int m_inStream;

	// buffers
	char *m_urls;
	int32_t  m_urlsBufferSize;
	int32_t *m_catids;
	int32_t  m_catidsBufferSize;
	unsigned char *m_numCatids;
	int32_t  m_numNumCatids;
	int32_t *m_updateIndexes;
	int32_t  m_numUpdateIndexes;
	int32_t  m_numRemoveUrls;

	// mini msg2as for switching to the updated catdb and categories
	MiniMsg2a m_miniMsg2as[NUM_MINIMSG2AS];
	int32_t      m_msgsSent;
	int32_t      m_msgsReplied;
	int32_t      m_numMsgsToSend;
	char      m_msgData;
};

#endif
