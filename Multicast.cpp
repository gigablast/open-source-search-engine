#include "gb-include.h"


//		i guess both msg0 send requests failed with no route to host, 
//and they got retired... why didnt they switch to eth1????


#include "Multicast.h"
#include "Rdb.h"       // RDB_TITLEDB
#include "Msg20.h"
#include "Profiler.h"
#include "Stats.h"
#include "Process.h"

// up to 10 twins in a group
//#define MAX_HOSTS_PER_GROUP 10

// TODO: if we're ordered to close and we still are waiting on stuff
//       to send we should send as much as we can and save the remaining
//       slots to disk for sending later??

static void sleepWrapper1       ( int bogusfd , void    *state ) ;
static void sleepWrapper1b      ( int bogusfd , void    *state ) ;
static void sleepWrapper2       ( int bogusfd , void    *state ) ;
static void gotReplyWrapperM1    ( void *state , UdpSlot *slot  ) ;
static void gotReplyWrapperM2    ( void *state , UdpSlot *slot  ) ;

void Multicast::constructor ( ) {
	m_msg      = NULL;
	m_readBuf  = NULL;
	m_replyBuf = NULL;
	m_inUse    = false;
}
void Multicast::destructor  ( ) { reset(); }

Multicast::Multicast ( ) { constructor(); }
Multicast::~Multicast ( ) { reset(); }

// free the send/read (request/reply) bufs we pirated from a UdpSlot or
// got from the caller
void Multicast::reset ( ) {
	// if this is called while we are shutting down and Scraper has a 
	// MsgE out it cores
	if ( m_inUse && ! g_process.m_exiting ) {
		log("net: Resetting multicast which is in use. msgType=0x%hhx",
		    m_msgType);
		char *xx = NULL; *xx = 0;
		// destroy the outstanding slots
		destroySlotsInProgress(NULL);
		// and undo any sleepwrapper
		if ( m_registeredSleep ) {
			g_loop.unregisterSleepCallback ( this , sleepWrapper1);
			g_loop.unregisterSleepCallback ( this , sleepWrapper2);
			m_registeredSleep = false;
		}
		if ( m_registeredSleep2 ) {
			g_loop.unregisterSleepCallback ( this ,sleepWrapper1b);
			m_registeredSleep2 = false;
		}
	}
	if ( m_msg   && m_ownMsg ) 
		mfree ( m_msg   , m_msgSize   , "Multicast" );
	if ( m_readBuf && m_ownReadBuf && m_freeReadBuf ) 
		mfree ( m_readBuf , m_readBufMaxSize , "Multicast" );
	// . replyBuf can be separate from m_readBuf if g_errno gets set
	//   and sets the slot's m_readBuf to NULL, then calls closeUpShop()
	//   which sets m_readBuf to the slot's readBuf, which is now NULL!
	// . this was causing the "bad engineer" errors from Msg22 to leak mem
	if ( m_replyBuf && m_ownReadBuf && m_freeReadBuf && 
	     m_replyBuf != m_readBuf ) 
		mfree ( m_replyBuf , m_replyBufMaxSize , "Multicast" );
	m_msg      = NULL;
	m_readBuf  = NULL;
	m_replyBuf = NULL;
	m_inUse    = false;
	m_replyingHost = NULL;
}

// . an individual transaction's udpSlot is not be removed because we might 
//   get it a reply from it later after it's timeout
// . returns false and sets g_errno on error
// . caller can now pass in his own reply buffer
// . if "freeReplyBuf" is true that means it needs to be freed at some point
//   otherwise, it's probably on the stack or part of a larger allocate class.
bool Multicast::send ( char         *msg              ,
		       int32_t          msgSize          ,
		       uint8_t       msgType          ,
		       bool          ownMsg           ,
		       //uint32_t groupId          ,
		       uint32_t shardNum,
		       bool          sendToWholeGroup ,
		       int32_t          key              ,
		       void         *state            ,
		       void         *state2           ,
		       void          (*callback) (void *state , void *state2),
		       int32_t          totalTimeout     , // in seconds
		       int32_t          niceness         ,
		       bool          realtime         ,
		       int32_t          firstHostId      ,
		       char         *replyBuf         ,
		       int32_t          replyBufMaxSize  ,
		       bool          freeReplyBuf     ,
		       bool          doDiskLoadBalancing  ,
		       int32_t          maxCacheAge      ,
		       key_t         cacheKey         ,
		       char          rdbId            ,
		       int32_t          minRecSizes      ,
		       bool          sendToSelf       ,
		       bool          retryForever     ,
		       class Hostdb *hostdb           ,
		       int32_t          redirectTimeout  ,
		       class Host   *firstHost        ) {
	// make sure not being re-used!
	if ( m_inUse ) {
		log("net: Attempt to re-use active multicast");
		char *xx = NULL; *xx = 0;
	}
	// reset to free "m_msg" in case we are being re-used (like by Msg14)
	//log(LOG_DEBUG, "Multicast: send() 0x%hhx",msgType);
	reset();
	// it is now in use
	m_inUse = true;
	// MDW: force this off for now, i'm not sure it helps and i'm tired
	//      of seeing the msg34 timed out msgs.
	// . crap, but seems like the indexdb lookups are getting biased!!
	//doDiskLoadBalancing = false;
	// set the parameters in this class
	m_msg              = msg;
	m_ownMsg           = ownMsg;
	m_ownReadBuf       = true;
	m_freeReadBuf      = freeReplyBuf;
	m_msgSize          = msgSize;
	m_msgType          = msgType;
	//m_groupId          = groupId;
	m_shardNum = shardNum;
	m_sendToWholeGroup = sendToWholeGroup;
	m_state            = state;
	m_state2           = state2;
	m_callback         = callback;
	m_totalTimeout     = totalTimeout; // in seconds
	m_niceness         = niceness;
	m_realtime         = realtime;
	// this can't be -1 i guess
	if ( totalTimeout <= 0 ) { char *xx=NULL;*xx=0; }
	// don't use this anymore!
	if ( m_realtime ) { char *xx = NULL; *xx = 0; }
	m_replyBuf         = replyBuf;
	m_replyBufMaxSize  = replyBufMaxSize;
	m_startTime        = getTime();
	m_numReplies       = 0;
	m_readBuf          = NULL;
	m_readBufSize      = 0;
	m_readBufMaxSize   = 0;
	m_registeredSleep  = false;
	m_registeredSleep2 = false;
	m_sendToSelf       = sendToSelf;
	m_retryForever     = retryForever;
	m_sentToTwin       = false;
	// turn it off until it is debugged
	m_retryForever     = false;
	m_hostdb           = hostdb;
	if ( ! m_hostdb ) m_hostdb = &g_hostdb;
	m_retryCount       = 0;
	m_key              = key;
	// reset Msg34's m_numRequests/m_numReplies since this may be
	// the second time send() was called for this particular class instance
	//m_msg34.reset();
	// keep track of how many outstanding requests to a host
	m_numLaunched      = 0;
	// variables for doing disk load balancing
	//m_doDiskLoadBalancing = doDiskLoadBalancing;
	m_maxCacheAge         = maxCacheAge;
	m_cacheKey            = cacheKey;
	m_rdbId               = rdbId;
	m_minRecSizes         = minRecSizes; // amount we try to read from disk
	m_redirectTimeout     = redirectTimeout;
	// clear m_retired, m_errnos, m_slots
	memset ( m_retired    , 0 , sizeof(char     ) * MAX_HOSTS_PER_GROUP );
	memset ( m_errnos     , 0 , sizeof(int32_t     ) * MAX_HOSTS_PER_GROUP );
	memset ( m_slots      , 0 , sizeof(UdpSlot *) * MAX_HOSTS_PER_GROUP );
	memset ( m_inProgress , 0 , sizeof(char     ) * MAX_HOSTS_PER_GROUP );
	// breathe
	QUICKPOLL(m_niceness);


	int32_t hostNumToTry = -1;

	if ( ! firstHost ) {
		// . get the list of hosts in this group
		// . returns false if blocked, true otherwise
		// . sets g_errno on error
		//Host *hostList = m_hostdb->getGroup ( groupId , &m_numHosts);
		Host *hostList = g_hostdb.getShard ( shardNum , &m_numHosts );
		if ( ! hostList ) {
			log("mcast: no group");g_errno=ENOHOSTS;return false;}
		// now copy the ptr into our array
		for ( int32_t i = 0 ; i < m_numHosts ; i++ )
			m_hostPtrs[i] = &hostList[i];
	}
	//
	// if we are sending to an scproxy then put all scproxies into the
	// list of hosts
	//
	else { // if ( firstHost && (firstHost->m_type & HT_SCPROXY) ) {
		int32_t np = 0;
		for ( int32_t i = 0 ; i < g_hostdb.m_numProxyHosts ; i++ ) {
			// int16_tcut
			Host *h = g_hostdb.getProxy(i);
			if ( ! (h->m_type & HT_SCPROXY ) ) continue;
			// stop breaching
			if ( np >= 32 ) { char *xx=NULL;*xx=0; }
			// assign this
			if ( h == firstHost ) hostNumToTry = np;
			// set our array of ptrs of valid hosts to send to
			m_hostPtrs[np++] = h;
		}
		// assign
		m_numHosts  = np;
		firstHostId = -1;
		// panic
		if ( ! np ) { char *xx=NULL;*xx=0; }
	}

	// . pick the fastest host in the group
	// . this should pick the fastest one we haven't already sent to yet
	if ( ! m_sendToWholeGroup ) {
		bool retVal = sendToHostLoop (key,hostNumToTry,firstHostId) ;
		// on error, un-use this class
		if ( ! retVal ) m_inUse = false;
		return retVal;
	}
	//if ( ! m_sendToWholeGroup ) return sendToHostLoop ( key , -1 );
	// . send to ALL hosts in this group if sendToWholeGroup is true
	// . blocks forever until sends to all hosts are successfull
	sendToGroup ( ); 
	// . sendToGroup() always blocks, but we return true if no g_errno
	// . we actually keep looping until all hosts get the msg w/o error
	return true;
}

