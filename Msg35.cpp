#include "gb-include.h"

#include "Msg35.h"
#include "Errno.h"
#include "UdpServer.h"
#include "Hostdb.h"

// a global class extern'd in .h file
Msg35 g_msg35;

#define REQUEST_GETTOKEN     0
#define REQUEST_GIVETOKEN    1
#define REQUEST_RELEASETOKEN 2
#define REQUEST_SYNC         3

#define REPLY_GOTTOKEN       4
#define REPLY_WAITFORTOKEN   5

static void gotReplyWrapper35           ( void *state , UdpSlot *slot ) ;
static void gotReleaseTokenReplyWrapper ( void *state , UdpSlot *slot ) ;
static void giveTokenReplyWrapper       ( void *state , UdpSlot *slot ) ;
static void handleRequestWrapper35      ( UdpSlot *slot , int32_t niceness ) ;
static void sleepWrapper                ( int fd , void *state ) ;

bool Msg35::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x35
	if ( ! g_udpServer.registerHandler ( 0x35, handleRequestWrapper35 )) 
		return false;
	// call sync every 15 seconds
	if ( ! g_loop.registerSleepCallback (15000,NULL,sleepWrapper) )
		return false;
	return true;
}

Msg35::Msg35 () {
	reset();
}

void Msg35::reset () {
	// reset all client/server slots
	for ( int32_t i = 0 ; i < 64  ; i++ ) m_clientWaits[i].m_isEmpty = true;
	for ( int32_t i = 0 ; i < 512 ; i++ ) m_serverWaits[i].m_isEmpty = true;
	m_topUsedClient = -1;
	m_topUsedServer = -1;
	m_serverTokeni  = -1;
	m_clientTokeni  = -1;
	m_discrepancyHid = -1;
	m_allReceived    = false;
	memset ( m_flags , 0 , 16 );
}

//////////////////////////////////////////////////////////////////////
//////////////////////////   GET  TOKEN  /////////////////////////////
//////////////////////////////////////////////////////////////////////

#include "Threads.h"

// . returns false if blocked true otherwise
// . sets g_errno on error
bool Msg35::getToken ( void *state, 
		       void (*callback )(void *state), char priority ){
	// if threads are disabled, we are probably repairing dbs
	// from main.cpp fixTitleRecs() or makeDbs() so no token needed
	if ( g_threads.areThreadsDisabled() ) return true;
	// you can also disable the token so twins can merge as the same time
	if ( ! g_conf.m_useMergeToken ) return true;
	// disable this until it works again
	return true;
	// . if only one host per group, you always have the token
	// . no, they can only have one merge going at a time
	//if ( g_hostdb.getNumHostsPerShard() == 1 ) return true;
	// . ensure not already registered
	// . this can happen if a client's get request arrives before their
	//   release request... so allow for that now
	for ( int32_t i = 0 ; i < 64 ; i++ ) {
		if ( m_clientWaits[i].m_isEmpty        ) continue;
		if ( m_clientWaits[i].m_state != state ) continue;
		//g_errno = EBADENGINEER;
		log(LOG_REMIND,"merge: Already queued merge token request.");
		// return false since they'll be called when token comes
		//return false;
		break;
	}
	// get next available slot
	int32_t i;
	for ( i = 0 ; i < 64 ; i++ )
		if ( m_clientWaits[i].m_isEmpty ) break;
	// . if none empty bitch and return
	// . this should never happen, if it does than increase the limit
	//   otherwise, his callback will never be called!!
	if ( i >= 64 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"merge: msg35: Too many waiting for token.");
		return true;
	}
	ClientWait *c = &m_clientWaits [ i ];
	// get current time
	int32_t timestamp = getTimeGlobal();
	// the request is just the priority really
	char *p = c->m_buf;
	*p         = REQUEST_GETTOKEN ; p += 1;
	*(int32_t *)p = g_hostdb.m_hostId; p += 4;
	*(int32_t *)p = timestamp        ; p += 4;
	*p         = priority         ; p += 1;
	*p         = i;               ; p += 1; // client slot #
	// . send to the governing host, he must be up
	// . this returns NULL and sets g_errno on error
	Host *h = getTokenManager ( );
	// . the priority of this msg is low, use g_udpServer
        // . returns false and sets g_errno on error
	// . if there is a sending error, we will try sending token manager
	//   our client queue (queue of requests) during call to sync()
        if ( !  g_udpServer.sendRequest ( c->m_buf   ,
					  11         , // requestLen
					  0x35       , // msgType 0x35
					  h->m_ip    , // low priority ip
					  h->m_port  , // low priority port
					  h->m_hostId,
					  NULL       , // slotPtr
					  this       , // state data
					  gotReplyWrapper35 ,
					  31536000 ) ) { // 1 yr timeout
		log("merge: Got error sending merge token request: %s.",
		    mstrerror(g_errno));
		g_errno = 0;
	}
	// save callback info even if request not launched successfully since
	// it will be retried during call to sync()
	c->m_state     = state;
	c->m_callback  = callback;
	c->m_priority  = priority;
	c->m_timestamp = timestamp;
	c->m_isEmpty   = false;
	if ( i > m_topUsedClient ) m_topUsedClient = i;
	// we blocked waiting for the reply
	return false;
}

