#include "gb-include.h"

#include "Msg17.h"
#include "Msg40.h"
#include "UdpServer.h"
//#include "zlib/zlib.h"
//#include "TitleRec.h"
#include "XmlDoc.h" // Z_BUF_ERROR

static void gotReplyWrapper17  ( void *state , UdpSlot *slot ) ;
static void handleRequest17    ( UdpSlot *slot , int32_t niceness ) ;
static void gotReplyWrapper17b ( void *state , UdpSlot *slot ) ;

// our caches
RdbCache g_genericCache[MAX_GENERIC_CACHES];

// for saving network
//RdbCache g_genericCacheSmallLocal[MAX_GENERIC_CACHES];

// . compress option array, compressed implies allocated uncompressed buffer
//   which the caller is responsible for.  Uncompressed data will be stored in
//   the m_buf.
char g_genericCacheCompress[MAX_GENERIC_CACHES] = { 1 };
//						    0 }; // seoresults

//static int32_t s_noMax = -1;
//static int32_t s_oneMonth = 86400*30;

int32_t *g_genericCacheMaxAge[MAX_GENERIC_CACHES] = {
	&g_conf.m_searchResultsMaxCacheAge 
	//&s_oneMonth // seoresultscache
	//&g_conf.m_siteLinkInfoMaxCacheAge ,
	// Msg50.cpp now has a dynamic max cache age which is higher
	// for higher qualities, since those take longer to recompute and
	// are usually much more stable...
	//&s_noMax // &g_conf.m_siteQualityMaxCacheAge
};

Msg17::Msg17() {
	m_cbuf = NULL;
	m_found = false;
}

Msg17::~Msg17() {
	reset();
}

void Msg17::reset() {
	if ( m_cbuf ) mfree ( m_cbuf , m_cbufSize , "Msg17" );
	m_cbuf = NULL;
	m_found = false;
}

bool Msg17::registerHandler ( ) {
	// . register ourselves with the high priority udp server
	// . it calls our callback when it receives a msg of type 0x17
	//if ( ! g_udpServer2.registerHandler ( 0x17, handleRequest17 )) 
	//	return false;
	if ( ! g_udpServer.registerHandler ( 0x17, handleRequest17 )) 
		return false;
	return true;
}

// . returns false if blocked, true otherwise
// . sets errno on error
bool Msg17::getFromCache ( char   cacheId,
			   key_t  key,
			   char **recPtr,
			   int32_t  *recSize,
			   //char  *coll ,
			   collnum_t collnum ,
			   void  *state ,
			   void (*callback) (void *state) ,
			   int32_t   niceness ,
			   int32_t   timeout ) {
	// assume not in cache
	m_found = false;
	// use a fake recSize if we should
	if ( ! recSize ) recSize = &m_tmpRecSize;
 	// save ptr to msg40 so we can deserialize it if we block
	//m_msg40 = msg40;
	// make the key based on the query and other input parms in msg40
	//SearchInput *si = msg40->m_si;
	//m_key = si->makeKey ( );
	m_cacheId = cacheId;
	m_key     = key;
	m_recPtr  = recPtr;
	m_recSize = recSize;
	// . only one machine will cache the query results
	// . if it goes down we are SOL
	//int32_t hostId = key.n0 % g_hostdb.getNumHosts(); 
	Host *host = getResponsibleHost ( key , &g_hostdb ) ;
	// if all in group are dead, we can't do it
	if ( ! host ) return true;

	//char *coll = si->m_coll;

	// . if we are the host responsible for caching this Msg40 check it
	//!g_conf.m_interfaceMachine ) {
	// look in our local cache
	RdbCache *c = NULL;
	if ( host->m_hostId == g_hostdb.m_hostId ) 
		c = &g_genericCache[(int)m_cacheId];
	// otherwise, try the small local cache to save network
	//else
	//	c = &g_genericCacheSmallLocal[m_cacheId];
	// is it in there?
	if ( c ) {
		time_t cachedTime;
		// return true if not found in our local cache
		if ( ! c->getRecord ( collnum   ,
				      m_key     ,
				      recPtr    ,
				      recSize   ,
				      false     ,  // do copy?
				      *g_genericCacheMaxAge[(int)cacheId],
				      true      ,  // keep stats
				      &cachedTime ))
			return true;
		// set the delta
		m_cachedTimeDelta = getTime() - cachedTime;
		// uncompress it if we should
		gotReply( NULL , *recPtr , *recSize , false );
		return true;
	}

	// if we were the man, we are done, it was not in the cache
	//if ( host->m_hostId == g_hostdb.m_hostId ) {
	//	// . set the msg40 from the list's data buf
	//	// . process it as a reply
	//	gotReply ( NULL , recPtr2 , recSize2 , false );
	//	// we did not block, so return true
	//	return true;
	//}

	// otherwise, we have to send a request over the network
	m_state    = state;
	m_callback = callback;
	m_niceness = niceness;
	// . skip if his ping is too high
	// . this is a sanity check now because we should never be returned
	//   the hostId of a dead host
	//if ( g_hostdb.isDead ( h ) ) return true;
	if ( g_hostdb.isDead ( host ) ) { char *xx = NULL; *xx = 0; }
	// make request
	char *p = m_request;
	*(key_t *)p = m_key; p += sizeof(key_t);
	// store the id
	*p++ = m_cacheId;
	// the flag (0 means read request, 1 means store request)
	*p++ = 0;
	gbmemcpy ( p , &collnum, sizeof(collnum_t)); p += sizeof(collnum_t);
	//strcpy ( p , coll ); p += gbstrlen ( coll ) + 1;
        // . send the request to the key host
	// . this returns false and sets g_errno on error
	// . now wait for 1 sec before timing out
	// . TODO: change timeout to 50ms instead of 1 second
        if ( ! g_udpServer.sendRequest ( m_request         ,
					 p - m_request     ,
					 0x17              , // msgType 0x17
					 host->m_ip        ,
					 host->m_port      ,
					 host->m_hostId    ,
					 NULL              ,
					 this              , // state data
					 gotReplyWrapper17 ,
					 timeout           , // timeout
					 -1                , // backoff
					 -1                , // maxWait
					 NULL              , // m_buf
					 0                 , // MSG17_BUF_SIZE
					 niceness        ) ) { // cback nice
		log("query: Had error sending request for cache cacheId=%i: "
		    "%s.",cacheId, mstrerror(g_errno));
		return true;
	}
	// otherwise we blocked
	return false;
}

