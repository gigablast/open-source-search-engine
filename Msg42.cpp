#include "gb-include.h"

#include "Msg42.h"

static void gotReplyWrapper42 ( void *state , void *state2 ) ;
static void handleRequest42 ( UdpSlot *slot , int32_t netnice ) ;

bool Msg42::registerHandler ( ) {
        // . register ourselves with the udp server
        // . it calls our callback when it receives a msg of type 0x42
        if ( ! g_udpServer.registerHandler ( 0x42, handleRequest42 )) 
                return false;
        return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "termIds/termFreqs" should NOT be on the stack in case we block
// . i based this on ../titledb/Msg23.cpp 
bool Msg42::getTermFreq ( char      *coll       ,
			  int32_t       maxAge     ,
			  int64_t  termId     ,
			  void      *state      ,
			  void (* callback)(void *state ) ,
			  int32_t       niceness   ) {
	// warning
	if ( ! coll ) log(LOG_LOGIC,"net: NULL collection. msg42.");
	// keep a pointer for the caller
	m_state    = state;
	m_callback = callback;
	m_termFreq = 0LL;

	m_errno    = 0LL;

	// TODO: have a local by-pass for speed
	// if we have this termlist local then we can skip the network stuff
	//if ( g_linkdb.isLocal ( termId ) ) { return getTermFreqLocally(); }

	// . now what group do we belong to?
	// . groupMask has hi bits set before it sets low bits
	//uint32_t groupId = key.n1 & g_hostdb.m_groupMask;
	uint32_t groupId = g_linkdb.getGroupIdFromUrlHash(termId);

	// . make a request
	// . just send the termId and collection name
	char *p = m_request;
	*(int64_t *)p = termId ; p += 8;
	strcpy ( p , coll ); p += gbstrlen(coll) + 1; // copy includes \0

	int32_t timeout = 5;
	// in case it fails somehow
	*(int64_t *)m_reply = 0LL;
	
	// .  multicast to a host in group
	// . returns false and sets g_errno on error
	if ( ! m_mcast.send ( m_request    , 
			      p - m_request, // request size
			      0x42         , // msgType 0x42
			      false        , // multicast owns msg?
			      groupId      ,
			      false        , // send to whole group?
			      termId       , // key is termId
			      this         , // state data
			      NULL         , // state data
			      gotReplyWrapper42 ,
			      timeout      ,
			      //5          , // 5 second timeout
			      niceness     ,
			      false        , // realtime?
			      -1           , // first hostid
			      m_reply      ,
			      8            ,
			      false        ) ) { // free reply buf?
		log("query: Got error sending request for term "
		    "frequency: %s.", mstrerror(g_errno));
		return true;
	}
	return false;
}

void gotReplyWrapper42 ( void *state , void *state2 ) {
	Msg42 *THIS = (Msg42 *)state;
	if ( g_errno ) THIS->m_errno = g_errno;

	// gotReply() does not block, and does NOT call our callback
	if ( ! THIS->m_errno ) THIS->gotReply( ) ;
	// call callback since we blocked, since we're here
	THIS->m_callback ( THIS->m_state );
}

void Msg42::gotReply ( ) {
	// sanity check
	if ( m_termFreq ) { char *xx = NULL; *xx = 0; }

	// . get best reply for multicast
	// . we are responsible for freeing it
	int32_t  replySize;
	int32_t  replyMaxSize;
	bool  freeit;
	char *reply = m_mcast.getBestReply( &replySize, &replyMaxSize, 
					    &freeit );
	// if no reply freak out!
	if ( reply != m_reply ) 
		log(LOG_LOGIC,"query: Got bad reply for term "
		    "frequency. Bad.");
	// buf should have the # of records for m_termId
	m_termFreq = *(int64_t *)m_reply ;
}

// . handle a request to get a linkInfo for a given docId/url/collection
// . returns false if slot should be nuked and no reply sent
// . sometimes sets g_errno on error
void handleRequest42 ( UdpSlot *slot , int32_t netnice ) {
	// get the request
        char *request     = slot->m_readBuf;
        int32_t  requestSize = slot->m_readBufSize;
	// if niceness is 0, use the higher priority udpServer
	UdpServer *us = &g_udpServer;
	//if ( netnice == 0 ) us = &g_udpServer2;
        // ensure it's size
        if ( requestSize <= 8 ) {
		log("query: Got bad request size of %"INT32" for term frequency.",
		    requestSize);
                us->sendErrorReply ( slot , EBADREQUESTSIZE ); 
		return;
	}
	int64_t  termId = *(int64_t *) (request) ; 
	char      *coll   = request + 8;

	int64_t termFreq = g_linkdb.getTermFreq(coll,termId);
	// serialize it into a reply buffer 
	// no need to malloc since we have the tmp buf
	char *reply = slot->m_tmpBuf;
	*(int64_t *)reply = termFreq ;
	// . send back the buffer, it now belongs to the slot
	// . this list and all our local vars should be freed on return
	us->sendReply_ass ( reply , 8 , reply , 8 , slot );
	return;
}
