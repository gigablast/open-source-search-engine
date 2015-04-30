#include "gb-include.h"

#include "Msg0.h"
#include "Tfndb.h"
//#include "Checksumdb.h"
#include "Clusterdb.h"
#include "Stats.h"
#include "Tagdb.h"
#include "Catdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Spider.h"
#include "Datedb.h"
#include "Linkdb.h"
#include "Msg5.h"                 // local getList()
#include "XmlDoc.h"

static void handleRequest0           ( UdpSlot *slot , int32_t niceness ) ;
static void gotMulticastReplyWrapper0( void *state , void *state2 ) ;
static void gotSingleReplyWrapper    ( void *state , UdpSlot *slot ) ;
static void gotListWrapper           ( void *state, RdbList *list, Msg5 *msg5);
static void gotListWrapper2          ( void *state, RdbList *list, Msg5 *msg5);
static void doneSending_ass          ( void *state , UdpSlot *slot ) ;

Msg0::Msg0 ( ) {
	constructor();
}

void Msg0::constructor ( ) {
	m_msg5  = NULL;
	m_msg5b = NULL;
//#ifdef SPLIT_INDEXDB
	//for ( int32_t i = 0; i < INDEXDB_SPLIT; i++ )
	//for ( int32_t i = 0; i < MAX_SHARDS; i++ )
	//	m_mcast[i].constructor();
	m_mcast.constructor();
	m_mcasts      = NULL;
	m_numRequests = 0;
	m_numReplies  = 0;
	//m_numSplit    = 1;
	m_errno       = 0;
	// reply buf
	m_replyBuf     = NULL;
	m_replyBufSize = 0;
	m_handyList.constructor();
//#endif
}

Msg0::~Msg0 ( ) {
	reset();
	m_handyList.freeList();
}

void Msg0::reset ( ) {
	if ( m_msg5  && m_deleteMsg5  ) {
		mdelete ( m_msg5 , sizeof(Msg5) , "Msg0" );
		delete ( m_msg5  );
	}
	if ( m_msg5b && m_deleteMsg5b ) {
		mdelete ( m_msg5b , sizeof(Msg5) , "Msg0" );
		delete ( m_msg5b );
	}
	m_msg5  = NULL;
	m_msg5b = NULL;
//#ifdef SPLIT_INDEXDB
	if ( m_replyBuf )
		mfree ( m_replyBuf, m_replyBufSize, "Msg0" );
	m_replyBuf = NULL;
	m_replyBufSize = 0;
//#endif
	if ( m_mcasts ) {
		mfree(m_mcasts,sizeof(Multicast),"msg0mcast");
		m_mcasts = NULL;
	}
	// no longer do this because we call reset after the msg5 completes
	// and it was destroying our handylist... so just call freelist
	// in the destructor now
	//m_handyList.freeList();
}

bool Msg0::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x0A
	if ( ! g_udpServer.registerHandler ( 0x00, handleRequest0 )) 
		return false;
	//if ( ! g_udpServer2.registerHandler ( 0x00, handleRequest0 )) 
	//	return false;
	return true;
}

