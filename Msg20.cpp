#include "gb-include.h"
#include "XmlDoc.h"

static void gotReplyWrapper20 ( void *state , void *state20 ) ;
//static void gotReplyWrapper20b ( void *state , UdpSlot *slot );
static void handleRequest20   ( UdpSlot *slot , int32_t netnice );
static bool gotReplyWrapperxd ( void *state ) ;

Msg20::Msg20 () { constructor(); }
Msg20::~Msg20() { reset(); }

void Msg20::constructor () {
	m_request = NULL;
	m_r       = NULL;
	m_inProgress = false;
	m_launched = false;
	m_ii = -1;
	m_owningParent = (void *)0x012345;
	m_constructedId = 5;
	reset();
	m_mcast.constructor();
}

void Msg20::destructor  () { reset(); m_mcast.destructor(); }

#include "Process.h"

void Msg20::freeReply() {

	//log("msg20: freeing reply for msg20=0x%"PTRFMT" reply=0x%"PTRFMT"",
	//    (PTRTYPE)this,(PTRTYPE)m_r);

	if ( ! m_r ) return;
	// sometimes the msg20 reply carries an merged bffer from
	// msg40 that is a constructed ptr_eventSummaryLines from a
	// merge operation in msg40. this fixes the "merge20buf1" memory
	// leak from Msg40.cpp
	m_r->destructor();
	if ( m_ownReply ) mfree ( m_r, m_replyMaxSize , "Msg20b"  );
	m_r = NULL;

}

void Msg20::reset() { 

	//log("msg20: resetting msg20=0x%"PTRFMT" reply=0x%"PTRFMT"",
	//    (PTRTYPE)this,(PTRTYPE)m_r);

	if ( ! m_owningParent ) { char *xx=NULL;*xx=0; }

	// not allowed to reset one in progress
	if ( m_inProgress ) { 
		// do not core on abrupt exits!
		if (g_process.m_mode == EXIT_MODE ) {
			log("msg20: msg20 not being freed because exiting.");
			return;
		}
		// otherwise core
		char *xx=NULL;*xx=0; 
	}
	m_launched = false;
	if ( m_request && m_request   != m_requestBuf )
		mfree ( m_request , m_requestSize  , "Msg20rb1" );
	freeReply();
	//if ( m_r ) m_r->destructor();
	//if ( m_r && m_ownReply ) //&& (char *)m_r != m_replyBuf )
	//	mfree ( m_r       , m_replyMaxSize , "Msg20b"  );
	//m_r            = NULL; // the reply ptr
	m_request      = NULL; // the request buf ptr
	m_gotReply     = false;
	m_errno        = 0;
	m_requestDocId = -1LL;
	m_callback     = NULL;
	m_callback2    = NULL;
	m_state        = NULL;
	m_ownReply     = true;
	// resets
	m_pqr_old_score        = 0.0;
	m_pqr_factor_quality   = 1.0;
	m_pqr_factor_diversity = 1.0;
	m_pqr_factor_inlinkers = 1.0;
	m_pqr_factor_proximity = 1.0;
	m_pqr_factor_ctype     = 1.0;
	m_pqr_factor_lang      = 1.0; // includes country
}

bool Msg20::registerHandler ( ) {
        // . register ourselves with the udp server
        // . it calls our callback when it receives a msg of type 0x20
        if ( ! g_udpServer.registerHandler ( 0x20, handleRequest20 )) 
		return false;
        return true;
}

// copy "src" to ourselves
void Msg20::copyFrom ( Msg20 *src ) {
	gbmemcpy ( this , src , sizeof(Msg20) );
	// if the Msg20Reply was actually in src->m_replyBuf[] we have to
	// re-serialize into our this->m_replyBuf[] in order for the ptrs
	// to be correct
	//if ( (char *)src->m_r == src->m_replyBuf ) {
	//	// ok, point our Msg20Reply to our m_replyBuf[]
	//	m_r = (Msg20Reply *)m_replyBuf;
	//	// serialize the reply into that buf
	//	src->m_r->serialize ( m_replyBuf , MSG20_MAX_REPLY_SIZE );
	//	// then change the offsets to ptrs into this->m_replyBuf
	//	m_r->deserialize ();
	//}

	// make sure it does not free it!
	src->m_r = NULL;
	// why would we need to re-do the request? dunno, pt to it just in case
	if ( src->m_request == src->m_requestBuf ) 
		m_request = m_requestBuf;
	// make sure destructor does not free this
	src->m_request = NULL;
	// but should free its request, m_request, if it does not point to
	// src->m_requestBuf[], but an allocated buffer
	src->destructor();
}