///////////////////////////////////////////////////////
//                                                   //
//                  GROUP SEND                       //
//                                                   //
///////////////////////////////////////////////////////

// . keeps calling itself back on any error
// . resends to host/ip's that had error forever
// . callback only called when all hosts transmission are successful
// . it does not send to hosts whose m_errnos is 0
// . TODO: deal with errors from g_udpServer::sendRequest() better
// . returns false and sets g_errno on error
void Multicast::sendToGroup ( ) {
	// see if anyone gets an error
	bool hadError = false;
	// . cast the msg to ALL hosts in the m_hosts group of hosts
	for ( int32_t i = 0 ; i < m_numHosts ; i++ ) {
		// cancel any errors
		g_errno = 0;
		// get the host
		Host *h = m_hostPtrs[i];//&m_hosts[i];
		// if we got a nice reply from him skip him
		//slots[i] && m_slots[i]->doneReading() ) continue;
		if ( m_retired[i] ) continue;
		// sometimes msg1.cpp is able to add the data to the tree
		// without problems and will save us a network trans here
		if ( ! m_sendToSelf && 
		     h->m_hostId == g_hostdb.m_hostId &&
		     m_hostdb == &g_hostdb &&
		     ! g_conf.m_interfaceMachine ) {
			m_retired[i] = true;
			m_errnos [i] = 0;
			m_numReplies++;
			continue;
		}
		// . timeout is in seconds
		// . timeout is just the time remaining for the whole groupcast
		// int32_t timeout = m_startTime + m_totalTimeout - getTime();
		// . since we now must get non-error replies from ALL hosts
		//   in the group we no longer have a "totalTimeout" per se
		// reset the g_errno for host #i
		m_errnos [i] = 0;
		// if niceness is 0, use the higher priority udpServer
		UdpServer *us = &g_udpServer;
		if ( m_realtime ) us = &g_udpServer2;
		// send to the same port as us!
		int16_t destPort = h->m_port;
		//if ( m_realtime ) destPort = h->m_port2;

		// if from hosts2.conf pick the best ip!
		int32_t  bestIp  = h->m_ip;
		if ( m_hostdb == &g_hostdb2 ) 
		       bestIp = g_hostdb.getBestHosts2IP ( h );

		// retire the host to prevent resends
		m_retired [ i ] = true;
#ifdef _GLOBALSPEC_
		// debug message for global spec
		//logf(LOG_DEBUG,"net: mcast state=%08"XINT32"",(int32_t)this);
#endif
		int32_t hid = h->m_hostId;
		if ( m_hostdb != &g_hostdb ) hid = -1;
		// . send to a single host
		// . this creates a transaction control slot, "udpSlot"
		// . returns false and sets g_errno on error
		if ( us->sendRequest ( m_msg       , 
				       m_msgSize   , 
				       m_msgType   ,
				       bestIp      , // h->m_ip     , 
				       destPort    ,
				       hid ,
				       &m_slots[i] ,
				       this        , // state
				       gotReplyWrapperM2 ,
				       m_totalTimeout   ,
				       -1               , // backoff
				       -1               , // max wait in ms
				       m_replyBuf       ,
				       m_replyBufMaxSize ,
				       m_niceness )) {  // cback niceness
#ifdef _GLOBALSPEC_
			// note the slot ptr for reference
			//logf(LOG_DEBUG, "net: mcast slotPtr=%08"XINT32"",
			//     (int32_t)&m_slots[i]);
#endif
			continue;
		}
		// g_errno must have been set, remember it
		m_errnos [ i ] = g_errno;
		// we had an error
		hadError = true;
		// bring him out of retirement to try again later in time
		m_retired[i] = false;
		// log the error
		log("net: Got error sending add data request (0x%hhx) "
		    "to host #%"INT32": %s. "
		    "Sleeping one second and retrying.", 
		    m_msgType,h->m_hostId,mstrerror(g_errno) );
		// . clear it, we'll try again
		// . if we don't clear Msg1::addList(), which returns
		//   true if it did not block, false if it did, will pick up
		//   on it and wierd things might happen.
		g_errno = 0;
		// continue if we're already registered for sleep callbacks
		if ( m_registeredSleep ) continue;
		// otherwise register for sleep callback to try again
		g_loop.registerSleepCallback  (5000/*ms*/,this,sleepWrapper2,
					       m_niceness);
		m_registeredSleep = true;
	}
	// if we had an error then we'll be called again in a second
	if ( hadError ) return;
	// otherwise, unregister sleep callback if we had no error
	if ( m_registeredSleep ) {
		g_loop.unregisterSleepCallback ( this , sleepWrapper2 );
		m_registeredSleep = false;
	}
}

void sleepWrapper2 ( int bogusfd , void *state ) {
	Multicast *THIS = (Multicast *)state;
	// try another round of sending to see if hosts had errors or not
	THIS->sendToGroup ( );
}

// C wrapper for the C++ callback
void gotReplyWrapperM2 ( void *state , UdpSlot *slot ) {
	Multicast *THIS = (Multicast *)state;
        THIS->gotReply2 ( slot );
}

// . otherwise, we were sending to a whole group so ALL HOSTS must produce a 
//   successful reply
// . we keep re-trying forever until they do
void Multicast::gotReply2 ( UdpSlot *slot ) {
	// don't ever let UdpServer free this send buf (it is m_msg)
	slot->m_sendBufAlloc = NULL;
	// save this for msg4 logic that calls injection callback
	m_slot = slot;
	// . log the error
	// . ETRYAGAIN often happens when we are falling too far behind in
	//   our merging (see Rdb.cpp) and we enter urgent merge mode
	// . it may also happen if tree is too full and is being dumped to disk
	//if ( g_errno && g_errno != ETRYAGAIN ) 
	//	log("net: Got error reply sending to a host during a "
	//	    "group send: %s.", mstrerror(g_errno) );
	// set m_errnos for this slot
	int32_t i;
	for ( i = 0 ; i < m_numHosts ; i++ ) if ( m_slots[i] == slot ) break;
	// if it matched no slot that's wierd
	if ( i == m_numHosts ) {
		//log("not our slot: mcast=%"UINT32"",(int32_t)this);
		log(LOG_LOGIC,"net: multicast: Not our slot."); return; }
	// clear a timeout error on dead hosts
	if ( g_conf.m_giveupOnDeadHosts &&
	     g_hostdb.isDead ( m_hostPtrs[i]->m_hostId ) ) {
		log ( "net: GIVING UP ON DEAD HOST! This will not "
		      "return an error." );
		g_errno = 0;
	}
	// set m_errnos to g_errno, if any
	m_errnos[i] = g_errno;
	// if g_errno was not set we have a legit reply
	if ( ! g_errno ) m_numReplies++;
	// reset g_errno in case we do more sending
	g_errno = 0;
	// . if we got all the legit replies we're done, call the callback
	// . all slots should be destroyed by UdpServer in this case
	if ( m_numReplies >= m_numHosts ) {
		// allow us to be re-used now, callback might relaunch
		m_inUse = false;
		if ( m_callback ) {
// 			uint64_t profilerStart,profilerEnd;
// 			uint64_t statStart,statEnd;

			//if(g_conf.m_profilingEnabled){
			//	address=(int32_t)m_callback;
			//	g_profiler.startTimer(address, 
			//			      __PRETTY_FUNCTION__);
			//}
			//g_loop.startBlockedCpuTimer();
			m_callback ( m_state , m_state2 );
			//if(g_conf.m_profilingEnabled){
			//	if(!g_profiler.endTimer(address, 
			//				__PRETTY_FUNCTION__))
			//		log(LOG_WARN,"admin: Couldn't add the"
			//		    "fn %"INT32"",  (int32_t)address);
			//}
		}
		return;
	}
	// if this guy had no error then wait for more callbacks
	if ( ! m_errnos[i] ) return;
	// bring this slot out of retirement so we can send to him again
	m_retired[i] = false;
	// do indeed log the try again things, cuz we have gotten into a 
	// nasty loop with them that took me a while to track down
	bool logIt = false;
	static int32_t s_elastTime = 0;
	if      ( m_errnos[i] != ETRYAGAIN ) logIt = true;
	// log it every 10 seconds even if it was a try again
	else {
		int32_t now = getTime();
		if (now - s_elastTime > 10) {s_elastTime = now; logIt=true;}
	}
	// don't log ETRYAGAIN, may come across as bad when it is normal
	if ( m_errnos[i] == ETRYAGAIN ) logIt = false;
	//logIt = true;
	// log a failure msg
	if ( logIt ) { // m_errnos[i] != ETRYAGAIN ) {
		Host *h = m_hostdb->getHost ( slot->m_ip ,slot->m_port );
		if ( h ) 
			log("net: Got error sending request to hostId %"INT32" "
			    "(msgType=0x%hhx transId=%"INT32" net=%s): "
			    "%s. Retrying.",
			    h->m_hostId, slot->m_msgType, slot->m_transId, 
			    m_hostdb->getNetName(),mstrerror(m_errnos[i]) );
		else
			log("net: Got error sending request to %s:%"INT32" "
			    "(msgType=0x%hhx transId=%"INT32" net=%s): "
			    "%s. Retrying.",
			    iptoa(slot->m_ip), (int32_t)slot->m_port, 
			    slot->m_msgType, slot->m_transId, 
			    m_hostdb->getNetName(),mstrerror(m_errnos[i]) );
	}
	// . let's sleep for a second before retrying the send
	// . the g_errno could be ETRYAGAIN which happens if we're trying to 
	//   add data but the other host is temporarily full
	// . continue if we're already registered for sleep callbacks
	if ( m_registeredSleep ) return ;
	// . otherwise register for sleep callback to try again
	// . sleepWrapper2() will call sendToGroup() for us
	g_loop.registerSleepCallback  (5000/*ms*/,this,sleepWrapper2,
				       m_niceness);
	m_registeredSleep = true;
	// . this was bad cause it looped incessantly quickly!
	// . when we finally return, udpServer destroy this slot
	// . try to re-send this guy again on error
	// . this should always block
	// sendToGroup ();
}

