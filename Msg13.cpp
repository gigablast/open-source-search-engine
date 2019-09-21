#include "gb-include.h"

#include "Msg13.h"
#include "UdpServer.h"
#include "HttpServer.h"
#include "Stats.h"
#include "HashTableX.h"
#include "XmlDoc.h"
#include "Test.h"
#include "Speller.h"
#include "SpiderProxy.h" // OP_GETPROXY OP_RETPROXY
#include "zlib.h"

char *g_fakeReply = 
	"HTTP/1.0 200 (OK)\r\n"
	"Content-Length: 0\r\n"
	"Connection: Close\r\n"
	"Content-Type: text/html\r\n\r\n\0";

int32_t convertIntoLinks ( char *reply , int32_t replySize ) ;
int32_t filterRobotsTxt ( char *reply , int32_t replySize , HttpMime *mime ,
		       int32_t niceness , char *userAgent , int32_t uaLen ) ;
bool getIframeExpandedContent ( Msg13Request *r , TcpSocket *ts );
void gotIframeExpandedContent ( void *state ) ;

bool addToHammerQueue ( Msg13Request *r ) ;
void scanHammerQueue ( int fd , void *state );
void downloadTheDocForReals ( Msg13Request *r ) ;
char *getRandUserAgent ( int32_t urlIp , int32_t proxyIp , int32_t proxyPort ) ;

// utility functions
bool getTestSpideredDate ( Url *u , int32_t *origSpiderDate , char *testDir ) ;
bool addTestSpideredDate ( Url *u , int32_t  spideredTime   , char *testDir ) ;
bool getTestDoc ( char *u , class TcpSocket *ts , Msg13Request *r );
bool addTestDoc ( int64_t urlHash64 , char *httpReply , int32_t httpReplySize ,
		  int32_t err , Msg13Request *r ) ;

static void gotForwardedReplyWrapper ( void *state , UdpSlot *slot ) ;
static void handleRequest13 ( UdpSlot *slot , int32_t niceness ) ;
//static bool downloadDoc     ( UdpSlot *slot, Msg13Request* r ) ;
static void gotHttpReply    ( void *state , TcpSocket *ts ) ;
static void gotHttpReply2 ( void *state , 
			    char *reply , 
			    int32_t  replySize ,
			    int32_t  replyAllocSize ,
			    TcpSocket *ts ) ;
static void passOnReply     ( void *state , UdpSlot *slot ) ;

bool hasIframe           ( char *reply, int32_t replySize , int32_t niceness );
int32_t hasGoodDates        ( char *content, 
			   int32_t contentLen, 
			   Xml *xml, 
			   Words *words,
			   char ctype,
			   int32_t niceness );
char getContentTypeQuick ( HttpMime *mime, char *reply, int32_t replySize , 
			   int32_t niceness ) ;
int32_t convertIntoLinks    ( char *reply, int32_t replySize , Xml *xml , 
			   int32_t niceness ) ;


static bool setProxiedUrlFromSquidProxiedRequest ( Msg13Request *r );
static void stripProxyAuthorization ( char *squidProxiedReqBuf ) ;
static bool addNewProxyAuthorization ( SafeBuf *req , Msg13Request *r );
static void fixGETorPOST ( char *squidProxiedReqBuf ) ;
static int64_t computeProxiedCacheKey64 ( Msg13Request *r ) ;

// cache for robots.txt pages
static RdbCache s_httpCacheRobots;
// cache for other pages
static RdbCache s_httpCacheOthers;
// queue up identical requests
static HashTableX s_rt;

void resetMsg13Caches ( ) {
	s_httpCacheRobots.reset();
	s_httpCacheOthers.reset();
	s_rt.reset();
}

RdbCache *Msg13::getHttpCacheRobots() { return &s_httpCacheRobots; }
RdbCache *Msg13::getHttpCacheOthers() { return &s_httpCacheOthers; }

Msg13::Msg13() {
	m_replyBuf = NULL;
}

Msg13::~Msg13() {
	reset();
}

void Msg13::reset() {
	if (m_replyBuf) mfree(m_replyBuf,m_replyBufAllocSize,"msg13rb");
	m_replyBuf = NULL;
}


bool Msg13::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x0A
	if ( ! g_udpServer.registerHandler ( 0x13, handleRequest13 )) 
		return false;

	// use 3MB per cache
	int32_t memRobots = 3000000;
	// make 10MB now that we have proxied url (i.e. squid) capabilities
	int32_t memOthers = 10000000;
	// assume 15k avg cache file
	int32_t maxCacheNodesRobots = memRobots / 106;
	int32_t maxCacheNodesOthers = memOthers / (10*1024);

	if ( ! s_httpCacheRobots.init ( memRobots ,
					-1        , // fixedDataSize
					false     , // lists o recs?
					maxCacheNodesRobots ,
					false     , // use half keys
					"robots.txt"  , // dbname
					true      ))// save to disk
		return false;

	if ( ! s_httpCacheOthers.init ( memOthers ,
					-1        , // fixedDataSize
					false     , // lists o recs?
					maxCacheNodesOthers ,
					false     , // use half keys
					"htmlPages"  , // dbname
					true      ))// save to disk
		return false;

	// . set up the request table (aka wait in line table)
	// . allowDups = "true"
	if ( ! s_rt.set ( 8 ,sizeof(UdpSlot *),0,NULL,0,true,0,"wait13tbl") )
		return false;

	if ( ! g_loop.registerSleepCallback(10,NULL,scanHammerQueue) )
		return log("build: Failed to register timer callback for "
			   "hammer queue.");


	// success
	return true;
}

// . returns false if blocked, returns true otherwise
// . returns true and sets g_errno on error
bool Msg13::getDoc ( Msg13Request *r,
		     bool isTestColl , 
		     void *state,void(*callback)(void *state)){

	// reset in case we are being reused
	reset();
	
	/*
	char buf[1024];
	char *s = "<td class=\"smallfont\" align=\"right\">November 14th, 2011 10:06 AM</td>\r\n\t\t";
	strcpy(buf,s);
	Xml xml;
	int32_t status = hasGoodDates ( buf ,
				     gbstrlen(buf),
				     &xml , 
				     CT_HTML,
				     0 );
	*/

	// set these even though we are not doing events, so we can use
	// the event spider proxies on scproxy3
	r->m_requireGoodDate = 0;
	r->m_harvestLinksIfNoGoodDate = 1;

	m_state    = state;
	m_callback = callback;

	m_request = r;
	// sanity check
	if ( r->m_urlIp ==  0 ) { char *xx = NULL; *xx = 0; }
	if ( r->m_urlIp == -1 ) { char *xx = NULL; *xx = 0; }

	// set this
	//r->m_urlLen    = gbstrlen ( r->ptr_url );
	r->m_urlHash64 = hash64 ( r->ptr_url , r->size_url-1);//m_urlLen );

	// no! i don't want to store images in there
	//if ( isTestColl )
	//	r->m_useTestCache = true;

	// default to 'qa' test coll if non given
	if ( r->m_useTestCache &&
	     ! r->m_testDir[0] ) {
		r->m_testDir[0] = 'q';
		r->m_testDir[1] = 'a';
	}

	// sanity check, if spidering the test coll make sure one of 
	// these is true!! this prevents us from mistakenly turning it off
	// and not using the doc cache on disk like we should
	if ( isTestColl &&
	     ! r->m_testDir[0] &&
	     //! g_conf.m_testSpiderEnabled &&
	     //! g_conf.m_testParserEnabled &&
	     //! r->m_isPageParser &&
	     r->m_useTestCache ) {
		char *xx=NULL;*xx=0; }

	//r->m_testSpiderEnabled = (bool)g_conf.m_testSpiderEnabled;
	//r->m_testParserEnabled = (bool)g_conf.m_testParserEnabled;
	// but default to parser dir if we are the test coll so that
	// the [analyze] link works!
	//if ( isTestColl && ! r->m_testSpiderEnabled )
	//	r->m_testParserEnabled = true;

	// is this a /robots.txt url?
	if ( r->size_url - 1 > 12 && 
	     ! strncmp ( r->ptr_url + r->size_url -1 -11,"/robots.txt",11))
		r->m_isRobotsTxt = true;

	// force caching if getting robots.txt so is compressed in cache
	if ( r->m_isRobotsTxt )
		r->m_compressReply = true;

	// do not get .google.com/ crap
	//if ( strstr(r->ptr_url,".google.com/") ) { char *xx=NULL;*xx=0; }

	// set it for this too
	//if ( g_conf.m_useCompressionProxy ) {
	//	r->m_useCompressionProxy = true;
	//	r->m_compressReply       = true;
	//}

	// make the cache key
	r->m_cacheKey  = r->m_urlHash64;
	// a compressed reply is different than a non-compressed reply
	if ( r->m_compressReply ) r->m_cacheKey ^= 0xff;



	if ( r->m_isSquidProxiedUrl )
		// sets r->m_proxiedUrl that we use a few times below
		setProxiedUrlFromSquidProxiedRequest ( r );

	// . if gigablast is acting like a squid proxy, then r->ptr_url
	//   is a COMPLETE http mime request, so hash the following fields in 
	//   the http mime request to make the cache key
	//   * url
	//   * cookie
	// . this is r->m_proxiedUrl which we set above
	if ( r->m_isSquidProxiedUrl )
		r->m_cacheKey = computeProxiedCacheKey64 ( r );


	// always forward these so we can use the robots.txt cache
	if ( r->m_isRobotsTxt ) r->m_forwardDownloadRequest = true;

	// always forward for now until things work better!
	r->m_forwardDownloadRequest = true;	

	// assume no http proxy ip/port
	r->m_proxyIp = 0;
	r->m_proxyPort = 0;

	// download it ourselves rather than forward it off to another host?
	//if ( r->m_forwardDownloadRequest ) return forwardRequest ( ); 

	return forwardRequest ( ); 

	// gotHttpReply() and passOnReply() call our Msg13::gotDocReply*() 
	// functions if Msg13Request::m_parent is non-NULL
	//r->m_parent = this;

	// . returns false if blocked, etc.
	// . if this doesn't block it calls getFinalReply()
	//return downloadDoc ( NULL , r ) ;
}

bool Msg13::forwardRequest ( ) {

	// int16_tcut
	Msg13Request *r = m_request;

	//
	// forward this request to the host responsible for this url's ip
	//
	int32_t nh     = g_hostdb.m_numHosts;
	int32_t hostId = hash32h(((uint32_t)r->m_firstIp >> 8), 0) % nh;

	if((uint32_t)r->m_firstIp >> 8 == 0) {
		// If the first IP is not set for the request then we don't
		// want to hammer the first host with spidering enabled.
		hostId = hash32n ( r->ptr_url ) % nh;
	}

	// avoid host #0 for diffbot hack which is dropping some requests
	// because of the streaming bug methinks
	if ( hostId == 0 && nh >= 2 && g_conf.m_diffbotMsg13Hack ) 
		hostId = 1;

	// get host to send to from hostId
	Host *h = NULL;
	// . pick first alive host, starting with "hostId" as the hostId
	// . if all dead, send to the original and we will timeout > 200 secs
	for ( int32_t count = 0 ; count <= nh ; count++ ) {
		// get that host
		//h = g_hostdb.getProxy ( hostId );;
		h = g_hostdb.getHost ( hostId );

		// Get the other one in shard instead of getting the first
		// one we find sequentially because that makes the load
		// imbalanced to the lowest host with spidering enabled.
		if(!h->m_spiderEnabled) {
			h = g_hostdb.getHost(g_hostdb.getHostIdWithSpideringEnabled(
			  h->m_hostId));
		}

		// stop if he is alive and able to spider
		if ( h->m_spiderEnabled && ! g_hostdb.isDead ( h ) ) break;
		// get the next otherwise
		if ( ++hostId >= nh ) hostId = 0;
	}


	hostId = 0; // HACK!!

	// forward it to self if we are the spider proxy!!!
	if ( g_hostdb.m_myHost->m_isProxy )
		h = g_hostdb.m_myHost;

	// log it
	if ( g_conf.m_logDebugSpider )
		logf ( LOG_DEBUG, 
		       "spider: sending download request of %s firstIp=%s "
		       "uh48=%"UINT64" to "
		       "host %"INT32" (child=%"INT32")", r->ptr_url, iptoa(r->m_firstIp), 
		       r->m_urlHash48, hostId,
		       r->m_skipHammerCheck);

	// sanity
	if ( r->m_useTestCache && ! r->m_testDir[0] ) { char *xx=NULL;*xx=0;}

	// fill up the request
	int32_t requestBufSize = r->getSize();

	// we have to serialize it now because it has cookies as well as
	// the url.
	char *requestBuf = serializeMsg ( sizeof(Msg13Request),
					  &r->size_url,
					  &r->size_cookie,
					  &r->ptr_url,
					  r,
					  &requestBufSize ,
					  NULL , 
					  0,//RBUF_SIZE , 
					  false );
	// g_errno should be set in this case, most likely to ENOMEM
	if ( ! requestBuf ) return true;

	// . otherwise, send the request to the key host
	// . returns false and sets g_errno on error
	// . now wait for 2 minutes before timing out
	if ( ! g_udpServer.sendRequest ( requestBuf, // (char *)r    ,
					 requestBufSize  , 
					 0x13         , // msgType 0x13
					 h->m_ip      ,
					 h->m_port    ,
					 // it was not using the proxy! because
					 // it thinks the hostid #0 is not
					 // the proxy... b/c ninad screwed that
					 // up by giving proxies the same ids
					 // as regular hosts!
					 -1 , // h->m_hostId  ,
					 NULL         ,
					 this         , // state data
					 gotForwardedReplyWrapper  ,
					 200   )){// 200 sec timeout
		// sanity check
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// report it
		log("spider: msg13 request: %s",mstrerror(g_errno));
		// g_errno must be set!
		return true;
	}
	// otherwise we block
	return false;
}

void gotForwardedReplyWrapper ( void *state , UdpSlot *slot ) {
	// int16_tcut
	Msg13 *THIS = (Msg13 *)state;
	// return if this blocked
	if ( ! THIS->gotForwardedReply ( slot ) ) return;
	// callback
	THIS->m_callback ( THIS->m_state );
}

bool Msg13::gotForwardedReply ( UdpSlot *slot ) {
	// don't let udpserver free the request, it's our m_request[]
	// no, now let him free it because it was serialized into there
	//slot->m_sendBufAlloc = NULL;
	// what did he give us?
	char *reply          = slot->m_readBuf;
	int32_t  replySize      = slot->m_readBufSize;
	int32_t  replyAllocSize = slot->m_readBufMaxSize;
	// UdpServer::makeReadBuf() sets m_readBuf to -1 when calling
	// alloc() with a zero length, so fix that
	if ( replySize == 0 ) reply = NULL;
	// this is messed up. why is it happening?
	if ( reply == (void *)-1 ) { char *xx=NULL;*xx=0; }

	// we are responsible for freeing reply now
	if ( ! g_errno ) slot->m_readBuf = NULL;

	return gotFinalReply ( reply , replySize , replyAllocSize );
}

#include "PageInject.h"

bool Msg13::gotFinalReply ( char *reply, int32_t replySize, int32_t replyAllocSize ){

	// how is this happening? ah from image downloads...
	if ( m_replyBuf ) { char *xx=NULL;*xx=0; }
		
	// assume none
	m_replyBuf     = NULL;
	m_replyBufSize = 0;

	// int16_tcut
	Msg13Request *r = m_request;

	//log("msg13: reply=%"XINT32" replysize=%"INT32" g_errno=%s",
	//    (int32_t)reply,(int32_t)replySize,mstrerror(g_errno));

	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads )
		logf(LOG_DEBUG,"spider: FINALIZED %s firstIp=%s",
		     r->ptr_url,iptoa(r->m_firstIp));


	// . if timed out probably the host is now dead so try another one!
	// . return if that blocked
	if ( g_errno == EUDPTIMEDOUT ) {
		// try again
		log("spider: retrying1. had error for %s : %s",
		    r->ptr_url,mstrerror(g_errno));
		// return if that blocked
		if ( ! forwardRequest ( ) ) return false;
		// a different g_errno should be set now!
	}

	if ( g_errno ) {
		// this error msg is repeated in XmlDoc::logIt() so no need
		// for it here
		if ( g_conf.m_logDebugSpider )
			log("spider: error for %s: %s",
			    r->ptr_url,mstrerror(g_errno));
		return true;
	}

	// set it
	m_replyBuf          = reply;
	m_replyBufSize      = replySize;
	m_replyBufAllocSize = replyAllocSize;

	// sanity check
	if ( replySize > 0 && ! reply ) { char *xx=NULL;*xx=0; }

	// no uncompressing if reply is empty
	if ( replySize == 0 ) return true;

	// if it was not compressed we are done! no need to uncompress it
	if ( ! r->m_compressReply ) return true;

	// get uncompressed size
	uint32_t unzippedLen = *(int32_t*)reply;
	// sanity checks
	if ( unzippedLen > 10000000 ) {
		log("spider: downloaded probable corrupt gzipped doc "
		    "with unzipped len of %"INT32"",(int32_t)unzippedLen);
		g_errno = ECORRUPTDATA;
		return true;
	}
	// make buffer to hold uncompressed data
	char *newBuf = (char*)mmalloc(unzippedLen, "Msg13Unzip");
	if( ! newBuf ) {
		g_errno = ENOMEM;
		return true;
	}
	// make another var to get mangled by gbuncompress
	uint32_t uncompressedLen = unzippedLen;
	// uncompress it
	int zipErr = gbuncompress( (unsigned char*)newBuf  ,  // dst
				   &uncompressedLen        ,  // dstLen
				   (unsigned char*)reply+4 ,  // src
				   replySize - 4           ); // srcLen
	if(zipErr != Z_OK || 
	   uncompressedLen!=(uint32_t)unzippedLen) {
		log("spider: had error unzipping Msg13 reply. unzipped "
		    "len should be %"INT32" but is %"INT32". ziperr=%"INT32"",
		    (int32_t)uncompressedLen,
		    (int32_t)unzippedLen,
		    (int32_t)zipErr);
		mfree (newBuf, unzippedLen, "Msg13UnzipError");
		g_errno = ECORRUPTDATA;//EBADREPLYSIZE;
		return true;
	}
	// all http replies should end in a \0. otherwise its likely
	// a compression error. i think i saw this on roadrunner core
	// a machine once in XmlDoc.cpp because httpReply did not end in \0
	//if ( uncompressedLen>0 && newBuf[uncompressedLen-1] ) {
	//	log("spider: had http reply with no NULL term");
	//	mfree(newBuf,unzippedLen,"Msg13Null");
	//	g_errno = EBADREPLYSIZE;
	//	return true;
	//}

	// count it for stats
	g_stats.m_compressedBytesIn += replySize;

	// free compressed
	mfree ( reply , replyAllocSize ,"ufree" );

	// assign uncompressed
	m_replyBuf          = newBuf;
	m_replyBufSize      = uncompressedLen;
	m_replyBufAllocSize = unzippedLen;

	// log it for now
	if ( g_conf.m_logDebugSpider )
		log("http: got doc %s %"INT32" to %"INT32"",
		    r->ptr_url,(int32_t)replySize,(int32_t)uncompressedLen);

	return true;
}

bool isIpInTwitchyTable ( CollectionRec *cr , int32_t ip ) {
	if ( ! cr ) return false;
	HashTableX *ht = &cr->m_twitchyTable;
	if ( ht->m_numSlots == 0 ) return false;
	return ( ht->getSlot ( &ip ) >= 0 );
}

bool addIpToTwitchyTable ( CollectionRec *cr , int32_t ip ) {
	if ( ! cr ) return true;
	HashTableX *ht = &cr->m_twitchyTable;
	if ( ht->m_numSlots == 0 )
		ht->set ( 4,0,16,NULL,0,false,MAX_NICENESS,"twitchtbl",true);
	return ht->addKey ( &ip );
}

RdbCache s_hammerCache;
static bool s_flag = false;
Msg13Request *s_hammerQueueHead = NULL;
Msg13Request *s_hammerQueueTail = NULL;

// . only return false if you want slot to be nuked w/o replying
// . MUST always call g_udpServer::sendReply() or sendErrorReply()
void handleRequest13 ( UdpSlot *slot , int32_t niceness  ) {

 	// cast it
	Msg13Request *r = (Msg13Request *)slot->m_readBuf;
	// use slot niceness
	r->m_niceness = niceness;

	// deserialize it now
	deserializeMsg ( sizeof(Msg13),
			 &r->size_url,
			 &r->size_cookie,
			 &r->ptr_url,
			 r->m_buf );

	// . sanity - otherwise xmldoc::set cores!
	// . no! sometimes the niceness gets converted!
	//if ( niceness == 0 ) { char *xx=NULL;*xx=0; }

	// make sure we do not download gigablast.com admin pages!
	if ( g_hostdb.isIpInNetwork ( r->m_firstIp ) && r->size_url-1 >= 7 ) {
		Url url;
		url.set ( r->ptr_url );
		// . never download /master urls from ips of hosts in cluster
		// . TODO: FIX! the pages might be in another cluster!
		// . pages are now /admin/* not any /master/* any more.
		if ( ( //strncasecmp ( url.getPath() , "/master/" , 8 ) == 0 ||
		       strncasecmp ( url.getPath() , "/admin/"  , 7 ) == 0 )) {
			log("spider: Got request to download possible "
			    "gigablast control page %s. Sending back "
			    "ERESTRICTEDPAGE.",
			    url.getUrl());
			g_errno = ERESTRICTEDPAGE;
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
	}

	// . use a max cached age of 24hrs for robots.txt files
	// . this returns true if robots.txt file for hostname found in cache
	// . don't copy since, we analyze immediately and don't block
	char *rec;
	int32_t  recSize;
	// get the cache
	RdbCache *c = &s_httpCacheOthers;
	if ( r->m_isRobotsTxt ) c = &s_httpCacheRobots;
	// the key is just the 64 bit hash of the url
	key_t k; k.n1 = 0; k.n0 = r->m_cacheKey;
	// see if in there already
	bool inCache = c->getRecord ( (collnum_t)0     , // share btwn colls
				      k                , // cacheKey
				      &rec             ,
				      &recSize         ,
				      true             , // copy?
				      r->m_maxCacheAge , // 24*60*60 ,
				      true             ); // stats?

	r->m_foundInCache = false;

	// . an empty rec is a cached not found (no robot.txt file)
	// . therefore it's allowed, so set *reply to 1 (true)
	if ( inCache ) {
		// log debug?
		//if ( r->m_isSquidProxiedUrl )
		if ( g_conf.m_logDebugSpider )
			log("proxy: found %"INT32" bytes in cache for %s",
			    recSize,r->ptr_url);

		r->m_foundInCache = true;

		// helpful for debugging. even though you may see a robots.txt
		// redirect and think we are downloading that each time,
		// we are not... the redirect is cached here as well.
		//log("spider: %s was in cache",r->ptr_url);
		// . send the cached reply back
		// . this will free send/read bufs on completion/g_errno
		g_udpServer.sendReply_ass ( rec , recSize , rec, recSize,slot);
		return;
	}

	// log it so we can see if we are hammering
	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads ||
	     g_conf.m_logDebugMsg13 )
		logf(LOG_DEBUG,"spider: DOWNLOADING %s firstIp=%s",
		     r->ptr_url,iptoa(r->m_firstIp));

	// temporary hack
	if ( r->m_parent ) { char *xx=NULL;*xx=0; }

	// assume we do not add it!
	//r->m_addToTestCache = false;

	if ( ! s_flag ) {
		s_flag = true;
		s_hammerCache.init ( 15000       , // maxcachemem,
				     8          , // fixed data size
				     false      , // support lists?
				     500        , // max nodes
				     false      , // use half keys?
				     "hamcache" , // dbname
				     false      , // load from disk?
				     12         , // key size
				     12         , // data key size?
				     -1         );// numPtrsMax
	}

	// sanity
	if ( r->m_useTestCache && ! r->m_testDir[0] ) { char *xx=NULL;*xx=0;}

	// try to get it from the test cache?
	TcpSocket ts;
	if ( r->m_useTestCache && getTestDoc ( r->ptr_url, &ts , r ) ) {
		// save this
		r->m_udpSlot = slot;
		// store the request so gotHttpReply can reply to it
		if ( ! s_rt.addKey ( &r->m_cacheKey , &r ) ) {
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
		// sanity check
		if ( ts.m_readOffset  < 0 ) { char *xx=NULL;*xx=0; }
		if ( ts.m_readBufSize < 0 ) { char *xx=NULL;*xx=0; }
		// reply to it right away
		gotHttpReply ( r , &ts );
		// done
		return;
	}

	// if wanted it to be in test cache but it was not, we have to 
	// download it, so use a fresh ip! we ran into a problem when
	// downloading a new doc from an old ip in ips.txt!!
	if ( r->m_useTestCache )
		r->m_urlIp = 0;

	// save this
	r->m_udpSlot = slot;
	// sanity check
	if ( ! slot ) { char *xx=NULL;*xx=0; }

	// send to a proxy if we are doing compression and not a proxy
	if ( r->m_useCompressionProxy && ! g_hostdb.m_myHost->m_isProxy ) {
		// use this key to select which proxy host
		int32_t key = ((uint32_t)r->m_firstIp >> 8);
		// send to host "h"
		Host *h = g_hostdb.getBestSpiderCompressionProxy(&key);
		if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
			log(LOG_DEBUG,"spider: sending to compression proxy "
			    "%s:%"UINT32"",iptoa(h->m_ip),(uint32_t)h->m_port);
		// . otherwise, send the request to the key host
		// . returns false and sets g_errno on error
		// . now wait for 2 minutes before timing out
		if ( ! g_udpServer.sendRequest ( (char *)r    ,
						 r->getSize() ,
						 0x13         , // msgType 0x13
						 h->m_ip      ,
						 h->m_port    ,
						 // we are sending to the proxy
						 // so make this -1
						 -1 , // h->m_hostId  ,
						 NULL         ,
						 r            , // state data
						 passOnReply  ,
						 200 , // 200 sec timeout
						 -1,//backoff
						 -1,//maxwait
						 NULL,//replybuf
						 0,//replybufmaxsize
						 niceness)) {
			// g_errno should be set
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
		// wait for it
		return;
	}

	// we skip it if its a frame page, robots.txt, root doc or some other
	// page that is a "child" page of the main page we are spidering.
	// MDW: i moved this AFTER sending to the compression proxy...
	// if ( ! r->m_skipHammerCheck ) {
	// 	// if addToHammerQueue() returns true and queues this url for
	// 	// download later, when ready to download call this function
	// 	r->m_hammerCallback = downloadTheDocForReals;
	// 	// this returns true if added to the queue for later
	// 	if ( addToHammerQueue ( r ) ) return;
	// }

	// do not get .google.com/ crap
	//if ( strstr(r->ptr_url,".google.com/") ) { char *xx=NULL;*xx=0; }

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// was it in our table of ips that are throttling us?
	r->m_wasInTableBeforeStarting = isIpInTwitchyTable ( cr , r->m_urlIp );

	downloadTheDocForReals ( r );
}

static void downloadTheDocForReals2  ( Msg13Request *r ) ;
static void downloadTheDocForReals3a ( Msg13Request *r ) ;
static void downloadTheDocForReals3b ( Msg13Request *r ) ;

static void gotHttpReply9 ( void *state , TcpSocket *ts ) ;

static void gotProxyHostReplyWrapper ( void *state , UdpSlot *slot ) ;

void downloadTheDocForReals ( Msg13Request *r ) {

	// are we the first?
	bool firstInLine = s_rt.isEmpty ( &r->m_cacheKey );
	// wait in line cuz someone else downloading it now
	if ( ! s_rt.addKey ( &r->m_cacheKey , &r ) ) {
		log("spider: error adding to waiting table %s",r->ptr_url);
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		return;
	}

	// this means our callback will be called
	if ( ! firstInLine ) {
		log("spider: waiting in line %s",r->ptr_url);
		return;
	}

	downloadTheDocForReals2 ( r );
}

// insertion point when we try to get another proxy to use because the one
// we tried seemed to be ip-banned
void downloadTheDocForReals2 ( Msg13Request *r ) {

	bool useProxies = false;

	// user can turn off proxy use with this switch
	//if ( ! g_conf.m_useProxyIps ) useProxies = false;

	// for diffbot turn ON if use robots is off
	if ( r->m_forceUseFloaters ) useProxies = true;

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// if you turned on automatically use proxies in spider controls...
	if ( ! useProxies && 
	     cr &&
	     r->m_urlIp != 0 &&
	     r->m_urlIp != -1 &&
	     // either the global or local setting will work
	     //( g_conf.m_automaticallyUseProxyIps || 
	     cr->m_automaticallyUseProxies &&
	     isIpInTwitchyTable( cr, r->m_urlIp ) )
		useProxies = true;

	// we gotta have some proxy ips that we can use
	if ( ! g_conf.m_proxyIps.hasDigits() ) useProxies = false;


	// we did not need a spider proxy ip so send this reuest to a host
	// to download the url
	if ( ! useProxies ) {
		downloadTheDocForReals3a ( r );
		return;
	}

	// before we send out the msg13 request try to get the spider proxy
	// that is the best one to use. only do this if we had spider proxies
	// specified in m_spiderProxyBuf

	// request is just the urlip
	//ProxyRequest *pr ;
	//pr = (ProxyRequest *)mmalloc ( sizeof(ProxyRequest),"proxreq");
	//if ( ! pr ) {
	//	log("sproxy: error: %s",mstrerror(g_errno));
	//	g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
	//}

	// try to get a proxy ip/port to download our url with
	//pr->m_urlIp      = r->m_urlIp;
	//pr->m_retryCount = r->m_retryCount;
	//pr->m_opCode     = OP_GETPROXY;

	r->m_opCode = OP_GETPROXY;

	// if we are being called from gotHttpReply9() below trying to
	// get a new proxy because the last was banned, we have to set
	// these so handleRequest54() in SpiderProxy.cpp can call
	// returnProxy()

	// get first alive host, usually host #0 but if he is dead then
	// host #1 must take over! if all are dead, it returns host #0.
	// so we are guaranteed "h will be non-null
	Host *h = g_hostdb.getFirstAliveHost();
	// now ask that host for the best spider proxy to send to
	if ( ! g_udpServer.sendRequest ( (char *)r,
					 // just the top part of the
					 // Msg13Request is sent to
					 // handleRequest54() now
					 r->getProxyRequestSize() ,
					 0x54         , // msgType 0x54
					 h->m_ip      ,
					 h->m_port    ,
					 -1 , // h->m_hostId  ,
					 NULL         ,
					 r         , // state data
					 gotProxyHostReplyWrapper  ,
					 9999999  )){// 9999999 sec timeout
		// sanity check
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// report it
		log("spider: msg54 request1: %s %s",
		    mstrerror(g_errno),r->ptr_url);
		// crap we gotta send back a reply i guess
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		// g_errno must be set!
		return;
	}
	// otherwise we block
	return;
}

void gotProxyHostReplyWrapper ( void *state , UdpSlot *slot ) {
	// int16_tcut
	Msg13Request *r = (Msg13Request *)state;
	//Msg13 *THIS = r->m_parent;
	// don't let udpserver free the request, it's our m_urlIp
	slot->m_sendBufAlloc = NULL;
	// error getting spider proxy to use?
	if ( g_errno ) {
		// note it
		log("sproxy: got proxy request error: %s",mstrerror(g_errno));
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		return;
	}
	//
	// the reply is the ip and port of the spider proxy to use
	//
	// what did he give us?
	char *reply          = slot->m_readBuf;
	int32_t  replySize      = slot->m_readBufSize;
	//int32_t  replyAllocSize = slot->m_readBufMaxSize;
	// bad reply? ip/port/LBid
	if ( replySize != sizeof(ProxyReply) ) {
		log("sproxy: bad 54 reply size of %"INT32" != %"INT32" %s",
		    replySize,(int32_t)sizeof(ProxyReply),r->ptr_url);
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		return;
	}

	// set that
	ProxyReply *prep = (ProxyReply *)reply;

	r->m_proxyIp   = prep->m_proxyIp;
	r->m_proxyPort = prep->m_proxyPort;
	// the id of this transaction for the LoadBucket
	// need to save this so we can use it when we send a msg55 request
	// out to tell host #0 how long it took use to use this proxy, etc.
	r->m_lbId = prep->m_lbId;

	// assume no username:password
	r->m_proxyUsernamePwdAuth[0] = '\0';

	// if proxy had one copy into the buf
	if ( prep->m_usernamePwd && prep->m_usernamePwd[0] ) {
		int32_t len = gbstrlen(prep->m_usernamePwd);
		gbmemcpy ( r->m_proxyUsernamePwdAuth , 
			   prep->m_usernamePwd ,
			   len );
		r->m_proxyUsernamePwdAuth[len] = '\0';
	}

	// if this proxy ip seems banned, are there more proxies to try?
	r->m_hasMoreProxiesToTry = prep->m_hasMoreProxiesToTry;

	// . how many proxies have been banned by the urlIP?
	// . the more that are banned the higher the self-imposed crawl delay.
	// . handleRequest54() in SpiderProxy.cpp will check s_banTable 
	//   to count how many are banned for this urlIp. it saves s_banTable
	//   (a hashtable) to disk so it should be persistent.
	r->m_numBannedProxies = prep->m_numBannedProxies;

	// if addToHammerQueue() returns true and queues this url for
	// download later, when ready to download call this function
	//r->m_hammerCallback = downloadTheDocForReals3;

	// . returns true if we queued it for trying later
	// . scanHammerQueue() will call downloadTheDocForReals3(r) for us
	//if ( addToHammerQueue ( r ) ) return;

	// now forward the request
	// now the reply should have the proxy host to use
	// return if this blocked
	//if ( ! THIS->forwardRequest() ) return;
	// it did not block...
	//THIS->m_callback ( THIS->m_state );
	downloadTheDocForReals3a ( r );
}

//
// we need r->m_numBannedProxies to be valid for hammer queueing
// so we have to do the hammer queue stuff after getting the proxy reply
//
void downloadTheDocForReals3a ( Msg13Request *r ) {

	// if addToHammerQueue() returns true and queues this url for
	// download later, when ready to download call this function
	r->m_hammerCallback = downloadTheDocForReals3b;

	// . returns true if we queued it for trying later
	// . scanHammerQueue() will call downloadTheDocForReals3(r) for us
	if ( addToHammerQueue ( r ) ) return;

	downloadTheDocForReals3b( r );
}

void downloadTheDocForReals3b ( Msg13Request *r ) {

	int64_t nowms = gettimeofdayInMilliseconds();

	// assume no download start time
	r->m_downloadStartTimeMS = 0;

	// . store time now
	// . no, now we store 0 to indicate in progress, then we
	//   will overwrite it with a timestamp when the download completes
	// . but if measuring crawldelay from beginning of the download then
	//   store the current time
	// . do NOT do this when downloading robots.txt etc. type files
	//   which should have skipHammerCheck set to true
	if ( r->m_crawlDelayFromEnd && ! r->m_skipHammerCheck ) {
		s_hammerCache.addLongLong(0,r->m_firstIp, 0LL);//nowms);
		log("spider: delay from end for %s %s",iptoa(r->m_firstIp),
		    r->ptr_url);
	}
	else if ( ! r->m_skipHammerCheck ) {
		// get time now
		s_hammerCache.addLongLong(0,r->m_firstIp, nowms);
		log(LOG_DEBUG,
		    "spider: adding new time to hammercache for %s %s = %"INT64"",
		    iptoa(r->m_firstIp),r->ptr_url,nowms);
	}
	else {
		log(LOG_DEBUG,
		    "spider: not adding new time to hammer cache for %s %s",
		    iptoa(r->m_firstIp),r->ptr_url);
	}

	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: adding special \"in-progress\" time "
		    "of %"INT32" for "
		    "firstIp=%s "
		    "url=%s "
		    "to msg13::hammerCache",
		    0,//-1,
		    iptoa(r->m_firstIp),
		    r->ptr_url);



	// flag this
	//if ( g_conf.m_qaBuildMode ) r->m_addToTestCache = true;
	// note it here
	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log("spider: downloading %s (%s) (skiphammercheck=%"INT32")",
		    r->ptr_url,iptoa(r->m_urlIp) ,
		    (int32_t)r->m_skipHammerCheck);

	// use the default agent unless scraping
	// force to event guru bot for now
	//char *agent = "Mozilla/5.0 (compatible; ProCogSEOBot/1.0; +http://www.procog.com/ )";
	//char *agent = "Mozilla/5.0 (compatible; GigaBot/1.0; +http://www.gigablast.com/ )";
	char *agent = g_conf.m_spiderUserAgent;
	if ( r->m_isScraping )
		agent = "Mozilla/4.0 "
			"(compatible; MSIE 6.0; Windows 98; "
			"Win 9x 4.90)" ;

	// if sending to a proxy use random user agent
	if ( g_conf.m_useRandAgents && r->m_proxyIp ) {
		// use that
		agent = getRandUserAgent ( r->m_urlIp , 
					   r->m_proxyIp ,
					   r->m_proxyPort );
	}

	// . for bulk jobs avoid actual downloads of the page for efficiency
	// . g_fakeReply is just a simple mostly empty 200 http reply
	if ( r->m_isCustomCrawl == 2 ) {
		int32_t slen = gbstrlen(g_fakeReply);
		int32_t fakeBufSize = slen + 1;
		// try to fix memleak
		char *fakeBuf = g_fakeReply;//mdup ( s, fakeBufSize , "fkblk");
		//r->m_freeMe = fakeBuf;
		//r->m_freeMeSize = fakeBufSize;
		gotHttpReply2 ( r , 
				fakeBuf,
				fakeBufSize, // include \0
				fakeBufSize, // allocsize
				NULL ); // tcpsock
		return;
	}


	// after downloading the doc call this callback
	void (* callback) ( void *,TcpSocket *);

	// if using a proxy tell host #0 we are done with that proxy so
	// it can do its proxy load balancing
	if ( r->m_proxyIp && r->m_proxyIp != -1 ) 
		callback = gotHttpReply9;
	// otherwise not using a proxy
	else
		callback = gotHttpReply;

	// debug note
	if ( r->m_proxyIp ) {
		char tmpIp[64];
		sprintf(tmpIp,"%s",iptoa(r->m_urlIp));
		log(LOG_INFO,
		    "sproxy: got proxy %s:%"UINT32" "
		    "and agent=\"%s\" to spider "
		    "%s %s (numBannedProxies=%"INT32")",
		    iptoa(r->m_proxyIp),
		    (uint32_t)(uint16_t)r->m_proxyPort,
		    agent,
		    tmpIp,
		    r->ptr_url,
		    r->m_numBannedProxies);
	}

	char *exactRequest = NULL;

	// if simulating squid just pass the proxied request on
	// to the webserver as it is, but without the secret
	// Proxy-Authorization: Basic abcdefgh base64 encoded
	// username/password info.
	if ( r->m_isSquidProxiedUrl ) {
		exactRequest = r->ptr_url;
		stripProxyAuthorization ( exactRequest );
	}

	// convert "GET http://xyz.com/abc" to "GET /abc" if not sending
	// to another proxy... and sending to the actual webserver
	if ( r->m_isSquidProxiedUrl && ! r->m_proxyIp )
		fixGETorPOST ( exactRequest );

	// ALSO ADD authorization to the NEW proxy we are sending to
	// r->m_proxyIp/r->m_proxyPort that has a username:password
	char tmpBuf[1024];
	SafeBuf newReq (tmpBuf,1024);
	if ( r->m_isSquidProxiedUrl && r->m_proxyIp ) {
		newReq.safeStrcpy ( exactRequest );
		addNewProxyAuthorization ( &newReq , r );
		newReq.nullTerm();
		exactRequest = newReq.getBufStart();
	}

	// indicate start of download so we can overwrite the 0 we stored
	// into the hammercache
	r->m_downloadStartTimeMS = nowms;

	// prevent core from violating MAX_DGRAMS #defined in UdpSlot.h
	int32_t maxDocLen1 = r->m_maxTextDocLen;
	int32_t maxDocLen2 = r->m_maxOtherDocLen;
	
	// fix core in UdpServer.cpp from sending back a too big reply
	if ( maxDocLen1 < 0 || maxDocLen1 > MAX_ABSDOCLEN )
		maxDocLen1 = MAX_ABSDOCLEN;
	if ( maxDocLen2 < 0 || maxDocLen2 > MAX_ABSDOCLEN )
		maxDocLen2 = MAX_ABSDOCLEN;


	// . download it
	// . if m_proxyIp is non-zero it will make requests like:
	//   GET http://xyz.com/abc
	if ( ! g_httpServer.getDoc ( r->ptr_url             ,
				     r->m_urlIp           ,
				     0                    , // offset
				     -1                   ,
				     r->m_ifModifiedSince ,
				     r                    , // state
				     callback       , // callback
				     30*1000              , // 30 sec timeout
				     r->m_proxyIp     ,
				     r->m_proxyPort   ,
				     maxDocLen1,//r->m_maxTextDocLen   ,
				     maxDocLen2,//r->m_maxOtherDocLen  ,
				     agent                ,
				     // prevent HTTP STATUS 406
				     // not acceptable response by using 1.1
				     // instead of 1.0 for www.mindanews.com
				     DEFAULT_SPIDER_HTTP_PROTO , // "HTTP/1.1"
				     false , // doPost?
				     r->ptr_cookie , // cookie
				     NULL , // additionalHeader
				     exactRequest , // our own mime!
				     NULL , // postContent
				     // this is NULL or '\0' if not there
				     r->m_proxyUsernamePwdAuth ) )
		// return false if blocked
		return;
	// . log this so i know about it
	// . g_errno MUST be set so that we do not DECREMENT
	//   the outstanding dom/ip counts in gotDoc() below
	//   because we did not increment them above
	logf(LOG_DEBUG,"spider: http server had error: %s",mstrerror(g_errno));
	// g_errno should be set
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// if called from ourselves. return true with g_errno set.
	//if ( r->m_parent ) return true;
	// if did not block -- should have been an error. call callback
	gotHttpReply ( r , NULL );
	return ;
}

static int32_t s_55Out = 0;

void doneReportingStatsWrapper ( void *state, UdpSlot *slot ) {
	//Msg13Request *r = (Msg13Request *)state;
	// note it
	if ( g_errno )
		log("sproxy: 55 reply: %s",mstrerror(g_errno));
	// do not free request, it was part of original Msg13Request
	//slot->m_sendBuf = NULL;
	// clear g_errno i guess
	g_errno = 0;
	// don't let udpserver free the request, it's our m_urlIp
	slot->m_sendBufAlloc = NULL;
	// resume on our way down the normal pipeline
	//gotHttpReply ( state , r->m_tcpSocket );
	s_55Out--;
}

bool ipWasBanned ( TcpSocket *ts , const char **msg , Msg13Request *r ) {

	// ts will be null if we got a fake reply from a bulk job
	if ( ! ts )
		return false;

	// do not do this on robots.txt files
	if ( r->m_isRobotsTxt )
		return false;

	// g_errno is 104 for 'connection reset by peer'
	if ( g_errno == ECONNRESET ) {
		*msg = "connection reset";
		return true;
	}

	// proxy returns empty reply not ECONNRESET if it experiences 
	// a conn reset
	if ( g_errno == EBADMIME && ts->m_readOffset == 0 ) {
		*msg = "empty reply";
		return true;
	}

	// on other errors do not do the ban check. it might be a
	// tcp time out or something so we have no reply. but connection resets
	// are a popular way of saying, hey, don't hit me so hard.
	if ( g_errno ) return false;

	// if they closed the socket on us we read 0 bytes, assumed
	// we were banned...
	if ( ts->m_readOffset == 0 ) {
		*msg = "empty reply";
		return true;
	}

	// check the http mime for 403 Forbidden
	HttpMime mime;
	mime.set ( ts->m_readBuf , ts->m_readOffset , NULL );

	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus == 403 ) {
		*msg = "status 403 forbidden";
		return true;
	}
	if ( httpStatus == 999 ) {
		*msg = "status 999 request denied";
		return true;
	}
	// let's add this new one
	if ( httpStatus == 503 ) {
		*msg = "status 503 service unavailable";
		return true;
	}

	// if it has link to "google.com/recaptcha"
	// TODO: use own gbstrstr so we can do QUICKPOLL(niceness)
	// TODO: ensure NOT in an invisible div
	if ( strstr ( ts->m_readBuf , "google.com/recaptcha/api/challenge") ) {
		*msg = "recaptcha link";
		return true;
	}

	//CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// if it is a seed url and there are no links, then perhaps we
	// are in a blacklist somewhere already from triggering a spider trap
	// i've seen this flub on a site where they just return a script
	// and it is not banned, so let's remove this until we thinkg
	// of something better.
	// if ( //isInSeedBuf ( cr , r->ptr_url ) &&
	//      // this is set in XmlDoc.cpp based on hopcount really
	//      r->m_isRootSeedUrl &&
	//      ! strstr ( ts->m_readBuf, "<a href" ) ) {
	// 	*msg = "root/seed url with no outlinks";
	// 	return true;
	// }


	// TODO: compare a simple checksum of the page content to what
	// we have downloaded previously from this domain or ip. if it 
	// seems to be the same no matter what the url, then perhaps we
	// are banned as well.

	// otherwise assume not.
	*msg = NULL;

	return false;
}

// come here after telling host #0 we are done using this proxy.
// host #0 will update the loadbucket for it, using m_lbId.
void gotHttpReply9 ( void *state , TcpSocket *ts ) {

 	// cast it
	Msg13Request *r = (Msg13Request *)state;


	// if we got a 403 Forbidden or an empty reply
	// then assume the proxy ip got banned so try another.
	const char *banMsg = NULL;
	//bool banned = false;

	if ( g_errno )
		log("msg13: got error from proxy: %s",mstrerror(g_errno));

	if ( g_conf.m_logDebugSpider )
		log("msg13: got proxy reply for %s",r->ptr_url);

	//if ( ! g_errno ) 
	bool banned = ipWasBanned ( ts , &banMsg , r );

	if ( g_conf.m_logDebugTcp && ts )
		log("msg13: got reply=%s",ts->m_readBuf);


	// inc this every time we try
	r->m_proxyTries++;

	// log a handy msg if proxy was banned
	if ( banned ) {
		char *msg = "No more proxies to try. Using reply as is.";
		if ( r->m_hasMoreProxiesToTry )  msg = "Trying another proxy.";
		char tmpIp[64];
		sprintf(tmpIp,"%s",iptoa(r->m_urlIp));
		log("msg13: detected that proxy %s is banned "
		    "(banmsg=%s) "
		    "(tries=%"INT32") by "
		    "url %s %s. %s"
		    , iptoa(r->m_proxyIp) // r->m_banProxyIp
		    , banMsg
		    , r->m_proxyTries
		    , tmpIp
		    , r->ptr_url 
		    , msg );
	}

	if ( banned &&
	     // try up to 5 different proxy ips. try to try new c blocks
	     // if available.
	     //r->m_proxyTries < 5 &&
	     // if all proxies are banned for this r->m_urlIp then
	     // this will be false
	     r->m_hasMoreProxiesToTry ) {
		// tell host #0 to add this urlip/proxyip pair to its ban tbl
		// when it sends a msg 0x54 request to get another proxy.
		// TODO: shit, it also has to return the banned proxy...
		r->m_banProxyIp   = r->m_proxyIp;
		r->m_banProxyPort = r->m_proxyPort;
		// . re-download but using a different proxy
		// . handleRequest54 should not increment the outstanding
		//   count beause we should give it the same m_lbId
		// . skip s_rt table since we are already first in line and
		//   others may be waiting for us...
		downloadTheDocForReals2 ( r );
		return;
	}

	//r->m_tcpSocket = ts;

	//ProxyRequest *preq; // = &r->m_proxyRequest;
	//preq = (ProxyRequest *)mmalloc ( sizeof(ProxyRequest),"stupid");
	//preq->m_urlIp = r->m_urlIp;
	//preq->m_retHttpProxyIp = r->m_proxyIp;
	//preq->m_retHttpProxyPort = r->m_proxyPort;
	//preq->m_lbId = r->m_lbId; // the LoadBucket ID
	//preq->m_opCode = OP_RETPROXY; // tell host #0 to reduce proxy load cn

	// tell host #0 to reduce proxy load cnt
	r->m_opCode = OP_RETPROXY; 

	//r->m_blocked = false;

	Host *h = g_hostdb.getFirstAliveHost();
	// now return the proxy. this will decrement the load count on
	// host "h" for this proxy.
	if ( g_udpServer.sendRequest ( (char *)r,
				       r->getProxyRequestSize(),
				       0x54 ,
				       h->m_ip      ,
				       h->m_port    ,
				       -1 , // h->m_hostId  ,
				       NULL         ,
				       r        , // state data
				       doneReportingStatsWrapper  ,
				       10    )){// 10 sec timeout
		// it blocked!
		//r->m_blocked = true;
		s_55Out++;
		// sanity
		if ( s_55Out > 500 )
			log("sproxy: s55out > 500 = %"INT32"",s_55Out);
	}
	// sanity check
	//if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// report it
	if ( g_errno ) log("spider: msg54 request2: %s %s",
			   mstrerror(g_errno),r->ptr_url);
	// it failed i guess proceed
	gotHttpReply( state , ts );
}
/*
static void doneInjectingProxyReply ( void *state ) {
	Msg7 *msg7 = (Msg7 *)state;
	if ( g_errno )
		log("msg13: got error injecting proxied req: %s",
		    mstrerror(g_errno));
	g_errno = 0;
	mdelete ( msg7 , sizeof(Msg7) , "dm7");
	delete ( msg7 );
}

static bool markupServerReply ( Msg13Request *r , TcpSocket *ts );
*/

void gotHttpReply ( void *state , TcpSocket *ts ) {

	/*
 	// cast it
	Msg13Request *r = (Msg13Request *)state;

	//////////
	//
	// before we mark it up, let's inject it!!
	// if a squid proxied request.
	// now inject this url into the main or GLOBAL-INDEX collection
	// so we can start accumulating sectiondb vote/markup stats
	// but do not wait for the injection to complete before sending
	// it back to the requester.
	//
	//////////
	if ( ! r->m_foundInCache && 
	     r->m_isSquidProxiedUrl ) {
		// make a new msg7 to inject it
		Msg7 *msg7;
		try { msg7 = new (Msg7); }
		catch ( ... ) { 
			g_errno = ENOMEM;
			log("squid: msg7 new(%i): %s",
			    (int)sizeof(Msg7),mstrerror(g_errno));
			return;
		}
		mnew ( msg7, sizeof(Msg7), "m7st" );

		int32_t httpReplyLen = ts->m_readOffset;

		// parse out the http mime
		HttpMime hm;
		hm.set ( ts->m_readBuf , httpReplyLen , NULL );
		if ( hm.getHttpStatus() != 200 ) 
			goto skipInject;

		// inject requires content be null terminated. sanity check
		if ( ts->m_readBuf && httpReplyLen > 0 &&
		     ts->m_readBuf[httpReplyLen] ) { char *xx=NULL;*xx=0;}

		// . this may or may not block, we give it a callback that
		//   just delete the msg7. we do not want this to hold up us 
		//   returning the proxied reply to the client browser.
		// . so frequently hit sites will accumulate useful voting 
		//   info  since we inject each one
		// . if we hit the page cache above we won't make it this far 
		//   though
		// . but i think that cache is only for 60 seconds
		if ( msg7->inject ( "main", // put in main collection
				    r->m_proxiedUrl,//url,
				    r->m_proxiedUrlLen,
				    ts->m_readBuf,
				    msg7 ,
				    doneInjectingProxyReply ) ) {
			log("msg7: inject error: %s",mstrerror(g_errno));
			mdelete ( msg7 , sizeof(Msg7) , "dm7");
			delete ( msg7 );
			// we can't return here we have to pass the request
			// on to the browser client...
			g_errno = 0;
			//return;
		}
	}

 skipInject:

	// now markup the reply with the sectiondb info
	// . it can block reading the disk
	// . returns false if blocked
	// . only do markup if its a proxied request
	// . our squid proxy simulator is only a markup simulator
	if ( ! r->m_foundInCache && r->m_isSquidProxiedUrl ) {
		// . now transform the html to include sectiondb data
		// . this will also send the reply back so no need to
		//   have a callback here
		// . it returns false if it did nothing because the
		//   content type was not html or http status was not 200
		// . if it returns false then just pass through it
		// . if it returns true, then it will be responsible for
		//   sending back the udp reply of the marked up page
		// . returns false if blocked, true otherwise
		// . returns true and sets g_errno on error
		// . when done it just calls gotHttpReply2() on its own
		if ( ! markupServerReply ( r , ts ) ) 
			return;
		// oom error? force ts to NULL so it will be sent below
		if ( g_errno ) {
			log("msg13: markupserverply: %s",mstrerror(g_errno));
			ts = NULL;
		}
	}
	*/

	// if we had no error, TcpSocket should be legit
	if ( ts ) {

		if ( g_conf.m_logDebugTcp )
			log("msg13: got reply=%s",ts->m_readBuf);

		gotHttpReply2 ( state , 
				ts->m_readBuf ,
				ts->m_readOffset ,
				ts->m_readBufSize,
				ts );
		// now after we return TcpServer will DESTROY "ts" and
		// free m_readBuf... so we should not have any reference to it
		return;
	}
	// sanity check, if ts is NULL must have g_errno set
	if ( ! g_errno ) { char *xx=NULL;*xx=0; } // g_errno=EBADENG...
	// if g_errno is set i guess ts is NULL!
	gotHttpReply2 ( state ,  NULL ,0 , 0 , NULL );
}

void gotHttpReply2 ( void *state , 
		     char *reply , 
		     int32_t  replySize ,
		     int32_t  replyAllocSize ,
		     TcpSocket *ts ) {

	// save error
	int32_t savedErr = g_errno;

	Msg13Request *r    = (Msg13Request *) state;
	UdpSlot      *slot = r->m_udpSlot;

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// ' connection reset' debug stuff
	// log("spider: httpreplysize=%i",(int)replySize);
	// if ( replySize == 0 )
	// 	log("hey");

	// error?
	if ( g_errno && ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 ) )
		log("spider: http reply (msg13) had error = %s "
		    "for %s at ip %s",
		    mstrerror(savedErr),r->ptr_url,iptoa(r->m_urlIp));

	bool inTable = false;
	bool checkIfBanned = false;
	if ( cr && cr->m_automaticallyBackOff    ) checkIfBanned = true;
	if ( cr && cr->m_automaticallyUseProxies ) checkIfBanned = true;
	// must have a collrec to hold the ips
	if ( checkIfBanned && cr && r->m_urlIp != 0 && r->m_urlIp != -1 )
		inTable = isIpInTwitchyTable ( cr , r->m_urlIp );

	// check if our ip seems banned. if g_errno was ECONNRESET that
	// is an indicator it was throttled/banned.
	const char *banMsg = NULL;
	bool banned = false;
	if ( checkIfBanned )
		banned = ipWasBanned ( ts , &banMsg , r );
	if (  banned )
		// should we turn proxies on for this IP address only?
		log("msg13: url %s detected as banned (%s), "
		    "for ip %s"
		    , r->ptr_url
		    , banMsg
		    , iptoa(r->m_urlIp) 
		    );

	// . add to the table if not in there yet
	// . store in our table of ips we should use proxies for
	// . also start off with a crawldelay of like 1 sec for this
	//   which is not normal for using proxies.
	if ( banned && ! inTable )
		addIpToTwitchyTable ( cr , r->m_urlIp );

	// did we detect it as banned?
	if ( banned && 
	     // retry iff we haven't already, but if we did stop the inf loop
	     ! r->m_wasInTableBeforeStarting &&
	     cr &&
	     ( cr->m_automaticallyBackOff || cr->m_automaticallyUseProxies ) &&
	     // but this is not for proxies... only native crawlbot backoff
	     ! r->m_proxyIp ) {
		// note this as well
		log("msg13: retrying spidered page with new logic for %s",
		    r->ptr_url);
		// reset this so we don't endless loop it
		r->m_wasInTableBeforeStarting = true;
		// reset error
		g_errno = 0;
		/// and retry. it should use the proxy... or at least
		// use a crawldelay of 3 seconds since we added it to the
		// twitchy table.
		downloadTheDocForReals2 ( r );
		// that's it. if it had an error it will send back a reply.
		return;
	}

	// do not print this if we are already using proxies, it is for
	// the auto crawldelay backoff logic only
	if ( banned && r->m_wasInTableBeforeStarting && ! r->m_proxyIp )
		log("msg13: can not retry banned download of %s "
		    "because we knew ip was banned at start",r->ptr_url);

	// get time now
	int64_t nowms = gettimeofdayInMilliseconds();

	// right now there is a 0 in there to indicate in-progress.
	// so we must overwrite with either the download start time or the
	// download end time.
	int64_t timeToAdd = r->m_downloadStartTimeMS;
	if ( r->m_crawlDelayFromEnd ) timeToAdd = nowms;

	// . now store the current time in the cache
	// . do NOT do this for robots.txt etc. where we skip hammer check
	if ( ! r->m_skipHammerCheck ) 
		s_hammerCache.addLongLong(0,r->m_firstIp,timeToAdd);

	// note it
	if ( g_conf.m_logDebugSpider && ! r->m_skipHammerCheck )
		log(LOG_DEBUG,"spider: adding last download time "
		    "of %"INT64" for firstIp=%s url=%s "
		    "to msg13::hammerCache",
		    timeToAdd,iptoa(r->m_firstIp),r->ptr_url);


	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log(LOG_DEBUG,"spider: got http reply for firstip=%s url=%s "
		    "err=%s",
		    iptoa(r->m_firstIp),r->ptr_url,mstrerror(savedErr));
	

	// sanity. this was happening from iframe download
	//if ( g_errno == EDNSTIMEDOUT ) { char *xx=NULL;*xx=0; }

	// . sanity check - robots.txt requests must always be compressed
	// . saves space in the cache
	if ( ! r->m_compressReply && r->m_isRobotsTxt ) {char *xx=NULL; *xx=0;}
	// null terminate it always! -- unless already null terminated...
	if ( replySize > 0 && reply[replySize-1] ) reply[replySize++] = '\0';
	// sanity check
	if ( replySize > replyAllocSize ) { char *xx=NULL;*xx=0; }

	// save original size
	int32_t originalSize = replySize;

	// . add the reply to our test cache
	// . if g_errno is set to something like "TCP Timed Out" then
	//   we end up saving a blank robots.txt or doc here...
	
	if ( r->m_useTestCache && r->m_addToTestCache )
		addTestDoc ( r->m_urlHash64,reply,replySize,
			     savedErr , r );

	// note it
	if ( r->m_useTestCache && 
	     ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 ) )
		logf(LOG_DEBUG,"spider: got reply for %s "
		     "firstIp=%s uh48=%"UINT64"",
		     r->ptr_url,iptoa(r->m_firstIp),r->m_urlHash48);

	int32_t niceness = r->m_niceness;

	// sanity check
	if ( replySize>0 && reply[replySize-1]!= '\0') { char *xx=NULL;*xx=0; }

	// assume http status is 200
	bool goodStatus = true;

	int64_t *docsPtr     = NULL;
	int64_t *bytesInPtr  = NULL;
	int64_t *bytesOutPtr = NULL;

	// use this mime
	HttpMime mime;
	int32_t httpStatus = 0; // 200;

	// do not do any of the content analysis routines below if we
	// had a g_errno like ETCPTIMEDOUT or EBADMIME or whatever...
	if ( savedErr ) goodStatus = false;

	// no, its on the content only, NOT including mime
	int32_t mimeLen = 0;

	// only bother rewriting the error mime if user wanted compression
	// otherwise, don't bother rewriting it.
	// DO NOT do this if savedErr is set because we end up calling
	// sendErorrReply() below for that!
	if ( replySize>0 && r->m_compressReply && ! savedErr ) {
		// assume fake reply
		if ( reply == g_fakeReply ) {
			httpStatus = 200;
		}
		else {
			// exclude the \0 i guess. use NULL for url.
			mime.set ( reply , replySize - 1, NULL );
			// no, its on the content only, NOT including mime
			mimeLen = mime.getMimeLen();
			// get this
			httpStatus = mime.getHttpStatus();
		}
		// if it's -1, unknown i guess, then force to 505
		// server side error. we get an EBADMIME for our g_errno
		// when we enter this loop sometimes, so in that case...
		if ( httpStatus == -1 ) httpStatus = 505;
		if ( savedErr         ) httpStatus = 505;
		// if bad http status, re-write it
		if ( httpStatus != 200 ) {
			char tmpBuf[2048];
			char *p = tmpBuf;
			p += sprintf( tmpBuf, 
				      "HTTP/1.0 %"INT32"\r\n"
				      "Content-Length: 0\r\n" ,
				      httpStatus );
			// convery redirect urls back to requester
			char *loc    = mime.getLocationField();
			int32_t  locLen = mime.getLocationFieldLen();
			// if too big, forget it! otherwise we breach tmpBuf
			if ( loc && locLen > 0 && locLen < 1024 ) {
				p += sprintf ( p , "Location: " );
				gbmemcpy ( p , loc , locLen );
				p += locLen;
				gbmemcpy ( p , "\r\n", 2 );
				p += 2;
			}
			// close it up
			p += sprintf ( p , "\r\n" );
			// copy it over as new reply, include \0
			int32_t newSize = p - tmpBuf + 1;
			if ( newSize >= 2048 ) { char *xx=NULL;*xx=0; }
			// record in the stats
			docsPtr     = &g_stats.m_compressMimeErrorDocs;
			bytesInPtr  = &g_stats.m_compressMimeErrorBytesIn;
			bytesOutPtr = &g_stats.m_compressMimeErrorBytesOut;
			// only replace orig reply if we are smaller
			if ( newSize < replySize ) {
				gbmemcpy ( reply , tmpBuf , newSize );
				replySize = newSize;
			}
			// reset content hash
			goodStatus = false;
		}
	}				 
				 
	//Xml xml;
	//Words words;

	// point to the content
	char *content = reply + mimeLen;
	// reduce length by that
	int32_t contentLen = replySize - 1 - mimeLen;
	// fix bad crap
	if ( contentLen < 0 ) contentLen = 0;

	// fake http 200 reply?
	if ( reply == g_fakeReply ) { content = NULL; contentLen = 0; }

	/*
	if ( replySize > 0 && 
	     goodStatus && 
	     r->m_forEvents &&
	     ! r->m_isRobotsTxt &&
	     r->m_compressReply ) {
		// Links class required Xml class
		if ( ! xml.set ( content   ,
				 contentLen , // lennotsize! do not include \0
				 false     , // ownData?
				 false     , // purexml?
				 0         , // version! (unused)
				 false     , // set parents?
				 niceness  ) )
			log("scproxy: xml set had error: %s",
			    mstrerror(g_errno));
		// definitely compute the wordids so Dates.cpp can see if they
		// are a month name or whatever...
		if ( ! words.set ( &xml , true , niceness ) ) 
			log("scproxy: words set had error: %s",
			    mstrerror(g_errno));
	}

	if ( replySize > 0 && 
	     goodStatus &&
	     r->m_forEvents &&
	     !r->m_isRobotsTxt && 
	     r->m_compressReply ) {
		int32_t cs = getCharsetFast ( &mime,
					   r->ptr_url,
					   content,
					   contentLen,
					   niceness);
		if ( cs != csUTF8 && // UTF-8
		     cs != csISOLatin1 && // ISO-8859-1
		     cs != csASCII &&
		     cs != csUnknown &&
		     cs != cswindows1256 &&
		     cs != cswindows1250 &&
		     cs != cswindows1255 &&
		     cs != cswindows1252 ) { // windows-1252
			// record in the stats
			docsPtr     = &g_stats.m_compressBadCharsetDocs;
			bytesInPtr  = &g_stats.m_compressBadCharsetBytesIn;
			bytesOutPtr = &g_stats.m_compressBadCharsetBytesOut;
			replySize = 0;
		}
	}
	*/

	if ( replySize > 0 && 
	     goodStatus &&
	     //r->m_forEvents &&
	     !r->m_isRobotsTxt && 
	     r->m_compressReply ) {
		// get the content type from mime
		char ct = mime.getContentType();
		if ( ct != CT_HTML &&
		     ct != CT_TEXT &&
		     ct != CT_XML &&
		     ct != CT_PDF &&
		     ct != CT_DOC &&
		     ct != CT_XLS &&
		     ct != CT_PPT &&
		     ct != CT_PS ) {
			// record in the stats
			docsPtr     = &g_stats.m_compressBadCTypeDocs;
			bytesInPtr  = &g_stats.m_compressBadCTypeBytesIn;
			bytesOutPtr = &g_stats.m_compressBadCTypeBytesOut;
			replySize = 0;
		}
	}

	/*
	if ( replySize > 0 && 
	     goodStatus && 
	     r->m_forEvents &&
	     ! r->m_isRobotsTxt && 
	     r->m_compressReply ) {
		// make sure we loaded the unifiedDict (do now in main.cpp)
		//g_speller.init();
		// detect language, if we can
		int32_t score;
		// returns -1 and sets g_errno on error, 
		// because 0 means langUnknown
		int32_t langid = words.getLanguage(NULL,1000,niceness,&score);
		// anything 2+ is non-english
		if ( langid >= 2 ) {
			// record in the stats
			docsPtr     = &g_stats.m_compressBadLangDocs;
			bytesInPtr  = &g_stats.m_compressBadLangBytesIn;
			bytesOutPtr = &g_stats.m_compressBadLangBytesOut;
			replySize = 0;
		}
	}
	*/

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	bool hasIframe2 = false;
	if ( r->m_compressReply &&
	     goodStatus &&
	     ! r->m_isRobotsTxt )
		hasIframe2 = hasIframe ( reply , replySize, niceness ) ;

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	if ( hasIframe2 && 
	     ! r->m_attemptedIframeExpansion &&
	     ! r->m_isSquidProxiedUrl ) {
		// must have ts i think
		if ( ! ts ) { char *xx=NULL; *xx=0; }
		// sanity
		if ( ts->m_readBuf != reply ) { char *xx=NULL;*xx=0;}
		// . try to expand each iframe tag in there
		// . return without sending a reply back if this blocks
		// . it will return true and set g_errno on error
		// . when it has fully expanded the doc's iframes it we
		//   re-call this gotHttpReply() function but with the
		//   TcpServer's buf swapped out to be the buf that has the
		//   expanded iframes in it
		// . returns false if blocks
		// . returns true if did not block, sets g_errno on error
		// . if it blocked it will recall THIS function
		if ( ! getIframeExpandedContent ( r , ts ) ) {
			if ( g_conf.m_logDebugMsg13 ||
			     g_conf.m_logDebugSpider )
				log("msg13: iframe expansion blocked %s",
				    r->ptr_url);
			return;
		}
		// ok, did we have an error?
		if ( g_errno )
			log("scproxy: xml set for %s had error: %s",
			    r->ptr_url,mstrerror(g_errno));
		// otherwise, i guess we had no iframes worthy of expanding
		// so pretend we do not have any iframes
		hasIframe2 = false;
		// crap... had an error, give up i guess
		// record in the stats
		//docsPtr     = &g_stats.m_compressHasIframeDocs;
		//bytesInPtr  = &g_stats.m_compressHasIframeBytesIn;
		//bytesOutPtr = &g_stats.m_compressHasIframeBytesOut;
	}

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	// compute content hash
	if ( r->m_contentHash32 && 
	     replySize>0 && 
	     goodStatus &&
	     r->m_compressReply &&
	     // if we got iframes we can't tell if content changed
	     ! hasIframe2 ) {
		// compute it
		int32_t ch32 = getContentHash32Fast( (unsigned char *)content ,
						  contentLen ,
						  niceness );
		// unchanged?
		if ( ch32 == r->m_contentHash32 ) {
			// record in the stats
			docsPtr     = &g_stats.m_compressUnchangedDocs;
			bytesInPtr  = &g_stats.m_compressUnchangedBytesIn;
			bytesOutPtr = &g_stats.m_compressUnchangedBytesOut;
			// do not send anything back
			replySize = 0;
			// and set error
			savedErr = EDOCUNCHANGED;
		}
	}

	// nuke the content if from flurbit.com website!!
	if ( r->ptr_url &&
	     replySize>0 &&
	     goodStatus &&
	     ! r->m_isSquidProxiedUrl &&
	     strstr ( r->ptr_url,"flurbit.com/" ) ) {
		// note it in log
		log("msg13: got flurbit url: %s",r->ptr_url);
		// record in the stats
		docsPtr     = &g_stats.m_compressUnchangedDocs;
		bytesInPtr  = &g_stats.m_compressUnchangedBytesIn;
		bytesOutPtr = &g_stats.m_compressUnchangedBytesOut;
		// do not send anything back
		replySize = 0;
	}


	// by default assume it has a good date
	int32_t status = 1;

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	// force it good for debugging
	//status = 1;
	// xml set error?
	//if ( status == -1 ) {
	//	// sanity
	//	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	//	// g_errno must have been set!
	//	savedErr = g_errno;
	//	replySize = 0;
	//}
	// these are typically roots!
	if ( status == 1 && 
	     // override HasIFrame with "FullPageRequested" if it has
	     // an iframe, because that is the overriding stat. i.e. if
	     // we ignored if it had iframes, we'd still end up here...
	     ( ! docsPtr || docsPtr == &g_stats.m_compressHasIframeDocs ) &&
	     r->m_compressReply ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressFullPageDocs;
		bytesInPtr  = &g_stats.m_compressFullPageBytesIn;
		bytesOutPtr = &g_stats.m_compressFullPageBytesOut;
	}
	// hey, it had a good date on it...
	else if ( status == 1 && 
		  ! docsPtr &&
		  r->m_compressReply ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressHasDateDocs;
		bytesInPtr  = &g_stats.m_compressHasDateBytesIn;
		bytesOutPtr = &g_stats.m_compressHasDateBytesOut;
	}

	// sanity check
	if ( status != -1 && status != 0 && status != 1 ){char *xx=NULL;*xx=0;}

	if ( r->m_isRobotsTxt && 
	     goodStatus &&
	     ! savedErr &&
	     r->m_compressReply && 
	     httpStatus == 200 ) {
		// . just take out the lines we need...
		// . if no user-agent line matches * or gigabot/flurbot we
		//   will get just a \0 for the reply, replySize=1!
		//char *ua = "ProCogBot";//"EventGuruBot";//r->m_userAgent;
		// take this out until it works for 
		// user-agent: *\ndisallow: blah
		//char *ua = "Gigabot";
		//int32_t uaLen = gbstrlen(ua);
		//replySize = filterRobotsTxt (reply,replySize,&mime,niceness,
		//			     ua,uaLen);
		// record in the stats
		docsPtr     = &g_stats.m_compressRobotsTxtDocs;
		bytesInPtr  = &g_stats.m_compressRobotsTxtBytesIn;
		bytesOutPtr = &g_stats.m_compressRobotsTxtBytesOut;
	}

	// unknown by default
	if ( ! docsPtr ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressUnknownTypeDocs;
		bytesInPtr  = &g_stats.m_compressUnknownTypeBytesIn;
		bytesOutPtr = &g_stats.m_compressUnknownTypeBytesOut;
	}		

	// assume we did not compress it
	bool compressed = false;
	// compress if we should. do not compress if we are original requester
	// because we call gotFinalReply() with the reply right below here.
	// CAUTION: do not compress empty replies.
	// do not bother if savedErr is set because we use sendErrorReply
	// to send that back!
	if ( r->m_compressReply && replySize>0 && ! savedErr ) {
		// how big should the compression buf be?
		int32_t need = sizeof(int32_t) +        // unzipped size
			(int32_t)(replySize * 1.01) + // worst case size
			25;                       // for zlib
		// for 7-zip
		need += 300;
		// back buffer to hold compressed reply
		uint32_t compressedLen;
		char *compressedBuf = (char*)mmalloc(need, "Msg13Zip");
		if ( ! compressedBuf ) {
			g_errno = ENOMEM;
			log("msg13: compression failed1 %s",r->ptr_url);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}

		// store uncompressed length as first four bytes in the
		// compressedBuf
		*(int32_t *)compressedBuf = replySize;
		// the remaining bytes are for data
		compressedLen = need - 4;
		// leave the first 4 bytes to hold the uncompressed size
		int zipErr = gbcompress( (unsigned char*)compressedBuf+4,
					 &compressedLen,
					 (unsigned char*)reply, 
					 replySize);
		if(zipErr != Z_OK) {
			log("spider: had error zipping Msg13 reply. %s "
			    "(%"INT32") url=%s",
			    zError(zipErr),(int32_t)zipErr,r->ptr_url);
			mfree (compressedBuf, need, "Msg13ZipError");
			g_errno = ECORRUPTDATA;
			log("msg13: compression failed2 %s",r->ptr_url);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
		// . free the uncompressed reply so tcpserver does not have to
		// . no, now TcpServer will nuke it!!! or if called from
		//   gotIframeExpansion(), then deleting the xmldoc will nuke
		//   it
		//mfree ( reply , replyAllocSize , "msg13ubuf" );
		// it is toast
		//if ( ts ) ts->m_readBuf = NULL;
		// record the uncompressed size.
		reply          = compressedBuf;
		replySize      = 4 + compressedLen;
		replyAllocSize = need;
		// sanity check
		if ( replySize<0||replySize>100000000 ) { char *xx=NULL;*xx=0;}
		// we did compress it
		compressed = true;
	}

	// record the stats
	if ( docsPtr ) {
		// we download a doc
		*docsPtr = *docsPtr + 1;
		// we spidered it at this size
		*bytesInPtr += originalSize;
		// and spit it back out at this size
		*bytesOutPtr += replySize;
		// and this always, the total
		g_stats.m_compressAllDocs++;
		g_stats.m_compressAllBytesIn  += originalSize;
		g_stats.m_compressAllBytesOut += replySize;
	}

	// store reply in the cache (might be compressed)
	if ( r->m_maxCacheAge > 0 ) { // && ! r->m_parent ) {
		// get the cache
		RdbCache *c = &s_httpCacheOthers;
		// use robots cache if we are a robots.txt file
		if ( r->m_isRobotsTxt ) c = &s_httpCacheRobots;
		// key is based on url hash
		key_t k; k.n1 = 0; k.n0 = r->m_cacheKey;
		// add it, use a generic collection
		c->addRecord ( (collnum_t) 0 , k , reply , replySize );
		// ignore errors caching it
		g_errno = 0;
	}

	// int16_tcut
	UdpServer *us = &g_udpServer;

	// how many have this key?
	int32_t count = s_rt.getCount ( &r->m_cacheKey );
	// sanity check
	if ( count < 1 ) { char *xx=NULL;*xx=0; }

	// send a reply for all waiting in line
	int32_t tableSlot;
	// loop
	for ( ; ( tableSlot = s_rt.getSlot ( &r->m_cacheKey) ) >= 0 ; ) {
		// use this
		int32_t err = 0;
		// set g_errno appropriately
		//if ( ! ts || savedErr ) err = savedErr;
		if ( savedErr ) err = savedErr;
		// sanity check. must be empty on any error
		if ( reply && replySize > 0 && err ) {
			// ETCPIMEDOUT can happen with a partial buf
			if ( err != ETCPTIMEDOUT && 
			     // sometimes zipped content from page
			     // is corrupt... we don't even request
			     // gzipped http replies but they send it anyway
			     err != ECORRUPTHTTPGZIP &&
			     // for proxied https urls
			     err != EPROXYSSLCONNECTFAILED &&
			     // now httpserver::gotDoc's call to
			     // unzipReply() can also set g_errno to
			     // EBADMIME
			     err != EBADMIME &&
			     // this happens sometimes in unzipReply()
			     err != ENOMEM &&
			     // this page had a bad mime
			     err != ECORRUPTHTTPGZIP &&
			     // broken pipe
			     err != EPIPE &&
			     err != EINLINESECTIONS &&
			     // connection reset by peer
			     err != ECONNRESET ) {
				log("http: bad error from httpserver get doc: %s",
				    mstrerror(err));
				char*xx=NULL;*xx=0;
			}
		}
		// replicate the reply. might return NULL and set g_errno
		char *copy          = reply;
		int32_t  copyAllocSize = replyAllocSize;
		// . only copy it if we are not the last guy in the table
		// . no, now always copy it
		if ( --count > 0 && ! err ) {
			copy          = (char *)mdup(reply,replySize,"msg13d");
			copyAllocSize = replySize;
			// oom doing the mdup? i've seen this core us so fix it
			// because calling sendreply_ass with a NULL 
			// 'copy' cores it.
			if ( reply && ! copy ) {
				copyAllocSize = 0;
				err = ENOMEM;
			}
		}
		// this is not freeable
		if ( copy == g_fakeReply ) copyAllocSize = 0;
		// get request
		Msg13Request *r2;
		r2 = *(Msg13Request **)s_rt.getValueFromSlot(tableSlot);
		// get udp slot for this transaction
		UdpSlot *slot = r2->m_udpSlot;
		// remove from list
		s_rt.removeSlot ( tableSlot );
		// send back error?  maybe...
		if ( err ) {
			if ( g_conf.m_logDebugSpider ||
			     g_conf.m_logDebugMsg13 )
				log("proxy: msg13: sending back error: %s "
				    "for url %s with ip %s",
				    mstrerror(err),
				    r2->ptr_url,
				    iptoa(r2->m_urlIp));
			g_udpServer.sendErrorReply ( slot , err );
			continue;
		}
		// for debug for now
		if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
			log("msg13: sending reply for %s",r->ptr_url);
		// send reply
		us->sendReply_ass ( copy,replySize,copy,copyAllocSize, slot );
		// now final udp slot will free the reply, so tcp server
		// no longer has to. set this tcp buf to null then.
		if ( ts && ts->m_readBuf == reply && count == 0 ) 
			ts->m_readBuf = NULL;
	}
	// return now if we sent a regular non-error reply. it will have
	// sent the reply buffer and udpserver will free it when its done
	// transmitting it. 
	//if ( ts && ! savedErr ) return;
	// otherwise, we sent back a quick little error reply and have to
	// free the buffer here now. i think this was the mem leak we were
	// seeing.
	//if ( ! reply ) return;
	// do not let tcpserver free it
	//if ( ts ) ts->m_readBuf = NULL;
	// we free it - if it was never sent over a udp slot
	if ( savedErr && compressed ) 
		mfree ( reply , replyAllocSize , "msg13ubuf" );

	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log("msg13: handled reply ok %s",r->ptr_url);
}


void passOnReply ( void *state , UdpSlot *slot ) {
	// send that back
	Msg13Request *r = (Msg13Request *)state;
	// core for now
	//char *xx=NULL;*xx=0;
	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;

	/*
	// do not pass it on, we are where it stops if this is non-null
	if ( r->m_parent ) {
		r->m_parent->gotForwardedReply ( slot );
		return ;
	}
	*/

	if ( g_errno ) {
		log("spider: error from proxy for %s: %s",
		    r->ptr_url,mstrerror(g_errno));
		g_udpServer.sendErrorReply(r->m_udpSlot, g_errno);
		return;
	}

	// what did he give us?
	char *reply          = slot->m_readBuf;
	int32_t  replySize      = slot->m_readBufSize;
	int32_t  replyAllocSize = slot->m_readBufMaxSize;
	// do not allow "slot" to free the read buf since it is being used
	// as the send buf for "udpSlot"
	slot->m_readBuf     = NULL;
	slot->m_readBufSize = 0;
	// prevent udpserver from trying to free g_fakeReply
	if ( reply == g_fakeReply ) replyAllocSize = 0;
	//int32_t  replyAllocSize = slot->m_readBufSize;
	// just forward it on
	g_udpServer.sendReply_ass (reply,replySize,
				   reply,replyAllocSize,
				   r->m_udpSlot);
}

//
//
// . UTILITY FUNCTIONS for injecting into the "qatest123" collection
// . we need to ensure that the web pages remain constant so we store them
//
//

// . returns true if found on disk in the test subdir
// . returns false with g_errno set on error
// . now that we are lower level in Msg13.cpp, set "ts" not "slot"
bool getTestDoc ( char *u , TcpSocket *ts , Msg13Request *r ) {
	// sanity check
	//if ( strcmp(m_coll,"qatest123") ) { char *xx=NULL;*xx=0; }
	// hash the url into 64 bits
	int64_t h = hash64 ( u , gbstrlen(u) );
	// read the spider date file first
	char fn[300]; 
	File f;

	// default to being from PageInject
	//char *td = "test-page-inject";
	//if ( r->m_testSpiderEnabled ) td = "test-spider";
	//if ( r->m_testParserEnabled ) td = "test-parser";
	//if ( r->m_isPageParser      ) td = "test-page-parser";
	char *td = r->m_testDir;
	//if ( r->m_isPageInject      ) td = "test-page-inject";
	//if ( ! td ) td = "test-page-parser";
	if ( ! td[0] ) { char *xx=NULL;*xx=0; }
	// make http reply filename
	sprintf(fn,"%s/%s/doc.%"UINT64".html",g_hostdb.m_dir,td,h);
	// look it up
	f.set ( fn );
	// try to get it
	if ( ! f.doesExist() ) {
		//if ( g_conf.m_logDebugSpider )
			log("test: doc not found in test cache: %s (%"UINT64")",
			    u,h);
		return false;
	}
	// get size
	int32_t fs = f.getFileSize();
	// error?
	if ( fs == -1 ) 
		return log("test: error getting file size from test");
	// make a buf
	char *buf = (char *)mmalloc ( fs + 1 , "gtd");
	// no mem?
	if ( ! buf ) return log("test: no mem to get html file");
	// open it
	f.open ( O_RDWR );
	// read the HTTP REPLY in
	int32_t rs = f.read ( buf , fs , 0 );
	// not read enough?
	if ( rs != fs ) {
		mfree ( buf,fs,"gtd");
		return log("test: read returned %"INT32" != %"INT32"",rs,fs);
	}
	f.close();
	// null term it
	buf[fs] = '\0';

	// was it error=%"UINT32" ?
	if ( ! strncmp(buf,"errno=",6) ) {
		ts->m_readBuf     = NULL;
		ts->m_readBufSize = 0;
		ts->m_readOffset  = 0;
		g_errno = atol(buf+6);
		// fix mem leak
		mfree ( buf , fs+1 , "gtd" );
		// log it for now
		if ( g_conf.m_logDebugSpider )
			log("test: GOT ERROR doc in test cache: %s (%"UINT64") "
			    "[%s]",u,h, mstrerror(g_errno));
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		return true;
	}

	// log it for now
	//if ( g_conf.m_logDebugSpider )
		log("test: GOT doc in test cache: %s (qa/doc.%"UINT64".html)",
		    u,h);
		
	//fprintf(stderr,"scp gk252:/e/test-spider/doc.%"UINT64".* /home/mwells/gigablast/test-parser/\n",h);

	// set the slot up now
	//slot->m_readBuf        = buf;
	//slot->m_readBufSize    = fs;
	//slot->m_readBufMaxSize = fs;
	ts->m_readBuf     = buf;
	ts->m_readOffset  = fs ;
	// if we had something, trim off the \0 so msg13.cpp can add it back
	if ( fs > 0 ) ts->m_readOffset--;
	ts->m_readBufSize = fs + 1;
	return true;
}

bool getTestSpideredDate ( Url *u , int32_t *origSpideredDate , char *testDir ) {
	// hash the url into 64 bits
	int64_t uh64 = hash64(u->getUrl(),u->getUrlLen());
	// read the spider date file first
	char fn[2000]; 
	File f;
	// get the spider date then
	sprintf(fn,"%s/%s/doc.%"UINT64".spiderdate.txt",
		g_hostdb.m_dir,testDir,uh64);
	// look it up
	f.set ( fn );
	// try to get it
	if ( ! f.doesExist() ) return false;
	// get size
	int32_t fs = f.getFileSize();
	// error?
	if ( fs == -1 ) return log("test: error getting file size from test");
	// open it
	f.open ( O_RDWR );
	// make a buf
	char dbuf[200];
	// read the date in (int format)
	int32_t rs = f.read ( dbuf , fs , 0 );
	// sanity check
	if ( rs <= 0 ) { char *xx=NULL;*xx=0; }
	// get it
	*origSpideredDate = atoi ( dbuf );
	// close it
	f.close();
	// note it
	//log("test: read spiderdate of %"UINT32" for %s",*origSpideredDate,
	//    u->getUrl());
	// good to go
	return true;
}

bool addTestSpideredDate ( Url *u , int32_t spideredTime , char *testDir ) {

	// ensure dir exists
	::mkdir(testDir,getDirCreationFlags());

	// set this
	int64_t uh64 = hash64(u->getUrl(),u->getUrlLen());
	// make that into a filename
	char fn[300]; 
	sprintf(fn,"%s/%s/doc.%"UINT64".spiderdate.txt",
		g_hostdb.m_dir,testDir,uh64);
	// look it up
	File f; f.set ( fn );
	// if already there, return now
	if ( f.doesExist() ) return true;
	// make it into buf
	char dbuf[200]; sprintf ( dbuf ,"%"INT32"\n",spideredTime);
	// open it
	f.open ( O_RDWR | O_CREAT );
	// write it now
	int32_t ws = f.write ( dbuf , gbstrlen(dbuf) , 0 );
	// close it
	f.close();
	// panic?
	if ( ws != (int32_t)gbstrlen(dbuf) )
		return log("test: error writing %"INT32" != %"INT32" to %s",ws,
			   (int32_t)gbstrlen(dbuf),fn);
	// close it up
	//f.close();
	return true;
}

// add it to our "qatest123" subdir
bool addTestDoc ( int64_t urlHash64 , char *httpReply , int32_t httpReplySize ,
		  int32_t err , Msg13Request *r ) {

	char fn[300];
	// default to being from PageInject
	//char *td = "test-page-inject";
	//if ( r->m_testSpiderEnabled ) td = "test-spider";
	//if ( r->m_testParserEnabled ) td = "test-parser";
	//if ( r->m_isPageParser      ) td = "test-page-parser";
	//if ( r->m_isPageInject      ) td = "test-page-inject";
	char *td = r->m_testDir;
	if ( ! td[0] ) { char *xx=NULL;*xx=0; }
	// make that into a filename
	sprintf(fn,"%s/%s/doc.%"UINT64".html",g_hostdb.m_dir,td,urlHash64);
	// look it up
	File f; f.set ( fn );
	// if already there, return now
	if ( f.doesExist() ) return true;
	// open it
	f.open ( O_RDWR | O_CREAT );
	// log it for now
	//if ( g_conf.m_logDebugSpider )
	log("test: ADDING doc to test cache: %"UINT64"",urlHash64);

	// write error only?
	if ( err ) {
		char ebuf[256];
		sprintf(ebuf,"errno=%"INT32"\n",err);
		f.write(ebuf,gbstrlen(ebuf),0);
		f.close();
		return true;
	}

	// write it now
	int32_t ws = f.write ( httpReply , httpReplySize , 0 );
	// close it
	f.close();
	// panic?
	if ( ws != httpReplySize )
		return log("test: error writing %"INT32" != %"INT32" to %s",ws,
			   httpReplySize,fn);
	// all done, success
	return true;
}

// . convert html/xml doc in place into a buffer of links, \n separated
// . return new reply size
// . return -1 on error w/ g_errno set on error
// . replySize includes terminating \0??? i dunno
int32_t convertIntoLinks ( char *reply , 
			int32_t replySize , 
			Xml *xml ,
			int32_t niceness ) {
	// the "doQuickSet" is just for us and make things faster and
	// more compressed...
	Links links;
	if ( ! links.set ( false , // useRelNoFollow
			   xml , 
			   NULL , // parentUrl
			   false , // setLinkHashes
			   NULL  , // baseUrl
			   0 , // version (unused)
			   niceness ,
			   false ,
			   NULL,
			   true ) )  // doQuickSet? YES!!!
		return -1;
	// use this to ensure we do not breach
	char *dstEnd = reply + replySize;
	// . store into the new buffer
	// . use gbmemcpy() because it deal with potential overlap issues
	char *dst    = reply;
	// store the thing first
	if ( dst + 100 >= dstEnd ) 
		// if no room, forget it
		return 0;
	// first the mime
	dst += sprintf ( dst , 
			 "HTTP/1.0 200\r\n"
			 "Content-Length: " );
	// save that place
	char *saved = dst;
	// now write a placeholder number
	dst += sprintf ( dst , "00000000\r\n\r\n" );

	// save this
	char *content = dst;
			 
	// this tells xmldoc.cpp what's up
	//gbmemcpy ( dst , "<!--links-->\n", 13 );
	//dst += 13;
	// iterate over the links
	for ( int32_t i = 0 ; i < links.m_numLinks ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// get link
		char *str = links.getLink(i);
		// get size
		int32_t len = links.getLinkLen(i);
		// ensure no breach. if so, return now
		if ( dst + len + 2 > dstEnd ) return dst - reply;
		// lead it
		gbmemcpy ( dst, "<a href=", 8 );
		dst += 8;
		// copy over, should be ok with overlaps
		gbmemcpy ( dst , str , len );
		dst += len;
		// end tag and line
		gbmemcpy ( dst , "></a>\n", 6 );
		dst += 6;
	}
	// null term it!
	*dst++ = '\0';
	// content length
	int32_t clen = dst - content - 1;
	// the last digit
	char *dptr = saved + 7;
	// store it up top in the mime header
	for ( int32_t x = 0 ; x < 8 ; x++ ) {
		//if ( clen == 0 ) *dptr-- = ' ';
		if ( clen == 0 ) break;
		*dptr-- = '0' + (clen % 10);
		clen /= 10;
	}
	// the new replysize is just this plain list of links
	return dst - reply;
}

// returns true if <iframe> tag in there
bool hasIframe ( char *reply, int32_t replySize , int32_t niceness ) {
	if ( ! reply || replySize <= 0 ) return false;
	char *p = reply;
	// exclude \0
	char *pend = reply + replySize - 1;
	for ( ; p < pend ; p++ ) {
		QUICKPOLL(niceness);
		if ( *p != '<' ) continue;
		if ( to_lower_a (p[1]) != 'i' ) continue;
		if ( to_lower_a (p[2]) != 'f' ) continue;
		if ( to_lower_a (p[3]) != 'r' ) continue;
		if ( to_lower_a (p[4]) != 'a' ) continue;
		if ( to_lower_a (p[5]) != 'm' ) continue;
		if ( to_lower_a (p[6]) != 'e' ) continue;
		return true;
	}
	return false;
}

// . returns -1 with g_errno set on error
// . returns 0 if has no future date
// . returns 1 if does have future date
// . TODO: for each street/city/state address, whether it is inlined or not,
//   look it up in zak's db that has all the street names and their city/state.
//   if it's in there then set AF_VERIFIED_STREET i guess...
int32_t hasGoodDates ( char *content ,
		    int32_t  contentLen , 
		    Xml *xml , 
		    Words *words,
		    char ctype ,
		    int32_t niceness ) {
	// now scan the text nodes for dates i guess...
	Dates dates;
	if ( ! dates.parseDates ( words ,
				  DF_FROM_BODY ,
				  NULL , // bits
				  NULL , // sections
				  niceness ,
				  NULL ,
				  ctype ) )
		return -1;
	// get the current year/month/etc in utc
	time_t now = getTimeLocal();
	struct tm *timeStruct = gmtime ( &now );
	int32_t year = 1900 + timeStruct->tm_year;
	// day of month. starts at 1.
	int32_t day  = timeStruct->tm_mday;
	// 0 is january. but we use 1 for january in Dates.cpp, so add 1.
	int32_t month = timeStruct->tm_mon + 1;

	bool gotTOD      = false;
	bool gotMonthDow = false;

	Date *d1 = NULL;
	Date *d2 = NULL;

	// scan the dates we got, looking for certain types
	for ( int32_t i = 0 ; i < dates.m_numDatePtrs ; i++ ) {
		// int16_tcut
		Date *di = dates.m_datePtrs[i];
		// skip if nuked
		if ( ! di ) continue;
		// int16_tcut
		datetype_t dt = di->m_hasType;
		// must be a tod month or dow
		if ( !(dt & (DT_TOD|DT_MONTH|DT_DOW)) ) continue;
		// get the date's year
		int32_t diyear = di->m_maxYear;
		if ( (int32_t)di->m_year <= 0 ) diyear = 0;
		// if it has a year but it is old, forget it
		if ( diyear > 0 && diyear < year ) continue;
		// get the date's month
		int32_t dimonth = di->m_month;
		// if has no year but, assuming it was this year, the month
		// and monthday is over
		if ( diyear == year && // this year,before or nonr
		     dimonth == month && // this month
		     di->m_dayNum > 0 &&
		     di->m_dayNum <= day ) 
			continue;
		// the same, but month is any before
		if ( diyear == year && 
		     dimonth > 0 &&
		     dimonth < month ) continue;
		// an unknown year (clock detector kinda)
		if ( diyear == 0 &&
		     dimonth == month &&
		     di->m_dayNum > 0 &&
		     di->m_dayNum <= day )
			continue;
		// recently past date PROBABLY...
		if ( diyear == 0 &&
		     dimonth > 0 &&
		     dimonth < month &&
		     // but more than 3 months back might be referring to
		     // NEXT YEAR!!! so cap it at that
		     dimonth > month - 3 )
			continue;
		// got one
		if ( dt & DT_TOD ) {
			gotTOD = true;
			if ( ! d1 ) d1 = di;
		}
		if ( dt & (DT_MONTH|DT_DOW) ) {
			gotMonthDow = true;
			if ( ! d2 ) d2 = di;
		}
	}
	// none found!
	if ( ! gotTOD      ) return 0;
	if ( ! gotMonthDow ) return 0;

	Addresses aa;
	if ( ! aa.set ( NULL     , // sections
			words    ,
			NULL     , // bits
			NULL     , // tag rec
			NULL     , // url
			0        , // docid
			0     , // collnum
			0        , // domhash32
			0        , // ip
			niceness ,
			NULL     , // pbuf, safebuf
			NULL     , // state
			NULL     , // callback
			ctype    ,
			NULL     , // siteTitleBuf
			0        , // siteTitleBufSize
			NULL     )) // xmldoc ptr
		// return -1 with g_errno set on error
		return -1;
	// scan the addresses
	for ( int32_t i = 0 ; i < aa.m_am.getNumPtrs() ; i++ ) {
		// breathe
                QUICKPOLL(niceness);
		// get it
		Address *ad = (Address *)aa.m_am.getPtr(i);
		// inlined?
		bool inlined = (ad->m_flags & AF_INLINED);
		// that is good enough
		if ( inlined ) return 1;
		// verified somehow?
		bool vs  = ( ad->m_flags & AF_VERIFIED_STREET);
		// that is good too, although how did it get verified?
		if ( vs ) return 1;
	}
	// ok, nothing inlined or verified...
	return 0;
}

char getContentTypeQuick ( HttpMime *mime,
			   char *reply , 
			   int32_t replySize , 
			   int32_t niceness ) {
	char ctype = mime->getContentType();
	char ctype2 = 0;
	if ( replySize>0 && reply ) {
		// mime is start of reply, so skip to content section
		char *content = reply + mime->getMimeLen();
		// defined in XmlDoc.cpp...
		ctype2 = getContentTypeFromContent(content,niceness);
	}
	if ( ctype2 ) ctype = ctype2;
	return ctype;
}

// . return new size, might be zero...
// . use a minimal mime as well
// . keep in same buffer
int32_t filterRobotsTxt ( char *reply , 
		       int32_t replySize , 
		       HttpMime *mime ,
		       int32_t niceness ,
		       char *userAgent ,
		       int32_t  userAgentLen ) {
	// bail if nothing
	if ( ! reply || replySize <= 0 ) return replySize;
	// skip mime
	char *content = reply + mime->getMimeLen();
	char *s = content;
	// end of a line
	char *end;
	char *agent = NULL;
	char *dst = reply;
	// get first user-agent
	for ( ; *s ; s = end ) {
		// breathe
		QUICKPOLL(niceness);
		// record line start
		char *start = s;
		// skip non breaking white space
		while ( *s && (*s == ' ' || *s == '\t') ) s++;
		// skip to next non-empty line
		for ( end = s ; *end && *end != '\n' ; end++ );
		// advance over \n
		if ( *end ) end++;
		// is it a comment line? skip if so
		if ( *s == '#' ) continue;
		// need "user-agent", but eof works too...
		if ( *s ) {
			if ( to_lower_a(s[0]) != 'u' ) continue;
			if ( to_lower_a(s[1]) != 's' ) continue;
			if ( strncasecmp ( s, "user-agent",10 ) ) continue;
		}
		// if we already had an agent and now another one... stop!
		if ( ! *s || agent ) {
			// this is a problem... if somehow its got a smaller
			// mime than us, we can't let our new mime overwrite 
			// the user-agent line we were going to gbmemcpy()
			if ( reply + 16 > agent ) return replySize;
			if ( dst == reply ) {
				gbmemcpy ( dst , "HTTP/1.0 200\r\n\r\n", 16 );
				dst += 16;
			}
			// store the user-agent and following allows/disallows
			gbmemcpy ( dst, agent , start - agent );
			dst += ( start - agent );
			// restart
			agent = NULL;
			// eof?
			if ( ! *s ) break;
		}
		// record line start
		char *lineStart = s;
		// skip over that
		s += 10;
		// then a colon or not!
		for ( ; *s ; s++ ) {
			if ( *s == ':'  ) continue;
			if ( *s == ' '  ) continue;
			if ( *s == '\t' ) continue;
			break;
		}
		// craziness? need a bot name, otherwise, skip the line
		if ( ! is_alnum_a(*s) && *s != '*' ) continue;
		// did the user-agent line match our bot name?
		bool match = false;
		// then the user-agent
		if ( *s == '*' ) match = true;
		if ( strncasecmp(s,userAgent,userAgentLen) == 0 ) match =true;
		/*
		if ( strncasecmp(s,"gigabot",7) == 0 ) match = true;
		if ( strncasecmp(s,"flurbot",7) == 0 ) match = true;
		if ( strncasecmp(s,"eventgurubot",12) == 0 ) match = true;
		if ( strncasecmp(s,"procogbot",8) == 0 ) match = true;
		if ( strncasecmp(s,"probot",6) == 0 ) match = true;
		*/
		// record agent position if we matched!
		if ( match ) agent = lineStart;
		// now a sequence of allow/disallow lines until
		// we hit another user agent
	}
	// if nothing keep it zero
	if ( dst - reply == 0 ) return 0;
	// otherwise null term it. this could be a one byte \0 reply!!! no mime
	*dst++ = '\0';
	// all done, return new replysize...
	return dst - reply;
}

// returns false if blocks, true otherwise
bool getIframeExpandedContent ( Msg13Request *r , TcpSocket *ts ) {

	if ( ! ts ) { char *xx=NULL;*xx=0; }

	int32_t niceness = r->m_niceness;

	// ok, we've an attempt now
	r->m_attemptedIframeExpansion = true;

	// we are doing something to destroy reply, so make a copy of it!
	int32_t copySize = ts->m_readOffset + 1;
	char *copy = (char *)mdup ( ts->m_readBuf , copySize , "ifrmcpy" );
	if ( ! copy ) return true;
	// sanity, must include \0 at the end
	if ( copy[copySize-1] ) { char *xx=NULL;*xx=0; }

	// need a new state for it, use XmlDoc itself
	XmlDoc *xd;
	try { xd = new ( XmlDoc ); }
	catch ( ... ) {
		mfree ( copy , copySize , "ifrmcpy" );
		g_errno = ENOMEM;
		return true;
	}
	mnew ( xd , sizeof(XmlDoc),"msg13xd");

	// make a fake spider request so we can do it
	SpiderRequest sreq;
	sreq.reset();
	strcpy(sreq.m_url,r->ptr_url);
	int32_t firstIp = hash32n(r->ptr_url);
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	sreq.setKey( firstIp,0LL, false );
	sreq.m_isInjecting   = 1; 
	sreq.m_isPageInject  = 1;
	sreq.m_hopCount      = 0;//m_hopCount;
	sreq.m_hopCountValid = 1;
	sreq.m_fakeFirstIp   = 1;
	sreq.m_firstIp       = firstIp;

	// log it now
	if ( g_conf.m_logDebugBuild ) 
		log("scproxy: expanding iframes for %s",r->ptr_url);

	// . use the enormous power of our new XmlDoc class
	// . this returns false with g_errno set on error
	// . sometimes niceness is 0, like when the UdpSlot
	//   gets its niceness converted, (see
	//   UdpSlot::m_converetedNiceness). 
	if ( ! xd->set4 ( &sreq       ,
			  NULL        ,
			  "main", // HACK!! m_coll  ,
			  NULL        , // pbuf
			  // give it a niceness of 1, we have to be
			  // careful since we are a niceness of 0!!!!
			  1, //niceness, // 1 , 
			  NULL , // content ,
			  false, // deleteFromIndex ,
			  0 )) { // forcedIp
		// log it
		log("scproxy: xmldoc set error: %s",mstrerror(g_errno));
		// now nuke xmldoc
		mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
		delete  ( xd );
		// g_errno should be set if that returned false
		return true;
	}

	// . re-set the niceness because it will core if we set it with
	//   a niceness of 0...
	xd->m_niceness = niceness;

	// we already downloaded the httpReply so this is valid. no need
	// to check robots.txt again for that url, but perhaps for the 
	// iframe urls.
	xd->m_isAllowed      = true;
	xd->m_isAllowedValid = true;

	// save stuff for calling gotHttpReply() back later with the
	// iframe expanded document
	xd->m_r   = r;

	// so XmlDoc::getExtraDoc doesn't have any issues
	xd->m_firstIp = 123456;
	xd->m_firstIpValid = true;

	// try using xmldoc to do it
	xd->m_httpReply          = copy;
	xd->m_httpReplySize      = copySize;
	xd->m_httpReplyAllocSize = copySize;
	xd->m_httpReplyValid     = true;

	// we claimed this buffer, do not let TcpServer destroy it!
	//ts->m_readBuf = NULL;//(char *)0x1234;

	// tell it to skip msg13 and call httpServer.getDoc directly
	xd->m_isSpiderProxy = true;

	// do not let XmlDoc::getRedirUrl() try to get old title rec
	xd->m_oldDocValid    = true;
	xd->m_oldDoc         = NULL;
	// can't be NULL, xmldoc uses for g_errno
	xd->ptr_linkInfo1    = (LinkInfo *)0x01; 
	xd->ptr_linkInfo2    = (LinkInfo *)0x01;
	xd->size_linkInfo1   = 0   ;
	xd->size_linkInfo2   = 0   ;
	xd->m_linkInfo1Valid = true;
	xd->m_linkInfo2Valid = true;

	// call this as callback
	xd->setCallback ( xd , gotIframeExpandedContent );

	xd->m_redirUrlValid = true;
	xd->ptr_redirUrl    = NULL;
	xd->size_redirUrl   = 0;

	xd->m_downloadEndTimeValid = true;
	xd->m_downloadEndTime = gettimeofdayInMillisecondsLocal();

	// now get the expanded content
	char **ec = xd->getExpandedUtf8Content();
	// this means it blocked
	if ( ec == (void *)-1 ) {
		//log("scproxy: waiting for %s",r->ptr_url);
		return false;
	}
	// return true with g_errno set
	if ( ! ec ) {
		log("scproxy: iframe expansion error: %s",mstrerror(g_errno));
		// g_errno should be set
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// clean up
	}

	// it did not block so signal gotIframeExpandedContent to not call
	// gotHttpReply()
	//xd->m_r = NULL;

	// hey... it did block and we are stil;l printing this!!
	// it happens when the iframe src is google or bing.. usually maps
	// so i'd think indicative of something special
	if ( g_conf.m_logDebugBuild ) 
		log("scproxy: got iframe expansion without blocking for url=%s"
		    " err=%s",r->ptr_url,mstrerror(g_errno));

	// save g_errno for returning
	int32_t saved = g_errno;

	// this also means that the iframe tag was probably not expanded
	// because it was from google.com or bing.com or had a bad src attribut
	// or bad url in the src attribute. 
	// so we have set m_attemptedIframeExpansion, just recall using
	// the original TcpSocket ptr... and this time we should not be
	// re-called because m_attemptedIframeExpansion is now true
	//gotHttpReply2 ( r, NULL , 0 , 0 , NULL );	

	// we can't be messing with it!! otherwise we'd have to reutrn
	// a new reply size i guess
	if ( xd->m_didExpansion ) { char *xx=NULL;*xx=0; }

	// try to reconstruct ts
	//ts->m_readBuf = xd->m_httpReply;
	// and do not allow xmldoc to free that buf
	//xd->m_httpReply = NULL;

	// now nuke xmldoc
	mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
	delete  ( xd );

	// reinstate g_errno in case mdelete() reset it
	g_errno = saved;

	// no blocking then...
	return true;
}

void gotIframeExpandedContent ( void *state ) {
	// save error in case mdelete nukes it
	int32_t saved = g_errno;

	XmlDoc *xd = (XmlDoc *)state;
	// this was stored in xd
	Msg13Request *r = xd->m_r;

	//log("scproxy: done waiting for %s",r->ptr_url);

	// note it
	if ( g_conf.m_logDebugBuild ) 
		log("scproxy: got iframe expansion for url=%s",r->ptr_url);

	// assume we had no expansion or there was an error
	char *reply          = NULL;
	int32_t  replySize      = 0;

	// . if no error, then grab it
	// . if failed to get the iframe content then m_didExpansion should
	//   be false
	if ( ! g_errno && xd->m_didExpansion ) {
		// original mime should have been valid
		if ( ! xd->m_mimeValid ) { char *xx=NULL;*xx=0; }
		// insert the mime into the expansion buffer! m_esbuf
		xd->m_esbuf.insert2 ( xd->m_httpReply ,
				      xd->m_mime.getMimeLen() ,
				      0 );
		// . get our buffer with the expanded iframes in it
		// . make sure that has the mime in it too
		//reply     = xd->m_expandedUtf8Content;
		//replySize = xd->m_expandedUtf8ContentSize;
		// just to make sure nothing bad happens, null this out
		xd->m_expandedUtf8Content = NULL;
		// this new reply includes the original mime!
		reply     = xd->m_esbuf.getBufStart();
		// include \0? yes.
		replySize = xd->m_esbuf.length() + 1;
		// sanity. must be null terminated
		if ( reply[replySize-1] ) { char *xx=NULL;*xx=0; }
	}
	// if expansion did not pan out, use original reply i guess
	else if ( ! g_errno ) {
		reply     = xd->m_httpReply;
		replySize = xd->m_httpReplySize;
	}

	// log it so we know why we are getting EDNSTIMEDOUT msgs back
	// on the main cluster!
	if ( g_errno )
		log("scproxy: error getting iframe content for url=%s : %s",
		    r->ptr_url,mstrerror(g_errno));
	// sanity check
	if ( reply && reply[replySize-1] != '\0') { char *xx=NULL;*xx=0; }
	// pass back the error we had, if any
	g_errno = saved;
	// . then resume the reply processing up above as if this was the
	//   document that was downloaded. 
	// . PASS g_errno BACK TO THIS if it was set, like ETCPTIMEDOUT
	gotHttpReply2 ( r, reply, replySize , replySize , NULL );

	// no, let's not dup it and pass what we got in, since ts is NULL
	// it should not free it!!!

	// . now destroy it
	// . the reply should have been sent back as a msg13 reply either
	//   as a normal reply or an error reply
	// . nuke out state then, including the xmldoc
	// . was there an error, maybe a TCPTIMEDOUT???
	mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
	delete  ( xd );
}

#define DELAYPERBAN 500

// how many milliseconds should spiders use for a crawldelay if
// ban was detected and no proxies are being used.
#define AUTOCRAWLDELAY 5000

// returns true if we queue the request to download later
bool addToHammerQueue ( Msg13Request *r ) {

	// sanity
	if ( ! r->m_udpSlot ) { char *xx=NULL;*xx=0; }

	// skip if not needed
	if ( r->m_skipHammerCheck ) return false;

	// . make sure we are not hammering an ip
	// . returns 0 if currently downloading a url from that ip
	// . returns -1 if not found
	int64_t last = s_hammerCache.getLongLong(0,r->m_firstIp,-1,true);
	// get time now
	int64_t nowms = gettimeofdayInMilliseconds();
	// how long has it been since last download START time?
	int64_t waited = nowms - last;

	int32_t crawlDelayMS = r->m_crawlDelayMS;

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	bool canUseProxies = false;
	if ( cr && cr->m_automaticallyUseProxies ) canUseProxies = true;
	if ( r->m_forceUseFloaters               ) canUseProxies = true;
	//if ( g_conf.m_useProxyIps          ) canUseProxies = true;
	//if ( g_conf.m_automaticallyUseProxyIps ) canUseProxies = true;

	// if no proxies listed, then it is pointless
	if ( ! g_conf.m_proxyIps.hasDigits() ) canUseProxies = false;

	// if not using proxies, but the ip is banning us, then at least 
	// backoff a bit
	if ( cr && 
	     r->m_urlIp !=  0 &&
	     r->m_urlIp != -1 &&
	     cr->m_automaticallyBackOff &&
	     // and it is in the twitchy table
	     isIpInTwitchyTable ( cr , r->m_urlIp ) ) {
		// and no proxies are available to use
		//! canUseProxies ) {
		// then just back off with a crawldelay of 3 seconds
		if ( ! canUseProxies && crawlDelayMS < AUTOCRAWLDELAY )
			crawlDelayMS = AUTOCRAWLDELAY;
		// mark this so we do not retry pointlessly
		r->m_wasInTableBeforeStarting = true;
		// and obey crawl delay
		r->m_skipHammerCheck = false;
	}


	// . if we got a proxybackoff base it on # of banned proxies for urlIp
	// . try to be more sensitive for more sensitive website policies
	// . we don't know why this proxy was banned, or if we were 
	//   responsible, or who banned it, but be more sensitive anyway
	if ( //r->m_hammerCallback == downloadTheDocForReals3b &&
	     r->m_numBannedProxies &&
	     r->m_numBannedProxies * DELAYPERBAN > crawlDelayMS ) {
		crawlDelayMS = r->m_numBannedProxies * DELAYPERBAN;
		if ( crawlDelayMS > MAX_PROXYCRAWLDELAYMS )
			crawlDelayMS = MAX_PROXYCRAWLDELAYMS;
	}

	// set the crawldelay we actually used when downloading this
	//r->m_usedCrawlDelay = crawlDelayMS;

	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: got timestamp of %"INT64" from "
		    "hammercache (waited=%"INT64" crawlDelayMS=%"INT32") "
		    "for %s"
		    ,last
		    ,waited
		    ,crawlDelayMS
		    ,iptoa(r->m_firstIp));

	bool queueIt = false;
	if ( last > 0 && waited < crawlDelayMS ) queueIt = true;
	// a "last" of 0 means currently downloading
	if ( crawlDelayMS > 0 && last == 0LL ) queueIt = true;
	// a last of -1 means not found. so first time i guess.
	if ( last == -1 ) queueIt = false;
	// ignore it if from iframe expansion etc.
	if ( r->m_skipHammerCheck ) queueIt = false;

	// . queue it up if we haven't waited int32_t enough
	// . then the functionr, scanHammerQueue(), will re-eval all
	//   the download requests in this hammer queue every 10ms. 
	// . it will just lookup the lastdownload time in the cache,
	//   which will store maybe a -1 if currently downloading...
	if ( queueIt ) {
		// debug
		log(LOG_INFO,
		    "spider: adding %s to crawldelayqueue cd=%"INT32"ms "
		    "ip=%s",
		    r->ptr_url,crawlDelayMS,iptoa(r->m_urlIp));
		// save this
		//r->m_udpSlot = slot; // this is already saved!
		r->m_nextLink = NULL;
		// we gotta update the crawldelay here in case we modified
		// it in the above logic.
		r->m_crawlDelayMS = crawlDelayMS;
		// when we stored it in the hammer queue
		r->m_stored = nowms;
		// add it to queue
		if ( ! s_hammerQueueHead ) {
			s_hammerQueueHead = r;
			s_hammerQueueTail = r;
		}
		else {
			s_hammerQueueTail->m_nextLink = r;
			s_hammerQueueTail = r;
		}
		return true;
	}
			
	// if we had it in cache check the wait time
	if ( last > 0 && waited < crawlDelayMS ) {
		log("spider: hammering firstIp=%s url=%s "
		    "only waited %"INT64" ms of %"INT32" ms",
		    iptoa(r->m_firstIp),r->ptr_url,waited,
		    crawlDelayMS);
		// this guy has too many redirects and it fails us...
		// BUT do not core if running live, only if for test
		// collection
		// for now disable it seems like 99.9% good... but
		// still cores on some wierd stuff...
		// if doing this on qatest123 then core
		if(r->m_useTestCache ) { // && r->m_firstIp!=-1944679785 ) {
			char*xx = NULL; *xx = 0; }
	}
	// store time now
	//s_hammerCache.addLongLong(0,r->m_firstIp,nowms);
	// note it
	//if ( g_conf.m_logDebugSpider )
	//	log("spider: adding download end time of %"UINT64" for "
	//	    "firstIp=%s "
	//	    "url=%s "
	//	    "to msg13::hammerCache",
	//	    nowms,iptoa(r->m_firstIp),r->ptr_url);
	// clear error from that if any, not important really
	g_errno = 0;
	return false;
}

// call this once every 10ms to launch queued up download requests so that
// we respect crawl delay for sure
void scanHammerQueue ( int fd , void *state ) {

	if ( ! s_hammerQueueHead ) return;

	int64_t nowms = gettimeofdayInMilliseconds();

 top:

	Msg13Request *r = s_hammerQueueHead;
	if ( ! r ) return;

	Msg13Request *prev = NULL;
	int64_t waited = -1LL;
	Msg13Request *nextLink = NULL;

	//bool useProxies = true;
	// user can turn off proxy use with this switch
	//if ( ! g_conf.m_useProxyIps ) useProxies = false;
	// we gotta have some proxy ips that we can use
	//if ( ! g_conf.m_proxyIps.hasDigits() ) useProxies = false;


	// scan down the linked list of queued of msg13 requests
	for ( ; r ; prev = r , r = nextLink ) { 

		// downloadTheDocForReals() could free "r" so save this here
		nextLink = r->m_nextLink;

		int64_t last;
		last = s_hammerCache.getLongLong(0,r->m_firstIp,30,true);
		// is one from this ip outstanding?
		if ( last == 0LL && r->m_crawlDelayFromEnd ) continue;


		int32_t crawlDelayMS = r->m_crawlDelayMS;

		// . if we got a proxybackoff base it on # of banned proxies 
		// . try to be more sensitive for more sensitive website policy
		// . we don't know why this proxy was banned, or if we were 
		//   responsible, or who banned it, but be more sensitive
		if ( //useProxies && 
		     r->m_numBannedProxies &&
		     r->m_hammerCallback == downloadTheDocForReals3b )
			crawlDelayMS = r->m_numBannedProxies * DELAYPERBAN;

		// download finished? 
		if ( last > 0 ) {
		        waited = nowms - last;
			// but skip if haven't waited int32_t enough
			if ( waited < crawlDelayMS ) continue;
		}
		// debug
		//log("spider: downloading %s from crawldelay queue "
		//    "waited=%"INT64"ms crawldelay=%"INT32"ms", 
		//    r->ptr_url,waited,r->m_crawlDelayMS);

		// good to go
		//downloadTheDocForReals ( r );

		// sanity check
		if ( ! r->m_hammerCallback ) { char *xx=NULL;*xx=0; }

		// callback can now be either downloadTheDocForReals(r)
		// or downloadTheDocForReals3b(r) if it is waiting after 
		// getting a ProxyReply that had a m_proxyBackoff set

		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: calling hammer callback for "
			    "%s (timestamp=%"INT64",waited=%"INT64",crawlDelayMS=%"INT32")",
			    r->ptr_url,
			    last,
			    waited,
			    crawlDelayMS);

		//
		// it should also add the current time to the hammer cache
		// for r->m_firstIp
		r->m_hammerCallback ( r );

		//
		// remove from future scans
		//
		if ( prev ) 
			prev->m_nextLink = nextLink;

		if ( s_hammerQueueHead == r )
			s_hammerQueueHead = nextLink;

		if ( s_hammerQueueTail == r )
			s_hammerQueueTail = prev;

		// if "r" was freed by downloadTheDocForReals() then
		// in the next iteration of this loop, "prev" will point
		// to a freed memory area, so start from the top again
		goto top;

		// try to download some more i guess...
	}
}

bool addNewProxyAuthorization ( SafeBuf *req , Msg13Request *r ) {

	if ( ! r->m_proxyIp   ) return true;
	if ( ! r->m_proxyPort ) return true;

	// get proxy from list to get username/password
	SpiderProxy *sp = getSpiderProxyByIpPort (r->m_proxyIp,r->m_proxyPort);

	// if none required, all done
	if ( ! sp->m_usernamePwd ) return true;
	// strange?
	if ( req->length() < 8 ) return false;
	// back up over final \r\n
	req->m_length -= 2 ;
	// insert it
	req->safePrintf("Proxy-Authorization: Basic ");
	req->base64Encode ( sp->m_usernamePwd );
	req->safePrintf("\r\n");
	req->safePrintf("\r\n");
	req->nullTerm();
	return true;
}

// When the Msg13Request::m_isSquidProxiedUrl bit then request we got is
// using us like a proxy, so Msg13Request::m_url is in reality a complete
// HTTP request mime. so in that case we have to call this code to
// fix the HTTP request before sending it to its final destination.
//
// Remove "Proxy-authorization: Basic abcdefghij\r\n"
void stripProxyAuthorization ( char *squidProxiedReqBuf ) {
	//
	// remove the proxy authorization that has the username/pwd
	// so the websites we download the url from do not see it in the
	// http request mime
	//
 loop:
	// include space so it won't match anything in url
	char *needle = "Proxy-Authorization: ";
	char *s = gb_strcasestr ( squidProxiedReqBuf , needle );
	if ( ! s ) return;
	// find next \r\n
	char *end = strstr ( s , "\r\n");
	if ( ! end ) return;
	// bury the \r\n as well
	end += 2;
	// bury that string
	int32_t reqLen = gbstrlen(squidProxiedReqBuf);
	char *reqEnd = squidProxiedReqBuf + reqLen;
	// include \0, so add +1
	gbmemcpy ( s ,end , reqEnd-end + 1);
	// bury more of them
	goto loop;
}


// . convert "GET http://xyz.com/abc" to "GET /abc"
// . TODO: add "Host:xyz.con\r\n" ?
void fixGETorPOST ( char *squidProxiedReqBuf ) {
	char *s = strstr ( squidProxiedReqBuf , "GET http" );
	int32_t slen = 8;
	if ( ! s ) {
		s = strstr ( squidProxiedReqBuf , "POST http");
		slen = 9;
	}
	if ( ! s ) {
		s = strstr ( squidProxiedReqBuf , "HEAD http");
		slen = 9;
	}
	if ( ! s ) return;
	// point to start of http://...
	char *httpStart = s + slen - 4;
	// https?
	s += slen;
	if ( *s == 's' ) s++;
	// skip ://
	if ( *s++ != ':' ) return;
	if ( *s++ != '/' ) return;
	if ( *s++ != '/' ) return;
	// skip until / or space or \r or \n or \0
	for ( ; *s && ! is_wspace_a(*s) && *s != '/' ; s++ );
	// bury the http://xyz.com part now
	char *reqEnd = squidProxiedReqBuf + gbstrlen(squidProxiedReqBuf);
	// include the terminating \0, so add +1
	gbmemcpy ( httpStart , s , reqEnd - s + 1 );
	// now make HTTP/1.1 into HTTP/1.0
	char *hs = strstr ( httpStart , "HTTP/1.1" );
	if ( ! hs ) return;
	hs[7] = '0';
}

// . sets Msg13Request m_proxiedUrl and m_proxiedUrlLen
bool setProxiedUrlFromSquidProxiedRequest ( Msg13Request *r ) {

	// this is actually the entire http request mime, not a url
	//char *squidProxiedReqBuf = r->ptr_url;

	// int16_tcut. this is the actual squid request like
	// "CONNECT www.youtube.com:443 HTTP/1.1\r\nProxy-COnnection: ... "
	// or
	// "GET http://www.youtube.com/..."
	char *s = r->ptr_url;
	char *pu = NULL;

	if ( strncmp ( s , "GET http" ,8 ) == 0 ) 
		pu = s + 4;
	else if ( strncmp ( s , "POST http" ,9 ) == 0 )
		pu = s + 5;
	else if ( strncmp ( s , "HEAD http" ,9 ) == 0 )
		pu = s + 5;
	// this doesn't always have http:// usually just a hostname
	else if ( strncmp ( s , "CONNECT " ,8 ) == 0 )
		pu = s + 8;

	if ( ! pu ) return false;

	r->m_proxiedUrl = pu;

	// find end of it
	char *p = r->m_proxiedUrl;
	for ( ; *p && !is_wspace_a(*p) ; p++ );

	r->m_proxiedUrlLen = p - r->m_proxiedUrl;

	return true;
}

// . for the page cache we hash the url and the cookie to make the cache key
// . also the GET/POST method i guess
// . returns 0 on issues
int64_t computeProxiedCacheKey64 ( Msg13Request *r ) {

	// hash the url
	char *start = r->m_proxiedUrl;

	// how can this happen?
	if ( ! start ) {
		log("proxy: no proxied url");
		return 0LL;
	}

	// skip http:// or https://
	// skip forward
	char *s = start;
	if ( strncmp(s,"http://",7) == 0 ) s += 7;
	if ( strncmp(s,"https://",8) == 0 ) s += 8;
	// skip till we hit end of url
	// skip until / or space or \r or \n or \0
	char *cgi = NULL;
	for ( ; *s && ! is_wspace_a(*s)  ; s++ ) {
		if ( *s == '?' && ! cgi ) cgi = s; }
	// hash the url
	int64_t h64 = hash64 ( start , s - start );


	//
	// if file extension implies it is an image, do not hash cookie
	//
	char *extEnd = NULL;
	if ( cgi ) extEnd = cgi;
	else       extEnd = s;
	char *ext = extEnd;
	for ( ; ext>extEnd-6 && ext>start && *ext!='.' && *ext!='/' ; ext-- );
	if ( *ext == '.' && ext+1 < extEnd ) {
		HttpMime mime;
		const char *cts;
		ext++; // skip over .
		cts = mime.getContentTypeFromExtension ( ext , extEnd-ext );
		if ( strncmp(cts,"image/",6) == 0 ) return h64;
	}

	// this is actually the entire http request mime, not a url
	char *squidProxiedReqBuf = r->ptr_url;
	// now for cookie
	s = strstr ( squidProxiedReqBuf , "Cookie: ");
	// if not there, just return url hash
	if ( ! s ) return h64;
	// save start
	start = s + 8;
	// skip till we hit end of cookie line
	for ( ; *s && *s != '\r' && *s != '\n' ; s++ );
	// incorporate cookie hash
	h64 = hash64 ( start , s - start , h64 );

	//log("debug: cookiehash=%"INT64"",hash64(start,s-start));

	return h64;
}

/*
static void sendBackInlineSectionVotingBuf ( void *xd ) ;

// . returns false if blocks and will call gotHttpReply2() when done
// . returns true if does not block
// . returns true and sets g_errno on error
// . insert sectiondb data into various html tags
// . <div id=poop> --> <div id=poop a=10 n=100> 
//   where "n" is the # of times the tag occurred on other pages from this site
//   and "a" is the # of those times that it had the same inner content.
bool markupServerReply ( Msg13Request *r , TcpSocket *ts ) {

	char *httpReply = ts->m_readBuf;
	int32_t  httpReplyLen = ts->m_readOffset;

	HttpMime mime;
	mime.set ( httpReply , httpReplyLen , NULL );

	// return NULL if we did nothing
	if ( mime.getHttpStatus() != 200 ) 
		return true;

	if ( mime.getContentType() != CT_HTML )
		return true;

	// make a new xmldoc class to handle the heavy lifting
	XmlDoc *xd;
	try { xd = new ( XmlDoc ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		return true;
	}
	mnew ( xd , sizeof(XmlDoc),"msg13xd");

	//char *content = mime.getContent();

	// if this doesn't copy the content we need to
	xd->m_httpReply = httpReply;
	xd->m_httpReplySize = ts->m_readOffset + 1;
	xd->m_httpReplyAllocSize = ts->m_readBufSize;
	xd->m_httpReplyValid = true;

	// tell ts not to free it now! xmldoc will do that.
	ts->m_readBuf = NULL;

	xd->m_redirError = 0;
	xd->m_redirErrorValid = true;

	xd->m_redirUrlPtr = NULL;
	xd->m_redirUrlValid = true;
	
	xd->m_niceness = 0;

	xd->m_firstUrl.set ( r->m_proxiedUrl , r->m_proxiedUrlLen );
	xd->m_firstUrlValid = true;
	xd->ptr_firstUrl = xd->m_firstUrl.getUrl();
	xd->m_ip = r->m_urlIp;
	xd->m_ipValid = true;

	xd->m_skipIframeExpansion = true;

	// . try to use the global index for this
	// . the main collection will now hold the html only, no json, and
	//   the section markup is done on the html, not the json
	CollectionRec *cr = g_collectiondb.getRec ( "main");//"GLOBAL-INDEX" );
	//if ( ! cr ) cr = g_collectiondb.getRec ( "test" ); // tmp hack
	//if ( ! cr ) cr = g_collectiondb.getRec ( "main" );
	if ( cr ) {
		xd->m_collnum = cr->m_collnum;
		xd->m_collnumValid = true;
	}
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
		delete  ( xd );
		return true;
	}


	// hack stash this for use by sendBackInlineSectionVotingBuf
	xd->m_hsr = r;

	// when it has the final output, it calls this
	xd->m_callback1 = sendBackInlineSectionVotingBuf;
	xd->m_state     = xd;

	// . xd uses this callback internally to get the markup
	//   but when done it calls xt->m_xd.m_callback.
	//   actually this function should set these. so comment out here.
	// . returns false if blocked
	SafeBuf *vb = xd->getInlineSectionVotingBuf();
	// we blocked, return true. vb is NULL with g_errno set on error.
	if ( vb == (void *)-1 ) return false;

	sendBackInlineSectionVotingBuf ( xd );

	// so gotHttpReply2() is not called again by our parent caller
	return false;
}

void sendBackInlineSectionVotingBuf ( void *state ) {

	XmlDoc *xd = (XmlDoc *)state;

	// error?
	int32_t saved = g_errno;

	SafeBuf *buf = &xd->m_inlineSectionVotingBuf;

	// steal it. this should now start with the original http mime
	char *reply          = buf->getBufStart();
	int32_t  replySize      = buf->length() + 1; // include \0
	int32_t  replyAllocSize = buf->getCapacity();

	// we hack stashed this. is just the original udpslot readbuf
	Msg13Request *r = xd->m_hsr;

	// totally empty?
	//if ( replySize <= 1 ) {
	// 	log("msg13: got empty inline section votingbuf for %s",
	// 	    r->m_url);
	// 	reply = "\0";
	// 	replySize = 1;
	// }

	// only steal the buf if no error happened
	if ( ! saved ) xd->m_inlineSectionVotingBuf.detachBuf();

	mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
	delete  ( xd );

	// did we have some error?
	if ( saved ) {
		log("msg13: error making inline section voting buf: %s",
		    mstrerror(g_errno));
		// re-write the error for sending back to client
		saved = EINLINESECTIONS;
		//return;
	}

	// and resume just like we got the reply from the webserver
	//mfree ( ts->m_alloc , ts->m_allocSize , "mkupts");

	// reinstate error
	g_errno = saved;

	// we set ts to NULL, i guess it was probably already freed up
	// any how by now since the sectiondb lookups probably blocked in the
	// call the XmlDoc::getInlineSectionVotingBuf()
	gotHttpReply2 ( r, reply, replySize , replyAllocSize , NULL ); // ts

	// it didn't block, send back the html with the inlined section
	// voting info
	// g_udpServer.sendReply_ass (reply,
	// 			   replySize,
	// 			   reply,
	// 			   replyAllocSize,
	// 			   r->m_udpSlot);
}
*/

static char *s_agentList[] = {
	"Mozilla/5.0 (compatible; U; ABrowse 0.6; Syllable) AppleWebKit/420+ (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; ABrowse 0.4; Syllable)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; Acoo Browser 1.98.744; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Acoo Browser; GTB5; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; Maxthon; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Acoo Browser; GTB5;",
	"Mozilla/4.0 (compatible; Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6; Acoo Browser; .NET CLR 1.1.4322; .NET CLR 2.0.50727); Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 2.0.50727; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; Acoo Browser; GTB6; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; Acoo Browser; GTB5; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6; Acoo Browser; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; Acoo Browser; GTB5; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Acoo Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Acoo Browser; GTB5; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Acoo Browser; GTB5; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Acoo Browser; InfoPath.2; .NET CLR 2.0.50727; Alexa Toolbar)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Acoo Browser; .NET CLR 2.0.50727; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Acoo Browser; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; FDM; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Acoo Browser; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Acoo Browser; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; America Online Browser 1.1; Windows NT 5.1; (R1 1.5); .NET CLR 2.0.50727; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; America Online Browser 1.1; rev1.5; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; America Online Browser 1.1; rev1.5; Windows NT 5.1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; America Online Browser 1.1; rev1.5; Windows NT 5.1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 4.0; InfoPath.1; .NET CLR 2.0.50727; Media Center PC 3.0; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; America Online Browser 1.1; rev1.2; Windows NT 5.1; SV1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1; HbTools 4.7.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1; FunWebProducts; .NET CLR 1.1.4322; InfoPath.1; HbTools 4.8.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1; FunWebProducts; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 3.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1; .NET CLR 1.1.4322; HbTools 4.7.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 3.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; SV1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; FunWebProducts; (R1 1.5); HbTools 4.7.7)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1; FunWebProducts)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; rev1.5; Windows NT 5.1; SV1; FunWebProducts; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; America Online Browser 1.1; rev1.5; Windows NT 5.1; SV1; .NET CLR 1.1.4322; InfoPath.1)",
	"Mozilla/5.0 (compatible; MSIE 9.0; AOL 9.7; AOLBuild 4343.19; Windows NT 6.1; WOW64; Trident/5.0; FunWebProducts)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.7; AOLBuild 4343.27; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.7; AOLBuild 4343.21; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET4.0C; .NET4.0E)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.7; AOLBuild 4343.19; Windows NT 5.1; Trident/4.0; GTB7.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.7; AOLBuild 4343.19; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET4.0C; .NET4.0E)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.7; AOLBuild 4343.19; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET4.0C; .NET4.0E)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.5004; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.5001; Windows NT 5.1; Trident/4.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.5000; Windows NT 5.1; Trident/4.0; FunWebProducts)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.5000; Windows NT 5.1; Trident/4.0; .NET4.0C; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.27; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.27; Windows NT 5.1; Trident/4.0; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 4.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.17; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.168; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.3; MS-RTC LM 8)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.168; Windows NT 5.1; Trident/4.0; GTB7.1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.130; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.130; Windows NT 5.1; Trident/4.0; FunWebProducts; GTB6.6; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; yie8)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.12; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.12; Windows NT 5.1; Trident/4.0; GTB6.3)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.124; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.122; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.3; MS-RTC LM 8)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.122; Windows NT 5.1; Trident/4.0; FunWebProducts)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.111; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET4.0C; .NET4.0E)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.110; Windows NT 5.1; Trident/4.0; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.6; AOLBuild 4340.104; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.6; AOLBuild 4340.128; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.5; AOLBuild 4337.43; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.21022; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.5; AOLBuild 4337.29; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.21022; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.93; Windows NT 5.1; Trident/4.0; DigExt; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.89; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.81; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.81; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618) (Compatible; ; ; Trident/4.0; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.81; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.80; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.53; Windows NT 6.0; FunWebProducts; GTB6; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.43; Windows NT 6.0; WOW64; GTB5; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.43; Windows NT 5.1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 9.1; AOLBuild 4334.5006; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30)",
	"Mozilla/5.0 (compatible; MSIE 9.0; AOL 9.0; Windows NT 6.0; Trident/5.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.0; AOLBuild 4327.5201; Windows NT 6.0; WOW64; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.30729; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; InfoPath.2; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; Trident/4.0; FunWebProducts; GTB6.4; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 1.1.4322; .NET CLR 3.5.30729; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; .NET CLR 3.0.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; Seekmo 10.0.406.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; FunWebProducts; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; InfoPath.2; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; FunWebProducts; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; Seekmo 10.0.341.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 6.0; FunWebProducts; GTB5; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; Trident/4.0; GTB6; FunWebProducts; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; GTB5; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 4.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; GTB5; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; GTB5; .NET CLR 1.1.4322; .NET CLR 2.0.50727; OfficeLiveConnector.1.3; OfficeLivePatch.0.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; GTB5; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; GTB5; .NET CLR 1.0.3705; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.0; Windows NT 5.1; FunWebProducts; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) )",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 8.0; Windows NT 5.1; GTB5; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 8.0; Windows NT 5.1; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 8.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1; .NET CLR 3.0.04506.30)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 8.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 8.0; Windows NT 5.1; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 8.0; Windows NT 5.1; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 8.0; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; YComp 5.0.0.0; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; SV1; (R1 1.3); .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; SV1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; Q312461)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; FunWebProducts; SV1; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; FunWebProducts; SV1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; FunWebProducts)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1; (R1 1.3))",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 8.0; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 7.0; Windows NT 5.1; FunWebProducts)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 7.0; Windows NT 5.1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 7.0; Windows NT 5.1) (Compatible; ; ; Trident/4.0; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 7.0; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; YComp 5.0.2.6; Hotbar 4.2.8.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; YComp 5.0.2.4)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; SV1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; SV1; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; SV1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; Q312461; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; Q312461)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; Hotbar 4.2.8.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; Hotbar 4.1.7.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows 98; Win 9x 4.90; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows 98; Win 9x 4.90; (R1 1.3))",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 7.0; Windows 98; Win 9x 4.90)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 6.0; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 6.0; Windows 98; Win 9x 4.90)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 6.0; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 6.0; Windows 95)",
	"Mozilla/4.0 (compatible; MSIE 5.0; AOL 6.0; Windows 98; DigExt; YComp 5.0.2.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 5.0; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 5.0; Windows 98; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; AOL 5.0; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 5.0; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 5.0; Windows 98; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 5.0; Windows 98; Win 9x 4.90)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 5.0; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 5.0; Windows 95)",
	"Mozilla/4.0 (compatible; MSIE 5.0; AOL 5.0; Windows 98; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 5.0; AOL 5.0; Windows 95; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 5.0; AOL 5.0; Windows 95)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 5.0; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 4.0; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 5.5; AOL 4.0; Windows 95)",
	"Mozilla/4.0 (compatible; MSIE 5.0; AOL 4.0; Windows 98; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 5.01; AOL 4.0; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 4.01; AOL 4.0; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 4.01; AOL 4.0; Windows 95)",
	"Mozilla/4.0 (compatible; MSIE 4.01; AOL 4.0; Mac_68K)",
	"Mozilla/5.0 (X11; U; Linux; de-DE) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.8.0",
	"Mozilla/5.0 (Windows; U; ; en-NZ) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.8.0",
	"Mozilla/5.0 (X11; U; Linux; ru-RU) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.6 (Change: 802 025a17d)",
	"Mozilla/5.0 (X11; U; Linux; fi-FI) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.6 (Change: 754 46b659a)",
	"Mozilla/5.0 (X11; U; Linux; en-US) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.6",
	"Mozilla/5.0 (X11; U; Linux; en-US) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.6 (Change: )",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.6 (Change: )",
	"Mozilla/5.0 (X11; U; Linux; pt-PT) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; nb-NO) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; it-IT) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: 413 12f13f8)",
	"Mozilla/5.0 (X11; U; Linux; it-IT) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; hu-HU) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: 388 835b3b6)",
	"Mozilla/5.0 (X11; U; Linux; hu-HU) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; fr-FR) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; es-ES) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: 388 835b3b6)",
	"Mozilla/5.0 (X11; U; Linux; en-US) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; en-GB) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: 388 835b3b6)",
	"Mozilla/5.0 (X11; U; Linux; en-GB) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; de-DE) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4",
	"Mozilla/5.0 (X11; U; Linux; cs-CZ) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: 333 41e3bc6)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: )",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-DE) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: )",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; pt-BR) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: )",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.4 (Change: )",
	"Mozilla/5.0 (X11; U; Linux; en-GB) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.3 (Change: 239 52c6958)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.3 (Change: 287 c9dfb30)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-BE) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.3 (Change: 287 c9dfb30)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.3 (Change: 287 c9dfb30)",
	"Mozilla/5.0 (X11; U; Linux; sk-SK) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2 (Change: 0 )",
	"Mozilla/5.0 (X11; U; Linux; nb-NO) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2 (Change: 0 )",
	"Mozilla/5.0 (X11; U; Linux; es-CR) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2 (Change: 0 )",
	"Mozilla/5.0 (X11; U; Linux; en-US) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2 (Change: 189 35c14e0)",
	"Mozilla/5.0 (X11; U; Linux; en-US) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2 (Change: 0 )",
	"Mozilla/5.0 (X11; U; Linux; de-DE) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2 (Change: 0 )",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-DE) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl-NL) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-CH) AppleWebKit/523.15 (KHTML, like Gecko, Safari/419.3) Arora/0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Arora/0.11.0 Safari/533.3",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.34 (KHTML, like Gecko) Arora/0.11.0 Safari/534.34",
	"Mozilla/5.0 (X11; U; Linux; pl-PL) AppleWebKit/532.4 (KHTML, like Gecko) Arora/0.10.2 Safari/532.4",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.34 (KHTML, like Gecko) Arora/0.10.2 Safari/534.34",
	"Mozilla/5.0 (X11; U; Linux; en-US) AppleWebKit/527 (KHTML, like Gecko, Safari/419.3) Arora/0.10.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-MY) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.10.0",
	"Mozilla/5.0 (Windows; U; ; hu-HU) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.10.0",
	"Mozilla/5.0 (Windows; U; ; hu-HU) AppleWebKit/527+ (KHTML, like Gecko, Safari/419.3) Arora/0.10.0",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; Avant Browser; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 3.5.21022; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; Avant Browser; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.4; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; chromeframe; Avant Browser; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; InfoPath.1; .NET CLR 3.0.4506.",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB5; Avant Browser; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Avant Browser; Avant Browser; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT; Avant Browser; Avant Browser; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/4.0; Avant Browser; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; WOW64; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 3.5.21022; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6.3; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 1.1.4322; .NET CLR 3.5.30729; .NET CLR 3.0.30729",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 3.5.21022; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; Avant Browser; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; Avant Browser; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Avant Browser; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618; InfoPath.2; OfficeLiveConnector.1.3; OfficeLivePatch.0.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Avant Browser; Avant Browser; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; Tablet PC 2.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Avant Browser; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.2; Avant Browser; Avant Browser; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; InfoPath.2)",
	"Mozilla/5.0 (Windows; U; WinNT; en; rv:1.0.2) Gecko/20030311 Beonex/0.8.2-stable",
	"Mozilla/5.0 (Windows; U; WinNT; en; Preview) Gecko/20020603 Beonex/0.8-stable",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.8.1b2) Gecko/20060821 BonEcho/2.0b2 (Debian-1.99+2.0b2+dfsg-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1b2) Gecko/20060821 BonEcho/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1b2) Gecko/20060826 BonEcho/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1b2) Gecko/20060831 BonEcho/2.0b2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.8.1b1) Gecko/20060601 BonEcho/2.0b1 (Ubuntu-edgy)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1a3) Gecko/20060526 BonEcho/2.0a3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1a2) Gecko/20060512 BonEcho/2.0a2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1a2) Gecko/20060512 BonEcho/2.0a2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1a2) Gecko/20060512 BonEcho/2.0a2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-GB; rv:1.8.1a2) Gecko/20060512 BonEcho/2.0a2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X Mach-O; en-US; rv:1.8.1a2) Gecko/20060512 BonEcho/2.0a2",
	"Mozilla/5.0 (X11; U; OpenBSD ppc; en-US; rv:1.8.1.9) Gecko/20070223 BonEcho/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.9) Gecko/20071103 BonEcho/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071113 BonEcho/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.8pre) Gecko/20071012 BonEcho/2.0.0.8pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.7pre) Gecko/20070901 BonEcho/2.0.0.7pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.7) Gecko/20070918 BonEcho/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.7) Gecko/20071018 BonEcho/2.0.0.7",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.8.1.7) Gecko/20070917 BonEcho/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.6) Gecko/20070812 BonEcho/2.0.0.6",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.8.1.6) Gecko/20070731 BonEcho/2.0.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.8.1.5pre) Gecko/20070604 BonEcho/2.0.0.5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.5pre) Gecko/20070622 BonEcho/2.0.0.5pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4pre) Gecko/20070414 BonEcho/2.0.0.4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.4pre) Gecko/20070510 BonEcho/2.0.0.4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4pre) Gecko/20070416 BonEcho/2.0.0.4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4pre) Gecko/20070410 BonEcho/2.0.0.4pre",
	"Mozilla/5.0 (X11; U; OpenBSD ppc; en-US; rv:1.8.1.4) Gecko/20070223 BonEcho/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070531 BonEcho/2.0.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4) Gecko/20070416 BonEcho/2.0.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-GB; rv:1.8.1.3pre) Gecko/20070302 BonEcho/2.0.0.3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.3pre) Gecko/20070302 BonEcho/2.0.0.3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.3pre) Gecko/20070301 BonEcho/2.0.0.3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.3) Gecko/20070517 BonEcho/2.0.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9a3) Gecko/20070409 BonEcho/2.0.0.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.3) Gecko/20070329 BonEcho/2.0.0.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.3) Gecko/20070322 BonEcho/2.0.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-GB; rv:1.8.1.2pre) Gecko/20070226 BonEcho/2.0.0.2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2pre) Gecko/20070213 BonEcho/2.0.0.2pre",
	"Mozilla/5.0 (BeOS; U; Haiku BePC; en-US; rv:1.8.1.21pre) Gecko/20090218 BonEcho/2.0.0.21pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070302 BonEcho/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070224 BonEcho/2.0.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2) Gecko/20070227 BonEcho/2.0.0.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.2) Gecko/20070223 BonEcho/2.0.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.1pre) Gecko/20061203 BonEcho/2.0.0.1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.1pre) Gecko/20061202 BonEcho/2.0.0.1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.1pre) Gecko/20061122 BonEcho/2.0.0.1pre",
	"Mozilla/5.0 (BeOS; U; Haiku BePC; en-US; rv:1.8.1.18) Gecko/20081114 BonEcho/2.0.0.18",
	"Mozilla/5.0 (BeOS; U; Haiku BePC; en-US; rv:1.8.1.17) Gecko/20080831 BonEcho/2.0.0.17",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.8.1.17) Gecko/20080831 BonEcho/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080417 BonEcho/2.0.0.14",
	"Mozilla/5.0 (BeOS; U; Haiku BePC; en-US; rv:1.8.1.14) Gecko/20080429 BonEcho/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080401 BonEcho/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.12pre) Gecko/20080103 BonEcho/2.0.0.12pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.12) Gecko/20080208 BonEcho/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080321 BonEcho/2.0.0.12 (SliTaz GNU/Linux)",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.11) Gecko/20080208 BonEcho/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20071204 BonEcho/2.0.0.11",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.8.1.10) Gecko/20071128 BonEcho/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.1.1) Gecko/20061219 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux mips; en-US; rv:1.8.1.1) Gecko/20070628 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.1) Gecko/20070117 Epiphany/2.16 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20070222 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20070220 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20070217 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20070215 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20070115 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.1) Gecko/20070110 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.1) Gecko/20070131 BonEcho/2.0.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.1) Gecko/20061222 BonEcho/2.0.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.8.1.1) Gecko/20061230 BonEcho/2.0.0.1",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.8.1.1) Gecko/20061220 BonEcho/2.0.0.1",
	"Mozilla/5.0 (X11; U; Win95; en-US; rv:1.8.1) Gecko/20061125 BonEcho/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1) Gecko/20061129 BonEcho/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1) Gecko/20061031 BonEcho/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1) Gecko/20061026 BonEcho/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1) Gecko/20061003 BonEcho/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1) Gecko/20061031 BonEcho/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1) Gecko/20061210 BonEcho/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1) Gecko/20061209 BonEcho/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1) Gecko/20061121 BonEcho/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1) Gecko/20061113 BonEcho/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1) Gecko/20061112 BonEcho/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1) Gecko/20060930 BonEcho/2.0",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1) Gecko/20061026 BonEcho/2.0",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1) Gecko/20061025 BonEcho/2.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1) Gecko/20061024 BonEcho/2.0",
	"Mozilla/5.0 (BeOS; U; BeOS BeBox; fr; rv:1.9) Gecko/2008052906 BonEcho/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en; rv:1.9a1) Gecko/20061128 BonEcho/0.7b1",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET4.0C; .NET4.0E; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Browzar)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en; rv:1.9.2.14pre) Gecko/20101212 Camino/2.1a1pre (like Firefox/3.6.14pre)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en; rv:1.9.2.29pre) Gecko/20130101 Camino/2.1.3pre (like Firefox/3.6.29pre)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.5; de; rv:1.9.2.28) Gecko/20120308 Camino/2.1.2 (MultiLang) (like Firefox/3.6.28)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.8; it; rv:1.9.2.28) Gecko/20120308 Camino/2.1.2 (MultiLang) (like Firefox/3.6.28)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; fr; rv:1.9.2.28) Gecko/20120308 Camino/2.1.2 (MultiLang) (like Firefox/3.6.28)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en; rv:1.9.2.24) Gecko/20111114 Camino/2.1 (like Firefox/3.6.24)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en; rv:1.9.0.8pre) Gecko/2009022800 Camino/2.0b3pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en; rv:1.9.0.10pre) Gecko/2009041800 Camino/2.0b3pre (like Firefox/3.0.10pre)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.5; it; rv:1.9.0.19) Gecko/2010111021 Camino/2.0.6 (MultiLang) (like Firefox/3.0.19)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en; rv:1.9.0.19) Gecko/2010111021 Camino/2.0.6 (MultiLang) (like Firefox/3.0.19)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en; rv:1.9.0.19) Gecko/2010051911 Camino/2.0.3 (like Firefox/3.0.19)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; nl; rv:1.9.0.19) Gecko/2010051911 Camino/2.0.3 (MultiLang) (like Firefox/3.0.19)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en; rv:1.9.0.18) Gecko/2010021619 Camino/2.0.2 (like Firefox/3.0.18)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.8.1.4pre) Gecko/20070511 Camino/1.6pre",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; de; rv:1.8.1.5pre) Gecko/20070605 Camino/1.6a1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.4pre) Gecko/20070526 Camino/1.6a1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.4pre) Gecko/20070521 Camino/1.6a1pre",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; it; rv:1.8.1.21) Gecko/20090327 Camino/1.6.7 (MultiLang) (like Firefox/2.0.0.21pre)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; fr; rv:1.8.1.21) Gecko/20090327 Camino/1.6.7 (MultiLang) (like Firefox/2.0.0.21pre)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.8.1.21) Gecko/20090327 Camino/1.6.7 (like Firefox/2.0.0.21pre)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.24) Gecko/20100305 Camino/1.6.11 (like Firefox/2.0.0.24)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.8.1.12) Gecko/20080206 Camino/1.5.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X Mach-O; en; rv:1.8.1.12) Gecko/20080206 Camino/1.5.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.11) Gecko/20071128 Camino/1.5.4",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.8.1.6) Gecko/20070809 Camino/1.5.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.6) Gecko/20070809 Firefox/2.0.0.6 Camino/1.5.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.6) Gecko/20070809 Camino/1.5.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.6) Gecko/20070725 Firefox/2.0.0.6 Camino/1.5.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.8.1.4) Gecko/20070509 Camino/1.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.4) Gecko/20070609 Camino/1.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.4) Gecko/20070607 Camino/1.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.4) Gecko/20070509 Camino/1.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.9a4pre) Gecko/20070404 Camino/1.2+",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.4pre) Gecko/20070417 Camino/1.1b+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.8.1.2pre) Gecko/20070227 Camino/1.1b",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.2pre) Gecko/20070223 Camino/1.1b",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.2pre) Gecko/20070108 Camino/1.1a2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.1pre) Gecko/20061126 Camino/1.1a1+",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1) Gecko/20061018 Camino/1.1a1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.1) Gecko/20060203 Camino/1.0rc1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.1) Gecko/20060119 Camino/1.0b2+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8) Gecko/20051229 Camino/1.0b2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8) Gecko/20051228 Camino/1.0b1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8) Gecko/20051107 Camino/1.0b1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8b4) Gecko/20050914 Camino/1.0a1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.10) Gecko/20070228 Camino/1.0.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.10) Gecko/20070228 Camino/1.0.4",
	"Mozilla/5.0 (Macintosh; U; PPC Max OS X Mach-O; it-IT; rv:1.8.0.7) Gecko/200609211 Camino/1.0.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.7) Gecko/20060911 Camino/1.0.3 (MultiLang)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.7) Gecko/20060911 Camino/1.0.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.7) Gecko/20060911 Camino/1.0.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.4) Gecko/20060613 Camino/1.0.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.4) Gecko/20060613 Camino/1.0.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.3) Gecko/20060503 Camino/1.0.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.3) Gecko/20060427 Camino/1.0.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1b1) Gecko/20060807 Camino/1.0+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1b1) Gecko/20060721 Camino/1.0+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1a3) Gecko/20060528 Camino/1.0+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1) Gecko/20061013 Camino/1.0+ (Firefox compatible)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1) Gecko/20061013 Camino/1.0+",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1a3) Gecko/20060601 Camino/1.0+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.1) Gecko/20060307 Camino/1.0",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.1) Gecko/20060214 Camino/1.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8b5) Gecko/20051021 Camino/1.0+",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.1) Gecko/20060214 Camino/1.0",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8b2) Gecko Camino/0.9+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7) Gecko/20040517 Camino/0.8b",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.8) Gecko/20050427 Camino/0.8.4",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.2) Gecko/20040825 Camino/0.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.0.1) Gecko/20030306 Camino/0.7",
	"Mozilla/4.08 (Charon; Inferno)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.8 (KHTML, like Gecko, Safari) Cheshire/1.0.UNOFFICIAL",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko, Safari) Cheshire/1.0.UNOFFICIAL",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko, Safari/419.3) Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Safari) Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko, Safari/111) Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko, Safari) Safari/419.3 Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko) Safari/419.3 Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko) AppleWebKit/418.9 Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko, Safari/419.3) Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko, Safari/125) Cheshire/1.0.ALPHA",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; pl-PL; rv:1.0.1) Gecko/20021111 Chimera/0.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US; rv:1.0.1) Gecko/20021111 Chimera/0.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US; rv:1.0.1) Gecko/20021104 Chimera/0.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.0.1) Gecko/20030111 Chimera/0.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.0.1) Gecko/20030109 Chimera/0.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.0.1) Gecko/20021220 Chimera/0.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.0.1) Gecko/20021216 Chimera/0.6",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_2) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/36.0.1944.0 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/35.0.1916.47 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/32.0.1667.0 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/32.0.1664.3 Safari/537.36",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/31.0.1650.16 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/31.0.1623.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.1599.17 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/29.0.1547.62 Safari/537.36",
	"Mozilla/5.0 (X11; CrOS i686 4319.74.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/29.0.1547.57 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/29.0.1547.2 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/28.0.1468.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/28.0.1467.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/28.0.1464.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.93 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.93 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.93 Safari/537.36",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.93 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_3) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.93 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_5) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.93 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.90 Safari/537.36",
	"Mozilla/5.0 (X11; CrOS i686 3912.101.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/27.0.1453.116 Safari/537.36",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.17 (KHTML, like Gecko) Chrome/24.0.1312.60 Safari/537.17",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_2) AppleWebKit/537.17 (KHTML, like Gecko) Chrome/24.0.1309.0 Safari/537.17",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.15 (KHTML, like Gecko) Chrome/24.0.1295.0 Safari/537.15",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.14 (KHTML, like Gecko) Chrome/24.0.1292.0 Safari/537.14",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.13 (KHTML, like Gecko) Chrome/24.0.1290.1 Safari/537.13",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/537.13 (KHTML, like Gecko) Chrome/24.0.1290.1 Safari/537.13",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_2) AppleWebKit/537.13 (KHTML, like Gecko) Chrome/24.0.1290.1 Safari/537.13",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_4) AppleWebKit/537.13 (KHTML, like Gecko) Chrome/24.0.1290.1 Safari/537.13",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.13 (KHTML, like Gecko) Chrome/24.0.1284.0 Safari/537.13",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.11 (KHTML, like Gecko) Chrome/23.0.1271.6 Safari/537.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_2) AppleWebKit/537.11 (KHTML, like Gecko) Chrome/23.0.1271.6 Safari/537.11",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/537.11 (KHTML, like Gecko) Chrome/23.0.1271.26 Safari/537.11",
	"Mozilla/5.0 (Windows NT 6.0) yi; AppleWebKit/345667.12221 (KHTML, like Gecko) Chrome/23.0.1271.26 Safari/453667.1221",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.11 (KHTML, like Gecko) Chrome/23.0.1271.17 Safari/537.11",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/537.4 (KHTML, like Gecko) Chrome/22.0.1229.94 Safari/537.4",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_0) AppleWebKit/537.4 (KHTML, like Gecko) Chrome/22.0.1229.79 Safari/537.4",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.2 (KHTML, like Gecko) Chrome/22.0.1216.0 Safari/537.2",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/22.0.1207.1 Safari/537.1",
	"Mozilla/5.0 (X11; CrOS i686 2268.111.0) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1132.57 Safari/536.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/536.6 (KHTML, like Gecko) Chrome/20.0.1092.0 Safari/536.6",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/536.6 (KHTML, like Gecko) Chrome/20.0.1090.0 Safari/536.6",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/19.77.34.5 Safari/537.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/536.5 (KHTML, like Gecko) Chrome/19.0.1084.9 Safari/536.5",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/536.5 (KHTML, like Gecko) Chrome/19.0.1084.36 Safari/536.5",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1063.0 Safari/536.3",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1063.0 Safari/536.3",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_0) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1063.0 Safari/536.3",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1062.0 Safari/536.3",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1062.0 Safari/536.3",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1061.1 Safari/536.3",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1061.1 Safari/536.3",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1061.1 Safari/536.3",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/536.3 (KHTML, like Gecko) Chrome/19.0.1061.0 Safari/536.3",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.24 (KHTML, like Gecko) Chrome/19.0.1055.1 Safari/535.24",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/535.24 (KHTML, like Gecko) Chrome/19.0.1055.1 Safari/535.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.24 (KHTML, like Gecko) Chrome/19.0.1055.1 Safari/535.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_3) AppleWebKit/535.22 (KHTML, like Gecko) Chrome/19.0.1047.0 Safari/535.22",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.21 (KHTML, like Gecko) Chrome/19.0.1042.0 Safari/535.21",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.21 (KHTML, like Gecko) Chrome/19.0.1041.0 Safari/535.21",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_3) AppleWebKit/535.20 (KHTML, like Gecko) Chrome/19.0.1036.7 Safari/535.20",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/18.6.872.0 Safari/535.2 UNTRUSTED/1.0 3gpp-gba UNTRUSTED/1.0",
	"Mozilla/5.0 (Macintosh; AMD Mac OS X 10_8_2) AppleWebKit/535.22 (KHTML, like Gecko) Chrome/18.6.872",
	"Mozilla/5.0 (X11; CrOS i686 1660.57.0) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.46 Safari/535.19",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.45 Safari/535.19",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.45 Safari/535.19",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.45 Safari/535.19",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.166 Safari/535.19",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_5_8) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.151 Safari/535.19",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.19 (KHTML, like Gecko) Ubuntu/11.10 Chromium/18.0.1025.142 Chrome/18.0.1025.142 Safari/535.19",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.11 Safari/535.19",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_3) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_5_8) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.66 Safari/535.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Ubuntu/11.10 Chromium/17.0.963.65 Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Ubuntu/11.04 Chromium/17.0.963.65 Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Ubuntu/10.10 Chromium/17.0.963.65 Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.11 (KHTML, like Gecko) Ubuntu/11.10 Chromium/17.0.963.65 Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (X11; FreeBSD amd64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_0) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_4) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.65 Safari/535.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Ubuntu/11.04 Chromium/17.0.963.56 Chrome/17.0.963.56 Safari/535.11",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.56 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.56 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.56 Safari/535.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.12 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.8 (KHTML, like Gecko) Chrome/17.0.940.0 Safari/535.8",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.77 Safari/535.7ad-imcjapan-syosyaman-xkgi3lqg03!wgz",
	"Mozilla/5.0 (X11; CrOS i686 1193.158.0) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.75 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.75 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.75 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.63 Safari/535.7xs5D9rRDFpg2g",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.8 (KHTML, like Gecko) Chrome/16.0.912.63 Safari/535.8",
	"Mozilla/5.0 (Windows NT 5.2; WOW64) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.63 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.36 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.36 Safari/535.7",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.7 (KHTML, like Gecko) Chrome/16.0.912.36 Safari/535.7",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.6 (KHTML, like Gecko) Chrome/16.0.897.0 Safari/535.6",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.874.54 Safari/535.2",
	"Mozilla/5.0 (X11; FreeBSD i386) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.874.121 Safari/535.2",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.2 (KHTML, like Gecko) Ubuntu/11.10 Chromium/15.0.874.120 Chrome/15.0.874.120 Safari/535.2",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.874.120 Safari/535.2",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.872.0 Safari/535.2",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.2 (KHTML, like Gecko) Ubuntu/11.04 Chromium/15.0.871.0 Chrome/15.0.871.0 Safari/535.2",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.864.0 Safari/535.2",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.861.0 Safari/535.2",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_0) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.861.0 Safari/535.2",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.861.0 Safari/535.2",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.2 (KHTML, like Gecko) Chrome/15.0.860.0 Safari/535.2",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.835.186 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.834.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/11.04 Chromium/14.0.825.0 Chrome/14.0.825.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.824.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.815.10913 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.815.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/11.04 Chromium/14.0.814.0 Chrome/14.0.814.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.814.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/10.04 Chromium/14.0.813.0 Chrome/14.0.813.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.813.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.813.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.813.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.813.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.812.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.811.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.810.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.810.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.809.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/10.10 Chromium/14.0.808.0 Chrome/14.0.808.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/10.04 Chromium/14.0.808.0 Chrome/14.0.808.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/10.04 Chromium/14.0.804.0 Chrome/14.0.804.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.803.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/11.04 Chromium/14.0.803.0 Chrome/14.0.803.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.803.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_0) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.803.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.803.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_5_8) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.803.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.801.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_5_8) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.801.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.794.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_0) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.794.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.792.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.792.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.792.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10_6_7) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.790.0 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/14.0.790.0 Safari/535.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1) AppleWebKit/526.3 (KHTML, like Gecko) Chrome/14.0.564.21 Safari/526.3",
	"Mozilla/5.0 (X11; CrOS i686 13.587.48) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.43 Safari/535.1",
	"Mozilla/5.0 Slackware/13.37 (X11; U; Linux x86_64; en-US) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41",
	"Mozilla/5.0 ArchLinux (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Ubuntu/11.04 Chromium/13.0.782.41 Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.2; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_3) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.41 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_3) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.32 Safari/535.1",
	"Mozilla/5.0 (X11; Linux amd64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.24 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.24 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.24 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.220 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.220 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.220 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.215 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.215 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.215 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.215 Safari/535.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.20 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.20 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.20 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.20 Safari/535.1",
	"Mozilla/5.0 (X11; CrOS i686 0.13.587) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.14 Safari/535.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.107 Safari/535.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_2) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.107 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.1 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.36 (KHTML, like Gecko) Chrome/13.0.766.0 Safari/534.36",
	"Mozilla/5.0 (X11; Linux amd64) AppleWebKit/534.36 (KHTML, like Gecko) Chrome/13.0.766.0 Safari/534.36",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.35 (KHTML, like Gecko) Ubuntu/10.10 Chromium/13.0.764.0 Chrome/13.0.764.0 Safari/534.35",
	"Mozilla/5.0 (X11; CrOS i686 0.13.507) AppleWebKit/534.35 (KHTML, like Gecko) Chrome/13.0.763.0 Safari/534.35",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.33 (KHTML, like Gecko) Ubuntu/9.10 Chromium/13.0.752.0 Chrome/13.0.752.0 Safari/534.33",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_5_8) AppleWebKit/534.31 (KHTML, like Gecko) Chrome/13.0.748.0 Safari/534.31",
	"Mozilla/5.0 (Windows NT 6.1; en-US) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.750.0 Safari/534.30",
	"Mozilla/5.0 (X11; CrOS i686 12.433.109) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.93 Safari/534.30",
	"Mozilla/5.0 (X11; CrOS i686 12.0.742.91) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.93 Safari/534.30",
	"Mozilla/5.0 Slackware/13.37 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/12.0.742.91",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.91 Chromium/12.0.742.91 Safari/534.30",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.68 Safari/534.30",
	"Mozilla/5.0 ArchLinux (X11; U; Linux x86_64; en-US) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.60 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.53 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.113 Safari/534.30",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.30 (KHTML, like Gecko) Ubuntu/11.04 Chromium/12.0.742.112 Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.30 (KHTML, like Gecko) Ubuntu/10.10 Chromium/12.0.742.112 Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.30 (KHTML, like Gecko) Ubuntu/10.04 Chromium/12.0.742.112 Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.30 (KHTML, like Gecko) Ubuntu/11.04 Chromium/12.0.742.112 Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.30 (KHTML, like Gecko) Ubuntu/10.10 Chromium/12.0.742.112 Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.30 (KHTML, like Gecko) Ubuntu/10.04 Chromium/12.0.742.112 Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (Windows NT 7.1) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (Windows 8) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_6) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_4) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.112 Safari/534.30",
	"Mozilla/5.0 (X11; CrOS i686 12.433.216) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.105 Safari/534.30",
	"Mozilla/5.0 ArchLinux (X11; U; Linux x86_64; en-US) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.100 Safari/534.30",
	"Mozilla/5.0 ArchLinux (X11; U; Linux x86_64; en-US) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.100",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.30 (KHTML, like Gecko) Slackware/Chrome/12.0.742.100 Safari/534.30",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.100 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.100 Safari/534.30",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_0) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.100 Safari/534.30",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_4) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.100 Safari/534.30",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.724.100 Safari/534.30",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.25 (KHTML, like Gecko) Chrome/12.0.706.0 Safari/534.25",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.25 (KHTML, like Gecko) Chrome/12.0.704.0 Safari/534.25",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.24 (KHTML, like Gecko) Ubuntu/10.10 Chromium/12.0.703.0 Chrome/12.0.703.0 Safari/534.24",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.24 (KHTML, like Gecko) Ubuntu/10.10 Chromium/12.0.702.0 Chrome/12.0.702.0 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/12.0.702.0 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/12.0.702.0 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.700.3 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.699.0 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.699.0 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_6) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.698.0 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.697.0 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_5_8) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 Slackware/13.37 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/11.0.696.50",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.43 Safari/534.24",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.34 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.34 Safari/534.24",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.3 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.3 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.3 Safari/534.24",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.14 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.12 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_6) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.12 Safari/534.24",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.24 (KHTML, like Gecko) Ubuntu/10.04 Chromium/11.0.696.0 Chrome/11.0.696.0 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_0) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.696.0 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Chrome/11.0.694.0 Safari/534.24",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.23 (KHTML, like Gecko) Chrome/11.0.686.3 Safari/534.23",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.21 (KHTML, like Gecko) Chrome/11.0.682.0 Safari/534.21",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.21 (KHTML, like Gecko) Chrome/11.0.678.0 Safari/534.21",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_7_0; en-US) AppleWebKit/534.21 (KHTML, like Gecko) Chrome/11.0.678.0 Safari/534.21",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.20 (KHTML, like Gecko) Chrome/11.0.672.2 Safari/534.20",
	"Mozilla/5.0 (Windows NT) AppleWebKit/534.20 (KHTML, like Gecko) Chrome/11.0.672.2 Safari/534.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.20 (KHTML, like Gecko) Chrome/11.0.672.2 Safari/534.20",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.20 (KHTML, like Gecko) Chrome/11.0.669.0 Safari/534.20",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.19 (KHTML, like Gecko) Chrome/11.0.661.0 Safari/534.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.18 (KHTML, like Gecko) Chrome/11.0.661.0 Safari/534.18",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.18 (KHTML, like Gecko) Chrome/11.0.660.0 Safari/534.18",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.17 (KHTML, like Gecko) Chrome/11.0.655.0 Safari/534.17",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.17 (KHTML, like Gecko) Chrome/11.0.655.0 Safari/534.17",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.17 (KHTML, like Gecko) Chrome/11.0.654.0 Safari/534.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.17 (KHTML, like Gecko) Chrome/11.0.652.0 Safari/534.17",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.17 (KHTML, like Gecko) Chrome/10.0.649.0 Safari/534.17",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de-DE) AppleWebKit/534.17 (KHTML, like Gecko) Chrome/10.0.649.0 Safari/534.17",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.82 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux armv7l; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.204 Safari/534.16",
	"Mozilla/5.0 (X11; U; FreeBSD x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.204 Safari/534.16",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.204 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.204",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.134 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.134 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.134 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.134 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.648.133 Chrome/10.0.648.133 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.133 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.648.133 Chrome/10.0.648.133 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.133 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.133 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.133 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.133 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.648.127 Chrome/10.0.648.127 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.127 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.127 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.127 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.11 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru-RU; AppleWebKit/534.16; KHTML; like Gecko; Chrome/10.0.648.11;Safari/534.16)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru-RU) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.11 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.11 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.648.0 Chrome/10.0.648.0 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.648.0 Chrome/10.0.648.0 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.648.0 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.642.0 Chrome/10.0.642.0 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.639.0 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.638.0 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.634.0 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Chrome/10.0.634.0 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 SUSE/10.0.626.0 (KHTML, like Gecko) Chrome/10.0.626.0 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.15 (KHTML, like Gecko) Chrome/10.0.613.0 Safari/534.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.15 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.613.0 Chrome/10.0.613.0 Safari/534.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.15 (KHTML, like Gecko) Ubuntu/10.04 Chromium/10.0.612.3 Chrome/10.0.612.3 Safari/534.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.15 (KHTML, like Gecko) Chrome/10.0.612.1 Safari/534.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.15 (KHTML, like Gecko) Ubuntu/10.10 Chromium/10.0.611.0 Chrome/10.0.611.0 Safari/534.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.14 (KHTML, like Gecko) Chrome/10.0.602.0 Safari/534.14",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.14 (KHTML, like Gecko) Chrome/10.0.601.0 Safari/534.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.14 (KHTML, like Gecko) Chrome/10.0.601.0 Safari/534.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/540.0 (KHTML,like Gecko) Chrome/9.1.0.0 Safari/540.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/540.0 (KHTML, like Gecko) Ubuntu/10.10 Chrome/9.1.0.0 Safari/540.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.14 (KHTML, like Gecko) Chrome/9.0.601.0 Safari/534.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.14 (KHTML, like Gecko) Ubuntu/10.10 Chromium/9.0.600.0 Chrome/9.0.600.0 Safari/534.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.14 (KHTML, like Gecko) Chrome/9.0.600.0 Safari/534.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.599.0 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-CA) AppleWebKit/534.13 (KHTML like Gecko) Chrome/9.0.597.98 Safari/534.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.84 Safari/534.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.44 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.19 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.15 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.15 Safari/534.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.107 Safari/534.13 v1333515017.9196",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.0 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.0 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.0 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.0 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.0 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.0 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.596.0 Safari/534.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Ubuntu/10.04 Chromium/9.0.595.0 Chrome/9.0.595.0 Safari/534.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Ubuntu/9.10 Chromium/9.0.592.0 Chrome/9.0.592.0 Safari/534.13",
	"Mozilla/5.0 (X11; U; Windows NT 6; en-US) AppleWebKit/534.12 (KHTML, like Gecko) Chrome/9.0.587.0 Safari/534.12",
	"Mozilla/5.0 (Windows U Windows NT 5.1 en-US) AppleWebKit/534.12 (KHTML, like Gecko) Chrome/9.0.583.0 Safari/534.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.12 (KHTML, like Gecko) Chrome/9.0.579.0 Safari/534.12",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/534.12 (KHTML, like Gecko) Chrome/9.0.576.0 Safari/534.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/540.0 (KHTML, like Gecko) Ubuntu/10.10 Chrome/8.1.0.0 Safari/540.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.558.0 Safari/534.10",
	"Mozilla/5.0 (X11; U; CrOS i686 0.9.130; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.344 Safari/534.10",
	"Mozilla/5.0 (X11; U; CrOS i686 0.9.128; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.343 Safari/534.10",
	"Mozilla/5.0 (X11; U; CrOS i686 0.9.128; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.341 Safari/534.10",
	"Mozilla/5.0 (X11; U; CrOS i686 0.9.128; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.339 Safari/534.10",
	"Mozilla/5.0 (X11; U; CrOS i686 0.9.128; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.339",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Ubuntu/10.10 Chromium/8.0.552.237 Chrome/8.0.552.237 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de-DE) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/533.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_8; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/534.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/534.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.210 Safari/534.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.200 Safari/534.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.551.0 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/7.0.548.0 Safari/534.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/7.0.544.0 Safari/534.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.15) Gecko/20101027 Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/7.0.540.0 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/7.0.540.0 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de-DE) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/7.0.540.0 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/7.0.540.0 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.9 (KHTML, like Gecko) Chrome/7.0.531.0 Safari/534.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.8 (KHTML, like Gecko) Chrome/7.0.521.0 Safari/534.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.517.24 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-FR) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.514.0 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.514.0 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.514.0 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.6 (KHTML, like Gecko) Chrome/7.0.500.0 Safari/534.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.6 (KHTML, like Gecko) Chrome/7.0.498.0 Safari/534.6",
	"Mozilla/5.0 (ipad Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.6 (KHTML, like Gecko) Chrome/7.0.498.0 Safari/534.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/7.0.0 Safari/700.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.4 (KHTML, like Gecko) Chrome/6.0.481.0 Safari/534.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.472.63 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.472.53 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.472.33 Safari/534.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.470.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.464.0 Safari/534.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.464.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.463.0 Safari/534.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.462.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.462.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.461.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.461.0 Safari/534.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.461.0 Safari/534.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.460.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.460.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.460.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.459.0 Safari/534.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.458.1 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.458.1 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.458.1 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.458.1 Safari/534.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.458.1 Safari/534.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.458.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.458.0 Safari/534.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.457.0 Safari/534.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.456.0 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.2 (KHTML, like Gecko) Chrome/6.0.454.0 Safari/534.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.2 (KHTML, like Gecko) Chrome/6.0.454.0 Safari/534.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.2 (KHTML, like Gecko) Chrome/6.0.453.1 Safari/534.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-US) AppleWebKit/534.2 (KHTML, like Gecko) Chrome/6.0.453.1 Safari/534.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/534.2 (KHTML, like Gecko) Chrome/6.0.453.1 Safari/534.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.2 (KHTML, like Gecko) Chrome/6.0.451.0 Safari/534.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.1 SUSE/6.0.428.0 (KHTML, like Gecko) Chrome/6.0.428.0 Safari/534.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.428.0 Safari/534.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.428.0 Safari/534.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-US) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.428.0 Safari/534.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.427.0 Safari/534.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.422.0 Safari/534.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.417.0 Safari/534.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.416.0 Safari/534.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.1 (KHTML, like Gecko) Chrome/6.0.414.0 Safari/534.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.9 (KHTML, like Gecko) Chrome/6.0.400.0 Safari/533.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.8 (KHTML, like Gecko) Chrome/6.0.397.0 Safari/533.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/6.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.999 Safari/533.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.99 Safari/533.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.99 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.99 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.99 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.99 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.86 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.86 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.86 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.70 Safari/533.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.127 Safari/533.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.126 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; fr-FR) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.126 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_8; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.125 Safari/533.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.370.0 Safari/533.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.368.0 Safari/533.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.366.2 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.366.0 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.366.0 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.363.0 Safari/533.3",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.359.0 Safari/533.3",
	"Mozilla/5.0 (X11; U; x86_64 Linux; en_GB, en_US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.358.0 Safari/533.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.358.0 Safari/533.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.358.0 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.357.0 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.356.0 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.355.0 Safari/533.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.354.0 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.354.0 Safari/533.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.353.0 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Chrome/5.0.353.0 Safari/533.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.343.0 Safari/533.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.343.0 Safari/533.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_7_0; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.7 Safari/533.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.7 Safari/533.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.5 Safari/533.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.3 Safari/533.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.3 Safari/533.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.2 Safari/533.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.1 Safari/533.2",
	"Mozilla/5.0 (X11; U; Linux i586; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.1 Safari/533.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.2 (KHTML, like Gecko) Chrome/5.0.342.1 Safari/533.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/533.1 (KHTML, like Gecko) Chrome/5.0.335.0 Safari/533.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN) AppleWebKit/533.16 (KHTML, like Gecko) Chrome/5.0.335.0 Safari/533.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Chrome/5.0.310.0 Safari/532.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Chrome/5.0.309.0 Safari/532.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Chrome/5.0.308.0 Safari/532.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Chrome/5.0.307.11 Safari/532.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Chrome/5.0.307.1 Safari/532.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Chrome/4.1.249.1025 Safari/532.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Chrome/4.0.302.2 Safari/532.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Chrome/4.0.288.1 Safari/532.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Chrome/4.0.277.0 Safari/532.8",
	"Mozilla/5.0 (X11; U; Slackware Linux x86_64; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Chrome/4.0.249.30 Safari/532.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; it-IT) AppleWebKit/532.5 (KHTML, like Gecko) Chrome/4.0.249.25 Safari/532.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Chrome/4.0.249.0 Safari/532.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_8; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Chrome/4.0.249.0 Safari/532.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Chrome/4.0.246.0 Safari/532.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.4 (KHTML, like Gecko) Chrome/4.0.241.0 Safari/532.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.4 (KHTML, like Gecko) Chrome/4.0.237.0 Safari/532.4 Debian",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Chrome/4.0.227.0 Safari/532.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Chrome/4.0.224.2 Safari/532.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Chrome/4.0.223.5 Safari/532.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.4 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.3 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE) Chrome/4.0.223.3 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.2 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.2 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.2 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.2 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.1 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.1 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.1 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.223.0 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.8 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.7 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.6 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.6 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.6 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.5 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.5 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.5 Safari/532.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.5 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.4 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.4 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.4 Safari/532.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.4 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.3 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.3 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.3 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.2 Safari/532.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.2 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.12 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.12 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.12 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.1 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.222.0 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.8 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.8 Safari/532.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.8 Safari/532.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.8 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.7 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.6 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.6 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.6 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.3 Safari/532.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.2 (KHTML, like Gecko) Chrome/4.0.221.0 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.220.1 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.6 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.5 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.5 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.4 Safari/532.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.3 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.3 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.3 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.219.0 Safari/532.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.1 Safari/532.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.1 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.1 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.1 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.1 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.1 Safari/532.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.0 Safari/532.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.0 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.0 Safari/532.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.213.0 Safari/532.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.212.1 Safari/532.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.212.1 Safari/532.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.212.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.1 (KHTML, like Gecko) Chrome/4.0.212.0 Safari/532.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.212.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.212.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.212.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.212.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.212.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.7 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.7 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.4 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.4 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.4 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.2 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.2 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.2 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.2 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.211.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.210.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.210.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.209.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.209.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.209.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.209.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.208.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.208.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.208.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.208.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.208.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.207.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.207.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.207.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.207.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.207.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.207.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.207.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.1 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.1 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.206.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.205.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.204.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.204.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.204.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.204.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.204.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.4 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.2 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.2 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.203.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.2 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0 (x86_64); de-DE) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-DE) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.2 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/525.13.",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.202.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.201.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.201.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/4.0.201.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.201.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198.1 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198.1 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.198 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197.11 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197.11 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197.11 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197.11 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.197 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.196.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.196.2 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.196.2 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.196.0 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.196.0 Safari/532.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.196 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.6 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.6 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.6 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.6 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.6 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.4 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.33 Safari/532.0",
	"Mozilla/4.0 (Windows; U; Windows NT 5.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.33 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.3 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.3 Safari/532.0",
	"Mozilla/6.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.27 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.27 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.27 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.27 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML,like Gecko) Chrome/3.0.195.27",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.27 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.27 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.24 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.24 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.21 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.21 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.21 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.21 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.20 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.20 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.17 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.17 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.10 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.10 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.10 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.1 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/3.0.195.1 Safari/532.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/531.4 (KHTML, like Gecko) Chrome/3.0.194.0 Safari/531.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/531.4 (KHTML, like Gecko) Chrome/3.0.194.0 Safari/531.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/531.3 (KHTML, like Gecko) Chrome/3.0.193.2 Safari/531.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/531.3 (KHTML, like Gecko) Chrome/3.0.193.2 Safari/531.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/531.3 (KHTML, like Gecko) Chrome/3.0.193.2 Safari/531.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/531.3 (KHTML, like Gecko) Chrome/3.0.193.0 Safari/531.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/531.3 (KHTML, like Gecko) Chrome/3.0.192 Safari/531.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/531.2 (KHTML, like Gecko) Chrome/3.0.191.3 Safari/531.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/531.0 (KHTML, like Gecko) Chrome/3.0.191.0 Safari/531.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/531.0 (KHTML, like Gecko) Chrome/3.0.191.0 Safari/531.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/531.0 (KHTML, like Gecko) Chrome/2.0.182.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/531.0 (KHTML, like Gecko) Chrome/2.0.182.0 Safari/531.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/530.0 (KHTML, like Gecko) Chrome/2.0.182.0 Safari/531.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.8 (KHTML, like Gecko) Chrome/2.0.178.0 Safari/530.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.8 (KHTML, like Gecko) Chrome/2.0.177.1 Safari/530.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.8 (KHTML, like Gecko) Chrome/2.0.177.0 Safari/530.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.7 (KHTML, like Gecko) Chrome/2.0.177.0 Safari/530.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.7 (KHTML, like Gecko) Chrome/2.0.176.0 Safari/530.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.7 (KHTML, like Gecko) Chrome/2.0.176.0 Safari/530.7",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/530.7 (KHTML, like Gecko) Chrome/2.0.175.0 Safari/530.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.7 (KHTML, like Gecko) Chrome/2.0.175.0 Safari/530.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.6 (KHTML, like Gecko) Chrome/2.0.175.0 Safari/530.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/530.6 (KHTML, like Gecko) Chrome/2.0.174.0 Safari/530.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.6 (KHTML, like Gecko) Chrome/2.0.174.0 Safari/530.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.6 (KHTML, like Gecko) Chrome/2.0.174.0 Safari/530.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.174.0 Safari/530.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/530.6 (KHTML, like Gecko) Chrome/2.0.174.0 Safari/530.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.173.1 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.173.1 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.173.0 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.8 Safari/530.5",
	"Mozilla/6.0 (Windows; U; Windows NT 6.0; en-US) Gecko/2009032609 Chrome/2.0.172.6 Safari/530.7",
	"Mozilla/6.0 (Windows; U; Windows NT 6.0; en-US) Gecko/2009032609 (KHTML, like Gecko) Chrome/2.0.172.6 Safari/530.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.6 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.43 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.43 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.43 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.43 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.42 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.40 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.40 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.39 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.39 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.23 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.2 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.2 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/530.4 (KHTML, like Gecko) Chrome/2.0.172.0 Safari/530.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; eu) AppleWebKit/530.4 (KHTML, like Gecko) Chrome/2.0.172.0 Safari/530.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/530.4 (KHTML, like Gecko) Chrome/2.0.172.0 Safari/530.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/2.0.172.0 Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.4 (KHTML, like Gecko) Chrome/2.0.171.0 Safari/530.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.1 (KHTML, like Gecko) Chrome/2.0.170.0 Safari/530.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.1 (KHTML, like Gecko) Chrome/2.0.169.0 Safari/530.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.1 (KHTML, like Gecko) Chrome/2.0.168.0 Safari/530.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.1 (KHTML, like Gecko) Chrome/2.0.164.0 Safari/530.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.0 (KHTML, like Gecko) Chrome/2.0.162.0 Safari/530.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.0 (KHTML, like Gecko) Chrome/2.0.160.0 Safari/530.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.10 (KHTML, like Gecko) Chrome/2.0.157.2 Safari/528.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.10 (KHTML, like Gecko) Chrome/2.0.157.2 Safari/528.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_0; en-US) AppleWebKit/528.10 (KHTML, like Gecko) Chrome/2.0.157.2 Safari/528.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.11 (KHTML, like Gecko) Chrome/2.0.157.0 Safari/528.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.9 (KHTML, like Gecko) Chrome/2.0.157.0 Safari/528.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.11 (KHTML, like Gecko) Chrome/2.0.157.0 Safari/528.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.10 (KHTML, like Gecko) Chrome/2.0.157.0 Safari/528.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/528.8 (KHTML, like Gecko) Chrome/2.0.156.1 Safari/528.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.8 (KHTML, like Gecko) Chrome/2.0.156.1 Safari/528.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.8 (KHTML, like Gecko) Chrome/2.0.156.1 Safari/528.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.8 (KHTML, like Gecko) Chrome/2.0.156.0 Version/3.2.1 Safari/528.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.8 (KHTML, like Gecko) Chrome/2.0.156.0 Safari/528.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/528.8 (KHTML, like Gecko) Chrome/1.0.156.0 Safari/528.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.59 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.59 Safari/525.19",
	"Mozilla/4.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.59 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.55 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.55 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.53 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.53 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.53 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.53 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.53 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.50 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.50 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.48 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.46 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.43 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.43 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.43 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.43 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.42 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/1.0.154.39 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.4.154.31 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.4.154.18 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.4 (KHTML, like Gecko) Chrome/0.3.155.0 Safari/528.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.3.155.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.3.154.9 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.3.154.6 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.153.1 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.153.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.153.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.152.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.152.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.151.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.151.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Chrome/0.2.151.0 Safari/525.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.6 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.6 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.30 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.30 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.29 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.29 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.29 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.27 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.27 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.27 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.13(KHTML, like Gecko) Chrome/0.2.149.27 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.27 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.27 Safari/525.13",
	"Mozilla/5.0 (Linux; U; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Chrome/0.2.149.27 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Mac OS X 10_6_1; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/ Safari/530.5",
	"Mozilla/5.0 (Macintosh; U; Mac OS X 10_5_7; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/ Safari/530.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-US) AppleWebKit/530.9 (KHTML, like Gecko) Chrome/ Safari/530.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-US) AppleWebKit/530.6 (KHTML, like Gecko) Chrome/ Safari/530.6",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-US) AppleWebKit/530.5 (KHTML, like Gecko) Chrome/ Safari/530.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.2 (KHTML, like Gecko) ChromePlus/4.0.222.3 Chrome/4.0.222.3 Safari/532.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.28.3 (KHTML, like Gecko) Version/3.2.3 ChromePlus/4.0.222.3 Chrome/4.0.222.3 Safari/525.28.3",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.122 Safari/534.30 ChromePlus/1.6.3.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.30 (KHTML, like Gecko) Chrome/12.0.742.122 Safari/534.30 ChromePlus/1.6.3.0alpha4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.98 Safari/534.13 ChromePlus/1.6.0.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Chrome/9.0.597.98 Safari/534.13 ChromePlus/1.5.3.0alpha4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/534.10 ChromePlus/1.5.2.0alpha1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/534.10 ChromePlus/1.5.2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/534.10 ChromePlus/1.5.2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.224 Safari/534.10 ChromePlus/1.5.2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10 ChromePlus/1.5.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10 ChromePlus/1.5.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10 ChromePlus/1.5.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10 ChromePlus/1.5.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.215 Safari/534.10 ChromePlus/1.5.1.0alpha3 ChromePlus/1.5.1.0alpha3 ChromePlus/1.5.1.0alpha3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) Chrome/8.0.552.216 Safari/534.10 ChromePlus/1.5.1.0alpha1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.517.41 Safari/534.7 ChromePlus/1.5.0.0alpha1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.517.41 Safari/534.7 ChromePlus/1.5.0.0alpha1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.517.41 Safari/534.7 ChromePlus/1.5.0.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.517.41 Safari/534.7 ChromePlus/1.5.0.0 ChromePlus/1.5.0.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Chrome/7.0.517.41 Safari/534.7 ChromePlus/1.4.3.0alpha3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.4 (KHTML, like Gecko) Chrome/5.0.375.99 Safari/533.4 ChromePlus/1.4.1.0alpha1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.1 (KHTML, like Gecko) Chrome/5.0.336.0 Safari/533.1 ChromePlus/1.3.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; mimic; rv:9.3.0) Gecko/20120117 Firefox/3.6.25 Classilla/CFM",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; mimic; rv:9.3.0) Clecko/20120101 Classilla/CFM",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.3) Gecko/20100409 Firefox/3.6.3 CometBird/3.6.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; it; rv:1.9.2.16) Gecko/20110325 Firefox/3.6.16 CometBird/3.6.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.5) Gecko/2009011615 Firefox/3.0.5 CometBird/3.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.5) Gecko/2009011615 Firefox/3.0.5 CometBird/3.0.5",
	"Mozilla/5.0 (Windows NT 6.2) AppleWebKit/535.7 (KHTML, like Gecko) Comodo_Dragon/16.1.1.0 Chrome/16.0.912.63 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.30 (KHTML, like Gecko) Comodo_Dragon/12.1.0.0 Chrome/12.0.742.91 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.30 (KHTML, like Gecko) Comodo_Dragon/12.1.0.0 Chrome/12.0.742.91 Safari/534.30",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.30 (KHTML, like Gecko) Comodo_Dragon/12.1.0.0 Chrome/12.0.742.91 Safari/534.30",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Comodo_Dragon/4.1.1.11 Chrome/4.1.249.1042 Safari/532.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Comodo_Dragon/4.1.1.11 Chrome/4.1.249.1042 Safari/532.5",
	"Mozilla/5.0 (X11; Linux x86_64; rv:10.0.11) Gecko/20100101 conkeror/1.0pre (Debian-1.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:16.0) Gecko/20121010 conkeror/1.0pre",
	"Mozilla/5.0 (X11; Linux x86_64; rv:6.0.1) Gecko/20110831 conkeror/0.9.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20101209 Conkeror/0.9.2 (Debian-0.9.2+git100804-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.15) Gecko/20101028 Conkeror/0.9.2 (Debian-0.9.2+git100804-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.14) Gecko/20101020 Conkeror/0.9.2 (Debian-0.9.2+git100804-1)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 6.0; SV1; Crazy Browser 9.0.04)",
	"Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.2; Trident/6.0; Crazy Browser 3.1.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 3.0.5) ; .NET CLR 3.0.04506.30; InfoPath.2; InfoPath.3; .NET CLR 1.1.4322; .NET4.0C; .NET4.0E; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.2; Crazy Browser 3.0.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; InfoPath.2; .NET CLR 2.0.50727; .NET CLR 1.1.4322; Crazy Browser 3.0.0 Beta2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Crazy Browser 3.0.0 Beta2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.2; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; Crazy Browser 3.0.0 Beta2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Crazy Browser 3.0.0 Beta2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Crazy Browser 3.0.0 Beta2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.3; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Avant Browser; .NET CLR 2.0.50727; .NET CLR 3.0.04506.590; .NET CLR 3.5.20706; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; InfoPath.1; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Alexa Toolbar; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; HbTools 4.7.0; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; (R1 1.3; Crazy Browser 2.0.1); .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Q312461; SV1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Q312461; SV1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Media Center PC 3.0; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; .NET CLR 1.1.4322; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 1.1.4322; Crazy Browser 2.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 2.0.0 Beta 1; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; Crazy Browser 2.0.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Crazy Browser 2.0.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; .NET CLR 1.1.4322; Crazy Browser 2.0.0)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows 98; Crazy Browser 1.x.x)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; YComp 5.0.0.0; Crazy Browser 1.0.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 1.0.5; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 1.0.5; .NET CLR 1.1.4322; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 1.0.5; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 1.0.5; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 1.0.5; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Crazy Browser 1.0.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Q312461; Crazy Browser 1.0.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; MyIE2; Crazy Browser 1.0.5; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Hotbar 4.3.1.0; Crazy Browser 1.0.5; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; FunWebProducts-MyWay; SV1; Crazy Browser 1.0.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; DigExt; Crazy Browser 1.0.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Crazy Browser 1.0.5; .NET CLR 2.0.40607; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Crazy Browser 1.0.5; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Crazy Browser 1.0.5; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Crazy Browser 1.0.5; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Crazy Browser 1.0.5; (R1 1.3))",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Crazy Browser 1.0.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Avant Browser [avantbrowser.com]; Crazy Browser 1.0.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; Q312461; Crazy Browser 1.0.5; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; WOW64; Trident/4.0; Deepnet Explorer 1.5.3; Smart 2x2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; .NET4.0E)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Deepnet Explorer 1.5.3; Smart 2x2; .NET CLR 2.0.50727; .NET CLR 1.1.4322; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Deepnet Explorer 1.5.3; Smart 2x2; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Deepnet Explorer 1.5.3; Smart 2x2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Deepnet Explorer 1.5.3; Smart 2x2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Deepnet Explorer 1.5.2; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; FunWebProducts; Deepnet Explorer 1.5.2; SpamBlockerUtility 4.7.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Deepnet Explorer 1.5.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Deepnet Explorer 1.5.2; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Deepnet Explorer 1.5.0; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Deepnet Explorer 1.5.0; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; pl-pl) AppleWebKit/312.8 (KHTML, like Gecko, Safari) DeskBrowse/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533+ (KHTML, like Gecko) Element Browser 5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.0.13) Gecko/2009073022 EnigmaFox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; it-it) AppleWebKit/534.26+ (KHTML, like Gecko) Ubuntu/11.04 Epiphany/2.30.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-FR) AppleWebKit/534.7 (KHTML, like Gecko) Epiphany/2.30.6 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Epiphany/2.30.6 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; ca-ad) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.30.6",
	"Mozilla/5.0 (X11; U; Linux x86; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Epiphany/2.30.6 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux i686; sv-se) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.30.6",
	"Mozilla/5.0 (X11; U; Linux i686; it-it) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.30.2",
	"Mozilla/5.0 (X11; U; OpenBSD arm; en-us) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.30.0",
	"Mozilla/5.0 (X11; U; FreeBSD amd64; en-us) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.30.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; nl-nl) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.29.91",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-fr) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.29.91",
	"Mozilla/5.0 (X11; U; Linux x86_64; zh-cn) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.28.2 SUSE/2.28.0-2.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; zh-cn) AppleWebKit/531.2+ (KHTML, like Gecko) Safari/531.2+ Epiphany/2.28.0 SUSE/2.28.0-2.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9.0.8) Gecko/20080528 Fedora/2.24.3-4.fc10 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9.0.8) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9.0.8) Gecko/20080528 Epiphany/2.22 (Debian/2.24.3-2)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9.0.7) Gecko/20080528 Epiphany/2.22",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9.0.14) Gecko/20080528 Ubuntu/9.10 (karmic) Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9.0.14) Gecko/20080528 Epiphany/2.22 (Debian/2.26.3-2)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9.0.1) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.9) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.8.1.13) Gecko/20080322 Epiphany/2.22 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en_GB; rv:1.9.0.1) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.9) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.8) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.8) Gecko/20080528 Epiphany/2.22 (Debian/2.24.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.7) Gecko/20080528 Epiphany/2.22",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.5) Gecko/20080528 Fedora/2.24.1-3.fc10 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.4) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.15) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.14) Gecko/20080528 Epiphany/2.22 (Debian/2.26.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.12) Gecko/20080528 Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9) Gecko/20080528 (Gentoo) Epiphany/2.22 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.8.1.14) Gecko/20080616 Fedora/2.20.3-4.fc8 Epiphany/2.20 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.8.1.13) Gecko/20080326 (Debian-1.8.1.13-1) Epiphany/2.20",
	"Mozilla/5.0 (X11; U; Linux ppc; en; rv:1.8.1.13) Gecko/20080325 Epiphany/2.20 Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9b3) Gecko Epiphany/2.20",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.19) Gecko/20081216 Epiphany/2.20 Firefox/2.0.0.19",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.17) Gecko/20080927 Epiphany/2.20 Firefox/2.0.0.17 (Dropline GNOME)",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.14) Gecko/20080418 Epiphany/2.20 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.12) Gecko/20080208 (Debian-1.8.1.12-5) Epiphany/2.20",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.12) Gecko/20080208 (Debian-1.8.1.12-2) Epiphany/2.20",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.10) Gecko/20071213 Epiphany/2.20 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; FreeBSD i386; pl; rv:1.8.1.12) Gecko/20080213 Epiphany/2.20 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en; rv:1.8.1.12) Gecko/20080213 Epiphany/2.20 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; OpenBSD amd64; en; rv:1.8.1.6) Gecko/20070817 Epiphany/2.18 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.8.1.4) Gecko/20061201 Epiphany/2.18 Firefox/2.0.0.4 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.8.1.3) Gecko/20061201 Epiphany/2.18 Firefox/2.0.0.3 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.5) Gecko/20070712 (Debian-1.8.1.5-1) Epiphany/2.18",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.4) Gecko/20070508 (Debian-1.8.1.4-1) Epiphany/2.18",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.3) Gecko/20070322 Epiphany/2.18",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.3) Gecko/20061201 Epiphany/2.18 Firefox/2.0.0.3 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.14) Gecko/20080416 Fedora/2.18.3-9.fc7 Epiphany/2.18 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.1) Gecko/20070322 Epiphany/2.18",
	"Mozilla/5.0 (X11; U; Linux x86_64; en; rv:1.8.1.4) Gecko/20070628 Epiphany/2.16 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.3) Gecko/20070403 Epiphany/2.16 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux sparc64; en-US; rv:1.8.0.14eol) Gecko/20070505 (Debian-1.8.0.15",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.0.7) Gecko/20060928 Epiphany/2.14 (Ubuntu)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20071201 (Debian-1.8.1.11-1) Epiphany/2.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20061205 (Debian-1.8.0.9-1) Epiphany/2.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.7) Gecko/20060928 (Debian-1.8.0.7-1) Epiphany/2.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.7) Gecko/20060924 Epiphany/2.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Epiphany/2.14 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Epiphany/2.14 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.3) Gecko/20060523 Ubuntu/dapper Epiphany/2.14 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.13pre) Gecko/20070505 (Debian-1.8.0.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.13pre) Gecko/20070505 (Debian-1.8.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.13) Gecko/20060501 Epiphany/2.14",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.7) Gecko/20060928 (Debian-1.8.0.7-1) Epiphany/2.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051215 Epiphany/1.8.4.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20060208 Epiphany/1.8.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.7.12) Gecko/20051010 Epiphany/1.8.2 (Ubuntu) (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051010 Epiphany/1.8.2 (Ubuntu) (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; SunOS 5.11; en-US; rv:1.8.0.2) Gecko/20050405 Epiphany/1.7.1",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.5) Gecko/20050105 Epiphany/1.4.8",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.8) Gecko/20060628 Epiphany/1.4.8 (Debian)",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.8) Gecko/20050831 Epiphany/1.4.8 (Debian)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050927 Epiphany/1.4.8 (Debian)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050831 Epiphany/1.4.8 (Debian)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050513 Epiphany/1.4.8 (Debian)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.3) Gecko/20041007 Epiphany/1.4.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.3) Gecko/20040924 Epiphany/1.4.4 (Ubuntu)",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.7.3) Gecko/20040924 Epiphany/1.4.4 (Ubuntu)",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.7.3) Gecko/20040924 Epiphany/1.4.4",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.6) Gecko/20040413 Epiphany/1.2.6",
	"Mozilla/5.0 (X11; U; Linux; i686; en-US; rv:1.6) Gecko Epiphany/1.2.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.6) Gecko/20040114 Epiphany/1.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.6) Gecko/20040114 Epiphany/1.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030908 Epiphany/0.9.2",
	"Mozilla/5.0 (X11; U; Linux i586; en-US) Gecko/20030908 Epiphany/0.9.2",
	"Mozilla/5.0 (X11; U; Linux i686; pl-pl) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) epiphany-browser",
	"Mozilla/5.0 (X11; U; Linux i686; en-gb) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) epiphany-webkit",
	"Mozilla/4.0 (compatible; MSIE 5.23; Macintosh; PPC) Escape 5.1.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; x64; fr; rv:1.9.2.13) Gecko/20101203 Firebird/3.6.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.6b) Gecko/20031212 Firebird/0.7+",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.6a) Gecko/20031002 Firebird/0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5) Gecko/20031007 Firebird/0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.5) Gecko/20031007 Firebird/0.7",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.5) Gecko/20031007 Firebird/0.7",
	"Mozilla/5.0 (Windows; U; Win95; en-US; rv:1.5) Gecko/20031007 Firebird/0.7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.5) Gecko/20031026 Firebird/0.7",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.5a) Gecko/20030729 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.5a) Gecko/20030728 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.5a) Gecko/20030728 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5b) Gecko/20030819 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:11.0) Gecko Firefox/11.0",
	"Mozilla/5.0 (Windows NT 6.1; U;WOW64; de;rv:11.0) Gecko Firefox/11.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:11.0) Gecko Firefox/11.0",
	"Mozilla/6.0 (Macintosh; I; Intel Mac OS X 11_7_9; de-LI; rv:1.9b4) Gecko/2012010317 Firefox/10.0a4",
	"Mozilla/5.0 (Macintosh; I; Intel Mac OS X 11_7_9; de-LI; rv:1.9b4) Gecko/2012010317 Firefox/10.0a4",
	"Mozilla/5.0 (X11; Mageia; Linux x86_64; rv:10.0.9) Gecko/20100101 Firefox/10.0.9",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:9.0a2) Gecko/20111101 Firefox/9.0a2",
	"Mozilla/5.0 (Windows NT 6.2; rv:9.0.1) Gecko/20100101 Firefox/9.0.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:9.0) Gecko/20100101 Firefox/9.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:8.0; en_us) Gecko/20100101 Firefox/8.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:6.0) Gecko/20100101 Firefox/7.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:6.0a2) Gecko/20110613 Firefox/6.0a2",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:6.0a2) Gecko/20110612 Firefox/6.0a2",
	"Mozilla/5.0 (X11; Linux i686; rv:6.0) Gecko/20100101 Firefox/6.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:6.0) Gecko/20110814 Firefox/6.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:6.0) Gecko/20100101 Firefox/6.0 FirePHP/0.6",
	"Mozilla/5.0 (Windows NT 5.0; WOW64; rv:6.0) Gecko/20100101 Firefox/6.0",
	"Mozilla/5.0 (X11; Linux i686 on x86_64; rv:5.0a2) Gecko/20110524 Firefox/5.0a2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.28) Gecko/20120306 Firefox/5.0.1",
	"Mozilla/5.0 (Windows NT 6.1; U; ru; rv:5.0.1.6) Gecko/20110501 Firefox/5.0.1 Firefox/5.0.1",
	"Mozilla/5.0 (X11; U; Linux i586; de; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (X11; U; Linux amd64; rv:5.0) Gecko/20100101 Firefox/5.0 (Debian)",
	"Mozilla/5.0 (X11; U; Linux amd64; en-US; rv:5.0) Gecko/20110619 Firefox/5.0",
	"Mozilla/5.0 (X11; Linux) Gecko Firefox/5.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:5.0) Gecko/20100101 Firefox/5.0 FirePHP/0.5",
	"Mozilla/5.0 (X11; Linux x86_64; rv:5.0) Gecko/20100101 Firefox/5.0 Firefox/5.0",
	"Mozilla/5.0 (X11; Linux x86_64) Gecko Firefox/5.0",
	"Mozilla/5.0 (X11; Linux ppc; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (X11; Linux AMD64) Gecko Firefox/5.0",
	"Mozilla/5.0 (X11; FreeBSD amd64; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:5.0) Gecko/20110619 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:6.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 6.1.1; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 5.2; WOW64; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 5.1; U; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:2.0.1) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 5.0; WOW64; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (Windows NT 5.0; rv:5.0) Gecko/20100101 Firefox/5.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:2.2a1pre) Gecko/20110324 Firefox/4.2a1pre",
	"Mozilla/5.0 (X11; Linux x86_64; rv:2.2a1pre) Gecko/20100101 Firefox/4.2a1pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.2a1pre) Gecko/20110324 Firefox/4.2a1pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.2a1pre) Gecko/20110323 Firefox/4.2a1pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.2a1pre) Gecko/20110208 Firefox/4.2a1pre",
	"Mozilla/5.0 (X11; Linux x86_64; rv:2.0b9pre) Gecko/20110111 Firefox/4.0b9pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b9pre) Gecko/20101228 Firefox/4.0b9pre",
	"Mozilla/5.0 (Windows NT 5.1; rv:2.0b9pre) Gecko/20110105 Firefox/4.0b9pre",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0b8pre) Gecko/20101114 Firefox/4.0b8pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b8pre) Gecko/20101213 Firefox/4.0b8pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b8pre) Gecko/20101128 Firefox/4.0b8pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b8pre) Gecko/20101114 Firefox/4.0b8pre",
	"Mozilla/5.0 (Windows NT 5.1; rv:2.0b8pre) Gecko/20101127 Firefox/4.0b8pre",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:2.0b8) Gecko/20100101 Firefox/4.0b8",
	"Mozilla/4.0 (compatible; Intel Mac OS X 10.6; rv:2.0b8) Gecko/20100101 Firefox/4.0b8)",
	"Mozilla/5.0 (Windows NT 6.1; rv:2.0b7pre) Gecko/20100921 Firefox/4.0b7pre",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0b7) Gecko/20101111 Firefox/4.0b7",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0b7) Gecko/20100101 Firefox/4.0b7",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0b6pre) Gecko/20100903 Firefox/4.0b6pre",
	"Mozilla/5.0 (Windows NT 6.1; rv:2.0b6pre) Gecko/20100903 Firefox/4.0b6pre Firefox/4.0b6pre",
	"Mozilla/5.0 (X11; Linux x86_64; rv:2.0b4) Gecko/20100818 Firefox/4.0b4",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b3pre) Gecko/20100731 Firefox/4.0b3pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b13pre) Gecko/20110304 Firefox/4.0b13pre",
	"Mozilla/5.0 (Windows NT 5.1; rv:2.0b13pre) Gecko/20110223 Firefox/4.0b13pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b12pre) Gecko/20110204 Firefox/4.0b12pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b12pre) Gecko/20100101 Firefox/4.0b12pre",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0b11pre) Gecko/20110128 Firefox/4.0b11pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b11pre) Gecko/20110131 Firefox/4.0b11pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b11pre) Gecko/20110129 Firefox/4.0b11pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b11pre) Gecko/20110128 Firefox/4.0b11pre",
	"Mozilla/5.0 (Windows NT 6.1; rv:2.0b11pre) Gecko/20110126 Firefox/4.0b11pre",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:2.0b11pre) Gecko/20110126 Firefox/4.0b11pre",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:2.0b10pre) Gecko/20110118 Firefox/4.0b10pre",
	"Mozilla/5.0 (Windows NT 6.1; rv:2.0b10pre) Gecko/20110113 Firefox/4.0b10pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b10) Gecko/20100101 Firefox/4.0b10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:2.0b10) Gecko/20110126 Firefox/4.0b10",
	"Mozilla/5.0 (Windows NT 6.1; rv:2.0b10) Gecko/20110126 Firefox/4.0b10",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.1.3) Gecko/20091020 Ubuntu/10.04 (lucid) Firefox/4.0.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:2.0.1) Gecko/20110506 Firefox/4.0.1",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0.1) Gecko/20110518 Firefox/4.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:2.0.1) Gecko/20110606 Firefox/4.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:2.0) Gecko/20110307 Firefox/4.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:2.0) Gecko/20110404 Fedora/16-dev Firefox/4.0",
	"Mozilla/5.0 (X11; Arch Linux i686; rv:2.0) Gecko/20110321 Firefox/4.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru; rv:1.9.2.3) Gecko/20100401 Firefox/4.0 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows NT 6.1; rv:2.0) Gecko/20110319 Firefox/4.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:1.9) Gecko/20100101 Firefox/4.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.2) Gecko/20121223 Ubuntu/9.25 (jaunty) Firefox/3.8",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.2) Gecko/2008092313 Ubuntu/9.25 (jaunty) Firefox/3.8",
	"Mozilla/5.0 (X11; U; Linux i686; it-IT; rv:1.9.0.2) Gecko/2008092313 Ubuntu/9.25 (jaunty) Firefox/3.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.3) Gecko/20100401 Mozilla/5.0 (X11; U; Linux i686; it-IT; rv:1.9.0.2) Gecko/2008092313 Ubuntu/9.25 (jaunty) Firefox/3.8",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.3a5pre) Gecko/20100526 Firefox/3.7a5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru; rv:1.9.2b5) Gecko/20091204 Firefox/3.6b5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2b5) Gecko/20091204 Firefox/3.6b5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.2b5) Gecko/20091204 Firefox/3.6b5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.2) Gecko/20091218 Firefox 3.6b5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.2b4) Gecko/20091124 Firefox/3.6b4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2b4) Gecko/20091124 Firefox/3.6b4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2b1) Gecko/20091014 Firefox/3.6b1 GTB5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20090428 Firefox/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20090405 Firefox/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; ru-RU; rv:1.9.2a1pre) Gecko/20090405 Ubuntu/9.04 (jaunty) Firefox/3.6a1pre",
	"Mozilla/5.0 (Windows; Windows NT 5.1; es-ES; rv:1.9.2a1pre) Gecko/20090402 Firefox/3.6a1pre",
	"Mozilla/5.0 (Windows; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090402 Firefox/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.2a1pre) Gecko/20090402 Firefox/3.6a1pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.9) Gecko/20100915 Gentoo Firefox/3.6.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.9) Gecko/20100827 Red Hat/3.6.9-2.el6 Firefox/3.6.9",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.9.2.9) Gecko/20100913 Firefox/3.6.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; rv:1.9.2.9) Gecko/20100913 Firefox/3.6.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.2.9) Gecko/20100824 Firefox/3.6.9 ( .NET CLR 3.5.30729; .NET CLR 4.0.20506)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-GB; rv:1.9.2.9) Gecko/20100824 Firefox/3.6.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6;en-US; rv:1.9.2.9) Gecko/20100824 Firefox/3.6.9",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.9.2.8) Gecko/20101230 Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.8) Gecko/20100804 Gentoo Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.8) Gecko/20100723 SUSE/3.6.8-0.1.1 Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; Linux i686; zh-CN; rv:1.9.2.8) Gecko/20100722 Ubuntu/10.04 (lucid) Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.2.8) Gecko/20100723 Ubuntu/10.04 (lucid) Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; Linux i686; fi-FI; rv:1.9.2.8) Gecko/20100723 Ubuntu/10.04 (lucid) Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.8) Gecko/20100727 Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.9.2.8) Gecko/20100725 Gentoo Firefox/3.6.8",
	"Mozilla/5.0 (X11; U; FreeBSD i386; de-CH; rv:1.9.2.8) Gecko/20100729 Firefox/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; pt-BR; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; it; rv:1.9.2.8) Gecko/20100722 AskTbADAP/3.9.1.14019 Firefox/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; he; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr; rv:1.9.2.8) Gecko/20100722 Firefox 3.6.8 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.2.8) Gecko/20100722 Firefox 3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.2.3) Gecko/20121221 Firefox/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; zh-TW; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.8 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.7) Gecko/20100809 Fedora/3.6.7-1.fc14 Firefox/3.6.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.7) Gecko/20100723 Fedora/3.6.7-1.fc13 Firefox/3.6.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.7) Gecko/20100726 CentOS/3.6-3.el5.centos Firefox/3.6.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; hu; rv:1.9.2.7) Gecko/20100713 Firefox/3.6.7 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9.2.8) Gecko/20100722 Firefox/3.6.7 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-PT; rv:1.9.2.7) Gecko/20100713 Firefox/3.6.7 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.6) Gecko/20100628 Ubuntu/10.04 (lucid) Firefox/3.6.6 GTB7.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.6) Gecko/20100628 Ubuntu/10.04 (lucid) Firefox/3.6.6 GTB7.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.6) Gecko/20100628 Ubuntu/10.04 (lucid) Firefox/3.6.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.6) Gecko/20100628 Ubuntu/10.04 (lucid) Firefox/3.6.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; pt-PT; rv:1.9.2.6) Gecko/20100625 Firefox/3.6.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; it; rv:1.9.2.6) Gecko/20100625 Firefox/3.6.6 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.6) Gecko/20100625 Firefox/3.6.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-CN; rv:1.9.2.6) Gecko/20100625 Firefox/3.6.6 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; nl; rv:1.9.2.6) Gecko/20100625 Firefox/3.6.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9.2.6) Gecko/20100625 Firefox/3.6.6 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; de-AT; rv:1.9.1.8) Gecko/20100625 Firefox/3.6.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.4) Gecko/20100614 Ubuntu/10.04 (lucid) Firefox/3.6.4",
	"Mozilla/5.0 (X11; U; Linux i686; fa; rv:1.8.1.4) Gecko/20100527 Firefox/3.6.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.4) Gecko/20100625 Gentoo Firefox/3.6.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-TW; rv:1.9.2.4) Gecko/20100611 Firefox/3.6.4 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru; rv:1.9.2.4) Gecko/20100513 Firefox/3.6.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ja; rv:1.9.2.4) Gecko/20100611 Firefox/3.6.4 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; cs; rv:1.9.2.4) Gecko/20100513 Firefox/3.6.4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-CN; rv:1.9.2.4) Gecko/20100513 Firefox/3.6.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.9.2.4) Gecko/20100513 Firefox/3.6.4 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.9.2.4) Gecko/20100523 Firefox/3.6.4 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.4) Gecko/20100527 Firefox/3.6.4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.4) Gecko/20100527 Firefox/3.6.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.4) Gecko/20100523 Firefox/3.6.4 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.4) Gecko/20100513 Firefox/3.6.4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.2.4) Gecko/20100523 Firefox/3.6.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.9.2.4) Gecko/20100611 Firefox/3.6.4 GTB7.0 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.2.4) Gecko/20100513 Firefox/3.6.4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.2.4) Gecko/20100503 Firefox/3.6.4 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nb-NO; rv:1.9.2.4) Gecko/20100611 Firefox/3.6.4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ko; rv:1.9.2.4) Gecko/20100523 Firefox/3.6.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.3pre) Gecko/20100405 Firefox/3.6.3plugin1 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; he; rv:1.9.1b4pre) Gecko/20100405 Firefox/3.6.3plugin1",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.2.3) Gecko/20100403 Fedora/3.6.3-4.fc13 Firefox/3.6.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.2.3) Gecko/20100401 SUSE/3.6.3-1.1 Firefox/3.6.3",
	"Mozilla/5.0 (X11; U; Linux i686; ko-KR; rv:1.9.2.3) Gecko/20100423 Ubuntu/10.04 (lucid) Firefox/3.6.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.3) Gecko/20100404 Ubuntu/10.04 (lucid) Firefox/3.6.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 GTB7.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.3",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.3) Gecko/20100423 Ubuntu/10.04 (lucid) Firefox/3.6.3",
	"Mozilla/5.0 (X11; U; Linux AMD64; en-US; rv:1.9.2.3) Gecko/20100403 Ubuntu/10.10 (maverick) Firefox/3.6.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; pl; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; it; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; hu; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; es-ES; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; es-ES; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 GTB7.0 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; es-ES; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; cs; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ca; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.9.2.28) Gecko/20120306 Firefox/3.6.28",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9.2.28) Gecko/20120306 AskTbSTC-SRS/3.13.1.18132 Firefox/3.6.28 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.28) Gecko/20120306 Firefox/3.6.28 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.2.25) Gecko/20111212 Firefox/3.6.25 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.2.24) Gecko/20111101 SUSE/3.6.24-0.2.1 Firefox/3.6.24",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.2.24) Gecko/20111103 Firefox/3.6.24",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.2.24) Gecko/20111103 Firefox/3.6.24",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; fr; rv:1.9.2.23) Gecko/20110920 Firefox/3.6.23",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en-US; rv:1.9.2.22) Gecko/20110902 Firefox/3.6.22",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; it; rv:1.9.2.22) Gecko/20110902 Firefox/3.6.22",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.21) Gecko/20110830 Ubuntu/10.10 (maverick) Firefox/3.6.21",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.2.20) Gecko/20110805 Ubuntu/10.04 (lucid) Firefox/3.6.20",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.20) Gecko/20110804 Red Hat/3.6-2.el5 Firefox/3.6.20",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; hu; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hu; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.20) Gecko/20110803 AskTbFWV5/3.13.0.17701 Firefox/3.6.20 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2 GTB7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.2) Gecko/20100316 AskTbSPC2/3.9.1.14019 Firefox/3.6.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2 GTB6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2 ( .NET CLR 3.0.04506.648)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2 ( .NET CLR 3.0.04506.30)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.7; en-US; rv:1.9.2.2) Gecko/20100316 Firefox/3.6.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.10pre) Gecko/20100902 Ubuntu/9.10 (karmic) Firefox/3.6.1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.19",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en-GB; rv:1.9.2.19) Gecko/20110707 Firefox/3.6.19",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.9.2.18) Gecko/20110628 Ubuntu/10.10 (maverick) Firefox/3.6.18",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.9.2.18) Gecko/20110614 Firefox/3.6.18 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.2.18) Gecko/20110628 Ubuntu/10.10 (maverick) Firefox/3.6.18",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.18) Gecko/20110628 Ubuntu/10.10 (maverick) Firefox/3.6.18",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.18) Gecko/20110615 Ubuntu/10.10 (maverick) Firefox/3.6.18",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; pt-BR; rv:1.9.2.18) Gecko/20110614 Firefox/3.6.18 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ar; rv:1.9.2.18) Gecko/20110614 Firefox/3.6.18",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pt-BR; rv:1.9.2.18) Gecko/20110614 Firefox/3.6.18 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.2.18) Gecko/20110614 Firefox/3.6.18 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-GB; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17",
	"Mozilla/5.0 (X11; Linux i686 on x86_64; rv:5.0) Gecko/20100101 Firefox/3.6.17 Firefox/3.6.17",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sl; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hu; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.2.17) Gecko/20110420 Firefox/3.6.17 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (X11; U; Linux x86_64; ja-JP; rv:1.9.2.16) Gecko/20110323 Ubuntu/10.10 (maverick) Firefox/3.6.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.16) Gecko/20110323 Ubuntu/9.10 (karmic) Firefox/3.6.16 FirePHP/0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.2.16) Gecko/20110319 Firefox/3.6.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr; rv:1.9.2.16) Gecko/20110319 Firefox/3.6.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pl; rv:1.9.2.16) Gecko/20110319 Firefox/3.6.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ko; rv:1.9.2.16) Gecko/20110319 Firefox/3.6.16 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.2.16) Gecko/20110319 Firefox/3.6.16 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en; rv:1.9.1.13) Gecko/20100914 Firefox/3.6.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.2.16) Gecko/20110319 AskTbUTR/3.11.3.15590 Firefox/3.6.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.16pre) Gecko/20110304 Ubuntu/10.10 (maverick) Firefox/3.6.15pre",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.2.15) Gecko/20110303 Ubuntu/8.04 (hardy) Firefox/3.6.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.15) Gecko/20110303 Ubuntu/10.04 (lucid) Firefox/3.6.15 FirePHP/0.5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.15) Gecko/20110330 CentOS/3.6-1.el5.centos Firefox/3.6.15",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; es-ES; rv:1.9.2.15) Gecko/20110303 Firefox/3.6.15",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.15) Gecko/20110303 Firefox/3.6.15 ( .NET CLR 3.5.30729; .NET4.0C) FirePHP/0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.2.15) Gecko/20110303 AskTbBT4/3.11.3.15590 Firefox/3.6.15 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.15) Gecko/20110303 Firefox/3.6.15 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.14pre) Gecko/20110105 Firefox/3.6.14pre",
	"Mozilla/5.0 (X11; U; Linux armv7l; en-US; rv:1.9.2.14) Gecko/20110224 Firefox/3.6.14 MB860/Version.0.43.3.MB860.AmericaMovil.en.MX",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN; rv:1.9.2.14) Gecko/20110218 Firefox/3.6.14",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-AU; rv:1.9.2.14) Gecko/20110218 Firefox/3.6.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.2.14) Gecko/20110218 Firefox/3.6.14 GTB7.1 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.2.13) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9.2.13) Gecko/20101206 Ubuntu/10.04 (lucid) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; nb-NO; rv:1.9.2.13) Gecko/20101206 Ubuntu/10.04 (lucid) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.2.13) Gecko/20101206 Ubuntu/10.04 (lucid) Firefox/3.6.13 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.2.13) Gecko/20110103 Fedora/3.6.13-1.fc14 Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.2.13) Gecko/20101203 Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.13) Gecko/20101223 Gentoo Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.13) Gecko/20101219 Gentoo Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.13) Gecko/20101206 Red Hat/3.6-3.el4 Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.13) Gecko/20101206 Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-NZ; rv:1.9.2.13) Gecko/20101206 Ubuntu/10.10 (maverick) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.2.13) Gecko/20101206 Ubuntu/9.10 (karmic) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.2.13) Gecko/20101206 Red Hat/3.6-2.el5 Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; da-DK; rv:1.9.2.13) Gecko/20101206 Ubuntu/10.10 (maverick) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux MIPS32 1074Kf CPS QuadCore; en-US; rv:1.9.2.13) Gecko/20110103 Fedora/3.6.13-1.fc14 Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.2.13) Gecko/20101206 Ubuntu/10.10 (maverick) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.9.2.13) Gecko/20101209 Fedora/3.6.13-1.fc13 Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.2.13) Gecko/20101206 Ubuntu/9.10 (karmic) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.13) Gecko/20101209 CentOS/3.6-2.el5.centos Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.13) Gecko/20101206 Ubuntu/10.10 (maverick) Firefox/3.6.13",
	"Mozilla/5.0 (X11; U; NetBSD i386; en-US; rv:1.9.2.12) Gecko/20101030 Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-MX; rv:1.9.2.12) Gecko/20101027 Ubuntu/10.04 (lucid) Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.2.12) Gecko/20101027 Fedora/3.6.12-1.fc13 Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.2.12) Gecko/20101026 SUSE/3.6.12-0.7.1 Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.12) Gecko/20101102 Gentoo Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.12) Gecko/20101102 Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux ppc; fr; rv:1.9.2.12) Gecko/20101027 Ubuntu/10.10 (maverick) Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux i686; ko-KR; rv:1.9.2.12) Gecko/20101027 Ubuntu/10.10 (maverick) Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.12) Gecko/20101114 Gentoo Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.2.12) Gecko/20101027 Ubuntu/10.10 (maverick) Firefox/3.6.12 GTB7.1",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.12) Gecko/20101027 Fedora/3.6.12-1.fc13 Firefox/3.6.12",
	"Mozilla/5.0 (X11; FreeBSD x86_64; rv:2.0) Gecko/20100101 Firefox/3.6.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN; rv:1.9.2.12) Gecko/20101026 Firefox/3.6.12 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sv-SE; rv:1.9.2.12) Gecko/20101026 Firefox/3.6.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.12) Gecko/20101026 Firefox/3.6.12 (.NET CLR 2.0.50727; .NET CLR 3.0.30729; .NET CLR 3.5.30729; .NET CLR 3.5.21022)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; de; rv:1.9.2.12) Gecko/20101026 Firefox/3.6.12 GTB5",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.9.2.11) Gecko/20101028 CentOS/3.6-2.el5.centos Firefox/3.6.11",
	"Mozilla/5.0 (X11; U; Linux armv7l; en-GB; rv:1.9.2.3pre) Gecko/20100723 Firefox/3.6.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; ru; rv:1.9.2.11) Gecko/20101012 Firefox/3.6.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; rv:1.9.2.11) Gecko/20101012 Firefox/3.6.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9.2.11) Gecko/20101012 Firefox/3.6.11 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; zh-CN; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; pt-BR; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10 GTB7.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; el-GR; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10 GTB7.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ; rv:1.9.2.10) Gecko/20100915 Ubuntu/10.04 (lucid) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.2.10) Gecko/20100915 Ubuntu/10.04 (lucid) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.9.2.10) Gecko/20100914 Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.10) Gecko/20100915 Ubuntu/9.04 (jaunty) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.2.11) Gecko/20101013 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-CA; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.10) Gecko/20100922 Ubuntu/10.10 (maverick) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.10) Gecko/20100915 Ubuntu/9.10 (karmic) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.10) Gecko/20100915 Ubuntu/10.04 (lucid) Firefox/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.10) Gecko/20100914 SUSE/3.6.10-0.3.1 Firefox/3.6.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ro; rv:1.9.2.10) Gecko/20100914 Firefox/3.6.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; nl; rv:1.9.2.10) Gecko/20100914 Firefox/3.6.10 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr; rv:1.9.2.10) Gecko/20100914 Firefox/3.6.10 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.1) Gecko/20100122 firefox/3.6.1",
	"Mozilla/5.0(Windows; U; Windows NT 7.0; rv:1.9.2) Gecko/20100101 Firefox/3.6",
	"Mozilla/5.0(Windows; U; Windows NT 5.2; rv:1.9.2) Gecko/20100101 Firefox/3.6",
	"Mozilla/5.0 (X11; U; x86_64 Linux; en_GB, en_US; rv:1.9.2) Gecko/20100115 Firefox/3.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2) Gecko/20100222 Ubuntu/10.04 (lucid) Firefox/3.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2) Gecko/20100130 Gentoo Firefox/3.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.2) Gecko/20100308 Ubuntu/10.04 (lucid) Firefox/3.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.2pre) Gecko/20100312 Ubuntu/9.04 (jaunty) Firefox/3.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2) Gecko/20100128 Gentoo Firefox/3.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2) Gecko/20100115 Ubuntu/10.04 (lucid) Firefox/3.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2) Gecko/20100115 Firefox/3.6 FirePHP/0.4",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0) Gecko/20100101 Firefox/3.6",
	"Mozilla/5.0 (X11; FreeBSD i686) Firefox/3.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru-RU; rv:1.9.2) Gecko/20100105 MRA 5.6 (build 03278) Firefox/3.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; lt; rv:1.9.2) Gecko/20100115 Firefox/3.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.3a3pre) Gecko/20100306 Firefox3.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.8) Gecko/20100806 Firefox/3.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.17) Gecko/20110420 Firefox/3.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.2.3) Gecko/20100401 Firefox/3.6;MEGAUPLOAD 1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ar; rv:1.9.2) Gecko/20100115 Firefox/3.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ru; rv:1.9.2) Gecko/20100115 Firefox/3.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b5pre) Gecko/20090517 Firefox/3.5b4pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b4pre) Gecko/20090409 Firefox/3.5b4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b4pre) Gecko/20090401 Firefox/3.5b4pre",
	"Mozilla/5.0 (X11; U; Linux i686; nl-NL; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4 GTB5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; fr; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.1b4) Gecko/20090423 Firefox/3.5b4 GTB5",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.1.9) Gecko/20100402 Ubuntu/9.10 (karmic) Firefox/3.5.9 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.1.9) Gecko/20100330 Fedora/3.5.9-2.fc12 Firefox/3.5.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.9) Gecko/20100317 SUSE/3.5.9-0.1.1 Firefox/3.5.9 GTB7.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-CL; rv:1.9.1.9) Gecko/20100402 Ubuntu/9.10 (karmic) Firefox/3.5.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ; rv:1.9.1.9) Gecko/20100317 SUSE/3.5.9-0.1.1 Firefox/3.5.9",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.1.9) Gecko/20100401 Ubuntu/9.10 (karmic) Firefox/3.5.9",
	"Mozilla/5.0 (X11; U; Linux i686; hu-HU; rv:1.9.1.9) Gecko/20100330 Fedora/3.5.9-1.fc12 Firefox/3.5.9",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.1.9) Gecko/20100317 SUSE/3.5.9-0.1 Firefox/3.5.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.9) Gecko/20100401 Ubuntu/9.10 (karmic) Firefox/3.5.9 GTB7.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.9) Gecko/20100315 Ubuntu/9.10 (karmic) Firefox/3.5.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.4) Gecko/20091028 Ubuntu/9.10 (karmic) Firefox/3.5.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; tr; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; hu; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; et; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; nl; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-ES; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9 GTB5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.2.13) Gecko/20101203 Firefox/3.5.9 (de)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1.9) Gecko/20100315 Firefox/3.5.9 GTB7.0 (.NET CLR 3.0.30618)",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.9.1.8) Gecko/20100216 Fedora/3.5.8-1.fc12 Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.1.8) Gecko/20100216 Fedora/3.5.8-1.fc11 Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.8) Gecko/20100318 Gentoo Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; Linux i686; zh-CN; rv:1.9.1.8) Gecko/20100216 Fedora/3.5.8-1.fc12 Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; Linux i686; ja-JP; rv:1.9.1.8) Gecko/20100216 Fedora/3.5.8-1.fc12 Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.9.1.8) Gecko/20100214 Ubuntu/9.10 (karmic) Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.8) Gecko/20100214 Ubuntu/9.10 (karmic) Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.8) Gecko/20100202 Firefox/3.5.8",
	"Mozilla/5.0 (X11; U; FreeBSD i386; ja-JP; rv:1.9.1.8) Gecko/20100305 Firefox/3.5.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; sl; rv:1.9.1.8) Gecko/20100202 Firefox/3.5.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.8) Gecko/20100202 Firefox/3.5.8 (.NET CLR 3.5.30729) FirePHP/0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.9.1.8) Gecko/20100202 Firefox/3.5.8 GTB6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.8) Gecko/20100202 Firefox/3.5.8 GTB7.0 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2) Gecko/20100305 Gentoo Firefox/3.5.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ; rv:1.9.1.7) Gecko/20100106 Ubuntu/9.10 (karmic) Firefox/3.5.7",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.1.7) Gecko/20091222 SUSE/3.5.7-1.1.1 Firefox/3.5.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.9.1.7) Gecko/20091221 Firefox/3.5.7 GTB6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1.7) Gecko/20091221 Firefox/3.5.7 (.NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.30729; .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; fr; rv:1.9.1.7) Gecko/20091221 Firefox/3.5.7 (.NET CLR 3.0.04506.648)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fa; rv:1.9.1.7) Gecko/20091221 Firefox/3.5.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.7) Gecko/20091221 MRA 5.5 (build 02842) Firefox/3.5.7 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.6) Gecko/20091215 Ubuntu/9.10 (karmic) Firefox/3.5.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.6) Gecko/20100117 Gentoo Firefox/3.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; zh-CN; rv:1.9.1.6) Gecko/20091216 Fedora/3.5.6-1.fc11 Firefox/3.5.6 GTB6",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.1.6) Gecko/20091201 SUSE/3.5.6-1.1.1 Firefox/3.5.6 GTB6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.6) Gecko/20100118 Gentoo Firefox/3.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.1.6) Gecko/20091215 Ubuntu/9.10 (karmic) Firefox/3.5.6 GTB6",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.6) Gecko/20091215 Ubuntu/9.10 (karmic) Firefox/3.5.6 GTB7.0",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.6) Gecko/20091215 Ubuntu/9.10 (karmic) Firefox/3.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.6) Gecko/20091201 SUSE/3.5.6-1.1.1 Firefox/3.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; cs-CZ; rv:1.9.1.6) Gecko/20100107 Fedora/3.5.6-1.fc12 Firefox/3.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; ca; rv:1.9.1.6) Gecko/20091215 Ubuntu/9.10 (karmic) Firefox/3.5.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; it; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; id; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.6) Gecko/20091201 MRA 5.4 (build 02647) Firefox/3.5.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 (.NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) Gecko/20091201 MRA 5.5 (build 02842) Firefox/3.5.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) Gecko/20091201 MRA 5.5 (build 02842) Firefox/3.5.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 GTB6 (.NET CLR 3.5.30729) FBSMTWB",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 (.NET CLR 3.5.30729) FBSMTWB",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.5) Gecko/20091109 Ubuntu/9.10 (karmic) Firefox/3.5.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.8pre) Gecko/20091227 Ubuntu/9.10 (karmic) Firefox/3.5.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.5) Gecko/20091114 Gentoo Firefox/3.5.5",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.1.5) Gecko/20091102 Firefox/3.5.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; uk; rv:1.9.1.5) Gecko/20091102 Firefox/3.5.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.5) Gecko/20091102 MRA 5.5 (build 02842) Firefox/3.5.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ru; rv:1.9.1.5) Gecko/20091102 MRA 5.5 (build 02842) Firefox/3.5.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1.5) Gecko/20091102 Firefox/3.5.5 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; zh-CN; rv:1.9.1.5) Gecko/Firefox/3.5.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.5) Gecko/20091102 MRA 5.5 (build 02842) Firefox/3.5.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.5) Gecko/20091102 MRA 5.5 (build 02842) Firefox/3.5.5",
	"Mozilla/5.0 (Windows NT 5.1; U; zh-cn; rv:1.8.1) Gecko/20091102 Firefox/3.5.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; pl; rv:1.9.1.5) Gecko/20091102 Firefox/3.5.5 FBSMTWB",
	"Mozilla/5.0 (X11; U; Linux x86_64; ja; rv:1.9.1.4) Gecko/20091016 SUSE/3.5.4-1.1.2 Firefox/3.5.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.4) Gecko/20091016 Firefox/3.5.4 (.NET CLR 3.5.30729) FBSMTWB",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1.4) Gecko/20091007 Firefox/3.5.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU; rv:1.9.1.4) Gecko/20091016 Firefox/3.5.4 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.1.4) Gecko/20091016 Firefox/3.5.4 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1.4) Gecko/20091007 Firefox/3.5.4",
	"Mozilla/4.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.2) Gecko/2010324480 Firefox/3.5.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.5) Gecko/20091109 Ubuntu/9.10 (karmic) Firefox/3.5.3pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.3) Gecko/20090914 Slackware/13.0_stable Firefox/3.5.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.3) Gecko/20090913 Firefox/3.5.3",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.1.3) Gecko/20091020 Ubuntu/9.10 (karmic) Firefox/3.5.3",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.1.3) Gecko/20090913 Firefox/3.5.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.3) Gecko/20090919 Firefox/3.5.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.3) Gecko/20090912 Gentoo Firefox/3.5.3 FirePHP/0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 GTB5",
	"Mozilla/5.0 (X11; U; FreeBSD i386; ru-RU; rv:1.9.1.3) Gecko/20090913 Firefox/3.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.3) Gecko/20100401 Firefox/3.5.3;MEGAUPLOAD 1.0 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de-DE; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ko; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fi; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 (.NET CLR 2.0.50727; .NET CLR 3.0.30618; .NET CLR 3.5.21022; .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; bg; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ko; rv:1.9.1.3) Gecko/20090824 Firefox/3.5.3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl; rv:1.9.1.2) Gecko/20090911 Slackware Firefox/3.5.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.2) Gecko/20090803 Slackware Firefox/3.5.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.2) Gecko/20090803 Firefox/3.5.2 Slackware",
	"Mozilla/5.0 (X11; U; Linux i686; ru-RU; rv:1.9.1.2) Gecko/20090804 Firefox/3.5.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.2) Gecko/20090729 Slackware/13.0 Firefox/3.5.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); fr; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pl; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2 GTB7.1 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-MX; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; uk; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.9.1.2) Gecko/20090729 Firefox/3.5.2 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.16) Gecko/20101130 Firefox/3.5.16 FirePHP/0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1.16) Gecko/20101130 AskTbMYC/3.9.1.14019 Firefox/3.5.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; it; rv:1.9.1.16) Gecko/20101130 Firefox/3.5.16 GTB7.1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.16) Gecko/20101130 MRA 5.4 (build 02647) Firefox/3.5.16 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.16) Gecko/20101130 Firefox/3.5.16 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.16) Gecko/20101130 AskTbPLTV5/3.8.0.12304 Firefox/3.5.16 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.1.16) Gecko/20101130 Firefox/3.5.16 GTB7.1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.1.16) Gecko/20101130 Firefox/3.5.16 GTB7.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.1.15) Gecko/20101027 Fedora/3.5.15-1.fc12 Firefox/3.5.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.1.15) Gecko/20101027 Fedora/3.5.15-1.fc12 Firefox/3.5.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ru; rv:1.9.1.13) Gecko/20100914 Firefox/3.5.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.12) Gecko/2009070611 Firefox/3.5.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9.1.12) Gecko/20100824 MRA 5.7 (build 03755) Firefox/3.5.12",
	"Mozilla/5.0 (X11; U; Linux; en-US; rv:1.9.1.11) Gecko/20100720 Firefox/3.5.11",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1.11) Gecko/20100701 Firefox/3.5.11 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.9.1.11) Gecko/20100701 Firefox/3.5.11 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hu; rv:1.9.1.11) Gecko/20100701 Firefox/3.5.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.10) Gecko/20100504 Firefox/3.5.11 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.1.10) Gecko/20100506 SUSE/3.5.10-0.1.1 Firefox/3.5.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1.10) Gecko/20100504 Firefox/3.5.10 GTB7.0 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; rv:1.9.1.1) Gecko/20090716 Linux Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.3) Gecko/20100524 Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.1) Gecko/20090716 Linux Mint/7 (Gloria) Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.1) Gecko/20090716 Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.1) Gecko/20090714 SUSE/3.5.1-1.1 Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux x86; rv:1.9.1.1) Gecko/20090716 Linux Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux i686; nl-NL; rv:1.9.0.19) Gecko/20090720 Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.2pre) Gecko/20090729 Ubuntu/9.04 (jaunty) Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1 GTB5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.1) Gecko/20090722 Gentoo Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.1) Gecko/20090714 SUSE/3.5.1-1.1 Firefox/3.5.1",
	"Mozilla/5.0 (X11; U; DragonFly i386; de; rv:1.9.1) Gecko/20090720 Firefox/3.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.1) Gecko/20090718 Firefox/3.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; tr; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sv-SE; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1 GTB5 (.NET CLR 4.0.20506)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1.1) Gecko/20090715 Firefox/3.5.1 GTB5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11;U; Linux i686; en-GB; rv:1.9.1) Gecko/20090624 Ubuntu/9.04 (jaunty) Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1) Gecko/20090630 Firefox/3.5 GTB6",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.9.1) Gecko/20090624 Firefox/3.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux i686; it-IT; rv:1.9.0.2) Gecko/2008092313 Ubuntu/9.04 (jaunty) Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.1) Gecko/20090624 Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.9.1) Gecko/20090624 Ubuntu/9.04 (jaunty) Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1) Gecko/20090701 Ubuntu/9.04 (jaunty) Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-us; rv:1.9.0.2) Gecko/2008092313 Ubuntu/9.04 (jaunty) Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1) Gecko/20090624 Ubuntu/8.04 (hardy) Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1) Gecko/20090624 Firefox/3.5",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); de; rv:1.9.1) Gecko/20090624 Firefox/3.5",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.9.1) Gecko/20090703 Firefox/3.5",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.9.0.10) Gecko/20090624 Firefox/3.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; pl; rv:1.9.1) Gecko/20090624 Firefox/3.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; es-ES; rv:1.9.1) Gecko/20090624 Firefox/3.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1) Gecko/20090612 Firefox/3.5 (.NET CLR 4.0.20506)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1) Gecko/20090612 Firefox/3.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1) Gecko/20090624 Firefox/3.5 (.NET CLR 4.0.20506)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1) Gecko/20090624 Firefox/3.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-TW; rv:1.9.1) Gecko/20090624 Firefox/3.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1b3pre) Gecko/20090105 Firefox/3.1b3pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.1b3pre) Gecko/20090204 Firefox/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b3) Gecko/20090327 Fedora/3.1-0.11.beta3.fc11 Firefox/3.1b3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b3) Gecko/20090312 Firefox/3.1b3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b3) Gecko/20090407 Firefox/3.1b3",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; pl; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3 GTB5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1) Gecko/20090624 Firefox/3.1b3;MEGAUPLOAD 1.0 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3 GTB5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-AR; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b3) Gecko/20090405 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.1b3) Gecko/20090305 Firefox/3.1b3 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; x64; en-US; rv:1.9.1b2pre) Gecko/20081026 Firefox/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0 x64; en-US; rv:1.9.1b2pre) Gecko/20081026 Firefox/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0 ; x64; en-US; rv:1.9.1b2pre) Gecko/20081026 Firefox/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1 ; x64; en-US; rv:1.9.1b2pre) Gecko/20081026 Firefox/3.1b2pre",
	"Mozilla/5.0 (X11; U; DragonFly i386; de; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de-AT; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sv-SE; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; it; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-AT; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; ko; rv:1.9.1b2) Gecko/20081201 Firefox/3.1b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.9.1b1) Gecko/20081007 Firefox/3.1b1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b2) Gecko/20081127 Firefox/3.1b1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.0.2) Gecko/2008092313 Firefox/3.1.6",
	"Mozilla/4.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.7) Gecko/2008398325 Firefox/3.1.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b3) Gecko/20090327 GNU/Linux/x86_64 Firefox/3.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.6pre) Gecko/2009011606 Firefox/3.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080716 Firefox/3.07",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.0.8) Gecko/2009032609 Firefox/3.07",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.03",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9pre) Gecko/2008040318 Firefox/3.0pre (Swiftfox)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b5pre) Gecko/2008030706 Firefox/3.0b5pre",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.9b5) Gecko/2008032620 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux x86_64; pt-BR; rv:1.9b5) Gecko/2008041515 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9pre) Gecko/2008042312 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9b5) Gecko/2008041816 Fedora/3.0-0.55.beta5.fc9 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9b5) Gecko/2008040514 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; tr-TR; rv:1.9b5) Gecko/2008032600 SUSE/2.9.95-25.1 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9b5) Gecko/2008032600 SUSE/2.9.95-25.1 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9b5) Gecko/2008050509 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.9b5) Gecko/2008041514 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b5) Gecko/2008050509 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9b5) Gecko/2008041514 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9b5) Gecko/2008050509 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9b5) Gecko/2008041514 Firefox/3.0b5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.9b5) Gecko/2008032620 Firefox/3.0b5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9b5) Gecko/2008032620 Firefox/3.0b5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; nl; rv:1.9b5) Gecko/2008032620 Firefox/3.0b5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; fr; rv:1.9b5) Gecko/2008032620 Firefox/3.0b5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9b5) Gecko/2008032620 Firefox/3.0b5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9b5) Gecko/2008032620 Firefox/3.0b5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en-GB; rv:1.9b5) Gecko/2008032619 Firefox/3.0b5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b4pre) Gecko/2008021714 Firefox/3.0b4pre (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b4pre) Gecko/2008021712 Firefox/3.0b4pre (Swiftfox)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b4pre) Gecko/2008020708 Firefox/3.0b4pre",
	"Mozilla/5.0 (X11; U; Windows NT 5.0; en-US; rv:1.9b4) Gecko/2008030318 Firefox/3.0b4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9b4) Gecko/2008040813 Firefox/3.0b4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9b4) Gecko/2008031318 Firefox/3.0b4",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9b4) Gecko/2008030800 SUSE/2.9.94-4.2 Firefox/3.0b4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b4) Gecko/2008031317 Firefox/3.0b4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pl; rv:1.9b4) Gecko/2008030714 Firefox/3.0b4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.9b4) Gecko/2008030714 Firefox/3.0b4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9b4) Gecko/2008030714 Firefox/3.0b4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl; rv:1.9b4) Gecko/2008030714 Firefox/3.0b4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; lt; rv:1.9b4) Gecko/2008030714 Firefox/3.0b4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; it; rv:1.9b4) Gecko/2008030317 Firefox/3.0b4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9b3pre) Gecko/2008020509 Firefox/3.0b3pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9b3pre) Gecko/2008011321 Firefox/3.0b3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b3pre) Gecko/2008020507 Firefox/3.0b3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b3) Gecko/2008020513 Firefox/3.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9b3) Gecko/2008020514 Firefox/3.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9b3) Gecko/2008020514 Firefox/3.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9b3) Gecko/2008020514 Firefox/3.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b3) Gecko/2008020514 Firefox/3.0b3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b2) Gecko/2007121016 Firefox/3.0b2",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9b2) Gecko/2007121016 Firefox/3.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9b2) Gecko/2007121120 Firefox/3.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-AR; rv:1.9b2) Gecko/2007121120 Firefox/3.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b1) Gecko/2007110703 Firefox/3.0b1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b3pre) Gecko/2008010415 Firefox/3.0b",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.9a2) Gecko/20080530 Firefox/3.0a2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20060814 Firefox/3.0a1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.9a1) Gecko/20061204 Firefox/3.0a1",
	"Mozilla/5.0 (Macintosh; I; PPC Mac OS X Mach-O; en-US; rv:1.9a1) Gecko/20061204 Firefox/3.0a1",
	"Mozilla/6.0 (Windows; U; Windows NT 7.0; en-US; rv:1.9.0.8) Gecko/2009032609 Firefox/3.0.9 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.9) Gecko/2009042114 Ubuntu/9.04 (jaunty) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.0.9) Gecko/2009042114 Ubuntu/9.04 (jaunty) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.9) Gecko/2009042113 Ubuntu/8.10 (intrepid) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.9) Gecko/2009042114 Ubuntu/9.04 (jaunty) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.9) Gecko/2009042113 Ubuntu/8.10 (intrepid) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.7) Gecko/2009030422 Kubuntu/8.10 (intrepid) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.9) Gecko/2009042113 Ubuntu/9.04 (jaunty) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.9) Gecko/2009042113 Ubuntu/8.04 (hardy) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; fi-FI; rv:1.9.0.9) Gecko/2009042113 Ubuntu/9.04 (jaunty) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.9.0.9) Gecko/2009042113 Ubuntu/9.04 (jaunty) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.9) Gecko/2009042113 Ubuntu/8.10 (intrepid) Firefox/3.0.9 GTB5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.9) Gecko/2009042113 Linux Mint/6 (Felicia) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.9) Gecko/2009041408 Red Hat/3.0.9-1.el5 Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.9) Gecko/2009040820 Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.9) Gecko/2009042113 Ubuntu/9.04 (jaunty) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.9) Gecko/2009042113 Ubuntu/8.10 (intrepid) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.9) Gecko/2009042113 Ubuntu/8.04 (hardy) Firefox/3.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.9) Gecko/2009041500 SUSE/3.0.9-2.2 Firefox/3.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; nl; rv:1.9.0.9) Gecko/2009040821 Firefox/3.0.9 FirePHP/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.0.4) Firefox/3.0.8)",
	"Mozilla/6.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.8) Gecko/2009032609 Firefox/3.0.8 (.NET CLR 3.5.30729)",
	"Mozilla/6.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.8) Gecko/2009032609 Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; zh-TW; rv:1.9.0.8) Gecko/2009032712 Ubuntu/8.04 (hardy) Firefox/3.0.8 GTB5",
	"Mozilla/5.0 (X11; U; Linux x86_64; nb-NO; rv:1.9.0.8) Gecko/2009032600 SUSE/3.0.8-1.2 Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.0.8) Gecko/2009033100 Ubuntu/9.04 (jaunty) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.0.8) Gecko/2009032712 Ubuntu/8.10 (intrepid) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; fi-FI; rv:1.9.0.8) Gecko/2009032712 Ubuntu/8.10 (intrepid) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009040312 Gentoo Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009033100 Ubuntu/9.04 (jaunty) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032908 Gentoo Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032713 Ubuntu/9.04 (jaunty) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032712 Ubuntu/8.10 (intrepid) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032712 Ubuntu/8.04 (hardy) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032712 Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032600 SUSE/3.0.8-1.1.1 Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032600 SUSE/3.0.8-1.1 Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009030810 Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) Gecko Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.8) Gecko/2009032712 Ubuntu/8.10 (intrepid) Firefox/3.0.8 FirePHP/0.2.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.8) Gecko/2009032712 Ubuntu/8.10 (intrepid) Firefox/3.0.8",
	"Mozilla/5.0 (X11; U; Windows NT 5.1; en-US; rv:1.9.0.7) Gecko/2009021910 Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Mac OSX; it; rv:1.9.0.7) Gecko/2009030422 Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; sv-SE; rv:1.9.0.7) Gecko/2009030423 Ubuntu/8.10 (intrepid) Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.7) Gecko/2009030423 Ubuntu/8.10 (intrepid) Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.0.7) Gecko/2009022800 SUSE/3.0.7-1.4 Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009032606 Red Hat/3.0.7-1.el5 Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009032319 Gentoo Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009031802 Gentoo Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009031120 Mandriva/1.9.0.7-0.1mdv2009.0 (2009.0) Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009031120 Mandriva Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009030516 Ubuntu/9.04 (jaunty) Firefox/3.0.7 GTB5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009030516 Ubuntu/9.04 (jaunty) Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009030423 Ubuntu/8.10 (intrepid) Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.7) Gecko/2009030503 Fedora/3.0.7-1.fc9 Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.7) Gecko/2009030620 Gentoo Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; zh-TW; rv:1.9.0.7) Gecko/2009030422 Ubuntu/8.04 (hardy) Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.7) Gecko/2009030503 Fedora/3.0.7-1.fc10 Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; hu-HU; rv:1.9.0.7) Gecko/2009030422 Ubuntu/8.10 (intrepid) Firefox/3.0.7 FirePHP/0.2.4",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.7) Gecko/2009031218 Gentoo Firefox/3.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.7) Gecko/2009030422 Ubuntu/8.10 (intrepid) Firefox/3.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.6pre) Gecko/2008121605 Firefox/3.0.6pre",
	"Mozilla/5.0 (X11; U; Linux; fr; rv:1.9.0.6) Gecko/2009011913 Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.0.6) Gecko/2009020911 Ubuntu/8.10 (intrepid) Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.6) Gecko/2009020519 Ubuntu/9.04 (jaunty) Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.6) Gecko/2009012700 SUSE/3.0.6-1.4 Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.16) Gecko/2009121609 Firefox/3.0.6 (Windows NT 5.1)",
	"Mozilla/5.0 (X11; U; Linux i686; sv-SE; rv:1.9.0.6) Gecko/2009011913 Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.9.0.6) Gecko/2009011912 Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.6) Gecko/2009020911 Ubuntu/8.10 (intrepid) Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; eu; rv:1.9.0.6) Gecko/2009012700 SUSE/3.0.6-0.1.2 Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.6) Gecko/2009020911 Ubuntu/8.10 (intrepid) Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009022714 Ubuntu/9.04 (jaunty) Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009022111 Gentoo Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009020911 Ubuntu/8.04 (hardy) Firefox/3.0.6 FirePHP/0.2.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009020616 Gentoo Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009020518 Ubuntu/9.04 (jaunty) Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009020410 Fedora/3.0.6-1.fc9 Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009012700 SUSE/3.0.6-0.1 Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.19) Gecko/2010091807 Firefox/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.19) Gecko/2010072023 Firefox/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.6) Gecko/2009020911 Ubuntu/8.10 (intrepid) Firefox/3.0.6",
	"Mozilla/5.0 (X11; U; x86_64 Linux; en_US; rv:1.9.0.5) Gecko/2008120121 Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9.0.5) Gecko/2008121623 Ubuntu/8.10 (intrepid) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008122406 Gentoo Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008122120 Gentoo Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008122014 CentOS/3.0.5-1.el4.centos Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008121911 CentOS/3.0.5-1.el5.centos Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008121806 Gentoo Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008121711 Ubuntu/9.04 (jaunty) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.5) Gecko/2008122010 Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; sk; rv:1.9.0.5) Gecko/2008121621 Ubuntu/8.04 (hardy) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.0.5) Gecko/2008121622 Ubuntu/8.10 (intrepid) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.0.5) Gecko/2008120121 Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.5) Gecko/2008121300 SUSE/3.0.5-0.1 Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.9.0.5) Gecko/2008121622 Ubuntu/8.10 (intrepid) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.5) Gecko/2008121711 Ubuntu/9.04 (jaunty) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.9.0.5) Gecko/2008123017 Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; fi-FI; rv:1.9.0.5) Gecko/2008121622 Ubuntu/8.10 (intrepid) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.5) Gecko/2009011301 Gentoo Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.5) Gecko/2008121914 Ubuntu/8.04 (hardy) Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.5) Gecko/2008121718 Gentoo Firefox/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.4pre) Gecko/2008101311 Firefox/3.0.4pre (Swiftfox)",
	"Mozilla/5.0 (X11; U; SunOS i86pc; fr; rv:1.9.0.4) Gecko/2008111710 Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.9.0.4) Gecko/2008111710 Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.0.4) Gecko/2008111217 Fedora/3.0.4-1.fc10 Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-AR; rv:1.9.0.4) Gecko/2008110510 Red Hat/3.0.4-1.el5_2 Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.6) Gecko/2009020407 Firefox/3.0.4 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.4) Gecko/2008120512 Gentoo Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ; rv:1.9.0.4) Gecko/2008111318 Ubuntu/8.04 (hardy) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.9.0.4) Gecko/2008111317 Ubuntu/8.04 (hardy) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; pt-PT; rv:1.9.0.5) Gecko/2008121622 Ubuntu/8.10 (intrepid) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.9.0.4) Gecko/2008111317 Ubuntu/8.04 (hardy) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.9.0.4) Gecko/2008111217 Fedora/3.0.4-1.fc10 Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.4) Gecko/20081031100 SUSE/3.0.4-4.6 Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.0.4) Gecko/2008111317 Ubuntu/8.04 (hardy) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.0.11) Gecko/2009060309 Ubuntu/8.04 (hardy) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.4) Gecko/2008111217 Red Hat Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.9.0.4) Gecko/2008111317 Ubuntu/8.04 (hardy) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.9.0.4) Gecko/2008111317 Linux Mint/5 (Elyssa) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.7) Gecko/2009032018 Firefox/3.0.4 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.5) Gecko/2008121622 Linux Mint/6 (Felicia) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.4) Gecko/2008111318 Ubuntu/8.10 (intrepid) Firefox/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.3pre) Gecko/2008090713 Firefox/3.0.3pre (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.0.3) Gecko/2008092813 Gentoo Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-AR; rv:1.9.0.3) Gecko/2008092515 Ubuntu/8.10 (intrepid) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009030719 Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3 (Linux Mint)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.3) Gecko/2008090713 Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x86; es-ES; rv:1.9.0.3) Gecko/2008092417 Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux x64_64; es-AR; rv:1.9.0.3) Gecko/2008092515 Ubuntu/8.10 (intrepid) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux ia64; en-US; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; zh-TW; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; sv-SE; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.3) Gecko/2008092700 SUSE/3.0.3-2.2 Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; ko-KR; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.3) Gecko/2008092510 Ubuntu/8.04 (hardy) Firefox/3.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.0.2pre) Gecko/2008082305 Firefox/3.0.2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9.0.2) Gecko/2008092213 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.2) Gecko/2008092213 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.2) Gecko/2008092418 CentOS/3.0.2-3.el5.centos Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.2) Gecko/2008092318 Fedora/3.0.2-1.fc9 Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.2) Gecko/2008092213 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.2) Gecko/2008092213 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.2) Gecko/2008092318 Fedora/3.0.2-1.fc9 Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008110715 ASPLinux/3.0.2-3.0.120asp Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092809 Gentoo Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092418 CentOS/3.0.2-3.el5.centos Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092318 Fedora/3.0.2-1.fc9 Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092313 Ubuntu/1.4.0 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008092000 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008091816 Red Hat/3.0.2-3.el5 Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.2) Gecko/2008092313 Ubuntu/8.04 (hardy) Firefox/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.1pre) Gecko/2008062222 Firefox/3.0.1pre (Swiftfox)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9) Gecko/2008052906 Firefox/3.0.1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20090213 Firefox/3.0.1b3pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.19) Gecko/2010051407 CentOS/3.0.19-1.el5.centos Firefox/3.0.19",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.19) Gecko/2010040118 Ubuntu/8.10 (intrepid) Firefox/3.0.19 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-CN; rv:1.9.0.19) Gecko/2010031422 Firefox/3.0.19 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.0.19) Gecko/2010031422 Firefox/3.0.19 (.NET CLR 3.5.30729) FirePHP/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; cs; rv:1.9.0.19) Gecko/2010031422 Firefox/3.0.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.0.19) Gecko/2010031422 Firefox/3.0.19 GTB7.0 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.0.19) Gecko/2010031422 Firefox/3.0.19 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.18) Gecko/2010021501 Ubuntu/9.04 (jaunty) Firefox/3.0.18",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.18) Gecko/2010021501 Ubuntu/9.04 (jaunty) Firefox/3.0.18",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.18) Gecko/2010021501 Firefox/3.0.18",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.18) Gecko/2010020400 SUSE/3.0.18-0.1.1 Firefox/3.0.18",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sv-SE; rv:1.9.0.18) Gecko/2010020220 Firefox/3.0.18 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT; rv:1.9a1) Gecko/20100202 Firefox/3.0.18",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.17) Gecko/2010011010 Mandriva/1.9.0.17-0.1mdv2009.1 (2009.1) Firefox/3.0.17 GTB6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.17) Gecko/2010010604 Ubuntu/9.04 (jaunty) Firefox/3.0.17 FirePHP/0.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.0.10) Gecko/2009122115 Firefox/3.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; cs-CZ; rv:1.9.0.16) Gecko/2009121601 Ubuntu/9.04 (jaunty) Firefox/3.0.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9.0.16) Gecko/2009120208 Firefox/3.0.16 FBSMTWB",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.9.0.16) Gecko/2009120208 Firefox/3.0.16 FBSMTWB",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.3) Gecko/20100401 Firefox/3.0.16 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.16) Gecko/2009120208 Firefox/3.0.16 FBSMTWB",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-LI; rv:1.9.0.16) Gecko/2009120208 Firefox/3.0.16 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.9.0.14) Gecko/2009090217 Ubuntu/9.04 (jaunty) Firefox/3.0.14 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; pt-BR; rv:1.9.0.14) Gecko/2009090217 Ubuntu/9.04 (jaunty) Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.0.14) Gecko/2009090216 Ubuntu/8.04 (hardy) Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.14) Gecko/2009090216 Ubuntu/8.04 (hardy) Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; fi-FI; rv:1.9.0.14) Gecko/2009090217 Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.14) Gecko/2009090217 Ubuntu/9.04 (jaunty) Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.14) Gecko/2009090216 Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.14) Gecko/20090916 Ubuntu/9.04 (jaunty) Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.14) Gecko/2009091010 Firefox/3.0.14 (Debian-3.0.14-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.14) Gecko/2009090905 Fedora/3.0.14-1.fc10 Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.14) Gecko/2009090216 Ubuntu/9.04 (jaunty) Firefox/3.0.14 GTB5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.14) Gecko/2009090216 Ubuntu/9.04 (jaunty) Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.14) Gecko/2009082505 Red Hat/3.0.14-1.el5_4 Firefox/3.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.0.14) Gecko/2009082707 Firefox/3.0.14 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.14) Gecko/2009082707 Firefox/3.0.14 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.9.0.14) Gecko/2009082707 Firefox/3.0.14 GTB6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.9.0.14) Gecko/2009082707 Firefox/3.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.0.14) Gecko/2009082707 Firefox/3.0.14 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ; rv:1.9.0.14) Gecko/2009082707 Firefox/3.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; zh-TW; rv:1.9.0.13) Gecko/2009080315 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.14) Gecko/2009090217 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.13) Gecko/2009080315 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; zh-TW; rv:1.9.0.13) Gecko/2009080315 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.13) Gecko/2009080315 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; fr-be; rv:1.9.0.8) Gecko/2009073022 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; fi-FI; rv:1.9.0.13) Gecko/2009080315 Linux Mint/6 (Felicia) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.13) Gecko/2009080315 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.13) Gecko/2009080316 Ubuntu/8.04 (hardy) Firefox/3.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.13) Gecko/2009080315 Ubuntu/9.04 (jaunty) Firefox/3.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13 (.NET CLR 4.0.20506)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; cs; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ro; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-be; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13 (.NET CLR 3.5.30729) FBSMTWB",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.0.13) Gecko/2009073022 Firefox/3.0.13 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.0.12) Gecko/2009072711 CentOS/3.0.12-1.el5.centos Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.0.12) Gecko/2009070811 Ubuntu/9.04 (jaunty) Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.12) Gecko/2009070818 Ubuntu/8.10 (intrepid) Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.12) Gecko/2009070811 Ubuntu/9.04 (jaunty) Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.12) Gecko/2009070811 Ubuntu/9.04 (jaunty) Firefox/3.0.12 FirePHP/0.3",
	"Mozilla/5.0 (X11; U; Linux ppc; en-GB; rv:1.9.0.12) Gecko/2009070818 Ubuntu/8.10 (intrepid) Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.12) Gecko/2009070818 Ubuntu/8.10 (intrepid) Firefox/3.0.12 FirePHP/0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.12) Gecko/2009070818 Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.12) Gecko/2009070812 Linux Mint/5 (Elyssa) Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.12) Gecko/2009070610 Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.12) Gecko/2009070812 Ubuntu/8.04 (hardy) Firefox/3.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.12) Gecko/2009070811 Ubuntu/9.04 (jaunty) Firefox/3.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12 (.NET CLR 3.5.30729) FirePHP/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sr; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ru; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; nl; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12 GTB5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.0.12) Gecko/2009070611 Firefox/3.0.12 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.11) Gecko/2009060309 Ubuntu/9.04 (jaunty) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.11) Gecko/2009070612 Gentoo Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.11) Gecko/2009061417 Gentoo Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.11) Gecko/2009061118 Fedora/3.0.11-1.fc9 Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.11) Gecko/2009060309 Linux Mint/7 (Gloria) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.11) Gecko/2009060308 Ubuntu/9.04 (jaunty) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.11) Gecko/2009070611 Gentoo Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.0.11) Gecko/2009060308 Ubuntu/9.04 (jaunty) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.11) Gecko/2009061118 Fedora/3.0.11-1.fc10 Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; it-IT; rv:1.9.0.11) Gecko/2009060308 Linux Mint/7 (Gloria) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; fi-FI; rv:1.9.0.11) Gecko/2009060308 Ubuntu/9.04 (jaunty) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.11) Gecko/2009061118 Fedora/3.0.11-1.fc9 Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.11) Gecko/2009060310 Ubuntu/8.10 (intrepid) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.11) Gecko/2009060309 Linux Mint/5 (Elyssa) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.11) Gecko/2009060310 Linux Mint/6 (Felicia) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.11) Gecko/2009060308 Linux Mint/7 (Gloria) Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.11) Gecko/2009060309 Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.11) Gecko/2009060308 Ubuntu/9.04 (jaunty) Firefox/3.0.11 GTB5",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.11) Gecko/2009060214 Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.11) Gecko/2009062218 Gentoo Firefox/3.0.11",
	"Mozilla/5.0 (X11; U; Slackware Linux i686; en-US; rv:1.9.0.10) Gecko/2009042315 Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.10) Gecko/2009042523 Ubuntu/9.04 (jaunty) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; da-DK; rv:1.9.0.10) Gecko/2009042523 Ubuntu/9.04 (jaunty) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; tr-TR; rv:1.9.0.10) Gecko/2009042523 Ubuntu/9.04 (jaunty) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.10) Gecko/2009042513 Ubuntu/8.04 (hardy) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; hu-HU; rv:1.9.0.10) Gecko/2009042718 CentOS/3.0.10-1.el5.centos Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.10) Gecko/2009042708 Fedora/3.0.10-1.fc10 Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.10) Gecko/2009042513 Ubuntu/8.04 (hardy) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.10) Gecko/2009042523 Ubuntu/9.04 (jaunty) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.10) Gecko/2009042513 Linux Mint/5 (Elyssa) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009020410 Fedora/3.0.6-1.fc10 Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko/2009042812 Gentoo Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko/2009042708 Fedora/3.0.10-1.fc10 Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko/2009042523 Ubuntu/8.10 (intrepid) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko/2009042523 Linux Mint/7 (Gloria) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko/2009042523 Linux Mint/6 (Felicia) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko/2009042513 Linux Mint/5 (Elyssa) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.10) Gecko/2009042523 Ubuntu/8.10 (intrepid) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.10) Gecko/2009042513 Ubuntu/8.04 (hardy) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.10) Gecko/2009042523 Ubuntu/9.04 (jaunty) Firefox/3.0.10",
	"Mozilla/5.0 (X11; U; OpenBSD amd64; en-US; rv:1.9.0.1) Gecko/2008081402 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; rv:1.9.0.1) Gecko/2008072820 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9.0.1) Gecko/2008071222 Ubuntu/hardy Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9.0.1) Gecko/2008071222 Ubuntu (hardy) Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9.0.1) Gecko/2008071222 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; ko-KR; rv:1.9.0.1) Gecko/2008071717 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.0.1) Gecko/2008071717 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.1) Gecko/2008071222 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.1) Gecko/2008070400 SUSE/3.0.1-1.1 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.0.1) Gecko/2008072820 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.1) Gecko/2008110312 Gentoo Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.1) Gecko/2008072820 Kubuntu/8.04 (hardy) Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.1) Gecko/2008072820 Firefox/3.0.1 FirePHP/0.1.1.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.1) Gecko/2008070400 SUSE/3.0.1-0.1 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64) Gecko/2008072820 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.9) Gecko/20080810020329 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.0.1) Gecko/2008071719 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.0.1) Gecko/2008070208 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.1) Gecko/2008071719 Firefox/3.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.1) Gecko/2008071222 Firefox/3.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.8) Gecko/2009032609 Firefox/3.0.0 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.1) Gecko/2008070208 Firefox/3.0.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.0.1) Gecko/2008070208 Firefox/3.0.0",
	"Mozilla/6.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:2.0.0.0) Gecko/20061028 Firefox/3.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; it-IT; ) Gecko/20080000 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.9) Gecko/2008060309 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9) Gecko/2008061017 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9) Gecko/2008061017 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-AR; rv:1.9) Gecko/2008061017 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-AR; rv:1.9) Gecko/2008061015 Ubuntu/8.04 (hardy) Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0) Gecko/2008061600 SUSE/3.0-1.2 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9) Gecko/2008062908 Firefox/3.0 (Debian-3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9) Gecko/2008062315 (Gentoo) Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9) Gecko/2008061317 (Gentoo) Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9) Gecko/2008061017 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; tr-TR; rv:1.9.0) Gecko/2008061600 SUSE/3.0-1.2 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; sk; rv:1.9.1) Gecko/20090630 Fedora/3.5-1.fc11 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; sk; rv:1.9) Gecko/2008061015 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.9) Gecko/2008080808 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9) Gecko/2008061812 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.5) Gecko/2008121622 Slackware/2.6.27-PiP Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9) Gecko/2008061015 Firefox/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9) Gecko/2008061015 Firefox/3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.0.15) Gecko/2009101601 Firefox 2.1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1b1) Gecko/20061110 Firefox/2.0b3",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1) Gecko/20060918 Firefox/2.0b2",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1) Gecko/20060916 Firefox/2.0b2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080208 Firefox/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1b2) Gecko/20060821 Firefox/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.8.1.1) Gecko/20061204 Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1) Gecko/20060918 Firefox/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1b2) Gecko/20060821 Firefox/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.8.1b2) Gecko/20060821 Firefox/2.0b2",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.8.1b2) Gecko/20060901 Firefox/2.0b2",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (X11; U; Linux i686; en_US; rv:1.8.1b1) Gecko/20060813 Firefox/2.0b1",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1b1) Gecko/20060707 Firefox/2.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ca; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1b1) Gecko/20060707 Firefox/2.0b1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1b1) Gecko/20060710 Firefox/2.0b1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1) Gecko/20061001 Firefox/2.0b (Swiftfox)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8) Gecko/20060321 Firefox/2.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8) Gecko/20060319 Firefox/2.0a1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8) Gecko/20060322 Firefox/2.0a1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8) Gecko/20060320 Firefox/2.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.9.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.4) Gecko/20070515 Firefox/2.0.4",
	"Mozilla/5.0 (X11; U; SunOS sun4v; es-ES; rv:1.8.1.9) Gecko/20071127 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.9) Gecko/20071102 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; nl-NL; rv:1.8.1.9) Gecko/20071105 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071105 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071105 Fedora/2.0.0.9-1.fc7 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071103 Firefox/2.0.0.9 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071103 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071025 FreeBSD/i386 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.9) Gecko/20071105 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-GB; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; Windows NT 5.1; en-US; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; tr; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; it; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; da; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sl; rv:1.8.1.9) Gecko/20071025 Firefox/2.0.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17pre) Gecko/20080715 Firefox/2.0.0.8pre",
	"Mozilla/5.0 (X11; U; x86_64 Linux; en_US; rv:1.8.16) Gecko/20071015 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Windows NT i686; fr; rv:1.9.0.1) Gecko/2008070206 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.8) Gecko/20071015 SUSE/2.0.0.8-1.1 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.12) Gecko/20080129 Firefox/2.0.0.8 (Debian-2.0.0.12-1)",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; hu; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.1) Gecko/2008070206 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.8) Gecko/20071030 Fedora/2.0.0.8-2.fc8 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071201 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071022 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071019 Fedora/2.0.0.8-1.fc7 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071008 FreeBSD/i386 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071004 Firefox/2.0.0.8 (Debian-2.0.0.8-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20061201 Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.8) Gecko/20071008 Ubuntu/7.10 (gutsy) Firefox/2.0.0.8",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.1.7) Gecko/20070930 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl; rv:1.8.1.7) Gecko/20071009 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.7) Gecko/20070918 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.6) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.7) Gecko/20070923 Firefox/2.0.0.7 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.7) Gecko/20070921 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.6) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux i386; en-US; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux Gentoo; pl-PL; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Linux amd64; en-US; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; Gentoo Linux x86_64; pl-PL; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; it-IT; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en_US; rv:1.8.1.6) Gecko/20070725 Firefox/2.0.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; nl; rv:1.8.1.7) Gecko/20070914 Firefox/2.0.0.7",
	"Mozilla/5.0 (X11; U; SunOS sun4u; pl-PL; rv:1.8.1.6) Gecko/20071217 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; SunOS sun4u; de-DE; rv:1.8.1.6) Gecko/20070805 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-ZW; rv:1.8.1.6) Gecko/20071125 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; OpenBSD sparc64; en-US; rv:1.8.1.6) Gecko/20070816 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; OpenBSD sparc64; en-AU; rv:1.8.1.6) Gecko/20071225 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.1.6) Gecko/20070819 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.1.4) Gecko/20070704 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; OpenBSD i386; de-DE; rv:1.8.1.6) Gecko/20080429 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; OpenBSD amd64; en-US; rv:1.8.1.6) Gecko/20070817 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; NetBSD sparc64; fr-FR; rv:1.8.1.6) Gecko/20070822 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; NetBSD alpha; en-US; rv:1.8.1.6) Gecko/20080115 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.6) Gecko/20061201 Firefox/2.0.0.6 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de-DE; rv:1.8.1.6) Gecko/20070802 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; Linux x86; en-US; rv:1.8.1.6) Gecko/20061201 Firefox/2.0.0.6 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1.6) Gecko/20070725 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.8.1.6) Gecko/20061201 Firefox/2.0.0.6 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.6) Gecko/20070803 Firefox/2.0.0.6 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.6) Gecko/20070831 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.6) Gecko/20070807 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.6) Gecko/20070804 Firefox/2.0.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.5) Gecko/20061201 Firefox/2.0.0.5 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; Ubuntu 7.04; de-CH; rv:1.8.1.5) Gecko/20070309 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.5) Gecko/20070718 Fedora/2.0.0.5-1.fc7 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.3) Gecko/2008100320 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.5) Gecko/20070728 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.5) Gecko/20070725 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.5) Gecko/20070719 Firefox/2.0.0.5 (Debian-2.0.0.5-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.5) Gecko/20070718 Fedora/2.0.0.5-1.fc7 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.5) Gecko/20061201 Firefox/2.0.0.5 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.5) Gecko/20060911 SUSE/2.0.0.5-1.2 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.5) Gecko/20070718 Fedora/2.0.0.5-1.fc7 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-GB; rv:1.8.1.5) Gecko/20070718 Fedora/2.0.0.5-1.fc7 Firefox/2.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-TW; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4pre) Gecko/20070509 Firefox/2.0.0.4pre (Swiftfox)",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.4) Gecko/20070622 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.4) Gecko/20070531 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.8.1.4) Gecko/20070622 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.1.4) Gecko/20070704 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl; rv:1.8.1.4) Gecko/20070611 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.4) Gecko/20070627 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.4) Gecko/20070604 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.4) Gecko/20070529 SUSE/2.0.0.4-6.1 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.4) Gecko/20070515 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.4) Gecko/20061201 Firefox/2.0.0.4 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.4) Gecko/20061201 Firefox/2.0.0.4 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.4) Gecko/20070621 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.4) Gecko/20060601 Firefox/2.0.0.4 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.4) Gecko/20070515 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.4) Gecko/20061201 Firefox/2.0.0.4 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070602 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070531 Firefox/2.0.0.4 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070531 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070530 Fedora/2.0.0.4-1.fc7 Firefox/2.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070515 Firefox/2.0.0.4 (Kubuntu)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.3pre) Gecko/20070307 Firefox/2.0.0.3pre (Swiftfox)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.1.5) Gecko/20070713 Firefox/2.0.0.3C",
	"Mozilla/5.0 (X11; U; SunOS sun4v; en-US; rv:1.8.1.3) Gecko/20070321 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.3) Gecko/20070321 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.8.1.3) Gecko/20070423 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.1.3) Gecko/20070505 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.8.1.3) Gecko/20070322 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008122010 Firefox/2.0.0.3 (Debian-3.0.5-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.3) Gecko/20070415 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.3) Gecko/20070324 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.3) Gecko/20070322 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.3) Gecko/20061201 Firefox/2.0.0.3 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.1.3) Gecko/20070310 Firefox/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; zh-TW; rv:1.8.1.3) Gecko/20070309 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.3) Gecko/20061201 Firefox/2.0.0.3 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.8.1.3) Gecko/20060601 Firefox/2.0.0.3 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; nb-NO; rv:1.8.1.3) Gecko/20070310 Firefox/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.8.1.3) Gecko/20070309 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.3) Gecko/20070410 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.3) Gecko/20070406 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.3) Gecko/20070310 Firefox/2.0.0.3 (Debian-2.0.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.3) Gecko/20070309 Firefox/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.8.1.2pre) Gecko/20061023 SUSE/2.0.0.1-0.1 Firefox/2.0.0.2pre",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.2pre) Gecko/20061023 SUSE/2.0.0.1-0.1 Firefox/2.0.0.2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2pre) Gecko/20070118 Firefox/2.0.0.2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.22pre) Gecko/20090327 Ubuntu/8.04 (hardy) Firefox/2.0.0.22pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.22pre) Gecko/20090327 Ubuntu/7.10 (gutsy) Firefox/2.0.0.22pre",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.22pre) Gecko/20090327 Ubuntu/7.10 (gutsy) Firefox/2.0.0.22pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.21",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.20) Gecko/20090108 Firefox/2.0.0.20",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.20) Gecko/20081217 Firefox(2.0.0.20)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.20) Gecko/20090206 Firefox/2.0.0.20",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.1.20) Gecko/20090413 Firefox/2.0.0.20",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.1.20) Gecko/20090225 Firefox/2.0.0.20",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20 GTB5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-TW; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ru; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ko; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; cs; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-GB; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.2) Gecko/20070226 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux; en-US; rv:1.8.1.2) Gecko/20070219 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.8.1.2) Gecko/20060601 Firefox/2.0.0.2 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; sv-SE; rv:1.8.1.2) Gecko/20061023 SUSE/2.0.0.2-1.1 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1.2) Gecko/20070220 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.2) Gecko/20060601 Firefox/2.0.0.2 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; hu; rv:1.8.1.2) Gecko/20070220 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.2) Gecko/20060601 Firefox/2.0.0.2 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.2) Gecko/20070225 Firefox/2.0.0.2 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.2) Gecko/20070220 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.2) Gecko/20060601 Firefox/2.0.0.2 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.2) Gecko/20070220 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070317 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070314 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070226 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070225 Firefox/2.0.0.2 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070221 SUSE/2.0.0.2-6.1 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070220 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20061201 Firefox/2.0.0.2 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20061201 Firefox/2.0.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.19) Gecko/20081213 SUSE/2.0.0.19-0.1 Firefox/2.0.0.19",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.19) Gecko/20081216 Ubuntu/7.10 (gutsy) Firefox/2.0.0.19",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081230 Firefox/2.0.0.19",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081216 Fedora/2.0.0.19-1.fc8 Firefox/2.0.0.19 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081213 SUSE/2.0.0.19-0.1 Firefox/2.0.0.19",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.19) Gecko/20081213 SUSE/2.0.0.19-0.1 Firefox/2.0.0.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-CN; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.19",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.8.1.19) Gecko/20081201 Firefox/2.0.0.19",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.18) Gecko/20081113 Ubuntu/8.04 (hardy) Firefox/2.0.0.18",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.18) Gecko/20081112 Fedora/2.0.0.18-1.fc8 Firefox/2.0.0.18",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.18) Gecko/20081113 Ubuntu/8.04 (hardy) Firefox/2.0.0.18",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.18) Gecko/20081112 Fedora/2.0.0.18-1.fc8 Firefox/2.0.0.18",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.18) Gecko/20080921 SUSE/2.0.0.18-0.1 Firefox/2.0.0.18",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.8.1.18) Gecko/20081029 Firefox/2.0.0.18",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.8.1.18) Gecko/20081029 Firefox/2.0.0.18",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.1.18) Gecko/20081029 Firefox/2.0.0.18",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.18) Gecko/20081029 Firefox/2.0.0.18",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs; rv:1.8.1.18) Gecko/20081029 Firefox/2.0.0.18",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en-US; rv:1.9.0.4) Gecko/20081029 Firefox/2.0.0.18",
	"Mozilla/5.0 (X11; U; Linux sparc64; en-US; rv:1.8.1.17) Gecko/20081108 Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.17) Gecko/20080924 Ubuntu/8.04 (hardy) Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.17) Gecko/20080922 Ubuntu/7.10 (gutsy) Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.17) Gecko/20080921 SUSE/2.0.0.17-1.2 Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.17) Gecko/20080703 Mandriva/2.0.0.17-1.1mdv2008.1 (2008.1) Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pl; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.14) Gecko/20080404 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv-SE; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.3) Gecko/2008092417 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de; rv:1.8.1.17) Gecko/20080829 Firefox/2.0.0.17",
	"Mozilla/5.0 (U; Windows NT 5.1; en-GB; rv:1.8.1.17) Gecko/20080808 Firefox/2.0.0.17",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.1.16) Gecko/20080812 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.8.1.16) Gecko/20080715 Fedora/2.0.0.16-1.fc8 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.16) Gecko/20080719 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.16) Gecko/20080718 Ubuntu/8.04 (hardy) Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080722 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080718 Ubuntu/8.04 (hardy) Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080715 Ubuntu/7.10 (gutsy) Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080715 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080715 Fedora/2.0.0.16-1.fc8 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.16) Gecko/20080715 Ubuntu/7.10 (gutsy) Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.16) Gecko/20080718 Ubuntu/8.04 (hardy) Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); fr; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.16) Gecko/20080716 Firefox/2.0.0.16",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-ES; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.8.1.16) Gecko/20080702 Firefox/2.0.0.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.15) Gecko/20080702 Ubuntu/8.04 (hardy) Firefox/2.0.0.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.15) Gecko/20080702 Ubuntu/8.04 (hardy) Firefox/2.0.0.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.15) Gecko/20061201 Firefox/2.0.0.15 (Ubuntu-feisty)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sv-SE; rv:1.8.1.15) Gecko/20080623 Firefox/2.0.0.15",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1.2) Gecko/20090729 Firefox/2.0.0.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; sk; rv:1.8.1.15) Gecko/20080623 Firefox/2.0.0.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.8.1.15) Gecko/20080623 Firefox/2.0.0.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.15) Gecko/20080623 Firefox/2.0.0.15",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; de; rv:1.8.1.15) Gecko/20080623 Firefox/2.0.0.15",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.14) Gecko/20080418 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; hu; rv:1.8.1.14) Gecko/20080416 Fedora/2.0.0.14-1.fc7 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.6) Gecko/2010012717 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux ppc64; en-US; rv:1.8.1.14) Gecko/20080418 Ubuntu/7.10 (gutsy) Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.14) Gecko/20080420 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.14) Gecko/20080416 Fedora/2.0.0.14-1.fc7 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.14) Gecko/20080419 Ubuntu/8.04 (hardy) Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.14) Gecko/20080404 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080525 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080508 Ubuntu/8.04 (hardy) Firefox/2.0.0.14 (Linux Mint)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080428 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080423 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080417 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080416 Fedora/2.0.0.14-1.fc8 Firefox/2.0.0.14 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080410 SUSE/2.0.0.14-0.4 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080404 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20061201 Firefox/2.0.0.14 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.14) Gecko/20080418 Ubuntu/7.10 (gutsy) Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.14) Gecko/20080410 SUSE/2.0.0.14-0.1 Firefox/2.0.0.14",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.14) Gecko/20080417 Firefox/2.0.0.14",
	"User-Agent:Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.8.1.13) Gecko/20080325 Ubuntu/7.10 (gutsy) Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.13) Gecko/20080208 Mandriva/2.0.0.13-1mdv2008.1 (2008.1) Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080330 Ubuntu/7.10 (gutsy) Firefox/2.0.0.13 (Linux Mint)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080325 Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080316 SUSE/2.0.0.13-1.1 Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080316 SUSE/2.0.0.13-0.1 Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20061201 Firefox/2.0.0.13 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.13) Gecko/20080325 Ubuntu/7.10 (gutsy) Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; bg; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.13",
	"Mozilla/5.0 (X11; U; Linux i686 Gentoo; en-US; rv:1.8.1.13) Gecko/20080413 Firefox/2.0.0.13 (Gentoo Linux)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-ES; rv:1.8.1.14) Gecko/20080404 Firefox/2.0.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-GB; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.13 (.NET CLR 3.0.04506.30)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.14) Gecko/20080404 Firefox/2.0.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-AR; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.1) Gecko/2008070208 Firefox/2.0.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.11) Gecko/20071127 Firefox/2.0.0.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.12pre) Gecko/20080122 Firefox/2.0.0.12pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ru; rv:1.8.1.12) Gecko/20080201 Firefox/2.0.0.12; MEGAUPLOAD 2.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.12) Gecko/20080210 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.1) Gecko/2008072610 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.12) Gecko/20080214 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.12) Gecko/20080203 SUSE/2.0.0.12-0.1 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.8.1.12) Gecko/20080207 Ubuntu/7.10 (gutsy) Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.8.1.12) Gecko/20080203 SUSE/2.0.0.12-0.1 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.8.1.12) Gecko/20080208 Fedora/2.0.0.12-1.fc8 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.8.1.12) Gecko/20080203 SUSE/2.0.0.12-6.1 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux x86; sv-SE; rv:1.8.1.12) Gecko/20080207 Ubuntu/8.04 (hardy) Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.12) Gecko/20080208 Fedora/2.0.0.12-1.fc8 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.8.1.6) Gecko/20080208 Ubuntu/7.10 (gutsy) Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.12) Gecko/20080213 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.12) Gecko/20080207 Ubuntu/7.10 (gutsy) Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080419 Ubuntu/8.04 (hardy) Firefox/2.0.0.12 MEGAUPLOAD 1.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080208 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080208 Fedora/2.0.0.12-1.fc8 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080201 Firefox/2.0.0.12 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080129 Firefox/2.0.0.12 (Debian-2.0.0.12-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.12) Gecko/20080203 SUSE/2.0.0.12-2.1 Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.12) Gecko/20080207 Ubuntu/7.10 (gutsy) Firefox/2.0.0.12",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1.11) Gecko/20080118 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.1.4) Gecko/20071127 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; zh-TW; rv:1.8.1.11) Gecko/20071204 Ubuntu/7.10 (gutsy) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.8.1.11) Gecko/20071127 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.11) Gecko/20071201 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.11) Gecko/20071127 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.11) Gecko/20070914 Mandriva/2.0.0.11-1.1mdv2008.0 (2008.0) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; ru-RU; rv:1.8.1.11) Gecko/20071201 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; pt-PT; rv:1.8.1.11) Gecko/20071204 Ubuntu/7.10 (gutsy) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.8.1.11) Gecko/20071128 Firefox/2.0.0.11 (Debian-2.0.0.11-1)",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.8.1.11) Gecko/20071127 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; ja-JP; rv:1.8.1.11) Gecko/20071204 Ubuntu/7.10 (gutsy) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.8) Gecko/20071022 Ubuntu/7.10 (gutsy) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.6) Gecko/20071008 Ubuntu/7.10 (gutsy) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.11) Gecko/20071204 Ubuntu/7.10 (gutsy) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.8.1.11) Gecko/20071216 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20080201 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20071217 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20071204 Ubuntu/7.10 (gutsy) Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20071204 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.10) Gecko/20061201 Firefox/2.0.0.10 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.10) Gecko/20071213 Fedora/2.0.0.10-3.fc8 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.10) Gecko/20071128 Fedora/2.0.0.10-2.fc7 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.10) Gecko/20071126 Ubuntu/7.10 (gutsy) Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.17) Gecko/20080827 Firefox/2.0.0.10 (Debian-2.0.0.17-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20071213 Fedora/2.0.0.10-3.fc8 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20071203 Ubuntu/7.10 (gutsy) Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20071128 Fedora/2.0.0.10-2.fc7 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20071126 Ubuntu/7.10 (gutsy) Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20071115 Firefox/2.0.0.10 (Debian-2.0.0.10-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20071115 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20071015 SUSE/2.0.0.10-0.2 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20061201 Firefox/2.0.0.10 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.10) Gecko/20060601 Firefox/2.0.0.10 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.10) Gecko/20071126 Ubuntu/7.10 (gutsy) Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.10) Gecko/20071126 Ubuntu/7.10 (gutsy) Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.10) Gecko/20071115 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.10) Gecko/20071015 SUSE/2.0.0.10-0.2 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.10) Gecko/20071015 SUSE/2.0.0.10-0.1 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.1.4) Gecko/20070515 Firefox/2.0.0.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.8.1.1) Gecko/20060601 Firefox/2.0.0.1 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fi-FI; rv:1.8.1.1) Gecko/20060601 Firefox/2.0.0.1 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.1) Gecko/20060601 Firefox/2.0.0.1 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.8.1.1) Gecko/20061208 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1.1) Gecko/20061204 Firefox/2.0.0.1 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.8.1.1) Gecko/20070311 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; hu; rv:1.8.1.1) Gecko/20061208 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.1) Gecko/20060601 Firefox/2.0.0.1 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.3) Gecko/20061201 Firefox/2.0.0.1 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20070224 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20070110 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20061220 Firefox/2.0.0.1 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20061208 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20061205 Firefox/2.0.0.1 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20061205 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20060601 Firefox/2.0.0.1 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.2pre) Gecko/20061023 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.1) Gecko/20061208 Firefox/2.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.1) Gecko/20061220 Firefox/2.0.0.1 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.1) Gecko/20061205 Firefox/2.0.0.1 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (X11; U; SunOS sun4u; de-DE; rv:1.9.1b4) Gecko/20090428 Firefox/2.0.0.0",
	"Mozilla/5.0 (X11; Linux x86_64; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (X11; Linux i686; U; pl; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.4) Gecko/20070509 Firefox/2.0.0",
	"Mozilla/5.0 (Windows NT 6.1; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (Windows NT 6.0; U; tr; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (Windows NT 6.0; U; sv; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (Windows NT 6.0; U; hu; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (Linux i686; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0",
	"Mozilla/5.0 (X11;U;Linux i686;en-US;rv:1.8.1) Gecko/2006101022 Firefox/2.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1) Gecko/20061228 Firefox/2.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.1) Gecko/20061024 Firefox/2.0",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.8.1) Gecko/20061211 Firefox/2.0",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.8.1) Gecko/20061024 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1) Gecko/20061202 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1) Gecko/20061128 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1) Gecko/20061122 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1) Gecko/20061023 SUSE/2.0-37 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1) Gecko/20060601 Firefox/2.0 (Ubuntu-edgy)",
	"Mozilla/5.0 (X11; U; Linux x86-64; en-US; rv:1.8.1) Gecko/20061010 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; zh-TW; rv:1.8.1) Gecko/20061010 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; tr-TR; rv:1.8.1) Gecko/20061023 SUSE/2.0-30 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1) Gecko/20061127 Firefox/2.0 (Gentoo Linux)",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1) Gecko/20061127 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1) Gecko/20061024 Firefox/2.0 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1) Gecko/20061010 Firefox/2.0 Ubuntu",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1) Gecko/20061010 Firefox/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.1) Gecko/20061003 Firefox/2.0 Ubuntu",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1) Gecko/20061010 Firefox/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ; rv:1.8.0.7) Gecko/20060917 Firefox/1.9.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ; rv:1.8.0.10) Gecko/20070216 Firefox/1.9.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ; rv:1.8.0.1) Gecko/20060111 Firefox/1.9.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9a1) Gecko/20060112 Firefox/1.6a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20060217 Firefox/1.6a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20060117 Firefox/1.6a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20051215 Firefox/1.6a1 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9a1) Gecko/20060127 Firefox/1.6a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2 x64; en-US; rv:1.9a1) Gecko/20060214 Firefox/1.6a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060323 Firefox/1.6a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060121 Firefox/1.6a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20051220 Firefox/1.6a1",
	"Mozilla/5.0 (Windows NT 5.1; rv:1.9a1) Gecko/20060217 Firefox/1.6a1",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.9a1) Gecko/20051002 Firefox/1.6a1",
	"Mozilla/5.0 (X11; U; OpenBSD amd64; en-US; rv:1.8.0.9) Gecko/20070101 Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.9) Gecko/20070126 Ubuntu/dapper-security Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071025 Firefox/1.5.0.9 (Debian-2.0.0.9-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20070316 CentOS/1.5.0.9-10.el5.centos Firefox/1.5.0.9 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20070126 Ubuntu/dapper-security Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20070102 Ubuntu/dapper-security Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20061221 Fedora/1.5.0.9-1.fc5 Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20061219 Fedora/1.5.0.9-1.fc6 Firefox/1.5.0.9 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20061215 Red Hat/1.5.0.9-0.1.el4 Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20060911 SUSE/1.5.0.9-3.2 Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20060911 SUSE/1.5.0.9-0.2 Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.9) Gecko/20061219 Fedora/1.5.0.9-1.fc6 Firefox/1.5.0.9 pango-text",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.8.0.9) Gecko/20061206 Firefox/1.5.0.9",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.0.8) Gecko/20061110 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; sv-SE; rv:1.8.0.8) Gecko/20061108 Fedora/1.5.0.8-1.fc5 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.0.8) Gecko/20061213 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8) Gecko/20061115 Ubuntu/dapper-security Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8) Gecko/20061110 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8) Gecko/20061107 Fedora/1.5.0.8-1.fc6 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8) Gecko/20060911 SUSE/1.5.0.8-0.2 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8) Gecko/20060802 Mandriva/1.5.0.8-1.1mdv2007.0 (2007.0) Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.8) Gecko/20061115 Ubuntu/dapper-security Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.8) Gecko/20060911 SUSE/1.5.0.8-0.2 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; Linux Gentoo i686; pl; rv:1.8.0.8) Gecko/20061219 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.0.8) Gecko/20061210 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; FreeBSD amd64; en-US; rv:1.8.0.8) Gecko/20061116 Firefox/1.5.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.0.7) Gecko/20060915 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.0.7) Gecko/20061017 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.0.7) Gecko/20060920 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; NetBSD amd64; fr-FR; rv:1.8.0.7) Gecko/20061102 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.7) Gecko/20060924 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.7) Gecko/20060921 Ubuntu/dapper-security Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.7) Gecko/20060919 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.7) Gecko/20060911 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; sk; rv:1.8.0.7) Gecko/20060909 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.8.0.7) Gecko/20060921 Ubuntu/dapper-security Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.7) Gecko/20060914 Firefox/1.5.0.7 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.0.7) Gecko/20060914 Firefox/1.5.0.7 (Swiftfox) Mnenhy/0.7.4.666",
	"Mozilla/5.0 (X11; U; Linux i686; ko-KR; rv:1.8.0.7) Gecko/20060913 Fedora/1.5.0.7-1.fc5 Firefox/1.5.0.7 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; hu; rv:1.8.0.7) Gecko/20060911 SUSE/1.5.0.7-0.1 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.0.7) Gecko/20060921 Ubuntu/dapper-security Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.0.7) Gecko/20060909 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.0.7) Gecko/20060830 Firefox/1.5.0.7 (Debian-1.5.dfsg+1.5.0.7-1",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.0.7) Gecko/20060909 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-ZW; rv:1.8.0.7) Gecko/20061018 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.7) Gecko/20061014 Firefox/1.5.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060905 Fedora/1.5.0.6-10 Firefox/1.5.0.6 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060808 Fedora/1.5.0.6-2.fc5 Firefox/1.5.0.6 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060807 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060803 Firefox/1.5.0.6 (Swiftfox)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060802 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060728 SUSE/1.5.0.6-0.1 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6 (Debian-1.5.dfsg+1.5.0.6-4)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6 (Debian-1.5.dfsg+1.5.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.0.6) Gecko/20060808 Fedora/1.5.0.6-2.fc5 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.6) Gecko/20060808 Fedora/1.5.0.6-2.fc5 Firefox/1.5.0.6 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); zh-TW; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); nl; rv:1.8.0.6) Gecko/20060728 SUSE/1.5.0.6-1.2 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.6) Gecko/20060728 SUSE/1.5.0.6-1.2 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); de; rv:1.8.0.6) Gecko/20060728 SUSE/1.5.0.6-1.3 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); de; rv:1.8.0.6) Gecko/20060728 Firefox/1.5.0.6",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.8.0.5) Gecko/20060728 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.0.5) Gecko/20060819 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; NetBSD i386; en-US; rv:1.8.0.5) Gecko/20060818 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.5) Gecko/20060911 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; sv-SE; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Firefox/1.5.0.5 Mnenhy/0.7.4.666",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060831 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060820 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060813 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060812 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060806 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060803 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060801 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060719 Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.5) Gecko/20060731 Ubuntu/dapper-security Firefox/1.5.0.5",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.5) Gecko/20060726 Red Hat/1.5.0.5-0.el4.1 Firefox/1.5.0.5 pango-text",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.0.4) Gecko/20060628 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.8.0.4) Gecko/20060508 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.4) Gecko/20060614 Fedora/1.5.0.4-1.2.fc5 Firefox/1.5.0.4 pango-text Mnenhy/0.7.4.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.4) Gecko/20060527 SUSE/1.5.0.4-1.7 Firefox/1.5.0.4 Mnenhy/0.7.4.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060716 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060711 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060704 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060629 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060614 Fedora/1.5.0.4-1.2.fc5 Firefox/1.5.0.4 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060613 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060608 Ubuntu/dapper-security Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060527 SUSE/1.5.0.4-1.3 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060508 Firefox/1.5.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060406 Firefox/1.5.0.4 (Debian-1.5.dfsg+1.5.0.4-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.3) Gecko/20060523 Ubuntu/dapper Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.3) Gecko/20060522 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.8.0.3) Gecko/20060523 Ubuntu/dapper Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.3) Gecko/20060523 Ubuntu/dapper Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.3) Gecko/20060504 Fedora/1.5.0.3-1.1.fc5 Firefox/1.5.0.3 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.3) Gecko/20060425 SUSE/1.5.0.3-7 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.3) Gecko/20060326 Firefox/1.5.0.3 (Debian-1.5.dfsg+1.5.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.3) Gecko/20060425 SUSE/1.5.0.3-7 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); ru; rv:1.8.0.3) Gecko/20060425 SUSE/1.5.0.3-7 Firefox/1.5.0.3",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.4) Gecko/20060508 Firefox/1.5.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; es-ES; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; es-ES; rv:1.8.0.3) Gecko/20060426 Firefox/1.5.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 4.0; en-US; rv:1.8.0.2) Gecko/20060418 Firefox/1.5.0.2;",
	"Mozilla/5.0 (X11; U; OpenBSD sparc64; pl-PL; rv:1.8.0.2) Gecko/20060429 Firefox/1.5.0.2",
	"Mozilla/5.0 (X11; U; OpenBSD sparc64; en-CA; rv:1.8.0.2) Gecko/20060429 Firefox/1.5.0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; de-AT; rv:1.8.0.2) Gecko/20060422 Firefox/1.5.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.2) Gecko/20060419 Fedora/1.5.0.2-1.2.fc5 Firefox/1.5.0.2 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.2) Gecko Firefox/1.5.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050921 Firefox/1.5.0.2 Mandriva/1.0.6-15mdk (2006.0)",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.0.2) Gecko/20060414 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv-SE; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.2) Gecko/20060419 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.2) Gecko/20060406 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.2) Gecko/20060309 Firefox/1.5.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.2) Gecko/20060308 Firefox/1.5.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; sv-SE; rv:1.8.0.13pre) Gecko/20071126 Ubuntu/dapper-security Firefox/1.5.0.13pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.13pre) Gecko/20080207 Ubuntu/dapper-security Firefox/1.5.0.13pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.12) Gecko/20080419 CentOS/1.5.0.12-0.15.el4.centos Firefox/1.5.0.12 pango-text",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.12) Gecko/20070718 Red Hat/1.5.0.12-3.el5 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.12) Gecko/20070530 Fedora/1.5.0.12-1.fc6 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.8.0.12) Gecko/20070601 Ubuntu/dapper-security Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.12) Gecko/20071126 Fedora/1.5.0.12-7.fc6 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.12) Gecko/20070719 CentOS/1.5.0.12-0.3.el4.centos Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.12) Gecko/20070530 Fedora/1.5.0.12-1.fc6 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.12) Gecko/20070529 Red Hat/1.5.0.12-0.1.el4 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.0.12) Gecko/20070718 Fedora/1.5.0.12-4.fc6 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.12) Gecko/20070731 Ubuntu/dapper-security Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.12) Gecko/20070719 CentOS/1.5.0.12-3.el5.centos Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.12) Gecko/20080326 CentOS/1.5.0.12-14.el5.centos Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.12) Gecko/20070731 Ubuntu/dapper-security Firefox/1.5.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.12 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv-SE; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ko; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.0.11) Gecko/20070327 Ubuntu/dapper-security Firefox/1.5.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.0.11) Gecko/20070327 Ubuntu/dapper-security Firefox/1.5.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; cs-CZ; rv:1.8.0.11) Gecko/20070327 Ubuntu/dapper-security Firefox/1.5.0.11",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hu; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fi; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.12) Gecko/20070508 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; pl; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; it; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; es-ES; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de; rv:1.8.0.11) Gecko/20070312 Firefox/1.5.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.0.10pre) Gecko/20070207 Firefox/1.5.0.10pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.10pre) Gecko/20070211 Firefox/1.5.0.10pre",
	"Mozilla/5.0 (X11; U; OpenBSD ppc; en-US; rv:1.8.0.10) Gecko/20070223 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.10) Gecko/20070409 CentOS/1.5.0.10-2.el5.centos Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; zh-TW; rv:1.8.0.10) Gecko/20070508 Fedora/1.5.0.10-1.fc5 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.8.0.10) Gecko/20070510 Fedora/1.5.0.10-6.fc6 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.0.10) Gecko/20070223 Fedora/1.5.0.10-1.fc5 Firefox/1.5.0.10 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070510 Fedora/1.5.0.10-6.fc6 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070409 CentOS/1.5.0.10-2.el5.centos Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070302 Ubuntu/dapper-security Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070226 Red Hat/1.5.0.10-0.1.el4 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070226 Fedora/1.5.0.10-1.fc6 Firefox/1.5.0.10 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070223 CentOS/1.5.0.10-0.1.el4.centos Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070221 Red Hat/1.5.0.10-0.1.el4 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070216 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20060911 SUSE/1.5.0.10-0.2 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-CA; rv:1.8.0.10) Gecko/20070223 Fedora/1.5.0.10-1.fc5 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; cs-CZ; rv:1.8.0.10) Gecko/20070313 Fedora/1.5.0.10-5.fc6 Firefox/1.5.0.10",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.0.10) Gecko/20060911 SUSE/1.5.0.10-0.2 Firefox/1.5.0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv-SE; rv:1.8.0.10) Gecko/20070216 Firefox/1.5.0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.8.0.10) Gecko/20070216 Firefox/1.5.0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.0.10) Gecko/20070216 Firefox/1.5.0.10",
	"Mozilla/5.0 (ZX-81; U; CP/M86; en-US; rv:1.8.0.1) Gecko/20060111 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8.0.1) Gecko/20060206 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-GB; rv:1.8.0.1) Gecko/20060206 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.8.0.1) Gecko/20060213 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; pl-PL; rv:1.8) Gecko/20051128 SUSE/1.5-0.1 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.1) Gecko/20060313 Fedora/1.5.0.1-9 Firefox/1.5.0.1 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.8.0.1) Gecko/20060124 Firefox/1.5.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8b4) Gecko/20050908 Firefox/1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8b4) Gecko/20050908 Firefox/1.4",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8b4) Gecko/20050908 Firefox/1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090403 Firefox/1.1.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.13) Gecko/20060413 Red Hat/1.0.8-1.4.1 Firefox/1.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.13) Gecko/20060411 Firefox/1.0.8 SUSE/1.0.8-0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20060410 Firefox/1.0.8 Mandriva/1.0.6-16.5.20060mdk (2006.0)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.7.13) Gecko/20060418 Fedora/1.0.8-1.1.fc4 Firefox/1.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.7.13) Gecko/20060418 Firefox/1.0.8 (Ubuntu package 1.0.8)",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.7.13) Gecko/20060411 Firefox/1.0.8 SUSE/1.0.8-0.2",
	"Mozilla/5.0 (X11; U; Linux i686; da-DK; rv:1.7.13) Gecko/20060411 Firefox/1.0.8 SUSE/1.0.8-0.2",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.7.12) Gecko/20051105 Firefox/1.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.13) Gecko/20060410 Firefox/1.0.8",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.13) Gecko/20060410 Firefox/1.0.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.13) Gecko/20060410 Firefox/1.0.8",
	"Mozilla/5.0 (X11; U; x86_64 Linux; en_US; rv:1.7.12) Gecko/20050915 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.12) Gecko/20050927 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.12) Gecko/20050922 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.7.12) Gecko/20051121 Firefox/1.0.7 (Nexenta package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.7.12) Gecko/20050922 Fedora/1.0.7-1.1.fc4 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.12) Gecko/20060202 CentOS/1.0.7-1.4.3.centos4 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.12) Gecko/20051218 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.12) Gecko/20051127 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.12) Gecko/20051010 Firefox/1.0.7 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.12) Gecko/20051010 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.12) Gecko/20050922 Fedora/1.0.7-1.1.fc4 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.7.12) Gecko/20051222 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; Linux ppc; da-DK; rv:1.7.12) Gecko/20051010 Firefox/1.0.7 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.7.12) Gecko/20051010 Firefox/1.0.7 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.7.12) Gecko/20051010 Firefox/1.0.7 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; it-IT; rv:1.7.12) Gecko/20051010 Firefox/1.0.7 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; hu-HU; rv:1.7.12) Gecko/20051010 Firefox/1.0.7 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.7.12) Gecko/20051010 Firefox/1.0.7 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.7.12) Gecko/20050922 Firefox/1.0.7 (Debian package 1.0.7-1)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.7.12) Gecko/20050922 Fedora/1.0.7-1.1.fc4 Firefox/1.0.7",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.7.10) Gecko/20050919 (No IDN) Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.10) Gecko/20050724 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.7.10) Gecko/20050717 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.7.10) Gecko/20050730 Firefox/1.0.6 (Debian package 1.0.6-2)",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.7.10) Gecko/20050717 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.7.10) Gecko/20050721 Firefox/1.0.6 (Ubuntu package 1.0.6)",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.7.10) Gecko/20050716 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20051111 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20051106 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050920 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050918 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050911 Firefox/1.0.6 (Debian package 1.0.6-5)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050815 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050811 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050721 Firefox/1.0.6 (Ubuntu package 1.0.6)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050720 Fedora/1.0.6-1.1.fc4.k12ltsp.4.4.0 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050720 Fedora/1.0.6-1.1.fc3 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050719 Red Hat/1.0.6-1.4.1 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050716 Firefox/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050715 Firefox/1.0.6 SUSE/1.0.6-16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT5.1; en; rv:1.7.10) Gecko/20050716 Firefox/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en; rv:1.7.10) Gecko/20050716 Firefox/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.9) Gecko/20050711 Firefox/1.0.5",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.8) Gecko/20050512 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.8) Gecko/20050511 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.7.8) Gecko/20050524 Fedora/1.0.4-4 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.7.10) Gecko/20050925 Firefox/1.0.4 (Debian package 1.0.4-2sarge5)",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.7.8) Gecko/20050511 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.7.10) Gecko/20050925 Firefox/1.0.4 (Debian package 1.0.4-2sarge5)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050610 Firefox/1.0.4 (Debian package 1.0.4-3)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050524 Fedora/1.0.4-4 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050523 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050517 Firefox/1.0.4 (Debian package 1.0.4-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050513 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050513 Fedora/1.0.4-1.3.1 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050512 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050511 Firefox/1.0.4 SUSE/1.0.4-1.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050511 Firefox/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051010 Firefox/1.0.4 (Ubuntu package 1.0.7)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20070530 Firefox/1.0.4 (Debian package 1.0.4-2sarge17)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20070116 Firefox/1.0.4 (Debian package 1.0.4-2sarge15)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20061113 Firefox/1.0.4 (Debian package 1.0.4-2sarge13)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20060927 Firefox/1.0.4 (Debian package 1.0.4-2sarge12)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.7) Gecko/20050421 Firefox/1.0.3 (Debian package 1.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.7.7) Gecko/20060303 Firefox/1.0.3",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.7.7) Gecko/20050420 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.7) Gecko/20050414 Firefox/1.0.3 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; da-DK; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Win98; fr-FR; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Win98; es-ES; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.7.7) Gecko/20050414 Firefox/1.0.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; nl-NL; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; ru-RU; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050317 Firefox/1.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.6) Gecko/20050325 Firefox/1.0.2 (Debian package 1.0.2-1)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-DE; rv:1.7.6) Gecko/20050321 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr-TR; rv:1.7.6) Gecko/20050321 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv-SE; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ro-RO; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl-NL; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6) Gecko/20050317 Firefox/1.0.2 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6) Gecko/20050317 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.7.6) Gecko/20050321 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.7.6) Gecko/20050321 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.6) Gecko/20050317 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-GB; rv:1.7.6) Gecko/20050321 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.7.6) Gecko/20050321 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Win98; fr-FR; rv:1.7.6) Gecko/20050318 Firefox/1.0.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.6) Gecko/20050317 Firefox/1.0.2 (ax)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050317 Firefox/1.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050311 Firefox/1.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050310 Firefox/1.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050225 Firefox/1.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.7.6) Gecko/20050322 Firefox/1.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.7.6) Gecko/20050306 Firefox/1.0.1 (Debian package 1.0.1-2)",
	"Mozilla/5.0 (X11; U; Linux i686; cs-CZ; rv:1.7.6) Gecko/20050226 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:1.7.6) Gecko/20050226 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.7.6) Gecko/20050226 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6) Gecko/20050225 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6) Gecko/20050223 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.7.6) Gecko/20050226 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.7.6) Gecko/20050226 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.7.6) Gecko/20050223 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.6) Gecko/20050225 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.7.6) Gecko/20050226 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.7.6) Gecko/20050223 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.6) Gecko/20040206 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Win98; fr-FR; rv:1.7.6) Gecko/20050226 Firefox/1.0.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.6) Gecko/20050225 Firefox/1.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; hu; rv:1.8b4) Gecko/20050827 Firefox/1.0+",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8b4) Gecko/20050729 Firefox/1.0+",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.7.5) Gecko/20041109 Firefox/1.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.7.6) Gecko/20050405 Firefox/1.0 (Ubuntu package 1.0.2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050405 Firefox/1.0 (Ubuntu package 1.0.2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20050814 Firefox/1.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20050221 Firefox/1.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20050210 Firefox/1.0 (Debian package 1.0+dfsg.1-6)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041218 Firefox/1.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041215 Firefox/1.0 Red Hat/1.0-12.EL4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041204 Firefox/1.0 (Debian package 1.0.x.2-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041128 Firefox/1.0 (Debian package 1.0-4)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041117 Firefox/1.0 (Debian package 1.0-2.0.0.45.linspire0.4)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041107 Firefox/1.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.7.6) Gecko/20050405 Firefox/1.0 (Ubuntu package 1.0.2)",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.7.5) Gecko/20041108 Firefox/1.0",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.5) Gecko/20041128 Firefox/1.0 (Debian package 1.0-4)",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.7.5) Gecko/20041114 Firefox/1.0",
	"Mozilla/5.0 (X11; Linux i686; rv:1.7.5) Gecko/20041108 Firefox/1.0",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.7.5) Gecko/20041107 Firefox/1.0",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:1.7.5) Gecko/20041108 Firefox/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.7.5) Gecko/20041119 Firefox/1.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7) Gecko/20040917 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7) Gecko/20040802 Firefox/0.9.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.7) Gecko/20040707 Firefox/0.9.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7) Gecko/20040707 Firefox/0.9.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7) Gecko/20040707 Firefox/0.9.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7) Gecko/20040630 Firefox/0.9.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.7) Gecko/20040626 Firefox/0.9.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.7) Gecko/20040626 Firefox/0.9.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7) Gecko/20040614 Firefox/0.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7) Gecko/20040614 Firefox/0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.6) Gecko/20040614 Firefox/0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.6) Gecko/20040225 Firefox/0.8",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.6) Gecko/20040207 Firefox/0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.6) Gecko/20040206 Firefox/0.8",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.3) Gecko/20041020 Firefox/0.10.1",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.3) Gecko/20041001 Firefox/0.10.1",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.3) Gecko/20040914 Firefox/0.10.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; rv:1.7.3) Gecko/20041001 Firefox/0.10.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:1.7.3) Gecko/20041001 Firefox/0.10.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:1.7.3) Gecko/20040913 Firefox/0.10.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:1.7.3) Gecko/20040911 Firefox/0.10.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; rv:1.7.3) Gecko/20041001 Firefox/0.10.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; rv:1.7.3) Gecko/20040913 Firefox/0.10.1",
	"Mozilla/5.0 (Windows; U; Win98; rv:1.7.3) Gecko/20041001 Firefox/0.10.1",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.3) Gecko/20040914 Firefox/0.10",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.3) Gecko/20040913 Firefox/0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:1.7.3) Gecko/20040913 Firefox/0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; zh-TW; rv:1.8.0.1) Gecko/20060111 Firefox/0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; rv:1.7.3) Gecko/20040913 Firefox/0.10",
	"Mozilla/5.0 (Windows; U; Win98; rv:1.7.3) Gecko/20040913 Firefox/0.10",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; rv:1.7.3) Gecko/20040913 Firefox/0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081202 Firefox (Debian-2.0.0.19-0etch1)",
	"Mozilla/5.0 (X11; U; Gentoo Linux x86_64; pl-PL) Gecko Firefox",
	"Mozilla/5.0 (X11; ; Linux x86_64; rv:1.8.1.6) Gecko/20070802 Firefox",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.0.6) Gecko/2009011913 Firefox",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.9.2.20) Gecko/20110803 Firefox",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; rv:1.8.1.16) Gecko/20080702 Firefox",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.13) Gecko/20080313 Firefox",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:2.0) Treco/20110515 Fireweb Navigator/2.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Flock/3.5.3.4628 Chrome/7.0.517.450 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Flock/3.5.2.4599 Chrome/7.0.517.442 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Flock/3.5.2.4599 Chrome/7.0.517.442 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Flock/3.5.0.4568 Chrome/7.0.517.440 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.4; en-US; rv:1.9.0.19) Gecko/2010062819 Firefox/3.0.19 Flock/2.6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.19) Gecko/2010061201 Firefox/3.0.19 Flock/2.6.0",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.0.16) Gecko/2009122206 Firefox/3.0.16 Flock/2.5.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.0.16) Gecko/2010021003 Firefox/3.0.16 Flock/2.5.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.7) Gecko/20091221 Firefox/3.5.7 Flock/2.5.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.7) Gecko/20091221 AppleWebKit/531.21.8 KHTML/4.3.5 (like Gecko) Firefox/3.5.7 Flock/2.5.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.7) Gecko/20091221 AppleWebKit/531.21.8 KHTML/4.3.2 (like Gecko) Firefox/3.5.7 Flock/2.5.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.7) Gecko/20091221 AppleWebKit/531.21.8 (KHTML, like Gecko) Firefox/3.5.7 Flock/2.5.6 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.16) Gecko/2010010414 Firefox/3.0.19 Flock/2.5.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.5; en-US; rv:1.9.0.16) Gecko/2010010314 Firefox/3.0.16 Flock/2.5.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en-US; rv:1.9.0.16) Gecko/2010010314 Firefox/3.0.16 Flock/2.5.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-AR; rv:1.9.0.2) Gecko/2008091920 Firefox/3.0.2 Flock/2.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.2) Gecko/2008092122 Firefox/3.0.2 Flock/2.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.2) Gecko/2008083108 Firefox/3.0.2 Flock/2.0b3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.1) Gecko/2008071523 Firefox/3.0.1 Flock/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.1) Gecko/2008071523 Firefox/3.0.1 Flock/2.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9) Gecko/2008061302 Firefox/3.0 Flock/2.0b1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9pre) Gecko/2008051917 Firefox/3.0pre Flock/2.0a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; zh-TW; rv:1.9.0.5) Gecko/2009012219 Firefox/3.0.5 Flock/2.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.0.5) Gecko/2009012105 Firefox/3.0.5 Flock/2.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-CA; rv:1.9.0.5) Gecko/2009012102 Firefox/3.0.5 Flock/2.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.5) Gecko/2008121620 Firefox/3.0.5 Flock/2.0.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.4; en-US; rv:1.9.0.5) Gecko/2008121716 Firefox/3.0.5 Flock/2.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.4) Gecko/2008112016 Firefox/3.0.4 Flock/2.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.4) Gecko/2008112016 Firefox/3.0.4 Flock/2.0.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.0.4) Gecko/2008111323 Firefox/3.0.4 Flock/2.0.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.4; en-US; rv:1.9.0.4) Gecko/2008111323 Firefox/3.0.4 Flock/2.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.3) Gecko/2008100719 Firefox/3.0.3 Flock/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.3) Gecko/2008100719 Firefox/3.0.3 Flock/2.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.0.3) Gecko/2008100716 Firefox/3.0.3 Flock/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.18) Gecko/20081107 Firefox/2.0.0.18 Flock/1.2.7",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.17) Gecko/20080913 Firefox/2.0.0.17 Flock/1.2.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; it; rv:1.8.1.17) Gecko/20080922 Firefox/2.0.0.17 Flock/1.2.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.17) Gecko/20080915 Firefox/2.0.0.17 Flock/1.2.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17) Gecko/20080915 Firefox/2.0.0.17 Flock/1.2.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17) Gecko/20080910 Firefox/2.0.0.17 Flock/1.2.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.16) Gecko/20080831 Firefox/2.0.0.16 Flock/1.2.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.16) Gecko/20080714 Firefox/2.0.0.16 Flock/1.2.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.8.1.15) Gecko/20080706 Firefox/2.0.0.15 Flock/1.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.14) Gecko/20080608 Firefox/2.0.0.14 Flock/1.2.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080530 Firefox/2.0.0.14 Flock/1.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pl; rv:1.8.1.14) Gecko/20080519 Firefox/2.0.0.14 Flock/1.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.8.1.14) Gecko/20080603 Firefox/2.0.0.14 Flock/1.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.14) Gecko/20080530 Firefox/2.0.0.14 Flock/1.2.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.14) Gecko/20080530 Firefox/2.0.0.14 Flock/1.2.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20101206 Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.14) Gecko/20080514 Firefox/2.0.0.14 Flock/1.1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.14) Gecko/20080514 Firefox/2.0.0.14 Flock/1.1.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080414 Firefox/2.0.0.14 Flock/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.14) Gecko/20080414 Firefox/2.0.0.14 Flock/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.13) Gecko/20080326 Firefox/2.0.0.13 Flock/1.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.13) Gecko/20080326 Firefox/2.0.0.13 Flock/1.1.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080304 Firefox/2.0.0.12 Flock/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.12) Gecko/20080304 Firefox/2.0.0.12 Flock/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.8) Gecko/20071018 Firefox/2.0.0.8 Flock/1.0RC3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.12) Gecko/20080211 Firefox/2.0.0.12 Flock/1.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.8.1.11) Gecko/20080131 Firefox/2.0.0.11 Flock/1.0.8",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.11) Gecko/20080126 Firefox/2.0.0.11 Flock/1.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.11) Gecko/20080126 Firefox/2.0.0.11 Flock/1.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.11) Gecko/20071231 Firefox/2.0.0.11 Flock/1.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20071206 Firefox/2.0.0.11 Flock/1.0.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.9) Gecko/20071106 Firefox/2.0.0.9 Flock/1.0.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.8) Gecko/20071101 Firefox/2.0.0.8 Flock/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.7) Gecko/20071013 Firefox/2.0.0.7 Flock/0.9.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.7) Gecko/20070925 Firefox/2.0.0.7 Flock/0.9.1.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.6) Gecko/20070914 Firefox/2.0.0.6 Flock/0.9.1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.6) Gecko/20070801 Firefox/2.0.0.6 Flock/0.9.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4) Gecko/20070707 Firefox/2.0.0.4 Flock/0.9.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070307 Firefox/2.0.0.2 Flock/0.7.99",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.9pre) Gecko/20061219 Firefox/1.5.0.9 Flock/0.7.9.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.9) Gecko/20061219 Flock/0.7.9.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.8) Gecko/20061025 Firefox/1.5.0.8 Flock/0.7.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.7) Gecko/20061025 Firefox/1.5.0.8 Flock/0.7.8",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.8) Gecko/20061109 Firefox/1.5.0.8 Flock/0.7.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.7) Gecko/20060929 Firefox/1.5.0.7 Flock/0.7.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.7) Gecko/20060915 Firefox/1.5.0.7 Flock/0.7.5.1",
	"Mozilla/5.0 (X11; U; Linux ia64; pl; rv:1.8.0.5) Gecko/20060801 Firefox/1.5.0.5 Flock/0.7.4.1",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.5) Gecko/20060801 Firefox/1.5.0.5 Flock/0.7.4.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.5) Gecko/20060731 Firefox/1.5.0.5 Flock/0.7.4.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.8.0.5) Gecko/20060801 Firefox/1.5.0.5 Flock/0.7.4.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.5) Gecko/20060731 Firefox/1.5.0.5 Flock/0.7.4.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.5) Gecko/20060731 Firefox/1.5.0.5 Flock/0.7.4.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.12) Gecko/20070530 Firefox/1.5.0.12 Flock/0.7.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.11) Gecko/20070501 Firefox/1.5.0.11 Flock/0.7.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.11) Gecko/20070502 Firefox/1.5.0.11 Flock/0.7.13.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.11) Gecko/20070321 Firefox/1.5.0.11 Flock/0.7.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.10) Gecko/20070228 Firefox/1.5.0.10 Flock/0.7.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.4) Gecko/20060620 Firefox/1.5.0.4 Flock/0.7.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.4) Gecko/20060620 Firefox/1.5.0.4 Flock/0.7.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060612 Firefox/1.5.0.4 Flock/0.7.0.17.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.4) Gecko/20060612 Firefox/1.5.0.4 Flock/0.7.0.17.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.4) Gecko/20060612 Firefox/1.5.0.4 Flock/0.7.0.17.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.1) Gecko/20060331 Flock/0.7",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.1) Gecko/20060314 Flock/0.5.13.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060314 Flock/0.5.13.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.1) Gecko/20060314 Flock/0.5.13.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060217 Flock/0.5.11",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.1) Gecko/20060217 Flock/0.5.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060102 Flock/0.4.11 Firefox/1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8) Gecko/20051119 Flock/0.4.11 Firefox/1.0+",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8b5) Gecko/20051019 Flock/0.4 Firefox/1.0+",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8b5) Gecko/20051103 Flock/0.4 Firefox/1.0+",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8b5) Gecko/20051021 Flock/0.4 Firefox/1.0+",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8b5) Gecko/20051021 Flock/0.4 Firefox/1.0+",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; nl-nl) AppleWebKit/532.3+ (KHTML, like Gecko) Fluid/0.9.6 Safari/532.3+",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; nl-nl) AppleWebKit/531.9 (KHTML, like Gecko) Fluid/0.9.6 Safari/531.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/528.16 (KHTML, like Gecko) Fluid/0.9.6 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.13 (KHTML, like Gecko) Fluid/0.9.4 Safari/525.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.8) Gecko/20090327 Galeon/2.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.8) Gecko Galeon/2.0.6 (Ubuntu 2.0.6-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.7) Gecko Galeon/2.0.6 (Debian 2.0.6-2.1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko Galeon/2.0.6 (Ubuntu 2.0.6-2.1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080716 (Gentoo) Galeon/2.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081216 Galeon/2.0.4 Firefox/2.0.0.19",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080716 (Gentoo) Galeon/2.0.4",
	"Mozilla/5.0 (X11; U; Linux sparc64; en-GB; rv:1.8.1.11) Gecko/20071217 Galeon/2.0.3 Firefox/2.0.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070508 (Debian-1.8.1.4-3) Galeon/2.0.2 (Debian package 2.0.2-4)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060627 Galeon/2.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.13pre) Gecko/20080207 Galeon/2.0.1 (Ubuntu package 2.0.1-1ubuntu2) Firefox/1.5.0.13pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/Debian-1.8.0.1-5 Galeon/2.0.1 (Debian package 2.0.1-3)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060027 (Debian-1.8.0.1-11) Galeon/2.0.1 (Debian package 2.0.1-3)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.13) Gecko/20060501 Galeon/2.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20060122 Galeon/2.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051007 Galeon/2.0.0 (Debian package 2.0.0-1)",
	"Mozilla/5.0 (X11; U; Linux i686) Gecko/20030430 Galeon/1.3.4 Debian/1.3.4.20030509-1",
	"Mozilla/5.0 (X11; U; Linux i686) Gecko/20030327 Galeon/1.3.4 Debian/1.3.3.20030419-1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20050929 Galeon/1.3.21",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.7.12) Gecko/20051007 Galeon/1.3.21 (Debian package 1.3.21-8)",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.7.12) Gecko/20051105 Galeon/1.3.21",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050718 Galeon/1.3.20 (Debian package 1.3.20-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050513 Galeon/1.3.20 (Debian package 1.3.20-1)",
	"Mozilla/5.0 (X11; U; Linux i686; hu; rv:1.7.3) Gecko/20050130 Galeon/1.3.19",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.3) Gecko/20041007 Galeon/1.3.18 (Debian package 1.3.18-1.1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.2) Gecko/20040906 Galeon/1.3.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.6) Gecko/20040510 Galeon/1.3.16",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.6) Gecko/20040406 Galeon/1.3.15",
	"Mozilla/5.0 (X11; U; Linux; i686; en-US; rv:1.6) Gecko Galeon/1.3.14",
	"Mozilla/5.0 Galeon/1.2.9 (X11; Linux i686; U;) Gecko/20021213 Debian/1.2.9-0.bunk",
	"Mozilla/5.0 Galeon/1.2.8 (X11; Linux i686; U;) Gecko/20030317",
	"Mozilla/5.0 Galeon/1.2.8 (X11; Linux i686; U;) Gecko/20030212",
	"Mozilla/5.0 Galeon/1.2.7 (X11; Linux i686; U;) Gecko/20021226 Debian/1.2.7-6",
	"Mozilla/5.0 Galeon/1.2.6 (X11; Linux i686; U;) Gecko/20020916",
	"Mozilla/5.0 Galeon/1.2.6 (X11; Linux i686; U;) Gecko/20020913 Debian/1.2.6-2",
	"Mozilla/5.0 Galeon/1.2.6 (X11; Linux i686; U;) Gecko/20020830",
	"Mozilla/5.0 Galeon/1.2.6 (X11; Linux i686; U;) Gecko/20020827",
	"Mozilla/5.0 Galeon/1.2.6 (X11; Linux i586; U;) Gecko/20020916",
	"Mozilla/5.0 Galeon/1.2.5 (X11; Linux i686; U;) Gecko/20020809",
	"Mozilla/5.0 Galeon/1.2.5 (X11; Linux i686; U;) Gecko/20020623 Debian/1.2.5-0.woody.1",
	"Mozilla/5.0 Galeon/1.2.5 (X11; Linux i686; U;) Gecko/20020610 Debian/1.2.5-1",
	"Mozilla/5.0 Galeon/1.2.5 (X11; Linux i686; U;) Gecko/0",
	"Mozilla/5.0 Galeon/1.2.5 (X11; Linux i586; U;) Gecko/20020623 Debian/1.2.5-0.woody.1",
	"Mozilla/5.0 Galeon/1.0.3 (X11; Linux i686; U;) Gecko/0",
	"Mozilla/5.0(X11;U;Linux(x86_64);en;rv:1.9a8)Gecko/2007100619;GranParadiso/3.1",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.9a8) Gecko/2007100620 GranParadiso/3.1",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.9a8) Gecko/2007100620 GranParadiso/3.0a8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a7) Gecko/2007080210 GranParadiso/3.0a7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a5) Gecko/20070605 GranParadiso/3.0a5",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9a4) Gecko/20070427 GranParadiso/3.0a4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a4) Gecko/20070427 GranParadiso/3.0a4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a4) Gecko/2007042705 GranParadiso/3.0a4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a4) Gecko/20070427 GranParadiso/3.0a4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a3) Gecko/20070322 GranParadiso/3.0a3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a3) Gecko/20070322 GranParadiso/3.0a3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2) Gecko/20070206 GranParadiso/3.0a2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20061204 GranParadiso/3.0a1",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9a1) Gecko/20061204 GranParadiso/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9a1) Gecko/20061204 GranParadiso/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061204 GranParadiso/3.0a1 MEGAUPLOAD 1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061204 GranParadiso/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9a1) Gecko/20061204 GranParadiso/3.0a1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.9a1) Gecko/20061204 GranParadiso/3.0a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.9) Gecko/2009042210 GranParadiso/3.0.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009033008 GranParadiso/3.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.8) Gecko/2009033017 GranParadiso/3.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.9.0.8) Gecko/2009033017 GranParadiso/3.0.8",
	"Mozilla/5.0 (X11; U; Darwin i386; en-US; rv:1.9.0.8) Gecko/2009040414 GranParadiso/3.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.0.7pre) Gecko/2009012106 GranParadiso/3.0.7pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.7) Gecko/2009030719 GranParadiso/3.0.7 FirePHP/0.2.4",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.9.0.7) Gecko/2009030719 GranParadiso/3.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.7) Gecko/2009030719 GranParadiso/3.0.7 FirePHP/0.2.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.4pre) Gecko/2008092704 GranParadiso/3.0.4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.4pre) Gecko/2008102405 GranParadiso/3.0.4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.4pre) Gecko/2008101305 GranParadiso/3.0.4pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.3pre) Gecko/2008092604 GranParadiso/3.0.3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.3pre) Gecko/2008091304 GranParadiso/3.0.3pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.0.3pre) Gecko/2008090704 GranParadiso/3.0.3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.2pre) Gecko/2008072405 GranParadiso/3.0.2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.2pre) Gecko/2008071405 GranParadiso/3.0.2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.1) Gecko/2008071818 GranParadiso/3.0.1",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9) Gecko/20070314 GranParadiso/3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9a3) Gecko/20070409 GranParadiso/2.0.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9a1) Gecko/20061204 GranParadiso/2.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; WOW64; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506; Media Center PC 5.0; .NET CLR 3.5.21022; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; InfoPath.2; .NET CLR 3.0.30729; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; OfficeLiveConnector.1.4; OfficeLivePatch.1.3; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6; .NET CLR 2.0.50727; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6.3; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB0.0; InfoPath.1; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 4.0.20506; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322; InfoPath.2; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.0.3705; .NET CLR 1.1.4322; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.04506.30; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; InfoPath.2; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 4.0; .NET CLR 2.0.50727; InfoPath.1; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 1.1.4322; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 1.1.4322; .NET CLR 3.0.04506.648; InfoPath.1; GreenBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; GreenBrowser)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko) Hana/1.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/417.9 (KHTML, like Gecko) Hana/1.0",
	"Mozilla/5.0 (Macintosh; U; i386 Mac OS X; en) AppleWebKit/417.9 (KHTML, like Gecko) Hana/1.0",
	"Mozilla/5.0 (compatible; IBrowse 3.0; AmigaOS4.0)",
	"Mozilla/4.0 (compatible; IBrowse 2.3; AmigaOS4.0)",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10_5_8) AppleWebKit/537.3+ (KHTML, like Gecko) iCab/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10_5_8) AppleWebKit/537.1+ (KHTML, like Gecko) iCab/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10_5_8) AppleWebKit/536.25+ (KHTML, like Gecko) iCab/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10_5_8) AppleWebKit/536.17+ (KHTML, like Gecko) iCab/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10_5_8) AppleWebKit/536.15+ (KHTML, like Gecko) iCab/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10_5_8) AppleWebKit/534.50.2 (KHTML, like Gecko) iCab/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8) AppleWebKit/536.15 (KHTML, like Gecko) iCab/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_8_1; nn-no) AppleWebKit/533.21.1 (KHTML, like Gecko) iCab/4.8b Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; nn-no) AppleWebKit/533.21.1 (KHTML, like Gecko) iCab/4.8b Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_8; en-us) AppleWebKit/533.21.1 (KHTML, like Gecko) iCab/4.8 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-us) AppleWebKit/533.21.1 (KHTML, like Gecko) iCab/4.8 Safari/533.16",
	"Mozilla/5.0 (compatible; iCab 3.0.5; Macintosh; U; PPC Mac OS)",
	"Mozilla/5.0 (compatible; iCab 3.0.5; Macintosh; U; PPC Mac OS X)",
	"Mozilla/5.0 (compatible; iCab 3.0.3; Macintosh; U; PPC Mac OS)",
	"Mozilla/5.0 (compatible; iCab 3.0.3; Macintosh; U; PPC Mac OS X)",
	"Mozilla/5.0 (compatible; iCab 3.0.2; Macintosh; U; PPC Mac OS)",
	"Mozilla/5.0 (compatible; iCab 3.0.2; Macintosh; U; PPC Mac OS X)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS; en) iCab 3",
	"Mozilla/4.5 (compatible; iCab 2.9.9; Macintosh; U; 68K)",
	"Mozilla/4.5 (compatible; iCab 2.9.5; Macintosh; U; PPC; Mac OS X)",
	"Mozilla/4.5 (compatible; iCab 2.9.1; Macintosh; U; PPC; Mac OS X)",
	"Mozilla/4.5 (compatible; iCab 2.9.1; Macintosh; U; PPC)",
	"Mozilla/4.5 (compatible; iCab 2.8.1; Macintosh; I; PPC)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.13) Gecko/20100916 Iceape/2.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.11) Gecko/20100721 Iceape/2.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.9) Gecko/20100502 Iceape/2.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20110302 Iceape/2.0.11",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20101227 Iceape/2.0.11",
	"Mozilla/5.0 (X11; U; Linux ppc; fr; rv:1.8.1.13) Gecko/20080313 Iceape/1.1.9 (Debian-1.1.9-5)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.1.13) Gecko/20080313 Iceape/1.1.9 (Debian-1.1.9-5)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.9) Gecko/20071030 Iceape/1.1.6 (Debian-1.1.6-3)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071008 Iceape/1.1.5 (Ubuntu-1.1.5-1ubuntu0.7.10)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070509 Iceape/1.1.2 (Debian-1.1.2-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.19) Gecko/20081204 Iceape/1.1.14 (Debian-1.1.14-1) Mnenhy/0.7.6.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081204 Iceape/1.1.14 (Debian-1.1.14-1)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.0.13pre) Gecko/20070505 Iceape/1.0.9 (Debian-1.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.13pre) Gecko/20070505 Iceape/1.0.9 (Debian-1.0.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.12) Gecko/20070510 Iceape/1.0.9 (Debian-1.0.9-0etch1)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.0.11) Gecko/20070217 Iceape/1.0.8 (Debian-1.0.8-4)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.11) Gecko/20070217 Iceape/1.0.8 (Debian-1.0.8-4)",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.8.0.11) Gecko/20070217 Iceape/1.0.8 (Debian-1.0.8-4)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.0.9) Gecko/20061219 Iceape/1.0.7 (Debian-1.0.7-3)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.0.9) Gecko/20061219 Iceape/1.0.7 (Debian-1.0.7-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20061219 Iceape/1.0.7 (Debian-1.0.7-3)",
	"Mozilla/5.0 (X11; Linux x86_64; rv:17.0) Gecko/20121201 icecat/17.0.1",
	"Mozilla/5.0 (X11; Linux i686; rv:7.0.1) Gecko/20111106 IceCat/7.0.1",
	"Mozilla/5.0 (X11; U; Linux sparc64; es-PY; rv:5.0) Gecko/20100101 IceCat/5.0 (like Firefox/5.0; Debian-6.0.1)",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b8) Gecko/20101227 IceCat/4.0b8",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.2.13) Gecko/20101203 IceCat/3.6.13-g1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.13) Gecko/20101214 IceCat/3.6.13 (like Firefox/3.6.13)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.2.13) Gecko/20101221 IceCat/3.6.13 (like Firefox/3.6.13) (Zenwalk GNU Linux)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; it; rv:1.9.2.12) Gecko/20101114 IceCat/3.6.12 (like Firefox/3.6.12)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.3) Gecko/2008092921 IceCat/3.0.3-g1",
	"Mozilla/5.0 (X11; U; Linux i686; en-CA; rv:1.9.0.3) Gecko/2008092921 IceCat/3.0.3-g1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008100722 IceCat/3.0.2-g1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.1) Gecko/2008072716 IceCat/3.0.1-g1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061920 IceCat/3.0-g1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11) Gecko/20071203 IceCat/2.0.0.11-g1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:17.0) Gecko/20121202 Firefox/17.0 Iceweasel/17.0.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:15.0) Gecko/20100101 Firefox/15.0.1 Iceweasel/15.0.1",
	"Mozilla/5.0 (X11; Linux i686; rv:15.0) Gecko/20100101 Firefox/15.0.1 Iceweasel/15.0.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:15.0) Gecko/20120724 Debian Iceweasel/15.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:15.0) Gecko/20100101 Firefox/15.0 Iceweasel/15.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:12.0) Gecko/20120721 Debian Iceweasel/15.0",
	"Mozilla/5.0 (X11; Linux i686; rv:15.0) Gecko/20100101 Firefox/15.0 Iceweasel/15.0",
	"Mozilla/5.0 (X11; debian; Linux x86_64; rv:15.0) Gecko/20100101 Iceweasel/15.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:14.0) Gecko/20100101 Firefox/14.0.1 Iceweasel/14.0.1",
	"Mozilla/5.0 (X11; Linux i686; rv:14.0) Gecko/20100101 Firefox/14.0.1 Iceweasel/14.0.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:14.0) Gecko/20100101 Firefox/14.0 Iceweasel/14.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:12.0) Gecko/20100101 Debian Iceweasel/14.0",
	"Mozilla/5.0 (X11; Linux i686; rv:14.0) Gecko/20100101 Firefox/14.0 Iceweasel/14.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:13.0) Gecko/20100101 Firefox/13.0.1 Iceweasel/13.0.1",
	"Mozilla/5.0 (X11; Linux i686; rv:13.0) Gecko/20100101 Firefox/13.0.1 Iceweasel/13.0.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:13.0) Gecko/20100101 Firefox/13.0 Iceweasel/13.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:11.0a2) Gecko/20111230 Firefox/11.0a2 Iceweasel/11.0a2",
	"Mozilla/5.0 (X11; Gentoo Linux x86_64; rv:11.0a2) Gecko/20111230 Firefox/11.0a2 Iceweasel/11.0a2",
	"Mozilla/5.0 (X11; Linux x86_64; rv:10.0a2) Gecko/20111118 Firefox/10.0a2 Iceweasel/10.0a2",
	"Mozilla/5.0 (X11; Linux x86_64; rv:10.0.7) Gecko/20100101 Firefox/10.0.7 Iceweasel/10.0.7",
	"Mozilla/5.0 (X11; Linux ppc; rv:10.0.7) Gecko/20100101 Firefox/10.0.7 Iceweasel/10.0.7",
	"Mozilla/5.0 (X11; Linux i686; rv:10.0.7) Gecko/20100101 Iceweasel/10.0.7",
	"Mozilla/5.0 (X11; Linux i686; rv:10.0.7) Gecko/20100101 Firefox/10.0.7 Iceweasel/10.0.7",
	"Mozilla/5.0 (X11; Linux x86_64; rv:10.0.6) Gecko/20100101 Firefox/10.0.6 Iceweasel/10.0.6",
	"Mozilla/5.0 (X11; Linux i686; rv:10.0.6) Gecko/20100101 Firefox/10.0.6 Iceweasel/10.0.6",
	"Mozilla/5.0 (X11; Linux i686 on x86_64; rv:10.0.6) Gecko/20100101 Firefox/10.0.6 Iceweasel/10.0.6",
	"Mozilla/5.0 (X11; Linux armv6l; rv:10.0.6) Gecko/20100101 Firefox/10.0.6 Iceweasel/10.0.6",
	"Mozilla/5.0 (X11; Linux armv6l; rv:10.0.5) Gecko/20100101 Firefox/10.0.5 Iceweasel/10.0.5",
	"Mozilla/5.0 (X11; Linux x86_64; rv:10.0) Gecko/20100101 Firefox/10.0 Iceweasel/10.0",
	"Mozilla/5.0 (X11; Linux i686; rv:10.0) Gecko/20100101 Firefox/10.0 Iceweasel/10.0",
	"Mozilla/5.0 (X11; Linux i686; rv:9.0a2) Gecko/20111104 Firefox/9.0a2 Iceweasel/9.0a2",
	"Mozilla/5.0 (X11; Linux x86_64; rv:9.0.1) Gecko/20100101 Firefox/9.0.1 Iceweasel/9.0.1",
	"Mozilla/5.0 (X11; Linux i686; rv:9.0.1) Gecko/20100101 Firefox/9.0.1 Iceweasel/9.0.1",
	"Mozilla/5.0 (X11; Linux i686; rv:8.0) Gecko/20100101 Firefox/8.0 Iceweasel/8.0",
	"Mozilla/5.0 (X11; Linux Debian i686; rv:8.0) Gecko/20100101 Firefox/8.0 Iceweasel/8.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:7.0.1) Gecko/20100101 Firefox/7.0.1 Iceweasel/7.0.1 Debian",
	"Mozilla/5.0 (X11; Linux i686; Debian Testing; rv:7.0.1) Gecko/20100101 Firefox/7.0.1 Iceweasel/7.0.1",
	"Mozilla/5.0 (X11; Linux i686 on x86_64; rv:6.0.2) Gecko/20100101 Firefox/6.0.2 Iceweasel/6.0.2",
	"Mozilla/5.0 (X11; Linux x86_64; rv:5.0) Gecko/20100101 Firefox/5.0 Iceweasel/5.0",
	"Mozilla/5.0 (X11; Linux i686; rv:5.0) Gecko/20100101 Firefox/5.0 Iceweasel/5.0",
	"Mozilla/5.0 (X11; Linux i686; rv: 5.0) Gecko/20100101 Firefox/5.0 Iceweasel/5.0",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0) Gecko/20110322 Firefox/4.0 Iceweasel/4.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.2.13) Gecko/20101203 Iceweasel/3.6.7 (like Firefox/3.6.13)",
	"Mozilla/5.0 (X11; U; Linux i686; pt-PT; rv:1.9.2.3) Gecko/20100402 Iceweasel/3.6.3 (like Firefox/3.6.3) GTB7.0",
	"Mozilla/5.0 (X11; U; Linux 2.6.34.1-SquidSheep; en-US; rv:1.9.2.3) Gecko/20100402 Iceweasel/3.6.3 (like Firefox/3.6.3)",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.2.13) Gecko/20110109 Iceweasel/3.6.13 (like Firefox/3.6.13)",
	"Mozilla/5.0 (X11; U; Linux i686; pt-PT; rv:1.9.2.3) Gecko/20100402 Iceweasel/3.6 (like Firefox/3.6) GTB7.0",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.9.1.9) Gecko/20100501 Iceweasel/3.5.9 (like Firefox/3.5.9)",
	"Mozilla/5.0 (X11; U; Linux x86_64; ja; rv:1.9.1.8) Gecko/20100324 Iceweasel/3.5.8 (like Firefox/3.5.8)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.9) Gecko/20100501 Iceweasel/3.5.8 (like Firefox/3.5.8)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.9) Gecko/20100501 Iceweasel/3.5.6 (like Firefox/3.5.6; Debian-3.5.6-2)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.5) Gecko/20091112 Iceweasel/3.5.5 (like Firefox/3.5.5; Debian-3.5.5-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.3) Gecko/20091010 Iceweasel/3.5.3 (Debian-3.5.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-GB; rv:1.9.1.3) Gecko/20091010 Iceweasel/3.5.3 (Debian-3.5.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.19) Gecko/20110430 Iceweasel/3.5.19 (like Firefox/3.5.19)",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.9.1.18) Gecko/20110324 Iceweasel/3.5.18 (like Firefox/3.5.18)",
	"Mozilla/5.0 (X11; U; Linux x86_64; sv-SE; rv:1.9.1.16) Gecko/20120714 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.1.16) Gecko/20120921 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.1.16) Gecko/20120714 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.1.16) Gecko/20120602 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.1.16) Gecko/20111108 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; hu-HU; rv:1.9.1.16) Gecko/20110107 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.16) Gecko/20120714 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.16) Gecko/20120511 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.1.16) Gecko/20120602 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.9.1.16) Gecko/20120315 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20120714 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20120602 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20111108 Iceweasel/3.5.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.16) Gecko/20110107 Iceweasel/3.5.16 (Debian-3.0.5-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.1.16) Gecko/20120714 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.1.16) Gecko/20120131 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ; rv:1.9.1.16) Gecko/20120602 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.1.16) Gecko/20120602 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.1.16) Gecko/20120714 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.1.16) Gecko/20111108 Iceweasel/3.5.16 (like Firefox/3.5.16)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.1.11) Gecko/20100819 Iceweasel/3.5.11 (like Firefox/3.5.11)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1) Gecko/20090704 Iceweasel/3.5 (Debian-3.5-0)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b3pre) Gecko/20090207 Ubuntu/9.04 (jaunty) IceWeasel/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b5) Gecko/2008042623 Iceweasel/3.0b5 (Debian-3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.11) Gecko/2009061208 Iceweasel/3.0.9 (Debian-3.0.9-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.18) Gecko/2010021720 Iceweasel/3.0.9 (Debian-3.0.9-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.11) Gecko/2009061212 Iceweasel/3.0.9 (Debian-3.0.9-1) GTB5",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.7) Gecko/2009030814 Iceweasel/3.0.9 (Debian-3.0.9-1)",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.9.0.11) Gecko/2009061212 Iceweasel/3.0.9 (Debian-3.0.9-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009033109 Gentoo Iceweasel/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.8) Gecko/2009032917 Gentoo Iceweasel/3.0.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.7) Gecko/2009030810 Iceweasel/3.0.7 (Debian-3.0.7-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.8) Gecko/2009032809 Iceweasel/3.0.7 (Debian-3.0.7-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.6) Gecko/2009020407 Iceweasel/3.0.7 (Debian-3.0.7-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.7) Gecko/2009030810 Iceweasel/3.0.7 (Debian-3.0.7-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.8) Gecko/2009032811 Iceweasel/3.0.7 (Debian-3.0.7-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.7) Gecko/2009032813 Iceweasel/3.0.6 Firefox/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.7) Gecko/2009031819 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.0.19) Gecko/2010072022 Iceweasel/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko/2009032813 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.19) Gecko/2011050707 Iceweasel/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.12) Gecko/2009072220 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.11) Gecko/2009061208 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.7) Gecko/2009031819 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.19) Gecko/2012013123 Iceweasel/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.16) Gecko/2009121609 Iceweasel/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux i686; nl; rv:1.9.0.11) Gecko/2009061212 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.9.0.7) Gecko/2009032803 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.6) Gecko/2009020409 Iceweasel 3.0.6 (Debian 5.0",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.19) Gecko/2010120923 Iceweasel/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.19) Gecko/2011092908 Iceweasel/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.19) Gecko/2010102906 Iceweasel/3.0.6 (Debian-3.0.6-3)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.0.13) Gecko/2009082121 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.11) Gecko/2009061212 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.9.0.7) Gecko/2009032803 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.7) Gecko/2009032803 Iceweasel/3.0.6 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.5) Gecko/2008122903 Gentoo Iceweasel/3.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.5) Gecko/2008122011 Iceweasel/3.0.5 (Debian-3.0.5-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ; rv:1.9.0.4) Gecko/2008112309 Iceweasel/3.0.4 (Debian-3.0.4-1)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.9.0.1) Gecko/2008072112 Iceweasel/3.0.3 (Debian-3.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; ca-AD; rv:1.9.0.3) Gecko/2008092816 Iceweasel/3.0.3 (Debian-3.0.3-3)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko/2008090211 Ubuntu/9.04 (jaunty) Iceweasel/3.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.0.11) Gecko/2009061212 Iceweasel/3.0.12 (Debian-3.0.12-1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.0.11) Gecko/2009061319 Iceweasel/3.0.11 (Debian-3.0.11-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.1) Gecko/2008071420 Iceweasel/3.0.1 (Debian-3.0.1-1)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.9.0.1) Gecko/2008072112 Iceweasel/3.0.1 (Debian-3.0.1-1)",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.9.0.1) Gecko/2008071618 Iceweasel/3.0.1 (Debian-3.0.1-1)",
	"Mozilla/5.0 (Linux X86; U; Debian SID; it; rv:1.9.0.1) Gecko/2008070208 Debian IceWeasel/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9) Gecko/2008062908 Iceweasel/3.0 (Debian-3.0",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.9) Gecko/2008062909 Iceweasel/3.0 (Debian-3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9) Gecko/2008062113 Iceweasel/3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008062113 Iceweasel/3.0 (Debian-3.0",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.9) Gecko/20071025 Iceweasel/2.0.0.9 (Debian-2.0.0.9-2)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.9) Gecko/20071025 Iceweasel/2.0.0.9",
	"Mozilla/15.0 (X11; U; Linux i686; es-ES; rv:1.8.1.9) Gecko/20071025 Iceweasel/2.0.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.8.1.8) Gecko/20071004 Iceweasel/2.0.0.8 (Debian-2.0.0.6+2.0.0.8-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071004 Iceweasel/2.0.0.8 (Debian-2.0.0.6+2.0.0.8-0etch1)",
	"Mozilla/5.0 (X11; U; Linux x64; en-US; rv:1.8.1.7) Gecko/20070914 Iceweasel/2.0.0.7 (Debian-2.0.0.7-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.6) Gecko/20070723 Iceweasel/2.0.0.6 (Debian-2.0.0.6-0etch1)",
	"Mozilla/5.0 (X11; U; Linux x64; en-US; rv:1.8.1.6) Gecko/20070723 Iceweasel/2.0.0.6 (Debian-2.0.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.6) Gecko/20070723 Iceweasel/2.0.0.6 (Debian-2.0.0.6-0etch1+lenny1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.6) Gecko/20070723 Iceweasel/2.0.0.6 (Debian-2.0.0.6-0etch1+lenny1) (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.4) Gecko/20070508 Iceweasel/2.0.0.4 (Debian-2.0.0.4-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070508 Iceweasel/2.0.0.4 (Debian-2.0.0.4-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070508 Iceweasel/2.0.0.4 (Debian-2.0.0.4-0etch1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; nb-NO; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080311 Firefox/2.0 Iceweasel/2.0.0.3 (Debian-2.0.0.13-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.3) Gecko/20070310 Iceweasel/2.0.0.3 (Debian-2.0.0.3-1)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9a3) Gecko/20070409 IceWeasel/2.0.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070208 Iceweasel/2.0.0.2 (Debian-2.0.0.2+dfsg-3)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.19) Gecko/20081202 Iceweasel/2.0.0.19 (Debian-2.0.0.19-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.18) Gecko/20081030 Iceweasel/2.0.0.18 (Debian-2.0.0.18-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.17) Gecko/20080827 Iceweasel/2.0.0.17 (Debian-2.0.0.17-0etch1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.16) Gecko/20080702 Iceweasel/2.0.0.16 (Debian-2.0.0.16-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.16) Gecko/20080702 Iceweasel/2.0.0.16 (Debian-2.0.0.16-0etch1)",
	"Mozilla/5.0 (X11; U; Linux ppc; de; rv:1.8.1.15) Gecko/20080612 Iceweasel/2.0.0.15 (Debian-2.0.0.15-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.15) Gecko/20080612 Iceweasel/2.0.0.15 (Debian-2.0.0.15-0etch1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-2)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-2)",
	"Mozilla/5.0 (X11; U; Linux sparc64; en-US; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-2)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.14) Gecko/20080404 Iceweasel/2.0.0.14 (Debian-2.0.0.14-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.13) Gecko/20080311 Iceweasel/2.0.0.13 (Debian-2.0.0.13-1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.1.13) Gecko/20080311 Iceweasel/2.0.0.13 (Debian-2.0.0.13-0etch1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080311 Firefox/2.0.0.3 Iceweasel/2.0.0.13 (Debian-2.0.0.13-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.8.1.12) Gecko/20080129 Iceweasel/2.0.0.12 (Debian-2.0.0.12-1)",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.12) Gecko/20080129 Iceweasel/2.0.0.12 (Debian-2.0.0.12-1)",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.1.12) Gecko/20080129 Iceweasel/2.0.0.12 (Debian-2.0.0.12-0etch1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.8.1.11) Gecko/20071128 Iceweasel/2.0.0.11 (Debian-2.0.0.11-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.11) Gecko/20071128 Iceweasel/2.0.0.11 (Debian-2.0.0.11-1)",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.8.1.11) Gecko/20071128 Iceweasel/2.0.0.11 (Debian-2.0.0.11-1)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ru; rv:1.9.2.13) Gecko/20101203 IceWeasel/2.0.0.11 Mnenhy/0.8.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-4)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.8.1.1) Gecko/2006120502 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-1)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.8.1.1) Gecko/20061205 Iceweasel/2.0.0.1 (Debian-2.0.0.1+dfsg-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1) Gecko/20061024 Iceweasel/2.0 (Debian-2.0+dfsg-1)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-GB; rv:1.9.0.9) Gecko/2009050519 iceweasel/2.0 (Debian-3.0.6-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1) Gecko/20061024 Iceweasel/2.0 (Debian-2.0+dfsg-1)",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en; rv:1.8.1) Gecko/20061024 Iceweasel/2.0 (Debian-2.0+dfsg-1)",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en; rv:1.8.1) Gecko/20061024 Iceweasel/2.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.7) Gecko/20061022 Iceweasel/1.5.0.7-g2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8pre) Gecko/20061001 Firefox/1.5.0.8pre (Iceweasel)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.0.7) Gecko/2009030814 Iceweasel Firefox/3.0.7 (Debian-3.0.7-1)",
	"Mozilla/5.0 (Linux) Gecko Iceweasel (Debian) Mnenhy",
	"Mozilla/5.0 (Future Star Technologies Corp.; Star-Blade OS; x86_64; U; en-US) iNet Browser 4.7",
	"Mozilla/6.0 (Future Star Technologies Corp. Star-Blade OS; U; en-US) iNet Browser 2.5",
	"Mozilla/5.0 (compatible; MSIE 10.6; Windows NT 6.1; Trident/5.0; InfoPath.2; SLCC1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 2.0.50727) 3gpp-gba UNTRUSTED/1.0",
	"Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.1; WOW64; Trident/6.0)",
	"Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.1; Trident/6.0)",
	"Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.1; Trident/5.0)",
	"Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.1; Trident/4.0; InfoPath.2; SV1; .NET CLR 2.0.50727; WOW64)",
	"Mozilla/5.0 (compatible; MSIE 10.0; Macintosh; Intel Mac OS X 10_7_3; Trident/6.0)",
	"Mozilla/4.0 (compatible; MSIE 10.0; Windows NT 6.1; Trident/5.0)",
	"Mozilla/1.22 (compatible; MSIE 10.0; Windows 3.1)",
	"Mozilla/5.0 (Windows; U; MSIE 9.0; WIndows NT 9.0; en-US))",
	"Mozilla/5.0 (Windows; U; MSIE 9.0; Windows NT 9.0; en-US)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 7.1; Trident/5.0)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0; SLCC2; Media Center PC 6.0; InfoPath.3; MS-RTC LM 8; Zune 4.7)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0; SLCC2; Media Center PC 6.0; InfoPath.3; MS-RTC LM 8; Zune 4.7",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Zune 4.0; InfoPath.3; MS-RTC LM 8; .NET4.0C; .NET4.0E)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0; chromeframe/12.0.742.112)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET CLR 2.0.50727; Media Center PC 6.0)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET CLR 2.0.50727; Media Center PC 6.0)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0; .NET CLR 2.0.50727; SLCC2; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Zune 4.0; Tablet PC 2.0; InfoPath.3; .NET4.0C; .NET4.0E)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0; yie8)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2; .NET CLR 1.1.4322; .NET4.0C; Tablet PC 2.0)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0; FunWebProducts)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0; chromeframe/13.0.782.215)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0; chromeframe/11.0.696.57)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0) chromeframe/10.0.648.205",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/4.0; GTB7.4; InfoPath.1; SV1; .NET CLR 2.8.52393; WOW64; en-US)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.0; Trident/5.0; chromeframe/11.0.696.57)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.0; Trident/4.0; GTB7.4; InfoPath.3; SV1; .NET CLR 3.1.76908; WOW64; en-US)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; GTB7.4; InfoPath.2; SV1; .NET CLR 3.3.69573; WOW64; en-US)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; InfoPath.1; SV1; .NET CLR 3.8.36217; WOW64; en-US)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; .NET CLR 2.7.58687; SLCC2; Media Center PC 5.0; Zune 3.4; Tablet PC 3.6; InfoPath.3)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.2; Trident/4.0; Media Center PC 4.0; SLCC1; .NET CLR 3.0.04320)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; SLCC1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; InfoPath.2; SLCC1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.1; SLCC1; .NET CLR 1.1.4322)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.0; Trident/4.0; InfoPath.1; SV1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 3.0.04506.30)",
	"Mozilla/5.0 (compatible; MSIE 7.0; Windows NT 5.0; Trident/4.0; FBSMTWB; .NET CLR 2.0.34861; .NET CLR 3.0.3746.3218; .NET CLR 3.5.33652; msn OptimizedIE8;ENUS)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.2; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; Media Center PC 6.0; InfoPath.2; MS-RTC LM 8)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; Media Center PC 6.0; InfoPath.2; MS-RTC LM 8",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; Media Center PC 6.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; InfoPath.3; .NET4.0C; .NET4.0E; .NET CLR 3.5.30729; .NET CLR 3.0.30729; MS-RTC LM 8)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Zune 3.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; msn OptimizedIE8;ZHCN)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; MS-RTC LM 8; InfoPath.3; .NET4.0C; .NET4.0E) chromeframe/8.0.552.224",
	"Mozilla/4.0(compatible; MSIE 7.0b; Windows NT 6.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 6.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 3.0.04506.30)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; Media Center PC 3.0; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; FDM; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.1.4322; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.1.4322; Alexa Toolbar; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.1.4322; Alexa Toolbar)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.40607)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0b; Windows NT 5.1; .NET CLR 1.0.3705; Media Center PC 3.1; Alexa Toolbar; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (Windows; U; MSIE 7.0; Windows NT 6.0; en-US)",
	"Mozilla/5.0 (Windows; U; MSIE 7.0; Windows NT 6.0; el-GR)",
	"Mozilla/5.0 (Windows; U; MSIE 7.0; Windows NT 5.2)",
	"Mozilla/5.0 (MSIE 7.0; Macintosh; U; SunOS; X11; gu; SV1; InfoPath.2; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648)",
	"Mozilla/5.0 (compatible; MSIE 7.0; Windows NT 6.0; WOW64; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; c .NET CLR 3.0.04506; .NET CLR 3.5.30707; InfoPath.1; el-GR)",
	"Mozilla/5.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; c .NET CLR 3.0.04506; .NET CLR 3.5.30707; InfoPath.1; el-GR)",
	"Mozilla/5.0 (compatible; MSIE 7.0; Windows NT 6.0; fr-FR)",
	"Mozilla/5.0 (compatible; MSIE 7.0; Windows NT 6.0; en-US)",
	"Mozilla/5.0 (compatible; MSIE 7.0; Windows NT 5.2; WOW64; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (compatible; MSIE 7.0; Windows 98; SpamBlockerUtility 6.3.91; SpamBlockerUtility 6.2.91; .NET CLR 4.1.89;GB)",
	"Mozilla/4.79 [en] (compatible; MSIE 7.0; Windows NT 5.0; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 1.1.4322; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648)",
	"Mozilla/4.0 (Windows; MSIE 7.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (Mozilla/4.0; MSIE 7.0; Windows NT 5.1; FDM; SV1; .NET CLR 3.0.04506.30)",
	"Mozilla/4.0 (Mozilla/4.0; MSIE 7.0; Windows NT 5.1; FDM; SV1)",
	"Mozilla/4.0 (compatible;MSIE 7.0;Windows NT 6.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.2; Win64; x64; Trident/6.0; .NET4.0E; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; WOW64; SLCC2; .NET CLR 2.0.50727; InfoPath.3; .NET4.0C; .NET4.0E; .NET CLR 3.5.30729; .NET CLR 3.0.30729; MS-RTC LM 8)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; WOW64; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; MS-RTC LM 8; .NET4.0C; .NET4.0E; InfoPath.3)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/6.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET4.0C; .NET4.0E)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; chromeframe/12.0.742.100)",
	"Mozilla/4.0 (compatible; MSIE 6.1; Windows XP; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.1; Windows XP)",
	"Mozilla/4.0 (compatible; MSIE 6.01; Windows NT 6.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.1; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.0; YComp 5.0.2.6)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.0; YComp 5.0.0.0) (Compatible; ; ; Trident/4.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.0; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.0; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 4.0; .NET CLR 1.0.2914)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 4.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows 98; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows 98; Win 9x 4.90)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.0; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 4.0)",
	"Mozilla/5.0 (Windows; U; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4325)",
	"Mozilla/5.0 (compatible; MSIE 6.0; Windows NT 5.1)",
	"Mozilla/45.0 (compatible; MSIE 6.0; Windows NT 5.1)",
	"Mozilla/4.08 (compatible; MSIE 6.0; Windows NT 5.1)",
	"Mozilla/4.01 (compatible; MSIE 6.0; Windows NT 5.1)",
	"Mozilla/4.0 (X11; MSIE 6.0; i686; .NET CLR 1.1.4322; .NET CLR 2.0.50727; FDM)",
	"Mozilla/4.0 (Windows; MSIE 6.0; Windows NT 6.0)",
	"Mozilla/4.0 (Windows; MSIE 6.0; Windows NT 5.2)",
	"Mozilla/4.0 (Windows; MSIE 6.0; Windows NT 5.0)",
	"Mozilla/4.0 (Windows; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (MSIE 6.0; Windows NT 5.1)",
	"Mozilla/4.0 (MSIE 6.0; Windows NT 5.0)",
	"Mozilla/4.0 (compatible;MSIE 6.0;Windows 98;Q312461)",
	"Mozilla/4.0 (Compatible; Windows NT 5.1; MSIE 6.0) (compatible; MSIE 6.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; U; MSIE 6.0; Windows NT 5.1) (Compatible; ; ; Trident/4.0; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; U; MSIE 6.0; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; InfoPath.3; Tablet PC 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; GTB6.5; QQDownload 534; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC2; .NET CLR 2.0.50727; Media Center PC 6.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729)",
	"Mozilla/4.0 (compatible; MSIE 5.5b1; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.50; Windows NT; SiteKiosk 4.9; SiteCoach 1.0)",
	"Mozilla/4.0 (compatible; MSIE 5.50; Windows NT; SiteKiosk 4.8; SiteCoach 1.0)",
	"Mozilla/4.0 (compatible; MSIE 5.50; Windows NT; SiteKiosk 4.8)",
	"Mozilla/4.0 (compatible; MSIE 5.50; Windows 98; SiteKiosk 4.8)",
	"Mozilla/4.0 (compatible; MSIE 5.50; Windows 95; SiteKiosk 4.8)",
	"Mozilla/4.0 (compatible;MSIE 5.5; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1)",
	"Mozilla/4.0 (compatible; MSIE 5.5;)",
	"Mozilla/4.0 (Compatible; MSIE 5.5; Windows NT5.0; Q312461; SV1; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT5)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 6.1; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; .NET4.0E)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 6.1; chromeframe/12.0.742.100; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 5.5)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 5.2; .NET CLR 1.1.4322; InfoPath.2; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; FDM)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 5.2; .NET CLR 1.1.4322) (Compatible; ; ; Trident/4.0; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 5.2; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 5.23; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.22; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.21; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.2; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.2; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.17; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.17; Mac_PowerPC Mac OS; en)",
	"Mozilla/4.0 (compatible; MSIE 5.16; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.16; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.15; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.15; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.14; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.13; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.12; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.12; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.05; Windows NT 4.0)",
	"Mozilla/4.0 (compatible; MSIE 5.05; Windows NT 3.51)",
	"Mozilla/4.0 (compatible; MSIE 5.05; Windows 98; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT; Hotbar 4.1.8.0)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; YComp 5.0.2.6; MSIECrawler)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; YComp 5.0.2.6; Hotbar 4.2.8.0)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; YComp 5.0.2.6; Hotbar 3.0)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; YComp 5.0.2.6)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; YComp 5.0.2.4)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; YComp 5.0.0.0; Hotbar 4.1.8.0)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; Wanadoo 5.6)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; Wanadoo 5.3; Wanadoo 5.5)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; Wanadoo 5.1)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; SV1; .NET CLR 1.1.4322; .NET CLR 1.0.3705; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; SV1)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; Q312461; T312461)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; Q312461)",
	"Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; MSIECrawler)",
	"Mozilla/4.0 (compatible; MSIE 5.0b1; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 5.00; Windows 98)",
	"Mozilla/4.0(compatible; MSIE 5.0; Windows 98; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT;)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT; DigExt; YComp 5.0.2.6)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT; DigExt; YComp 5.0.2.5)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT; DigExt; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT; DigExt; Hotbar 4.1.8.0)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT; DigExt; Hotbar 3.0)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT; DigExt; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 5.9; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 5.2; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98;)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98; YComp 5.0.2.4)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98; Hotbar 3.0)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98; DigExt; YComp 5.0.2.6; yplus 1.0)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98; DigExt; YComp 5.0.2.6)",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98; DigExt; YComp 5.0.2.5; YComp 5.0.0.0)",
	"Mozilla/4.0 (compatible; MSIE 4.5; Windows NT 5.1; .NET CLR 2.0.40607)",
	"Mozilla/4.0 (compatible; MSIE 4.5; Windows 98; )",
	"Mozilla/4.0 (compatible; MSIE 4.5; Mac_PowerPC)",
	"Mozilla/4.0 (compatible; MSIE 4.5; Mac_PowerPC)",
	"Mozilla/4.0 PPC (compatible; MSIE 4.01; Windows CE; PPC; 240x320; Sprint:PPC-6700; PPC; 240x320)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows NT)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows NT 5.0)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Sprint;PPC-i830; PPC; 240x320)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Sprint; SCH-i830; PPC; 240x320)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Sprint:SPH-ip830w; PPC; 240x320)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Sprint:SPH-ip320; Smartphone; 176x220)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Sprint:SCH-i830; PPC; 240x320)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Sprint:SCH-i320; Smartphone; 176x220)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Sprint:PPC-i830; PPC; 240x320)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; Smartphone; 176x220)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; PPC; 240x320; Sprint:PPC-6700; PPC; 240x320)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; PPC; 240x320; PPC)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE; PPC)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows CE)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows 98; Hotbar 3.0)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows 98; DigExt)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows 98)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Windows 95)",
	"Mozilla/4.0 (compatible; MSIE 4.01; Mac_PowerPC)",
	"Mozilla/4.0 WebTV/2.6 (compatible; MSIE 4.0)",
	"Mozilla/4.0 (compatible; MSIE 4.0; Windows NT)",
	"Mozilla/4.0 (compatible; MSIE 4.0; Windows 98 )",
	"Mozilla/4.0 (compatible; MSIE 4.0; Windows 95; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 4.0; Windows 95)",
	"Mozilla/4.0 (Compatible; MSIE 4.0)",
	"Mozilla/2.0 (compatible; MSIE 4.0; Windows 98)",
	"Mozilla/2.0 (compatible; MSIE 3.03; Windows 3.1)",
	"Mozilla/2.0 (compatible; MSIE 3.02; Windows 3.1)",
	"Mozilla/2.0 (compatible; MSIE 3.01; Windows 95)",
	"Mozilla/2.0 (compatible; MSIE 3.01; Windows 95)",
	"Mozilla/2.0 (compatible; MSIE 3.0B; Windows NT)",
	"Mozilla/3.0 (compatible; MSIE 3.0; Windows NT 5.0)",
	"Mozilla/2.0 (compatible; MSIE 3.0; Windows 95)",
	"Mozilla/2.0 (compatible; MSIE 3.0; Windows 3.1)",
	"Mozilla/4.0 (compatible; MSIE 2.0; Windows NT 5.0; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/1.22 (compatible; MSIE 2.0; Windows 95)",
	"Mozilla/1.22 (compatible; MSIE 2.0; Windows 3.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/4.0; iRider 2.60.0008; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; iRider 2.21.1108)",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.4 (KHTML, like Gecko) Chrome/22.0.1250.0 Iron/22.0.2150.0 Safari/537.4",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.4 (KHTML, like Gecko) Chrome/22.0.1250.0 Iron/22.0.2150.0 Safari/537.4",
	"Mozilla/5.0 (X11; U; Linux amd64) Iron/21.0.1200.0 Chrome/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.1 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_1) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_0) AppleWebKit/537.1 (KHTML, like Gecko) Chrome/21.0.1200.0 Iron/21.0.1200.0 Safari/537.1",
	"Mozilla/5.0 (X11; U; Linux amd64) Iron/20.0.1150.1 Chrome/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Windows NT 5.0) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_0) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.1 Iron/20.0.1150.1 Safari/536.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.0 Iron/20.0.1150.0 Safari/536.11",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.0 Iron/20.0.1150.0 Safari/536.11",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/536.11 (KHTML, like Gecko) Chrome/20.0.1150.0 Iron/20.0.1150.0 Safari/536.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/536.5 (KHTML, like Gecko) Iron/19.0.1100.0 Chrome/19.0.1100.0 Safari/536.5",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/536.5 (KHTML, like Gecko) Iron/19.0.1100.0 Chrome/19.0.1100.0 Safari/536.5",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/536.5 (KHTML, like Gecko) Iron/19.0.1100.0 Chrome/19.0.1100.0 Safari/536.5",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_4) AppleWebKit/536.5 (KHTML, like Gecko) Iron/19.0.1100.0 Chrome/19.0.1100.0 Safari/536.5",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_4) AppleWebKit/535.19 (KHTML, like Gecko) Iron/18.0.1050.0 Chrome/18.0.1050.0 Safari/535.19",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.2 Chrome/17.0.1000.2 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.1 Chrome/17.0.1000.1 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.1 Chrome/17.0.1000.1 Safari/535.11",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.1 Chrome/17.0.1000.1 Safari/535.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.11 (KHTML, like Gecko) Iron/17.0.1000.0 Chrome/17.0.1000.0 Safari/535.11",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/535.7 (KHTML, like Gecko) Iron/16.0.950.0 Chrome/16.0.950.0 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.7 (KHTML, like Gecko) Iron/16.0.950.0 Chrome/16.0.950.0 Safari/535.7 []",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.1 (KHTML, like Gecko) Iron/16.0.950.0 Chrome/16.0.950.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/535.7 (KHTML, like Gecko) Iron/16.0.950.0 Chrome/16.0.950.0 Safari/535.7",
	"Mozilla/5.0 (Windows NT 5.2) AppleWebKit/535.7 (KHTML, like Gecko) Iron/16.0.950.0 Chrome/16.0.950.0 Safari/535.7",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_3) AppleWebKit/535.7 (KHTML, like Gecko) Iron/16.0.950.0 Chrome/16.0.950.0 Safari/535.7",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_2) AppleWebKit/535.7 (KHTML, like Gecko) Iron/16.0.950.0 Chrome/16.0.950.0 Safari/535.7",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.2 (KHTML, like Gecko) Iron/15.0.900.1 Chrome/15.0.900.1 Safari/535.2",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_3) AppleWebKit/535.1 (KHTML, like Gecko) Iron/14.0.850.0 Chrome/14.0.850.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.1 (KHTML, like Gecko) Iron/13.0.800.1 Chrome/13.0.800.1 Safari/535.1",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/535.1 (KHTML, like Gecko) Iron/13.0.800.0 Chrome/13.0.800.0 Safari/535.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/535.1 (KHTML, like Gecko) Iron/13.0.800.0 Chrome/13.0.800.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/535.1 (KHTML, like Gecko) Iron/13.0.800.0 Chrome/13.0.800.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/535.1 (KHTML, like Gecko) Iron/13.0.800.0 Chrome/13.0.800.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/535.1 (KHTML, like Gecko) Iron/13.0.800.0 Chrome/13.0.800.0 Safari/535.1",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/535.1 (KHTML, like Gecko) Iron/13.0.800.0 Chrome/13.0.800.0 Safari/535.1",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.30 (KHTML, like Gecko) Iron/12.0.750.0 Chrome/12.0.750.0 Safari/534.30",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.30 (KHTML, like Gecko) Iron/12.0.750.0 Chrome/12.0.750.0 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.30 (KHTML, like Gecko) Iron/12.0.750.0 Chrome/12.0.750.0 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.30 (KHTML, like Gecko) Iron/12.0.750.0 Chrome/12.0.750.0 Safari/534.30 Lightning/1.0b4pre",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.30 (KHTML, like Gecko) Iron/12.0.750.0 Chrome/12.0.750.0 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/534.30 (KHTML, like Gecko) Iron/12.0.750.0 Chrome/12.0.750.0 Safari/534.30",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.30 (KHTML, like Gecko) Iron/12.0.750.0 Chrome/12.0.750.0 Safari/534.30",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.777.3 Chrome/11.0.777.3 Safari/534.66",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.3 Chrome/11.0.700.3 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.3 Chrome/11.0.700.3 Safari/534.66",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.3 Chrome/11.0.700.3 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.3 Chrome/11.0.700.3 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.3 Chrome/11.0.700.3 Safari/534.24",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (X11; Linux i686) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.0; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1; U; zh-tw) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.2 Chrome/11.0.700.2 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.1 Chrome/11.0.700.1 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.1 Chrome/11.0.700.1 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.0) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.1 Chrome/11.0.700.1 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.1 Chrome/11.0.700.1 Safari/534.24",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.24 (KHTML, like Gecko) Iron/11.0.700.0 Chrome/11.0.700.0 Safari/534.24",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.1 Chrome/10.0.650.1 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.1 Chrome/10.0.650.1 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.0 Chrome/10.0.650.0 Safari/534.16",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.0 Chrome/10.0.650.0 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.0 Chrome/10.0.650.0 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.0 Chrome/10.0.650.0 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.0 Chrome/10.0.650.0 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR) AppleWebKit/534.16 (KHTML, like Gecko) Iron/10.0.650.0 Chrome/10.0.650.0 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Iron/9.0.600.2 Chrome/9.0.600.2 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Iron/9.0.600.2 Chrome/9.0.600.2 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Iron/9.0.600.2 Chrome/9.0.600.2 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.13 (KHTML, like Gecko) Iron/9.0.600.2 Chrome/9.0.600.2 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.1 Chrome/7.0.520.1 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-jp) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.1 Chrome/7.0.520.1 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.1 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.1 Chrome/7.0.520.1 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.1 Chrome/7.0.520.1 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.0 Chrome/7.0.520.0 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.0 Chrome/7.0.520.0 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.0 Chrome/7.0.520.0 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.0 Chrome/7.0.520.0 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) Iron/7.0.520.0 Chrome/7.0.520.0 Safari/534.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/534.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/98035072.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/97486176.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/9724672.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/95066112.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/94403424.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/89895776.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/83554272.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/76829344.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/73530880.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/69296032.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/6838624.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/67162016.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/65969728.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/65209600.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/6316928.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/61389024.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/61170080.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/58473792.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475.1 Chrome/6.0.475.1 Safari/534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/92861792.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/66529120.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/53013696.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/42050816.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/112818688.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/87693504.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/59178784.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/56984160.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/42721888.534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Chrome/6.0.475.0 Safari/14183168.534",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Safari/534",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE) AppleWebKit/534.3 (KHTML, like Gecko) Iron/6.0.475 Safari/534",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.1 (KHTML, like Gecko) Iron/5.0.326.0 Chrome/5.0.326.0 Safari/533.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Iron/4.0.280.0 Chrome/4.0.280.0 Safari/532.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Iron/4.0.280.0 Chrome/4.0.275.0 Safari/532.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Chrome/4.0.280.0 Safari/532.8 Iron/4.0.280.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Iron/4.0.280.0 Chrome/4.0.280.0 Safari/532.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.9 (KHTML, like Gecko) Iron/4.0.280.0 Chrome/4.0.280.0 Safari/532.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Iron/4.0.275.2 Chrome/4.0.275.2 Safari/532.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Iron/4.0.275.2 Chrome/4.0.275.2 Safari/532.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Iron/4.0.275.2 Chrome/4.0.275.2 Safari/532.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Chrome/4.0.275.2 Safari/532.8 Iron/4.0.275.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Iron/4.0.275.2 Chrome/4.0.275.2 Safari/532.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Iron/4.0.275.2 Chrome/4.0.275.2 Safari/532.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.8 (KHTML, like Gecko) Iron/4.0.275.2 Chrome/4.0.275.2 Safari/532.8",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/532.5 (KHTML, like Gecko) Iron/4.0.275.2 Chrome/4.0.275.2 Safari/532.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Iron/4.0.227.0 Chrome/4.0.227.0 Safari/532.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Iron/4.0.227.0 Chrome/4.0.227.0 Safari/532",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US) AppleWebKit/532.3 (KHTML, like Gecko) Iron/4.0.227.0 Chrome/4.0.227.0 Safari/532.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Iron/4.0.227.0 Chrome/4.0.227.0 Safari/532.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Iron/4.0.227.0 Chrome/4.0.227.0 Safari/532.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/532.3 (KHTML, like Gecko) Iron/4.0.227.0 Chrome/4.0.227.0 Safari/532.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Iron/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U;) AppleWebKit/532.0 (KHTML, like Gecko) Iron/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) Chrome/0.0.0 Iron/3.0.197.0 AppleWebKit/532.0 (KHTML, like Gecko) Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Iron/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/532.0 (KHTML, like Gecko) Chrome/0.0.0 Safari/532.0 Iron/3.0.197.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE) AppleWebKit/532.0 (KHTML, like Gecko) Iron/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1) AppleWebKit/532.0 (KHTML, like Gecko) Iron/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (U;) AppleWebKit/532.0 (KHTML, like Gecko) Iron/3.0.197.0 Safari/532.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/531.0 (KHTML, like Gecko) Iron/3.0.189.0 Safari/531.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/530.9 (KHTML, like Gecko) Iron/2.0.178.0 Safari/530.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.1 (KHTML, like Gecko) Iron/2.0.168.0 Safari/530.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.1 (KHTML, like Gecko) Iron/2.0.168.0 Safari/530.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/528.7 (KHTML, like Gecko) Iron/1.0.155.0 Safari/528.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.7 (KHTML, like Gecko) Iron/1.0.155.0 Safari/528.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.7 (KHTML, like Gecko) Iron/1.0.155.0 Safari/528.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.5 (KHTML, like Gecko) Iron/0.4.155.0 Safari/528.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.4 (KHTML, like Gecko) Iron/0.3.155.0 Safari/19322656.528",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.4 (KHTML, like Gecko) Iron/0.3.155.0 Safari/18455624.528",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.4 (KHTML, like Gecko) Iron/0.3.155.0 Safari/13506912.528",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/41562480.525",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/14871328.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US)AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/13657880.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/28768176.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/13543896.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12733120.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12595016.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12542120.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12535056.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12475112.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12285712.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12282560.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12279816.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12272384.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12207312.525",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Iron/0.2.152.0 Safari/12079480.525",
	"Mozilla/5.0 (Windows; U;) AppleWebKit/532.0 (KHTML, like Gecko) Iron",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.21pre) Gecko K-Meleon/1.7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1) Gecko K-Meleon/1.6.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.8.1.24pre) Gecko/20100228 K-Meleon/1.5.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JA; rv:1.8.1.24pre) Gecko/20100228 K-Meleon/1.5.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.24) Gecko/20100228 K-Meleon/1.5.4",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.24pre) Gecko/20091010 K-Meleon/1.5.4",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.24) Gecko/20100228 K-Meleon/1.5.4",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.8.1.24) Gecko/20100228 K-Meleon/1.5.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.7pre) Gecko K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; fr-FR; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.8.1.22) Gecko/20090623 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hu-HU; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.23) Gecko/20090825 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.22) Gecko/20090623 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:1.8.1.21) Gecko/20090331 K-Meleon/1.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.12) Gecko/20050610 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20041220 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.12) Gecko/20050610 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.5) Gecko/20041220 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.7.5) Gecko/20041220 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5) Gecko/20031016 K-Meleon/0.8.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.5) Gecko/20031016 K-Meleon/0.8.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.5) Gecko/20031016 K-Meleon/0.8.2",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.5) Gecko/20031016 K-Meleon/0.8",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.2b) Gecko/20021016 K-Meleon 0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2b) Gecko/20021016 K-Meleon 0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4pre) Gecko/20070404 K-Ninja/2.1.3",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.4pre) Gecko/20070404 K-Ninja/2.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2pre) Gecko/20070215 K-Ninja/2.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.7) Gecko/20060917 K-Ninja/2.0.4",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.7) Gecko/20060917 K-Ninja/2.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.6) Gecko/20060731 K-Ninja/2.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.5) Gecko/20060706 K-Ninja/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.0.1) Gecko/20080722 Firefox/3.0.1 Kapiko/3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9) Gecko/20080705 Firefox/3.0 Kapiko/3.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.7) Gecko Kazehakase/0.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.8) Gecko Fedora/1.9.0.8-1.fc10 Kazehakase/0.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.7) Gecko Kazehakase/0.5.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.1) Gecko Kazehakase/0.5.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.7) Gecko Kazehakase/0.5.4 Debian/0.5.4-2.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko Kazehakase/0.5.4 Debian/0.5.4-2.1ubuntu3",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.1.16) Gecko/20080816 Firefox/2.0.0.16 Kazehakase/0.5.4",
	"Mozilla/5.0 (X11; U; FreeBSD i386; rv:1.8.1.12) Gecko/0 Kazehakase/0.4.9",
	"Mozilla/5.0 (X11; Linux x86_64; U;) Gecko/20070610 Kazehakase/0.4.7",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20070610 Kazehakase/0.4.7",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20070322 Kazehakase/0.4.7",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20070304 Kazehakase/0.4.6",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20070322 Kazehakase/0.4.5",
	"Mozilla/5.0 (X11; Linux i686; U;) AppleWebKit/146.1 (KHTML, like Gecko) Kazehakase0.4.5",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/0 Kazehakase/0.4.3 Debian/0.4.3-1ubuntu1",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/0 Kazehakase/0.4.3",
	"Mozilla/5.0 (X11; Linux i686; U; rv:1.7) Gecko/0 Kazehakase/0.4.3",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20060216 Kazehakase/0.4.2",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20070224 Kazehakase/0.3.9",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20050923 Kazehakase/0.3.9",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/0 Kazehakase/0.3.9",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20060717 Kazehakase/0.3.8 Debian/0.3.8-2",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/0 Kazehakase/0.3.8",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/0 Kazehakase/0.3.5",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20051128 Kazehakase/0.3.3 Debian/0.3.3-1",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/0 Kazehakase/0.3.1",
	"Mozilla/5.0 (X11; Linux i686; U;) Gecko/20050923 Kazehakase/0.2.8 Debian/0.2.8-1ubuntu2",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET CLR 1.1.4322; InfoPath.3)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; GTB6.6; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; GTB6.6; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; GTB6.5; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; eSobiSubscriber 2.0.4.16; InfoPath.3)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; GTB6.6; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; GTB6.6; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; GTB6.6; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET CLR 1.1.4322; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; GTB6.5; KKMAN3.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Tablet PC 2.0; InfoPath.3; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; KKMAN3.2; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729; OfficeLiveConnector.1.5; OfficeLivePatch.1.3; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; GTB6.6; KKMAN3.2; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.30729; .NET CLR 3.5.30729; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; GTB6.6; KKMAN3.2; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.21022; .NET CLR 3.5.30729; .NET CLR 3.0.30729; OfficeLiveConnector.1.5; OfficeLivePatch.1.3; .NET4.0C; InfoPath.2; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; KKMAN3.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; KKMAN3.2; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; KKMAN3.2; InfoPath.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; AskTB5.6)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; KKMAN3.2; InfoPath.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; KKMAN3.2; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.3)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; KKMAN3.2; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; (R1 1.6); KKMAN3.2; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; KKman3.0; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; (R1 1.5); KKman3.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6.6; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6.6; KKman3.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.5.30729; OfficeLiveConnector.1.5; OfficeLivePatch.1.3; .NET CLR 3.0.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; KKman3.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506; .NET CLR 1.1.4322; MEGAUPLOAD 2.0; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; OfficeLiveConnector.1.3; OfficeLivePatch.0.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.30618; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; InfoPath.2; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; GTB6; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322; .NET CLR 3.5.21022; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; GTB5; KKman3.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; GTB5; KKman3.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; KKman3.0; InfoPath.2; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; KKman3.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; KKman3.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; KKman3.0; InfoPath.1; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; KKman3.0; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; KKman2.0; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 5.5; Windows NT 5.0; KKman2.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; KKman3.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.19) Gecko/20081217 KMLite/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.19) Gecko/20081217 KMLite/1.1.2",
	"Mozilla/5.0 (X11; Linux) KHTML/4.9.1 (like Gecko) Konqueror/4.9",
	"Mozilla/5.0 (X11; Linux 3.5.4-1-ARCH i686; es) KHTML/4.9.1 (like Gecko) Konqueror/4.9",
	"Mozilla/5.0 (X11) KHTML/4.9.1 (like Gecko) Konqueror/4.9",
	"Mozilla/5.0 (compatible; Konqueror/4.5; FreeBSD) KHTML/4.5.4 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.4; Linux) KHTML/4.4.1 (like Gecko) Fedora/4.4.1-1.fc12",
	"Mozilla/5.0 (compatible; Konqueror/4.4; Linux 2.6.32-22-generic; X11; en_US) KHTML/4.4.3 (like Gecko) Kubuntu",
	"Mozilla/5.0 (compatible; Konqueror/4.3; Linux) KHTML/4.3.1 (like Gecko) Fedora/4.3.1-3.fc11",
	"Mozilla/5.0 (compatible; Konqueror/4.3; Linux 2.6.31-16-generic; X11) KHTML/4.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.2; Linux; X11; x86_64) KHTML/4.2.4 (like Gecko) Fedora/4.2.4-2.fc11",
	"Mozilla/5.0 (compatible; Konqueror/4.2; Linux) KHTML/4.2.98 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.2; Linux) KHTML/4.2.96 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.2; Linux) KHTML/4.2.4 (like Gecko) Slackware/13.0",
	"Mozilla/5.0 (compatible; Konqueror/4.2; Linux) KHTML/4.2.4 (like Gecko) Fedora/4.2.4-2.fc11",
	"Mozilla/5.0 (compatible; Konqueror/4.2; Linux) KHTML/4.2.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.2; Linux) KHTML/4.2.1 (like Gecko) Fedora/4.2.1-4.fc11",
	"Mozilla/5.0 (compatible; Konqueror/4.2) KHTML/4.2.4 (like Gecko) Fedora/4.2.4-2.fc11",
	"Mozilla/5.0 (compatible; Konqueror/4.1; OpenBSD) KHTML/4.1.4 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.1; Linux) KHTML/4.1.4 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.1; Linux) KHTML/4.1.3 (like Gecko) Fedora/4.1.3-3.fc10",
	"Mozilla/5.0 (compatible; Konqueror/4.1; Linux 2.6.27.7-134.fc10.x86_64; X11; x86_64) KHTML/4.1.3 (like Gecko) Fedora/4.1.3-4.fc10",
	"Mozilla/5.0 (compatible; Konqueror/4.1; DragonFly) KHTML/4.1.4 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.0; X11) KHTML/4.0.3 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.0; Windows) KHTML/4.0.83 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.0; Linux; x86_64) KHTML/4.0.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/4.0; Linux) KHTML/4.0.82 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Windows NT 6.0) KHTML/3.5.6 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; SunOS) KHTML/3.5.1 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; SunOS) KHTML/3.5.0 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; SunOS)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; NetBSD 4.0_RC3; X11) KHTML/3.5.7 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; NetBSD 3.0; X11) KHTML/3.5.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; x86_64; en_US) KHTML/3.5.10 (like Gecko) SUSE",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; x86_64) KHTML/3.5.5 (like Gecko) (Debian)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; x86_64) KHTML/3.5.5 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; X11; i686; en_US) KHTML/3.5.6 (like Gecko) (Debian)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; X11) KHTML/3.5.3 (like Gecko) Kubuntu 6.06 Dapper",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; i686; U; it-IT) KHTML/3.5.5 (like Gecko) (Debian)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; en_US) KHTML/3.5.6 (like Gecko) (Kubuntu)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux; de) KHTML/3.5.5 (like Gecko) (Debian)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux) KHTML/3.5.9 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux) KHTML/3.5.7 (like Gecko) SUSE",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux) KHTML/3.5.7 (like Gecko) (Kubuntu)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux) KHTML/3.5.7 (like Gecko) (Debian)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux) KHTML/3.5.7 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.5; Linux) KHTML/3.5.6 (like Gecko) (Kubuntu)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; SunOS) KHTML/3.4.1 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux; de, en_US) KHTML/3.4.2 (like Gecko) (Debian package 4:3.4.2-4)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.3 (like Gecko) (Kubuntu package 4:3.4.3-0ubuntu2)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.3 (like Gecko) (Kubuntu package 4:3.4.3-0ubuntu1)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.3 (like Gecko) (Debian package 4:3.4.3-2)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.3 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.2 (like Gecko) (Debian package 4:3.4.2-4)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.1 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux) KHTML/3.4.0 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux 2.6.12; X11) KHTML/3.4.1 (like Gecko) (Debian package 4:3.4.1-1)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux 2.6.12.6; X11; i686; en_US) KHTML/3.4.3 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux 2.6.11; X11) KHTML/3.4.0 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4; Linux 2.6.11; X11)",
	"Mozilla/5.0 (compatible; Konqueror/3.4) KHTML/3.4.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.4) KHTML/3.4.0 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; SunOS) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.6.9-1.667) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.6.11; X11; i686; de) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.6.11; X11; i686) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.6.11; X11) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.6.11.12-whnetz-xenU; X11; i686; en_US) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.6.11) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.4.27; X11) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3; Linux 2.4.22-xfs; X11) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3) KHTML/3.3.2 (like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.3) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.2; Linux; X11; en_US) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.2; Linux) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.2; Linux 2.6.2) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.2; FreeBSD) (KHTML, like Gecko)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021224)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021219)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021203)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021124)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021119)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021113)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021105)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021019)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021006)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20021002)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020915)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020907)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020905)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020828)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020822)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020815)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020626)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020624)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020614)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc6; i686 Linux; 20020607)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20021224)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20021219)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20021212)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20021127)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20021112)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20021001)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020927)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020913)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020910)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020906)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020823)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020819)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020809)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020712)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020625)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020621)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020615)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020606)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020601)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc5; i686 Linux; 20020524)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20021217)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20021208)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20021204)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20021124)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20021114)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20021026)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020928)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020913)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020912)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020901)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020827)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020824)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020811)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020808)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020718)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020714)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020602)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020521)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020511)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc4; i686 Linux; 20020420)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20021223)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20021210)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20021204)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20021125)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20021110)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20021025)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20021004)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020926)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020915)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020912)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020818)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020725)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020716)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020709)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020607)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020520)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020515)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020510)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020426)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc3; i686 Linux; 20020421)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20021221)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20021128)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20021119)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20021020)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20021014)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20021011)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020925)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020917)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020905)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020820)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020818)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020809)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020808)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020721)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020619)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020614)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020612)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020605)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020513)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc2; i686 Linux; 20020509)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20021226)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20021221)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20021120)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20021113)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20021022)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20021008)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020919)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020823)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020816)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020723)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020722)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020718)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020711)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020703)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020620)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020618)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020608)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020520)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020515)",
	"Mozilla/5.0 (compatible; Konqueror/3.1-rc1; i686 Linux; 20020510)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; Linux; X11; i686)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; Linux; en)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021128)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021113)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021106)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021105)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021103)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021102)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021027)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021007)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021006)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20021001)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20020928)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20020913)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20020817)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20020811)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20020810)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20020720)",
	"Mozilla/5.0 (compatible; Konqueror/3.1; i686 Linux; 20020712)",
	"Mozilla/5.0 (compatible; Konqueror/3.0.0-10; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/3.0.0-10; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/3.0.0; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/3.0.0; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20021127)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20021115)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20021106)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20021027)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20021012)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020923)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020918)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020912)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020908)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020827)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020817)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020723)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020718)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020624)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020614)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020613)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020522)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020520)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020512)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc6; i686 Linux; 20020312)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021226)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021213)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021210)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021208)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021121)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021120)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021109)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021105)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021026)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021020)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20021015)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020921)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020913)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020910)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020901)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020822)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020821)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020724)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020703)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc5; i686 Linux; 20020628)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20021117)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20021028)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20021016)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20021004)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020926)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020923)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020920)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020915)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020821)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020818)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020802)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020721)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020707)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020628)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020622)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020609)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020519)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020517)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020504)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc4; i686 Linux; 20020420)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20021125)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20021123)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20021025)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20021018)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20021013)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020914)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020910)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020818)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020812)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020724)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020703)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020626)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020624)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020608)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020605)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020519)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020517)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020506)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020426)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc3; i686 Linux; 20020412)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20021221)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20021127)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20021118)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20021008)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020809)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020702)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020606)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020602)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020505)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020424)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020323)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020226)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020219)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020213)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020110)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020108)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020107)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020106)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc2; i686 Linux; 20020105)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20021208)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20021206)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20021118)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20021103)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20021026)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020917)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020911)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020906)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020808)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020807)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020801)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020726)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020723)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020705)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020704)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020703)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020523)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020515)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020319)",
	"Mozilla/5.0 (compatible; Konqueror/3.0-rc1; i686 Linux; 20020217)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20021219)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20021117)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20021107)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20021012)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20021006)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020927)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020914)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020825)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020823)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020817)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020809)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020728)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020716)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020707)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020608)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020603)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020511)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020510)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020502)",
	"Mozilla/5.0 (compatible; Konqueror/3.0; i686 Linux; 20020423)",
	"Mozilla/5.0 (compatible; Konqueror/2.2.2-3; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.2.2; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.2.2; Linux 2.4.14-xfs; X11; i686)",
	"Mozilla/5.0 (compatible; Konqueror/2.2.2)",
	"Mozilla/5.0 (compatible; Konqueror/2.2.2; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.2.1; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.2.1; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.2-12; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.2-11; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.2-11; Linux)",
	"Mozilla/5.0 (compatible; Konqueror/2.1.2; X11)",
	"Mozilla/5.0 (compatible; Konqueror/2.1.1; X11)",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru-RU) AppleWebKit/533.3 (KHTML, like Gecko) Leechcraft/0.4.55-13-g2230d9f Safari/533.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru-RU) AppleWebKit/533.3 (KHTML, like Gecko) Leechcraft/0.3.95-1-g84cc6b7 Safari/533.3",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows XP 5.1) Lobo/0.98.4",
	"Mozilla/4.0 (compatible; MSIE 6.0; Linux 2.6.26-1-amd64) Lobo/0.98.3",
	"Mozilla/4.0 (compatible; MSIE 6.0; U; Windows;) Lobo/0.98.2",
	"Mozilla/4.0 (compatible; MSIE 6.0; U; Windows;) Lobo/0.98",
	"Mozilla/4.0 (compatible; MSIE 6.0; U; Windows;) Lobo/0.97.5",
	"Mozilla/4.0 (compatible; MSIE 6.0; U; Windows;) Lobo/0.97.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.1.17) Gecko/20110123 Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2) Gecko/20070225 lolifox/0.32",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2) Gecko/20070225 lolifox/0.32",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.8pre) Gecko/20071012 lolifox/0.3.6 Firefox/2.0.0.7 compatible",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2) Gecko/20070224 lolifox/0.3.2 MEGAUPLOAD 1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2) Gecko/20070224 lolifox/0.3.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1) Gecko/20061127 lolifox/0.3.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1) Gecko/20061127 lolifox/0.3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1b1) Gecko/20060713 lolifox/0.2.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.3pre) Gecko/20100403 Lorentz/3.6.3plugin2pre (.NET CLR 4.0.20506)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.28) Gecko/20120410 Firefox/3.6.28 Lunascape/6.7.1.25446",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Lunascape 6.7.1.25446)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ko; rv:1.9.2.16) Gecko/20110325 Firefox/3.6.16 Lunascape/6.4.5.23569",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.3 (KHTML, like Gecko) Lunascape/6.4.2.23236 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ja; rv:1.9.1.15) Gecko/20101029 Firefox/3.5.15 Lunascape/6.3.4.23051 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.9.2.12) Gecko/20101029 Firefox/3.6.12 Lunascape/6.3.4.23051 ( .NET CLR 3.5.30729; .NET4.0C)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.9.1.15) Gecko/20101029 Firefox/3.5.15 Lunascape/6.3.4.23051 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.15) Gecko/20101029 Firefox/3.5.15 Lunascape/6.3.4.23051 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.15) Gecko/20101029 Firefox/3.5.15 Lunascape/6.3.4.23051 ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.15) Gecko/20101029 Firefox/3.5.15 Lunascape/6.3.4.23051 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP) AppleWebKit/533.3 (KHTML, like Gecko) Lunascape/6.3.4.23051 Safari/533.3",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; SLCC1; Tablet PC 2.0; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET CLR 1.1.4322; InfoPath.3; .NET4.0C; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; OfficeLiveConnector.1.5; OfficeLivePatch.1.3; .NET4.0C; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET4.0C; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; GTB6.5; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; InfoPath.2; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB5; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.2; OfficeLiveConnector.1.4; OfficeLivePatch.1.3; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.2; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.1; .NET4.0C; Lunascape 6.3.4.23051)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Media Center PC 5.0; SLCC1; OfficeLiveConnector.1.5; OfficeLivePatch.1.3; .NET4.0C; Lunascape 6.3.",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.13) Gecko/20100917 Firefox/3.5.13 Lunascape/6.3.3.22929",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.2; OfficeLiveConnector.1.4; OfficeLivePatch.1.3; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.3.3.22929)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Lunascape 6.3.3.22929)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt; rv:1.9.1.13) Gecko/20100917 Firefox/3.5.13 Lunascape/6.3.2.22803",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2; Lunascape 6.3.2.22803)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.11) Gecko/20100821 Firefox/3.5.11 Lunascape/6.3.1.22729",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.11) Gecko/20100723 Firefox/3.5.11 Lunascape/6.2.1.22445 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.10) Gecko/20100624 Firefox/3.5.10 Lunascape/6.2.0.22177 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.10) Gecko/20100624 Firefox/3.5.10 Lunascape/6.2.0.22177",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.4) Gecko/20100624 Firefox/3.6.4 Lunascape/6.2.0.22177 GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.10) Gecko/20100624 Firefox/3.5.10 Lunascape/6.2.0.22177 ( .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; SLCC1; Tablet PC 2.0; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; OfficeLiveConnector.1.5; OfficeLivePatch.1.3; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6.5; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6.5; .NET CLR 2.0.50727; eSobiSubscriber 2.0.4.16; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6.5; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6.5; (R1 1.6); .NET CLR 2.0.50727; eSobiSubscriber 2.0.4.16; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; GTB6.5; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; Lunascape 6.2.0.22177)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Lunascape 6.2.0.22177)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; es-ES; rv:1.9.1.10) Gecko/20100624 Firefox/3.5.10 Lunascape/6.1.7.21880 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ja; rv:1.9.1.10) Gecko/20100624 Firefox/3.5.10 Lunascape/6.1.7.21880",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Lunascape 6.1.7.21880)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET4.0C; .NET4.0E; Lunascape 6.1.7.21880)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.1.7.21880)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.1.5.21576)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 1.1.4322; Lunascape 6.1.5.21576)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.9) Gecko/20100331 Firefox/3.5.9 Lunascape/6.1.4.21478 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.9) Gecko/20100331 Firefox/3.5.9 Lunascape/6.1.4.21478",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.8) Gecko/20100223 Firefox/3.5.8 Lunascape/6.1.0.20995",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs-CZ) AppleWebKit/533.3 (KHTML, like Gecko) Lunascape/6.1.0.20995 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.8) Gecko/20100223 Firefox/3.5.8 Lunascape/6.1.0.20940 ( .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Lunascape 6.0.3.20663)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 6.0.1.20094)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.1.3) Gecko/20090804 Firefox/3.5.3 Lunascape/5.1.5.19059",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 3.5.30729; .NET CLR 3.0.30618; Lunascape 5.1.5.19059)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; OfficeLiveConnector.1.3; OfficeLivePatch.1.3; Lunascape 5.1.4.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1; Lunascape 5.1.4.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; MathPlayer 2.10b; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 1.0.3705; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape ",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.1) Gecko/20090721 Firefox/3.5.1 Lunascape/5.1.3.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; InfoPath.2; .NET CLR 3.5.30729; .NET CLR 3.0.30618; Lunascape 5.1.3.4)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; OfficeLiveConnector.1.4; OfficeLivePatch.1.3; .NET CLR 3.0.30729; Lunascape 5.1.3.4)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1) Gecko/20090701 Firefox/3.5 Lunascape/5.1.2.3",
	"Mozilla/5.0 (Windows; U; ; cs-CZ) AppleWebKit/532+ (KHTML, like Gecko, Safari/532.0) Lunascape/5.1.2.3",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.1; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Lunascape 5.1.2.3)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja-JP) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.2.0",
	"Mozilla/5.0 (Windows; U; ; cs-CZ) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1) Gecko/20090701 Firefox/3.5 Lunascape/5.1.1.2",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; GTB6; SLCC1; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 1.1.4322; .NET CLR 3.5.21022; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Lunascape 5.1.1.2)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1) Gecko/20090701 Firefox/3.5 Lunascape/5.1.1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja-JP) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs-CZ) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.1.0",
	"Mozilla/5.0 (Windows; U; ; cs-CZ) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1pre) Gecko/20090516 Firefox/3.5pre Lunascape/5.1.0.1",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; Lunascape 5.1.0.1)",
	"Mozilla/5.0 (Windows; U; ; cs-CZ) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.1.0.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b4pre) Gecko/20090312 Firefox/3.1b4pre Lunascape/5.0.5.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; WOW64; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; .NET CLR 3.0.30729; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB5; SLCC1; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; OfficeLiveConnector.1.3; OfficeLivePatch.1.3; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; GTB6; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; Lunascape 5.0.5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; .NET CLR 3.0.30729; Lunascape 5.0.4.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 5.0.4.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 3.0.4506.2152; .NET CLR 2.0.50727; Lunascape 5.0.4.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Lunascape 5.0.4.0)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja-JP) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.0.3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b4pre) Gecko/20090312 Firefox/3.1b4pre Lunascape/5.0.3.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Lunascape 5.0.3.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; Lunascape 5.0.3.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 5.0.3.0)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528+ (KHTML, like Gecko, Safari/528.0) Lunascape/5.0.2.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; Lunascape 5.0 alpha3)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; Lunascape 5.0 alpha3)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Lunascape 5.0 alpha3)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Lunascape 5.0 alpha3)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; Lunascape 5.0 alpha3)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; Tablet PC 2.0; Lunascape 5.0 alpha2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 2.0.50727; Lunascape 5.0 alpha1)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.99",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Lunascape 4.9.9.98)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Lunascape 4.9.9.97)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.96",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ja-JP) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.96",
	"Mozilla/5.0 (Windows; N; Windows NT 5.2; ru-RU) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.94",
	"Mozilla/5.0 (Windows; N; Windows NT 5.1; id-ID) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.94",
	"Mozilla/5.0 (Windows; N; Windows NT 5.1; hu-HU) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.94",
	"Mozilla/5.0 (Windows; N; Windows NT 5.1; en-US) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.94",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/529 (KHTML, like Gecko, Safari/529.0) Lunascape/4.9.9.100",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; Lunascape 4.8.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; Lunascape 4.7.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Lunascape 4.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98; Q312461; .NET CLR 1.1.4322; Lunascape 4.0.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727; Lunascape 3.0.4)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 1.1.4322; Lunascape 2.1.3)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en; rv:1.7.12) Gecko/20050928 Firefox/1.0.7 Madfox/3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.7.12) Gecko/20051001 Firefox/1.0.7 Madfox/0.3.2u3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.7) Gecko/20050503 Firefox/1.0.3 Madfox/0.3.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.1 (KHTML, like Gecko) Maxthon/3.0.8.2 Safari/533.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532.4 (KHTML, like Gecko) Maxthon/3.0.6.27 Safari/532.4",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.0; Maxthon/3.0)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/4.0; WOW64; Trident/5.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Zune 4.0; InfoPath.3; MS-RTC LM 8; .NET4.0C; .NET4.0E; Maxthon 2.0)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/4.0; WOW64; Trident/5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET CLR 2.0.50727; Media Center PC 6.0; Maxthon 2.0)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.2; Trident/4.0; Trident/4.0; Media Center PC 4.0; SLCC1; .NET CLR 3.0.04320; Maxthon 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; chromeframe; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; .NET4.0C; Maxthon 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; Win64; x64; Trident/4.0; .NET CLR 2.0.50727; SLCC1; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; Maxthon 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; SLCC2; Maxthon 2.0; DigExt; Zune 4.7)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.2; Trident/4.0; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Trident/4.0; Zango 10.1.181.0; Maxthon 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.1.4322; .NET CLR 2.0.50727; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET4.0C; .NET4.0E; Maxthon 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Maxthon 2; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 1.0.3705; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; InfoPath.1; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; MAXTHON 2.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; Maxthon; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Maxthon; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Maxthon; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.21022; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; GTB6; Maxthon; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.21022; .NET CLR 3.0.30618; .NET CLR 1.1.4322; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.2; Maxthon; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Maxthon; Maxthon; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Maxthon; Maxthon; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 3.5.30729; FDM)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux i686; fr-fr) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori/1.19",
	"Mozilla/5.0 (X11; Linux; rv:2.0.1) Gecko/20100101 Firefox/4.0.1 Midori/0.4",
	"Mozilla/5.0 (X11; U; Linux; pt-br) AppleWebKit/531+ (KHTML, like Gecko) Safari/531.2+ Midori/0.3",
	"Mozilla/5.0 (X11; U; Linux i686; pt-br; rv:1.8.1) Gecko/20061010 Firefox/2.0 Midori/0.2.0",
	"Mozilla/5.0 (X11; U; Linux; it-it) AppleWebKit/531+ (KHTML, like Gecko) Safari/531.2+ Midori/0.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-us) AppleWebKit/532+ (KHTML, like Gecko) Safari/419.3 Midori/0.1.8",
	"Mozilla/5.0 (X11; U; Linux; fr-fr) AppleWebKit/532+ (KHTML, like Gecko) Safari/419.3 Midori/0.1.7",
	"Mozilla/5.0 (X11; U; Linux; it-IT; rv:1.9.1) Gecko/20080913 Firefox/2.0 Midori/0.1.6",
	"Mozilla/5.0 (X11; U; Linux; en-us; rv:1.8.1) Gecko/20061010 Firefox/2.0 Midori/0.1.6",
	"Mozilla/5.0 (X11; U; Linux i686; de) AppleWebKit/523+ midori/0.1",
	"Mozilla/5.0 (X11; U; Linux i686; de) AppleWebKit/523+ (KHTML like Gecko) midori/0.1",
	"Mozilla/5.0 (X11; U; Linux i686; de) AppleWebKit/523 midori/0.1",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.8.1.9) Gecko/20071103 Midori/0.0.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru-ru) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; it-it) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; hu-hu) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-fr) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-us) AppleWebKit/528.5+ (KHTML, like Gecko, Safari/528.5+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-us) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-gb) AppleWebKit/528.5+ (KHTML, like Gecko, Safari/528.5+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-gb) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; de-de) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux x86_64; de-at) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; zh-tw) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; zh-cn) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; sv-se) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; ru-ru) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; pt-pt) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; pl-pl) AppleWebKit/528.5+ (KHTML, like Gecko, Safari/528.5+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; pl-pl) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; nl-nl) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; ja-jp) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (X11; U; Linux i686; hu-hu) AppleWebKit/525.1+ (KHTML, like Gecko, Safari/525.1+) midori",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0b4pre) Gecko/20100815 Minefield/4.0b4pre",
	"Mozilla/5.0 (X11; Linux x86_64; en-US; rv:2.0b2pre) Gecko/20100712 Minefield/4.0b2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:2.0a1pre) Gecko/2008060602 Minefield/4.0a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:2.0a1pre) Gecko/2008032002 Minefield/4.0a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:2.0a1pre) Gecko/2008032902 Minefield/4.0a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.3a5pre) Gecko/20100527 Minefield/3.7a5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja; rv:1.9.3a5pre) Gecko/20100605 Minefield/3.7a5pre ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.3a5pre) Gecko/20100418 Minefield/3.7a5pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.9.3a4pre) Gecko/20100319 Minefield/3.7a4pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.3a4pre) Gecko/20100318 Minefield/3.7a4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru; rv:1.9.3a4pre) Gecko/20100402 Minefield/3.7a4pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.3a4pre) Gecko/20100318 Minefield/3.7a4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.3a3pre) Gecko/20100313 Minefield/3.7a3pre (.NET CLR 4.0.20506)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.3a3pre) Gecko/20100306 Minefield/3.7a3pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.3a3pre) Gecko/20100305 Minefield/3.7a3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.3a3pre) Gecko/20100312 Minefield/3.7a3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.3a3pre) Gecko/20100311 Minefield/3.7a3pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.3a3pre) Gecko/20100306 Minefield/3.7a3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.3a1pre) Gecko/20091022 Minefield/3.7a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.3a1pre) Gecko/20090829 Minefield/3.7a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.3a1pre) Gecko/20091013 Minefield/3.7a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.3a1pre) Gecko/20091118 Minefield/3.7a1pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.3a1pre) Gecko/20090829 Minefield/3.7a1pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.3a1pre) Gecko/20091219 Minefield/3.7a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.3a1pre) Gecko/20091130 Minefield/3.7a1pre GTB6",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.3a1pre) Gecko/20100103 Minefield/3.7a1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.3a1pre) Gecko/20091002 Minefield/3.7a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20090716 Minefield/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20090501 Minefield/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20090418 Minefield/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20090417 Minefield/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20090331 Minefield/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2a1pre) Gecko/20090403 Minefield/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2a1pre) Gecko/20090327 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090707 Minefield/3.6a1pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090420 Minefield/3.6a1pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090411 Minefield/3.6a1pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090407 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090401 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090324 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090709 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090425 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090424 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090418 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090415 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090413 Minefield/3.6a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090410 Minefield/3.6a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a1pre) Gecko/20081205 Minefield/3.2a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.2a1pre) Gecko/20090330 Kubuntu/8.10 (intrepid) Minefield/3.2a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.2a1pre) Gecko/20090128 Kubuntu/8.10 (intrepid) Minefield/3.2a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2a1pre) Gecko/20090102 Ubuntu/9.04 (jaunty) Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2a1pre) Gecko/20090316 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090306 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090226 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090210 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1pre) Gecko/20090207 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090306 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090304 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090219 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090120 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1pre) Gecko/20090113 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.2a1pre) Gecko/20090117 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows CE 6.0; en-US; rv:1.9.2a1pre) Gecko/20090219 Minefield/3.2a1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.2a1pre) Gecko/20090315 Minefield/3.2a1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.2a1pre) Gecko/20090302 Minefield/3.2a1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.2a1pre) Gecko/20090225 Minefield/3.2a1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.2a1pre) Gecko/20090224 Minefield/3.2a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20081201 Minefield/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b2pre) Gecko/20081115 Minefield/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b2pre) Gecko/20081011 Minefield/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0 x64; en-US; rv:1.9.1b2pre) Gecko/20081026 Minefield/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1b2pre) Gecko/20081110 Minefield/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20090206 Minefield/3.1b2pre Firefox/3.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b2pre) Gecko/20081031 Minefield/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b2pre) Gecko/20081026 Minefield/3.1b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b2pre) Gecko/20081020 Minefield/3.1b2pre",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en-US; rv:1.9.1b2pre) Gecko/20081027 Minefield/3.1b2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b2) Gecko/20090128 Fedora/3.1-0.4.beta2.fc11 Minefield/3.1b2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b1pre) Gecko/20080929020931 Minefield/3.1b1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b1pre) Gecko/20080930020755 Minefield/3.1b1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b1pre) Gecko/20080916020338 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b1pre) Gecko/20081001 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b1pre) Gecko/20080930093007 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b1pre) Gecko/20080927033433 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b1pre) Gecko/20080926033937 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b1pre) Gecko/20080920085411 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b1pre) Gecko/20080913185648 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b1pre) Gecko/20080904053130 Minefield/3.1b1pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.1b1pre) Gecko/20080908170408 Minefield/3.1b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061102 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060910 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060826 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060816 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060809 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060725 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2 x64; en-US; rv:1.9.0.7) Gecko/2009030713 Minefield/3.0.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6.0; en-US; rv:1.9.0.7) Gecko/2009030517 Minefield/3.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009030302 Minefield/3.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0 x64; en-US; rv:1.9.0.3pre) Gecko/2008111500 Minefield/3.0.5pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.4) Gecko/2008112012 Minefield/3.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.1) Gecko/2008071910 Minefield/3.0.1",
	"Mozilla/5.0 (X11; U; Linux armv7l; en-US; rv:1.9.0.1) Gecko/2009010915 Minefield/3.0.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.6) Gecko/2009020911 Ubuntu/8.10 (intrepid) Minefield/3.0 MEGAUPLOAD 2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9) Gecko/2008071513 Minefield/3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; rv:2.2) Gecko/20110201",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; it; rv:2.0b4) Gecko/20100818",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a3pre) Gecko/20070330",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.2a1pre) Gecko",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.9.2.3) Gecko/20100401 Lightningquail/3.6.3",
	"Mozilla/5.0 (X11; ; Linux i686; rv:1.9.2.20) Gecko/20110805",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.2.13; ) Gecko/20101203",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b3) Gecko/20090305",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.9.0.9) Gecko/2009040821",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:1.9.0.8) Gecko/2009032711",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.7) Gecko/2009032803",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-GB; rv:1.9.0.7) Gecko/2009021910 MEGAUPLOAD 1.0",
	"Mozilla/5.0 (Windows; U; BeOS; en-US; rv:1.9.0.7) Gecko/2009021910",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.9.0.6) Gecko/2009020911",
	"Mozilla/5.0 (X11; U; Linux i686; en; rv:1.9.0.6) Gecko/20080528",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.6) Gecko/2009020409",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.3) Gecko/2008092814 (Debian-3.0.1-1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.3) Gecko/2008092816",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.3) Gecko/2008090713",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.2) Gecko Fedora/1.9.0.2-1.fc9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.14) Gecko/2009091010",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.10) Gecko/2009042523",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.1) Gecko/2008072610",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.1) Gecko/2008072820 Ubuntu/8.04 (hardy) (Linux Mint)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.0.1) Gecko",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.0.1) Gecko/2008070206",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-au; rv:1.9.0.1) Gecko/2008070206",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9) Gecko",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs; rv:1.9) Gecko/2008052906",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8b2) Gecko/20050702",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8b) Gecko/20050217",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8b) Gecko/20050217",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8b) Gecko/20050217",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8a6) Gecko/20050111",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8a5) Gecko/20041122",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8a4) Gecko/20040927",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8a4) Gecko/20040927",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8a3) Gecko/20040817",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8a1) Gecko/20040520",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8a1) Gecko/20040520",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1a2) Gecko/20060512",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8) Gecko/20071022",
	"Mozilla/5.001 (X11; U; Linux i686; rv:1.8.1.6; de-ch) Gecko/25250101 (ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070531",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20070508 (Debian-1.8.1.4-2ubuntu5)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.4) Gecko/20061201 Mozilla/5.0 (Linux Mint)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.3) Gecko/20061201 MEGAUPLOAD 1.0 (Ubuntu-feisty)",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.8.1.3) Gecko/20070310",
	"Mozilla/5.0 (X11; ; Linux i686; en-US; rv:1.8.1.3) Gecko",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.3) Gecko/20070321",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.3) Gecko/20070309 Mozilla/4.8 [en] (Windows NT 5.1; U)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; nl-NL; rv:1.8.1.3) Gecko/20080722",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en; rv:1.8.1.2pre) Gecko/20070223",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070208",
	"Mozilla/5.0 (compatible; Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2) Gecko/20070219",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.18) Gecko/20081029",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.16) Gecko/20080702",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.8.1.15) Gecko/20080620 Mozilla/4.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080313",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.1.12) Gecko/20080201",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en; rv:1.8.1.11) Gecko/20071127 Mozilla",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.11) Gecko/20071213",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.11) Gecko/20071206",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.11) Gecko/20071127",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.1) Gecko/20061205 Mozilla/5.0 (Debian-2.0.0.1+dfsg-2)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.8.1.1) Gecko/20061204",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.1) Gecko/20061204",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.1) Gecko/20061204",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.9) Gecko/20061206",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.6) Gecko/20060728",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.5) Gecko/20060719 KHTML/3.5.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060912 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060508",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.14eol) Gecko/20070505 (Debian-1.8.0.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060126",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.8) Gecko/20051111",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.7b) Gecko/20040429",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7b) Gecko/20040421",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7b) Gecko/20040316",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7b) Gecko/20040421",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.9) Gecko/20050711",
	"Mozilla/5.0 (X11; U; Linux x86_64; de-AT; rv:1.7.8) Gecko/20050513 Debian/1.7.8-1",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.8) Gecko/20061113 Debian/1.7.8-1sarge8",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.8) Gecko/20060904 Debian/1.7.8-1sarge7.2.2",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.8) Gecko/20060628 Debian/1.7.8-1sarge7.1",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.8) Gecko/20050927 Debian/1.7.8-1sarge3",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.7.8) Gecko/20050831 Debian/1.7.8-1sarge2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050927 Debian/1.7.8-1sarge3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050921",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050831 Debian/1.7.8-1sarge2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050610",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050513 Debian/1.7.8-1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050512 Red Hat/1.7.8-1.1.3.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.8) Gecko/20050511",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.7.8) Gecko/20050511",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.8) Gecko/20050511",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.8) Gecko/20050511",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr; rv:1.7.8) Gecko/20050511",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.8) Gecko/20050511",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7.8) Gecko/20050511 (No IDN)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7.8) Gecko/20050511",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.7) Gecko/20050421",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.7) Gecko/20050427 Red Hat/1.7.7-1.1.3.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.7) Gecko/20050420 Debian/1.7.7-2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.7) Gecko/20050414",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.7) Gecko/20050415",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.7.7) Gecko/20050414",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.7) Gecko/20050414",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.7) Gecko/20050414",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.7) Gecko/20050414",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7.7) Gecko/20050414",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050328 Fedora/1.7.6-1.2.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.6) Gecko/20050319",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU; rv:1.7.6) Gecko/20050319",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.7.6) Gecko/20050319",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6) Gecko/20050319",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6) Gecko/20050225",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.6) Gecko/20050319",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7.6) Gecko/20050319",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.6) Gecko/20050319",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.5) Gecko/20041221",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041221",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20041013",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.9.0.6) Gecko/2009011913 Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.5) Gecko/20041221",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20041217",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.5) Gecko/20041217",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20041217",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; cs-CZ; rv:1.7.5) Gecko/20041217",
	"Mozilla/5.0 (Windows NT 5.1; U; pt-br; rv:1.7.5) Gecko/20041110",
	"Mozilla/5.0 (Windows NT 5.1; U; es-es; rv:1.7.5) Gecko/20041110",
	"Mozilla/5.0 (Windows NT 5.1; U; en; rv:1.7.5) Gecko/20041110",
	"Mozilla/5.0 (Windows NT 5.1; U; de; rv:1.7.5) Gecko/20041110",
	"Mozilla/5.0 (OS/2; U; Warp 4.5; de-DE; rv:1.7.5) Gecko/20050523",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X; U; nb; rv:1.7.5) Gecko/20041110",
	"Mozilla/5.0 (X11; U; Linux i686; hu; rv:1.7.3) Gecko/20050130",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.7.3) Gecko/20040913",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.3) Gecko/20041007 Debian/1.7.3-5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.3) Gecko/20040913",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Win98; fr; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Windows; U; Win98; de-AT; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; fr; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; es-ES; rv:1.7.3) Gecko/20040910",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.7.2) Gecko/20040804",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.2) Gecko/20040906",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.2) Gecko/20040804",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.2) Gecko/20040803",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.2) Gecko/20040906",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.2) Gecko/20040810 Debian/1.7.2-2",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.2) Gecko/20040804",
	"Mozilla/5.0 (X11; U; FreeBSD i386; ja-JP; rv:1.7.2) Gecko/20050330",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.7.2) Gecko/20040709",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.7.2) Gecko/20040803",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.2) Gecko/20040804",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.2) Gecko/20040803",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.2) Gecko/20040803",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-AT; rv:1.7.2) Gecko/20040803",
	"Mozilla/5.0 (Windows; ; Windows NT 5.1; rv:1.7.2) Gecko/20040804",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.2) Gecko/20040803",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.13) Gecko/20060509",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.7.13) Gecko/20060901",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.13) Gecko/20060717 Debian/1.7.13-0.2ubuntu1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.13) Gecko/20060427 Debian/1.7.13-0ubuntu05.04",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.13) Gecko/20060417",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.7.13) Gecko/20060417",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.7.13) Gecko/20061230",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.7.13) Gecko/20060414",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.13) Gecko/20060414",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7.13) Gecko/20060414",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.13) Gecko/20060414",
	"Mozilla/4.0 (compatible; Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.13) Gecko/20060414; Windows NT 5.1)",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.7.12) Gecko/20050929",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20060607 Debian/1.7.12-1.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20060216 Debian/1.7.12-1.1ubuntu2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20060205 Debian/1.7.12-1.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20060202 Fedora/1.7.12-1.5.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051203",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051013 Debian/1.7.12-1ubuntu1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051010 Debian/1.7.12-0ubuntu2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20051007 Debian/1.7.12-1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20050926",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20050923",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20050921 Red Hat/1.7.12-1.1.3.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20050921",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.12) Gecko/20050920",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.12) Gecko/20060205 Debian/1.7.12-1.1",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.12) Gecko/20050923 Fedora/1.7.12-1.5.1",
	"Mozilla/5.0 (X11; U; Linux i686; cs-CZ; rv:1.7.12) Gecko/20050929",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); fr; rv:1.7.12) Gecko/20051010 Debian/1.7.12-0ubuntu2",
	"Mozilla/5.0 (X11; U; AIX 5.3; en-US; rv:1.7.12) Gecko/20051025",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.7.12) Gecko/20050915",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7.11) Gecko/20050802",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.11) Gecko/20050729",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-AT; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.11) Gecko/20050728 (No IDN)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Windows; U; Win95; de-AT; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-AT; rv:1.7.11) Gecko/20050728",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; fr-FR; rv:1.7.11) Gecko/20050727",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.10) Gecko/20050811 Fedora/1.7.10-1.2.1.legacy",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.10) Gecko/20050727",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.7.10) Gecko/20050722",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.10) Gecko/20050716",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.7.1) Gecko/20040707",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.1) Gecko/20040707",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7.1) Gecko/20040707",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.7.1) Gecko/20040707",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.1) Gecko/20040707",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.7.1) Gecko/20040707",
	"Mozilla/5.0 (X11; U; OpenBSD i386; en-US; rv:1.7.0.13) Gecko/20060901",
	"Mozilla/5.0 (X11; U; SunOS sun4v; en-US; rv:1.7) Gecko/20060120",
	"Mozilla/5.0 (X11; U; SunOS sun4u; fr-FR; rv:1.7) Gecko/20040621",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7) Gecko/20060629",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.7) Gecko/20060120",
	"Mozilla/5.0 (X11; U; SunOS sun4u; de-DE; rv:1.7) Gecko/20070606",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.7) Gecko/20060627",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.7) Gecko/20051122",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.7) Gecko/20051027",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.7) Gecko/20050502",
	"Mozilla/5.0 (X11; U; SunOS i86pc; en-US; rv:1.7) Gecko/20041221",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7) Gecko/20040514",
	"Mozilla/5.0 (X11; U; FreeBSD; i386; it-IT; rv:1.7) Gecko",
	"Mozilla/5.0 (X11; U; FreeBSD; i386; en-US; rv:1.7) Gecko",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.7) Gecko/20040616",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.7) Gecko/20040803 Firefox/0.9.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7) Gecko/20040616",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7) Gecko/20040514",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.7) Gecko/20040616",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7) Gecko/20040616",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.7) Gecko/20040616",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.6a) Gecko/20031030",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.6) Gecko/20040115",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.6) Gecko/20040413 Debian/1.6-5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.6) Gecko/20040114",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.6) Gecko/20040115",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.6) Gecko/20040114",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hu; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (Photon; U; QNX x86pc; en-US; rv:1.6) Gecko/20040429",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.6) Gecko/20040113",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5b) Gecko/20030827",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.5b) Gecko/20030827",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5a) Gecko/20030718",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.5a) Gecko/20030718",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.5.1) Gecko/20031120",
	"Mozilla/5.0 (X11; U; SunOS5.10 sun4u; ja-JP; rv:1.5) Gecko/20031022",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5) Gecko/20030916",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.5) Gecko/20030916",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (Windows; U; Win98; de-AT; rv:1.5) Gecko/20031007",
	"Mozilla/5.0 (Windows; U; WinNT4.0; it-IT; rv:1.4b) Gecko/20030507",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.4b) Gecko/20030507",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4b) Gecko/20030507",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4b) Gecko/20030427",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4b) Gecko/20030507",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.4a) Gecko/20030318",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4a) Gecko/20030401",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.4a) Gecko/20030401",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4a) Gecko/20030401",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.4a) Gecko/20030401",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4a) Gecko/20030401",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.4a) Gecko/20030401",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.4a) Gecko/20030401",
	"Mozilla/5.0 (X11; U; IRIX64 IP35; en-US; rv:1.4.3) Gecko/20040909",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4.2) Gecko/20040220",
	"Mozilla/5.0 (X11; U; Linux i686; zh-CN; rv:1.4.1) Gecko/20031114",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4.1) Gecko/20040406",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4.1) Gecko/20031114",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4.1) Gecko/20031008",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.4) Gecko/20041224",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.4) Gecko/20030714 Debian/1.4-2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030908 Debian/1.4-4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030828",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030827 Debian/1.4-3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030821",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030818",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030723",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.4) Gecko/20030908 Debian/1.4-4",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.4) Gecko/20030812",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.4) Gecko/20030624",
	"Mozilla/5.0 (X11; U; Linux i586; de-AT; rv:1.4) Gecko/20030908 Debian/1.4-4",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.4) Gecko/20030624",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4) Gecko/20030624",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4) Gecko/20030529",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.4) Gecko/20030624",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.4) Gecko/20030624",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4) Gecko/20030624",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4) Gecko/20030612",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4) Gecko/20030529",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3b) Gecko/20030210",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3b) Gecko/20030125",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3b) Gecko/20021213",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.3b) Gecko/20030210",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.3b) Gecko/20030210",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.3b) Gecko/20030210",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3b) Gecko/20030210",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3b) Gecko/20030204",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.3b) Gecko/20030210",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.3b) Gecko/20030210",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.3a) Gecko/20021212",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.3a) Gecko/20021212",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.3a) Gecko/20021212",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3a) Gecko/20021212",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.3a) Gecko/20021212",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.3a) Gecko/20021212",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.3.1) Gecko/20030509",
	"Mozilla/5.0 (X11; U; Linux i686; hu-HU; rv:1.3.1) Gecko",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3.1) Gecko/20030428",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3.1) Gecko/20030425",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-AT; rv:1.3.1) Gecko/20030425",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.3.1) Gecko/20030425",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.3.1) Gecko/20030425",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3.1) Gecko/20030425",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.3.1) Gecko/20030425",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.3.1) Gecko/20030425",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.3) Gecko/20030318",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030523",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030413",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030401",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030327 Debian/1.3-4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030326",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030320",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030314",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030313",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.3) Gecko/20030430 Debian/1.3-5",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.3) Gecko/20030327 Debian/1.3-4",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (X11; U; HP-UX 9000/785; en-US; rv:1.3) Gecko/20030321",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.3) Gecko/20030312",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.2b) Gecko/20021016",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:1.2a) Gecko/20020910",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.2.1) Gecko/20030711",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.2.1) Gecko/20021217",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.2.1) Gecko/20021212",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.2.1) Gecko/20021205",
	"Mozilla/5.0 (X11; U; Linux i686;en-US; rv:1.2.1) Gecko/20030225",
	"Mozilla/5.0 (X11; U; Linux i686; zh-CN; rv:1.2.1) Gecko/20030225",
	"Mozilla/5.0 (X11; U; Linux i686; es-AR; rv:1.2.1) Gecko/20021130",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20030427",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20030409 Debian/1.2.1-9woody2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20030225",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20030113",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20021213 Debian/1.2.1-2.bunk",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20021208 Debian/1.2.1-2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20021204",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20021203",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2.1) Gecko/20021130",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.2.1) Gecko/20021226 Debian/1.2.1-9",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.2.1) Gecko/20021204",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.2.1) Gecko/20021130",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.2.1) Gecko/20021204",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2) Gecko/20021202",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2) Gecko/20021126",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.2) Gecko/20021203",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.2) Gecko/20050223",
	"Mozilla/5.0 (X11; U; HP-UX 9000/785; en-US; rv:1.2) Gecko/20021203",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.2) Gecko/20021126",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-AT; rv:1.2) Gecko/20021126",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.2) Gecko/20021126",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2) Gecko/20021126",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.2) Gecko/20021126",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US; rv:1.2) Gecko/20021126",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.1b) Gecko/20020722",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.1b) Gecko/20020721",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.1b) Gecko/20020721",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.1a) Gecko/20020610",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.1a) Gecko/20020611",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.1a) Gecko/20020611",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.1a) Gecko/20020611",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US; rv:1.1a) Gecko/20020610",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.1a) Gecko/20020611",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.1a) Gecko/20020611",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.1) Gecko/20020925",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.1) Gecko/20020909",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.1) Gecko/20020827",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.1) Gecko/20020927",
	"Mozilla/5.0 (X11; U; Linux i686; it-IT; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.1) Gecko/20020913 Debian/1.1-1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.1) Gecko/20020829",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.1) Gecko/20020828",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.1) Gecko/20020913 Debian/1.1-1",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; Linux i386; en-US; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.1) Gecko/20021223",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-AT; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0rc3) Gecko/20020529 Debian/1.0rc3-1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0rc3) Gecko/20020523",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.0rc3) Gecko/20020523",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0rc2) Gecko/20020510",
	"Mozilla/5.0 (X11; U; AIX 005A471A4C00; en-US; rv:1.0rc2) Gecko/20020514",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0rc2) Gecko/20020510",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.0rc2) Gecko/20020510",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.0rc2) Gecko/20020510",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0rc2) Gecko/20020510",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.0rc2) Gecko/20020510",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0rc1) Gecko/20020417",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.2) Gecko/20030716",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.0.2) Gecko/20030208",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.2) Gecko/20021216",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:1.0.2) Gecko/20021216",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20021203",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20021122 Debian/1.0.1-2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20021110",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20021003",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020919",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020918",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020912",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020903",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020830",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0.1) Gecko/20020815",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020903",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.1) Gecko/20020830",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0.1) Gecko/20020826",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.0.0) Gecko/20020611",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.0.0) Gecko/20020622 Debian/1.0.0-0.woody.1",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.0.0) Gecko/20020623 Debian/1.0.0-0.woody.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.0) Gecko/20021004",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.0) Gecko/20020623 Debian/1.0.0-0.woody.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.0) Gecko/20020612",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.0) Gecko/20020605",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.0) Gecko/20020529",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.0.0) Gecko/20020615 Debian/1.0.0-3",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.0.0) Gecko/20020623 Debian/1.0.0-0.woody.1",
	"Mozilla/5.0 (X11; U; HP-UX 9000/785; en-US; rv:1.0.0) Gecko/20020605",
	"Mozilla/5.0 (Windows; U; WinNT4.0; fr-FR; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.0) Gecko/20020509",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.0.0) Gecko/20020530",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.9) Gecko/20020513",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.9) Gecko/20020423",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.9) Gecko/20020408",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.9) Gecko/20020313",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:0.9.9) Gecko/20020513",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:0.9.9) Gecko/20020311",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.8) Gecko/20020204",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:0.9.8) Gecko/20020204",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:0.9.8) Gecko/20020204",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.8) Gecko/20020204",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.7) Gecko/20011221",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.6) Gecko/20011202",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:0.9.5) Gecko/20011011",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.5) Gecko/20011011",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.4) Gecko/20010923",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.3) Gecko/20010801",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:0.9.3) Gecko/20010802",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.2.1) Gecko/20010901",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.2.1) Gecko/20010901",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.2) Gecko/20010809",
	"Mozilla/5.001 (Macintosh; N; PPC; ja) Gecko/25250101",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; cs; rv:1.9.2.6) Gecko/20100628 myibrow/4alpha2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; WOW64; cs; rv:1.9.2.6) Gecko/20100723 myibrow/4.0.0.0 (Firefox/3.6 compatible)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; MyIE2; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; MyIE2; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; SV1; MyIE2; .NET CLR 1.1.4322; .NET CLR 2.0.50727; WinFX RunTime 3.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; MyIE2; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; MyIE2; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; MyIE2; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; MyIE2; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.2; MyIE2; .NET CLR 1.1.4322; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.2; MyIE2; .NET CLR 1.1.4322; Alexa Toolbar; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.2; MyIE2; .NET CLR 1.1.4322; .NET CLR 1.0.3705)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; MyIE2; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; MyIE2; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; MyIE2; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; MyIE2; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; MyIE2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Q312461; MyIE2; YComp 5.0.2.6)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; Q312461; MyIE2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; MyIE2; YComp 5.0.2.6)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; MyIE2; SV1; iebar)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; MyIE2; SV1; Alexa Toolbar)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a2pre) Gecko/20090908 Ubuntu/9.04 (jaunty) Namoroka/3.6a2pre GTB5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a2pre) Gecko/20090901 Ubuntu/9.10 (karmic) Namoroka/3.6a2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a2pre) Gecko/20090824 Ubuntu/9.10 (karmic) Namoroka/3.6a2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2a2pre) Gecko/20090817 Ubuntu/9.04 (jaunty) Namoroka/3.6a2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ; rv:1.9.2a2pre) Gecko/20090826 Namoroka/3.6a2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2a2pre) Gecko/20090921 Ubuntu/9.04 (jaunty) Namoroka/3.6a2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2a2pre) Gecko/20090906 Ubuntu/9.04 (jaunty) Namoroka/3.6a2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2a2pre) Gecko/20090825 Namoroka/3.6a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2a2pre) Gecko/20090918 Namoroka/3.6a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2a2pre) Gecko/20090917 Namoroka/3.6a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; cs; rv:1.9.2a2pre) Gecko/20090912 Namoroka/3.6a2pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a2pre) Gecko/20090826 Namoroka/3.6a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a2pre) Gecko/20090826 Namoroka/3.6a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a2pre) Gecko/20090816 Namoroka/3.6a2pre",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1 (Debian GNU/Linux Sid)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.2a1) Gecko/20090806 Namoroka/3.6a1",
	"Mozilla/5.0 (X11; U; Linux x86_64; zh-CN; rv:1.9.2.9pre) Gecko/20100811 Ubuntu/10.04 (lucid) Namoroka/3.6.9pre GTB7.0",
	"Mozilla/5.0 (X11; U; Linux x86_64; it; rv:1.9.2.9pre) Gecko/20100818 Ubuntu/10.04 (lucid) Namoroka/3.6.9pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.9pre) Gecko/20100812 Namoroka/3.6.9pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.8) Gecko/20100805 Namoroka/3.6.8",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr; rv:1.9.2.6pre) Gecko/20100604 Namoroka/3.6.6pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.5pre) Gecko/20100526 Ubuntu/10.04 (lucid) Namoroka/3.6.5pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.5pre) Gecko/20100526 Namoroka/3.6.5pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.3pre) Gecko/20100324 Ubuntu/9.04 (jaunty) Namoroka/3.6.3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.3pre) Gecko/20100316 Ubuntu/9.10 (karmic) Namoroka/3.6.3pre",
	"Mozilla/5.0 (X11; U; NetBSD i386; en-US; rv:1.9.2.3) Gecko/20100403 Namoroka/3.6.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.3) Gecko/20100402 Namoroka/3.6.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.3) Gecko/20100405 Namoroka/3.6.3 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.2pre) Gecko/20100310 Ubuntu/9.10 (karmic) Namoroka/3.6.2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.2pre) Gecko/20100306 Namoroka/3.6.2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.2pre) Gecko/20100129 Ubuntu/9.10 (karmic) Namoroka/3.6.2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.2pre) Gecko/20100315 Ubuntu/9.10 (karmic) Namoroka/3.6.2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.2pre) Gecko/20100310 Ubuntu/8.10 (intrepid) Namoroka/3.6.2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.2pre) Gecko/20100129 Ubuntu/9.10 (karmic) Namoroka/3.6.2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.2pre) Gecko/20100312 Namoroka/3.6.2pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9.2.20pre) Gecko/20110718 Namoroka/3.6.20pre ( )",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.19pre) Gecko/20110620 Namoroka/3.6.19pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru; rv:1.9.2.18pre) Gecko/20110419 Ubuntu/10.04 (lucid) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; hu-HU; rv:1.9.2.18pre) Gecko/20110515 Ubuntu/10.10 (maverick) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.18pre) Gecko/20110515 Ubuntu/9.10 (karmic) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.18pre) Gecko/20110515 Ubuntu/10.04 (lucid) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.18pre) Gecko/20110419 Ubuntu/10.10 (maverick) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.2.18pre) Gecko/20110419 Ubuntu/10.04 (lucid) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.18pre) Gecko/20110509 Ubuntu/10.10 (maverick) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.18pre) Gecko/20110419 Ubuntu/9.10 (karmic) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.18pre) Gecko/20110419 Ubuntu/10.10 (maverick) Namoroka/3.6.18pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.18pre) Gecko/20110419 Ubuntu/10.04 (lucid) Namoroka/3.6.18pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9.2.18pre) Gecko/20110610 Namoroka/3.6.18pre ( )",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.2.17pre) Gecko/20110413 Ubuntu/10.04 (lucid) Namoroka/3.6.17pre",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.9.2.17pre) Gecko/20110322 Ubuntu/10.10 (maverick) Namoroka/3.6.17pre",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.9.2.17pre) Gecko/20110404 Ubuntu/10.10 (Maverick) Namoroka/3.6.17pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.17pre) Gecko/20110401 Ubuntu/10.04 (lucid) Namoroka/3.6.17Pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.17pre) Gecko/20110322 Ubuntu/10.10 (maverick) Namoroka/3.6.17pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.2.17) Gecko/20110415 Ubuntu/10.10 (maverick) Namoroka/3.6.17",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.15pre) Gecko/20110130 Ubuntu/10.10 (maverick) Namoroka/3.6.15pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.9.2.15pre) Gecko/20110127 Namoroka/3.6.15pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.2.14pre) Gecko/20101224 Ubuntu/10.04 (lucid) Namoroka/3.6.14pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.14pre) Gecko/20110111 Ubuntu/8.04 (hardy) Namoroka/3.6.14pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN; rv:1.9.2.13) Gecko/20101210 Namoroka/3.6.13 Firefox/3.6.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.12pre) Gecko/20101011 Ubuntu/10.04 (lucid) Namoroka/3.6.12pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.12pre) Gecko/20101010 Ubuntu/10.04 (lucid) Namoroka/3.6.12pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.10pre) Gecko/20100826 Ubuntu/9.04 (jaunty) Namoroka/3.6.10pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.10pre) Gecko/20100828 Namoroka/3.6.10pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.10) Gecko/20100928 Namoroka/3.6.10",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2) Gecko/20100206 Namoroka/3.6",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR) AppleWebKit/533.3 (KHTML, like Gecko) Navscape/Pre-0.2 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/534.8 (KHTML, like Gecko) Navscape/Pre-0.2 Safari/534.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/534.12 (KHTML, like Gecko) NavscapeNavigator/Pre-0.1 Safari/534.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/534.12 (KHTML, like Gecko) Navscape/Pre-0.1 Safari/534.12",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; de-de) AppleWebKit/531.22.7 (KHTML, like Gecko) NetNewsWire/3.2.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; de-de) AppleWebKit/531.21.8 (KHTML, like Gecko) NetNewsWire/3.2.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; de-de) AppleWebKit/525.28.3 (KHTML, like Gecko) NetNewsWire/3.1.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; de-de) AppleWebKit/525.27.1 (KHTML, like Gecko) NetNewsWire/3.1.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; en-us) AppleWebKit/525.18 (KHTML, like Gecko) NetNewsWire/3.1.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko) NetNewsWire/3.0d7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.8 (KHTML, like Gecko) NetNewsWire/2.1.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko) NetNewsWire/2.1.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/417.9 (KHTML, like Gecko) NetNewsWire/2.0.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/417.9 (KHTML, like Gecko) NetNewsWire/2.0.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/417.9 (KHTML, like Gecko) NetNewsWire/2.0",
	"Mozilla/3.0 (compatible; NetPositive/2.2.2; BeOS)",
	"Mozilla/3.0 (compatible; NetPositive/2.2)",
	"Mozilla/3.0 (compatible; NetPositive/2.1.1; BeOS)",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; SG; rv:1.9.2.4) Gecko/20101104 Netscape/9.1.0285",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.8pre) Gecko/20070928 Firefox/2.0.0.7 Navigator/9.0RC1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.8pre) Gecko/20070928 Firefox/2.0.0.7 Navigator/9.0RC1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.8pre) Gecko/20071001 Firefox/2.0.0.7 Navigator/9.0RC1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.7pre) Gecko/20070815 Firefox/2.0.0.6 Navigator/9.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.7pre) Gecko/20070815 Firefox/2.0.0.6 Navigator/9.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.7pre) Gecko/20070815 Firefox/2.0.0.6 Navigator/9.0b3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.7pre) Gecko/20070815 Firefox/2.0.0.6 Navigator/9.0b3",
	"Mozilla/5.0 (Windows; U; Windows 98; en-US; rv:1.8.1.5pre) Gecko/20070710 Firefox/2.0.0.4 Navigator/9.0b2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.5pre) Gecko/20070710 Firefox/2.0.0.4 Navigator/9.0b2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080219 Firefox/2.0.0.12 Navigator/9.0.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.12) Gecko/20080219 Firefox/2.0.0.12 Navigator/9.0.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.12) Gecko/20080219 Firefox/2.0.0.12 Navigator/9.0.0.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.12) Gecko/20080219 Firefox/2.0.0.12 Navigator/9.0.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.11pre) Gecko/20071206 Firefox/2.0.0.11 Navigator/9.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.11pre) Gecko/20071206 Firefox/2.0.0.11 Navigator/9.0.0.5 GTB5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.11pre) Gecko/20071206 Firefox/2.0.0.11 Navigator/9.0.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.11pre) Gecko/20071206 Firefox/2.0.0.11 Navigator/9.0.0.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.11pre) Gecko/20071206 Firefox/2.0.0.11 Navigator/9.0.0.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.11pre) Gecko/20071206 Firefox/2.0.0.11 Navigator/9.0.0.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.10pre) Gecko/20071127 Firefox/2.0.0.10 Navigator/9.0.0.4",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.8.1.9pre) Gecko/20071102 Firefox/2.0.0.9 Navigator/9.0.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.9pre) Gecko/20071102 Firefox/2.0.0.9 Navigator/9.0.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr; rv:1.8.1.9pre) Gecko/20071102 Firefox/2.0.0.9 Navigator/9.0.0.3",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.9pre) Gecko/20071102 Firefox/2.0.0.9 Navigator/9.0.0.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.9pre) Gecko/20071102 Firefox/2.0.0.9 Navigator/9.0.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.8pre) Gecko/20071019 Firefox/2.0.0.8 Navigator/9.0.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.8pre) Gecko/20071019 Firefox/2.0.0.8 Navigator/9.0.0.1",
	"Mozilla/5.0 (Windows; U; Windows 98; en-US; rv:1.8.1.8pre) Gecko/20071019 Firefox/2.0.0.8 Navigator/9.0.0.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.8pre) Gecko/20071019 Firefox/2.0.0.8 Navigator/9.0.0.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.8pre) Gecko/20071019 Firefox/2.0.0.8 Navigator/9.0.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.8pre) Gecko/20071015 Firefox/2.0.0.7 Navigator/9.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20070321 Netscape/9.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.8pre) Gecko/20071015 Firefox/2.0.0.7 Navigator/9.0",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.8.1.8pre) Gecko/20071015 Firefox/2.0.0.7 Navigator/9.0",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.8pre) Gecko/20071015 Firefox/2.0.0.7 Navigator/9.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20070321 Netscape/8.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.7.5) Gecko/20070321 Netscape/8.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.7.5) Gecko/20060912 Netscape/8.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-AR; rv:1.7.5) Gecko/20060912 Netscape/8.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; es-AR; rv:1.7.5) Gecko/20060912 Netscape/8.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20060912 Netscape/8.1.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.5) Gecko/20060127 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT; rv:1.7.5) Gecko/20060127 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.7.5) Gecko/20060127 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20060127 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20060111 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.7.5) Gecko/20060127 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20060127 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.5) Gecko/20060127 Netscape/8.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20051012 Netscape/8.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20051012 Netscape/8.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20050817 Netscape/8.0.3.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20050729 Netscape/8.0.3.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20050729 Netscape/8.0.3.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-CA; rv:1.7.5) Gecko/20050610 Netscape/8.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20050603 Netscape/8.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 4.0; SG; rv:1.7.5) Gecko/20050610 Netscape/8.0.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.5) Gecko/20050603 Netscape/8.0.2",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.7.5) Gecko/20050603 Netscape/8.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20050519 Netscape/8.0.1 FirePHP/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20050519 Netscape/8.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.5) Gecko/20050519 Netscape/8.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.2) Gecko/20050208 Netscape/7.20",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.7.2) Gecko/20040805 Netscape/7.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.2) Gecko/20040804 Netscape/7.2 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.7.2) Gecko/20040804 Netscape/7.2 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.7.2) Gecko/20040804 Netscape/7.2 (ax)",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.2) Gecko/20040804 Netscape/7.2 (ax)",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.7.2) Gecko/20040804 Netscape/7.2 (ax)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; rv:1.7.2) Gecko/20040804 Netscape/7.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7.2) Gecko/20040804 Netscape/7.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4) Gecko/20030624 Netscape/7.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-CA; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.4) Gecko/20030619 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.4) Gecko/20030619 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Win98; ja-JP; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.7.2) Gecko/20040804 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.4) Gecko Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.4) Gecko/20030619 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Win95; en-US; rv:1.4) Gecko/20030624 Netscape/7.1",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.4) Gecko/20030624 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:1.4) Gecko/20030619 Netscape/7.1 (ax)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.4) Gecko/20030624 Netscape/7.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.0.2) Gecko/20030208 Netscape/7.02 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Win98; fr-FR; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Win95; de-DE; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Macintosh; U; PPC; ja-JP; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Macintosh; U; PPC; fr-FR; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Macintosh; U; PPC; de-DE; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-FR; rv:1.0.2) Gecko/20030208 Netscape/7.02",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; WinNT4.0; fr-FR; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-AT; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; ja-JP; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Macintosh; U; PPC; fr-FR; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Macintosh; U; PPC; de-DE; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US; rv:1.0.2) Gecko/20021120 Netscape/7.01",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.0rc2) Gecko/20020513 Netscape/7.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.0rc2) Gecko/20020512 Netscape/7.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0rc2) Gecko/20020618 Netscape/7.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0rc2) Gecko/20020512 Netscape/7.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0rc2) Gecko/20020512 Netscape/7.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.0rc2) Gecko/20020512 Netscape/7.0b1",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:1.0rc2) Gecko/20020512 Netscape/7.0b1",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.0.1) Gecko/20020921 Netscape/7.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.0.1) Gecko/20020920 Netscape/7.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.0.1) Gecko/20020719 Netscape/7.0",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (X11; U; Linux i586; fr-FR; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (X11; U; HP-UX 9000/785; es-ES; rv:1.0.1) Gecko/20020827 Netscape/7.0",
	"Mozilla/5.0 (X11; U; AIX 0048013C4C00; en-US; rv:1.0.1) Gecko/20021009 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; WinNT4.0; fr-FR; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-GB; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.1) Gecko/20020823 Netscape/7.0 (ax)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; pt-BR; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-GB; rv:1.0.1) Gecko/20020823 Netscape/7.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:0.9.4.1) Gecko/20020518 Netscape6/6.2.3",
	"Mozilla/5.0 (X11; U; SunOS sun4u; de-DE; rv:0.9.4.1) Gecko/20020518 Netscape6/6.2.3",
	"Mozilla/5.0 (X11; U; OSF1 alpha; en-US; rv:0.9.4.1) Gecko/20020517 Netscape6/6.2.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; WinNT4.0; fr-FR; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; it-IT; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-GB; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Win98; fr-FR; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:0.9.4.1) Gecko/20020508 Netscape6/6.2.3",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:0.9.4.1) Gecko/20020406 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.0.1) Gecko/20020823 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:0.9.4.1) Gecko/20020318 Netscape6/6.2.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-FR; rv:0.9.4.1) Gecko/20020315 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:0.9.4.1) Gecko/20020314 Netscape6/6.2.2",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:0.9.4) Gecko/20011206 Netscape6/6.2.1",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:0.9.4) Gecko/20011126 Netscape6/6.2.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.4) Gecko/20011126 Netscape6/6.2.1",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:0.9.4) Gecko/20011126 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; fr-FR; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-CA; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Win98; fr-FR; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Win98; en-GB; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; fr-FR; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:0.9.4) Gecko/20011128 Netscape6/6.2.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.4) Gecko/20011022 Netscape6/6.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Win98; fr-FR; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Win95; en-GB; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-GB; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:0.9.4) Gecko/20011019 Netscape6/6.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:0.9.1) Gecko/20010607 Netscape6/6.1b1",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:0.9.2) Gecko/20011002 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; fr-FR; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; it-IT; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-GB; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Win95; de-DE; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-DE; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Macintosh; U; PPC; de-DE; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; WinNT4.0; de-DE; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Macintosh; U; PPC; en-US; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/5.0 (Macintosh; U; PPC; de-DE; rv:0.9.2) Gecko/20010726 Netscape6/6.1",
	"Mozilla/4.8C-SGI [en] (X11; U; IRIX64 6.5 IP27)",
	"Mozilla/4.8 [pl] (Windows NT 5.1; U)",
	"Mozilla/4.8 [nl] (Windows NT 6.0; U)",
	"Mozilla/4.8 [es] (Windows NT 5.1; U)",
	"Mozilla/4.8 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.8 [en] (X11; U; SunOS 5.8 sun4m; Nav)",
	"Mozilla/4.8 [en] (X11; U; Linux 2.6.12-1.1372_FC3 i686; Nav)",
	"Mozilla/4.8 [en] (X11; U; Linux 2.4.20-4GB-athlon i686)",
	"Mozilla/4.8 [en] (X11; U; IRIX64 6.5 IP27)",
	"Mozilla/4.8 [en] (X11; U; HP-UX B.11.00 9000/785)",
	"Mozilla/4.8 [en] (WinNT; U)",
	"Mozilla/4.8 [en] (Windows NT 6.0; U) Paros/3.2.13",
	"Mozilla/4.8 [en] (Windows NT 6.0; U)",
	"Mozilla/4.8 [en] (Windows NT 6.0; en-US; U)",
	"Mozilla/4.8 [en] (Windows NT 5.1; U)",
	"Mozilla/4.8 [en] (Windows NT 5.0; U)",
	"Mozilla/4.8 [en] (Win98; U)",
	"Mozilla/4.8 [en] (Linux; U)",
	"Mozilla/4.8 [en] (FreeBSD; U)",
	"Mozilla/4.8 [en-US] (Windows NT 6.0; U)",
	"Mozilla/4.8 [de] (X11; U; Linux 2.4.20-4GB i686)",
	"Mozilla/4.79C-SGI [en] (X11; I; IRIX64 6.5 IP30)",
	"Mozilla/4.79C-SGI [en] (X11; I; IRIX64 6.5 IP28)",
	"Mozilla/4.79 [fr] (X11; U; Linux 2.4.18-4 i686)",
	"Mozilla/4.79 [fr] (X11; U; Linux 2.4.18-27.7.xcustom i686)",
	"Mozilla/4.79 [fr] (X11; U; Linux 2.4.18-24.7.xcustom i686)",
	"Mozilla/4.79 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.79 [en] (X11; U; SunOS 5.7 sun4u)",
	"Mozilla/4.79 [en] (X11; U; SunOS 5.6 sun4u)",
	"Mozilla/4.79 [en] (X11; U; SunOS 5.10 i86pc)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.21-pre5 i686)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.20-4GB i586)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.2 i386)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.18-5smp i686)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.18-5 i686)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.18-27.7.xsmp i686)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.18-10 i686)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.4.16-4GB-SMP i686)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.2.19-6.2.16 i686)",
	"Mozilla/4.79 [en] (X11; U; Linux 2.2.12-32 i686)",
	"Mozilla/4.79 [en] (WinNT; U)",
	"Mozilla/4.79 [en] (Windows NT 5.0; U)",
	"Mozilla/4.79 [en] (Win98; U)",
	"Mozilla/4.78 [ja] (Windows NT 5.0; U)",
	"Mozilla/4.78 [fr] (X11; U; Linux 2.4.8-26mdk i686)",
	"Mozilla/4.78 [fr] (X11; U; Linux 2.4.7-10.2 i686)",
	"Mozilla/4.78 [fr] (X11; U; Linux 2.4.7-10 i686)",
	"Mozilla/4.78 [fr] (X11; U; Linux 2.4.18-14 i686)",
	"Mozilla/4.78 [fr] (Windows NT 5.0; U)",
	"Mozilla/4.78 [fr] (Win98; U)",
	"Mozilla/4.78 [fr] (Win95; U)",
	"Mozilla/4.78 [es] (Windows NT 5.0; U)",
	"Mozilla/4.78 [es] (Win98; U)",
	"Mozilla/4.78 [en] (X11; U; SunOS 5.9 sun4u)",
	"Mozilla/4.78 [en] (X11; U; SunOS 5.8 sun4u; Nav)",
	"Mozilla/4.78 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.78 [en] (X11; U; SunOS 5.7 sun4u)",
	"Mozilla/4.78 [en] (X11; U; Linux 2.4.9-34smp i686)",
	"Mozilla/4.78 [en] (X11; U; Linux 2.4.9-21 i686)",
	"Mozilla/4.78 [en] (X11; U; Linux 2.4.7-10 i686)",
	"Mozilla/4.78 [en] (X11; U; Linux 2.4.20-18.7 i686)",
	"Mozilla/4.78 [en] (X11; U; Linux 2.4.20 i686; Nav)",
	"Mozilla/4.78 [en] (X11; U; Linux 2.4.2 i386)",
	"Mozilla/4.77C-SGI [en] (X11; I; IRIX64 6.5 IP30)",
	"Mozilla/4.77C-SGI [en] (X11; I; IRIX64 6.5 IP30)",
	"Mozilla/4.77 [fr] (X11; U; Linux 2.4.9-34 i686)",
	"Mozilla/4.77 [fr] (X11; U; Linux 2.4.4-4GB i686)",
	"Mozilla/4.77 [fr] (X11; U; Linux 2.4.3-20mdk i686)",
	"Mozilla/4.77 [fr] (X11; U; Linux 2.4.17 i686; Nav)",
	"Mozilla/4.77 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.77 [en] (X11; U; SunOS 5.7 sun4u)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.9-a22m i686)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.20-bf2.4 i686)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.2-2 i686)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.19-acheron i686; Nav)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.19 i686; Nav)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.18-386 i686)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.18-27.7.x i686)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.4.17-lsm i686)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.2.17 i586)",
	"Mozilla/4.77 [en] (X11; U; Linux 2.2.14 i686)",
	"Mozilla/4.77 [en] (X11; U; HP-UX B.11.00 9000/800)",
	"Mozilla/4.77 [en] (WinNT; U)",
	"Mozilla/4.77 [en] (Windows NT 5.0; U)",
	"Mozilla/4.77 [en] (Win98; U)",
	"Mozilla/4.76C-SGI [en] (X11; I; IRIX64 6.5 IP30)",
	"Mozilla/4.76C-SGI [en] (X11; I; IRIX 6.5 IP32)",
	"Mozilla/4.76 [fr] (X11; U; Linux 2.4.2-2 i686)",
	"Mozilla/4.76 [en] (X11; U; SunOS 5.8 sun4u; Nav)",
	"Mozilla/4.76 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.76 [en] (X11; U; SunOS 5.8 i86pc)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.4.9-34 i686)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.4.5 i686)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.4.20 i686)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.4.18p3 i686)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.4.0 i686)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.2.19pre17 i686)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.2.16-22 i686)",
	"Mozilla/4.76 [en] (X11; U; Linux 2.2.16 i686)",
	"Mozilla/4.76 [en] (X11; U; HP-UX B.10.20 9000/782)",
	"Mozilla/4.76 [en] (WinNT; U)",
	"Mozilla/4.76 [en] (Win98; U)",
	"Mozilla/4.76 [en] (Win95; U)",
	"Mozilla/4.76 [de] (X11; U; Linux 2.4.4-4GB i686; Nav)",
	"Mozilla/4.76 [de] (X11; U; Linux 2.4.0-4GB i686)",
	"Mozilla/4.76 [de] (X11; U; Linux 2.2.18 i686)",
	"Mozilla/4.76 (X11; U; Linux 2.4.10-4GB i686)",
	"Mozilla/4.75 [pl] (X11; U; Linux 2.2.17-21mdk i686)",
	"Mozilla/4.75 [fr] (X11; U; Linux 2.2.16-3smp i686)",
	"Mozilla/4.75 [fr] (X11; U; Linux 2.2.16-22 i686)",
	"Mozilla/4.75 [fr] (WinNT; U)",
	"Mozilla/4.75 [fr] (Windows NT 5.0; U)",
	"Mozilla/4.75 [fr] (Win98; U)",
	"Mozilla/4.75 [fr] (Win95; U)",
	"Mozilla/4.75 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.75 [en] (X11; U; SunOS 5.7 sun4u)",
	"Mozilla/4.75 [en] (X11; U; SunOS 5.6 sun4u)",
	"Mozilla/4.75 [en] (X11; U; OpenBSD 2.8 i386)",
	"Mozilla/4.75 [en] (X11; U; Linux 2.2.16-3 i686)",
	"Mozilla/4.75 [en] (X11; U; Linux 2.2.12-20 i586)",
	"Mozilla/4.75 [en] (WinNT; U)",
	"Mozilla/4.75 [en] (Windows NT 5.0; U)",
	"Mozilla/4.75 [en] (Win98; U)",
	"Mozilla/4.75 [en] (Win95; U)",
	"Mozilla/4.75 [de] (WinNT; U)",
	"Mozilla/4.75 [de] (Windows NT 5.0; U)",
	"Mozilla/4.75 [de] (Win98; U)",
	"Mozilla/4.74 [en] (X11; U; Linux 2.2.16 i686)",
	"Mozilla/4.74 [en] (WinNT; U)",
	"Mozilla/4.74 [en] (Windows NT 5.0; U)",
	"Mozilla/4.74 [en] (Win98; U)",
	"Mozilla/4.74 [en] (Win95; U)",
	"Mozilla/4.74 [de] (X11; U; Linux 2.2.16 i686)",
	"Mozilla/4.74 [de] (X11; U; Linux 2.2.16 i586)",
	"Mozilla/4.74 (Macintosh; U; PPC)",
	"Mozilla/4.74 (Macintosh; U; PPC)",
	"Mozilla/4.73 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.73 [en] (X11; I; HP-UX B.10.20 9000/879)",
	"Mozilla/4.73 [en] (WinNT; U)",
	"Mozilla/4.73 [en] (WinNT; I)",
	"Mozilla/4.73 [en] (Windows NT 5.0; U)",
	"Mozilla/4.73 [en] (Windows NT 5.0; I)",
	"Mozilla/4.73 [en] (Win98; U)",
	"Mozilla/4.73 [en] (Win98; I)",
	"Mozilla/4.73 [en] (Win95; U)",
	"Mozilla/4.73 [en] (Win95; I)",
	"Mozilla/4.73 [de] (WinNT; U)",
	"Mozilla/4.73 [de] (Windows NT 5.0; U)",
	"Mozilla/4.73 [de] (Win98; U)",
	"Mozilla/4.73 [de] (Win95; U)",
	"Mozilla/4.73 (Macintosh; U; PPC)",
	"Mozilla/4.73 (Macintosh; I; PPC)",
	"Mozilla/4.73 [en] (Win95; I)",
	"Mozilla/4.73 [de] (Win98; U)",
	"Mozilla/4.72 [fr] (X11; U; Linux 2.2.14-5.0 i686)",
	"Mozilla/4.72 [en] (X11; U; Linux 2.2.20 i586; Nav)",
	"Mozilla/4.72 [en] (X11; U; Linux 2.2.14-5.0 i686)",
	"Mozilla/4.72 [en] (X11; I; SunOS 5.7 sun4m)",
	"Mozilla/4.72 [en] (X11; I; Linux 2.2.14 i686)",
	"Mozilla/4.72 [en] (X11; I; Linux 2.2.14 i586)",
	"Mozilla/4.72 [en] (X11; I; Linux 2.2.13 i586)",
	"Mozilla/4.72 [en] (X11; I; HP-UX B.11.00 9000/800)",
	"Mozilla/4.72 [en] (WinNT; U)",
	"Mozilla/4.72 [en] (WinNT; I)",
	"Mozilla/4.72 [en] (Windows NT 5.0; U)",
	"Mozilla/4.72 [en] (Windows NT 5.0; I)",
	"Mozilla/4.72 [en] (Win98;I)",
	"Mozilla/4.72 [en] (Win98; U)",
	"Mozilla/4.72 [en] (Win98; I)",
	"Mozilla/4.72 [en] (Win95; I)",
	"Mozilla/4.72 [de] (WinNT; U)",
	"Mozilla/4.72 [de] (Windows NT 5.0; U)",
	"Mozilla/4.72 [de] (Win95; U)",
	"Mozilla/4.72 (Macintosh; U; PPC)",
	"Mozilla/4.71 [en] (X11; U; Linux 2.0.36 i586)",
	"Mozilla/4.71 [en] (WinNT; I)",
	"Mozilla/4.71 [en] (Win98; I)",
	"Mozilla/4.7C-SGI [en] (X11; I; IRIX 6.5 IP32)",
	"Mozilla/4.7 [fr] (WinNT; I)",
	"Mozilla/4.7 [fr] (Win98; U)",
	"Mozilla/4.7 [fr] (Win98; I)",
	"Mozilla/4.7 [fr] (Win95; I)",
	"Mozilla/4.7 [en] (X11; U; SunOS 5.6 sun4u)",
	"Mozilla/4.7 [en] (X11; I; SunOS 5.8 sun4u)",
	"Mozilla/4.7 [en] (X11; I; SunOS 5.7 sun4u)",
	"Mozilla/4.7 [en] (X11; I; SunOS 5.6 sun4u)",
	"Mozilla/4.7 [en] (X11; I; Linux 2.2.13 i686; Nav)",
	"Mozilla/4.7 [en] (X11; I; Linux 2.2.13 i586)",
	"Mozilla/4.7 [en] (X11; I; Linux 2.2.12 i686; Nav)",
	"Mozilla/4.7 [en] (WinNT; U)",
	"Mozilla/4.7 [en] (WinNT; I)",
	"Mozilla/4.7 [en] (Windows NT 6.0; U)",
	"Mozilla/4.7 [en] (Win98; I)",
	"Mozilla/4.7 [en] (Win95; I)",
	"Mozilla/4.7 [en-gb] (WinNT; U)",
	"Mozilla/4.7 [en-gb] (WinNT; I)",
	"Mozilla/4.7 [en-gb] (Win98; U)",
	"Mozilla/4.7 [de] (WinNT; U)",
	"Mozilla/4.61 [ja] (X11; I; Linux 2.6.13-33cmc1 i686)",
	"Mozilla/4.61 [fi] (OS/2; I)",
	"Mozilla/4.61 [en] (X11; I; SunOS 5.6 sun4u)",
	"Mozilla/4.61 [en] (X11; I; Linux 2.2.12-20 i686; Nav)",
	"Mozilla/4.61 [en] (WinNT; I)",
	"Mozilla/4.61 [en] (Win98; I)",
	"Mozilla/4.61 [en] (Win95; I)",
	"Mozilla/4.61 [en] (OS/2; U)",
	"Mozilla/4.61 [en] (OS/2; I)",
	"Mozilla/4.61 [de] (OS/2; U)",
	"Mozilla/4.61 [de] (OS/2; I)",
	"Mozilla/4.61 (Macintosh; I; PPC)",
	"Mozilla/4.6 [fr] (WinNT; I)",
	"Mozilla/4.6 [fr] (Win95; I)",
	"Mozilla/4.6 [en] (X11; U; SunOS 5.8 sun4u)",
	"Mozilla/4.6 [en] (X11; I; SunOS 5.8 sun4u)",
	"Mozilla/4.6 [en] (X11; I; SunOS 5.5.1 sun4u; Nav)",
	"Mozilla/4.6 [en] (WinNT; I)",
	"Mozilla/4.6 [en] (Win98; I)",
	"Mozilla/4.6 [en] (Win95; I)",
	"Mozilla/4.6 [de] (WinNT; I)",
	"Mozilla/4.6 [de] (Win98; I)",
	"Mozilla/4.6 [de] (Win95; I)",
	"Mozilla/4.6 (Macintosh; U; PPC)",
	"Mozilla/4.6 (Macintosh; I; PPC)",
	"Mozilla/4.6 [de] (Win98; I)",
	"Mozilla/4.51 [it] (Win98; U)",
	"Mozilla/4.51 [fr] (Win95; I)",
	"Mozilla/4.51 [en] (X11; I; Linux 2.2.7 i686)",
	"Mozilla/4.51 [en] (X11; I; Linux 2.2.5 i686)",
	"Mozilla/4.51 [en] (WinNT; I)",
	"Mozilla/4.51 [en] (Win98; U)",
	"Mozilla/4.51 [en] (Win95; I)",
	"Mozilla/4.51 [de] (WinNT; I)",
	"Mozilla/4.51 [de] (Win98; I)",
	"Mozilla/4.51 [de] (Win95; I)",
	"Mozilla/4.51 (Macintosh; I; PPC)",
	"Mozilla/4.51 [de] (WinNT; I)",
	"Mozilla/4.5 [it] (Win98; I)",
	"Mozilla/4.5 [fr] (Win98; I)",
	"Mozilla/4.5 [fr] (Win95; I)",
	"Mozilla/4.5 [fr] (Macintosh; U; PPC)",
	"Mozilla/4.5 [fr] (Macintosh; I; PPC)",
	"Mozilla/4.5 [es] (Win98; I)",
	"Mozilla/4.5 [en] (X11; I; SunOS 5.8 sun4u)",
	"Mozilla/4.5 [en] (X11; I; SunOS 5.7 sun4u)",
	"Mozilla/4.5 [en] (X11; I; SunOS 5.6 sun4u)",
	"Mozilla/4.5 [en] (X11; I; Linux 2.2.16 i586)",
	"Mozilla/4.5 [en] (WinNT; U)",
	"Mozilla/4.5 [en] (Win98; I)",
	"Mozilla/4.5 [en] (Win95; I)",
	"Mozilla/4.5 [de] (WinNT; I)",
	"Mozilla/4.5 [de] (Win98; I)",
	"Mozilla/4.5 [de] (Macintosh; I; PPC)",
	"Mozilla/4.5 (Macintosh; U; PPC)",
	"Mozilla/4.5 [fr] (Macintosh; U; PPC)",
	"Mozilla/4.5 [en] (WinNT; I)",
	"Mozilla/4.41 (BEOS; U ;Nav)",
	"Mozilla/4.08 [en] (WinNT; U ;Nav)",
	"Mozilla/4.08 [en] (WinNT; I ;Nav)",
	"Mozilla/4.08 [en] (Win98; I ;Nav)",
	"Mozilla/4.08 [en] (Win95; I ;Nav)",
	"Mozilla/4.08 (Macintosh; I; PPC, Nav)",
	"Mozilla/4.08 [de] (WinNT; I)",
	"Mozilla/4.07 [en] (X11; I; Linux 2.0.36 i586)",
	"Mozilla/4.07 [en] (WinNT; U ;Nav)",
	"Mozilla/4.07 [en] (WinNT; I)",
	"Mozilla/4.07 [de] (Win98; I)",
	"Mozilla/4.07 [de] (Win95; I)",
	"Mozilla/4.07 [fr] (Win95; I)",
	"Mozilla/4.07 [en] (X11; I; Linux 2.0.36 i586)",
	"Mozilla/4.07 [en] (WinNT; U ;Nav)",
	"Mozilla/4.06 [hu] (Win98; I)",
	"Mozilla/4.06 [en] (X11; U; Linux 2.0.27 i586)",
	"Mozilla/4.06 [en] (X11; I; Linux 2.0.35 i686)",
	"Mozilla/4.06 [en] (WinNT; I)",
	"Mozilla/4.06 [en] (WinNT; I ;Nav)",
	"Mozilla/4.06 [de] (WinNT; I)",
	"Mozilla/4.06 [de] (Win98; I)",
	"Mozilla/4.06 (Win95; I)",
	"Mozilla/4.06 (Win95; I)",
	"Mozilla/4.05 [en] (X11; I; Linux 2.0.33 i586)",
	"Mozilla/4.05 [en] (Win95; I)",
	"Mozilla/4.05 [de] (Win95; I)",
	"Mozilla/4.04 [fr] (Macintosh; I; PPC, Nav)",
	"Mozilla/4.04 [en] (X11; I; IRIX 5.3 IP22)",
	"Mozilla/4.04 [en] (WinNT; U)",
	"Mozilla/4.04 [en] (WinNT; I)",
	"Mozilla/4.04 [en] (Win95; I ;Nav)",
	"Mozilla/4.04 [de] (WinNT; I ;Nav)",
	"Mozilla/4.04 [en] (Win95; I ;Nav)",
	"Mozilla/4.03 [fr] (Win95; U)",
	"Mozilla/4.03 [fr] (Win95; U)",
	"Mozilla/4.01 [en] (Win95; I)",
	"Mozilla/4.01 [de] (WinNT; I)",
	"Mozilla/4.0 (compatible; Windows NT 5.1; U; en)",
	"Mozilla/4.0 (compatible; Mozilla/5.0 ; Linux i686)",
	"Mozilla/3.04Gold (WinNT; U)",
	"Mozilla/3.04Gold (Macintosh; I; PPC)",
	"Mozilla/3.04 (WinNT; I)",
	"Mozilla/3.02 [en] (Windows NT 5.1; U)",
	"Mozilla/3.01Gold (X11; I; SunOS 5.5.1 sun4m)",
	"Mozilla/3.01Gold (Macintosh; I; 68K)",
	"Mozilla/3.01 [de] (Win95; I)",
	"Mozilla/3.01 (Macintosh; U; PPC)",
	"Mozilla/3.0 (X11; I; AIX 2)",
	"Mozilla/3.0 (Win95; I)",
	"Mozilla/3.0 (Macintosh; U; PPC Mac OS X; pl-PL)",
	"Mozilla/3.0 (Macintosh; I; PPC)",
	"Mozilla/3.0 (Macintosh; I; 68K)",
	"Mozilla/2.02Gold (Win95; I)",
	"Mozilla/2.02 [fr] (WinNT; I)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US) AppleWebKit/528.16 (KHTML, like Gecko, Safari/528.16) OmniWeb/v622.8.0.112941",
	"Mozilla/5.0 (Macintosh; U; Intel 80486Mac OS X; en-US) AppleWebKit/528.16 (KHTML, like Gecko, Safari/528.16) OmniWeb/v622.8.0.112916",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/530.18+(KHTML, like Gecko, Safari/528.16) OmniWeb/v622.8.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/528.16+(KHTML, like Gecko, Safari/528.16) OmniWeb/v622.8.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-US) AppleWebKit/528.16 (KHTML, like Gecko, Safari/528.16) OmniWeb/v622.8.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-US) AppleWebKit/528.16 (KHTML, like Gecko, Safari/528.16) OmniWeb/v622.8.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US) AppleWebKit/525.18 (KHTML, like Gecko, Safari/525.20) OmniWeb/v622.6.1.0.111015",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_7_5; en-US) AppleWebKit/533.21.1+(KHTML, like Gecko, Safari/533.19.4) Version/5.11.2 OmniWeb/622.19.3.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-US) AppleWebKit/531.21.8+(KHTML, like Gecko, Safari/528.16) Version/5.10.3 OmniWeb/622.14.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-US) AppleWebKit/531.21.8+(KHTML, like Gecko, Safari/528.16) OmniWeb/v622.11.0",
	"Mozilla/5.0 (Macintosh; U; PowerPC Mac OS X 10_5_8; en-US) AppleWebKit/531.9+(KHTML, like Gecko, Safari/528.16) OmniWeb/v622.10.0",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-US) AppleWebKit/531.9+(KHTML, like Gecko, Safari/528.16) OmniWeb/v622.10.0",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/420+ (KHTML, like Gecko, Safari/420) OmniWeb/v605",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/420+ (KHTML, like Gecko, Safari/420) OmniWeb/v603",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/420+ (KHTML, like Gecko, Safari/420) OmniWeb/v602",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/420+ (KHTML, like Gecko, Safari/420) OmniWeb/v601",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/420+ (KHTML, like Gecko, Safari) OmniWeb/v595",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/125.4 (KHTML, like Gecko, Safari) OmniWeb/v563.66",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/125.4 (KHTML, like Gecko, Safari) OmniWeb/v563.60",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/125.4 (KHTML, like Gecko, Safari) OmniWeb/v563.59",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/125.4 (KHTML, like Gecko, Safari) OmniWeb/v563.57",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-IT) AppleWebKit/125.4 (KHTML, like Gecko, Safari) OmniWeb/v563.15",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/125.4 (KHTML, like Gecko, Safari) OmniWeb/v563.15",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-DE) AppleWebKit/85 (KHTML, like Gecko) OmniWeb/v558.46",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-US) AppleWebKit/85 (KHTML, like Gecko) OmniWeb/v496",
	"Mozilla/4.5 (compatible; OmniWeb/4.2.1-v435.9; Mac_PowerPC)",
	"Mozilla/4.5 (compatible; OmniWeb/4.2-v435.5; Mac_PowerPC)",
	"Mozilla/4.5 (compatible; OmniWeb/4.1.1-v424.6; Mac_PowerPC)",
	"Mozilla/4.5 (compatible; OmniWeb/4.1.1-v424.6; Mac_PowerPC)",
	"Mozilla/4.5 (compatible; OmniWeb/4.1-v422; Mac_PowerPC)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/522+ (KHTML, like Gecko) OmniWeb",
	"Mozilla/5.0 (Windows NT 6.0; rv:2.0) Gecko/20100101 Firefox/4.0 Opera 12.14",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.0) Opera 12.14",
	"Mozilla/5.0 (Windows NT 5.1) Gecko/20100101 Firefox/14.0 Opera/12.0",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; de) Opera 11.51",
	"Mozilla/5.0 (Windows NT 5.1; U; en; rv:1.8.1) Gecko/20061208 Firefox/5.0 Opera 11.11",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.13) Gecko/20101213 Opera/9.80 (Windows NT 6.1; U; zh-tw) Presto/2.7.62 Version/11.01",
	"Mozilla/5.0 (Windows NT 6.1; U; nl; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 11.01",
	"Mozilla/5.0 (Windows NT 6.1; U; de; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 11.01",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; de) Opera 11.01",
	"Mozilla/5.0 (Windows NT 6.0; U; ja; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 11.00",
	"Mozilla/5.0 (Windows NT 5.1; U; pl; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 11.00",
	"Mozilla/5.0 (Windows NT 5.1; U; de; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 11.00",
	"Mozilla/4.0 (compatible; MSIE 8.0; X11; Linux x86_64; pl) Opera 11.00",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; fr) Opera 11.00",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; ja) Opera 11.00",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; en) Opera 11.00",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; pl) Opera 11.00",
	"Mozilla/5.0 (Windows NT 5.2; U; ru; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.70",
	"Mozilla/5.0 (Windows NT 5.1; U; zh-cn; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.70",
	"Mozilla/5.0 (X11; Linux x86_64; U; de; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.62",
	"Mozilla/4.0 (compatible; MSIE 8.0; X11; Linux x86_64; de) Opera 10.62",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; en) Opera 10.62",
	"Mozilla/5.0 (Windows NT 5.1; U; zh-cn; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.53",
	"Mozilla/5.0 (Windows NT 5.1; U; Firefox/5.0; en; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.53",
	"Mozilla/5.0 (Windows NT 5.1; U; Firefox/4.5; en; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.53",
	"Mozilla/5.0 (Windows NT 5.1; U; Firefox/3.5; en; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.53",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; ko) Opera 10.53",
	"Mozilla/5.0 (Windows NT 6.1; U; en-GB; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.51",
	"Mozilla/5.0 (Linux i686; U; en; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.51",
	"Mozilla/4.0 (compatible; MSIE 8.0; Linux i686; en) Opera 10.51",
	"Mozilla/5.0 (Windows NT 6.0; U; tr; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 10.10",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; de) Opera 10.10",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 6.0; tr) Opera 10.10",
	"Mozilla/5.0 (Linux i686 ; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.70",
	"Mozilla/4.0 (compatible; MSIE 6.0; Linux i686 ; en) Opera 9.70",
	"Mozilla/5.0 (Windows NT 5.1; U; en-GB; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.61",
	"Mozilla/5.0 (X11; Linux x86_64; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.60",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux x86_64; en) Opera 9.60",
	"Mozilla/5.0 (Windows NT 5.1; U; de; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.52",
	"Mozilla/5.0 (Windows NT 5.1; U; ; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.52",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; ru) Opera 9.52",
	"Mozilla/5.0 (X11; Linux i686; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.51",
	"Mozilla/5.0 (Windows NT 6.0; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.51",
	"Mozilla/5.0 (Windows NT 5.1; U; en; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.51",
	"Mozilla/5.0 (Windows NT 5.1; U; en-GB; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.51",
	"Mozilla/5.0 (Windows NT 5.1; U; de; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.51",
	"Mozilla/5.0 (Windows NT 5.1; U; zh-cn; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 9.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux x86_64; en) Opera 9.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 6.0; en) Opera 9.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.2; en) Opera 9.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; de) Opera 9.50",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9b3) Gecko/2008020514 Opera 9.5",
	"Mozilla/5.0 (Windows NT 5.2; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0 Opera 9.27",
	"Mozilla/5.0 (Windows NT 5.1; U; es-la; rv:1.8.0) Gecko/20060728 Firefox/1.5.0 Opera 9.27",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0 Opera 9.27",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; en) Opera 9.27",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.2; en) Opera 9.27",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; es-la) Opera 9.27",
	"Mozilla/5.0 (Windows NT 5.1; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0 Opera 9.26",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 6.0; en) Opera 9.26",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 9.26",
	"Mozilla/5.0 (Windows NT 5.1; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0 Opera 9.24",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 9.24",
	"Mozilla/4.0 (compatible; MSIE 6.0; Mac_PowerPC; en) Opera 9.24",
	"Mozilla/5.0 (X11; Linux i686; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0 Opera 9.23",
	"Mozilla/5.0 (Windows NT 5.1; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0 Opera 9.22",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; en) Opera 9.22",
	"Mozilla/5.0 (compatible; MSIE 6.0; Windows NT 5.1; zh-cn) Opera 8.65",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; zh-cn) Opera 8.65",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) Opera 8.65 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; Sprint:PPC-6700) Opera 8.65 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 320x320)Opera 8.65 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 320x320) Opera 8.65 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 240x320) Opera 8.65 [zh-cn]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 240x320) Opera 8.65 [nl]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 240x320) Opera 8.65 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 240x240) Opera 8.65 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC) Opera 8.65 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) Opera 8.60 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 240x320) Opera 8.60 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE; PPC; 240x240) Opera 8.60 [en]",
	"Mozilla/5.0 (Windows NT 5.1; U; pl) Opera 8.54",
	"Mozilla/5.0 (Windows 98; U; en) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; en) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; ru) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; pl) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; fr) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; de) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; da) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; pl) Opera 8.54",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; en) Opera 8.54",
	"Mozilla/5.0 (Windows NT 5.1; U; en) Opera 8.53",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; sv) Opera 8.53",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; ru) Opera 8.53",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 8.53",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; en) Opera 8.53",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98; en) Opera 8.53",
	"Mozilla/5.0 (X11; Linux i686; U; en) Opera 8.52",
	"Mozilla/5.0 (Windows NT 5.1; U; en) Opera 8.52",
	"Mozilla/5.0 (Windows NT 5.1; U; de) Opera 8.52",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; en) Opera 8.52",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; pl) Opera 8.52",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 8.52",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; de) Opera 8.52",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; en) Opera 8.52",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98; en) Opera 8.52",
	"Mozilla/5.0 (Windows NT 5.1; U; ru) Opera 8.51",
	"Mozilla/5.0 (Windows NT 5.1; U; fr) Opera 8.51",
	"Mozilla/5.0 (Windows NT 5.1; U; en) Opera 8.51",
	"Mozilla/5.0 (Windows ME; U; en) Opera 8.51",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X; U; en) Opera 8.51",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; ru) Opera 8.51",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; en) Opera 8.51",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; sv) Opera 8.51",
	"Mozilla/5.0 (Windows NT 5.1; U; en) Opera 8.50",
	"Mozilla/5.0 (Windows NT 5.1; U; de) Opera 8.50",
	"Mozilla/5.0 (Windows NT 5.0; U; de) Opera 8.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.2; ru) Opera 8.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.2; en) Opera 8.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; tr) Opera 8.50",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; sv) Opera 8.50",
	"Mozilla/5.0 (Windows NT 5.1; U; en) Opera 8.02",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; en) Opera 8.02",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 8.02",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; de) Opera 8.02",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; en) Opera 8.02",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; de) Opera 8.02",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows ME; pl) Opera 8.02",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98; de) Opera 8.02",
	"Mozilla/5.0 (Windows NT 5.1; U; en) Opera 8.01",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; ru) Opera 8.01",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 8.01",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; de) Opera 8.01",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 8.00",
	"Mozilla/5.0 (Windows NT 5.1; U; en) Opera 8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; ru) Opera 8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; IT) Opera 8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; de) Opera 8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; en) Opera 8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; de) Opera 8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE) Opera 8.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98; en) Opera 8.0",
	"Mozilla/5.0 (X11; Linux i386; U) Opera 7.60 [en-GB]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera 7.60",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.54u1 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98) Opera 7.54u1 [en]",
	"Mozilla/5.0 (X11; Linux i686; U) Opera 7.54 [en]",
	"Mozilla/5.0 (X11; Linux i686; U) Opera 7.54 [en]",
	"Mozilla/5.0 (Windows NT 5.1; U) Opera 7.54 [de]",
	"Mozilla/5.0 (Windows NT 5.0; U) Opera 7.54 [en]",
	"Mozilla/4.78 (Windows NT 5.1; U) Opera 7.54 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686) Opera 7.54 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.54 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.54 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.54 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.54 [pl]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.54 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.23; Mac_PowerPC) Opera 7.54 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.53 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows ME) Opera 7.53 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.52 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.52 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.52 [en]",
	"Mozilla/4.78 (Windows NT 5.1; U) Opera 7.51 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.51 [ru]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.51 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.51 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.50 [ru]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.50 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.50 [ru]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.50 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98) Opera 7.50 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; ; Linux x86_64) Opera 7.50 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; ; Linux i686) Opera 7.50 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686) Opera 7.23 [fi]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.23 [ru]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.23 [ru]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.23 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.23 [en-GB]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.23 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.23 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.23 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.23 [ca]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 4.0) Opera 7.23 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98) Opera 7.23 [en]",
	"Mozilla/5.0 (Windows NT 5.0; U) Opera 7.21 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.20 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.20 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98) Opera 7.20 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows 98) Opera 7.20 [de]",
	"Mozilla/5.0 (Windows NT 5.1; U) Opera 7.11 [en]",
	"Mozilla/5.0 (Windows NT 5.0; U) Opera 7.11 [en]",
	"Mozilla/5.0 (Linux 2.4.21-0.13mdk i686; U) Opera 7.11 [en]",
	"Mozilla/4.78 (Windows NT 5.0; U) Opera 7.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.11 [ru]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.11 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.11 [fr]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.11 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 4.0) Opera 7.11 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows ME) Opera 7.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.10 [fr]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Opera 7.10 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Opera 7.10 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 4.0) Opera 7.10 [de]",
	"Mozilla/3.0 (Windows NT 5.0; U) Opera 7.10 [de]",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.0.6) Gecko/2009020911 Ubuntu/8.10 (intrepid) Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.03 [de]",
	"Mozilla/5.0 (Windows NT 5.1; U) Opera 7.03 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.03 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.0) Opera 7.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.0) Opera 7.03 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows ME) Opera 7.03 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows 98) Opera 7.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows 98) Opera 7.03 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows 95) Opera 7.03 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.02 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 4.0) Opera 7.02 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows ME) Opera 7.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows 98) Opera 7.02 [en]",
	"Mozilla/5.0 (Windows NT 5.0; U) Opera 7.01 [en]",
	"Mozilla/4.78 (Windows NT 5.0; U) Opera 7.01 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.01 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.01 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.0) Opera 7.01 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows 98) Opera 7.01 [en]",
	"Mozilla/3.0 (Windows NT 5.0; U) Opera 7.01 [en]",
	"Mozilla/5.0 (Windows 2000; U) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows XP) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.1) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.0) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 5.0) Opera 7.0 [de]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows NT 4.0) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows ME) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows 98) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 6.0; MSIE 5.5; Windows 2000) Opera 7.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; UNIX) Opera 6.12 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.20-4GB i686) Opera 6.12 [de]",
	"Mozilla/5.0 (Linux 2.4.19-16mdk i686; U) Opera 6.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; UNIX) Opera 6.11 [fr]",
	"Mozilla/4.0 (compatible; MSIE 5.0; UNIX) Opera 6.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.4 i686) Opera 6.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.20-13.7 i686) Opera 6.11 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.19-4GB i686) Opera 6.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.19-16mdk i686) Opera 6.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.18 i686) Opera 6.11 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.10-4GB i686) Opera 6.11 [en]",
	"Mozilla/5.0 (Linux 2.4.18-ltsp-1 i686; U) Opera 6.1 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.19 i686) Opera 6.1 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.18-4GB i686) Opera 6.1 [de]",
	"Mozilla/5.0 (Windows XP; U) Opera 6.06 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.06 [fr]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.06 [de]",
	"Mozilla/5.0 (Windows XP; U) Opera 6.05 [de]",
	"Mozilla/5.0 (Windows NT 4.0; U) Opera 6.05 [en]",
	"Mozilla/5.0 (Windows ME; U) Opera 6.05 [de]",
	"Mozilla/5.0 (Windows 2000; U) Opera 6.04 [en]",
	"Mozilla/4.78 (Windows 2000; U) Opera 6.04 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.04 [fr]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.04 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.04 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 6.04 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.04 [pl]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.04 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 2000) Opera 6.04 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 2000) Opera 6.04 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.04 [en]",
	"Mozilla/5.0 (Windows 2000; U) Opera 6.03 [en]",
	"Mozilla/5.0 (Linux 2.4.18-18.7.x i686; U) Opera 6.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 2000) Opera 6.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.20-4GB i686) Opera 6.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.19-4GB i686) Opera 6.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.18-4GB i686) Opera 6.03 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.0-64GB-SMP i686) Opera 6.03 [en]",
	"Mozilla/5.0 (Windows 2000; U) Opera 6.02 [en]",
	"Mozilla/5.0 (Linux; U) Opera 6.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 6.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 95) Opera 6.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 95) Opera 6.02 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 2000) Opera 6.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.20-686 i686) Opera 6.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.18-4GB i686) Opera 6.02 [en]",
	"Mozilla/5.0 (Windows 2000; U) Opera 6.01 [en]",
	"Mozilla/5.0 (Windows 2000; U) Opera 6.01 [de]",
	"Mozilla/4.78 (Windows 2000; U) Opera 6.01 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.01 [it]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.01 [et]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.01 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 6.01 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 6.01 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows ME) Opera 6.01 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows ME) Opera 6.01 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.01 [it]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.01 [fr]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.01 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.01 [de]",
	"Mozilla/4.76 (Windows NT 4.0; U) Opera 6.0 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows XP) Opera 6.0 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 6.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 6.0 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows ME) Opera 6.0 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.0 [fr]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 6.0 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 2000) Opera 6.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 2000) Opera 6.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 2000) Opera 6.0 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Mac_PowerPC) Opera 6.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Mac_PowerPC) Opera 6.0 [de]",
	"Mozilla/5.0 (Windows 98; U) Opera 5.12 [de]",
	"Mozilla/4.76 (Windows NT 4.0; U) Opera 5.12 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 5.12 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows ME) Opera 5.12 [it]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows ME) Opera 5.12 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 5.12 [it]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 5.12 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 5.12 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Mac_PowerPC) Opera 5.12 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 5.11 [de]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows ME) Opera 5.11 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 5.1) Opera 5.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows NT 4.0) Opera 5.02 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Windows 98) Opera 5.02 [en]",
	"Mozilla/5.0 (SunOS 5.8 sun4u; U) Opera 5.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; SunOS 5.8 sun4u) Opera 5.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Mac_PowerPC) Opera 5.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux) Opera 5.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.4-4GB i686) Opera 5.0 [en]",
	"Mozilla/4.0 (compatible; MSIE 5.0; Linux 2.4.0-4GB i686) Opera 5.0 [en]",
	"Mozilla/5.0 (Macintosh; ; Intel Mac OS X; fr; rv:1.8.1.1) Gecko/20061204 Opera",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; en) Opera",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows CE) Opera",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; x64; fr; rv:1.9.1.1) Gecko/20090722 Firefox/3.5.1 Orca/1.2 build 2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.1) Gecko/20090722 Firefox/3.5.1 Orca/1.2 build 2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.1) Gecko/20090722 Firefox/3.5.1 Orca/1.2 build 2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.7) Gecko/2009030821 Firefox/3.0.7 Orca/1.1 build 2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.7) Gecko/2009030821 Firefox/3.0.7 Orca/1.1 build 2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.6) Gecko/2009022300 Firefox/3.0.6 Orca/1.1 build 1",
	"Mozilla/1.10 [en] (Compatible; RISC OS 3.70; Oregano 1.10)",
	"Mozilla/1.10 [en] (Compatible; RISC OS 3.70; Oregano 1.10)",
	"Mozilla/5.0 (X11; U; Linux i686; en-us) AppleWebKit/146.1 (KHTML, like Gecko) osb-browser/0.5",
	"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:19.0) Gecko/20130308 Firefox/19.0 PaleMoon/19.0.2",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64; rv:15.0) Gecko/20120919 Firefox/15.1.1-x64 PaleMoon/15.1.1-x64",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:15.0) Gecko/20120919 Firefox/15.1.1-x64 PaleMoon/15.1.1-x64",
	"Mozilla/5.0 (Windows NT 6.1; rv:15.0) Gecko/20120919 Firefox/15.1.1 PaleMoon/15.1.1",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:15.0) Gecko/20120912 Firefox/15.1-x64 PaleMoon/15.1-x64",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:15.0) Gecko/20120911 Firefox/15.1 PaleMoon/15.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:15.0) Gecko/20120911 Firefox/15.1 PaleMoon/15.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:15.0) Gecko/20120911 Firefox/15.1 PaleMoon/15.1",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64; rv:15.0) Gecko/20120819 Firefox/15.0-x64 PaleMoon/15.0-x64",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:15.0) Gecko/20120819 Firefox/15.0-x64 PaleMoon/15.0-x64",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:15.0) Gecko/20120819 Firefox/15.0 PaleMoon/15.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:15.0) Gecko/20120819 Firefox/15.0 PaleMoon/15.0",
	"Mozilla/5.0 (Windows NT 6.0; rv:15.0) Gecko/20120819 Firefox/15.0 PaleMoon/15.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:15.0) Gecko/20120819 Firefox/15.0 PaleMoon/15.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:12.3) Gecko/20120728 Firefox/12.3r2 PaleMoon/12.3r2",
	"Mozilla/5.0 (Windows NT 6.1; rv:12.3) Gecko/20120728 Firefox/12.3r2 PaleMoon/12.3r2",
	"Mozilla/5.0 (Windows NT 6.0; rv:12.3) Gecko/20120728 Firefox/12.3r2 PaleMoon/12.3r2",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.3) Gecko/20120728 Firefox/12.3r2 PaleMoon/12.3r2",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:12.3) Gecko/20120714 Firefox/12.3-x64 PaleMoon/12.3-x64",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:12.3) Gecko/20120717 Firefox/12.3 PaleMoon/12.3",
	"Mozilla/5.0 (Windows NT 6.1; rv:12.3) Gecko/20120717 Firefox/12.3 PaleMoon/12.3",
	"Mozilla/5.0 (Windows NT 6.1; rv:12.3) Gecko/20120714 Firefox/12.3 PaleMoon/12.3",
	"Mozilla/5.0 (Windows NT 6.0; rv:12.3) Gecko/20120717 Firefox/12.3 PaleMoon/12.3",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.3) Gecko/20120717 Firefox/12.3 PaleMoon/12.3",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.3) Gecko/20120714 Firefox/12.3 PaleMoon/12.3",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64; rv:12.2.1) Gecko/20120616 Firefox/12.2.1-x64 PaleMoon/12.2.1-x64",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:12.2.1) Gecko/20120616 Firefox/12.2.1-x64 PaleMoon/12.2.1-x64",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:12.2.1) Gecko/20120616 Firefox/13.0.1 PaleMoon/12.2.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:12.2.1) Gecko/20120616 Firefox/13.0 PaleMoon/12.2.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:12.2.1) Gecko/20120616 Firefox/12.2.1 PaleMoon/12.2.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:12.2.1) Gecko/20120616 Firefox/12.2.1 PaleMoon/12.2.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.2.1) Gecko/20120616 PaleMoon/12.2.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.2.1) Gecko/20120616 Firefox/12.2.1 PaleMoon/12.2.1",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:12.2) Gecko/20120606 Firefox/12.2-x64 PaleMoon/12.2-x64",
	"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:12.2) Gecko/20120605 Firefox/12.2 PaleMoon/12.2",
	"Mozilla/5.0 (Windows NT 6.0; WOW64; rv:12.2) Gecko/20120605 Firefox/12.2 PaleMoon/12.2",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.2) Gecko/20120605 Firefox/12.2 PaleMoon/12.2",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.0) Gecko/20120605 Firefox/12.2 Palemoon/12.2",
	"Mozilla/5.0 (Windows NT 6.0; rv:12.0) Gecko/20120424 Firefox/12.0 PaleMoon/12.0",
	"Mozilla/5.0 (Windows NT 6.0; rv:11.0) Gecko/20120319 Firefox/11.0 PaleMoon/11.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP; rv:1.9.2.8) Gecko/20100817 Firefox/3.6.8 (Palemoon/3.6.8a)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.32) Gecko/20120529 Firefox/3.6.32 (Palemoon/3.6.32)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv-SE; rv:1.9.2.32) Gecko/20120529 Firefox/3.6.32 (Palemoon/3.6.32)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.2.32) Gecko/20120529 Firefox/3.6.32 (Palemoon/3.6.32)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT; rv:1.9.2.32) Gecko/20120529 Firefox/3.6.32 (Palemoon/3.6.32)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.32) Gecko/20120529 Firefox/3.6.32 (Palemoon/3.6.32)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.2.31) Gecko/20120408 Firefox/3.6.31 (Palemoon/3.6.31)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.30) Gecko/20120217 Firefox/3.6.30 (Palemoon/3.6.30)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.30) Gecko/20120217 Firefox/3.6.30 (Palemoon/3.6.30)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.2.30) Gecko/20120217 Firefox/3.6.30 (Palemoon/3.6.30)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ja-JP; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3 (Palemoon/3.6.3)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3 (Palemoon/3.6.3)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3 (Palemoon/3.6.3) (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3 (Palemoon/3.6.3) GTB7.0 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3 (Palemoon/3.6.3) (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3 (Palemoon/3.6.3) (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.3) Gecko/20100403 Firefox/3.6.3 (Palemoon/3.6.3)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.2) Gecko/20100324 Firefox/3.6.2 (Palemoon/3.6.2)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.2) Gecko/20100324 Firefox/3.6.2 (Palemoon/3.6.2) (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.17) Gecko/20110531 Firefox/3.6.17 (Palemoon/3.6.17)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.13) Gecko/20101211 Firefox/3.6.13 (Palemoon/3.6.13) GTB7.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.13) Gecko/20101211 Firefox/3.6.13 (Palemoon/3.6.13)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.2.11) Gecko/20101023 Firefox/3.6.11 (Palemoon/3.6.11) ( .NET CLR 3.5.30729; .NET4.0E)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2.11) Gecko/20101023 Firefox/3.6.11 (Palemoon/3.6.11)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP; rv:1.9.2) Gecko/20100206 Palemoon/3.6.0.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2) Gecko/20100206 Palemoon/3.6.0.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2) Gecko/20100206 Palemoon/3.6.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.3a) Gecko/20021207 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.3a) Gecko/20021207 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4a) Gecko/20030411 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.3a) Gecko/20021207 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.3a) Gecko/20021207 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4a) Gecko/20030403 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3a) Gecko/20030105 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3a) Gecko/20021207 Phoenix/0.5",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.3a) Gecko/20021207 Phoenix/0.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.3a) Gecko/20030101 Phoenix/0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2b) Gecko/20021029 Phoenix/0.4",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.2b) Gecko/20021029 Phoenix/0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.2b) Gecko/20021029 Phoenix/0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.3a) Gecko/20021203 Phoenix/0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2b) Gecko/20021029 Phoenix/0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.2b) Gecko/20021014 Phoenix/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.2b) Gecko/20021014 Phoenix/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2b) Gecko/20021014 Phoenix/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.2b) Gecko/20021001 Phoenix/0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.2b) Gecko/20020923 Phoenix/0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.13) Gecko/20080414 Firefox/2.0.0.13 Pogo/2.0.0.13.6866",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.2.3) Gecko/20100402 Prism/1.0b4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.13) Gecko/20101206 Prism/1.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.2pre) Gecko/20100115 Prism/1.0b3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.7) Gecko/20091221 Firefox/3.5.7 Prism/1.0b2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.0.17) Gecko/2010010604 prism/0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/533.3 (KHTML, like Gecko) QtWeb Internet Browser/3.7 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/3.0 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/2.0 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/1.7 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-CO) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/1.7 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/1.7 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr-TR) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/1.5 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/1.5 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/527+ (KHTML, like Gecko) QtWeb Internet Browser/1.2 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/523.15 (KHTML, like Gecko) QtWeb Internet Browser/1.2 http://www.QtWeb.net",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB) AppleWebKit/523.15 (KHTML, like Gecko) QtWeb Internet Browser/1.2 http://www.QtWeb.net",
	"Mozilla/5.0 (X11; U; Linux x86_64; cs-CZ) AppleWebKit/533.3 (KHTML, like Gecko) rekonq Safari/533.3",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL) AppleWebKit/533.3 (KHTML, like Gecko) rekonq Safari/533.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB) AppleWebKit/533.3 (KHTML, like Gecko) rekonq Safari/533.3",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.494 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.494 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.494 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.494 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.484 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.484 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.478 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.478 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.471 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.423 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.423 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.390 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.58.209 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.357 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.357 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.357 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.357 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_6) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.357 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.343 Chrome/11.0.696.71 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.310 Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.310 Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.310 Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.310 Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.292 Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.292 Chrome/11.0.696.68 Safari/534.24",
	"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.283 Chrome/11.0.696.65 Safari/534.24",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_7) AppleWebKit/534.24 (KHTML, like Gecko) RockMelt/0.9.56.283 Chrome/11.0.696.65 Safari/534.24",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.549 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.549 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.549 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.549 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.549 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.549 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.518 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.518 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.518 Chrome/10.0.648.205 Safari/534.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.459 Chrome/10.0.648.204 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.459 Chrome/10.0.648.204 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.16 (KHTML, like Gecko) RockMelt/0.9.50.459 Chrome/10.0.648.204 Safari/534.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.48.59 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.48.59 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.48.59 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.48.59 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.48.58 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.48.51 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.48.51 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.46.126 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.9.46.126 Chrome/9.0.597.107 Safari/534.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) RockMelt/0.8.40.147 Chrome/8.0.552.231 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.10 (KHTML, like Gecko) RockMelt/0.8.40.147 Chrome/8.0.552.231 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.10 (KHTML, like Gecko) RockMelt/0.8.40.147 Chrome/8.0.552.231 Safari/534.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.10 (KHTML, like Gecko) RockMelt/0.8.40.147 Chrome/8.0.552.231 Safari/534.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.10 (KHTML, like Gecko) RockMelt/0.8.40.147 Chrome/8.0.552.231 Safari/534.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.79 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.79 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.74 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.128 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.128 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.128 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.116 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.116 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.116 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.116 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.116 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-US) AppleWebKit/534.7 (KHTML, like Gecko) RockMelt/0.8.36.116 Chrome/7.0.517.44 Safari/534.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; ha) AppleWebKit/534.13 (KHTML, like Gecko) RockMelt/0.445.436.1326 Chrome/12.0.632.107 Safari/534.13",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/537.13+ (KHTML, like Gecko) Version/5.1.7 Safari/534.57.2",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_3) AppleWebKit/534.55.3 (KHTML, like Gecko) Version/5.1.3 Safari/534.53.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_8; de-at) AppleWebKit/533.21.1 (KHTML, like Gecko) Version/5.0.5 Safari/533.21.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; da-dk) AppleWebKit/533.21.1 (KHTML, like Gecko) Version/5.0.5 Safari/533.21.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; tr-TR) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ko-KR) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; fr-FR) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; cs-CZ) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja-JP) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; zh-cn) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; ja-jp) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; ja-jp) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; zh-cn) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; sv-se) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; ko-kr) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; ja-jp) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; it-it) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; fr-fr) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; es-es) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-us) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; en-gb) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; de-de) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.4 Safari/533.20.27",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; sv-SE) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ja-JP) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de-DE) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; hu-HU) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-DE) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/533.20.25 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_7; en-us) AppleWebKit/534.16+ (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_6; fr-ch) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; de-de) AppleWebKit/534.15+ (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_5; ar) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/5.0.3 Safari/533.19.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-HK) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; tr-TR) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; nb-NO) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr-FR) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; zh-cn) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0.2 Safari/533.18.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/533.17.8 (KHTML, like Gecko) Version/5.0.1 Safari/533.17.8",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; th-th) AppleWebKit/533.17.8 (KHTML, like Gecko) Version/5.0.1 Safari/533.17.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-us) AppleWebKit/531.2+ (KHTML, like Gecko) Version/5.0 Safari/531.2+",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-ca) AppleWebKit/531.2+ (KHTML, like Gecko) Version/5.0 Safari/531.2+",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ja-JP) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; es-ES) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja-JP) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; ja-jp) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; fr) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; zh-cn) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; ru-ru) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; ko-kr) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; it-it) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; HTC-P715a; en-ca) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-us) AppleWebKit/534.1+ (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-au) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; el-gr) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; ca-es) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; zh-tw) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; ja-jp) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; it-it) AppleWebKit/533.16 (KHTML, like Gecko) Version/5.0 Safari/533.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-en) AppleWebKit/533.16 (KHTML, like Gecko) Version/4.1 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; nl-nl) AppleWebKit/533.16 (KHTML, like Gecko) Version/4.1 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; ja-jp) AppleWebKit/533.16 (KHTML, like Gecko) Version/4.1 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; de-de) AppleWebKit/533.16 (KHTML, like Gecko) Version/4.1 Safari/533.16",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_7; en-us) AppleWebKit/533.4 (KHTML, like Gecko) Version/4.1 Safari/533.4",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; nb-no) AppleWebKit/533.16 (KHTML, like Gecko) Version/4.1 Safari/533.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en) AppleWebKit/526.9 (KHTML, like Gecko) Version/4.0dp1 Safari/526.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; tr) AppleWebKit/528.4+ (KHTML, like Gecko) Version/4.0dp1 Safari/526.11.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; en) AppleWebKit/528.4+ (KHTML, like Gecko) Version/4.0dp1 Safari/526.11.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; de) AppleWebKit/528.4+ (KHTML, like Gecko) Version/4.0dp1 Safari/526.11.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.5; en-US; rv:1.9.1b3pre) Gecko/20081212 Mozilla/5.0 (Windows; U; Windows NT 5.1; en) AppleWebKit/526.9 (KHTML, like Gecko) Version/4.0dp1 Safari/526.8",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-gb) AppleWebKit/528.10+ (KHTML, like Gecko) Version/4.0dp1 Safari/526.11.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_4; en-us) AppleWebKit/528.4+ (KHTML, like Gecko) Version/4.0dp1 Safari/526.11.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_4; en-gb) AppleWebKit/528.4+ (KHTML, like Gecko) Version/4.0dp1 Safari/526.11.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; es-ES) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-gb) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs-CZ) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; en-us) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; da-dk) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; ja-jp) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-us) AppleWebKit/533.4+ (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-us) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; de-de) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; ja-jp) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; nl-nl) AppleWebKit/531.22.7 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/534.57.2 (KHTML, like Gecko) Version/4.0.5 Safari/531.22.7",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-TW) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ko-KR) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/533.18.1 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE) AppleWebKit/532+ (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_6_1; en_GB, en_US) AppleWebKit/531.21.10 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; hu-hu) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; en-us) AppleWebKit/531.21.11 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; ru-ru) AppleWebKit/533.2+ (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-us) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; de-at) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.4 Safari/531.21.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr-ch) AppleWebKit/531.9 (KHTML, like Gecko) Version/4.0.3 Safari/531.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-us) AppleWebKit/531.9 (KHTML, like Gecko) Version/4.0.3 Safari/531.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; en-us) AppleWebKit/532.0+ (KHTML, like Gecko) Version/4.0.3 Safari/531.9.2009",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; en-us) AppleWebKit/532.0+ (KHTML, like Gecko) Version/4.0.3 Safari/531.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_1; nl-nl) AppleWebKit/532.3+ (KHTML, like Gecko) Version/4.0.3 Safari/531.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; fi-fi) AppleWebKit/531.9 (KHTML, like Gecko) Version/4.0.3 Safari/531.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_8; en-us) AppleWebKit/531.21.8 (KHTML, like Gecko) Version/4.0.3 Safari/531.21.10",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6) AppleWebKit/531.4 (KHTML, like Gecko) Version/4.0.3 Safari/531.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/532+ (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; zh-TW) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pl-PL) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja-JP) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr-FR) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-DE) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_7; en-us) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-us) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.2 Safari/530.19",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-us) AppleWebKit/531.2+ (KHTML, like Gecko) Version/4.0.1 Safari/530.18",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; en-us) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/4.0.1 Safari/530.18",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ru-RU) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ja-JP) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; hu-HU) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; he-IL) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; he-IL) AppleWebKit/528+ (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr-FR) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-es) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-DE) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv-SE) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-PT) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nb-NO) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hu-HU) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fi-FI) AppleWebKit/528.16 (KHTML, like Gecko) Version/4.0 Safari/528.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs-CZ) AppleWebKit/525.28.3 (KHTML, like Gecko) Version/3.2.3 Safari/525.29",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; ja-jp) AppleWebKit/530.19.2 (KHTML, like Gecko) Version/3.2.3 Safari/525.28.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; de-de) AppleWebKit/525.28.3 (KHTML, like Gecko) Version/3.2.3 Safari/525.28.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de-DE) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-DE) AppleWebKit/528+ (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nb-NO) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ko-KR) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.28 (KHTML, like Gecko) Version/3.2.2 Safari/525.28.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sv-SE) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-DE) AppleWebKit/528+ (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja-JP) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_8; ja-jp) AppleWebKit/533.19.4 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_6; nl-nl) AppleWebKit/530.0+ (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_6; fr-fr) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_6; en-us) AppleWebKit/530.1+ (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; sv-se) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; pl-pl) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; it-it) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; fr-fr) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; es-es) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; zh-tw) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; ru-ru) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; nb-no) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; ko-kr) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; it-it) AppleWebKit/528.8+ (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; it-it) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; hr-hr) AppleWebKit/530.1+ (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; fr-fr) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.2.1 Safari/525.27.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; hu-HU) AppleWebKit/525.26.2 (KHTML, like Gecko) Version/3.2 Safari/525.26.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU) AppleWebKit/525.26.2 (KHTML, like Gecko) Version/3.2 Safari/525.26.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_5; fi-fi) AppleWebKit/525.26.2 (KHTML, like Gecko) Version/3.2 Safari/525.26.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_5; en-us) AppleWebKit/525.26.2 (KHTML, like Gecko) Version/3.2 Safari/525.26.12",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; sv-se) AppleWebKit/525.26.2 (KHTML, like Gecko) Version/3.2 Safari/525.26.12",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; ja-jp) AppleWebKit/525.26.2 (KHTML, like Gecko) Version/3.2 Safari/525.26.12",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; en-us) AppleWebKit/525.25 (KHTML, like Gecko) Version/3.2 Safari/525.25",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; pl-PL) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr-FR) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; pt-BR) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT) AppleWebKit/525+ (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB) AppleWebKit/525.19 (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Windows; U; en) AppleWebKit/420+ (KHTML, like Gecko) Version/3.1.2 Safari/525.21",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_6; en-us) AppleWebKit/525.18.1 (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_5; fr-fr) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_4; fr-fr) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; sv-se) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.2 Safari/525.22",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; fr) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.2 Safari/525.22",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/530.6+ (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/528.7+ (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/528.4+ (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-gb) AppleWebKit/525.18.1 (KHTML, like Gecko) Version/3.1.2 Safari/525.20.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/525+ (KHTML, like Gecko) Version/3.1.1 Safari/525.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ca-es) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_3; sv-se) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_3; en-us) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_3; en) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_2; en) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.18",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; ja-jp) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.18",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; en) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.18",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_7; de-de) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/525.18.1 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_3; nl-nl) AppleWebKit/527+ (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_3; nb-no) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_3; hu-hu) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_3; es-es) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_3; en-ca) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.20",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; ja-jp) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1.1 Safari/525.18",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; ru-RU) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; da-DK) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_4; en-us) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_2; en-gb) AppleWebKit/526+ (KHTML, like Gecko) Version/3.1 Safari/525.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; nl-nl) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; zh-tw) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/525.27.1 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; pt-br) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; it-it) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; fr-fr) AppleWebKit/525.9 (KHTML, like Gecko) Version/3.1 Safari/525.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; es-es) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; en-us) AppleWebKit/526.1+ (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; en-us) AppleWebKit/525.9 (KHTML, like Gecko) Version/3.1 Safari/525.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; en-us) AppleWebKit/525.7 (KHTML, like Gecko) Version/3.1 Safari/525.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; en-gb) AppleWebKit/525.13 (KHTML, like Gecko) Version/3.1 Safari/525.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; en-au) AppleWebKit/525.8+ (KHTML, like Gecko) Version/3.1 Safari/525.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en) AppleWebKit/525+ (KHTML, like Gecko) Version/3.0.4 Safari/523.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; da-dk) AppleWebKit/523.15.1 (KHTML, like Gecko) Version/3.0.4 Safari/523.15",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/523.12.2 (KHTML, like Gecko) Version/3.0.4 Safari/523.12.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/523.10.3 (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/523.10.3 (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_4; en-us) AppleWebKit/525.18 (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_4_11; en) AppleWebKit/525.3+ (KHTML, like Gecko) Version/3.0.4 Safari/523.12.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; sv-se) AppleWebKit/523.12.2 (KHTML, like Gecko) Version/3.0.4 Safari/523.12.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; sv-se) AppleWebKit/523.10.6 (KHTML, like Gecko) Version/3.0.4 Safari/523.10.6",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; sv-se) AppleWebKit/523.10.3 (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; ko-kr) AppleWebKit/523.15.1 (KHTML, like Gecko) Version/3.0.4 Safari/523.15",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; ja-jp) AppleWebKit/523.12.2 (KHTML, like Gecko) Version/3.0.4 Safari/523.12.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; ja-jp) AppleWebKit/523.10.3 (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; it-it) AppleWebKit/523.12.2 (KHTML, like Gecko) Version/3.0.4 Safari/523.12.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; it-it) AppleWebKit/523.10.6 (KHTML, like Gecko) Version/3.0.4 Safari/523.10.6",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; fr-fr) AppleWebKit/525.1+ (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; fr-fr) AppleWebKit/523.10.3 (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; fr) AppleWebKit/523.12.2 (KHTML, like Gecko) Version/3.0.4 Safari/523.12.2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; es-es) AppleWebKit/523.15.1 (KHTML, like Gecko) Version/3.0.4 Safari/523.15",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-us) AppleWebKit/525.1+ (KHTML, like Gecko) Version/3.0.4 Safari/523.10",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; cs) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; da-DK) AppleWebKit/523.11.1+ (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; da) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs) AppleWebKit/522.15.5 (KHTML, like Gecko) Version/3.0.3 Safari/522.15.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/523.6 (KHTML, like Gecko) Version/3.0.3 Safari/523.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/523.3+ (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/522.11.1 (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ca-es) AppleWebKit/522.11.1 (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; ru-ru) AppleWebKit/522.11.1 (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-us) AppleWebKit/522.11.1 (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/523.9+ (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/523.5+ (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/523.2+ (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/522.11.1 (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; de-de) AppleWebKit/522.11.1 (KHTML, like Gecko) Version/3.0.3 Safari/522.12.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; nl) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; zh) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; el) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; cs) AppleWebKit/522.13.1 (KHTML, like Gecko) Version/3.0.2 Safari/522.13.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/522.11 (KHTML, like Gecko) Version/3.0.2 Safari/522.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/522+ (KHTML, like Gecko) Version/3.0.2 Safari/522.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/522.11 (KHTML, like Gecko) Version/3.0.2 Safari/522.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/522.11 (KHTML, like Gecko) Version/3.0.2 Safari/522.12",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/522.11 (KHTML, like Gecko) Version/3.0.2 Safari/522.12",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/522+ (KHTML, like Gecko) Version/3.0.2 Safari/522.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fi) AppleWebKit/522.12.1 (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en) AppleWebKit/522.12.1 (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; th) AppleWebKit/522.12.1 (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv) AppleWebKit/522.12.1 (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl) AppleWebKit/522.12.1 (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en) AppleWebKit/522.4.1+ (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en) AppleWebKit/522.12.1 (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en) AppleWebKit/522.12.1 (KHTML, like Gecko) Version/3.0.1 Safari/522.12.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; sv-SE) AppleWebKit/523.13 (KHTML, like Gecko) Version/3.0 Safari/523.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; nl) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US) AppleWebKit/523.15 (KHTML, like Gecko) Version/3.0 Safari/523.15",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; da-DK) AppleWebKit/523.12.9 (KHTML, like Gecko) Version/3.0 Safari/523.12.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; pt) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; nl) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW) AppleWebKit/523.15 (KHTML, like Gecko) Version/3.0 Safari/523.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr-TR) AppleWebKit/523.15 (KHTML, like Gecko) Version/3.0 Safari/523.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; sv) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/525+ (KHTML, like Gecko) Version/3.0 Safari/523.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/523.15 (KHTML, like Gecko) Version/3.0 Safari/523.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL) AppleWebKit/523.15 (KHTML, like Gecko) Version/3.0 Safari/523.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL) AppleWebKit/523.12.9 (KHTML, like Gecko) Version/3.0 Safari/523.12.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nb) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; id) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; hr) AppleWebKit/522.11.3 (KHTML, like Gecko) Version/3.0 Safari/522.11.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR) AppleWebKit/523.15 (KHTML, like Gecko) Version/3.0 Safari/523.15",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/419 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/418.9 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/418.9 (KHTML, like Gecko) Safari/",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; pt-pt) AppleWebKit/418.9.1 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nl-nl) AppleWebKit/418.8 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/418.9.1 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/418.9 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/419 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/418.9 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/418.9.1 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fi-fi) AppleWebKit/418.8 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es-es) AppleWebKit/418.8 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es) AppleWebKit/419 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en_CA) AppleWebKit/419 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/419 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/418.9 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/418.8 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9.1 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; tr-tr) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nl-nl) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nl-nl) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.9.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nl-nl) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nb-no) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nb-no) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.9.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/417.9 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.9.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/417.9 (KHTML, like Gecko) Safari/417.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418 (KHTML, like Gecko) Safari/417.9.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nl-nl) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nl-nl) AppleWebKit/416.11 (KHTML, like Gecko) Safari/416.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nl-nl) AppleWebKit/416.11 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; nb-no) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/416.11 (KHTML, like Gecko) Safari/416.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/416.12 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/416.11 (KHTML, like Gecko) Safari/416.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/416.11 (KHTML, like Gecko) Safari/416.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-ca) AppleWebKit/416.11 (KHTML, like Gecko) Safari/416.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/416.11 (KHTML, like Gecko) Safari/416.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/416.11 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/416.12 (KHTML, like Gecko) Safari/416.13",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/412.7 (KHTML, like Gecko) Safari/412.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS; pl-pl) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS; en-en) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/412.6 (KHTML, like Gecko) Safari/412.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/412.6 (KHTML, like Gecko) Safari/412.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es-ES) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en_US) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/412.6 (KHTML, like Gecko) Safari/412.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/412 (KHTML, like Gecko) Safari/412 Privoxy/3.0",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/412.6.2 (KHTML, like Gecko) Safari/412.2.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/412.6.2 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/412.6 (KHTML, like Gecko) Safari/412.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/412.6.2 (KHTML, like Gecko) Safari/412.2.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/412.6 (KHTML, like Gecko) Safari/412.2_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/412.6 (KHTML, like Gecko) Safari/412.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/412.6 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/412 (KHTML, like Gecko) Safari/412",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/312.8.1 (KHTML, like Gecko) Safari/312.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.3.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.8.1 (KHTML, like Gecko) Safari/312.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.5_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.8 (KHTML, like Gecko) Safari/312.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/312.5.2 (KHTML, like Gecko) Safari/312.3.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.5.2 (KHTML, like Gecko) Safari/312.3.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.5 (KHTML, like Gecko) Safari/312.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/312.5.2 (KHTML, like Gecko) Safari/312.3.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/312.5 (KHTML, like Gecko) Safari/312.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es-es) AppleWebKit/312.5.2 (KHTML, like Gecko) Safari/312.3.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/312.5 (KHTML, like Gecko) Safari/312.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.5.2 (KHTML, like Gecko) Safari/312.3.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.5.2 (KHTML, like Gecko) Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.5.1 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.5 (KHTML, like Gecko) Safari/312.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.5.2 (KHTML, like Gecko) Safari/312.3.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.1.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/312.1 (KHTML, like Gecko) Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-ch) AppleWebKit/312.1.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-ca) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/312.1 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.1.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.1.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312.3.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-ch) AppleWebKit/312.1 (KHTML, like Gecko) Safari/312",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/125.5.6 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.11",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-ch) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-ch) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.11",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.7 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.6 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.11",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.5.7 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.5.6 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.5.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.11",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.5.7 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.5.6 (KHTML, like Gecko) Safari/125.12_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.5.6 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.12_Adobe",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.5.5 (KHTML, like Gecko) Safari/125.12",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/125.4 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/125.5 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/125.4 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en_CA) AppleWebKit/125.4 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.4 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-au) AppleWebKit/125.4 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.5 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.4 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.4 (KHTML, like Gecko) Safari/100",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.4 (KHTML, like Gecko) Safari/125.9",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; es-es) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-gb) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.2 (KHTML, like Gecko) Safari/85.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/125.2 (KHTML, like Gecko) Safari/125.7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; it-it) AppleWebKit/124 (KHTML, like Gecko) Safari/125.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/124 (KHTML, like Gecko) Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/124 (KHTML, like Gecko) Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/124 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/124 (KHTML, like Gecko) Safari/125.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/124 (KHTML, like Gecko) Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/85.8.5 (KHTML, like Gecko) Safari/85.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/85.8.5 (KHTML, like Gecko) Safari/85.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/85.8.5 (KHTML, like Gecko) Safari/85.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/85.8.2 (KHTML, like Gecko) Safari/85.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-gb) AppleWebKit/85.8.5 (KHTML, like Gecko) Safari/85.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/85.8.5 (KHTML, like Gecko) Safari/85.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/85.8.2 (KHTML, like Gecko) Safari/85.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/85.8.5 (KHTML, like Gecko) Safari/85.8.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/85.8.5 (KHTML, like Gecko) Safari/85",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/85.8.2 (KHTML, like Gecko) Safari/85.8",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; sv-se) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr-fr) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fr) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.7",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/85.7 (KHTML, like Gecko) Safari/85.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; zh-CN) AppleWebKit/533+ (KHTML, like Gecko)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US) AppleWebKit/528.8 (KHTML, like Gecko)",
	"Mozilla/5.0 (Windows NT 5.1) AppleWebKit/534.34 (KHTML, like Gecko) Dooble/1.40 Safari/534.34",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; fi-fi) AppleWebKit/420+ (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/312.8.1 (KHTML, like Gecko) Safari/312.6",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/419.2 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/418.9.1 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-ch) AppleWebKit/85 (KHTML, like Gecko) Safari/85",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-CH) AppleWebKit/419.2 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; da-dk) AppleWebKit/522+ (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10_5_6; en-us) AppleWebKit/528.16 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; it-IT) AppleWebKit/521.25 (KHTML, like Gecko) Safari/521.24",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-us) AppleWebKit/419.2.1 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/522.11.1 (KHTML, like Gecko) Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/521.32.1 (KHTML, like Gecko) Safari/521.32.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_3; es-es) AppleWebKit/531.22.7 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/528.16 (KHTML, like Gecko)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; it-it) AppleWebKit/525.18 (KHTML, like Gecko)",
	"Mozilla/5.0 (Windows NT 5.2; RW; rv:7.0a1) Gecko/20091211 SeaMonkey/9.23a1pre",
	"Mozilla/5.0 (Windows NT 5.2; RW; rv:7.0a1) Gecko/20091211 SeaMonkey/9.23a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.2; WOW64; rv:1.8.0.7) Gecko/20110321 MultiZilla/4.33.2.6a SeaMonkey/8.6.55",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; rv:1.8.0.7) Gecko/20110321 MultiZilla/4.33.2.6a SeaMonkey/8.6.55",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; RW; rv:1.8.0.7) Gecko/20110321 MultiZilla/4.33.2.6a SeaMonkey/8.6.55",
	"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:1.8.0.7) Gecko/20110321 MultiZilla/4.33.2.6a SeaMonkey/8.6.55",
	"Mozilla/5.0 (X11; U; Linux i686; ru; rv:33.2.3.12) Gecko/20120201 SeaMonkey/8.2.8",
	"Mozilla/5.0 (X11; Linux x86_64; rv:12.0) Gecko/20120501 Firefox/12.0 SeaMonkey/2.9.1 Lightning/1.4",
	"Mozilla/5.0 (X11; Linux i686; rv:12.0) Gecko/20120502 SeaMonkey/2.9.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:12.0) Gecko/20120427 Firefox/12.0 SeaMonkey/2.9",
	"Mozilla/5.0 (Windows NT 5.0; rv:1.9.2.8) Gecko/20120427 Firefox/12.0 SeaMonkey/2.9",
	"Mozilla/5.0 (Windows NT 6.2; rv:11.0) Gecko/20120312 Firefox/11.0 SeaMonkey/2.8",
	"Mozilla/5.0 (X11; Linux x86_64; rv:10.0.2) Gecko/20120216 Firefox/10.0.2 SeaMonkey/2.7.2",
	"Mozilla/5.0 (OS/2; Warp 4.5; rv:10.0.12) Gecko/20130108 Firefox/10.0.12 SeaMonkey/2.7.2",
	"Mozilla/5.0 (X11; Linux i686; rv:10.0.1) Gecko/20100101 Firefox/10.0.1 SeaMonkey/2.7.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:10.0.1) Gecko/20100101 Firefox/10.0.1 SeaMonkey/2.7.1",
	"Mozilla/5.0 (Windows NT 5.2; rv:10.0.1) Gecko/20100101 Firefox/10.0.1 SeaMonkey/2.7.1",
	"Mozilla/5.0 (X11; Linux i686; rv:9.0.1) Gecko/20120127 SeaMonkey/2.6.1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110622 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110621 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110619 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110618 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110616 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110614 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110613 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110610 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110607 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.1; rv:7.0a1) Gecko/20110619 Firefox/7.0a1 SeaMonkey/2.4a1",
	"Mozilla/5.0 (Windows NT 5.1; rv:7.0a1) Gecko/20110612 Firefox/7.0a1 SeaMonkey/2.4a1",
	"Mozilla/5.0 (X11; FreeBSD amd64; rv:6.0) Gecko/20110818 Firefox/6.0 SeaMonkey/2.3",
	"Mozilla/5.0 (X11; Linux i686; rv:31.0) Gecko/20100101 Firefox/31.0 SeaMonkey/2.28a1",
	"Mozilla/5.0 (X11; Linux i686; rv:28.0) Gecko/20100101 Firefox/28.0 SeaMonkey/2.25",
	"Mozilla/5.0 (Windows NT 6.3; WOW64; rv:28.0) Gecko/20100101 Firefox/28.0 SeaMonkey/2.25",
	"Mozilla/5.0 (Windows NT 5.1; rv:28.0) Gecko/20100101 Firefox/28.0 SeaMonkey/2.25 Lightning/3.0b1",
	"Mozilla/5.0 (X11; FreeBSD amd64; rv:27.0) Gecko/20100101 Firefox/27.0 SeaMonkey/2.24",
	"Mozilla/5.0 (Windows NT 5.1; rv:26.0) Gecko/20100101 Firefox/26.0 SeaMonkey/2.23a1",
	"Mozilla/5.0 (X11; OpenBSD amd64; rv:24.0) Gecko/20100101 Firefox/24.0 SeaMonkey/2.21",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:24.0) Gecko/20100101 Firefox/24.0 SeaMonkey/2.21 Lightning/2.6b3",
	"Mozilla/5.0 (X11; Linux x86_64; rv:7.0a1) Gecko/20110602 Firefox/7.0a1 SeaMonkey/2.2a1pre Lightning/1.1a1pre",
	"Mozilla/5.0 (X11; Linux i686; rv:7.0a1) Gecko/20110604 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (X11; Linux i686; rv:7.0a1) Gecko/20110603 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (X11; Linux i686; rv:7.0a1) Gecko/20110530 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (X11; Linux i686; rv:7.0a1) Gecko/20110526 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.2a1pre) Gecko/20110407 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.2a1pre) Gecko/20110327 SeaMonkey/2.2a1pr0 (X11; U; Linux i686; en-US; rv:1.9.1.11) Gecko/20100701 SeaMonkey/2.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 9.0; en-US; rv:1.9.1.11) Gecko/20100701 SeaMonkey/2.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.11) Gecko/20100701 SeaMonkey/2.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.11) Gecko/20100722 SeaMonkey/2.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.11) Gecko/20100701 SeaMonkey/2.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1.11) Gecko/20100701 SeaMonkey/2.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.1.11) Gecko/20100701 SeaMonkey/2.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.10) Gecko/20100623 Fedora/2.0.5-1.fc12 SeaMonkey/2.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.10) Gecko/20100504 Lightning/1.0b1 Mnenhy/0.8.2 SeaMonkey/2.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; ru; rv:1.9.1.10) Gecko/20100504 Lightning/1.0b1 SeaMonkey/2.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1.10) Gecko/20100504 Lightning/1.0b1 SeaMonkey/2.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de; rv:1.9.1.10) Gecko/20100504 SeaMonkey/2.0.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.9pre) Gecko/20100212 SeaMonkey/2.0.4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.9pre) Gecko/20100305 SeaMonkey/2.0.4pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.9) Gecko/20100428 Lightning/1.0b1 SeaMonkey/2.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.9) Gecko/20100317 SUSE/2.0.4-3.2 Lightning/1.0b1 SeaMonkey/2.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.9) Gecko/20100318 Mandriva/2.0.4-69.1mib2010.0 SeaMonkey/2.0.4",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.1.9) Gecko/20100317 Firefox/3.5.9 Seamonkey/2.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.9) Gecko/20100317 Lightning/1.0b1 SeaMonkey/2.0.4",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.8) Gecko/20100206 SUSE/2.0.3-0.1.1 SeaMonkey/2.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; nb-NO; rv:1.9.1.10) Gecko/20100623 Fedora/2.0.5-1.fc12 Fedora SeaMonkey/2.0.3",
	"Mozilla/5.0 (X11; U; Fedora 12 i686; nb-NO; rv:1.9.1.8) Gecko/20100205 SeaMonkey/2.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1.8) Gecko/20100205 SeaMonkey/2.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de; rv:1.9.1.8) Gecko/20100205 SeaMonkey/2.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1.7) Gecko/20100104 SeaMonkey/2.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1.7) Gecko/20100104 SeaMonkey/2.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1.7) Gecko/20100104 Firefox/3.5.8 (SeaMonkey/2.0.2)",
	"Mozilla/5.0 (X11; U; Linux ia64; de; rv:1.9.1.19) Gecko/20110429 Lightning/1.0b2pre SeaMonkey/2.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; nb-NO; rv:1.9.1.16) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.19) Gecko/20110518 SeaMonkey/2.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.19) Gecko/20110429 Gentoo/2.0.14 SeaMonkey/2.0.14",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (X11; U; Fedora i686; en-US; rv:1.9.1.19) Gecko/20110429 Fedora/2.0.14-1.fc14 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-AR; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.19) Gecko/20110420 Firefox/3.5.19 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.1.19) Gecko/20110420 SeaMonkey/2.0.14",
	"Mozilla/5.0 (Windows NT 5.0; rv:1.9.1.19) Gecko/20110420 Firefox/3.6 SeaMonkey/2.0.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.18) Gecko/20110412 Lightning/1.0b1 SeaMonkey/2.0.13",
	"Mozilla/5.0 (X11; U; Linux ia64; de; rv:1.9.1.18) Gecko/20110331 Lightning/1.0b2pre SeaMonkey/2.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (X11; U; Linux i686; de; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; nl; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; hu; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; de; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.18) Gecko/20110320 Lightning/1.0b1 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.1.18) Gecko/20110320 SeaMonkey/2.0.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.17pre) Gecko/20101211 SeaMonkey/2.0.12pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.17) Gecko/20110303 SeaMonkey/2.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.17) Gecko/20110121 SUSE/2.0.12-0.2.1 SeaMonkey/2.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.17) Gecko/20110309 Lightning/1.0b2pre SeaMonkey/2.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.17) Gecko/20110303 SeaMonkey/2.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.17) Gecko/20110123 SeaMonkey/2.0.12",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.9.1.17) Gecko/20110123 SeaMonkey/2.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-GB; rv:1.9.1.17) Gecko/20110123 SeaMonkey/2.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.9.1.17) Gecko/20110123 SeaMonkey/2.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ja; rv:1.9.1.17) Gecko/20110123 SeaMonkey/2.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1.17) Gecko/20110123 SeaMonkey/2.0.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.1.17) Gecko/20110123 SeaMonkey/2.0.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.9.1.16) Gecko/20101206 Lightning/1.0b1 SeaMonkey/2.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.16) Gecko/20101123 SeaMonkey/2.0.11",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.9.1.16) Gecko/20101124 SUSE/2.0.11-2.2 SeaMonkey/2.0.11",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.1.16) Gecko/20101123 SeaMonkey/2.0.11",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.9.1.16) Gecko/20110227 SeaMonkey/2.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.16) Gecko/20101123 SeaMonkey/2.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de; rv:1.9.1.16) Gecko/20101123 SeaMonkey/2.0.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de; rv:1.9.1.16) Gecko/20101123 SeaMonkey/2.0.11",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.1.16) Gecko/20101123 SeaMonkey/2.0.11",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.1.15) Gecko/20101027 SeaMonkey/2.0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de; rv:1.9.1.15) Gecko/20101027 SeaMonkey/2.0.10",
	"Mozilla/5.0 (X11; U; Linux x86_64; de; rv:1.9.1.6) Gecko/20091210 SUSE/2.0.1-1.1.1 SeaMonkey/2.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) Gecko/20091206 Firefox/2.0.0.20 SeaMonkey/2.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1b3pre) Gecko/20081208 SeaMonkey/2.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.9a1) Gecko/20060812 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a5pre) Gecko/20070527 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a4pre) Gecko/20070404 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a3pre) Gecko/20070317 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2pre) Gecko/20070130 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2pre) Gecko/20070109 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060906 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20060520 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9a1) Gecko/20060206 SeaMonkey/1.5a",
	"Mozilla/5.0 (OS/2; U; Warp 4.5; en-US; rv:1.9a1) Gecko/20051119 MultiZilla/1.8.1.0s SeaMonkey/1.5a",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.9a1) Gecko/20060707 SeaMonkey/1.5a",
	"Mozilla/5.0 (BeOS; U; BeOS BePC; en-US; rv:1.9a1) Gecko/20060702 SeaMonkey/1.5a",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1) Gecko/20061101 SeaMonkey/1.1b",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060301 SeaMonkey/1.1a Mnenhy/0.7.3.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1b2) Gecko/20060821 SeaMonkey/1.1a",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-FR; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9 (Ubuntu-1.1.9+nobinonly-0ubuntu1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9 (Ubuntu-1.1.9+nobinonly-0ubuntu1)",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; PL; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.13) Gecko/20080313 SeaMonkey/1.1.9",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-FR; rv:1.8.1.12) Gecko/20080209 SeaMonkey/1.1.8",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.12) Gecko/20080209 SeaMonkey/1.1.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080208 SeaMonkey/1.1.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.12) Gecko/20080201 SeaMonkey/1.1.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.6) Gecko/20070802 Firefox/2.0.0.11 SeaMonkey/1.1.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.6) Gecko/20070802 Firefox/2.0.0.11 SeaMonkey/1.1.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.12) Gecko/20080201 SeaMonkey/1.1.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.12) Gecko/20080201 MultiZilla/1.8.3.4e SeaMonkey/1.1.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.8.1.12) Gecko/20080201 SeaMonkey/1.1.8",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.12) Gecko/20080201 SeaMonkey/1.1.8",
	"Mozilla/5.0 (BeOS; U; Haiku BePC; en-US; rv:1.8.1.10pre) Gecko/20080112 SeaMonkey/1.1.7pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.11) Gecko/20071128 SeaMonkey/1.1.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.12) Gecko/20080201 SeaMonkey/1.1.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.11) Gecko/20071128 SeaMonkey/1.1.7 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.8.1.9) Gecko/20071030 SeaMonkey/1.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.8.1.9) Gecko/20071030 SeaMonkey/1.1.6 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.9) Gecko/20071030 SeaMonkey/1.1.6",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.9) Gecko/20071030 SeaMonkey/1.1.6",
	"Mozilla/5.0 (Windows; U; WinNT3.51; en-US; rv:1.8.1.8) Gecko/20071009 SeaMonkey/1.1.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-FR; rv:1.8.1.6) Gecko/20070803 SeaMonkey/1.1.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.6) Gecko/20070802 SeaMonkey/1.1.4 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.6) Gecko/20070802 SeaMonkey/1.1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.6) Gecko/20070802 SeaMonkey/1.1.4",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.6) Gecko/20070802 SeaMonkey/1.1.4",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-GB; rv:1.8.1.6) Gecko/20070802 SeaMonkey/1.1.4",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.8.1.5) Gecko/20070716 SeaMonkey/1.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.5) Gecko/20070716 SeaMonkey/1.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.5) Gecko/20070716 SeaMonkey/1.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.5) Gecko/20070716 SeaMonkey/1.1.3",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr-FR; rv:1.8.1.4) Gecko/20070528 SeaMonkey/1.1.2",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.8.1.4) Gecko/20070509 SeaMonkey/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4) Gecko/20070509 SeaMonkey/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.4) Gecko/20070509 SeaMonkey/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.4) Gecko/20070509 SeaMonkey/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8.1.4) Gecko/20070509 SeaMonkey/1.1.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.4) Gecko/20070509 SeaMonkey/1.1.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.24) Gecko/20100228 SeaMonkey/1.1.19",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.24) Gecko/20100301 SeaMonkey/1.1.19",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.23) Gecko/20090908 Fedora/1.1.18-1.fc10 SeaMonkey/1.1.18",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.23) Gecko/20090907 SeaMonkey/1.1.18",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.1.23) Gecko/20090825 SeaMonkey/1.1.18",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.23) Gecko/20090825 MultiZilla/1.8.3.4e SeaMonkey/1.1.18",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; de-AT; rv:1.8.1.23) Gecko/20090825 SeaMonkey/1.1.18",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.23) Gecko/20090823 SeaMonkey/1.1.18",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.22) Gecko/20090708 SeaMonkey/1.1.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.22) Gecko/20090624 SeaMonkey/1.1.17",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.22) Gecko/20090605 SeaMonkey/1.1.17 (Ubuntu-1.1.17+nobinonly-0ubuntu1)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it-IT; rv:1.8.1.22) Gecko/20090605 SeaMonkey/1.1.17",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.22) Gecko/20090605 SeaMonkey/1.1.17 Mnenhy/0.7.6.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.22) Gecko/20090605 SeaMonkey/1.1.17 Firefox/3.0.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8.1.22) Gecko/20090605 SeaMonkey/1.1.17",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.8.1.22) Gecko/20090605 SeaMonkey/1.1.17",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.22) Gecko/20090605 SeaMonkey/1.1.17",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.21) Gecko/20090413 SeaMonkey/1.1.16",
	"Mozilla/5.0 (X11; U; FreeBSD amd64; en-US; rv:1.8.1.21) Gecko/20090424 SeaMonkey/1.1.16",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.21) Gecko/20090403 SeaMonkey/1.1.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-AT; rv:1.8.1.21) Gecko/20090403 SeaMonkey/1.1.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.21) Gecko/20090403 SeaMonkey/1.1.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090403 SeaMonkey/1.1.16 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090403 SeaMonkey/1.1.16 Mnenhy/0.7.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090403 SeaMonkey/1.1.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090403 MultiZilla/1.8.3.4e SeaMonkey/1.1.16",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.21) Gecko/20090403 SeaMonkey/1.1.16",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.21) Gecko/20090328 Fedora/1.1.15-3.fc10 SeaMonkey/1.1.15",
	"Mozilla/5.0 (X11; U; Linux i686; ja-JP; rv:1.8.1.21) Gecko/20090322 SeaMonkey/1.1.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.21) Gecko/20090322 SeaMonkey/1.1.15",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.8.1.21) Gecko/20090322 SeaMonkey/1.1.15",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.15 Mnenhy/0.7.6.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.15 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.15",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.15 Mnenhy/0.7.6.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.15",
	"Mozilla/5.0 (AmigaOS; U; AmigaOS 1.3; en-US; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.15",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081221 SeaMonkey/1.1.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081218 SeaMonkey/1.1.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081216 SeaMonkey/1.1.14",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14 Mnenhy/0.7.6.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090303 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14 Mnenhy/0.7.6.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.19) Gecko/20081204 MultiZilla/1.8.3.5c SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (AmigaOS; U; AmigaOS 1.3; en; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (Amiga; U; AmigaOS 1.3; en; rv:1.8.1.19) Gecko/20081204 SeaMonkey/1.1.14",
	"Mozilla/5.0 (X11; U; Linux x86_64; es-ES; rv:1.8.1.18) Gecko/20081112 Fedora/1.1.13-1.fc10 SeaMonkey/1.1.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.18) Gecko/20081110 SUSE/1.1.13-1.10 SeaMonkey/1.1.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.18) Gecko/20081113 SeaMonkey/1.1.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.18) Gecko/20081113 Fedora/1.1.13-1.fc8 SeaMonkey/1.1.13",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.18) Gecko/20081030 SeaMonkey/1.1.13 (Ubuntu-1.1.13+nobinonly-0ubuntu1)",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.18) Gecko/20081031 SeaMonkey/1.1.13",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-AT; rv:1.8.1.18) Gecko/20081031 SeaMonkey/1.1.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; PL; rv:1.8.1.18) Gecko/20081031 SeaMonkey/1.1.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.18) Gecko/20081031 SeaMonkey/1.1.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.18) Gecko/20081031 SeaMonkey/1.1.13",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.18) Gecko/20081031 SeaMonkey/1.1.13",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; es-ES; rv:1.8.1.18) Gecko/20081031 SeaMonkey/1.1.13",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.17) Gecko/20080922 SUSE/1.1.12-0.1 SeaMonkey/1.1.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.17) Gecko/20080829 SeaMonkey/1.1.12 (Ubuntu-1.1.12+nobinonly-0ubuntu1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.17) Gecko/20080829 SeaMonkey/1.1.12 (Ubuntu-1.1.12+nobinonly-0ubuntu1)",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.1.17) Gecko/20080925 Fedora/1.1.12-1.fc9 SeaMonkey/1.1.12",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.17) Gecko/20080829 SeaMonkey/1.1.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17) Gecko/20090224 SeaMonkey/1.1.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17) Gecko/20080829 SeaMonkey/1.1.12 Mnenhy/0.7.5.666",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.17) Gecko/20080829 SeaMonkey/1.1.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.17) Gecko/20080829 SeaMonkey/1.1.12",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.17) Gecko/20080829 SeaMonkey/1.1.12",
	"Mozilla/5.0 (X11; U; Linux x86_64; ru-RU; rv:1.8.1.16) Gecko/20080716 Fedora/1.1.11-1.fc9 SeaMonkey/1.1.11",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.1.16) Gecko/20080716 SeaMonkey/1.1.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080716 SeaMonkey/1.1.11",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.16) Gecko/20080716 Fedora/1.1.11-1.fc8 SeaMonkey/1.1.11",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.16) Gecko/20080702 SeaMonkey/1.1.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-AT; rv:1.8.1.16) Gecko/20080702 SeaMonkey/1.1.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.8.1.16) Gecko/20080702 SeaMonkey/1.1.11 Mnenhy/0.7.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.16) Gecko/20080702 SeaMonkey/1.1.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.16) Gecko/20080702 MultiZilla/1.8.3.4e SeaMonkey/1.1.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8.1.16) Gecko/20080702 SeaMonkey/1.1.11 Mnenhy/0.7.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8.1.16) Gecko/20080702 SeaMonkey/1.1.11",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.8.1.16) Gecko/20080702 SeaMonkey/1.1.11",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.16) Gecko/20080703 SeaMonkey/1.1.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; PL; rv:1.8.1.15) Gecko/20080621 SeaMonkey/1.1.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.15) Gecko/20080621 SeaMonkey/1.1.10 Mnenhy/0.7.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.15) Gecko/20080621 SeaMonkey/1.1.10",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.15) Gecko/20080621 SeaMonkey/1.1.10",
	"Mozilla/5.0 (OS/2; U; Warp 4.5; en-US; rv:1.8.1.3pre) Gecko/20070307 SeaMonkey/1.1.1+",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.1.2) Gecko/20070224 SeaMonkey/1.1.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070309 SeaMonkey/1.1.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.1.2) Gecko/20070221 SeaMonkey/1.1.1",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); fr; rv:1.8.1.2) Gecko/20070221 SeaMonkey/1.1.1",
	"Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.8.1.2) Gecko/20070221 SeaMonkey/1.1.1",
	"Mozilla/5.0 (X11; U; Linux i586; en-US; rv:1.8.1.2) Gecko/20070227 SeaMonkey/1.1.1",
	"Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.8.1.2) Gecko/20070303 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; PL; rv:1.8.1.2) Gecko/20070222 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.1.2) Gecko/20070222 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2) Gecko/20070222 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.1.2) Gecko/20070222 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.8.1.2) Gecko/20070222 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.2) Gecko/20070222 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.2) Gecko/20070222 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.1.2) Gecko/20070221 SeaMonkey/1.1.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.1.2) Gecko/20070221 SeaMonkey/1.1.1",
	"Firefox/2.0b1 SeaMonkey/1.1.1 Mozilla/5.0 Gecko/20061101",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.1.2pre) Gecko/20070111 SeaMonkey/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.2pre) Gecko/20070111 SeaMonkey/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2pre) Gecko/20070111 SeaMonkey/1.1 Mnenhy/0.7.4.10005",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.2pre) Gecko/20070111 SeaMonkey/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.8.1.2pre) Gecko/20070111 SeaMonkey/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.2pre) Gecko/20070111 SeaMonkey/1.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.2pre) Gecko/20070111 SeaMonkey/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.0.6) Gecko/20060729 SeaMonkey/1.0.4",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.6) Gecko/20060729 SeaMonkey/1.0.4",
	"Mozilla/5.0 (OS/2; U; Warp 4.5; en-US; rv:1.8.0.6) Gecko/20060730 MultiZilla/1.8.2.0i SeaMonkey/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-GB; rv:1.8.0.5) Gecko/20060805 CentOS/1.0.3-0.el4.1.centos4 SeaMonkey/1.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en; rv:1.8.0.5) Gecko/20060721 SeaMonkey/1.0.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060619 SeaMonkey/1.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.4) Gecko/20060516 SeaMonkey/1.0.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.2) Gecko/20060630 Red Hat/1.0.1-0.1.9.EL3 SeaMonkey/1.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.2) Gecko/20060404 SeaMonkey/1.0.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.2) Gecko/20060404 SeaMonkey/1.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.0.1) Gecko/20060130 SeaMonkey/1.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060316 SUSE/1.0-27 SeaMonkey/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU; rv:1.8.0.1) Gecko/20060130 SeaMonkey/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.1) Gecko/20060130 SeaMonkey/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.0.1) Gecko/20060130 SeaMonkey/1.0",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.1) Gecko/20060130 SeaMonkey/1.0",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; en-US; rv:1.8.0.1) Gecko/20060130 SeaMonkey/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.8.1.8) Gecko/20071008 SeaMonkey",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; ja-jp) AppleWebKit/419 (KHTML, like Gecko) Shiira/1.2.3 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en_CA) AppleWebKit/522+ (KHTML, like Gecko) Shiira/1.2.3 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko) Shiira/1.2.3 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/522.10.1 (KHTML, like Gecko) Shiira/1.2.2 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko) Shiira/1.2.2 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko) Shiira/1.2.2 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; de-de) AppleWebKit/312.8 (KHTML, like Gecko) Shiira/1.2.2 Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/418.9.1 (KHTML, like Gecko) Shiira/1.2.2 Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/418.9 (KHTML, like Gecko) Shiira/1.2.2 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; pl-pl) AppleWebKit/312.8 (KHTML, like Gecko) Shiira/1.2.1 Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/417.9 (KHTML, like Gecko, Safari) Shiira/1.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/523.15.1 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/419 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/522.11.1 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/522.10.1 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/419.3 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/419.2.1 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; fr) AppleWebKit/418.9.1 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-au) AppleWebKit/523.10.3 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/419 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_2; en-us) AppleWebKit/531.21.8 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_4_11; en) AppleWebKit/528.16 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_4_11; en) AppleWebKit/525.18 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_4_11; en) AppleWebKit/525.13 (KHTML, like Gecko) Shiira Safari/125",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b5pre) Gecko/20090424 Shiretoko/3.5b5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b5pre) Gecko/20090519 Shiretoko/3.5b5pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b4pre) Gecko/20090404 Shiretoko/3.5b4pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b4pre) Gecko/20090401 Ubuntu/9.04 (jaunty) Shiretoko/3.5b4pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b4pre) Gecko/20090405 Shiretoko/3.5b4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1b4pre) Gecko/20090420 Shiretoko/3.5b4pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b4pre) Gecko/20090413 Shiretoko/3.5b4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b4pre) Gecko/20090411 Shiretoko/3.5b4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b4pre) Gecko/20090323 Shiretoko/3.5b4pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.8pre) Gecko/20100112 Shiretoko/3.5.8pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.8pre) Gecko/20100110 Shiretoko/3.5.8pre",
	"Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.9.1.6) Gecko/20091216 Shiretoko/3.5.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1.6) Gecko/20091222 Shiretoko/3.5.6 ( .NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.5pre) Gecko/20091016 Shiretoko/3.5.5pre GTB6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.5) Gecko/20100309 Ubuntu/9.04 (jaunty) Shiretoko/3.5.5",
	"Mozilla/5.0 (X11; U; Darwin i386; en-US; rv:1.9.1.4) Gecko/20100311 Shiretoko/3.5.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; zh-TW; rv:1.9.1.5) Gecko/20091106 Shiretoko/3.5.5 (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.4pre) Gecko/20090921 Ubuntu/9.04 (jaunty) Shiretoko/3.5.4pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.4pre) Gecko/20090921 Ubuntu/8.10 (intrepid) Shiretoko/3.5.4pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.3pre) Gecko/20090803 Ubuntu/9.04 (jaunty) Shiretoko/3.5.3pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.3pre) Gecko/20090730 Ubuntu/9.04 (jaunty) Shiretoko/3.5.2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.2) Gecko/20090803 Ubuntu/9.04 (jaunty) Shiretoko/3.5.2 FirePHP/0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.2) Gecko/20090805 Shiretoko/3.5.2",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.1) Gecko/20090716 Ubuntu/9.04 (jaunty) Shiretoko/3.5.1",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.1pre) Gecko/20090701 Ubuntu/9.04 (jaunty) Shiretoko/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1) Gecko/20090701 Ubuntu/9.10 (karmic) Shiretoko/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1) Gecko/20090701 Linux Mint/7 (Gloria) Shiretoko/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1) Gecko/20090630 Ubuntu/9.04 (jaunty) Shiretoko/3.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b4pre) Gecko/20090311 Ubuntu/9.04 (jaunty) Shiretoko/3.1b4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b4pre) Gecko/20090311 Shiretoko/3.1b4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b4pre) Gecko/20090307 Shiretoko/3.1b4pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-TW; rv:1.9.1b4pre) Gecko/20090308 Shiretoko/3.1b4pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; bg-BG; rv:1.9.1b4pre) Gecko/20090307 Shiretoko/3.1b4pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b3pre) Gecko/20090109 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b3pre) Gecko/20081223 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b3pre) Gecko/20081222 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b3pre) Gecko/20090106 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b3pre) Gecko/20090105 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b3pre) Gecko/20081203 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; pt-BR; rv:1.9.1b3pre) Gecko/20090103 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b3pre) Gecko/20081207 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1b3pre) Gecko/20081204 Shiretoko/3.1b3pre (.NET CLR 3.5.30729)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1b3pre) Gecko/20090105 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.1b3pre) Gecko/20090104 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl; rv:1.9.1b3pre) Gecko/20090205 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20090207 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20090121 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20090113 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20090102 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20081228 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20081221 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20081218 Shiretoko/3.1b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20081212 Shiretoko/3.1b3pre",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; .NET4.0C; .NET4.0E; Sleipnir/2.9.9)",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.0; Trident/5.0; Sleipnir/2.9.7)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2; Sleipnir/2.9.6)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB0.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 1.1.4322; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; Sleipnir/2.9.6)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; InfoPath.1; MS-RTC LM 8; Sleipnir/2.9.6)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; WOW64; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Sleipnir/2.9.4)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; InfoPath.1; Sleipnir/2.9.4)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; InfoPath.2; Sleipnir/2.9.3)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.2; Sleipnir/2.9.3)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.3; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 1.1.4322; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 2.0.50727; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; GTB6.3; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; .NET CLR 1.1.4322; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; InfoPath.2; .NET CLR 2.0.50727; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; InfoPath.2; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727; Sleipnir/2.9.2)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1; Sleipnir/2.9.1)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Sleipnir/2.9.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; OfficeLiveConnector.1.4; OfficeLivePatch.1.3; .NET CLR 3.0.30729; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618; FDM; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; GTB6; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SV1; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.2; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 4.0.20506; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; InfoPath.2; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 1.0.3705; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; msn OptimizedIE8;JAJP; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1; OfficeLiveConnector.1.3; OfficeLivePatch.0.0; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; FDM; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 1.1.4322; Sleipnir/2.8.5)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; OfficeLiveConnector.1.3; OfficeLivePatch.1.3) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; MathPlayer 2.10d; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; OfficeLiveConnector.1.3; OfficeLivePatch.1.3; .NET CLR 3.5.30729; .NET CLR 3.0.30618; .NET CLR 1.1.4322) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.2; WOW64; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; InfoPath.1; .NET CLR 2.0.50727) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; User-agent: Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1); .NET CLR 2.0.50727; .NET CLR 1.1.4322) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; User-agent: Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1); .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; FunWebProducts-AskJeevesJapan; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; FDM) Sleipnir/2.8.4",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.8.3",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0; .NET CLR 1.1.4322) Sleipnir/2.8.3",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1) Sleipnir/2.8.1",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Sleipnir/2.8.1",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; GTB5; .NET CLR 1.1.4322; .NET CLR 2.0.50727) Sleipnir/2.8.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648) Sleipnir/2.8.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727) Sleipnir/2.8.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; WOW64; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506; Media Center PC 5.0) Sleipnir/2.7.2",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727) Sleipnir/2.7.2",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) Sleipnir/2.7.2",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 4.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30) Sleipnir/2.7.1",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 2.0.50727; InfoPath.1; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648) Sleipnir/2.7.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; .NET CLR 1.1.4322) Sleipnir/2.7.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506) Sleipnir/2.6.1",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30) Sleipnir/2.6.0",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727) Sleipnir/2.6.0",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; InfoPath.1) Sleipnir/2.5.17",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152) Sleipnir/2.5.13",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; InfoPath.1; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729) Sleipnir/2.5.12",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) Sleipnir/2.5.12",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727) Sleipnir/2.49",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.0) Sleipnir/2.49",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727) Sleipnir/2.48",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322) Sleipnir/2.48",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; GTB6; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022) Sleipnir/2.46",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727) Sleipnir/2.41",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.0.3705; Media Center PC 3.1; .NET CLR 2.0.50727; .NET CLR 1.1.4322) Sleipnir/2.30",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) Sleipnir/2.30",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; InfoPath.1) Sleipnir/2.21",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322) Sleipnir/2.21",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; Sleipnir 2.8.4)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; SlimBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; SlimBrowser)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; SlimBrowser)",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; en-us) AppleWebKit/528.16 (KHTML, like Gecko) Stainless/0.5.3 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_6; es-es) AppleWebKit/525.27.1 (KHTML, like Gecko) Stainless/0.4.5 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; zh-tw) AppleWebKit/525.27.1 (KHTML, like Gecko) Stainless/0.4.5 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; en-us) AppleWebKit/525.27.1 (KHTML, like Gecko) Stainless/0.4.5 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; zh-tw) AppleWebKit/525.27.1 (KHTML, like Gecko) Stainless/0.4 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; en-us) AppleWebKit/525.27.1 (KHTML, like Gecko) Stainless/0.4 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; en-us) AppleWebKit/528.1 (KHTML, like Gecko) Stainless/0.3.5 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; zh-tw) AppleWebKit/525.18 (KHTML, like Gecko) Stainless/0.3 Safari/525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_4; en-us) AppleWebKit/528.1 (KHTML, like Gecko) Version/4.0 Safari/528.1 Stainless/0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ) Sundance/0.9x",
	"Mozilla/5.0 (compatible; Sundance/0.9x)",
	"Mozilla/5.0(Compatible; Windows; U; en-US;) Sundance/0.9.0.33",
	"Mozilla/5.0(Compatible; Windows; U; en-US;) Sundance/0.9",
	"Mozilla/6.0 (X11; U; Linux x86_64; en-US; rv:2.9.0.3) Gecko/2009022510 FreeBSD/ Sunrise/4.0.1/like Safari",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_5; ja-jp) AppleWebKit/525.18 (KHTML, like Gecko) Sunrise/1.7.5 like Safari/5525.20.1",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_4_11; en) AppleWebKit/525.18 (KHTML, like Gecko) Sunrise/1.7.4 like Safari/4525.22",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_5_2; en-us) AppleWebKit/525.18 (KHTML, like Gecko) Sunrise/1.7.1 like Safari/5525.18",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en) AppleWebKit/418.9.1 (KHTML, like Gecko) Sunrise/1.6.5 like Safari/419.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; fr) AppleWebKit/523.12.2 (KHTML, like Gecko) Sunrise/1.6.0 like Safari/523.12.2",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.7 (KHTML, like Gecko) SunriseBrowser/0.895",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.7 (KHTML, like Gecko) SunriseBrowser/0.853",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.7 (KHTML, like Gecko) SunriseBrowser/0.84",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X; en-us) AppleWebKit/125.5.7 (KHTML, like Gecko) SunriseBrowser/0.833",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.1.9) Gecko/20071110 Sylera/3.0.20 SeaMonkey/1.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.1.24) Gecko/20100228 Sylera/3.0.20 SeaMonkey/1.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.1.24) Gecko/20100228 Sylera/3.0.20 SeaMonkey/1.1.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.9) Gecko/20071110 Sylera/3.0.20 SeaMonkey/1.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.24) Gecko/20100228 Sylera/3.0.20 SeaMonkey/1.1.19",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.9) Gecko/20071110 Sylera/3.0.20 SeaMonkey/1.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.6) Gecko/20070809 Sylera/3.0.18 SeaMonkey/1.1.4",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; TencentTraveler 4.0; Trident/4.0; SLCC1; Media Center PC 5.0; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0; Trident/4.0; QQDownload 1.7; GTB6.6; TencentTraveler 4.0; SLCC1; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 3.5.30729; .NET CLR 3.0.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; iCafeMedia; TencentTraveler 4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; TencentTraveler 4.0; QQDownload 667; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; QQPinyin 686; QQDownload 661; GTB6.6; TencentTraveler 4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; Trident/4.0; QQDownload 1.7; GTB6.6; TencentTraveler 4.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; TencentTraveler 4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.2)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; TencentTraveler 4.0; GTB6.3; QQDownload 625; (R1 1.6))",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; TencentTraveler 4.0; .NET CLR 2.0.50727; AskTB5.6)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1; QQDownload 627; TencentTraveler 4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) )",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; TencentTraveler 4.0; GTB6.5; SV1; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) )",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; TencentTraveler 4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; msn OptimizedIE8;ZHCN)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; TencentTraveler 4.0; (R1 1.5); .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; QQDownload 661; GTB6.6; TencentTraveler 4.0; .NET CLR 1.0.3705; .NET CLR 1.1.4322; Media Center PC 4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; QQDownload 551; QQDownload 661; TencentTraveler 4.0; (R1 1.5))",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; Alexa Toolbar; TencentTraveler 4.0)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; QQPinyin 730; SV1; TencentTraveler 4.0; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; QQDownload 667; SV1; QQDownload 669; TencentTraveler 4.0; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 2.0.50727)",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10.5; rv:10.0.2) Gecko/20120216 Firefox/10.0.2 TenFourFox/7450",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10.4; rv:10.0.2) Gecko/20120217 Firefox/10.0.2 TenFourFox/G3",
	"Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.0; Trident/5.0; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET CLR 1.1.4322; AskTB5.6; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; Tablet PC 2.0; InfoPath.1; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; .NET4.0E; InfoPath.2; Tablet PC 2.0; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET CLR 1.1.4322; Tablet PC 2.0; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; InfoPath.3; .NET4.0C; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; InfoPath.3; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Trident/4.0; GTB6.5; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) ; InfoPath.2; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET4.0C; .NET4.0E; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; InfoPath.2; .NET CLR 2.0.50727; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; GTB6.6; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET4.0C; .NET4.0E; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; AskTbFXTV5/5.9.1.14019; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; InfoPath.2; .NET4.0C; .NET4.0E; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; (R1 1.6); .NET CLR 2.0.50727; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; SV1; TheWorld)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; Trident/5.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; TheWorld)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1pre) Gecko/20090629 Vonkeror/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/534.12 (KHTML, like Gecko) WeltweitimnetzBrowser/0.25 Safari/534.12",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/533.3 (KHTML, like Gecko) WeltweitimnetzBrowser/0.25 Safari/533.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR) AppleWebKit/532.4 (KHTML, like Gecko) WeltweitimnetzBrowser/0.25 Safari/532.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.6) Gecko/20100121 Firefox/3.5.6 Wyzo/3.5.6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.6) Gecko/20100121 Firefox/3.5.6 Wyzo/3.5.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.10) Gecko/2009042815 Firefox/3.0.10 Wyzo/3.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.0.9) Gecko/2009042410 Firefox/3.0.9 Wyzo/3.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.0.9) Gecko/2009042410 Firefox/3.0.9 Wyzo/3.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9.0.9) Gecko/2009042410 Firefox/3.0.9 Wyzo/3.0.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.0.9) Gecko/2009042410 Firefox/3.0.9 Wyzo/3.0.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.0.9) Gecko/2009042318 Firefox/3.0.9 Wyzo/3.0.3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.4; en-US; rv:1.9.0.9) Gecko/2009042318 Firefox/3.0.9 Wyzo/3.0.3 GTB6",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.4; en-US; rv:1.9.0.9) Gecko/2009042318 Firefox/3.0.9 Wyzo/3.0.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.6) Gecko/20070801 Firefox/2.0 Wyzo/0.5.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.6) Gecko/20070801 Firefox/2.0 Wyzo/0.5.3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20051219 SeaMonkey/1.0b",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8) Gecko/20060102 SeaMonkey/1.0b",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8) Gecko/20051219 SeaMonkey/1.0b",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8) Gecko/20051219 SeaMonkey/1.0b",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8) Gecko/20051219 SeaMonkey/1.0b",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8b4) Gecko/20050910 SeaMonkey/1.0a",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8b4) Gecko/20050910 SeaMonkey/1.0a",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.14eol) Gecko/20120628 CentOS/1.0.9-40.el4.centos SeaMonkey/1.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.14eol) Gecko/20101004 Red Hat/1.0.9-64.el4 SeaMonkey/1.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.14eol) Gecko/20090422 CentOS/1.0.9-0.37.el3.centos3 SeaMonkey/1.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.13pre) Gecko/20070717 Red Hat/1.0.9-4.el4 SeaMonkey/1.0.9",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070306 SeaMonkey/1.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070301 SUSE/1.8_seamonkey_1.0.8-0.1 SeaMonkey/1.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.10) Gecko/20070223 Fedora/1.0.8-0.5.1.fc5 SeaMonkey/1.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.8.0.10) Gecko/20070306 SeaMonkey/1.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.10) Gecko/20070306 SeaMonkey/1.0.8",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20070104 Red Hat/1.0.7-0.6.fc5 SeaMonkey/1.0.7",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.9) Gecko/20061211 SeaMonkey/1.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.9) Gecko/20061211 SeaMonkey/1.0.7",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.9) Gecko/20061211 SeaMonkey/1.0.7",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.8.0.9) Gecko/20061211 SeaMonkey/1.0.7",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8.0.8) Gecko/20061109 SeaMonkey/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.8.0.8) Gecko/20061029 SeaMonkey/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.8) Gecko/20061105 Red Hat/1.0.6-0.1.el3 SeaMonkey/1.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.8) Gecko/20061030 SeaMonkey/1.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.8) Gecko/20061030 MultiZilla/1.8.3.0a SeaMonkey/1.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.0.8) Gecko/20061030 SeaMonkey/1.0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-AT; rv:1.8.0.8) Gecko/20061030 SeaMonkey/1.0.6",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.8) Gecko/20061030 SeaMonkey/1.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.7) Gecko/20060914 SeaMonkey/1.0.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.7) Gecko/20060910 SeaMonkey/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.8.0.7) Gecko/20060910 SeaMonkey/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.8.0.7) Gecko/20060910 SeaMonkey/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.7) Gecko/20060910 SeaMonkey/1.0.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.7) Gecko/20060910 MultiZilla/1.7.9.0a SeaMonkey/1.0.5",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.7) Gecko/20060910 SeaMonkey/1.0.5",
	"Mozilla/5.0 (OS/2; U; Warp 4.5; en-US; rv:1.8.0.7) Gecko/20060910 MultiZilla/1.8.2.0i SeaMonkey/1.0.5",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.8.0.7) Gecko/20060910 SeaMonkey/1.0.5",
	"Mozilla/5.0 (X11; U; Linux ppc; en-US; rv:1.8.0.6) Gecko/20060815 SeaMonkey/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060803 SeaMonkey/1.0.4",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.6) Gecko/20060730 SeaMonkey/1.0.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; e5.1; en-US; rv:1.9.1b5pre) Gecko/20090428 SeaMonkey/2.0b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.1b4pre) Gecko/20090419 SeaMonkey/2.0b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1.1pre) Gecko/20090717 SeaMonkey/2.0b1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1b3pre) Gecko/20081208 SeaMonkey/2.0a3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1b3pre) Gecko/20081208 SeaMonkey/2.0a3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9.2a1pre) Gecko/20081228 SeaMonkey/2.0a3pre",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X 10.4; en-US; rv:1.9.1b3pre) Gecko/20090223 SeaMonkey/2.0a3",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.4; en-US; rv:1.9.1b3pre) Gecko/20090223 SeaMonkey/2.0a3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b1pre) Gecko/20080926001251 SeaMonkey/2.0a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:1.9.1b3pre) Gecko/20081202 SeaMonkey/2.0a2",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.1b3pre) Gecko/20081202 SeaMonkey/2.0a2",
	"Mozilla/5.0 (X11; U; Linux i686; rv:1.9.1a2pre) Gecko/20080824052448 SeaMonkey/2.0a1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9pre) Gecko/2008061501 SeaMonkey/2.0a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:1.9b3pre) Gecko/2008010602 SeaMonkey/2.0a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a6pre) Gecko/20070625 SeaMonkey/2.0a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a5pre) Gecko/20070529 SeaMonkey/2.0a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr; rv:1.9b4pre) Gecko/2008022502 SeaMonkey/2.0a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de; rv:1.9.1b1pre) Gecko/20080925121544 SeaMonkey/2.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.14) Gecko/20100930 SeaMonkey/2.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.9.1.14) Gecko/20100930 SeaMonkey/2.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.14) Gecko/20100930 SeaMonkey/2.0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; nl; rv:1.9.1.13) Gecko/20100914 Lightning/1.0b1 SeaMonkey/2.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.13) Gecko/20100914 Mnenhy/0.8.3 SeaMonkey/2.0.8",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.9.1.12) Gecko/20100825 SeaMonkey/2.0.7",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100630 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100629 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100627 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100625 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100623 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100622 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100617 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.1.11pre) Gecko/20100605 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.11pre) Gecko/20100515 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.11pre) Gecko/20100508 SeaMonkey/2.0.6pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.11) Gecko/20100721 SeaMonkey/2.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.11) Gecko/20100720 Fedora/2.0.6-1.fc12 SeaMonkey/2.0.6",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1.11) Gecko/20100714 SUSE/2.0.6-2.1 SeaMonkey/2.0.6",
	"Mozilla/5.0 (X11; U; Linux ia64; de; rv:1.9.1.11) Gecko/20100820 Lightning/1.0b2pre SeaMonkey/2.0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.11) Gecko/20100722 SeaMonkey/2.0.6",
	"Mozilla/5..0) Gecko/20120715 Firefox/14.0.1 SeaMonkey/2.11 Lightning/1.6",
	"Mozilla/5.0 (X11; Linux i686; rv:2.2a1pre) Gecko/20110324 SeaMonkey/2.1b3pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b13pre) Gecko/20110321 SeaMonkey/2.1b3pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b13pre) Gecko/20110316 SeaMonkey/2.1b3pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b12pre) Gecko/20110204 SeaMonkey/2.1b3pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b13pre) Gecko/20110317 SeaMonkey/2.1b3pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b9pre) Gecko/20110101 SeaMonkey/2.1b2pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b9pre) Gecko/20101231 SeaMonkey/2.1b2pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b9pre) Gecko/20101230 SeaMonkey/2.1b2pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b9pre) Gecko/20110110 SeaMonkey/2.1b2pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b9pre) Gecko/20101231 SeaMonkey/2.1b2pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b8pre) Gecko/20101028 SeaMonkey/2.1b2pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b8pre) Gecko/20101014 SeaMonkey/2.1b2pre",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:2.0b11) Gecko/20110209 Firefox/ SeaMonkey/2.1b2",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b7pre) Gecko/20100915 Firefox/4.0b7pre SeaMonkey/2.1b1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.0b5pre) Gecko/20100830 SeaMonkey/2.1b1pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0b7pre) Gecko/20101008 Firefox/4.0b7pre SeaMonkey/2.1b1",
	"Mozilla/5.0 (Windows; Windows NT 6.1; rv:2.0b2pre) Gecko/20100720 SeaMonkey/2.1a3pre",
	"Mozilla/5.0 (Windows; Windows NT 5.2; rv:2.0b3pre) Gecko/20100803 SeaMonkey/2.1a3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.3a4pre) Gecko/20100404 SeaMonkey/2.1a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-CA; rv:1.9.3a3pre) Gecko/20100312 SeaMonkey/2.1a1pre",
	"Mozilla/5.0 (Windows NT 5.1; rv:13.0) Gecko/20120615 Firefox/13.0.1 SeaMonkey/2.10.1 Lightning/1.5.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:13.0) Gecko/20120615 Firefox/13.0.1 SeaMonkey/2.10.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:13.0) Gecko/20120604 Firefox/13.0 SeaMonkey/2.10",
	"Mozilla/5.0 (X11; Linux i686; rv:13.0) Gecko/20120604 Firefox/13.0 SeaMonkey/2.10 Lightning/1.5.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:2.0.1) Gecko/20110609 Firefox/4.0.1 SeaMonkey/2.1",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0.1) Gecko/20110608 Firefox/4.0.1 SeaMonkey/2.1 Lightning/1.0b4pre",
	"Mozilla/5.0 (X11; Linux i686; rv:2.0.1) Gecko/20110608 Firefox/4.0.1 SeaMonkey/2.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0.1) Gecko/20110608 SeaMonkey/2.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:2.0.1) Gecko/20110608 Firefox/4.0.1 SeaMonkey/2.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:2.0.1) Gecko/20110608 Firefox/4.0.1 SeaMonkey/2.1 Lightning/1.0b4pre",
	"Mozilla/5.0 (Windows NT 5.1; rv:2.0.1) Gecko/20110608 Firefox/4.0.1 SeaMonkey/2.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:2.0.1) Gecko/20110511 Firefox/4.0.1 SeaMonkey/2.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:1.9.2.8) Gecko/20110608 Firefox/3.6.8 Seamonkey/2.1",
	"Mozilla/5.0 (Windows NT 5.0; rv:2.0.1) Gecko/20110608 Firefox/4.0.1 SeaMonkey/2.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:2.0.1) Gecko/20110608 SeaMonkey/2.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:1.9.1.x) Gecko/20110606 SeaMonkey/2.x Firefox/5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.2pre) Gecko/20090723 SeaMonkey/2.0b2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1b3pre) Gecko/20090302 SeaMonkey/2.0b1pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1b4pre) Gecko/20090405 SeaMonkey/2.0b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; rv:1.9.1b4pre) Gecko/20090419 SeaMonkey/2.0b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; ru; rv:1.9.1b4pre) Gecko/20090419 SeaMonkey/2.0b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en; rv:1.9.1b4pre) Gecko/20090419 SeaMonkey/2.0b1pre",
	"Mozilla/5.0 (Windows; U; Windows NT e",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110605 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110603 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110602 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110601 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110530 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110529 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110527 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110526 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110525 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:7.0a1) Gecko/20110524 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:6.0a1) Gecko/20110512 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 5.2; rv:2.2a1pre) Gecko/20110327 SeaMonkey/2.2a1pre",
	"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:23.0) Gecko/20100101 Firefox/23.0 SeaMonkey/2.20",
	"Mozilla/5.0 (Windows NT 5.0; rv:5.0) Gecko/20110706 Firefox/5.0 SeaMonkey/2.2",
	"Mozilla/5.0 (Windows NT 6.1; rv:21.0) Gecko/20100101 Firefox/21.0 SeaMonkey/2.18a1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:20.0) Gecko/20100101 Firefox/20.0 SeaMonkey/2.17",
	"Mozilla/5.0 (Windows NT 5.2; rv:20.0) Gecko/20100101 Firefox/20.0 SeaMonkey/2.17",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.7; rv:19.0) Gecko/20100101 Firefox/19.0 SeaMonkey/2.16.2",
	"Mozilla/5.0 (Windows NT 6.0; rv:19.0) Gecko/20100101 Firefox/19.0 SeaMonkey/2.16.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:19.0) Gecko/20100101 Firefox/19.0 SeaMonkey/2.16",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:16.0) Gecko/20121011 Firefox/16.0 SeaMonkey/2.13.1 Lightning/1.8",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.7; rv:16.0) Gecko/20121011 Firefox/16.0 SeaMonkey/2.13.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:16.0) Gecko/20121011 Firefox/16.0 SeaMonkey/2.13.1",
	"Mozilla/5.0 (X11; Linux i686; rv:16.0) Gecko/20120906 SeaMonkey/2.13",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:16.0) Gecko/20120830 Firefox/16.0 SeaMonkey/2.13 Lightning/1.8b1",
	"Mozilla/5.0 (Windows NT 5.1; rv:16.0) Gecko/20121007 SeaMonkey/2.13",
	"Mozilla/5.0 (Windows NT 5.1; rv:16.0) Gecko/20121007 Firefox/16.0 SeaMonkey/2.13",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10.5; rv:16.0) Gecko/20121009 Firefox/16.0 SeaMonkey/2.13",
	"Mozilla/5.0 (X11; Linux x86_64; rv:15.0) Gecko/20120909 SeaMonkey/2.12.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (X11; Linux i686; rv:15.0) Gecko/20120910 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (X11; Linux i686; rv:15.0) Gecko/20120909 SeaMonkey/2.12.1",
	"Mozilla/5.0 (X11; Linux i686; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Windows NT 6.0; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Windows NT 5.2; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:15.0) Gecko/20120909 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X 10.5; rv:15.0) Gecko/20120910 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:15.0) Gecko/20120909 SeaMonkey/2.12.1",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:15.0) Gecko/20120909 Firefox/15.0.1 SeaMonkey/2.12.1",
	"Mozilla/5.0 (X11; Linux x86_64; rv:15.0) Gecko/20120826 Firefox/15.0 SeaMonkey/2.12 Lightning/1.7",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:15.0) Gecko/20120826 Firefox/15.0 SeaMonkey/2.12",
	"Mozilla/5.0 (X11; Linux x86_64; rv:14.0) Gecko/20120817 SeaMonkey/2.11",
	"Mozilla/5.0 (X11; Linux x86_64; rv:14.0) Gecko/20120817 Firefox/14.0.1 SeaMonkey/2.11",
	"Mozilla/5.0 (Windows NT 6.1; rv:14082705 Minefield/3.0a8pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X; en-US; rv:1.9a8pre) Gecko/2007083104 Minefield/3.0a8pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a7pre) Gecko/2007073105 Minefield/3.0a7pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9a6pre) Gecko/20070615 Minefield/3.0a6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a6pre) Gecko/20070630 Minefield/3.0a6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a6pre) Gecko/20070626 Minefield/3.0a6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a6pre) Gecko/20070622 Minefield/3.0a6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a6pre) Gecko/20070604 Minefield/3.0a6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a6pre) Gecko/20070603 Minefield/3.0a6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a6pre) Gecko/20070602 Minefield/3.0a6pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9a5pre) Gecko/20070428 Minefield/3.0a5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a5pre) Gecko/20070529 Minefield/3.0a5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a5pre) Gecko/20070517 Minefield/3.0a5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9a4pre) Gecko/20070427 Minefield/3.0a4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a4pre) Gecko/20070427 Minefield/3.0a4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a4pre) Gecko/20070416 Minefield/3.0a4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a4pre) Gecko/20070407 Minefield/3.0a4pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a3pre) Gecko/20070301 Minefield/3.0a3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9a3pre) Gecko/20070320 Minefield/3.0a3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a3pre) Gecko/20070218 Minefield/3.0a3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a3) Gecko/20070328 Minefield/3.0a3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2pre) Gecko/20070204 Minefield/3.0a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2pre) Gecko/20070105 Minefield/3.0a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2pre) Gecko/20061231 Minefield/3.0a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2pre) Gecko/20061230 Minefield/3.0a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a2pre) Gecko/20061221 Minefield/3.0a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9a2pre) Gecko/20061225 Minefield/3.0a2pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a2) Gecko/20070221 Minefield/3.0a2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20070308 Minefield/3.0a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20061111 Minefield/3.0a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20061016 Minefield/3.0a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20060819 Minefield/3.0a1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a1) Gecko/20060609 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0 x64; en-US; rv:1.9a1) Gecko/20061007 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.9a1) Gecko/20060926 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2 x64; en-US; rv:1.9a1) Gecko/20061007 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061217 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061129 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061125 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061124 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/20061123 Minefield/3.0a1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a1) Gecko/2Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.1a2pre) Gecko/2008080205 Minefield/3.1a2pre",
	"Mozilla/5.0 (X11; U; Linux i686; de-DE; rv:1.9.1a2pre) Gecko/20080826020557 Minefield/3.1a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1a2pre) Gecko/2008072403 Minefield/3.1a2pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9.1a2pre) Gecko/20080826052737 Minefield/3.1a2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9.1a1pre) Gecko/2008071403 Minefield/3.1a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1a1pre) Gecko/2008071603 Firefox Minefield/3.1a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1a1pre) Gecko/2008071003 Minefield/3.1a1pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1a1pre) Gecko/2008062005 Minefield/3.1a1pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9pre) Gecko/2008042312 Minefield/3.0pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9pre) Gecko/2008061504 Minefield/3.0pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9pre) Gecko/2008032621 Fedora/3.0-0.49.cvs20080326.fc9 Minefield/3.0pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9pre) Gecko/2008041506 Minefield/3.0pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9pre) Gecko/2008041406 Minefield/3.0pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9pre) Gecko/2008040907 Minefield/3.0pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9pre) Gecko/2008032904 Minefield/3.0pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b5pre) Gecko/2008032204 Minefield/3.0b5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b5pre) Gecko/2008031004 Minefield/3.0b5pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b5pre) Gecko/2008030706 Minefield/3.0b5pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b4pre) Gecko/2008022304 Minefield/3.0b4pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b4pre) Gecko/2008021304 Minefield/3.0b4pre",
	"Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.5; en-US; rv:1.9b4pre) Gecko/2008022104 Minefield/3.0b4pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b3pre) Gecko/2008010404 Minefield/3.0b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr; rv:1.9b3pre) Gecko/2008011205 Minefield/3.0b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9b3pre) Gecko/2007121805 Minefield/3.0b3pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b3pre) Gecko/2007122205 Minefield/3.0b3pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b3) Gecko/2008021322 Minefield/3.0b3",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b2pre) Gecko/2007112704 Minefield/3.0b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9b2pre) Gecko/2007112619 Minefield/3.0b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b2pre) Gecko/2007120505 Minefield/3.0b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b2pre) Gecko/2007120405 Minefield/3.0b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b2pre) Gecko/2007111605 Minefield/3.0b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9b2pre) Gecko/2007110805 Minefield/3.0b2pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.9b2pre) Gecko/2007110913 Minefield/3.0b2pre",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9b2) Gecko/2007122607 Minefield/3.0b2",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9b2) Gecko/2008011913 Minefield/3.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.9a9pre) Gecko/2007092705 Minefield/3.0a9pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a9pre) Gecko/2007110705 Minefield/3.0a9pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a9pre) Gecko/2007102105 Minefield/3.0a9pre",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9a8pre) Gecko/2007092004 Minefield/3.0a8pre",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0 x64; en-US; rv:1.9a8pre) Gecko/2007090213 Minefield/3.0a8pre",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9a8pre) Gecko/2007Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.0; Trident/4.0; Maxthon; SV1; .NET CLR 1.1.4322; .NET CLR 2.4.84947; SLCC1; Media Center PC 4.0; Zune 3.5; Tablet PC 3.5; InfoPath.3)",
	"Mozilla/5.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 3.0.04320)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; Maxthon; Win64; x64; Trident/4.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 2.0.50727; InfoPath.2; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 2.0.50727; .NET CLR 1.1.4322; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022; InfoPath.2; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Maxthon; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1; Trident/4.0; Maxthon)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; Maxthon; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 1.1.4322; .NET CLR 3.5.21022; .NET CLR 3.0.30618; .NET CLR 3.5.30729; OfficeLiveConnector.1.3; OfficeLivePatch.0.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; Trident/4.0; Maxthon; SLCC1; .NET CLR 1.1.4322; .NET CLR 2.0./20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5 (+http://www.kangaroo-personal.de)",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.12) Gecko/20080203 K-Meleon/1.1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-DE; rv:1.8.1.12) Gecko/20080203 K-Meleon/1.1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU; rv:1.8.1.12) Gecko/20080203 K-Meleon/1.1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.12) Gecko/20080203 K-Meleon/1.1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.8.1.12) Gecko/20080203 K-Meleon/1.1.4",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.10) Gecko/20071116 K-Meleon/1.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.10) Gecko/20071116 K-Meleon/1.1.3",
	"Mozilla/5.0 (Windows; U; Win 9x 4.90; es-ES; rv:1.8.1.10) Gecko/20071116 K-Meleon/1.1.3",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.6) Gecko/20070727 K-Meleon/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.6) Gecko/20070727 K-Meleon/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.1.6) Gecko/20070727 K-Meleon/1.1.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:1.8.1.4) Gecko/20070511 K-Meleon/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-AR; rv:1.8.1.4) Gecko/20070511 K-Meleon/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.4) Gecko/20070511 K-Meleon/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.8.1.4) Gecko/20070511 K-Meleon/1.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.21) Gecko/20090403 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.2) Gecko/20070222 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ko-KR; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-AR; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.0.7) Gecko/20060917 K-Meleon/1.02",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.0.6) Gecko/20060730 K-Meleon/1.01",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.6) Gecko/20060730 K-Meleon/1.01",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; de-AT; rv:1.8.0.5) Gecko/20060706 K-Meleon/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.0.5) Gecko/20060706 K-Meleon/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.0.1) Gecko/20060115 K-Meleon/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.0.5) Gecko/20060730 K-Meleon/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-AT; rv:1.8.0.5) Gecko/20060706 K-Meleon/1.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.2pre) Gecko/20070221 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.7.12) Gecko/20050915 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.5) Gecko/20041220 K-Meleon/0.9",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; de-DE; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-PT; rv:1.8.1.21) Gecko/20090303 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.8.1.21) Gecko/20090403 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pt-BR; rv:1.8.1.21) Gecko/20090303 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; pl-PL; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.22pre) Gecko/20090502 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.21) Gecko/20090403 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-GB; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.19) Gecko/20081217 K-Meleon/1.5.2",
	"Mozilla/5.0 (Darwin; FreeBSD 5.6; en-GB; rv:1.9.1b3pre)Gecko/20081211 K-Meleon/1.5.2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; ru-RU; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; fr-FR; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.17) Gecko/20080919 K-Meleon/1.5.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.5.0beta",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.5.0b2",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-ES; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; es-ES; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; fr-FR; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Darwin; FreeBSD 5.6; en-GB; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.5.0",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.8) Gecko/20071013 K-Meleon/1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-EN; rv:1.8.1.8) Gecko/20071013 K-Meleon/1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.8.1.8) Gecko/20071013 K-Meleon/1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.5) Gecko/20070722 K-Meleon/1.11",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.1.6",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.8.1.17pre) Gecko/20080716 K-Meleon/1.1.6",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; fr-FR; rv:1.8.1.14) Gecko/20080406 K-Meleon/1.1.5",
	"Mozilla/5.0 (Windows; U; Windows NT 6.0; es-ES; rv:1.8.1.14) GeckoMozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.1) Gecko/20060313 Fedora/1.5.0.1-9 Firefox/1.5.0.1 pango-text Mnenhy/0.7.3.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.1) Gecko/20060201 Firefox/1.5.0.1 (Swiftfox) Mnenhy/0.7.3.0",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.1) Gecko/20060124 Firefox/1.5.0.1 Ubuntu",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8.0.1) Gecko/20060124 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; pl-PL; rv:1.8.0.1) Gecko/20060313 Fedora/1.5.0.1-9 Firefox/1.5.0.1 pango-text Mnenhy/0.7.3.0",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8.0.1) Gecko/20060124 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8.0.1) Gecko/20060124 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; es-ES; rv:1.8.0.1) Gecko/20060124 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.7) Gecko/20060911 Red Hat/1.5.0.7-0.1.el4 Firefox/1.5.0.1 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060404 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060324 Ubuntu/dapper Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060313 Fedora/1.5.0.1-9 Firefox/1.5.0.1 pango-text",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8.0.1) Gecko/20060313 Debian/1.5.dfsg+1.5.0.1-4 Firefox/1.5.0.1",
	"Mozilla/5.0 (X11; Linux i686; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0",
	"Mozilla/5.0 (Windows NT 5.2; U; de; rv:1.8.0) Gecko/20060728 Firefox/1.5.0",
	"Mozilla/5.0 (Windows NT 5.1; U; tr; rv:1.8.0) Gecko/20060728 Firefox/1.5.0",
	"Mozilla/5.0 (Windows NT 5.1; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0",
	"Mozilla/5.0 (Windows NT 5.1; U; de; rv:1.8.0) Gecko/20060728 Firefox/1.5.0",
	"Mozilla/5.0 (Windows 98; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0",
	"Mozilla/5.0 (Macintosh; PPC Mac OS X; U; en; rv:1.8.0) Gecko/20060728 Firefox/1.5.0",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.8) Gecko/20051130 Firefox/1.5",
	"Mozilla/5.0 (X11; U; NetBSD i386; en-US; rv:1.8) Gecko/20060104 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; fr; rv:1.8) Gecko/20051231 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8) Gecko/20051212 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.8) Gecko/20051201 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; pt-BR; rv:1.8) Gecko/20051111 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8) Gecko/20051111 Firefox/1.5 Ubuntu",
	"Mozilla/5.0 (X11; U; Linux i686; pl; rv:1.8) Gecko/20051111 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; lt; rv:1.6) Gecko/20051114 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; lt-LT; rv:1.6) Gecko/20051114 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; it; rv:1.8) Gecko/20060113 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8) Gecko/20060110 Debian/1.5.dfsg-4 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; fr; rv:1.8) Gecko/20051111 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; fr-FR; rv:1.8) Gecko/20051111 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060806 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060130 Ubuntu/1.5.dfsg-4ubuntu6 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060119 Debian/1.5.dfsg-4ubuntu3 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060118 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060111 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8) Gecko/20060110 Debian/1.5.dfsg-4 Firefox/1.5",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8b5) Gecko/20051008 Fedora/1.5-0.5.0.beta2 Firefox/1.4.1",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.8b5) Gecko/20051006 Firefox/1.4.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.2; en-US; rv:1.8b5) Gecko/20051006 Firefox/1.4.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; tr; rv:1.8b5) Gecko/20051006 Firefox/1.4.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; it; rv:1.8b5) Gecko/20051006 Firefox/1.4.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.8b5) Gecko/20051006 Firefox/1.4.1",
	"Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-.0.0",
	"Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:21.0) Gecko/20130331 Firefox/21.0",
	"Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:21.0) Gecko/20100101 Firefox/21.0",
	"Mozilla/5.0 (X11; Linux i686; rv:21.0) Gecko/20100101 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.2; rv:21.0) Gecko/20130326 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:21.0) Gecko/20130401 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:21.0) Gecko/20130331 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:21.0) Gecko/20130330 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:21.0) Gecko/20100101 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:21.0) Gecko/20130401 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:21.0) Gecko/20130328 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:21.0) Gecko/20100101 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:21.0) Gecko/20130401 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:21.0) Gecko/20130331 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:21.0) Gecko/20100101 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 5.0; rv:21.0) Gecko/20100101 Firefox/21.0",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.8; rv:21.0) Gecko/20100101 Firefox/21.0",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64;) Gecko/20100101 Firefox/20.0",
	"Mozilla/5.0 (Windows x86; rv:19.0) Gecko/20100101 Firefox/19.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:6.0) Gecko/20100101 Firefox/19.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:14.0) Gecko/20100101 Firefox/18.0.1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:18.0) Gecko/20100101 Firefox/18.0",
	"Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:17.0) Gecko/20100101 Firefox/17.0.6",
	"Mozilla/5.0 (X11; Ubuntu; Linux armv7l; rv:17.0) Gecko/20100101 Firefox/17.0",
	"Mozilla/6.0 (Windows NT 6.2; WOW64; rv:16.0.1) Gecko/20121011 Firefox/16.0.1",
	"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:16.0.1) Gecko/20121011 Firefox/16.0.1",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64; rv:16.0.1) Gecko/20121011 Firefox/16.0.1",
	"Mozilla/5.0 (X11; NetBSD amd64; rv:16.0) Gecko/20121102 Firefox/16.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:15.0) Gecko/20120716 Firefox/15.0a2",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.9.1.16) Gecko/20120427 Firefox/15.0a1",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:15.0) Gecko/20120427 Firefox/15.0a1",
	"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:15.0) Gecko/20120910144328 Firefox/15.0.2",
	"Mozilla/5.0 (X11; Ubuntu; Linux i686; rv:15.0) Gecko/20100101 Firefox/15.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; rv:15.0) Gecko/20121011 Firefox/15.0.1",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:14.0) Gecko/20120405 Firefox/14.0a1",
	"Mozilla/5.0 (Windows NT 6.1; rv:14.0) Gecko/20120405 Firefox/14.0a1",
	"Mozilla/5.0 (Windows NT 5.1; rv:14.0) Gecko/20120405 Firefox/14.0a1",
	"Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:14.0) Gecko/20100101 Firefox/14.0.1",
	"Mozilla/5.0 (X11; Ubuntu; Linux i686; rv:14.0) Gecko/20100101 Firefox/14.0.1",
	"Mozilla/5.0 (Windows; U; Windows NT 6.1; WOW64; en-US; rv:2.0.4) Gecko/20120718 AskTbAVR-IDW/3.12.5.17700 Firefox/14.0.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:12.0) Gecko/20120403211507 Firefox/14.0.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:12.0) Gecko/ 20120405 Firefox/14.0.1",
	"Mozilla/5.0 (Windows NT 6.0; rv:14.0) Gecko/20100101 Firefox/14.0.1",
	"Mozilla/5.0 (Windows NT 5.1; rv:15.0) Gecko/20100101 Firefox/13.0.1",
	"Mozilla/5.0 (Windows NT 6.1; rv:12.0) Gecko/20120403211507 Firefox/12.0",
	"Mozilla/5.0 (Windows NT 6.1; de;rv:12.0) Gecko/20120403211507 Firefox/12.0",
	"Mozilla/5.0 (Windows NT 5.1; rv:12.0) Gecko/20120403211507 Firefox/12.0",
	"Mozilla/5.0 (compatible; Windows; U; Windows NT 6.2; WOW64; en-US; rv:12.0) Gecko/20120403211507 Firefox/12.0",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.1.16) Gecko/20120421 Gecko Firefox/11.0",
	"Mozilla/5.0 (X11; U; Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5a) Gecko/20030728 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.5a) Gecko/20030728 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.5a) Gecko/20030728 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.5a) Gecko/20030728 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.5a) Gecko/20030728 Mozilla Firebird/0.6.1",
	"Mozilla/5.0 (X11; U; SunOS sun4u; en-US; rv:1.4b) Gecko/20030517 Mozilla Firebird/0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4b) Gecko/20030630 Mozilla Firebird/0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4b) Gecko/20030607 Mozilla Firebird/0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4b) Gecko/20030516 Mozilla Firebird/0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4b) Gecko/20030505 Mozilla Firebird/0.6",
	"Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.4a) Gecko/20030425 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; WinNT4.0; en-US; rv:1.4b) Gecko/20030610 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:x.xx) Gecko/20030504 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5a) Gecko/20030702 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.5a) Gecko/20030630 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4b) Gecko/20030615 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4b) Gecko/20030516 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.4b) Gecko/20030503 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.1; de-DE; rv:1.4b) Gecko/20030516 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4b) Gecko/20030516 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4b) Gecko/20030514 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; en-US; rv:1.4b) Gecko/20030504 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Windows NT 5.0; de-DE; rv:1.4b) Gecko/20030516 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Win98; en-US; rv:1.4b) Gecko/20030516 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows; U; Win98; de-DE; rv:1.4b) Gecko/20030516 Mozilla Firebird/0.6",
	"Mozilla/5.0 (Windows NT 5.1; rv:31.0) Gecko/20100101 Firefox/31.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:29.0) Gecko/20120101 Firefox/29.0",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:25.0) Gecko/20100101 Firefox/29.0",
	"Mozilla/5.0 (X11; OpenBSD amd64; rv:28.0) Gecko/20100101 Firefox/28.0",
	"Mozilla/5.0 (X11; Linux x86_64; rv:28.0) Gecko/20100101 Firefox/28.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:27.3) Gecko/20130101 Firefox/27.3",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64; rv:27.0) Gecko/20121011 Firefox/27.0",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:25.0) Gecko/20100101 Firefox/25.0",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:25.0) Gecko/20100101 Firefox/25.0",
	"Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:24.0) Gecko/20100101 Firefox/24.0",
	"Mozilla/5.0 (Windows NT 6.0; WOW64; rv:24.0) Gecko/20100101 Firefox/24.0",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.8; rv:24.0) Gecko/20100101 Firefox/24.0",
	"Mozilla/5.0 (Windows NT 6.2; rv:22.0) Gecko/20130405 Firefox/23.0",
	"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:23.0) Gecko/20130406 Firefox/23.0",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:23.0) Gecko/20131011 Firefox/23.0",
	"Mozilla/5.0 (Windows NT 6.2; rv:22.0) Gecko/20130405 Firefox/22.0",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:22.0) Gecko/20130328 Firefox/22.0",
	"Mozilla/5.0 (Windows NT 6.1; rv:22.0) Gecko/20130405 Firefox/22.0",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64; rv:16.0.1) Gecko/20121011 Firefox/21.0.1",
	"Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:16.0.1) Gecko/20121011 Firefox/21.0.1",
	"Mozilla/5.0 (Windows NT 6.2; Win64; x64; rv:21.0.0) Gecko/20121011 Firefox/21Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.43; Windows NT 5.1; .NET CLR 1.0.3705; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.42; Windows NT 5.1; Trident/4.0; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; .NET CLR 3.0.04506.648; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.40; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.40; Windows NT 6.0; FunWebProducts; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.40; Windows NT 5.1; Trident/4.0; GTB6; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.40; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.36; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.30618; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.36; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30618; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.5; AOLBuild 4337.36; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.5.30729; .NET CLR 3.0.30618)",
	"Mozilla/5.0 (compatible; MSIE 9.0; AOL 9.1; AOLBuild 4334.5012; Windows NT 6.0; WOW64; Trident/5.0)",
	"Mozilla/4.0 (compatible; MSIE 8.0; AOL 9.1; AOLBuild 4334.5011; Windows NT 6.1; WOW64; Trident/4.0; GTB7.2; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; .NET4.0C)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5010; Windows NT 6.0; Trident/4.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.30729; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5009; Windows NT 5.1; GTB5; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5006; Windows NT 5.1; Trident/4.0; DigExt; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5006; Windows NT 5.1; Trident/4.0; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5006; Windows NT 5.1; GTB5; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5006; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 1.0.3705; .NET CLR 2.0.50727; .NET CLR 3.0.4506.2152; .NET CLR 3.5.30729)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5000; Windows NT 5.1; Trident/4.0)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.5000; Windows NT 5.1; Media Center PC 3.0; .NET CLR 1.0.3705; .NET CLR 1.1.4322; InfoPath.1)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.36; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.34; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; Media Center PC 5.0; .NET CLR 3.0.04506; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.34; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.34; Windows NT 5.1; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.34; Windows NT 5.1; .NET CLR 1.0.3705; .NET CLR 1.1.4322)",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.27; Windows NT 6.0; WOW64; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506; Media Center PC 5.0); UnAuth-State",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.27; Windows NT 6.0; SLCC1; .NET CLR 2.0.50727; .NET CLR 3.0.04506); UnAuth-State",
	"Mozilla/4.0 (compatible; MSIE 7.0; AOL 9.1; AOLBuild 4334.27; Windows NT 5.1; .NET CLR 1.1.4322; .NET CLR 2.0.50727; .NET CLR 3.0.04506.30; InfoPath.1); UnAuth-State"
};

char *getRandUserAgent ( int32_t urlIp , int32_t proxyIp , int32_t proxyPort ) {
	// it is a function of the website ip and the proxy ip
	uint32_t x = (uint32_t)urlIp;
	x = hash32h ( x , proxyIp );
	x = hash32h ( x , proxyPort );
	// // get count
	int32_t numAgents = sizeof(s_agentList)/sizeof(char *);
	// // select from the list then
	int32_t n = x % numAgents;

	// just make it random all the time
	//int32_t n = rand() % numAgents;

	//log("urlip=%"UINT32" proxyip=%"UINT32" proxyport=%"UINT32" n=%"INT32"",
	//    urlIp,proxyIp,proxyPort,n);

	return s_agentList[n];
}

bool printHammerQueueTable ( SafeBuf *sb ) {

	char *title = "Queued Download Requests";
	sb->safePrintf ( 
			 "<table %s>"
			 "<tr class=hdrow><td colspan=19>"
			 "<center>"
			 "<b>%s</b>"
			 "</td></tr>"

			 "<tr bgcolor=#%s>"
			 "<td><b>#</td>"
			 "<td><b>age</td>"
			 "<td><b>first ip found</td>"
			 "<td><b>actual ip</td>"
			 "<td><b>crawlDelayMS</td>"
			 "<td><b># proxies banning</td>"
			 
			 "<td><b>coll</td>"
			 "<td><b>url</td>"

			 "</tr>\n"
			 , TABLE_STYLE
			 , title 
			 , DARK_BLUE
			 );

	Msg13Request *r = s_hammerQueueHead ;

	int32_t count = 0;
	int64_t nowms = gettimeofdayInMilliseconds();

 loop:
	if ( ! r ) return true;

	// print row
	sb->safePrintf( "<tr bgcolor=#%s>"
		       "<td>%i</td>" // #
		       "<td>%ims</td>" // age in hammer queue
		       "<td>%s</td>"
			,LIGHT_BLUE
		       ,(int)count
		       ,(int)(nowms - r->m_stored)
		       ,iptoa(r->m_firstIp)
		       );

	sb->safePrintf("<td>%s</td>" // actual ip
		       , iptoa(r->m_urlIp));

	// print crawl delay as link to robots.txt
	sb->safePrintf( "<td><a href=\"");
	Url cu;
	cu.set ( r->ptr_url );
	bool isHttps = false;
	if ( cu.m_url && cu.m_url[4] == 's' ) isHttps = true;
	if ( isHttps ) sb->safeStrcpy ( "https://");
	else           sb->safeStrcpy ( "http://" );
        sb->safeMemcpy ( cu.getHost() , cu.getHostLen() );
	int32_t port = cu.getPort();
	int32_t defPort = 80;
	if ( isHttps ) defPort = 443;
	if ( port != defPort ) sb->safePrintf ( ":%"INT32"",port );
	sb->safePrintf ( "/robots.txt\">"
			 "%i"
			 "</a>"
			 "</td>" // crawl delay MS
			 "<td>%i</td>" // proxies banning
			 , r->m_crawlDelayMS
			 , r->m_numBannedProxies
			 );

	// show collection name as a link, also truncate to 32 chars
	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );
	char *coll = "none";
	if ( cr ) coll = cr->m_coll;
	sb->safePrintf("<td>");
	if ( cr ) {
		sb->safePrintf("<a href=/admin/sockets?c=");
		sb->urlEncode(coll);
		sb->safePrintf(">");
	}
	sb->safeTruncateEllipsis ( coll , 32 );
	if ( cr ) sb->safePrintf("</a>");
	sb->safePrintf("</td>");
	// then the url itself
	sb->safePrintf("<td><a href=%s>",r->ptr_url);
	sb->safeTruncateEllipsis ( r->ptr_url , 128 );
	sb->safePrintf("</a></td>");
	sb->safePrintf("</tr>\n");

	// print next entry now
	r = r->m_nextLink;
	goto loop;

}
