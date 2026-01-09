// Matt Wells, copyright Feb 2001

// . get a catdb record over the network
// . uses ../rdb/Msg0.h to extract RdbList of "interesting" records

#ifndef _MSG8B_H_
#define _MSG8B_H_
#include "gb-include.h"
#include "Catdb.h"
#include "CatRec.h"
#include "UdpServer.h"
#include "Xml.h"
#include "Msg0.h"
#include "Msg22.h"
#include "MsgC.h"
#include "HashTableT.h"
#define MSG8BQUEUE_SIZE         256
#define MSG8BQUEUE_MAX_ATTACHED 64

// coll, url, niceness, rdbid, useCanonicalName
#define MSG8B_REQ_SIZE (MAX_COLL_LEN + MAX_URL_LEN + 5)

class Msg8b {

 public:
	
	bool registerHandler();

	// . if record does not exist we just set it to default record
	// . rdb just consists of keys used to lookup the template recs
	// . if defaultVersion is -1 we use the most current
	// . if template key is -1 we look in the little record for this site
	// . New param updateFlag is added to differentiate between
	//   the callers intention( read or write).
	//   updateFlag=true corresponds to the list add/del/update operation
	bool getCatRec ( Url     *url              , 
			 char    *coll             , 
			 int32_t     collLen          ,
			 bool     useCanonicalName ,
			 int32_t     niceness         ,
			 CatRec  *rec              ,
			 void    *state            ,
			 //void   (* callback)(void *state , CatRec *rec ) );
			 void   (* callback)(void *state ) );

	// private:
	bool gotList ( ) ;

	// got forwarded reply
	void gotReply ( );

	// get indirect catids for catdb
	void getIndirectCatids ( );

	// . checks the Msg8 queue for the desired list
	// . if it exists, it will attach this Msg8 to it and set m_queueSlave
	// . if it doesn't, it will setup a new slot in the queue and set
	//   m_queueMaster
	// . if the queue is full, both master and slave will be false and the
	//   local RdbList will be used
	// . returns true if attached to queue, false if not and msg0 should
	//   be called
	bool checkQueueForList ( uint32_t domainHash );

	// process queue slaves
	void processSlaves ( );
	// clean the master slot
	void cleanSlot     ( );

	// some specified input
	//char  *m_coll;
	//int32_t   m_collLen;
	Url   *m_url;

	//collnum_t m_collnum;

	void    (*m_callback ) ( void *state );//, CatRec *rec );
	void     *m_state;      // ptr to caller's private state data

	Msg0      m_msg0;  // getList()

	// . output
	// . the tagdb record is stored here
	CatRec *m_cr;

	// hold possible tagdb records
	RdbList *m_list;
	RdbList  m_localList;

	bool m_queueMaster;
	bool m_queueSlave;
	int32_t m_queueSlot;

	//bool m_triedIp;

	int32_t m_defaultSiteFileNum;

	int32_t m_niceness;

	// for forwarding
	//uint32_t m_groupId;
	uint32_t m_shardNum;
	char          m_request[MSG8B_REQ_SIZE];
	int32_t          m_requestSize;
	Multicast     m_mcast;

	// normalized url
	Url       m_normalizedUrl;

	void     *m_parent;
	int32_t      m_slotNum;
	int32_t      m_slotNum2;

	// used by MsgE to store its data
	void *m_state2;
	void *m_state3;
};

struct Msg8bListQueue {
	RdbList        m_list;
	Msg8b          *m_masterMsg8b;
	Msg8b          *m_attachedMsg8bs[MSG8BQUEUE_MAX_ATTACHED];
	int32_t           m_numAttached;
	uint32_t  m_domainHash;
	char           m_isOpen;
};


#endif