void gotReplyWrapper17 ( void *state , UdpSlot *slot ) {
	Msg17 *THIS = (Msg17 *)state;
	// don't let udpserver free the request, it's our m_key
	slot->m_sendBufAlloc = NULL;
	// don't let UdpServer free the reply buffer, it is our m_buf
	//slot->m_readBuf = NULL;
	// gotReply() does not block, it may set g_errno
	//THIS->gotReply ( slot , THIS->m_buf , slot->m_readBufSize , true ) ;
	THIS->gotReply ( slot, slot->m_readBuf, slot->m_readBufSize, true ) ;
	// call callback since we blocked, since we're here
	THIS->m_callback ( THIS->m_state );
}

// . reply should hold our new docId
// . returns false on error and sets g_errno
bool Msg17::gotReply ( UdpSlot *slot , char *cbuf , int32_t cbufSize ,
		       bool includesCachedTime ) {
	// bitch about any error we got
	if ( g_errno ) return log("query: Reply for cache cacheId=%i "
				  "(niceness=%"INT32") had error: %s.",
				  m_cacheId,m_niceness,mstrerror(g_errno));
	// assume we were not able to get the cached Msg40
	m_found = false;
	// we were not found if reply was size 0
	if ( cbufSize <= 0 ) return false;
	// first 4 bytes is cached time
	if ( includesCachedTime ) {
		m_cachedTimeDelta = *(int32_t *)cbuf; 
		cbuf     += 4;
		cbufSize -= 4;
	}
	// to save network, try to cache this in the small local cache
	/*
	if ( slot ) {
		char *coll = m_request + sizeof(key_t) + 2;
		RdbCache *c = &g_genericCacheSmallLocal[m_cacheId];
		if ( c && ! c->addRecord ( coll     ,
					   k        ,
					   p        ,
					   pend - p ) )
			log("query: Had error storing cache cacheId=%"INT32": "
			    "%s.", cacheId, mstrerror(g_errno));
	}
	*/
	// set the buf size to its max for call to uncompress()
	//char ubuf [ 32*1024 ];
	if ( g_genericCacheCompress[(int)m_cacheId] ) {
		// the uncompressed size is always preceeds the compressed data
		int32_t recSize = *(int32_t *)cbuf;
		// sanity check
		if ( recSize < 0 ) {
			log("query: got bad cached rec size=%"INT32" cacheid=%"INT32"",
			    recSize,(int32_t)m_cacheId);
			return false;
		}
		cbuf     += 4;
		cbufSize -= 4;
		//int32_t  ubufMaxSize = 32*1024;
		//int32_t  ubufSize = ubufMaxSize;
		int32_t  ubufSize = recSize;
		char *ubuf = (char *)mmalloc ( ubufSize , "Msg17" );
		if ( ! ubuf ) 
			return log("query: Could not allocate %"INT32" bytes for "
				   "uncompressing cache cacheId=%i: "
				   "%s.",
				   ubufSize,m_cacheId,mstrerror(g_errno));
		// uncompress the reply
		int err = gbuncompress ( (unsigned char *)  ubuf     ,
					 (uint32_t *) &ubufSize ,
					 (unsigned char *)  cbuf     ,
					 (uint32_t  )  cbufSize );
		// hmmmm...
		if ( err == Z_BUF_ERROR ) {
			mfree ( ubuf , ubufSize , "Msg17");
			return log("query: Allocated buffer space was not "
				   "enough to hold uncompressed cache "
				   "cacheId=%i.", m_cacheId);
		}
		// set g_errno and return false on error
		if ( err != Z_OK ) {
			mfree ( ubuf , ubufSize , "Msg17");
			g_errno = EUNCOMPRESSERROR;
			return log("query: Got error in zlib when "
				   "uncompressing cache cacheId=%i: "
				   "ZG_ERRNO=%i", m_cacheId, err);
		}
		// sanity check
		if ( ubufSize != recSize ) { char *xx = NULL; *xx = 0; }
		if ( m_recPtr  ) *m_recPtr  = ubuf;
		if ( m_recSize ) *m_recSize = ubufSize;
	}
	// can be called after a local call to RdbCache::getRecord() in order
	// to just uncompress the data, so ignore this part
	else if ( slot ) {
		// . in case we haven't reset, free any buffer we've stolen
		//   before we overwrite it
		if ( m_cbuf ) mfree ( m_cbuf , m_cbufSize , "Msg17" );
		// . we need to free it though i guess, so remember it
		// oopsy, readBufSize is not the allocation size!
		//m_cbuf     = cbuf;
		//m_cbufSize = cbufSize;
		m_cbuf = slot->m_readBuf;
		m_cbufSize = slot->m_readBufMaxSize;
		if ( m_recPtr  ) *m_recPtr  = cbuf;
		if ( m_recSize ) *m_recSize = cbufSize;
		// do not free the buf, we will steal it at this point
		slot->m_readBuf = NULL;
	}
	// we got it
	m_found = true;
	// return true on success
	return true;
}