// . THIS Msg0 class must be alloc'd, i.e. not on the stack, etc.
// . if list is stored locally this tries to get it locally
// . otherwise tries to get the list from the network
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . NOTE: i was having problems with queries being cached too long, you
//   see the cache here is a NETWORK cache, so when the machines that owns
//   the list updates it on disk it can't flush our cache... so use a small
//   maxCacheAge of like , 30 seconds or so...
bool Msg0::getList ( int64_t hostId      , // host to ask (-1 if none)
		     int32_t      ip          , // info on hostId
		     int16_t     port        ,
		     int32_t      maxCacheAge , // max cached age in seconds
		     bool      addToCache  , // add net recv'd list to cache?
		     char      rdbId       , // specifies the rdb
		     //char     *coll        ,
		     collnum_t collnum ,
		     RdbList  *list        ,
		     //key_t     startKey    , 
		     //key_t     endKey      , 
		     char     *startKey    ,
		     char     *endKey      ,
		     int32_t      minRecSizes ,  // use -1 for no max
		     void     *state       ,
		     void    (* callback)(void *state ),//, RdbList *list ) ,
		     int32_t      niceness    ,
		     bool      doErrorCorrection ,
		     bool      includeTree ,
		     bool      doMerge     ,
		     int32_t      firstHostId   ,
		     int32_t      startFileNum  ,
		     int32_t      numFiles      ,
		     int32_t      timeout       ,
		     int64_t syncPoint     ,
		     int32_t      preferLocalReads ,
		     Msg5     *msg5             ,
		     Msg5     *msg5b            ,
		     bool      isRealMerge      ,
//#ifdef SPLIT_INDEXDB
		     bool      allowPageCache    ,
		     bool      forceLocalIndexdb ,
		     bool      noSplit , // doIndexdbSplit    ,
		     int32_t      forceParitySplit  ) {
//#else
//		     bool      allowPageCache ) {
//#endif
	// this is obsolete! mostly, but we need it for PageIndexdb.cpp to 
	// show a "termlist" for a given query term in its entirety so you 
	// don't have to check each machine in the network. if this is true it
	// means to query each split and merge the results together into a
	// single unified termlist. only applies to indexdb/datedb.
	//if ( doIndexdbSplit ) { char *xx = NULL; *xx = 0; }
	// note this because if caller is wrong it hurts performance major!!
	//if ( doIndexdbSplit ) 
	//	logf(LOG_DEBUG,"net: doing msg0 with indexdb split true");
	// warning
	if ( collnum < 0 ) log(LOG_LOGIC,"net: NULL collection. msg0.");

	//if ( doIndexdbSplit ) { char *xx=NULL;*xx=0; }

	// reset the list they passed us
	list->reset();
	// get keySize of rdb
	m_ks = getKeySizeFromRdbId ( rdbId );
	// if startKey > endKey, don't read anything
	//if ( startKey > endKey ) return true;
	if ( KEYCMP(startKey,endKey,m_ks)>0 ) { char *xx=NULL;*xx=0; }//rettrue
	// . reset hostid if it is dead
	// . this is causing UOR queries to take forever when we have a dead
	if ( hostId >= 0 && g_hostdb.isDead ( hostId ) ) hostId = -1;
	// no longer accept negative minrecsize
	if ( minRecSizes < 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,
		    "net: msg0: Negative minRecSizes no longer supported.");
		char *xx=NULL;*xx=0;
		return true;
	}

	// debug msg
	//if ( niceness != 0 ) log("HEY start");
	// ensure startKey last bit clear, endKey last bit set
	//if ( (startKey.n0 & 0x01) == 0x01 ) 
	//	log("Msg0::getList: warning startKey lastbit set"); 
	//if ( (endKey.n0   & 0x01) == 0x00 ) 
	//	log("Msg0::getList: warning endKey lastbit clear"); 
	// remember these
	m_state         = state;
	m_callback      = callback;
	m_list          = list;
	m_hostId        = hostId;
	m_niceness      = niceness;
	//m_ip            = ip;
	//m_port          = port;
	m_addToCache    = addToCache;
	// . these define our request 100%
	//m_startKey      = startKey;
	//m_endKey        = endKey;
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey,endKey,m_ks);
	m_minRecSizes   = minRecSizes;
	m_rdbId         = rdbId;
	m_collnum = collnum;//          = coll;
	m_isRealMerge   = isRealMerge;
	m_allowPageCache = allowPageCache;

	// . group to ask is based on the first key 
	// . we only do 1 group per call right now
	// . groupMask must turn on higher bits first (count downwards kinda)
	// . titledb and spiderdb use special masks to get groupId

	// if diffbot.cpp is reading spiderdb from each shard we have to
	// get groupid from hostid here lest we core in getGroupId() below.
	// it does that for dumping spiderdb to the client browser. they
	// can download the whole enchilada.
	if ( hostId >= 0 && m_rdbId == RDB_SPIDERDB )
		m_shardNum = 0;
	// did they force it? core until i figure out what this is
	else if ( forceParitySplit >= 0 ) 
		//m_groupId =  g_hostdb.getGroupId ( forceParitySplit );
		m_shardNum = forceParitySplit;
	else
		//m_groupId = getGroupId ( m_rdbId , startKey , ! noSplit );
		m_shardNum = getShardNum ( m_rdbId , startKey );

	// if we are looking up a termlist in posdb that is split by termid and
	// not the usual docid then we have to set this posdb key bit that tells
	// us that ...
	if ( noSplit && m_rdbId == RDB_POSDB )
		m_shardNum = g_hostdb.getShardNumByTermId ( startKey );

	// how is this used?
	//if ( forceLocalIndexdb ) m_groupId = g_hostdb.m_groupId;
	if ( forceLocalIndexdb ) m_shardNum = getMyShardNum();

	// . store these parameters
	// . get a handle to the rdb in case we can satisfy locally
	// . returns NULL and sets g_errno on error
	QUICKPOLL((m_niceness));
	Rdb *rdb = getRdbFromId ( m_rdbId );
	if ( ! rdb ) return true;
	// we need the fixedDataSize
	m_fixedDataSize = rdb->getFixedDataSize();
	m_useHalfKeys   = rdb->useHalfKeys();
	// . debug msg
	// . Msg2 does this when checking for a cached compound list.
	//   compound lists do not actually exist, they are merges of smaller
	//   UOR'd lists.
	if ( maxCacheAge != 0 && ! addToCache && (numFiles > 0 || includeTree))
		log(LOG_LOGIC,"net: msg0: "
		    "Weird. check but don't add... rdbid=%"INT32".",(int32_t)m_rdbId);
	// set this here since we may not call msg5 if list not local
	//m_list->setFixedDataSize ( m_fixedDataSize );

	// . now that we do load balancing we don't want to do a disk lookup
	//   even if local if we are merging or dumping
	// . UNLESS g_conf.m_preferLocalReads is true
	if ( preferLocalReads == -1 ) 
		preferLocalReads = g_conf.m_preferLocalReads;

	// . always prefer local for full split clusterdb
	// . and keep the tfndb/titledb lookups in the same stripe
	// . so basically we can't do biased caches if fully split
	//if ( g_conf.m_fullSplit ) preferLocalReads = true;
	preferLocalReads = true;

	// it it stored locally?
	bool isLocal = ( m_hostId == -1 && //g_hostdb.m_groupId == m_groupId );
			 m_shardNum == getMyShardNum() );
	// only do local lookups if this is true
	if ( ! preferLocalReads ) isLocal = false;

	/*
	m_numSplit = 1;
	if ( g_hostdb.m_indexSplits > 1 &&
	     ( rdbId == RDB_POSDB || rdbId==RDB_DATEDB)&&
	     ! forceLocalIndexdb && doIndexdbSplit ) {
		isLocal  = false;
		//m_numSplit = INDEXDB_SPLIT;
		m_numSplit = g_hostdb.m_indexSplits;
		char *xx=NULL;*xx=0;
	}
	*/
	/*
	int64_t singleDocIdQuery = 0LL;
	if ( rdbId == RDB_POSDB ) {
		int64_t d1 = g_posdb.getDocId(m_startKey);
		int64_t d2 = g_posdb.getDocId(m_endKey);
		if ( d1+1 == d2 ) singleDocIdQuery = d1;
	}

	// . try the LOCAL termlist cache
	// . so when msg2 is evaluating a gbdocid:| query and it has to
	//   use msg0 to go across the network to get the same damn termlist
	//   over and over again for the same docid, this will help alot.
	// . ideally it'd be nice if the seo pipe in xmldoc.cpp can try to
	//   send the same gbdocid:xxxx docids to the same hosts. maybe hash
	//   based on docid into the list of hosts and if that host is busy
	//   just chain until we find someone not busy.
	if ( singleDocIdQuery &&
	     getListFromTermListCache ( coll,
					m_startKey,
					m_endKey,
					maxCacheAge,
					list ) )
		// found!
		return true;
	*/

	// but always local if only one host
	if ( g_hostdb.getNumHosts() == 1 ) isLocal = true;

	// force a msg0 if doing a docid restrictive query like
	// gbdocid:xxxx|<query> so we call cacheTermLists() 
	//if ( singleDocIdQuery ) isLocal = false;

	// . if the group is local then do it locally
	// . Msg5::getList() returns false if blocked, true otherwise
	// . Msg5::getList() sets g_errno on error
	// . don't do this if m_hostId was specified
	if ( isLocal ) { // && !g_conf.m_interfaceMachine ) {
		if ( msg5 ) {
			m_msg5 = msg5;
			m_deleteMsg5 = false;
		}
		else {
			try { m_msg5 = new ( Msg5 ); } 
			catch ( ... ) {
				g_errno = ENOMEM;
				log("net: Local alloc for disk read failed "
				    "while tring to read data for %s. "
				    "Trying remote request.",
				    getDbnameFromId(m_rdbId));
				goto skip;
			}
			mnew ( m_msg5 , sizeof(Msg5) , "Msg0" );
			m_deleteMsg5 = true;
		}

		QUICKPOLL(m_niceness);
		// same for msg5b
		if ( msg5b ) {
			m_msg5b = msg5b;
			m_deleteMsg5b = false;
		}
		/*
		else if ( m_rdbId == RDB_TITLEDB ) {
			try { m_msg5b = new ( Msg5 ); } 
			catch ( ... ) {
				g_errno = ENOMEM;
				log("net: Local alloc for disk read failed "
				    "while tring to read data for %s. "
				    "Trying remote request. 2.",
				    getDbnameFromId(m_rdbId));
				goto skip;
			}
			mnew ( m_msg5b , sizeof(Msg5) , "Msg0b" );
			m_deleteMsg5b = true;
		}
		*/
		QUICKPOLL(m_niceness);
		if ( ! m_msg5->getList ( rdbId,
					 m_collnum ,
					 m_list ,
					 m_startKey ,
					 m_endKey   ,
					 m_minRecSizes ,
					 includeTree   , // include Tree?
					 addToCache    , // addToCache?
					 maxCacheAge   ,
					 startFileNum  , 
					 numFiles      ,
					 this ,
					 gotListWrapper2   ,
					 niceness          ,
					 doErrorCorrection ,
					 NULL , // cacheKeyPtr
					 0    , // retryNum
					 -1   , // maxRetries
					 true , // compensateForMerge
					 syncPoint ,
					 NULL,//m_msg5b   ,
					 m_isRealMerge ,
					 m_allowPageCache ) ) return false;
		// nuke it
		reset();
		return true;
	}