void gotReplyWrapper35 ( void *state , UdpSlot *slot ) {
	Msg35 *THIS = (Msg35 *)state;
	THIS->gotReply( slot );
}

void Msg35::gotReply ( UdpSlot *slot ) {
	// get the reply
	char *reply     = slot->m_readBuf;
	int32_t  replySize = slot->m_readBufSize;
	// don't let UdpServer free the send buffer
	slot->m_sendBufAlloc = NULL;
	// bitch if bad reply
	if ( ! g_errno && replySize != 1 ) g_errno = EBADREPLYSIZE;
	// on error we will try sending the request again via call to sync()
	if ( g_errno ) {
		log("merge: Received error reply when getting merge token: "
		    "%s.", mstrerror(g_errno));
		return;
	}
	// . sometimes we get it right away, without waiting
	// . get the client #
	int32_t n = reply[0];
	// -1 means we're waiting
	if ( n == -1 ) return;
	// debug msg
	log(LOG_DEBUG,"merge: msg35: Got merge token in reply.");
	// returns false and sets g_errno on error, true otherwise
	callCallback ( n );
}

// . call this once the token manager says you've got the token
// . returns false and sets g_errno on error, true otherwise
// . m_callback should call releaseToken() when done with it
bool Msg35::callCallback ( int32_t n ) {
	// ensure legit
	if ( n > m_topUsedClient || n < 0 ) {
		g_errno = EBADREQUEST;
		return log(LOG_LOGIC,"merge: msg35: Bad client slot = %"INT32".",n);
	}
	// if we already got the token somewhere, that is a problem
	if ( m_clientTokeni != -1 ) {
		g_errno = EBADREQUEST;
		return log(LOG_LOGIC,"merge: msg35: Manager tried to give "
			   "token to client #%"INT32", but #%"INT32" has it now.",
			   n , m_clientTokeni );
	}
	// if we're empty, do not accept
	ClientWait *c = &m_clientWaits[n];
	if ( c->m_isEmpty ) {
		g_errno = EBADREQUEST;
		return log(LOG_LOGIC,"merge: msg35: Token not needed.");
	}
	// bitch if double called, but don't send back an error
	if ( ! c->m_callback ) {
		log(LOG_LOGIC,"merge: msg35: Merge token repeat give.");
		return true;
	}
	// we got it local now
	m_clientTokeni = n;
	// we got the token now, call the callback of the winner
	// so he can do his dump or merge
	c->m_callback ( c->m_state );
	// now flag this so he don't get called again
	c->m_callback = NULL;
	return true;
}