// returns true and sets g_errno on error, otherwise, blocks and returns false
bool Msg20::getSummary ( Msg20Request *req ) {

	// reset ourselves in case recycled
	reset();

	if ( ! m_owningParent ) { char *xx=NULL;*xx=0; }

	// consider it "launched"
	m_launched = true;

	// save it
	m_requestDocId = req->m_docId;
	m_state        = req->m_state;
	m_callback     = req->m_callback;
	m_callback2    = req->m_callback2;
	m_expected     = req->m_expected;
	m_eventId      = req->m_eventId;

	// clear this
	//m_eventIdBits.clear();
	// set this
	//if ( req->m_eventId ) m_eventIdBits.addEventId(req->m_eventId);

	Hostdb *hostdb = req->m_hostdb;
	// ensure hostdb has a host in it
	if ( ! hostdb ) hostdb = &g_hostdb;
	// does this ever happen?
	if ( hostdb->getNumHosts() <= 0 ) {
		log("build: hosts2.conf is not in working directory, or "
		    "contains no valid hosts.");
		g_errno = EBADENGINEER;
		return true;
	}

	// do not re-route to twins if accessing an external network
	if ( hostdb != &g_hostdb ) req->m_expected = false;

	if ( req->m_docId < 0 && ! req->ptr_ubuf ) {
		log("msg20: docid<0 and no url for msg20::getsummary");
		g_errno = EBADREQUEST;
		return true;
	}

	// get groupId from docId, if positive
	uint32_t shardNum;
	if ( req->m_docId >= 0 ) 
		shardNum = hostdb->getShardNumFromDocId(req->m_docId);
	else {
		int64_t pdocId = g_titledb.getProbableDocId(req->ptr_ubuf);
		shardNum = getShardNumFromDocId(pdocId);
	}

	// we might be getting inlinks for a spider request
	// so make sure timeout is inifinite for that...
	int32_t timeout = 9999999; // 10 million seconds, basically inf.
	if ( req->m_niceness == 0 ) timeout = 20;

	// for diffbot make timeout super long so we aren't tripped up
	// by dead hosts that aren't really dead.
	// CollectionRec *cr = g_collectiondb.getRec ( req->m_collnum );
	// if ( cr && cr->m_isCustomCrawl && req->m_niceness == 0 ) 
	// 	timeout = 300;

	// get our group
	int32_t  allNumHosts = hostdb->getNumHostsPerShard();
	Host *allHosts    = hostdb->getShard ( shardNum );//getGroup(groupId );

	// put all alive hosts in this array
	Host *cand[32];
	int64_t  nc = 0;
	for ( int32_t i = 0 ; i < allNumHosts ; i++ ) {
		// get that host
		Host *hh = &allHosts[i];
		// skip if dead
		if ( g_hostdb.isDead(hh) ) continue;

		// Respect no-spider, no-query directives from hosts.conf 
		if ( !req->m_getLinkInfo && ! hh->m_queryEnabled ) continue;
		if ( req->m_getLinkInfo && ! hh->m_spiderEnabled ) continue;
		// add it if alive
		cand[nc++] = hh;
	}
	// if none alive, make them all candidates then
	bool allDead = (nc == 0);
	for ( int32_t i = 0 ; allDead && i < allNumHosts ; i++ ) {
		// NEVER add a noquery host to the candidate list, even
		// if the query host is dead
		if ( ! allHosts[i].m_queryEnabled ) continue;
		cand[nc++] = &allHosts[i];
	}

	if ( nc == 0 ) {
		log("msg20: error sending mcast: no queryable hosts "
		    "availble to handle summary generation");
		g_errno = EBADENGINEER;
		m_gotReply = true;
		return true;
	}

	// route based on docid region, not parity, because we want to hit
	// the urldb page cache as much as possible
	int64_t sectionWidth =((128LL*1024*1024)/nc)+1;//(DOCID_MASK/nc)+1LL;
	int64_t probDocId    = req->m_docId;
	// i think reference pages just pass in a url to get the summary
	if ( probDocId < 0 && req->size_ubuf ) 
		probDocId = g_titledb.getProbableDocId ( req->ptr_ubuf );
	if ( probDocId < 0        ) {
		log("query: Got bad docid/url combo.");
		probDocId = 0;
	}
	// we mod by 1MB since tied scores resort to sorting by docid
	// so we don't want to overload the host responsible for the lowest
	// range of docids. CAUTION: do this for msg22 too!
	// in this way we should still ensure a pretty good biased urldb
	// cache... 
	// . TODO: fix the urldb cache preload logic
	int32_t hostNum = (probDocId % (128LL*1024*1024)) / sectionWidth;
	if ( hostNum < 0 ) hostNum = 0; // watch out for negative docids
	if ( hostNum >= nc ) { char *xx = NULL; *xx = 0; }
	int32_t firstHostId = cand [ hostNum ]->m_hostId ;

	// . make buffer m_request to hold the request
	// . tries to use m_requestBuf[] if it is big enough to hold it
	// . allocs a new buf if MAX_MSG20_REQUEST_SIZE is too small
	// . serializes the request into m_request
	// . sets m_requestSize to the size of the serialized request
	m_requestSize = 0;
	m_request = req->serialize ( &m_requestSize, m_requestBuf ,
				     MAX_MSG20_REQUEST_SIZE );
	// . it sets g_errno on error and returns NULL
	// . we MUST call gotReply() here to set m_gotReply
	//   otherwise Msg40.cpp can end up looping forever
	//   calling Msg40::launchMsg20s()
	if ( ! m_request ) { gotReply(NULL); return true; }

        // . otherwise, multicast to a host in group "groupId"
	// . returns false and sets g_errno on error
	// . use a pre-allocated buffer to hold the reply
	// . TMPBUFSIZE is how much a UdpSlot can hold w/o allocating
        if ( ! m_mcast.send ( m_request         ,
			      m_requestSize     , 
			      0x20              , // msgType 0x20
			      false             , // m_mcast own m_request?
			      shardNum          , // send to group (groupKey)
			      false             , // send to whole group?
			      probDocId         , // key is lower bits of docId
			      this              , // state data
			      NULL              , // state data
			      gotReplyWrapper20 ,
			      timeout           , // 60 second time out
			      req->m_niceness   ,
			      false             , // real time?
			      firstHostId       , // first hostid
			      NULL,//m_replyBuf        ,
			      0,//MSG20_MAX_REPLY_SIZE,//m_replyMaxSize
			      false             , // free reply buf?
			      false             , // do disk load balancing?
			      -1                , // max cache age
			      0                 , // cacheKey
			      0                 , // bogus rdbId
			      -1                , // minRecSizes(unknownRDsize)
			      true              , // sendToSelf
			      true              , // retry forever
			      hostdb            )) {
		// sendto() sometimes returns "Network is down" so i guess
		// we just had an "error reply".
		log("msg20: error sending mcast %s",mstrerror(g_errno));
		m_gotReply = true;
		return true;
	}

	// we are officially "in progress"
	m_inProgress = true;

	// we blocked
	return false;
}