skip:
	// debug msg
	if ( g_conf.m_logDebugQuery )
		log(LOG_DEBUG,"net: msg0: Sending request for data to "
		    "shard=%"UINT32" "
		    "listPtr=%"PTRFMT" minRecSizes=%"INT32" termId=%"UINT64" "
		    //"startKey.n1=%"XINT32",n0=%"XINT64" (niceness=%"INT32")",
		    "startKey.n1=%"XINT64",n0=%"XINT64" (niceness=%"INT32")",
		    //g_hostdb.makeHostId ( m_groupId ) ,
		    m_shardNum,
		    (PTRTYPE)m_list,
		    m_minRecSizes, g_posdb.getTermId(m_startKey) , 
		    //m_startKey.n1,m_startKey.n0 , (int32_t)m_niceness);
		    KEY1(m_startKey,m_ks),KEY0(m_startKey),
		    (int32_t)m_niceness);

	char *replyBuf = NULL;
	int32_t  replyBufMaxSize = 0;
	bool  freeReply = true;

	// adjust niceness for net transmission
	bool realtime = false;
	//if ( minRecSizes + 32 < TMPBUFSIZE ) realtime = true;

	// if we're niceness 0 we need to pre-allocate for reply since it
	// might be received within the asynchronous signal handler which
	// cannot call mmalloc()
	if ( realtime ) { // niceness <= 0 || netnice == 0 ) {
		// . we should not get back more than minRecSizes bytes since 
		//   we are now performing merges
		// . it should not slow things down too much since the hashing
		//   is 10 times slower than merging anyhow...
		// . CAUTION: if rdb is not fixed-datasize then this will
		//            not work for us! it can exceed m_minRecSizes.
		replyBufMaxSize = m_minRecSizes ;
		// . get a little extra to fix the error where we ask for 64 
		//   but get 72
		// . where is that coming from?
		// . when getting titleRecs we often exceed the minRecSizes 
		// . ?Msg8? was having trouble. was int16_t 32 bytes sometimes.
		replyBufMaxSize += 36;
		// why add ten percent?
		//replyBufMaxSize *= 110 ;
		//replyBufMaxSize /= 100 ;
		// make a buffer to hold the reply
//#ifdef SPLIT_INDEXDB
/*
		if ( m_numSplit > 1 ) {
			m_replyBufSize = replyBufMaxSize * m_numSplit;
			replyBuf = (char *) mmalloc(m_replyBufSize, "Msg0");
			m_replyBuf  = replyBuf;
			freeReply = false;
		}
		else
*/
//#endif
			replyBuf = (char *) mmalloc(replyBufMaxSize , "Msg0");
		// g_errno is set and we return true if it failed
		if ( ! replyBuf ) {
			log("net: Failed to pre-allocate %"INT32" bytes to hold "
			    "data read remotely from %s: %s.",
			    replyBufMaxSize,getDbnameFromId(m_rdbId),
			    mstrerror(g_errno));
			return true;
		}
	}

	// . make a request with the info above (note: not in network order)
	// . IMPORTANT!!!!! if you change this change 
	//   Multicast.cpp::sleepWrapper1 too!!!!!!!!!!!!
	//   no, not anymore, we commented out that request peeking code
	char *p = m_request;
	*(int64_t *) p = syncPoint        ; p += 8;
	//*(key_t     *) p = m_startKey       ; p += sizeof(key_t);
	//*(key_t     *) p = m_endKey         ; p += sizeof(key_t);
	*(int32_t      *) p = m_minRecSizes    ; p += 4;
	*(int32_t      *) p = startFileNum     ; p += 4;
	*(int32_t      *) p = numFiles         ; p += 4;
	*(int32_t      *) p = maxCacheAge      ; p += 4;
	if ( p - m_request != RDBIDOFFSET ) { char *xx=NULL;*xx=0; }
	*p               = m_rdbId          ; p++;
	*p               = addToCache       ; p++;
	*p               = doErrorCorrection; p++;
	*p               = includeTree      ; p++;
	*p               = (char)niceness   ; p++;
	*p               = (char)m_allowPageCache; p++;
	KEYSET(p,m_startKey,m_ks);          ; p+=m_ks;
	KEYSET(p,m_endKey,m_ks);            ; p+=m_ks;
	// NULL terminated collection name
	//strcpy ( p , coll ); p += gbstrlen ( coll ); *p++ = '\0';
	*(collnum_t *)p = m_collnum; p += sizeof(collnum_t);
	m_requestSize    = p - m_request;
	// ask an individual host for this list if hostId is NOT -1
	if ( m_hostId != -1 ) {
		// get Host
		Host *h = g_hostdb.getHost ( m_hostId );
		if ( ! h ) { 
			g_errno = EBADHOSTID; 
			log(LOG_LOGIC,"net: msg0: Bad hostId of %"INT64".",
			    m_hostId);
			return true;
		}
		// if niceness is 0, use the higher priority udpServer
		UdpServer *us ;
		uint16_t port;
		QUICKPOLL(m_niceness);
		//if ( niceness <= 0 || netnice == 0 ) { 
		//if ( realtime ) {
		//	us = &g_udpServer2; port = h->m_port2; }
		//else                 { 
		us = &g_udpServer ; port = h->m_port ; 
		// . returns false on error and sets g_errno, true otherwise
		// . calls callback when reply is received (or error)
		// . we return true if it returns false
		if ( ! us->sendRequest ( m_request     ,
					 m_requestSize ,
					 0x00          , // msgType
					 h->m_ip       ,
					 port          ,
					 m_hostId      ,
					 NULL          , // the slotPtr
					 this          ,
					 gotSingleReplyWrapper ,
					 timeout       ,
					 -1            , // backoff
					 -1            , // maxwait
					 replyBuf      ,
					 replyBufMaxSize ,
					 m_niceness     ) ) // cback niceness
			return true;
		// return false cuz it blocked
		return false;
	}
	// timing debug
	if ( g_conf.m_logTimingNet )
		m_startTime = gettimeofdayInMilliseconds();
	else
		m_startTime = 0;
	//if ( m_rdbId == RDB_INDEXDB ) log("Msg0:: getting remote indexlist. "
	//			"termId=%"UINT64", "
	//			"groupNum=%"UINT32"",
	//			g_indexdb.getTermId(m_startKey) ,
	//			g_hostdb.makeHostId ( m_groupId ) );

	/*
	// make the cache key so we can see what remote host cached it, if any
	char cacheKey[MAX_KEY_BYTES];
	//key_t cacheKey = makeCacheKey ( startKey     ,
	makeCacheKey ( startKey     ,
		       endKey       ,
		       includeTree  ,
		       minRecSizes  ,
		       startFileNum ,
		       numFiles     ,
		       cacheKey     ,
		       m_ks         );
	*/

	// . get the top int32_t of the key
	// . i guess this will work for 128 bit keys... hmmmmm
	int32_t keyTop = hash32 ( (char *)startKey , m_ks );

	/*
	// allocate space
	if ( m_numSplit > 1 ) {
		int32_t  need = m_numSplit * sizeof(Multicast) ;
		char *buf  = (char *)mmalloc ( need,"msg0mcast" );
		if ( ! buf ) return true;
		m_mcasts = (Multicast *)buf;
		for ( int32_t i = 0; i < m_numSplit ; i++ )
			m_mcasts[i].constructor();
	}
	*/

        // . otherwise, multicast to a host in group "groupId"
	// . returns false and sets g_errno on error
	// . calls callback on completion
	// . select first host to send to in group based on upper 32 bits
	//   of termId (m_startKey.n1)