///////////////////////////////////////////////////////
//                                                   //
//                  PICK & SEND                      //
//                                                   //
///////////////////////////////////////////////////////

//static void gotBestHostWrapper ( void *state ) ;

// . returns false and sets g_errno on error
// . returns true if managed to send to one host ok (initially at least)
// . uses key to pick the first host to send to (for consistency)
// . after we pick a host and launch the request to him the sleepWrapper1
//   will call this at regular intervals, so be careful, if msg34 is in
//   progress, then just skip it and use pickBestHost!
bool Multicast::sendToHostLoop ( int32_t key , int32_t hostNumToTry ,
				 int32_t firstHostId ) {
	// erase any errors we may have got
	g_errno = 0 ;
	// ALL multicast classes share "s_lastPing"
	//static time_t s_lastPing = 0;
	// . every 10 seconds we send to a random host in this group in 
	//   addition to the best host to act like a ping
	// . this keeps the ping times of all the hosts fresh
	// . but we should only use msgs NOT of type 0 so we don't send over
	//   huge indexLists!
	/*
	if ( m_msgType == 0x36 && getTime() - s_lastPing > 10 ) {
		// . pick a host other than the best at random
		// . if there's multiple dead hosts we should hit a random one
		int32_t i = pickRandomHost();
		// if we got one, try sending to it
		if ( i >= 0 ) sendToHost(i);
		// erase any errors we may have got
		g_errno = 0 ;
		// note time of our last ping for ALL multicast classes
		s_lastPing = getTime();
		// the best host
		int32_t bh = pickBestHost ( key );
		// if we sent to the one we were going to already, return now
		if ( i >= 0 && i == bh ) return true;
		// if we sent to the one and only host, bail
		if ( i >= 0 && bh < 0  ) return true;
	}
	*/
loop:

	// if it is already in progress... wait for it to get back
	//if ( m_msg34.isInProgress() ) return true ;

	int32_t i;

	// what if this host is dead?!?!?
	if ( hostNumToTry >= 0 ) // && ! g_hostdb.isDead(hostNumToTry) ) 
		i = hostNumToTry;
	// try to stay on the same switch if the twin groups are on 
	// different switches
	//else if ( g_conf.m_splitTwins && m_msgType == 0x00 )
	//	i = pickBestHost2 (key,-1,true);
	// . gigablast will often fetch data across the network even if it
	//   is available locally because it thinks it will be faster than
	//   hitting the local disk too much. This is often bad, because local
	//   disk can be a fast scsi and network can be slow. so override
	//   gigablast here
	// . only use this for msg0s, like for reading termlists, stuff like
	//   msg39 should still be routed without any problems
	// . this is messing up the biasing logic in Msg51 which calls Msg0
	//   to get a cluster rec and want to bias the page cache to save mem
	// . also, Msg0 should handle preferLocalReads itself. it has logic
	//   in there just for that
	//else if ( g_conf.m_preferLocalReads && 
	//	  ( m_msgType == 0x00 ) ) { // || m_msgType == 0x22 ) ) {
	//	i = pickBestHost (key,-1/*firstHostId*/,true/*preferLocal?*/);
	//}
	// . only requests that typically read from disk should set
	//   m_doDiskLoadBalancing to true
	// . Msg39 should not do it otherwise host#16 ends up being starved 
	//   since host0 is the gateway
	// . this is called at regular intervals by sleepWrapper1 with 
	//   hostNumToTry set to -1 so don't call Msg34 if its already going
	// . NOTE: this is essentially an advanced replacement for 
	//   pickBestHost() so it should return essentially the same values
	// . pickBestHost() doesn't have a problem returning retired host #s
	//else if ( m_doDiskLoadBalancing ) { // && hostNumToTry == -1 ) {
	/*
	else if ( m_doDiskLoadBalancing && firstHostId < 0 ) {
		// debug msg
		//log("getting best host (this=%"INT32")",(int32_t)&m_msg34);
		// . if multiple hosts have it in memory,prefers the local host
		// . return true if this blocks
		if ( ! m_msg34.getBestHost ( m_hosts    ,
					     m_retired  ,
					     m_numHosts ,
					     m_niceness ,
					     m_maxCacheAge ,
					     m_cacheKey    , 
					     m_rdbId       ,
					     this          ,
					     gotBestHostWrapper ) )
			return true;
		// . if we did not block then get the winning host
		// . best hostNum is -1 if none untried or all had errors
		// . this can return a retired host number if all its twins
		//   and itself are dead
		i = m_msg34.getBestHostNum ();
		// if no candidate, try pickBestHost
		if ( i < 0 ) i = pickBestHost ( key , -1 , false );
	}
	*/
	// . otherwise we had an error on this host
	// . pick the next best host we haven't sent to yet
	// . returns -1 if we've sent to all of them in our group, m_hosts
	//else i = pickBestHost ( (uint32_t)key , firstHostId );
	//else i = pickBestHost ( key , -1 , false ); // firstHostId
	else i = pickBestHost ( key , firstHostId , false ); // firstHostId
	
	// do not resend to retired hosts
	if ( m_retired[i] ) i = -1;
	// debug msg
	//if ( m_msgType == 0x39 || m_msgType == 0x00 )
	//	log("Multicast:: routing msgType=0x%hhx to hostId %"INT32"",
	//	     m_msgType,m_hosts[i].m_hostId);
	// . if no more hosts return FALSE
	// . we need to return false to the caller of us below
	if ( i < 0 ) { 
		// debug msg
		//log("Multicast:: no hosts left to send to");
		g_errno = ENOHOSTS; return false; }

	// log("build: msg %x sent to host %"INT32 " first hostId is %"INT32 
	// 	" oustanding msgs %"INT32, 
	// 	m_msgType, i, firstHostId, m_hostPtrs[i]->m_numOutstandingRequests);

	// . send to this guy, if we haven't yet
	// . returns false and sets g_errno on error
	// . if it returns true, we sent ok, so we should return true
	// . will return false if the whole thing is timed out and g_errno
	//   will be set to ETIMEDOUT
	// . i guess ENOSLOTS means the udp server has no slots available
	//   for sending, so its pointless to try to send to another host
	if ( sendToHost ( i ) ) return true;
	// if no more slots, we're done, don't loop!
	if ( g_errno == ENOSLOTS ) return false;
	// pointless as well if no time left in the multicast
	if ( g_errno == EUDPTIMEDOUT ) return false;
	// or if shutting down the server! otherwise it loops forever and
	// won't exit when sending a msg20 request. i've seen this...
	if ( g_errno == ESHUTTINGDOWN ) return false;
	// otherwise try another host and hope for the best
	g_errno = 0;
	key = 0 ; 
	// what kind of error leads us here? EBUFTOOSMALL or EBADENGINEER...
	hostNumToTry = -1;
	goto loop;
}