void gotReplyWrapper20 ( void *state , void *state2 ) {
	Msg20 *THIS = (Msg20 *)state;
	// gotReply() does not block, and does NOT call our callback
	THIS->gotReply ( NULL ) ;
	if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state );
	else THIS->m_callback2 ( THIS->m_state );
}

/*
void gotReplyWrapper20b ( void *state , UdpSlot *slot ) {
	Msg20 *THIS = (Msg20 *)state;
	// gotReply() does not block, and does NOT call our callback
	THIS->gotReply ( slot ) ;
	THIS->m_callback ( THIS->m_state );
}
*/

// . set m_reply/m_replySize to the reply
void Msg20::gotReply ( UdpSlot *slot ) {
	// we got the reply
	m_gotReply = true;
	// no longer in progress, we got a reply
	m_inProgress = false;
	// sanity check
	if ( m_r ) { char *xx = NULL; *xx = 0; }

	// free our serialized request buffer to save mem
	if ( m_request && m_request   != m_requestBuf ) {
		mfree ( m_request , m_requestSize  , "Msg20rb2" );
		m_request = NULL;
	}

	// save error so Msg40 can look at it
	if ( g_errno ) { 
		m_errno = g_errno; 
		if ( g_errno != EMISSINGQUERYTERMS )
			log("query: msg20: got reply for docid %"INT64" : %s",
			    m_requestDocId,mstrerror(g_errno));
		return; 
	}
	// . get the best reply we got
	// . we are responsible for freeing this reply
	bool freeit;
	// . freeit is true if mcast will free it
	// . we should always own it since we call deserialize and has ptrs
	//   into it
	char *rp = NULL;
	if ( slot ) {
		rp             = slot->m_readBuf;
		m_replySize    = slot->m_readBufSize;
		m_replyMaxSize = slot->m_readBufMaxSize;
		freeit = false;
	}
	else
		rp =m_mcast.getBestReply(&m_replySize,&m_replyMaxSize,&freeit);


	//if( rp != m_replyBuf )
	relabel( rp , m_replyMaxSize, "Msg20-mcastGBR" );

	// sanity check. make sure multicast is not going to free the
	// slot's m_readBuf... we need to own it.
	if ( freeit ) {
		log(LOG_LOGIC,"query: msg20: gotReply: Bad engineer.");
		char *xx = NULL; *xx = 0;
		return;
	}
	// see if too small for a getSummary request
	if ( m_replySize < (int32_t)sizeof(Msg20Reply) ) { 
		log("query: Summary reply is too small.");
		//char *xx = NULL; *xx = 0;
		m_errno = g_errno = EREPLYTOOSMALL; return; }

	// cast it
	m_r = (Msg20Reply *)rp;

	m_r->m_parentOwner = (void *)this;
	m_r->m_constructorId = 2;

	// reset this since constructor never called
	m_r->m_tmp = 0;
	// we own it now
	m_ownReply = true;
	// deserialize it, sets g_errno on error??? not yet TODO!
	m_r->deserialize();

	// log("msg20: got msg20=0x%"PTRFMT" msg20reply=0x%"PTRFMT" slot=0x%"PTRFMT" s_tmp=%i"
	//     ,(PTRTYPE)this
	//     ,(PTRTYPE)m_r
	//     ,(PTRTYPE)slot
	//     ,s_tmp);
}