//#ifdef SPLIT_INDEXDB
	// . need to send out to all the indexdb split hosts
	m_numRequests = 0;
	m_numReplies  = 0;
	//for ( int32_t i = 0; i < m_numSplit; i++ ) {

	QUICKPOLL(m_niceness);
	//int32_t gr;
	char *buf;
	/*
	if ( m_numSplit > 1 ) {
		gr  = g_indexdb.getSplitGroupId ( baseGroupId, i );
		buf = &replyBuf[i*replyBufMaxSize];
	}
	else {
	*/
	//gr  = m_groupId;
	buf = replyBuf;
	//}

	// get the multicast
	Multicast *m = &m_mcast;
	//if ( m_numSplit > 1 ) m = &m_mcasts[i];

        if ( ! m->send ( m_request    , 
//#else
//        if ( ! m_mcast.send ( m_request    , 
//#endif
			      m_requestSize, 
			      0x00         , // msgType 0x00
			      false        , // does multicast own request?
			 m_shardNum ,
//#ifdef SPLIT_INDEXDB
//			      gr           , // group + offset
//#else
//			      m_groupId    , // group to send to (groupKey)
//#endif
			      false        , // send to whole group?
			      //m_startKey.n1, // key is passed on startKey
			      keyTop       , // key is passed on startKey
			      this         , // state data
			      NULL         , // state data
			      gotMulticastReplyWrapper0 ,
			      timeout      , // timeout in seconds (was 30)
			      niceness     ,
			      realtime     ,
			      firstHostId  ,
//#ifdef SPLIT_INDEXDB
//			      &replyBuf[i*replyBufMaxSize] ,
//#else
//			      replyBuf        ,
//#endif
			      buf             ,
			      replyBufMaxSize ,
			      freeReply       , // free reply buf?
			      true            , // do disk load balancing?
			      maxCacheAge     ,
			      //(key_t *)cacheKey        ,
			      // multicast uses it for determining the best
			      // host to send the request to when doing 
			      // disk load balancing. if the host has our 
			      // data cached, then it will probably get to
			      // handle the request. for now let's just assume
			      // this is a 96-bit key. TODO: fix...
			 0 , // *(key_t *)cacheKey        ,
			      rdbId           ,
			      minRecSizes     ) ) {
		log("net: Failed to send request for data from %s in shard "
		    "#%"UINT32" over network: %s.",
		    getDbnameFromId(m_rdbId),m_shardNum, mstrerror(g_errno));
		// no, multicast will free this when it is destroyed
		//if (replyBuf) mfree ( replyBuf , replyBufMaxSize , "Msg22" );
		// but speed it up
//#ifdef SPLIT_INDEXDB
		m_errno = g_errno;
		m->reset();
		if ( m_numRequests > 0 )
			return false;
//#else
//		m_mcast.reset();
//#endif
		return true;
	}
//#ifdef SPLIT_INDEXDB
	m_numRequests++;

//#endif
	// we blocked
	return false;
}

// . this is called when we got a local RdbList
// . we need to call it to call the original caller callback
void gotListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) {
	Msg0 *THIS = (Msg0 *) state;
	THIS->reset(); // delete m_msg5
	THIS->m_callback ( THIS->m_state );//, THIS->m_list );
}

// . return false if you want this slot immediately nuked w/o replying to it
void gotSingleReplyWrapper ( void *state , UdpSlot *slot ) {
	Msg0 *THIS = (Msg0 *)state;
	if ( ! g_errno ) { 
		int32_t  replySize    = slot->m_readBufSize;
		int32_t  replyMaxSize = slot->m_readBufMaxSize;
		char *reply        = slot->m_readBuf;
		THIS->gotReply( reply , replySize , replyMaxSize );
		// don't let UdpServer free this since we own it now
		slot->m_readBuf = NULL;
	}
	// never let m_request (sendBuf) be freed
	slot->m_sendBufAlloc = NULL;
	// do the callback now
	THIS->m_callback ( THIS->m_state );// THIS->m_list );
}

void gotMulticastReplyWrapper0 ( void *state , void *state2 ) {
	Msg0 *THIS = (Msg0 *)state;
//#ifdef SPLIT_INDEXDB
	//if ( g_hostdb.m_indexSplits > 1 ) {
	/*
	if ( THIS->m_numSplit > 1 ) {
		THIS->m_numReplies++;
		if ( ! g_errno ) {
			QUICKPOLL(THIS->m_niceness);
			// for split, wait for all replies
			if ( THIS->m_numReplies < THIS->m_numRequests )
				return;
			else {
				// got it all, call the reply
				// watch out for someone having an error
				if ( ! THIS->m_errno )
					THIS->gotSplitReply();
				else
					g_errno = THIS->m_errno;
			}
		}
		else {
			// got an error, set an error state and wait for all
			// replies
			THIS->m_errno = g_errno;
			if ( THIS->m_numReplies < THIS->m_numRequests )
				return;
		}
		THIS->m_callback ( THIS->m_state );//, THIS->m_list );
	}
//#else
	else {
	*/
	if ( ! g_errno ) {
		int32_t  replySize;
		int32_t  replyMaxSize;
		bool  freeit;
		char *reply = THIS->m_mcast.getBestReply (&replySize,
							  &replyMaxSize,
							  &freeit);
		THIS->gotReply( reply , replySize , replyMaxSize ) ;
	}
	THIS->m_callback ( THIS->m_state );//, THIS->m_list );
	//}
//#endif
}

/*
//#ifdef SPLIT_INDEXDB
// . we are responsible for freeing reply/replySize
void Msg0::gotSplitReply ( ) {
	// sanity check
	if ( m_numSplit <= 1 ) { char *xx = NULL; *xx = 0; }
	// i don't think we use this, otherwise need to update for posdb
	char *xx=NULL;*xx=0;
	// get all the split lists
	int32_t totalSize = 0;
	RdbList lists[MAX_SHARDS];
	RdbList *listPtrs[MAX_SHARDS];
	for ( int32_t i = 0; i < m_numSplit; i++ ) {
		listPtrs[i] = &lists[i];
		int32_t replySize;
		int32_t replyMaxSize;
		bool freeit;
		char *reply = m_mcasts[i].getBestReply ( &replySize,
							&replyMaxSize,
							&freeit );
		lists[i].set ( reply,
			       replySize,
			       reply,
			       replyMaxSize,
			       m_startKey,
			       m_endKey,
			       m_fixedDataSize,
			       freeit,
			       m_useHalfKeys,
			       m_ks );
		totalSize += lists[i].m_listSize;

		QUICKPOLL(m_niceness);
		//log(LOG_INFO, "Msg0: ls=%"INT32" rs=%"INT32" rms=%"INT32" fi=%"INT32" r=%"XINT32"",
		//	      lists[i].m_listSize, replySize,
		//	      replyMaxSize, (int32_t)freeit, reply);
		//log(LOG_INFO, "Msg0: ------------------------------------");
		//lists[i].printList();
	}
	// empty list?
	if ( totalSize <= 0 ) {
		m_list->set ( NULL,
			      0,
			      NULL,
			      0,
			      m_fixedDataSize,
			      true,
			      m_useHalfKeys,
			      m_ks );
		return;
	}
	// init the list
	char *alloc = (char*)mmalloc(totalSize, "Msg0");
	if ( !alloc ) {
		g_errno = ENOMEM;
		log ( "Msg0: Could not allocate %"INT32" bytes for split merge",
		      totalSize );
		return;
	}
	m_list->set ( alloc,
		      0,
		      alloc,
		      totalSize,
		      m_fixedDataSize,
		      true,
		      m_useHalfKeys,
		      m_ks );

	QUICKPOLL(m_niceness);
	// merge them together into one list
	// NOTE: This should only be happening for index lists
	char prevKey[MAX_KEY_BYTES];
	memset(prevKey, 0, MAX_KEY_BYTES);
	int32_t prevCount = 0;
	int32_t dupsRemoved = 0;
	int32_t filtered    = 0;
	m_list->indexMerge_r ( (RdbList**)listPtrs,
			       m_numSplit,
			       m_startKey,
			       m_endKey,
			       m_minRecSizes,  // minRecSizes
			       false,          // remove negative keys
			       (char*)prevKey, // prevKey
			       &prevCount,     // prevCountPtr
			       0x7fffffff,     // truncLimit
			       &dupsRemoved,   // dupsRemoved
			       m_rdbId,        // rdbId
			       &filtered,      // filtered
			       false,          // doGroupMask
			       false ,         // isRealMerge
			       false         , // fast merge?
			       m_niceness    );
	//log(LOG_INFO, "Msg0: ------------------------------------");
	//log(LOG_INFO, "Msg0: ls=%"INT32"",
	//	      m_list->m_listSize);
	//m_list->printList();
	// cache?
	if ( ! m_addToCache ) return;
	// cache...
}
//#endif
*/