Host *Msg35::getTokenManager ( ) {
	//int32_t numHosts;
	// take this out for now
	/*
	Host **hosts = g_hostdb.getTokenGroup ( g_hostdb.m_hostId, &numHosts);
	Host *h = hosts[0];
	*/
	Host *h = NULL;
	// now perfer the guy that shares his ide if he has lower hostid
	//Host *s = g_hostdb.getSharer ( h );
	//if ( s && s->m_hostId < h->m_hostId ) return s;
	// otherwise, we are the one
	return h;
	// even if he's dead, try sending to him forever until he awakes
	//if ( ! g_hostdb.isDead ( h ) ) return h;
	//log("Msg35::getTokenManager: manager is dead"); 
	//return NULL;
}

/*
Host **Msg35::getTokenGroup( int32_t *numHosts ) {
	static Host *s_hosts [ 16 ];
	static int32_t  s_numHosts = -1;
	// if we already made it, return it
	if ( s_numHosts >= 0 ) { *numHosts = s_numHosts ; return s_hosts ; }
	// otherwise, make the group that uses this token
	int32_t n;
	Host *g = g_hostdb.getGroup ( g_hostdb.m_groupId , &n );
	s_numHosts = 0;
	for ( int32_t i = 0 ; i < n && s_numHosts + 1 < 16 ; i++ ) {
		s_hosts [ s_numHosts++ ] = &g[i] ;
		// add ide sharer, if any
		Host *s = g_hostdb.getSharer ( &g[i] );
		if ( s ) s_hosts [ s_numHosts++ ] = s ;
	}
	// bitch on too many
	if ( s_numHosts >= 15 ) { char *xx = NULL ; *xx = 0; }
	*numHosts = s_numHosts;
	return s_hosts;
}
*/

//////////////////////////////////////////////////////////////////////
//////////////////////////  RELASE TOKEN  ////////////////////////////
//////////////////////////////////////////////////////////////////////

// call this when you are done with the token
void Msg35::releaseToken ( ) {
	// if threads are disabled, we are probably repairing dbs
	// from main.cpp fixTitleRecs() or makeDbs() so no token needed
	if ( g_threads.areThreadsDisabled() ) return;
	// . if only one host per group, you always have the token
	// . no, they can only have one merge going at a time
	//if ( g_hostdb.getNumHostsPerShard() == 1 ) return;
	// . send to the governing host, he must be up
	// . this returns NULL and sets g_errno on error
	Host *h = getTokenManager ( );
	// get the client Wait class #
	int32_t i = m_clientTokeni;
	// if we don't have the token, nothing to release
	if ( i < 0 ) { log(LOG_REMIND,"merge: msg35: releaseToken() called "
			   "but token not in possession."); return ; }
	// free it now because if we free it when we get the reply from
	// token manager, he may have already sent another token to us and we
	// may get the token before freeing this one, which is bad because
	// our m_clientTokeni will not be -1... so empty it here
	ClientWait *c = &m_clientWaits [ m_clientTokeni ];
	c->m_isEmpty = true;
	while ( m_topUsedClient>= 0 &&m_clientWaits[m_topUsedClient].m_isEmpty)
		m_topUsedClient--;
	m_clientTokeni = -1;
	// request buffer
	static char s_req = REQUEST_RELEASETOKEN;
	// . send the return request to the token manager
	// . the priority of this msg is low, use g_udpServer
        // . returns false and sets g_errno on error
        if ( g_udpServer.sendRequest ( &s_req     ,
				       1          , // requestLen
				       0x35       , // msgType 0x04
				       h->m_ip    , // low priority ip
				       h->m_port  , // low priority port
				       h->m_hostId,
				       NULL       , // slotPtr
				       this       , // state data
				       gotReleaseTokenReplyWrapper ,
				       31536000 ) ) // 1 yr timeout
		return;
	// damn, the request failed
	log("merge: Failed to send token request: %s.", mstrerror(g_errno));
	// clear error
	g_errno = 0;
	// sync() will update managers queue with our own ~30 secs from now
}