// . this is called
// . destroys the UdpSlot if false is returned
void handleRequest20 ( UdpSlot *slot , int32_t netnice ) {
	// . check g_errno
	// . before, we were not sending a reply back here and we continued
	//   to process the request, even though it was empty. the slot
	//   had a NULL m_readBuf because it could not alloc mem for the read
	//   buf i'm assuming. and the slot was saved in a line below here...
	//   state20->m_msg22.m_parent = slot;
	if ( g_errno ) {
		log("net: Msg20 handler got error: %s.",mstrerror(g_errno));
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	// ensure request is big enough
	if ( slot->m_readBufSize < (int32_t)sizeof(Msg20Request) ) {
		g_udpServer.sendErrorReply ( slot , EBADREQUESTSIZE );
		return;
	}

	//log("query: got umsg20 %i 0x%"PTRFMT"",slot->m_readBufMaxSize,
	//    (PTRTYPE)slot->m_readBuf);

	// parse the request
	Msg20Request *req = (Msg20Request *)slot->m_readBuf;

	// . turn the string offsets into ptrs in the request
	// . this is "destructive" on "request"
	int32_t nb = req->deserialize();
	// sanity check
	if ( nb != slot->m_readBufSize ) { char *xx = NULL; *xx = 0; }

	// sanity check, the size include the \0
	if ( req->m_collnum < 0 ) {
		log("query: Got empty collection in msg20 handler. FIX! "
		    "from ip=%s port=%i",iptoa(slot->m_ip),(int)slot->m_port);
	        g_udpServer.sendErrorReply ( slot , ENOTFOUND );
		return; 
		//char *xx =NULL; *xx = 0; 
	}
	// if it's not stored locally that's an error
	if ( req->m_docId >= 0 && ! g_titledb.isLocal ( req->m_docId ) ) {
		log("query: Got msg20 request for non-local docId %"INT64"",
		    req->m_docId);
	        g_udpServer.sendErrorReply ( slot , ENOTLOCAL ); 
		return; 
	}

	// sanity
	if ( req->m_docId == 0 && ! req->ptr_ubuf ) { //char *xx=NULL;*xx=0; }
		log("query: Got msg20 request for docid of 0 and no url for "
		    "collnum=%"INT32" query %s",(int32_t)req->m_collnum,req->ptr_qbuf);
	        g_udpServer.sendErrorReply ( slot , ENOTFOUND );
		return; 
	}

	int64_t startTime = gettimeofdayInMilliseconds();

	// alloc a new state to get the titlerec
	XmlDoc *xd;

	try { xd = new (XmlDoc); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("query: msg20 new(%"INT32"): %s", (int32_t)sizeof(XmlDoc),
		    mstrerror(g_errno));
		g_udpServer.sendErrorReply ( slot, g_errno ); 
		return; 
	}
	mnew ( xd , sizeof(XmlDoc) , "xd20" );

	// ok, let's use the new XmlDoc.cpp class now!
	xd->set20 ( req );
	// encode slot
	xd->m_slot = slot;
	// set the callback
	xd->setCallback ( xd , gotReplyWrapperxd );
	// set set time
	xd->m_setTime = startTime;
	xd->m_cpuSummaryStartTime = 0;
	// . now as for the msg20 reply!
	// . TODO: move the parse state cache into just a cache of the
	//   XmlDoc itself, and put that cache logic into XmlDoc.cpp so
	//   it can be used more generally.
	Msg20Reply *reply = xd->getMsg20Reply ( );
	// this is just blocked
	if ( reply == (void *)-1 ) return;
	// got it?
	gotReplyWrapperxd ( xd );
}

bool gotReplyWrapperxd ( void *state ) {
	// grab it
	XmlDoc *xd = (XmlDoc *)state;

	// get it
	UdpSlot *slot = (UdpSlot *)xd->m_slot;
	// parse the request
	Msg20Request *req = (Msg20Request *)slot->m_readBuf;
	// print time
	int64_t now = gettimeofdayInMilliseconds();
	int64_t took = now - xd->m_setTime;
	int64_t took2 = now - xd->m_cpuSummaryStartTime;

	// if there is a baclkog of msg20 summary generation requests this
	// is really not the cpu it took to make the smmary, but how long it
	// took to get the reply. this request might have had to wait for the
	// other summaries to finish computing before it got its turn, 
	// meanwhile its clock was ticking. TODO: make this better?
	// only do for niceness 0 otherwise it gets interrupted by quickpoll
	// and can take a int32_t time.
	if ( (req->m_isDebug || took > 100) && req->m_niceness == 0 )
		log("query: Took %"INT64" ms to compute summary for d=%"INT64" u=%s "
		    "niceness=%"INT32" status=%s",
		    took,
		    xd->m_docId,xd->m_firstUrl.m_url,
		    xd->m_niceness ,
		    mstrerror(g_errno));
	if ( (req->m_isDebug || took2 > 100) &&
	     xd->m_cpuSummaryStartTime &&
	     req->m_niceness == 0 )
		log("query: Took %"INT64" ms of CPU to compute summary for d=%"INT64" "
		    "u=%s niceness=%"INT32" q=%s",
		    took2 ,
		    xd->m_docId,xd->m_firstUrl.m_url,
		    xd->m_niceness ,
		    req->ptr_qbuf );
	// error?
	if ( g_errno ) { xd->m_reply.sendReply ( xd ); return true; }
	// this should not block now
	Msg20Reply *reply = xd->getMsg20Reply ( );
	// sanity check, should not block here now
	if ( reply == (void *)-1 ) { char *xx=NULL;*xx=0; }
	// NULL means error, -1 means blocked. on error g_errno should be set
	if ( ! reply && ! g_errno ) { char *xx=NULL;*xx=0;}
	// send it off. will send an error reply if g_errno is set
	return reply->sendReply ( xd );
}

Msg20Reply::Msg20Reply ( ) {
	// this is free in destructor, so clear it here
	//ptr_eventSummaryLines = NULL;
	m_tmp = 0;

	m_parentOwner = NULL;
	m_constructorId = 0;

	// seems to be an issue... caused a core with bogus size_dbuf
	int32_t *sizePtr = &size_tbuf;
	int32_t *sizeEnd = &size_note;
	for ( ; sizePtr <= sizeEnd ; sizePtr++ )
		*sizePtr = 0;
}


// we need to free the ptr_summaryLines if it is pointing into a new buffer
// which is what Msg40 sometimes does to it when it merges Msg20Reply's 
// summaries for events together.
Msg20Reply::~Msg20Reply ( ) {
	destructor();
}

void Msg20Reply::destructor ( ) {
	// m_tmp is set to be the allocated buffer size in Msg40.cpp
	//if ( m_tmp == 0 ) return;
	//char *p = ptr_eventSummaryLines;
	//if ( ! p ) return;
	// this was causing a core!... try again now with NULLing out above
	//mfree ( p , m_tmp , "merge20buf" );
	//m_tmp = 0;
}

#include "Stats.h"

// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
bool Msg20Reply::sendReply ( XmlDoc *xd ) {

	// get it
	UdpSlot *slot = (UdpSlot *)xd->m_slot;

	if ( g_errno ) {
		// extract titleRec ptr
		log("query: Had error generating msg20 reply for d=%"INT64": "
		    "%s",xd->m_docId, mstrerror(g_errno));
		// don't forget to delete this list
	haderror:
		mdelete ( xd, sizeof(XmlDoc) , "Msg20" );
		delete ( xd );
		g_udpServer.sendErrorReply ( slot , g_errno ) ;
		return true;
	}

	// now create a buffer to store title/summary/url/docLen and send back
	int32_t  need = getStoredSize();
	char *buf  = (char *)mmalloc ( need , "Msg20Reply" );
	if ( ! buf ) goto haderror;

	// should never have an error!
	int32_t used = serialize ( buf , need );

	// sanity
	if ( used != need ) { char *xx=NULL;*xx=0; }

	// sanity check, no, might have been banned/filtered above around
	// line 956 and just called sendReply directly
	//if ( st->m_memUsed == 0 ) { char *xx=NULL;*xx=0; }

	// use blue for our color
	int32_t color = 0x0000ff;
	// but use dark blue for niceness > 0
	if ( xd->m_niceness > 0 ) color = 0x0000b0;

	//Msg20Reply *tt = (Msg20Reply *)buf;

	// sanity check
	if ( ! xd->m_utf8ContentValid ) { char *xx=NULL;*xx=0; }
	// for records
	int32_t clen = 0;
	if ( xd->m_utf8ContentValid ) clen = xd->size_utf8Content - 1;
	// show it in performance graph
	if ( xd->m_startTimeValid ) 
		g_stats.addStat_r ( clen                         ,
				    xd->m_startTime              , 
				    gettimeofdayInMilliseconds() ,
				    color                        );
	
	
	// . del the list at this point, we've copied all the data into reply
	// . this will free a non-null State20::m_ps (ParseState) for us
	mdelete ( xd , sizeof(XmlDoc) , "xd20" );
	delete ( xd );
	
	g_udpServer.sendReply_ass ( buf , need , buf , need , slot );

	return true;
}

// . this is destructive on the "buf". it converts offs to ptrs
// . sets m_r to the modified "buf" when done
// . sets g_errno and returns -1 on error, otherwise # of bytes deseril
int32_t Msg20::deserialize ( char *buf , int32_t bufSize ) { 
	if ( bufSize < (int32_t)sizeof(Msg20Reply) ) {
		g_errno = ECORRUPTDATA; return -1; }
	m_r = (Msg20Reply *)buf;
	m_r->m_parentOwner = (void *)this;
	m_r->m_constructorId = 1;
	// do not free "buf"/"m_r"
	m_ownReply = false;
	return m_r->deserialize ( );
}

int32_t Msg20Request::getStoredSize ( ) {
	int32_t size = (int32_t)sizeof(Msg20Request);
	// add up string buffer sizes
	int32_t *sizePtr = &size_qbuf;
	int32_t *sizeEnd = &size_displayMetas;
	for ( ; sizePtr <= sizeEnd ; sizePtr++ )
		size += *sizePtr;
	return size;
}

// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *Msg20Request::serialize ( int32_t *retSize     ,
				char *userBuf     ,
				int32_t  userBufSize ) {
	// make a buffer to serialize into
	char *buf  = NULL;
	int32_t  need = getStoredSize();
	// big enough?
	if ( need <= userBufSize ) buf = userBuf;
	// alloc if we should
	if ( ! buf ) buf = (char *)mmalloc ( need , "Msg20Ra" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy the easy stuff
	char *p = buf;
	gbmemcpy ( p , (char *)this , sizeof(Msg20Request) );
	p += (int32_t)sizeof(Msg20Request);
	// then store the strings!
	int32_t  *sizePtr = &size_qbuf;
	int32_t  *sizeEnd = &size_displayMetas;
	char **strPtr  = &ptr_qbuf;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// sanity check -- cannot copy onto ourselves
		if ( p > *strPtr && p < *strPtr + *sizePtr ) {
			char *xx = NULL; *xx = 0; }
		// copy the string into the buffer
		gbmemcpy ( p , *strPtr , *sizePtr );
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	return buf;
}

// convert offsets back into ptrs
int32_t Msg20Request::deserialize ( ) {
	// point to our string buffer
	char *p = m_buf;
	// then store the strings!
	int32_t  *sizePtr = &size_qbuf;
	int32_t  *sizeEnd = &size_displayMetas;
	char **strPtr  = &ptr_qbuf;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// sanity check
		if ( *sizePtr < 0 ) { char *xx = NULL; *xx =0; }
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	// return how many bytes we processed
	return (int32_t)sizeof(Msg20Request) + (p - m_buf);
}

int32_t Msg20Reply::getStoredSize ( ) {
	int32_t size = (int32_t)sizeof(Msg20Reply);
	// add up string buffer sizes
	int32_t *sizePtr = &size_tbuf;
	int32_t *sizeEnd = &size_note;
	for ( ; sizePtr <= sizeEnd ; sizePtr++ )
		size += *sizePtr;
	return size;
}


// returns NULL and set g_errno on error
int32_t Msg20Reply::serialize ( char *buf , int32_t bufSize ) {
	// copy the easy stuff
	char *p = buf;
	gbmemcpy ( p , (char *)this , sizeof(Msg20Reply) );
	p += (int32_t)sizeof(Msg20Reply);
	// then store the strings!
	int32_t  *sizePtr = &size_tbuf;
	int32_t  *sizeEnd = &size_note;
	char **strPtr  = &ptr_tbuf;
	//char **strEnd= &ptr_note;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// sometimes the ptr is NULL but size is positive
		// so watch out for that
		if ( *strPtr ) {
			gbmemcpy ( p , *strPtr , *sizePtr );
			// advance our destination ptr
			p += *sizePtr;
		}
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	int32_t used = p - buf;
	// sanity check, core on overflow of supplied buffer
	if ( used > bufSize ) { char *xx = NULL; *xx = 0; }
	// return it
	return used;
}

// convert offsets back into ptrs
int32_t Msg20Reply::deserialize ( ) {
	// point to our string buffer
	char *p = m_buf;
	// reset this since constructor never called
	m_tmp = 0;
	// then store the strings!
	int32_t  *sizePtr = &size_tbuf;
	int32_t  *sizeEnd = &size_note;
	char **strPtr  = &ptr_tbuf;
	//char **strEnd= &ptr_note;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// null str?
		if ( ! p ) *sizePtr = 0;
		// sanity check
		if ( *sizePtr < 0 ) { char *xx = NULL; *xx =0; }
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	// sanity
	if ( ptr_linkInfo && ((LinkInfo *)ptr_linkInfo)->m_lisize !=
		    size_linkInfo ) { 
		log("xmldoc: deserialize msg20 reply corruption error");
		log("xmldoc: DO YOU NEED TO NUKE CACHEDB.DAT?????");
		return -1;
		char *xx=NULL;*xx=0; 
	}

	// return how many bytes we used
	return (int32_t)sizeof(Msg20Reply) + (p - m_buf);
}