// . returns false and sets g_errno on error
// . we are responsible for freeing reply/replySize
void Msg0::gotReply ( char *reply , int32_t replySize , int32_t replyMaxSize ) {
	// timing debug
	if ( g_conf.m_logTimingNet && m_rdbId==RDB_POSDB && m_startTime > 0 )
		log(LOG_TIMING,"net: msg0: Got termlist, termId=%"UINT64". "
		    "Took %"INT64" ms, replySize=%"INT32" (niceness=%"INT32").",
		    g_posdb.getTermId ( m_startKey ) ,
		    gettimeofdayInMilliseconds()-m_startTime,
		    replySize,m_niceness);
	// TODO: insert some seals for security, may have to alloc
	//       separate space for the list then
	// set the list w/ the remaining data
	QUICKPOLL(m_niceness);

	m_list->set ( reply                , 
		      replySize            , 
		      reply                , // alloc buf begins here, too
		      replyMaxSize         ,
		      m_startKey           , 
		      m_endKey             , 
		      m_fixedDataSize      ,
		      true                 , // ownData?
		      m_useHalfKeys        ,
		      m_ks                 );

	// return now if we don't add to cache
	//if ( ! m_addToCache ) return;
	//
	// add posdb list to termlist cache
	//
	//if ( m_rdbId != RDB_POSDB ) return;
	// add to LOCAL termlist cache
	//addToTermListCache(m_coll,m_startKey,m_endKey,m_list);
	// ignore any error adding to cache
	//g_errno = 0;

	// . NO! no more network caching, we got gigabit... save space
	//   for our disk, no replication, man, mem is expensive

	// . throw the just the list into the net cache
	// . addToNetCache() will copy it for it's own
	// . our current copy should be freed by the user's callback somewhere
	// . grab our corresponding rdb's local cache
	// . we'll use it to store this list since there's no collision chance
	//RdbCache *cache = m_rdb->getCache ();
	// . add the list to this cache
	// . returns false and sets g_errno on error
	// . will not be added if cannot copy the data
	//cache->addList ( m_startKey , m_list ) ;
	// reset g_errno -- we don't care if cache coulnd't add it
	//g_errno = 0;
}

// this conflicts with the State0 class in PageResults.cpp, so make it State00
class State00 {
public:
	Msg5       m_msg5;
	//Msg5       m_msg5b;
	RdbList    m_list;
	UdpSlot   *m_slot;
	int64_t  m_startTime;
	int32_t       m_niceness;
	UdpServer *m_us;
	char       m_rdbId;
};

//static RdbCache s_sectiondbCache;
//static bool s_initCache = false;
/*
HashTableX g_waitingTable;

void callWaitingHandlers ( void *state ) {
	int32_t saved = g_errno;
	XmlDoc *xd = (XmlDoc *)state;
	//slot = xd->m_hackSlot;
	//int32_t netnice = 1;
	//xd->m_hackSlot = NULL;
	int32_t niceness = xd->m_niceness;
	int64_t docId = xd->m_docId;
	delete ( xd );

	// call everyone in wait queue's handler0 now
 slotLoop:
	int32_t tableSlot = g_waitingTable.getSlot ( &docId );
	if ( tableSlot < 0 ) return;
	// get the udp socket that was waiting for the termlists to be cached
	UdpSlot *x = *(UdpSlot **)g_waitingTable.getValueFromSlot(tableSlot);
	// delete it after calling handler
	g_waitingTable.removeSlot ( tableSlot );
	// breathe
	QUICKPOLL(niceness);
	// re-instate error
	g_errno = saved;
	// re-call handler now that all termlists should be cached
	handleRequest0 ( x , 99 ); //netnice );
	// get the next guy waiting
	goto slotLoop;
}
*/