// this is called when the token manager replies to our RELEASETOKEN request
void gotReleaseTokenReplyWrapper ( void *state , UdpSlot *slot ) {
	Msg35 *THIS = (Msg35 *)state;
	// don't let UdpServer free the send buffer
	slot->m_sendBufAlloc = NULL;
	THIS->gotReleaseTokenReply();
}

void Msg35::gotReleaseTokenReply ( ) {
	// log any error
	if ( g_errno )
		log("merge: Merge token release reply had error: %s.",
		    mstrerror(g_errno));
	// debug msg
	else
		log(LOG_INFO,"merge: Successfully released merge token.");
	g_errno = 0;
}


//////////////////////////////////////////////////////////////////////
////////////////////////   HANDLE REQUESTS  //////////////////////////
//////////////////////////////////////////////////////////////////////

// this routine handles some requests from clients and one from the manager
void handleRequestWrapper35 ( UdpSlot *slot , int32_t niceness ) {
	g_msg35.handleRequest ( slot );
}

void Msg35::handleRequest ( UdpSlot *slot ) {
	int32_t  requestSize = slot->m_readBufSize;
	char *request     = slot->m_readBuf;
	// . get request code
	// . REQUEST_GETTOKEN
	// . REQUEST_GIVETOKEN
	// . REQUEST_RELEASETOKEN
	// . REQUEST_SYNC

	// this is from manager to client, giving the token to the client
	if ( requestSize == 2 && *request == REQUEST_GIVETOKEN ) {
		// get the client #
		int32_t n = request[1];
		// debug msg
		log(LOG_INFO,"merge: Received merge token after waiting.");
		// . call the callback
		// . returns false and sets g_errno on error, true otherwise
		// . let manager know we received the token
		// . m_callback should call releaseToken() when done with it
		if ( ! callCallback ( n ) ) 
			g_udpServer.sendErrorReply ( slot , EBADREQUEST );
		else
			g_udpServer.sendReply_ass  ( NULL, 0, NULL, 0, slot );
		return;
	}
	// this is asking for the token, from the client to the manager
	if ( requestSize == 11 && *request == REQUEST_GETTOKEN ) {
		char *p          = request + 1;
		int32_t  hostId     = *(int32_t *)p ; p += 4;
		int32_t  timestamp  = *(int32_t *)p ; p += 4;
		char  priority   = *p         ; p += 1;
		int32_t  clientSlot = *p         ; p += 1;
		// see if a repeat request
		for ( int32_t i = 0 ; i < m_topUsedServer ; i++ ) {
			ServerWait *s = &m_serverWaits[i];
			if ( s->m_isEmpty                  ) continue;
			if ( s->m_hostId     != hostId     ) continue;
			if ( s->m_clientSlot != clientSlot ) continue;
			// it might be a priority update
			s->m_priority = priority;
			// we might have just sent them the token, they
			// released it and called again for another slot,
			// but we clean out the serverWait when we do that
			// right? ... ? well we remove the request from the
			// server table BEFORE sending the GIVETOKEN so that
			// these repeats should not happen legitimately
			log(LOG_LOGIC,"merge: msg35: Got repeat request "
			    "for token.");
			char *p = s->m_buf;
			*p = -1;
			g_udpServer.sendReply_ass  ( p , 1, NULL , 0 , slot );
			return;
		}
		// add him to our queue
		int32_t w = addServerWait ( hostId , priority , clientSlot ,
					 timestamp );
		// return error if failed
		if ( w < 0 ) {
			g_udpServer.sendErrorReply ( slot , EBUFTOOSMALL );
			return;
		}
		// otherwise, see if we can give him the token right now
		ServerWait *s = &m_serverWaits[w];
		p = s->m_buf;
		// if nobody has token now, give it to our client now
		if ( m_serverTokeni == -1 ) {
			*p = clientSlot;
			m_serverTokeni = w;
			// always exit discrepancy mode on token re-assignment
			m_discrepancyHid = -1;
		}
		// otherwise, he has to wait for it to be released from another
		else 
			*p = -1;
		g_udpServer.sendReply_ass  ( p , 1, NULL , 0 , slot );
		return;
	}
	// this is asking to release token, from the client to the manager
	if ( requestSize == 1 && *request == REQUEST_RELEASETOKEN ) {
		// TODO: mdw: ensure we think releaser has the token?
		int32_t w = m_serverTokeni;
		// ensure someone is actually holding the token
		if ( w < 0 ) {
			log(LOG_LOGIC,"merge: msg35: Host released token "
			    "to us, but he did not hold it anyway.");
			g_udpServer.sendReply_ass  ( NULL, 0, NULL, 0, slot );
			return;
		}
		// free the token
		m_serverTokeni = -1;
		// always exit discrepancy mode on token re-assignment
		m_discrepancyHid = -1;
		// empty the slot
		removeServerWait ( w );
		// send acknowledgement, empty reply
		g_udpServer.sendReply_ass  ( NULL, 0 , NULL , 0 , slot );
		// give the token to the next in line, if any
		giveToken();
		return;
	}
	// this is asking us to sync with the client, from client to manager
	if ( requestSize >= 1 && *request == REQUEST_SYNC ) {
		// get hostid of the requesting client
		int32_t hid = *(int32_t *)(&request[1]);
		// mark the sync as received for this hostId so that once
		// we get syncs from all hostids in our group we can give
		// the token to one
		if ( ! m_allReceived ) {
			int32_t numHosts;
			//Host **h = g_hostdb.getTokenGroup (g_hostdb.m_hostId,
			//				    &numHosts ) ;
			Host **h = NULL;
			//Host **h = getTokenGroup(&numHosts);
			if ( numHosts > 16 ) {
				log(LOG_LOGIC,
				    "merge: msg35: too many hosts in group.");
				char *xx = NULL; *xx = 0;
				numHosts = 16;
			}
			int32_t count = 0;
			for ( int32_t i = 0 ; i < numHosts ; i++ ) {
				if ( h[i]->m_hostId == hid ) m_flags[i] = true;
				if ( m_flags[i] ) count++;
			}
			if ( count == numHosts ) m_allReceived = true;
		}
		// clear out all his token requests from our table
		for ( int32_t i = 0 ; i <= m_topUsedServer ; i++ )
			if ( m_serverWaits[i].m_hostId == hid )
				removeServerWait ( i );
		// . client's version of who has the token relative to his tble
		// . is -1 if nobody in his table has it
		int32_t clientTokeni = *(int32_t *)(&request[5]);
		// does this guy think he has the token?
		int32_t newi = -1;
		// add the server waits back in for this hostid
		char *p = &request[9];
		char *pend = request + requestSize;
		while ( p + 6 <= pend ) {
			int32_t timestamp  = *(int32_t *)p ; p += 4;
			char priority   = *p         ; p += 1;
			char clientSlot = *p         ; p += 1;
			if ( clientSlot < 0 ) {
				log(LOG_LOGIC,"merge: msg35: bad clientSlot.");
				continue;
			}
			int32_t a = addServerWait ( hid, priority, clientSlot ,
						 timestamp );
			// if this request is reported by client to have the
			// token now, then remember its slot # in OUR table,
			// slot # "newi"
			if ( a >= 0 && clientSlot == clientTokeni )
				newi = a;
		}
		// sanity check
		if ( p != pend )
			log(LOG_LOGIC,"merge: msg35: p != pend, bad engineer."
			    "diff = %"INT32".",
			    (int32_t)(pend - p));
			
		// . what HOSTID do we think has the token? 
		// . set tokenHid to -1 if we don't think anybody has it
		int32_t tokenHid = -1;
		if ( m_serverTokeni >= 0 ) 
			tokenHid = m_serverWaits[m_serverTokeni].m_hostId;
		// . if we do not think client has the token but he says he
		//   does then believe him, maybe we just came online
		// . if this info is not accurate it will be corrected in
		//   call to sync()
		if  ( tokenHid != hid && newi >= 0 ) {
			log("merge: HostId #%"INT32" claims he "
			    "has the merge token. Giving it to him.",hid);
			m_serverTokeni = newi;
			// always exit discrepancy mode on token re-assignment
			m_discrepancyHid = -1;
		}
		// if we think he's got the token and he does too, we might
		// have assigned it to a different bucket when re-adding the
		// requests using addServerWait() above
		else if ( tokenHid == hid && newi >= 0 ) {
			m_serverTokeni = newi;
			// always exit discrepancy mode on token re-assignment
			m_discrepancyHid = -1;
		}
		// if we think he has the token but he says he does not
		// then it may be because we just sent it to him, so wait 
		// until HIS next sync before actually updating m_serverTokeni
		else if ( tokenHid == hid && newi == -1 ) {
			// if we are already in discrepancy for someone else...
			if ( m_discrepancyHid >= 0 && 
			     m_discrepancyHid != hid ) {
				log(LOG_INFO,
				    "merge: Host #%"INT32" says he "
				    "does not have the merge token, "
				    "but already in "
				    "discrepancy mode for host #%"INT32". "
				    "Reassigning.", hid, m_discrepancyHid);
				// we need to re-assign to prevent token lockup
				m_discrepancyHid = hid;
			}
			// if we aren't already in discrepancy mode... enter it
			else if ( m_discrepancyHid != hid ) {
				log(LOG_INFO,"merge: Entering "
				    "discrepancy mode for host #%"INT32"",hid);
				// this is >= 0 when in discrepancy mode
				m_discrepancyHid = hid;
			}
			// . if we were already in discrepancy mode for him
			//   and this is his follow up sync, AND he STILL 
			//   claims not to have the token, then believe him 
			//   this time
			// . it may be the case that he did have it, but 
			//   released it right after and we ran into this
			//   situation again... but if that was the case then
			//   our m_serverTokeni would have been changed
			//   and anytime that happens we exit discrepancy mode
			else {
				log(LOG_INFO,
				    "merge: Leaving discrepancy mode. "
				    "Merge token unassigned.");
				// leave the mode
				m_discrepancyHid = -1;
				// unassign the token
				m_serverTokeni = -1;
			}
		}

		// send acknowledgement, empty reply
		g_udpServer.sendReply_ass  ( NULL, 0 , NULL , 0 , slot );
		// call giveToken() in case we need to give it because it
		// was unassigned due to a discrepancy
		giveToken();
		return;
	}

	// bitch and return if a bad request
	log(LOG_LOGIC, "merge: Received bad merge token related request "
	    "of %"INT32" bytes.",requestSize );
	g_udpServer.sendErrorReply ( slot , EBADREQUESTSIZE );
}