// . only return false if you want slot to be nuked w/o replying
// . MUST always call g_udpServer::sendReply() or sendErrorReply()
void handleRequest17 ( UdpSlot *slot , int32_t niceness  ) {
	// get the request, should be a full url
	char *request     = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;

	UdpServer            *us = &g_udpServer;
	//if ( niceness == 0 )  us = &g_udpServer2;

	// need at least a key in the request
	if ( requestSize < (int32_t)sizeof(key_t) ) {
		log("query: Request size for cache (%"INT32") "
		    "is too small for some reason.", (int32_t)sizeof(key_t));
		us->sendErrorReply ( slot , EBADREQUESTSIZE );
		return;
	}

	char *p    = request;
	char *pend = request + requestSize;
	// get the key
	key_t k = *(key_t *)p; p += sizeof(key_t);
	// id
	char cacheId = *p++;
	// then 1-byte flag (0 means read request, 1 means store request)
	char flag = *p++;
	// NULL terminated collection name follows
	//char *coll = p; p += gbstrlen ( coll ) + 1 ;
	collnum_t collnum = *(collnum_t *)p; p += sizeof(collnum_t);

	RdbCache *c = &g_genericCache[(int)cacheId];

	// if flag is 1 then it is a request to store a compressed Msg40
	if ( flag == 1 ) {
		if ( ! c->addRecord ( collnum ,
				      k, 
				      p, 
				      pend - p ) )
			log("query: Had error storing cache cacheId=%i: "
			    "%s.", cacheId, mstrerror(g_errno));
		// send an empty reply
		us->sendReply_ass ( NULL , 0 , NULL , 0 , slot );
		return;
	}

	char  *rec;
	int32_t   recSize;
	time_t cachedTime;
	// send back nothing if not in cache
	if ( ! c->getRecord ( collnum  ,
			      k        ,
			      &rec     ,
			      &recSize ,
			      false    ,  // do copy?
			      *g_genericCacheMaxAge[(int)cacheId],
			      true     ,// keep stats
			      &cachedTime)) {
		us->sendReply_ass ( NULL , 0 , NULL , 0 , slot );
		return;
	}
	// alloc a buf to hold reply and 4 bytes for cachedTime
	int32_t  bufSize = 4 + recSize;
	char *buf = (char *)mmalloc ( bufSize , "Msg17");
	if ( ! buf ) {
		us->sendReply_ass ( NULL , 0 , NULL , 0 , slot );
		return;
	}

	// make the cached time into a delta because all hosts in the 
	// cluster may not be synced
	time_t cachedTimeDelta = getTime() - cachedTime;

	char *x = buf;
	*(int32_t *)x = cachedTimeDelta; x += 4;
	gbmemcpy ( x , rec , recSize );

	// . set the msg40 from the cached record
	// . UdpServer should free "rec" when he's done sending it
	us->sendReply_ass ( buf     , 
			    bufSize ,
			    buf     , // alloc
			    bufSize , // allocSize
			    slot    ,
			    2       ); // timeout in 2 secs
}