/*
void gotBestHostWrapper ( void *state ) {
	Multicast *THIS = (Multicast *)state;
	//int32_t i = THIS->m_msg34.getBestHostNum ( );
	// . if we could select none, go with non-intelligent load balancing
	// . this should still return -1 if all hosts retired though
	//if ( i < 0 ) i = THIS->pickBestHost ( 0 , -1 , false );
	int32_t i = THIS->pickBestHost ( 0 , -1 , false );
	// . if we got a candidate to try to send to him
	// . i is -1 if we could get none
	// . this also returns false on ENOSLOTS, if no slots available
	//   for sending on.
	if ( i >= 0 && THIS->sendToHostLoop ( 0 , i , -1 ) ) return;
	// if i was -1 or sendToHostLoop failed return now if we are still
	// awaiting a reply... gotReplyWrapperM1() will be called when that
	// reply comes back and that will call closeUpShop().
	if ( THIS->m_numLaunched > 0 ) return;
	// EUREKA! if the Msg34 replies timeout, that 
	// sets Msg34's LoadPoints m_errno var, and we end up with
	// no host to try AFTER blocking, which means we're responsible
	// for closing up shop and calling the callback.
	// just call the closeUpShop() routine.
	THIS->closeUpShop ( NULL );
}
*/

/*
int32_t Multicast::pickBestHost2 ( uint32_t key , int32_t firstHostId ,
				bool preferLocal ) {
	// now select the host on our same network switch
	int32_t hpg     = m_hostdb->m_numHostsPerShard;
	// . get the hostid range on our switch
	// . a segment is all the hosts on the same switch
	int32_t segmentSize = m_hostdb->m_numHosts / hpg;
	// get our segment
	int32_t segment = m_hostdb->m_hostId / segmentSize;
	int32_t i;
	for ( i = 0 ; i < m_numHosts ; i++ ) {
		// skip if he's dead
		if ( m_hostdb->isDead ( &m_hosts[i] )             ) continue;
		// skip if he's reporting system errors
		if ( m_hostdb->kernelErrors(&m_hosts[i])       ) continue;
		// skip if he's not on our segment
		if ( m_hosts[i].m_hostId / segmentSize != segment ) continue;
		break;
	}
	// return if we got someone in our group
	if ( i < m_numHosts ) {
		if ( g_conf.m_logDebugNet )
			log(LOG_DEBUG,"net: Splitting request to hostid %"INT32"",
			    m_hosts[i].m_hostId);
		return i;
	}
	// if we got nothing, default to this one
	return pickBestHost ( key , firstHostId , preferLocal );
}
*/

// . pick the fastest host from m_hosts based on avg roundtrip time for ACKs
// . skip hosts in our m_retired[] list of hostIds
// . returns -1 if none left to pick
int32_t Multicast::pickBestHost ( uint32_t key , int32_t firstHostId ,
			       bool preferLocal ) {
	// debug msg
	//log("pickBestHost manually");
	// bail if no hosts
	if ( m_numHosts == 0 ) return -1;
	// . should we always pick host on same machine first?
	// . we now only run one instance of gb per physical server, not like
	//   the old days... so this is somewhat obsolete... MDW
	/*
	if ( preferLocal && !g_conf.m_interfaceMachine ) {
		for ( int32_t i = 0 ; i < m_numHosts ; i++ )
			if ( m_hosts[i].m_machineNum == 
			     m_hostdb->getMyMachineNum()        &&
			     ! m_hostdb->isDead ( &m_hosts[i] ) &&
			     ! m_hostdb->kernelErrors( &m_hosts[i] ) &&
			     ! m_retired[i] ) return i;
	}
	*/
	// . if firstHostId not -1, try it first
	// . Msg0 uses this only to select hosts on same machine for now
	// . Msg20 now uses this to try to make sure the lower half of docids
	//   go to one twin and the upper half to the other. this makes the
	//   tfndb page cache twice as effective when getting summaries.
	if ( firstHostId >= 0 ) {
		//log("got first hostId!!!!");
		// find it in group
		int32_t i;
		for ( i = 0 ; i < m_numHosts ; i++ )
			if ( m_hostPtrs[i]->m_hostId == firstHostId ) break;
		// if not found bitch
		if ( i >= m_numHosts ) {
			log(LOG_LOGIC,"net: multicast: HostId %"INT32" not "
			    "in group.", firstHostId );
			char *xx = NULL; *xx = 0;
		}
		// if we got a match and it's not dead, and not reporting
		// system errors, return it
		if ( i < m_numHosts && ! m_hostdb->isDead ( m_hostPtrs[i] ) &&
		     ! m_hostdb->kernelErrors ( m_hostPtrs[i] ) ) 
			return i;
	}

	// round robin selection
	//static int32_t s_lastGroup = 0;
	//int32_t        count       = 0;
	//int32_t        i ;
	//int32_t        slow = -1;
	int32_t   numDead   =  0;
	int32_t   dead      = -1;
	int32_t   n         = 0;
	//int32_t   count     = 0;
	bool   balance   = g_conf.m_doStripeBalancing;
	// always turn off stripe balancing for all but these 3 msgTypes
	if ( m_msgType != 0x39 &&
	     m_msgType != 0x37 &&
	     m_msgType != 0x36  ) 
		balance = false;
	// . pick the guy in our "stripe" first if we are doing these msgs
	// . this will prevent a ton of msg39s from hitting one host and
	//   "spiking" it.
	if ( balance ) n = g_hostdb.m_myHost->m_stripe;
	// . if key is not zero, use it to select a host in this group
	// . if the host we want is dead then do it the old way
	// . ignore the key if balance is true though! MDW
	if ( key != 0 && ! balance ) {
		// often the groupId was selected based on the key, so lets
		// randomize everything up a bit
		uint32_t i = hashLong ( key ) % m_numHosts;
		// if he's not dead or retired use him right away
		if ( ! m_retired[i] &&
		     ! m_hostdb->isDead ( m_hostPtrs[i] ) &&
		     ! m_hostdb->kernelErrors( m_hostPtrs[i] ) ) return i;
	}

	// no no no we need to randomize the order that we try them
	Host *fh = m_hostPtrs[n];
	// if this host is not dead,  and not reporting system errors, use him
	if ( ! m_retired[n] &&
	     ! m_hostdb->isDead(fh) && 
	     ! m_hostdb->kernelErrors(fh) )
		return n;

	// . ok now select the kth available host
	// . make a list of the candidates
	int32_t cand[32];
	int32_t nc = 0;
	for ( int32_t i = 0 ; i < m_numHosts ; i++ ) {
		// get the host
		Host *h = m_hostPtrs[i];
		// count those that are dead or are reporting system errors
		if ( m_hostdb->isDead ( h ) || m_hostdb->kernelErrors(h) )
			numDead++;
		// skip host if we've retired it
		if ( m_retired[i] ) continue;
		// if this host is not dead,  and not reporting system errors,
		// use him
		if ( !m_hostdb->isDead(h) && !m_hostdb->kernelErrors(h) )
			cand[nc++] = i;
		// pick a dead that isn't retired
		dead = i;
	}
	// if a host was alive and untried, use him next
	if ( nc > 0 ) {
		int32_t k = ((uint32_t)m_key) % nc;
		return cand[k];
	}
	// . come here if all hosts were DEAD
	// . try sending to a host that is dead, but not retired now
	// . if all deadies are retired this will return -1
	// . sometimes a host can appear to be dead even though it was
	//   just under severe load
	if ( numDead == m_numHosts ) return dead;
	// otherwise, they weren't all dead so don't send to a deadie
	return -1;
	// . if no host we sent to had an error then we should send to deadies
	// . TODO: we should only send to a deadie if we haven't got back a
	//   reply from any live hosts!!
	//if ( numErrors == 0 ) return dead;
	// . now alive host was found that we haven't tried, so return -1
	// . before we were returning hosts that were marked as dead!! This
	//   caused problems when the only alive host returned an error code
	//   because it would take forever for the dead host to timeout...
	//return -1;
	// update lastGroup
	//if ( ++s_lastGroup >= m_numHosts ) s_lastGroup = 0;
	// return i if we got it
	//if ( count >= m_numHosts ) return slow;
	// otherwise return i
	//return i;
}

// . pick the fastest host from m_hosts based on avg roundtrip time for ACKs
// . skip hosts in our m_retired[] list of hostIds
// . returns -1 if none left to pick
/*
int32_t Multicast::pickBestHost ( ) {
	int32_t mini    = -1;
	int32_t minPing = 0x7fffffff;
	// TODO: reset the sublist ptr????
	// cast the msg to "hostsPerGroup" hosts in group "groupId"
	for ( int32_t i = 0 ; i < m_numHosts ; i++ ) {
		// skip host if we've retired it
		if ( m_retired[i] ) continue;
		// get the host
		Host *h = &m_hosts[i];
		// see if we got a new fastest host, continue if not
		if ( h->m_pingAvg > minPing ) continue;
		minPing = h->m_pingAvg;
		mini    = i;
	}
	// return our candidate, may be -1 if all were picked before
	return mini;
}
*/

// . pick a random host
// . returns -1 if we already sent to that host (he's retired)
int32_t Multicast::pickRandomHost ( ) {
	// select one of the dead hosts at random
	int32_t i = rand() % m_numHosts ;
	// if he's retired return -1
	if ( m_retired[i] ) return -1;
	// otherwise, he's a valid candidate
	return i;
}