void Msg35::removeServerWait ( int32_t i ) {
	if ( i < 0 || i >= 512 ) {
		log(LOG_LOGIC,"merge: msg35: removeServerWait: i=%"INT32".",i);
		return;
	}
	ServerWait *s = &m_serverWaits [ i ];
	s->m_isEmpty = true;
	while ( m_topUsedServer>= 0 && 
		m_serverWaits[m_topUsedServer].m_isEmpty )
		m_topUsedServer--;
}

int32_t Msg35::addServerWait ( int32_t hostId , char priority , char clientSlot ,
			    int32_t timestamp ) {
	int32_t i;
	for ( i = 0 ; i < 512 ; i++ )
		if ( m_serverWaits[i].m_isEmpty ) break;
	if ( i >= 512 ) {
		log(LOG_LOGIC,"merge: msg35: addServerWait: "
		    "already have 512 waits.");
		return -1;
	}
	ServerWait *s = &m_serverWaits [ i ];
	s->m_timestamp  = timestamp;
	s->m_isEmpty    = false;
	s->m_hostId     = hostId;
	s->m_priority   = priority ;
	s->m_clientSlot = clientSlot;
	if ( i > m_topUsedServer ) m_topUsedServer = i;
	return i;
}

void Msg35::giveToken ( ) {
	// can't give it if already assign to someone
	if ( m_serverTokeni >= 0 ) return;
	// . if we were in discrepancy mode then we must exit it because
	//   the token has become unassigned; it must have been released
	// . we enter discrepenancy mode when a client claims he hasn't the
	//   token but we think he has. if he still believes that when he
	//   sends his next SYNC request 30 seconds later, we believe him
	// . BUT if in discrepancy mode, we can only end up here if the client
	//   realized he did have the token (he recvd GIVETOKEN late?)
	//   and called release token on us
	if ( m_discrepancyHid >= 0 ) {
		log(LOG_INFO,"merge: Exiting merge token discrepenacy mode.");
		m_discrepancyHid = -1;
	}
	// . don't give token until we've received one sync request from
	//   all of the hosts in our group
	// . so, if we just came up and host already had the token, we won't
	//   give it to another
	if ( ! m_allReceived ) return;
	// pick the highest priority, lowest time request to get the token
	char maxPriority = -1;
	int32_t minTime     = 0x7fffffff;
	int32_t mini        = -1;
	for ( int32_t i = 0 ; i <= m_topUsedServer ; i++ ) {
		ServerWait *s = &m_serverWaits[i];
		if ( s->m_isEmpty ) continue;
		// debug msg
		log(LOG_INFO,"merge: Queued merge token request: "
		    "slot #%"INT32" hid=%"INT32" p=%i t=%"INT32"",
		    i,s->m_hostId,s->m_priority,s->m_timestamp);
		if ( s->m_priority <  maxPriority ) continue;
		if ( s->m_priority == maxPriority &&
		     s->m_timestamp >  minTime     ) continue;
		maxPriority = s->m_priority;
		minTime     = s->m_timestamp;
		mini        = i;
	}
	// bail if nobody needs the token
	if ( mini == -1 ) return;
	// send token to the winner
	ServerWait *s = &m_serverWaits[mini];
	// make request
	char *p = s->m_buf;
	p[0] = REQUEST_GIVETOKEN;
	p[1] = s->m_clientSlot;
	// get Host from hostId
	Host *h = g_hostdb.getHost ( s->m_hostId );
	if ( ! h ) { log(LOG_LOGIC,"merge: msg35: Bad hostid."); return; }
	// assign the token
	m_serverTokeni = mini;
	// always exit discrepancy mode on token re-assignment
	m_discrepancyHid = -1;
	// . remove the request on error or no error from our server table
	// . if we could not send because of an error then we will get it back
	//   during the sync() phase
	removeServerWait ( m_serverTokeni );
	// . send the return request to the token manager
	// . the priority of this msg is low, use g_udpServer
        // . returns false and sets g_errno on error
	// . this times out after 60 seconds i suppose
        if ( g_udpServer.sendRequest ( s->m_buf   ,
				       2          , // requestLen
				       0x35       , // msgType 0x35
				       h->m_ip    , // low priority ip
				       h->m_port  , // low priority port
				       h->m_hostId,
				       NULL       , // slotPtr
				       this       , // state data
				       giveTokenReplyWrapper ) )
		return;
	// if it failed bitch and return
	log("merge: Got error sending merge token: %s.", mstrerror(g_errno));
	// unassign the token
	m_serverTokeni = -1;
}

