#include "gb-include.h"

#include "Msg30.h"
#include "UdpServer.h"
#include "Collectiondb.h"

static void gotReplyWrapper30 ( void *state , UdpSlot *slot ) ;
static void handleRequest30   ( UdpSlot *slot , int32_t niceness ) ;

bool Msg30::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x0A
	// . use the high prioirty udpServer for best response time
	//if ( ! g_udpServer2.registerHandler ( 0x30, handleRequest )) 
	//	return log("Msg0::set: udp server registration failed");
	// . can't use HOT high priority until request is smaller!!
	if ( ! g_udpServer.registerHandler ( 0x30, handleRequest30 )) 
		return false;
	return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . tells ALL hosts update to this coll rec
bool Msg30::update ( CollectionRec *rec      ,
		     bool           deleteIt ,
		     void          *state    , 
		     void         (* callback)(void *state ) ) {
	// bail if this is an interface machine, don't modify the main
	if ( g_conf.m_interfaceMachine ) return true;
	// remember callback parms
	m_state    = state;
	m_callback = callback;
	// quick delete send
	if ( deleteIt ) {
		// include the terminating \0
		m_sendBufSize = gbstrlen ( rec->m_coll ) + 1;
		gbmemcpy ( m_sendBuf , rec->m_coll , m_sendBufSize );
	}
	else {
		// serialize the rec into m_sendBuf
		m_sendBufSize = sizeof(CollectionRec);
		gbmemcpy ( m_sendBuf , rec , sizeof(CollectionRec) );
	}
	// reset some parms
	m_requests = 0;
	m_replies  = 0;
	// send a Msg30 to all hosts so they update it!
	int32_t n = g_hostdb.getNumHosts();
	for ( int32_t i = 0; i < n ; i++ ) {
		// get the ith host
		Host *h = g_hostdb.getHost ( i );
		// not to THIS host, however
		if ( h->m_hostId == g_hostdb.m_hostId ) continue;
		// # requests sent
		m_requests++;
		// . send to him
		// . returns false and sets g_errno on error
		// . NEVER timeout... like Msg10 (add url)
		if ( g_udpServer.sendRequest ( m_sendBuf    ,// request
					       m_sendBufSize,// requestLen
					       0x30         ,// msgType 0x30
					       h->m_ip      ,
					       h->m_port    ,
					       h->m_hostId  ,
					       NULL         , // slotPtr
					       this         , // state data
					       gotReplyWrapper30 ,
					       60*60*24*365 ) ) continue;
		// log it
		log("admin: Error sending collection record update request: "
		    "%s.",mstrerror(g_errno));
		// got it w/o blocking?? how?
		g_errno = 0;
		// mark reply as read
		m_replies++;
	}
	// return false if waiting on any reply
	if ( m_replies < m_requests ) return false;
	// got em all!
	return true;
}

void gotReplyWrapper30 ( void *state , UdpSlot *slot ) {
	// log it
	if ( g_errno ) 
		log("admin: Received error sending collection record update "
		    "request: %s.",mstrerror(g_errno));
	// get the state
	Msg30 *THIS = (Msg30 *)state;
	// don't let udpserver free the sendbuf, we own it
	slot->m_sendBufAlloc = NULL;
	// count reply
	THIS->m_replies++;
	// continue if not done reading replies yet
	if ( THIS->m_replies < THIS->m_requests ) return;
	// otherwise, call callback
        THIS->m_callback ( THIS->m_state );
}	

// . reply to a request for an RdbList
// . MUST call g_udpServer::sendReply or sendErrorReply() so slot can
//   be destroyed
void handleRequest30 ( UdpSlot *slot , int32_t niceness ) {
	// get what we've read
	char *readBuf     = slot->m_readBuf;
	int32_t  readBufSize = slot->m_readBufSize;
	// is it a delete?
	if ( readBufSize < (int32_t)sizeof(CollectionRec) ) {
		char *coll = readBuf;
		g_collectiondb.deleteRec ( coll );
		return;
	}
	CollectionRec *cr = (CollectionRec *)readBuf;
	// otherwise add/update it
	if ( ! g_collectiondb.addRec (cr->m_coll,NULL,0,true,-1,
				      false , // isdump?
				      true  )) { // save it?
		log("admin: Failed to update new record.");
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}
	// set it
	CollectionRec *nr = g_collectiondb.getRec ( cr->m_coll );
	if ( ! nr ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"admin: New collection added but not found.");
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}
	// set to what it should be
	gbmemcpy ( nr , cr , sizeof(CollectionRec) );
	// always return a reply immediately, even though list not loaded yet
	g_udpServer.sendReply_ass ( NULL , 0 , NULL , 0 , slot );
}
