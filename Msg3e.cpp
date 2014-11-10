#include "gb-include.h"

#include "Msg3e.h"
#include "Parms.h"

static void gotReplyWrapper3e ( void *state , UdpSlot *slot ) ;
static void handleRequest3e ( UdpSlot *slot , int32_t netnice ) ;
static void trySyncConf ( int fd, void *state );


// replace the broadcast() crap in Pages.cpp
// . just update your collection rec on host #0 then call this
// . this will send a msg3e to each host
// . when doing a reset operation 
//syncCollections ( ) {

bool Msg3e::registerHandler ( ) {
        // . register ourselves with the udp server
        // . it calls our callback when it receives a msg of type 0x3e
        if ( ! g_udpServer.registerHandler ( 0x3e, handleRequest3e )) 
                return false;
        return true;
}

// sleep callback that trys host 0 up to 5 times
void trySyncConf ( int fd, void *state ) {
	Msg3e *THIS = (Msg3e *)state;

	static int retryCnt = 0;
	if ( ++retryCnt > 5 ) {
		log( LOG_WARN, "Host 0 dead, cannot sync parms" );
		g_loop.unregisterSleepCallback( state, trySyncConf );
		return;
	}

	Host *h = g_hostdb.getHost( 0 );
	if ( h == NULL || g_hostdb.isDead ( h ) ) {
		return;
	}

	g_loop.unregisterSleepCallback( state, trySyncConf );

	THIS->sendChecksum( h );
}


// . picks a host and sends checksum of current parms
// . returns false if blocked, true otherwise
// . sets g_errno on error
void Msg3e::checkForNewParms ( ) {
	Host *h = g_hostdb.getMyHost ();
	if ( h->m_hostId == 0 ) {
		// don't sync with self
		return;
	}

	if ( g_hostdb.getNumHosts() < 2 ) {
		// only 1 host, nothing to sync with
		return;
	}

	// try to send checksum to host 0
	g_loop.registerSleepCallback( 13500, this, trySyncConf , 0 );
}

// send the checksum to selected host
void Msg3e::sendChecksum( Host *h ) {
	// get our checksum
	uint32_t cs = g_parms.calcChecksum();
        uint32_t *request = (uint32_t *)mmalloc( sizeof( cs ),
							  "req checksum");
	if ( ! request ) {
		log("Unable to alloc memory for sync request");
		return;
	}
	*request = cs;

	m_errno = 0;

	// send our checksum
	if ( ! g_udpServer.sendRequest ( (char *)request ,
		 		         sizeof( cs )  , // request size
			 	         0x3e          ,
				         h->m_ip       ,
				         h->m_port     ,
				         h->m_hostId   ,
				         NULL          ,
				         this          ,
				         gotReplyWrapper3e ) ) {
	}
}

void gotReplyWrapper3e ( void *state , UdpSlot *slot ) {
	Msg3e *THIS = (Msg3e *)state;
	if ( g_errno ) THIS->m_errno = g_errno;
	// gotReply() does not block, and does NOT call our callback
	if ( ! THIS->m_errno ) {
		THIS->m_reply = slot->m_readBuf;
		THIS->m_replySize = slot->m_readBufSize;
		THIS->gotReply( ) ;
	}
}

// . checks to make sure parms are the same (a reply size of one)
// . if not the same, sets these parms to the new parms
void Msg3e::gotReply ( ) {
	// when reply size is 1, parms match, so don't do anything
	if ( m_replySize == 1 ) return;

	// otherwise, deserialize parms to set the new values
	g_parms.deserialize( m_reply );
}

// . handle a request to set parms
void handleRequest3e ( UdpSlot *slot , int32_t netnice ) {
	// get the request
        char *request     = slot->m_readBuf;

	uint32_t otherChecksum = *(uint32_t *)request; 
	uint32_t myChecksum  = g_parms.calcChecksum ();

	char *reply = NULL;
	int32_t replySize = 0L;

	// check if parms are the same
	if ( myChecksum != otherChecksum ) { 
		// reply with parms
		replySize = g_parms.getStoredSize ();
		reply = (char *)mmalloc( replySize, "parms serialized buf" );
		if ( ! reply ) {
			log( LOG_WARN, "Cannot alloc %"INT32" bytes for sync"
			               "reply buffer", replySize );
			return;
		}
		
		if ( ! g_parms.serialize( reply, &replySize ) ) {
			mfree( reply, replySize, "parms serialized buf" );
			return;
		}

		// send our reply
		g_udpServer.sendReply_ass ( reply     , // msg 
					    replySize , // msgSize
					    reply     , // alloc
					    replySize , // alloc size
					    slot      ,
					    60        , // timeout in seconds
					    NULL      , // state
					    NULL      , // callback
					    500       , // backoff in ms
					    1000      , // max wait for backoff
					    true );     // use same switch?
	} 
	else {
		// reply with 1 byte for parm match
		static char s_gdReply = 0x01;

		// send our reply
		g_udpServer.sendReply_ass ( &s_gdReply, // msg 
					    1         , // msgSize
					    NULL      , // alloc
					    0         , // alloc size
					    slot      ,
					    60        , // timeout in seconds
					    NULL      , // state
					    NULL      , // callback
					    500       , // backoff in ms
					    1000      , // max wait for backoff
					    true );     // use same switch?
	}

	return;
}