// . returns false and sets error on g_errno
// . returns true if kicked of the request (m_msg)
// . sends m_msg to host "h"
bool Multicast::sendToHost ( int32_t i ) {
	// sanity check
	if ( i >= m_numHosts ) { char *xx=NULL;*xx=0; }
	// sanity check , bitch if retired
	if ( m_retired [ i ] ) {
		log(LOG_LOGIC,"net: multicast: Host #%"INT32" is retired. "
		    "Bad engineer.",i);
		//char *xx = NULL; *xx = 0;
		return true;
	}
	// debug msg
	//log("sending to host #%"INT32" (this=%"INT32")",i,(int32_t)&m_msg34);
	// . add this host to our retired list so we don't try again
	// . only used by pickBestHost() and sendToHost()
	m_retired [ i ] = true;
	// what time is it now?
	int64_t nowms = gettimeofdayInMilliseconds();
	time_t    now   = nowms / 1000;
	// save the time
	m_launchTime [ i ] = nowms;
	// sometimes clock is updated on us
	if ( m_startTime > now ) m_startTime = now ;
	// . timeout is in seconds
	// . timeout is just the time remaining for the whole groupcast
	int32_t timeRemaining = m_startTime + m_totalTimeout - now;
	// . if timeout is negative then reset start time so we try forever
	// . no, this could be called by a re-route in sleepWrapper1 in which
	//   case we really should timeout.
	// . this can happen if sleepWrapper found a timeout before UdpServer
	//   got its timeout.
	if ( timeRemaining <= 0 ) {
		//m_startTime = getTime();; timeout = m_totalTimeout;}
		//g_errno = ETIMEDOUT; 
		// this can happen if the udp reply timed out!!! like if a
		// host is under severe load... with Msg23::getLinkText()
		// or Msg22::getTitleRec() timing out on us. basically, our
		// msg23 request tried to send a msg22 request which timed out
		// on it so it sent us back this error.
		if ( g_errno != EUDPTIMEDOUT ) 
		log(LOG_INFO,"net: multicast: had negative timeout, %"INT32". "
		    "startTime=%"INT32" totalTimeout=%"INT32" "
		    "now=%"INT32". msgType=0x%hhx "
		    "niceness=%"INT32" clock updated?",
		    timeRemaining,m_startTime,m_totalTimeout,
		    (int32_t)now,m_msgType,
		    (int32_t)m_niceness);
		// we are timed out so do not bother re-routing
		//g_errno = ETIMEDOUT; 		
		//return false;
		// give it a fighting chance of 2 seconds then
		//timeout = 2;
		timeRemaining = m_totalTimeout;
	}
	// get the host
	Host *h = m_hostPtrs[i];
	// if niceness is 0, use the higher priority udpServer
	UdpServer *us = &g_udpServer;
	if ( m_realtime ) us = &g_udpServer2;
	// send to the same port as us!
	int16_t destPort = h->m_port;
	//if ( m_realtime ) destPort = h->m_port2;

	// if from hosts2.conf pick the best ip!
	int32_t  bestIp   = h->m_ip;
	if ( m_hostdb == &g_hostdb2 ) 
		bestIp = g_hostdb.getBestHosts2IP ( h );

#ifdef _GLOBALSPEC_
	// debug message for global spec
	//logf(LOG_DEBUG,"net: mcast2 state=%08"XINT32"",(int32_t)this);
#endif
	// sanity check
	//if ( g_hostdb.isDead(h) ) {
	//	log("net: trying to send to dead host.");
	//	char *xx = NULL; 
	//	*xx = 0; 
	//}
	// don't set hostid if we're sending to a remote cluster
	int32_t hid = h->m_hostId;
	if ( m_hostdb != &g_hostdb ) hid = -1;
	// if sending to a proxy keep this set to -1
	if ( h->m_type != HT_GRUNT ) hid = -1;
	// max resends. if we resend a request dgram this many times and
	// got no ack, bail out with g_errno set to ENOACK. this is better
	// than the timeout because it takes like 20 seconds to mark a 
	// host as dead and takes "timeRemaining" seconds to timeout the
	// request
	int32_t maxResends = -1;
	// . only use for nicness 0
	// . it uses a backoff scheme, increments delay for first few resends:
	// . it starts of at 33ms, then 66, then 132, then 200 from there out
	if ( m_niceness == 0 ) maxResends = 4;
	// . send to a single host
	// . this creates a transaction control slot, "udpSlot"
	// . return false and sets g_errno on error
	// . returns true on successful launch and calls callback on completion
	if ( !  us->sendRequest ( m_msg       , 
				  m_msgSize   , 
				  m_msgType   ,
				  bestIp      , // h->m_ip     , 
				  destPort    ,
				  hid ,
				  &m_slots[i] ,
				  this        , // state
				  gotReplyWrapperM1 ,
				  timeRemaining    , // timeout
				  -1               , // backoff
				  -1               , // max wait in ms
				  m_replyBuf       ,
				  m_replyBufMaxSize ,
				  m_niceness        , // cback niceness
				  maxResends        )) {
		log("net: Had error sending msgtype 0x%hhx to host "
		    "#%"INT32": %s. Not retrying.", 
		    m_msgType,h->m_hostId,mstrerror(g_errno));
		// i've seen ENOUDPSLOTS available msg here along with oom
		// condition...
		//char *xx=NULL;*xx=0; 
		return false;
	}
	// mark it as outstanding
	m_inProgress[i] = 1;
#ifdef _GLOBALSPEC_
	// note the slot ptr for reference
	//logf(LOG_DEBUG,"net: mcast2 slotPtr=%08"XINT32"",(int32_t)&m_slots[i]);
#endif
	// set our last launch date
	m_lastLaunch = nowms ; // gettimeofdayInMilliseconds();
	// save the host, too
	m_lastLaunchHost = h;
	/*
	// assume this host has more disk load now
	if ( m_doDiskLoadBalancing && ! m_msg34.isInBestHostCache() ) {
		// . add the disk load right after sending it in case we have
		//   many successive sends right after this one
		// . in the case of titledb use a low avg read size, otherwise
		//   this would be like over 1 Meg (the max titleRec size)
		int32_t avg = m_minRecSizes;
		if ( m_rdbId == RDB_TITLEDB ) avg = 32*1024; // titledb read?
		m_msg34.addLoad ( avg , h->m_hostId , nowms );
	}
	*/
	// count it as launched
	m_numLaunched++;
	// timing debug
	//log("Multicast sent to hostId %"INT32", this=%"INT32", transId=%"INT32"", 
	//    h->m_hostId, (int32_t)this , m_slots[i]->m_transId );
	// . let's sleep so we have a chance to launch to another host in
	//   the same group in case this guy takes too long
	// . don't re-register if we already did
	if ( m_registeredSleep ) return true;
	// . otherwise register for sleep callback to try again
	// . sleepWrapper1() will call sendToHostLoop() for us
	g_loop.registerSleepCallback  (50/*ms*/,this,sleepWrapper1,m_niceness);
	m_registeredSleep = true;
#ifdef _GLOBALSPEC_
	// debug msg
	//logf(LOG_DEBUG,"net: mcast registered1 this=%08"XINT32"",(int32_t)this);
#endif
	// successful launch
	return true;
}