void giveTokenReplyWrapper ( void *state , UdpSlot *slot ) {
	// don't let UdpServer free the send buffer
	slot->m_sendBufAlloc = NULL;
	// bail on success
	if ( ! g_errno ) return;
	// unassign the token
	g_msg35.m_serverTokeni = -1;
	// always exit discrepancy mode on token re-assignment
	g_msg35.m_discrepancyHid = -1;
	// if it timed out, give token to someone else
	log("merge: Had error sending merge token: %s.",mstrerror(g_errno));
	// this could also mean the host went down and came back up
	// so he is not in sync with us!! in that case he should be calling
	// sync() below to sync up with us eventually
	//g_errno = 0;
	//Msg35 *THIS = (Msg35 *)state;
	//THIS->giveToken ();
	// just wait to sync up, and we'll try calling giveToken() again
	// after that
	//return;
}

//////////////////////////////////////////////////////////////////////
/////////////////////////////   SYNC  ////////////////////////////////
//////////////////////////////////////////////////////////////////////

// . every 30 seconds or so each client sends his m_clientWaits queue to the
//   manager, just to make sure they are in sync. sometimes the manager
//   can go down and come back and never be registered as dead by the client,
//   or vice versa, so this keeps things in sync for sure.
// . each client also sends his m_clientTokeni to the manager so the manager
//   knows if the client thinks he has the token, and can update on that.
// . if the client tells the server he hasn't the token, but the server thinks
//   he does have the token, then we enter what's called "discrepancy mode"
//   and will only unassign the token if the client STILL believes he hasn't
//   the token in his second call to sync AND the we did not reassign
//   the token (m_serverTokeni) to another value