static int32_t s_numInProgress = 0;

// . if you had to make your own Msg40 class because it wasn't cached
//   then you should store it in the cache here
// . returns false if blocked, true otherwise
// . MAY set g_errno on error
//bool Msg17::storeInCache ( Msg40 *msg40 ) {
bool Msg17::storeInCache ( char   cacheId ,
			   key_t  key ,
			   char  *recPtr ,
			   int32_t   recSize ,
			   collnum_t collnum, // char  *coll ,
			   int32_t   niceness ,
			   int32_t   timeout  ) {

	// only allow 200 launched in progress stores at a time to
	// save UdpSlots
	if ( s_numInProgress >= 200 ) {
		log("query: Unable to launch Msg17 request, already have 200 "
		    "in progress. May affect performance.");
		return true;
	}
//	// how much room?
//	int32_t tmpSize = msg40->getStoredSize();
//	// store in this buffer
//	char tmp [ 32 * 1024 ];
//	// bail if too much
//	if ( tmpSize > 32*1024 ) {
//		log(LOG_LIMIT,
//		    "query: Size of cached search results page (and all "
//		    "associated data) is %"INT32" bytes. Max is %"INT32". "
//		    "Page not cached.",tmpSize,32*1024);
//		return true; 
//	}
//	// serialize into tmp
//	int32_t nb = msg40->serialize ( tmp , tmpSize );
//	// it must fit exactly
//	if ( nb != tmpSize || nb == 0 ) {
//		log(LOG_LOGIC,
//		    "query: Size of cached search results page (%"INT32") does not "
//		    "match what it should be. (%"INT32")" , nb , tmpSize );
//		return true;
//	}
//	// make key
//	SearchInput *si = msg40->m_si;
//	key_t k = si->makeKey ( );
	m_key = key;
	m_cacheId = cacheId;
	// get this host responsible for holding this
	//int32_t hostId = key.n0 % g_hostdb.getNumHosts(); 
	Host *host = getResponsibleHost ( key , &g_hostdb ) ;
	// if all in the group are dead, can't do it
	if ( ! host ) return true;
	// . skip if his ping is too high
	// . NO, because if we are caching something that is repeated
	//   a lot, and has a high computation value, like the root
	//   quality computed in Msg50.cpp, we can really hurt performance
	//   so, let's cache it here now if its twin(s) are dead!!

	//if ( g_hostdb.isDead ( h ) && hostId!=g_hostdb.m_hostId) return true;
	// make a buffer to hold the request
	char buf [ 200000 ]; // MSG17_BUF_SIZE;

	char *p    = buf;
	char *pend = buf + 200000; // MSG17_BUF_SIZE;
	*(key_t *)p = key ; p += sizeof(key_t);
	// id
	*p++ = m_cacheId;
	// use "1" for a store request
	*p++ = 1;
	//char *coll = si->m_coll;
	//strcpy ( p , coll ); p += gbstrlen(coll) + 1; // includes '\0'
	gbmemcpy ( p ,&collnum ,sizeof(collnum_t)); p += sizeof(collnum_t);

	QUICKPOLL(niceness);

	// now start the rec that will go into the cache
	char *cacheRec = p;

	// debug point
	//if ( key.n0 == 0x6ff1ee0116d9cfebLL )
	//	log("hey");

	if ( g_genericCacheCompress[(int)m_cacheId] ) {
		// uncompressed size
		*(int32_t *)p = recSize; p += 4;
		// sanity check
		if ( recSize < 0 ) { char *xx = NULL; *xx = 0; }
		// how much left over
		int32_t avail = pend - p;
		// save it
		int32_t saved = avail;
		//int32_t clen = gbstrlen(coll);
		// compress "tmp" into m_buf, but leave leading bytes
		// for the key
		int err = gbcompress ( (unsigned char *)p ,
				       (uint32_t *)&avail  ,
				       (unsigned char *)recPtr  ,
				       (uint32_t  )recSize );
		// advance p by how many bytes we stored into "p"
		p += avail;
		// check for error
		if ( err != Z_OK ) { 
			g_errno = ECOMPRESSFAILED; 
			log("query: Compression of cache cacheId=%i "
			    "failed err=%"INT32" avail=%"INT32" collnum=%"INT32" "
			    "recSize=%"INT32".", 
			    cacheId , (int32_t)err ,
			    saved , (int32_t)collnum , recSize );
			return true;
		}
	}
	else {
		// bail if not enough room!
		if ( recSize > pend - p ) return true;
		// otheriwse, store it
		gbmemcpy ( p, recPtr, recSize );
		// advance p by how many bytes we stored into "p"
		p += recSize;
	}
	// . size of whole request, key and the serialized/compressed Msg40
	// . "size" is set by call to ::compress() above
	int32_t requestSize = p - buf; // p - buf + size 

	// size of the part of the request that goes into the cache
	int32_t cacheRecSize = p - cacheRec ;

	// store the key
	*(key_t *) buf = key;
	// if we are that host, store it ourselves right now
	if ( host->m_hostId == g_hostdb.m_hostId ) {
		RdbCache *c = &g_genericCache[(int)m_cacheId];
		if ( ! c->addRecord ( collnum ,
				      key  ,
				      cacheRec     ,
				      cacheRecSize ) )
			log("query: Failed to add compressed search results "
			    "page to cache cacheId=%i: %s.",
			    cacheId, mstrerror(g_errno));
		return true;
	}
	// make a request to hold it
	char *request = (char *) mdup ( buf , requestSize , "Msg17" );
	if ( ! request ) {
		log("query: Failed to allocate %i bytes to hold cache "
		    "cacheId=%"INT32" for transmission to caching host.",
		    cacheId, requestSize);
		return true;
	}
	QUICKPOLL(niceness);

	// . send it as a request to the appropriate machine
	// . this returns false and sets g_errno on error
	// . returns true if it blocks
        if ( !  g_udpServer.sendRequest ( request        , // request
					  requestSize    , // request size
					  0x17           , // msgType 0x17
					  host->m_ip     ,
					  host->m_port   , // low priority
					  host->m_hostId ,
					  NULL           ,
					  this           , // state data
					  gotReplyWrapper17b ,
					  timeout        ,// timeout in secs
					  -1             ,
					  -1             ,
					  NULL           ,
					  0              ,
					  niceness       )){// cback nice
		log("query: Had error sending request to cache cacheid=%i: "
		    " %s.",cacheId,mstrerror(g_errno));
		mfree ( request , requestSize , "Msg17" );
		return true;
	}
	// count as in progress
	s_numInProgress++;
	// we blocked
	return false;
}