// . reply to a request for an RdbList
// . MUST call g_udpServer::sendReply or sendErrorReply() so slot can
//   be destroyed
void handleRequest0 ( UdpSlot *slot , int32_t netnice ) {
	// if niceness is 0, use the higher priority udpServer
	UdpServer *us = &g_udpServer;
	//if ( netnice == 0 ) us = &g_udpServer2;
	// get the request
	char *request     = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;
	// collection is now stored in the request, so i commented this out
	//if ( requestSize != MSG0_REQ_SIZE ) {
	//	log("net: Received bad data request size of %"INT32" bytes. "
	//	    "Should be %"INT32".", requestSize ,(int32_t)MSG0_REQ_SIZE);
	//	us->sendErrorReply ( slot , EBADREQUESTSIZE );
	//	return;
	//}
	// parse the request
	char *p                  = request;
	int64_t syncPoint          = *(int64_t *)p ; p += 8;
	//key_t     startKey           = *(key_t     *)p ; p += sizeof(key_t);
	//key_t     endKey             = *(key_t     *)p ; p += sizeof(key_t);
	int32_t      minRecSizes        = *(int32_t      *)p ; p += 4;
	int32_t      startFileNum       = *(int32_t      *)p ; p += 4;
	int32_t      numFiles           = *(int32_t      *)p ; p += 4;
	int32_t      maxCacheAge        = *(int32_t      *)p ; p += 4;
	char      rdbId              = *p++;
	char      addToCache         = *p++;
	char      doErrorCorrection  = *p++;
	char      includeTree        = *p++;
	// this was messing up our niceness conversion logic
	int32_t      niceness           = slot->m_niceness;//(int32_t)(*p++);
	// still need to skip it though!
	p++;
	bool      allowPageCache     = (bool)(*p++);
	char ks = getKeySizeFromRdbId ( rdbId );
	char     *startKey           = p; p+=ks;
	char     *endKey             = p; p+=ks;
	// then null terminated collection
	//char     *coll               = p;
	collnum_t collnum = *(collnum_t *)p; p += sizeof(collnum_t);

	CollectionRec *xcr = g_collectiondb.getRec ( collnum );
	if ( ! xcr ) g_errno = ENOCOLLREC;
	
	// error set from XmlDoc::cacheTermLists()?
	if ( g_errno ) {
		us->sendErrorReply ( slot , EBADRDBID ); return;}

	// is this being called from callWaitingHandlers()
	//bool isRecall = (netnice == 99);

	// . get the rdb we need to get the RdbList from
	// . returns NULL and sets g_errno on error
	//Msg0 msg0;
	//Rdb *rdb = msg0.getRdb ( rdbId );
	Rdb *rdb = getRdbFromId ( rdbId );
	if ( ! rdb ) { 
		us->sendErrorReply ( slot , EBADRDBID ); return;}

	// keep track of stats
	rdb->readRequestGet ( requestSize );

	/*
	// keep track of stats
	if ( ! isRecall ) rdb->readRequestGet ( requestSize );

	int64_t singleDocId2 = 0LL;
	if ( rdbId == RDB_POSDB && maxCacheAge ) {
		int64_t d1 = g_posdb.getDocId(startKey);
		int64_t d2 = g_posdb.getDocId(endKey);
		if ( d1+1 == d2 ) singleDocId2 = d1;
	}

	// have we parsed this docid and cached its termlists?
	bool shouldBeCached2 = false;
	if ( singleDocId2 && 
	     isDocIdInTermListCache ( singleDocId2 , coll ) ) 
		shouldBeCached2 = true;

	// if in the termlist cache, send it back right away
	char *trec;
	int32_t trecSize;
	if ( singleDocId2 &&
	     getRecFromTermListCache(coll,
				     startKey,
				     endKey,
				     maxCacheAge,
				     &trec,
				     &trecSize) ) {
		// if in cache send it back!
		us->sendReply_ass(trec,trecSize,trec,trecSize,slot);
		return;
	}

	// if should be cached but was not found then it's probably a
	// synonym form not in the doc content. make an empty list then.
	if ( shouldBeCached2 ) {
		// send back an empty termlist
		us->sendReply_ass(NULL,0,NULL,0,slot);
		return;
	}

	// MUST be in termlist cache! if not in there it is a probably
	// a synonym term termlist of a word in the doc.
	if ( isRecall ) {
		// send back an empty termlist
		us->sendReply_ass(NULL,0,NULL,0,slot);
		return;
	}
	
	// init waiting table?
	static bool s_waitInit = false;
	if ( ! s_waitInit ) {
		// do not repeat
		s_waitInit = true;
		// niceness = 0
		if ( ! g_waitingTable.set(8,4,2048,NULL,0,true,0,"m5wtbl")){
			log("msg5: failed to init waiting table");
			// error kills us!
			us->sendErrorReply ( slot , EBADRDBID ); 
			return;
		}
	}

	// wait in waiting table?
	if ( singleDocId2 && g_waitingTable.isInTable ( &singleDocId2 ) ) {
		g_waitingTable.addKey ( &singleDocId2 , &slot );
		return;
	}

	// if it's for a special gbdocid: query then cache ALL termlists
	// for this docid into g_termListCache right now
	if ( singleDocId2 ) {
		// have all further incoming requests for this docid
		// wait in the waiting table
		g_waitingTable.addKey ( &singleDocId2 , &slot );
		// load the title rec and store its posdb termlists in cache
		XmlDoc *xd;
		try { xd = new ( XmlDoc ); }
		catch ( ... ) {
			g_errno = ENOMEM;
			us->sendErrorReply ( slot , g_errno );
			return;
		}
		mnew ( xd, sizeof(XmlDoc),"msg0xd");
		// always use niceness 1 now even though we use niceness 0
		// to make the cache hits fast
		//niceness = 1;
		// . load the old title rec first and just recycle all
		// . typically there might be a few hundred related docids
		//   each with 50,000 matching queries on average to evaluate
		//   with the gbdocid:xxxx| restriction?
		if ( ! xd->set3 ( singleDocId2 , coll , niceness ) ) {
			us->sendErrorReply ( slot , g_errno ); return;}
		// init the new xmldoc
		xd->m_callback1 = callWaitingHandlers;
		xd->m_state     = xd;
		// . if this blocks then return
		// . should call loadOldTitleRec() and get JUST the posdb recs
		//   by setting m_useTitledb, etc. to false. then it should
		//   make posdb termlists with the compression using
		//   RdbList::addRecord() and add those lists to 
		//   g_termListCache
		if ( ! xd->cacheTermLists ( ) ) return;
		// otherwise, it completed right away!
		callWaitingHandlers ( xd );
		return;
	}
	*/

	/*
	// init special sectiondb cache?
	if ( rdbId == RDB_SECTIONDB && ! s_initCache ) {
		// try to init cache
		if ( ! s_sectiondbCache.init ( 20000000 , // 20MB max mem
					       -1       , // fixed data size
					       false    , // support lists?
					       20000    , // 20k max recs
					       false    , // use half keys?
					       "secdbche", // dbname
					       false, // load from disk?
					       sizeof(key128_t), //cachekeysize
					       0 , // data key size
					       20000 )) // numPtrs max
			log("msg0: failed to init sectiondb cache: %s",
			    mstrerror(g_errno));
		else
			s_initCache = true;
	}

	// check the sectiondb cache
	if ( rdbId == RDB_SECTIONDB ) {
		//int64_t sh48 = g_datedb.getTermId((key128_t *)startKey);
		// use the start key now!!!
		char *data;
		int32_t  dataSize;
		if (s_sectiondbCache.getRecord ( coll,
						 startKey,//&sh48,
						 &data,
						 &dataSize,
						 true, // docopy?
						 600, // maxage (10 mins)
						 true, // inc counts?
						 NULL, // cachedtime
						 true // promoteRec?
						 )){
			// debug
			//log("msg0: got sectiondblist in cache datasize=%"INT32"",
			//    dataSize);
			// send that back
			g_udpServer.sendReply_ass ( data            ,
						    dataSize        ,
						    data            ,
						    dataSize        ,
						    slot            ,
						    60              ,
						    NULL            ,
						    doneSending_ass ,
						    -1              ,
						    -1              ,
						    true            );
			return;
		}
	}
	*/

	// . do a local get
	// . create a msg5 to get the list
	State00 *st0 ;
	try { st0 = new (State00); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("Msg0: new(%"INT32"): %s", 
		    (int32_t)sizeof(State00),mstrerror(g_errno));
		us->sendErrorReply ( slot , g_errno ); 
		return; 
	}
	mnew ( st0 , sizeof(State00) , "State00" );
	// timing debug
	if ( g_conf.m_logTimingNet )
		st0->m_startTime = gettimeofdayInMilliseconds();
	// save slot in state
	st0->m_slot = slot;
	// save udp server to send back reply on
	st0->m_us = us;
	// init this one
	st0->m_niceness = niceness;
	st0->m_rdbId    = rdbId;

	QUICKPOLL(niceness);

	// debug msg
	if ( maxCacheAge != 0 && ! addToCache )
		log(LOG_LOGIC,"net: msg0: check but don't add... rdbid=%"INT32".",
		    (int32_t)rdbId);
	// . if this request came over on the high priority udp server
	//   make sure the priority gets passed along
	// . return if this blocks
	// . we'll call sendReply later
	if ( ! st0->m_msg5.getList ( rdbId             ,
				     collnum           ,
				     &st0->m_list      ,
				     startKey          ,
				     endKey            ,
				     minRecSizes       ,
				     includeTree       , // include tree?
				     addToCache        , // addToCache?
				     maxCacheAge       ,
				     startFileNum      , 
				     numFiles          ,
				     st0               ,
				     gotListWrapper    ,
				     niceness          ,
				     doErrorCorrection ,
				     NULL , // cacheKeyPtr
				     0    , // retryNum
				     2    , // maxRetries
				     true , // compensateForMerge
				     syncPoint ,
				     NULL,//&st0->m_msg5b ,
				     false,
				     allowPageCache ) )
		return;
	// call wrapper ouselves
	gotListWrapper ( st0 , NULL , NULL );
}