// this is called every 50 ms so we have the chance to launch our request
// to a more responsive host
void sleepWrapper1 ( int bogusfd , void    *state ) {
	Multicast *THIS = (Multicast *) state;
	// . if our last launch was less than X seconds ago, wait another tick
	// . we often send out 2+ requests and end up getting one reply before
	//   the others and that results in us getting unwanted dgrams...
	// . increasing this delay here results in fewer wasted requests but
	//   if a host goes down you don't want a user to wait too long
	// . after a host goes down it's ping takes a few secs to decrease
	// . if a host is shutdown properly it will broadcast a msg to
	//   all hosts using Hostdb::broadcast() informing them that it's 
	//   going down so they know to stop sending to it and mark him as
	//   dead

	int64_t now = gettimeofdayInMilliseconds();
	// watch out for someone advancing the system clock
	if ( THIS->m_lastLaunch > now ) THIS->m_lastLaunch = now;
	// get elapsed time since we started the send
	int32_t elapsed = now - THIS->m_lastLaunch;
	int32_t docsWanted;
	int32_t firstResultNum;
	int32_t nqterms;
	int32_t rerankRuleset;
	int32_t wait;
	char exact;
	//int32_t hid = -1;
	Host *hd;
	//log("elapsed = %"INT32" type=0x%hhx",elapsed,THIS->m_msgType);

	// . don't relaunch any niceness 1 stuff for a while
	// . it often gets suspended due to query traffic
	//if ( THIS->m_niceness > 0 && elapsed < 800000 ) return;
	if ( THIS->m_niceness > 0 ) return;

	// TODO: if the host went dead on us, re-route

	// . Msg36 is used to get the length of an IndexList (termFreq)
	//   and is very fast, all in memory, don't wait more than 50ms
	// . if we do re-route this is sucks cuz we'll get slightly different
	//   termFreqs which impact the total results count as well as summary
	//   generation since it's based on termFreq, not too mention the
	//   biggest impact being ordering of search results since the
	//   score weight is based on termFreq as well
	// . but unfortunately, this scheme doesn't increase the ping time
	//   of dead hosts that much!!
	// . NOTE: 2/26/04: i put most everything to 8000 ms since rerouting
	//   too much on an already saturated network of drives just 
	//   excacerbates the problem. this stuff was originally put here
	//   to reroute for when a host went down... let's keep it that way
	//int32_t ta , nb;
	if ( THIS->m_redirectTimeout != -1 ) {
		if ( elapsed < THIS->m_redirectTimeout ) return;
		goto redirectTimedout;
	}
	switch ( THIS->m_msgType ) {
	// term freqs are msg 0x36 and don't hit disk, so reroute all the time
	case 0x36: 
		exact = 0;
		// first byte is 1 if caller wants an *exact* termlist size
		// lookup which requires hitting disk and is *much* slower
		if ( THIS->m_msg ) exact = *(char *)(THIS->m_msg);
		// these don't take any resources... unless exact i guess,
		// so let them fly... 10ms or more to reroute
		if ( ! exact && elapsed < 10    ) return; 
		//if (   exact && elapsed < 20000 ) return;
		// never re-reoute these, they may be incrementing/decrementing
		// a count, and we only store that count on one host!!
		return;
		break;
	// msg to get a summary from a query (calls msg22)
	// buzz takes extra long! it calls Msg25 sometimes.
	// no more buzz.. put back to 8 seconds.
	// put to 5 seconds now since some hosts freezeup still it seems
	// and i haven't seen a summary generation of 5 seconds
	case 0x20: if ( elapsed <  5000 ) return; break;
	// msg 0x20 calls this to get the title rec
	case 0x22: if ( elapsed <  1000 ) return; break;
	// Msg23 niceness 0 is only for doing &rerank=X queries
	//case 0x23: if ( elapsed < 100000 ) return; break;
	// a request to get the score of a docid, can be *very* intensive
	case 0x3b: if ( elapsed < 500000 ) return; break;
	// related topics request, calls many Msg22 to get titlerecs...
	case 0x24: if ( elapsed <  2000 ) return; break;
	// . msg to get an index list over the net
	// . this limit should really be based on length of the index list
	// . this was 15 then 12 now it is 4
	case 0x00: 
		// this should just be for when a host goes down, not for
		// performance reasons, cuz we do pretty good load balancing
		// and when things get saturated, rerouting excacerbates it
		if ( elapsed <  8000 ) return; break;
		// how many bytes were requested?
		/*
		if ( THIS->m_msg ) nb=*(int32_t *)(THIS->m_msg + sizeof(key_t)*2);
		else               nb=2000000;
		// . givem 300ms + 1ms per 5000 bytes
		// . a 6M   read would be allowed 1500ms before re-routing
		// . a 1M   read would be allowed 500ms
		// . a 100k read would be allowed 320ms
		ta = 300 + nb / 5000;
		// limit it
		if ( ta < 100  ) ta = 100;
		if ( ta > 9000 ) ta = 9000; // could this hurt us?
		if ( elapsed <  ta ) return; 
		break;
		*/
	// msg to get a clusterdb rec
	case 0x38: if ( elapsed <  2000 ) return; break;
	// msg to get docIds from a query, may take a while
	case 0x39: 
		// how many docsids request? first 4 bytes of request.
		docsWanted = 10;
		firstResultNum = 0;
		nqterms        = 0;
		rerankRuleset  = -1;
		if ( THIS->m_msg ) docsWanted     = *(int32_t *)(THIS->m_msg);
		if ( THIS->m_msg ) firstResultNum = *(int32_t *)(THIS->m_msg+4);
		if ( THIS->m_msg ) nqterms        = *(int32_t *)(THIS->m_msg+8);
		// never re-route if it has a rerank, those take forever
		if ( THIS->m_msg ) rerankRuleset  = *(int32_t *)(THIS->m_msg+12);
		if ( rerankRuleset >= 0 ) return;
		// . how many milliseconds of waiting before we re-route?
		// . 100 ms per doc wanted, but if they all end up 
		//   clustering then docsWanted is no indication of the
		//   actual number of titleRecs (or title keys) read
		// . it may take a while to do dup removal on 1 million docs
		wait = 5000 + 100  * (docsWanted+firstResultNum);
		// those big UOR queries should not get re-routed all the time
		if ( nqterms > 0 ) wait += 1000 * nqterms;
		if ( wait < 8000 ) wait = 8000;
		// seems like buzz is hammering the cluster and 0x39'saretiming
		// out too much because of huge title recs taking forever with
		// Msg20
		//if ( wait < 120000 ) wait = 120000;
		if ( elapsed < wait ) return; 
		break;
	// these tagdb lookups are usually lickety split, should all be in mem
	case 0x08: if ( elapsed <    10 ) return; break;
	// this no longer exists! it uses msg0
	//case 0x8a: if ( elapsed <    200 ) return; break;
	case 0x8b: if ( elapsed <    10 ) return; break;
	// don't relaunch anything else unless over 8 secs
	default:   if ( elapsed <  8000 ) return; break;
	}

	// find out which host timedout
	//hid = -1;
	hd = NULL;
	//if ( THIS->m_retired[0] && THIS->m_hosts && THIS->m_numHosts >= 1 )
	if ( THIS->m_retired[0] && THIS->m_numHosts >= 1 )
		hd = THIS->m_hostPtrs[0];
	//if ( THIS->m_retired[1] && THIS->m_hosts && THIS->m_numHosts >= 2 )
	if ( THIS->m_retired[1] && THIS->m_numHosts >= 2 )
		hd = THIS->m_hostPtrs[1];
	// 11/21/06: now we only reroute if the host we sent to is marked as
	// dead unless it is a msg type that takes little reply generation time
	if ( hd && // hid >= 0 && 
	     //! g_hostdb.isDead(hid) && 
	     ! g_hostdb.isDead(hd) && 
	     //m_msgType != 0x36      && (see above)
	     //m_msgType != 0x17      &&
	     // hosts freezeup sometimes and we don't get a summary in time...
	     // no! we got EDISKSTUCK now and this was causing a problem
	     // dumping core in the parse cache logic
	     //THIS->m_msgType != 0x20  &&
	     THIS->m_msgType != 0x08  &&
	     //THIS->m_msgType != 0x8a  &&
	     THIS->m_msgType != 0x8b    )
	         return;

redirectTimedout:
	// cancel any outstanding transactions iff we have a m_replyBuf
	// that we must read the reply into because we cannot share!!
	if ( THIS->m_readBuf ) THIS->destroySlotsInProgress ( NULL );
	//if ( THIS->m_replyBuf ) 
	//	THIS->destroySlotsInProgress ( NULL );

	// . do a loop over all hosts in the group
	// . if a whole group of twins is down this will loop forever here
	//   every Xms, based the sleepWrapper timer for the msgType
	if ( g_conf.m_logDebugQuery ) {
	for (int32_t i = 0 ; i < THIS->m_numHosts ; i++ ) {
		if ( ! THIS->m_slots[i]         ) continue;
		// transaction is not in progress if m_errnos[i] is set
		char *ee = "";
		if ( THIS->m_errnos[i] ) ee = mstrerror(THIS->m_errnos[i]);
		log("net: Multicast::sleepWrapper1: tried host "
		    "%s:%"INT32" %s" ,iptoa(THIS->m_slots[i]->m_ip),
		    (int32_t)THIS->m_slots[i]->m_port , ee );
	}		
	}

	// log msg that we are trying to re-route
	//log("Multicast::sleepWrapper1: trying to re-route msgType=0x%hhx "
	//    "to new host",   THIS->m_msgType );	

	// if we were trying to contact a host in the secondary cluster, 
	// mark the host as dead. this is our passive monitoring system.
	if ( THIS->m_hostdb == &g_hostdb2 ) {
		log("net: Marking hostid %"INT32" in secondary cluster as dead.",
		    (int32_t)THIS->m_lastLaunchHost->m_hostId);
		THIS->m_lastLaunchHost->m_ping = g_conf.m_deadHostTimeout;
	}

	// . otherwise, launch another request if we can
	// . returns true if we successfully sent to another host
	// . returns false and sets g_errno if no hosts left or other error
	if ( THIS->sendToHostLoop(0,-1,-1) ) {
		// msgtype 0x36 is always rerouting because the timeout is so
		// low because it is an easy request to satisfy... so don't
		// flood the logs with it
		int32_t logtype = LOG_WARN;
		if ( THIS->m_msgType == 0x36 ) logtype = LOG_DEBUG;
		// same goes for msg8
		if ( THIS->m_msgType == 0x08 ) logtype = LOG_DEBUG;
		//if ( THIS->m_msgType == 0x8a ) logtype = LOG_DEBUG;
		if ( THIS->m_msgType == 0x8b ) logtype = LOG_DEBUG;
		// log msg that we were successful
		int32_t hid = -1;
		if ( hd ) hid = hd->m_hostId;
		log(logtype,
		    "net: Multicast::sleepWrapper1: rerouted msgType=0x%hhx "
		    "from host #%"INT32" "
		    "to new host after waiting %"INT32" ms", 
		    THIS->m_msgType, hid,elapsed);
		// . mark it in the stats for PageStats.cpp
		// . this is timeout based rerouting
		g_stats.m_reroutes[(int)THIS->m_msgType][THIS->m_niceness]++;
		return;
	}
	// if we registered the sleep callback we must have launched a 
	// request to a host so let gotReplyWrapperM1() deal with closeUpShop()

	// . let replyWrapper1 be called if we got one launched
	// . it should then call closeUpShop()
	//if ( THIS->m_numLaunched ) return;
	// otherwise, no outstanding requests and we failed to send to another
	// host, probably because :
	// 1. Msg34 timed out on all hosts
	// 2. there were no udp slots available (which is bad)
	//log("Multicast:: re-route failed for msgType=%hhx. abandoning.",
	//     THIS->m_msgType );
	// . the next send failed to send to a host, so close up shop
	// . this is probably because the Msg34s timed out and we could not
	//   find a next "best host" to send to because of that
	//THIS->closeUpShop ( NULL );
	// . we were not able to send to another host, maybe it was dead or
	//   there are no hosts left!
	// . i guess keep sleeping until host comes back up or transaction
	//   is cancelled
	//log("Multicast::sleepWrapper1: re-route of msgType=0x%hhx failed",
	//    THIS->m_msgType);
}