void sleepWrapper ( int fd , void *state ) { g_msg35.sync(); }

static void gotSyncReplyWrapper ( void *state , UdpSlot *slot ) ;

void Msg35::sync ( ) {
	// disable syncing for now
	return;
	// . send to the governing host, he must be up
	// . this returns NULL and sets g_errno on error
	Host *h = getTokenManager ( );

	char *p    = m_syncBuf;
	char *pend = m_syncBuf + 1 + 4 + 4 + 64*(4+1+1);
	// request type identifier
	*p++ = REQUEST_SYNC;
	// store our hostid
	*(int32_t *)p = g_hostdb.m_hostId ; p += 4;
	// then which one of our clientSlots has the token, if any
	*(int32_t *)p = m_clientTokeni  ; p += 4;
	// the sequence of ClientWaits, just the priority and slot #
	for ( int32_t i = 0 ; i <= m_topUsedClient && p + 5 < pend ; i++ ) {
		ClientWait *c = &m_clientWaits[i];
		if ( c->m_isEmpty ) continue;
		*(int32_t *)p = c->m_timestamp; p += 4;
		*p         = c->m_priority ; p += 1;
		*p         = (char)i       ; p += 1;
		// debug msg
		log(LOG_INFO,"merge: queued merge token request "
		    "#%"INT32" priority=%"INT32".", (int32_t)p[-1],(int32_t)p[-2]);
	}
	// . the priority of this msg is low, use g_udpServer
        // . returns false and sets g_errno on error
	// . we don't care about reply
	// . this times out after 60 seconds i guess
        if ( g_udpServer.sendRequest ( m_syncBuf     ,
				       p - m_syncBuf , // requestLen
				       0x35       , // msgType 0x04
				       h->m_ip    , // low priority ip
				       h->m_port  , // low priority port
				       h->m_hostId,
				       NULL       , // slotPtr
				       this       , // state data
				       gotSyncReplyWrapper ) ) return;
	log("merge: Had error sending client merge token request queue to "
	    "managing host: %s",mstrerror(g_errno));
}

void gotSyncReplyWrapper ( void *state , UdpSlot *slot ) {
	// don't let UdpServer free the send buffer
	slot->m_sendBufAlloc = NULL;
}