#include "Sections.h" // SectionVote

// . slot should be auto-nuked upon transmission or error
// . TODO: ensure if this sendReply() fails does it really nuke the slot?
void gotListWrapper ( void *state , RdbList *listb , Msg5 *msg5xx ) {
	// get the state
	State00 *st0 = (State00 *)state;
	// extract the udp slot and list and msg5
	UdpSlot   *slot =  st0->m_slot;
	RdbList   *list = &st0->m_list;
	Msg5      *msg5 = &st0->m_msg5;
	UdpServer *us   =  st0->m_us;
	// sanity check -- ensure they match
	//if ( niceness != st0->m_niceness )
	//	log("Msg0: niceness mismatch");
	// debug msg
	//if ( niceness != 0 ) 
	//	log("HEY! niceness is not 0");
	// timing debug
	if ( g_conf.m_logTimingNet || g_conf.m_logDebugNet ) {
		//log("Msg0:hndled request %"UINT64"",gettimeofdayInMilliseconds());
		int32_t size = -1;
		if ( list ) size     = list->getListSize();
		log(LOG_TIMING|LOG_DEBUG,
		    "net: msg0: Handled request for data. "
		    "Now sending data termId=%"UINT64" size=%"INT32""
		    " transId=%"INT32" ip=%s port=%i took=%"INT64" "
		    "(niceness=%"INT32").",
		    g_posdb.getTermId(msg5->m_startKey),
		    size,slot->m_transId,
		    iptoa(slot->m_ip),slot->m_port,
		    gettimeofdayInMilliseconds() - st0->m_startTime ,
		    st0->m_niceness );
	}
	// debug
	//if ( ! msg5->m_includeTree )
	//	log("hotit\n");
	// on error nuke the list and it's data
	if ( g_errno ) {
		mdelete ( st0 , sizeof(State00) , "Msg0" );
		delete (st0);
		// TODO: free "slot" if this send fails
		us->sendErrorReply ( slot , g_errno );
		return;
	}

	QUICKPOLL(st0->m_niceness);
	// point to the serialized list in "list"
	char *data      = list->getList();
	int32_t  dataSize  = list->getListSize();
	char *alloc     = list->getAlloc();
	int32_t  allocSize = list->getAllocSize();
	// tell list not to free the data since it is a reply so UdpServer
	// will free it when it destroys the slot
	list->setOwnData ( false );
	// keep track of stats
	Rdb *rdb = getRdbFromId ( st0->m_rdbId );
	if ( rdb ) rdb->sentReplyGet ( dataSize );
	// TODO: can we free any memory here???

	// keep track of how long it takes to complete the send
	st0->m_startTime = gettimeofdayInMilliseconds();
	// debug point
	int32_t oldSize = msg5->m_minRecSizes;
	int32_t newSize = msg5->m_minRecSizes + 20;
	// watch for wrap around
	if ( newSize < oldSize ) newSize = 0x7fffffff;
	if ( dataSize > newSize && list->getFixedDataSize() == 0 &&
	     // do not annoy me with these linkdb msgs
	     dataSize > newSize+100 ) 
		log(LOG_LOGIC,"net: msg0: Sending more data than what was "
		    "requested. Ineffcient. Bad engineer. dataSize=%"INT32" "
		    "minRecSizes=%"INT32".",dataSize,oldSize);
	/*
	// always compress these lists
	if ( st0->m_rdbId == RDB_SECTIONDB ) { // && 1 == 3) {

		// get sh48, the sitehash
		key128_t *startKey = (key128_t *)msg5->m_startKey ;
		int64_t sh48 = g_datedb.getTermId(startKey);

		// debug
		//log("msg0: got sectiondblist from disk listsize=%"INT32"",
		//    list->getListSize());

		if ( dataSize > 50000 )
			log("msg0: sending back list rdb=%"INT32" "
			    "listsize=%"INT32" sh48=0x%"XINT64"",
			    (int32_t)st0->m_rdbId,
			    dataSize,
			    sh48);

		// save it
		int32_t origDataSize = dataSize;
		// store compressed list on itself
		char *dst = list->m_list;
		// warn if niceness is 0!
		if ( st0->m_niceness == 0 )
			log("msg0: compressing sectiondb list at niceness 0!");
		// compress the list
		uint32_t lastVoteHash32 = 0LL;
		SectionVote *lastVote = NULL;
		for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
			// breathe
			QUICKPOLL ( st0->m_niceness );
			// get rec
			char *rec = list->getCurrentRec();
			// for ehre
			key128_t *key = (key128_t *)rec;
			// the score is the bit which is was set in 
			// Section::m_flags for that docid
			int32_t secType = g_indexdb.getScore ( (char *)key );
			// 0 means it probably used to count # of voters
			// from this site, so i don't think xmldoc uses
			// that any more
			if ( secType == SV_SITE_VOTER ) continue;
			// treat key like a datedb key and get the taghash
			uint32_t h32 = g_datedb.getDate ( key );
			// get data/vote from the current record in the 
			// sectiondb list
			SectionVote *sv=(SectionVote *)list->getCurrentData ();
			// get the average score for this doc
			float avg = sv->m_score ;
			if ( sv->m_numSampled > 0.0 ) avg /= sv->m_numSampled;
			// if same as last guy, add to it
			if ( lastVoteHash32 == h32 && lastVote ) {
				// turn possible multi-vote into single docid
				// into a single vote, with the score averaged.
				lastVote->m_score += avg;
				lastVote->m_numSampled++;
				continue;
			}
			// otherwise, add in a new guy!
			*(key128_t *)dst = *key;
			dst += sizeof(key128_t);
			// the new vote
			SectionVote *dsv = (SectionVote *)dst;
			dsv->m_score = avg;
			dsv->m_numSampled = 1;
			// set this
			lastVote = dsv;
			lastVoteHash32 = h32;
			// skip over
			dst += sizeof(SectionVote);
		}
		// update the list size now for sending back
		dataSize = dst - data;
		// if the list was over the requested minrecsizes we need
		// to set a flag so that the caller will do a re-call.
		// so making the entire odd, will be the flag.
	        if ( origDataSize > msg5->m_minRecSizes && 
		     dataSize < origDataSize ) {
			*dst++ = '\0';
			dataSize++;
		}

		// debug
		//log("msg0: compressed sectiondblist from disk "
		//    "newlistsize=%"INT32"", dataSize);
		
		// use this timestamp
		int32_t now = getTimeLocal();//Global();
		// finally, cache this sucker
		s_sectiondbCache.addRecord ( msg5->m_coll,
					     (char *)startKey,//(char *)&sh48
					     data, 
					     dataSize ,
					     now );
		// ignore errors
		g_errno = 0;
	}
	*/
		    
	//
	// for linkdb lists, remove all the keys that have the same IP32
	// and store a count of what we removed somewhere
	//
	if ( st0->m_rdbId == RDB_LINKDB ) {
		// store compressed list on itself
		char *dst = list->m_list;
		// keep stats
		int32_t totalOrigLinks = 0;
		int32_t ipDups = 0;
		int32_t lastIp32 = 0;
		char *listEnd = list->getListEnd();
		// compress the list
		for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
			// breathe
			QUICKPOLL ( st0->m_niceness );
			// count it
			totalOrigLinks++;
			// get rec
			char *rec = list->getCurrentRec();
			int32_t ip32 = g_linkdb.getLinkerIp_uk((key224_t *)rec );
			// same as one before?
			if ( ip32 == lastIp32 && 
			     // are we the last rec? include that for
			     // advancing the m_nextKey in Linkdb more 
			     // efficiently.
			     rec + LDBKS < listEnd ) {
				ipDups++;
				continue;
			}
			// store it
			gbmemcpy (dst , rec , LDBKS );
			dst += LDBKS;
			// update it
			lastIp32 = ip32;
		}
		// . if we removed one key, store the stats
		// . caller should recognize reply is not a multiple of
		//   the linkdb key size LDBKS and no its there!
		if ( ipDups ) {
			//*(int32_t *)dst = totalOrigLinks;
			//dst += 4;
			//*(int32_t *)dst = ipDups;
			//dst += 4;
		}
		// update list parms
		list->m_listSize = dst - list->m_list;
		list->m_listEnd  = list->m_list + list->m_listSize;
		data      = list->getList();
		dataSize  = list->getListSize();
	}


	//log("sending replySize=%"INT32" min=%"INT32"",dataSize,msg5->m_minRecSizes);
	// . TODO: dataSize may not equal list->getListMaxSize() so
	//         Mem class may show an imblanace
	// . now g_udpServer is responsible for freeing data/dataSize
	// . the "true" means to call doneSending_ass() from the signal handler
	//   if need be
	st0->m_us->sendReply_ass  ( data            ,
				    dataSize        ,
				    alloc           , // alloc
				    allocSize       , // alloc size
				    slot            ,
				    60              ,
				    st0             ,
				    doneSending_ass ,
				    -1              ,
				    -1              ,
				    true            );
}	