// C wrapper for the C++ callback
void gotReplyWrapperM1 ( void *state , UdpSlot *slot ) {
	Multicast *THIS = (Multicast *)state;
	// debug msg
	//log("gotReplyWrapperM1 for msg34=%"INT32"",(int32_t)(&THIS->m_msg34));
        THIS->gotReply1 ( slot );
}

// come here if we've got a reply from a host that's not part of a group send
void Multicast::gotReply1 ( UdpSlot *slot ) {		
	// debug msg
	//log("gotReply1: this=%"INT32" should exit",(int32_t)&m_msg34);
	// count it as returned
	m_numLaunched--;
	// don't ever let UdpServer free this send buf (it is m_msg)
	slot->m_sendBufAlloc = NULL;
	// remove the slot from m_slots so it doesn't get nuked in
	// gotSlot(slot) routine above
	int32_t i = 0;
	// careful! we might have recycled a slot!!! start with top and go down
	// because UdpServer might give us the same slot ptr on our 3rd try
	// that we had on our first try!
	for ( i = 0 ; i < m_numHosts ; i++ ) {
		// skip if not in progress
		if ( ! m_inProgress[i] ) continue;
		// slot must match
		if ( m_slots[i] == slot ) break;
	}
	// if it matched no slot that's wierd
	if ( i >= m_numHosts ) {
		log(LOG_LOGIC,"net: multicast: Not our slot 2."); 
		char *xx = NULL; *xx = 0;
		return; 
	}
	// set m_errnos[i], if any
	if ( g_errno ) m_errnos[i] = g_errno;

	// mark it as no longer in progress
	m_inProgress[i] = 0;

	// if he was marked as dead on the secondary cluster, mark him as up
	Host *h = m_hostPtrs[i];
	if ( m_hostdb == &g_hostdb2 && h && m_hostdb->isDead(h) ) {
		log("net: Marking hostid %"INT32" in secondary cluster "
		    "as alive.",
		    (int32_t)h->m_hostId);
		h->m_ping = 0;
	}

	// save the host we got a reply from
	m_replyingHost    = h;
	m_replyLaunchTime = m_launchTime[i];

	if ( m_sentToTwin ) 
		log("net: Twin msgType=0x%"XINT32" (this=0x%"PTRFMT") "
		    "reply: %s.",
		    (int32_t)m_msgType,(PTRTYPE)this,mstrerror(g_errno));

	// on error try sending the request to another host
	// return if we kicked another request off ok
	if ( g_errno ) {
		Host *h;
		char logIt = true;
		// do not log not found on an external network
		if ( g_errno == ENOTFOUND && m_hostdb != &g_hostdb ) goto skip;
		// log the error
		h = m_hostdb->getHost ( slot->m_ip ,slot->m_port );
		// do not log if not expected msg20
		if ( slot->m_msgType == 0x20 && g_errno == ENOTFOUND &&
		     ! ((Msg20 *)m_state)->m_expected )
			logIt = false;
		if ( slot->m_msgType == 0x20 && g_errno == EMISSINGQUERYTERMS )
			logIt = false;
		if ( h && logIt )
			log("net: Multicast got error in reply from "
			    "hostId %"INT32""
			    " (msgType=0x%hhx transId=%"INT32" "
			    "nice=%"INT32" net=%s): "
			    "%s.",
			    h->m_hostId, slot->m_msgType, slot->m_transId, 
			    m_niceness,
			    m_hostdb->getNetName(),mstrerror(g_errno ));
		else if ( logIt )
			log("net: Multicast got error in reply from %s:%"INT32" "
			    "(msgType=0x%hhx transId=%"INT32" nice =%"INT32" net=%s): "
			    "%s.",
			    iptoa(slot->m_ip), (int32_t)slot->m_port, 
			    slot->m_msgType, slot->m_transId,  m_niceness,
			    m_hostdb->getNetName(),mstrerror(g_errno) );
	skip:
		// if this slot had an error we may have to tell UdpServer
		// not to free the read buf
		if ( m_replyBuf == slot->m_readBuf ) slot->m_readBuf = NULL;
		// . try to send to another host
		// . on successful sending return, we'll be called on reply
		// . this also returns false if no new hosts left to send to
		// . only try another host if g_errno is NOT ENOTFOUND cuz
		//   we have quite a few missing clustRecs and titleRecs
		//   and doing a second lookup will decrease query response
		// . if the Msg22 lookup cannot find the titleRec for indexing
		//   purposes, it should check any twin hosts because this
		//   is very important... if this is for query time, however,
		//   then accept the ENOTFOUND without spawning another request
		// . but if the record is really not there we waste seeks!
		// . EBADENGINEER is now used by titledb's Msg22 when a docid
		//   is in tfndb but not in titledb (or id2 is invalid)
		// . it is more important that we serve the title rec than
		//   the performance gain. if you want the performance gain
		//   then you should repair your index to avoid this. therefore
		//   send to twin on ENOTFOUND
		// . often, though, we are restring to indexdb root so after
		//   doing a lot of deletes there will be a lot of not founds
		//   that are really not found (not corruption) so don't do it
		//   anymore
		// . let's go for accuracy even for queries
		// . until i fix the bug of losing titlerecs for some reason
		//   probably during merges now, we reroute on ENOTFOUND.
		bool sendToTwin = true;
		if ( g_errno == EBADENGINEER       ) sendToTwin = false;
		if ( g_errno == EMISSINGQUERYTERMS ) sendToTwin = false;
		if ( g_errno == EMSGTOOBIG         ) sendToTwin = false;
		// "Argument list too long"
		if ( g_errno == 7                  ) sendToTwin = false;
		// i guess msg50 calls msg25 with no ip sometimes?
		if ( g_errno == EURLHASNOIP        ) sendToTwin = false;
		if ( g_errno == EUNCOMPRESSERROR   ) sendToTwin = false;
		// ok, let's give up on ENOTFOUND, because the vast majority
		// of time it seems it is really not on the twin either...
		if ( g_errno == ENOTFOUND && m_msgType == 0x020 ) 
			sendToTwin = false;
		if ( g_errno == ENOTFOUND && m_msgType == 0x022 ) 
			sendToTwin = false;
		// do not worry if it was a not found msg20 for a titleRec
		// which was not expected to be there
		if ( ! logIt                       ) sendToTwin = false;
		// or a notfound on the external/secondary cluster
		if ( g_errno == ENOTFOUND && m_hostdb == &g_hostdb2 )
			sendToTwin = false;
		// no longer do this for titledb, too common since msg4
		// cached stuff can make us slightly out of sync
		//if ( g_errno == ENOTFOUND )
		//	sendToTwin = false;
		// or a problem with a tfndb lookup, those are different for
		// each twin right now
		if ( m_msgType == 0x00 && m_rdbId == RDB_TFNDB )
			sendToTwin = false;
		// do not send to twin if we are out of time
		time_t now           = getTime();
		int32_t   timeRemaining = m_startTime + m_totalTimeout - now;
		if ( timeRemaining <= 0 ) sendToTwin = false;
		// send to the twin
		if ( sendToTwin && sendToHostLoop(0,-1,-1) ) {
			log("net: Trying to send request msgType=0x%"XINT32" "
			    "to a twin. (this=0x%"PTRFMT")",
			    (int32_t)m_msgType,(PTRTYPE)this);
			m_sentToTwin = true;
			// . keep stats
			// . this is error based rerouting
			// . this can be timeouts as well, if the
			//   receiver sent a request itself and that
			//   timed out...
			g_stats.m_reroutes[(int)m_msgType][m_niceness]++;
			return;
		}
		// . otherwise we've failed on all hosts
		// . re-instate g_errno,might have been set by sendToHostLoop()
		g_errno = m_errnos[i];
		// unregister our sleep wrapper if we did
		//if ( m_registeredSleep ) {
		//	g_loop.unregisterSleepCallback ( this, sleepWrapper1 );
		//	m_registeredSleep = false;
		//}
		// destroy all slots that may be in progress (except "slot")
		//destroySlotsInProgress ( slot );
		// call callback with g_errno set
		//if ( m_callback ) m_callback ( m_state );
		// we're done, all slots should be destroyed by UdpServer
		//return;
	}
	closeUpShop ( slot );
}