// got reply from a store request
void gotReplyWrapper17b ( void *state , UdpSlot *slot ) {
	// . don't let udp server free m_buf, we own it
	// . not anymore, we don't want caller to delay just to store this!
	//slot->m_sendBufAlloc = NULL;
	// dec the count
	s_numInProgress--;
}

// . Dns.cpp uses key.n1 but we keep using key.n0 here so we can re-use old
//   saved caches
Host *Msg17::getResponsibleHost ( key_t key , Hostdb * hostdb ) {
	// get the hostNum that should handle this
	int32_t hostId = key.n0 % hostdb->getNumHosts();
	// return it if it is alive
	if ( ! hostdb->isDead ( hostId ) ) return hostdb->getHost ( hostId );
	// how many hosts are up?
	int32_t numAlive = hostdb->getNumHostsAlive();
	// if all dead return NULL
	if ( numAlive == 0 ) return NULL;
	// try another hostNum
	int32_t hostNum = key.n0 % numAlive;
	// otherwise, chain to him
	int32_t count = 0;
	for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		// get the ith host
		Host *host = &hostdb->m_hosts[i];
		// skip him if he is dead
		if ( hostdb->isDead ( host ) ) continue;
		// count it if alive, continue if not our number
		if ( count++ != hostNum ) continue;
		// we got a match, we cannot use hostNum as the hostId now
		// because the host with that hostId might be dead
		return host;
	}
	// Msg17 does not need to set this
	//g_errno = EDEADHOST;
	return NULL;
}