// . this may be called from a signal handler
// . we call from a signal handler to keep msg21 zippy
// . this may be called twice, onece from sig handler and next time not
//   from the sig handler
void doneSending_ass ( void *state , UdpSlot *slot ) {
	// point to our state
	State00 *st0 = (State00 *)state;
	// this is nULL if we hit the cache above
	if ( ! st0 ) return;
	// this might be inaccurate cuz sig handler can't call it!
	int64_t now = gettimeofdayInMilliseconds();
	// log the stats
	if ( g_conf.m_logTimingNet ) {
		double mbps ;
		mbps = (((double)slot->m_sendBufSize) * 8.0 / (1024.0*1024.0))/
			(((double)slot->m_startTime)/1000.0);
		log("net: msg0: Sent %"INT32" bytes of data in %"INT64" ms (%3.1fMbps) "
		      "(niceness=%"INT32").",
		      slot->m_sendBufSize , now - slot->m_startTime , mbps ,
		      st0->m_niceness );
	}
	// can't go any further if we're in a sig handler
	//if ( g_inSigHandler ) return;
	// . mark it in pinkish purple
	// . BUT, do not add stats here for tagdb, we get WAY too many lookups
	//   and it clutters the performance graph
	if ( st0->m_rdbId == RDB_TAGDB ) {
	}
	else if(slot->m_niceness > 0) {
		g_stats.addStat_r ( slot->m_sendBufSize , 
				    st0->m_startTime ,
				    now ,
				    //"transmit_data_nice",
				    0x00aa00aa);
	} 
	else {
		g_stats.addStat_r ( slot->m_sendBufSize , 
				    st0->m_startTime ,
				    now ,
				    //"transmit_data",
				    0x00ff00ff );
	}


	// release st0 now
	mdelete ( st0 , sizeof(State00) , "Msg0" );
	delete ( st0 );
}

/*
RdbCache g_termListCache;

RdbCache *getTermListCache ( ) {
	RdbCache *c = &g_termListCache;
	// init the cache
	static bool s_init = false;
	if ( s_init ) return c;
	// 100MB!
	int32_t maxMem = 100000000;
	int32_t maxNodes = maxMem / 25;
	if ( ! c->init( maxMem ,
			-1 , // fixed data size
			false, // support lists?
			maxNodes ,
			false , // use half keys?
			"termlist" , // dbname
			false, // load from disk?
			8 , // cache key size
			0 )) {// data key size {
		log("msg0: termlist cache init failed");
		return NULL;
	}
	s_init = true;
	// ignore errors
	g_errno = 0;
	return c;
}

// for posdb only!
int64_t getTermListCacheKey ( char *startKey , char *endKey ) {
	// make the cache key by hashing the startkey + endkey together
	int32_t ks = sizeof(POSDBKEY);
	int32_t conti = 0;
	int64_t ck64;
	ck64 = hash64_cont ( startKey , ks, 0LL, &conti );
	ck64 = hash64_cont ( endKey , ks , ck64, &conti );
	return ck64;
}

bool addRecToTermListCache ( char *coll,
			     char *startKey , 
			     char *endKey , 
			     char *list ,
			     int32_t  listSize ) {
	RdbCache *c = getTermListCache();
	if ( ! c ) return false;
	int64_t ck64 = getTermListCacheKey ( startKey , endKey );
	//int32_t recSize = list->getListSize();
	//char *rec = list->getList();
	return c->addRecord ( coll ,
			      (char *)&ck64 , 
			      list ,
			      listSize );
}


bool getListFromTermListCache ( char *coll,
				char *startKey,
				char *endKey,
				int32_t  maxCacheAge,
				RdbList *list ) {

	RdbCache *c = getTermListCache();
	if ( ! c ) return false;
	int64_t ck64 = getTermListCacheKey(startKey,endKey);
	int32_t recSize;
	char *rec;

	// return false if not found
	if ( ! c->getRecord ( coll ,
			      (char *)&ck64 , 
			      &rec ,
			      &recSize ,
			      true , // doCopy?
			      maxCacheAge ,
			      true ) ) // inc counts?
		return false;

	// set the list otherwise
	list->set ( rec ,
		    recSize ,
		    rec ,
		    recSize ,
		    startKey ,
		    endKey ,
		    0 , // posdb keys
		    true , // own data?
		    true , // use half keys? yes for posdb.
		    sizeof(POSDBKEY));

	// true means found
	return true;
}


bool getRecFromTermListCache ( char *coll,
			       char *startKey,
			       char *endKey,
			       int32_t  maxCacheAge,
			       char **rec ,
			       int32_t *recSize ) {

	RdbCache *c = getTermListCache();
	if ( ! c ) return false;
	int64_t ck64 = getTermListCacheKey(startKey,endKey);
	// return false if not found
	if ( ! c->getRecord ( coll ,
			      (char *)&ck64 , 
			      rec ,
			      recSize ,
			      true , // doCopy?
			      maxCacheAge ,
			      true ) ) // inc counts?
		return false;
	// true means found
	return true;
}
*/
/*
bool isDocIdInTermListCache ( int64_t docId , char *coll ) {
	RdbCache *c = getTermListCache();
	char *rec;
	int32_t  recSize;
	// return false if not found
	if ( ! c->getRecord ( coll ,
			      (char *)&docId, // docid is the key!
			      &rec ,
			      &recSize ,
			      false , // doCopy?
			      -1 , // maxCacheAge -- -1 means no limit
			      true ) ) // inc counts?
		return false;
	// true means found
	return true;
}

bool addDocIdToTermListCache ( int64_t docId , char *coll ) {
	RdbCache *c = getTermListCache();
	char data = 1;
	bool status = c->addRecord ( coll ,
				     (char *)&docId , 
				     &data,
				     1 );
	if ( ! status ) return false;
	// sanity!
	//if ( ! isDocIdInTermListCache ( docId , coll)){char *xx=NULL;*xx=0;}
	return true;
}
*/