void Multicast::closeUpShop ( UdpSlot *slot ) {
	// sanity check
	if ( ! m_inUse ) { char *xx=NULL;*xx=0; }
	// debug msg
	//log("Multicast exiting (this=%"INT32")",(int32_t)&m_msg34);
	// destroy the OTHER slots we've spawned that are in progress
	destroySlotsInProgress ( slot );
	// if we have no slot per se, skip this stuff
	if ( ! slot ) goto skip;
	// . now we have a good reply... but not if g_errno is set
	// . save the reply of this slot here
	// . this is bad if we got an g_errno above, it will set the slot's
	//   readBuf to NULL up there, and that will make m_readBuf NULL here
	//   causing a mem leak. i fixed by adding an mfree on m_replyBuf 
	//   in Multicast::reset() routine. 
	// . i fixed again by ensuring we do not set m_ownReadBuf to false
	//   in getBestReply() below if m_readBuf is NULL
	m_readBuf        = slot->m_readBuf;
	m_readBufSize    = slot->m_readBufSize;
	m_readBufMaxSize = slot->m_readBufMaxSize;
	// . if the slot had an error, propagate it so it will be set when
	//   we call the callback.
	if(!g_errno) g_errno = slot->m_errno;
	// . sometimes UdpServer will read the reply into a temporary buffer
	// . this happens if the udp server is hot (async signal based) and
	//   m_replyBuf is NULL because he cannot malloc a buf to read into
	//   because malloc is not async signal safe
	if ( slot->m_tmpBuf == slot->m_readBuf ) m_freeReadBuf = false;
	// don't let UdpServer free the readBuf now that we point to it
	slot->m_readBuf = NULL;

	// save slot so msg4 knows what slot replied in udpserver
	// for doing its flush callback logic
	m_slot = slot;

 skip:
	// unregister our sleep wrapper if we did
	if ( m_registeredSleep ) {
		g_loop.unregisterSleepCallback ( this , sleepWrapper1 );
		m_registeredSleep = false;
#ifdef _GLOBALSPEC_
		// debug msg
		//logf(LOG_DEBUG,"net: mcast unregistered1 this= %08"XINT32"",
		//     (int32_t)this);
#endif
	}
	// unregister our sleep wrapper if we did
	if ( m_registeredSleep2 ) {
		g_loop.unregisterSleepCallback ( this , sleepWrapper1b );
		m_registeredSleep2 = false;
	}
	// ok, now if infiniteLoop is true, we must retry. this helps us
	// alot if one host is under such sever load that any disk read
	// returns ETRYAGAIN because there is no room in the thread queue.
	// do not retry on persistent error, only on temporary ones.
	if ( m_retryForever                &&
	     g_errno                       &&
	     g_errno != ENOTFOUND          && 
	     g_errno != EMISSINGQUERYTERMS &&
	     g_errno != EBADENGINEER         ) {
		log("net: Multicast retrying request send in 2 seconds.");
		m_retryCount++;
		// bail if already registered
		if ( m_registeredSleep2 ) return;
		// try the whole shebang again every 2 seconds
		if ( ! g_loop.registerSleepCallback (2000,this,sleepWrapper1b,
						     m_niceness))
			log("net: Failed to register sleep callback to "
			    "resend multicast request. Giving up. Retry "
			    "failed.");
		else
			m_registeredSleep2 = true;
		return;
	}
	if ( ! g_errno && m_retryCount > 0 ) 
	       log("net: Multicast succeeded after %"INT32" retries.",m_retryCount);
	// allow us to be re-used now, callback might relaunch
	m_inUse = false;
	// now call the user callback if it exists
	if ( m_callback ) {
		//		uint64_t profilerStart;
		//uint64_t profilerEnd;
		//uint64_t statStart,statEnd;

		//if (g_conf.m_profilingEnabled){
		//	//profilerStart=gettimeofdayInMillisecondsLocal();
		//	address=(int32_t)m_callback;
		//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
		//}
		//g_loop.startBlockedCpuTimer();
		m_callback ( m_state , m_state2 );
		//if (g_conf.m_profilingEnabled) {
		//	if(!g_profiler.endTimer(address,__PRETTY_FUNCTION__))
		//		log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
		//		    (int32_t)address);
		//}
	}
}

void sleepWrapper1b ( int bogusfd , void *state ) {
	Multicast *THIS = (Multicast *)state;
	// clear m_retired, m_errnos, m_slots
	memset ( THIS->m_retired, 0, sizeof(char     ) * MAX_HOSTS_PER_GROUP );
	memset ( THIS->m_errnos , 0, sizeof(int32_t  ) * MAX_HOSTS_PER_GROUP );
	memset ( THIS->m_slots  , 0, sizeof(UdpSlot *) * MAX_HOSTS_PER_GROUP );
	memset ( THIS->m_inProgress,0,sizeof(char    ) * MAX_HOSTS_PER_GROUP );
	// retry the whole she-bang
	if ( THIS->sendToHostLoop ( THIS->m_key , -1 , -1 ) ) {
		// if call succeeded, unregister our sleep callback
		g_loop.unregisterSleepCallback ( THIS , sleepWrapper1b );
		return;
	}
	// otherwise, retry forever
	log("net: Failed to launch multicast request. THIS=%"PTRFMT". Waiting "
	    "and retrying.",(PTRTYPE)THIS);
}

// destroy all slots that may be in progress (except "slot")
void Multicast::destroySlotsInProgress ( UdpSlot *slot ) {
	// . always destroy any msg34 slots in progress
	// . if we re-route then we span new msg34 requests, and if we get
	//   back an original reply we need to take out those msg34 requests
	//   because if they get a reply they may try to access a Multicast
	//   class that no longer exists
	//if ( m_doDiskLoadBalancing ) m_msg34.destroySlotsInProgress ( );
	// do a loop over all hosts in the group
	for (int32_t i = 0 ; i < m_numHosts ; i++ ) {
		// . destroy all slots but this one that are in progress
		// . we'll be destroyed when we return from the cback
		if ( ! m_slots[i]         ) continue;
		// transaction is not in progress if m_errnos[i] is set
		if (   m_errnos[i]        ) continue;
		// dont' destroy us, it'll happen when we return
		if (   m_slots[i] == slot ) continue;
		// must be in progress
		if ( ! m_inProgress[i] ) continue;
		// sometimes the slot is recycled from under us because
		// we already got a reply from it
		//if ( m_slots[i]->m_state != this ) continue;
		// don't free his sendBuf, readBuf is ok to free, however
		m_slots[i]->m_sendBufAlloc = NULL;
		// if niceness is 0, use the higher priority udpServer
		UdpServer *us = &g_udpServer;
		if ( m_realtime ) us = &g_udpServer2;
		// . stamp him so he doesn't have a better ping than host of #i
		// . timedOut=true -->only stamp him if it makes his ping worse
		//int32_t      hostId       = m_slots[i]->m_hostId;
		//int64_t lastSendTime = m_slots[i]->m_lastSendTime;
		//int64_t now          = gettimeofdayInMilliseconds() ;
		//int64_t tripTime     = now - lastSendTime;
		// . we no longer stamp hosts here, leave that up to
		//   Hostdb::pingHost()
		// tripTime is always in milliseconds
		//m_hostdb->stampHost ( hostId , tripTime , true/*timedOut?*/);
		//#ifdef _DEBUG_
		//fprintf(stderr,"stamping host #%"INT32" w/ tripTime=%"INT64"ms\n",
		//	hostId, tripTime);
		//#endif

		// if caller provided the buffer, don't free it cuz "slot"
		// contains it (or m_readBuf)
		if ( m_replyBuf == m_slots[i]->m_readBuf )
			m_slots[i]->m_readBuf = NULL;
		// destroy this slot that's in progress
		us->destroySlot ( m_slots[i] );
		// do not re-destroy. consider no longer in progress.
		m_inProgress[i] = 0;
	}
}


// we set *freeReply to true if you'll need to free it
char *Multicast::getBestReply ( int32_t *replySize , 
				int32_t *replyMaxSize , 
				bool *freeReply, 
				bool  steal) {
	*replySize    = m_readBufSize;
	*replyMaxSize = m_readBufMaxSize;
	if(steal) m_freeReadBuf = false;
	*freeReply    = m_freeReadBuf;
	// this can be NULL if we destroyed the slot in progress only to
	// try another host who was dead!
	if ( m_readBuf ) m_ownReadBuf  = false;
	return m_readBuf;
}

