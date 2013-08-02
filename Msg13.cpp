#include "gb-include.h"

#include "Msg13.h"
#include "UdpServer.h"
#include "HttpServer.h"
#include "Stats.h"
#include "HashTableX.h"
#include "XmlDoc.h"
#include "Test.h"
#include "Speller.h"

long convertIntoLinks ( char *reply , long replySize ) ;
long filterRobotsTxt ( char *reply , long replySize , HttpMime *mime ,
		       long niceness , char *userAgent , long uaLen ) ;
bool getIframeExpandedContent ( Msg13Request *r , TcpSocket *ts );
void gotIframeExpandedContent ( void *state ) ;

// utility functions
bool getTestSpideredDate ( Url *u , long *origSpiderDate , char *testDir ) ;
bool addTestSpideredDate ( Url *u , long  spideredTime   , char *testDir ) ;
bool getTestDoc ( char *u , class TcpSocket *ts , Msg13Request *r );
bool addTestDoc ( long long urlHash64 , char *httpReply , long httpReplySize ,
		  long err , Msg13Request *r ) ;

static void gotForwardedReplyWrapper ( void *state , UdpSlot *slot ) ;
static void handleRequest13 ( UdpSlot *slot , long niceness ) ;
//static bool downloadDoc     ( UdpSlot *slot, Msg13Request* r ) ;
static void gotHttpReply    ( void *state , TcpSocket *ts ) ;
static void gotHttpReply2 ( void *state , 
			    char *reply , 
			    long  replySize ,
			    long  replyAllocSize ,
			    TcpSocket *ts ) ;
static void passOnReply     ( void *state , UdpSlot *slot ) ;

bool hasIframe           ( char *reply, long replySize , long niceness );
long hasGoodDates        ( char *content, 
			   long contentLen, 
			   Xml *xml, 
			   Words *words,
			   char ctype,
			   long niceness );
char getContentTypeQuick ( HttpMime *mime, char *reply, long replySize , 
			   long niceness ) ;
long convertIntoLinks    ( char *reply, long replySize , Xml *xml , 
			   long niceness ) ;

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
	long memRobots = 3000000;
	long memOthers = 2000000;
	// assume 15k avg cache file
	long maxCacheNodesRobots = memRobots / 106;
	long maxCacheNodesOthers = memOthers / (10*1024);

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
	if ( ! s_rt.set ( 8 , 4 , 0 , NULL , 0 , true,0,"wait13tbl") )
		return false;

	// success
	return true;
}

// . returns false if blocked, returns true otherwise
// . returns true and sets g_errno on error
bool Msg13::getDoc ( Msg13Request *r,
		     bool isTestColl , 
		     void *state,void(*callback)(void *state)){
	
	/*
	char buf[1024];
	char *s = "<td class=\"smallfont\" align=\"right\">November 14th, 2011 10:06 AM</td>\r\n\t\t";
	strcpy(buf,s);
	Xml xml;
	long status = hasGoodDates ( buf ,
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
	r->m_urlLen    = gbstrlen ( r->m_url );
	r->m_urlHash64 = hash64 ( r->m_url , r->m_urlLen );

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
	if ( r->m_urlLen > 12 && 
	     ! strncmp ( r->m_url + r->m_urlLen - 11,"/robots.txt",11))
		r->m_isRobotsTxt = true;

	// force caching if getting robots.txt so is compressed in cache
	if ( r->m_isRobotsTxt )
		r->m_compressReply = true;

	// do not get .google.com/ crap
	//if ( strstr(r->m_url,".google.com/") ) { char *xx=NULL;*xx=0; }

	// set it for this too
	//if ( g_conf.m_useCompressionProxy ) {
	//	r->m_useCompressionProxy = true;
	//	r->m_compressReply       = true;
	//}

	// make the cache key
	r->m_cacheKey  = r->m_urlHash64;
	// a compressed reply is different than a non-compressed reply
	if ( r->m_compressReply ) r->m_cacheKey ^= 0xff;

	// always forward these so we can use the robots.txt cache
	if ( r->m_isRobotsTxt ) r->m_forwardDownloadRequest = true;

	// always forward for now until things work better!
	r->m_forwardDownloadRequest = true;	

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

	// shortcut
	Msg13Request *r = m_request;

	//
	// forward this request to the host responsible for this url's ip
	//
	long nh     = g_hostdb.m_numHosts;
	long hostId = hash32h(((unsigned long)r->m_firstIp >> 8), 0) % nh;
	// get host to send to from hostId
	Host *h = NULL;
	// . pick first alive host, starting with "hostId" as the hostId
	// . if all dead, send to the original and we will timeout > 200 secs
	for ( long count = 0 ; count <= nh ; count++ ) {
		// get that host
		//h = g_hostdb.getProxy ( hostId );;
		h = g_hostdb.getHost ( hostId );
		// stop if he is alive
		if ( ! g_hostdb.isDead ( h ) ) break;
		// get the next otherwise
		if ( ++hostId >= nh ) hostId = 0;
	}

	// forward it to self if we are the spider proxy!!!
	if ( g_hostdb.m_myHost->m_isProxy )
		h = g_hostdb.m_myHost;

	// log it
	if ( g_conf.m_logDebugSpider )
		logf ( LOG_DEBUG, 
		       "spider: sending download request of %s firstIp=%s "
		       "uh48=%llu to "
		       "host %li (child=%li)", r->m_url, iptoa(r->m_firstIp), 
		       r->m_urlHash48, hostId,
		       r->m_skipHammerCheck);


	// fill up the request
	long requestSize = r->getSize();

	// . otherwise, send the request to the key host
	// . returns false and sets g_errno on error
	// . now wait for 2 minutes before timing out
	if ( ! g_udpServer.sendRequest ( (char *)r    ,
					 requestSize  , 
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
					 200          )){// 200 sec timeout
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
	// shortcut
	Msg13 *THIS = (Msg13 *)state;
	// return if this blocked
	if ( ! THIS->gotForwardedReply ( slot ) ) return;
	// callback
	THIS->m_callback ( THIS->m_state );
}

bool Msg13::gotForwardedReply ( UdpSlot *slot ) {
	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;
	// what did he give us?
	char *reply          = slot->m_readBuf;
	long  replySize      = slot->m_readBufSize;
	long  replyAllocSize = slot->m_readBufMaxSize;
	// UdpServer::makeReadBuf() sets m_readBuf to -1 when calling
	// alloc() with a zero length, so fix that
	if ( replySize == 0 ) reply = NULL;
	// this is messed up. why is it happening?
	if ( reply == (void *)-1 ) { char *xx=NULL;*xx=0; }

	// we are responsible for freeing reply now
	if ( ! g_errno ) slot->m_readBuf = NULL;

	return gotFinalReply ( reply , replySize , replyAllocSize );
}

bool Msg13::gotFinalReply ( char *reply, long replySize, long replyAllocSize ){

	// assume none
	m_replyBuf     = NULL;
	m_replyBufSize = 0;

	// shortcut
	Msg13Request *r = m_request;

	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads )
		logf(LOG_DEBUG,"spider: FINALIZED %s firstIp=%s",
		     r->m_url,iptoa(r->m_firstIp));


	// . if timed out probably the host is now dead so try another one!
	// . return if that blocked
	if ( g_errno == EUDPTIMEDOUT ) {
		// try again
		log("spider: retrying1. had error for %s : %s",
		    r->m_url,mstrerror(g_errno));
		// return if that blocked
		if ( ! forwardRequest ( ) ) return false;
		// a different g_errno should be set now!
	}

	if ( g_errno ) {
		// this error msg is repeated in XmlDoc::logIt() so no need
		// for it here
		if ( g_conf.m_logDebugSpider )
			log("spider: error for %s: %s",
			    r->m_url,mstrerror(g_errno));
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
	uint32_t unzippedLen = *(long*)reply;
	// sanity checks
	if ( unzippedLen < 0 || unzippedLen > 10000000 ) {
		log("spider: got possible corrupt compressed doc "
		    "with unzipped len of %li",(long)unzippedLen);
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
	unsigned long uncompressedLen = unzippedLen;
	// uncompress it
	int zipErr = gbuncompress( (unsigned char*)newBuf  ,  // dst
				   &uncompressedLen        ,  // dstLen
				   (unsigned char*)reply+4 ,  // src
				   replySize - 4           ); // srcLen
	if(zipErr != Z_OK || uncompressedLen!=(long unsigned int)unzippedLen) {
		log("spider: had error unzipping Msg13 reply. unzipped "
		    "len should be %li but is %li. ziperr=%li",
		    (long)uncompressedLen,(long)unzippedLen,(long)zipErr);
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
		log("http: got doc %s %li to %li",
		    r->m_url,(long)replySize,(long)uncompressedLen);

	return true;
}

RdbCache s_hammerCache;
static bool s_flag = false;

// . only return false if you want slot to be nuked w/o replying
// . MUST always call g_udpServer::sendReply() or sendErrorReply()
void handleRequest13 ( UdpSlot *slot , long niceness  ) {

 	// cast it
	Msg13Request *r = (Msg13Request *)slot->m_readBuf;
	// use slot niceness
	r->m_niceness = niceness;
	// . sanity - otherwise xmldoc::set cores!
	// . no! sometimes the niceness gets converted!
	//if ( niceness == 0 ) { char *xx=NULL;*xx=0; }

	// make sure we do not download gigablast.com admin pages!
	if ( g_hostdb.isIpInNetwork ( r->m_firstIp ) && r->m_urlLen >= 7 ) {
		Url url;
		url.set ( r->m_url );
		// . never download /master urls from ips of hosts in cluster
		// . TODO: FIX! the pages might be in another cluster!
		if ( ( strncasecmp ( url.getPath() , "/master/" , 8 ) == 0 ||
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
	long  recSize;
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

	// . an empty rec is a cached not found (no robot.txt file)
	// . therefore it's allowed, so set *reply to 1 (true)
	if ( inCache ) {
		// . send the cached reply back
		// . this will free send/read bufs on completion/g_errno
		g_udpServer.sendReply_ass ( rec , recSize , rec, recSize,slot);
		return;
	}

	// log it so we can see if we are hammering
	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads )
		logf(LOG_DEBUG,"spider: DOWNLOADING %s firstIp=%s",
		     r->m_url,iptoa(r->m_firstIp));

	// temporary hack
	if ( r->m_parent ) { char *xx=NULL;*xx=0; }

	// use the default agent unless scraping
	// force to event guru bot for now
	//char *agent = "Mozilla/5.0 (compatible; ProCogSEOBot/1.0; +http://www.procog.com/ )";
	char *agent = "Mozilla/5.0 (compatible; GigaBot/1.0; +http://www.gigablast.com/ )";
	if ( r->m_isScraping )
		agent = "Mozilla/4.0 "
			"(compatible; MSIE 6.0; Windows 98; "
			"Win 9x 4.90)" ;
	// assume we do not add it!
	r->m_addToTestCache = false;

	if ( ! s_flag ) {
		s_flag = true;
		s_hammerCache.init ( 5000       , // maxcachemem,
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

	// we skip it if its a frame page, robots.txt, root doc or some other
	// page that is a "child" page of the main page we are spidering
	if ( ! r->m_skipHammerCheck ) {
		// make sure we are not hammering an ip
		long long last=s_hammerCache.getLongLong(0,r->m_firstIp,
							 30,true);
		// get time now
		long long nowms = gettimeofdayInMilliseconds();
		// how long has it been since last download START time?
		long long waited = nowms - last;
		// if we had it in cache check the wait time
		if ( last > 0 && waited < 400 ) {
			log("spider: hammering firstIp=%s url=%s "
			    "only waited %lli ms",
			    iptoa(r->m_firstIp),r->m_url,waited);
			// this guy has too many redirects and it fails us...
			// BUT do not core if running live, only if for test
			// collection
			// for now disable it seems like 99.9% good... but
			// still cores on some wierd stuff...
			//if(r->m_useTestCache && r->m_firstIp!=-1944679785 ) {
			//	char*xx = NULL; *xx = 0; }
		}
		// store time now
		s_hammerCache.addLongLong(0,r->m_firstIp,nowms);
		// note it
		if ( g_conf.m_logDebugSpider )
			log("spider: adding download end time of %llu for "
			    "firstIp=%s "
			    "url=%s "
			    "to msg13::hammerCache",
			    nowms,iptoa(r->m_firstIp),r->m_url);
		// clear error from that if any, not important really
		g_errno = 0;
	}

	// try to get it from the test cache?
	TcpSocket ts;
	if ( r->m_useTestCache && getTestDoc ( r->m_url, &ts , r ) ) {
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
		long key = ((uint32_t)r->m_firstIp >> 8);
		// send to host "h"
		Host *h = g_hostdb.getBestSpiderCompressionProxy(&key);
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: sending to compression proxy "
			    "%s:%lu",iptoa(h->m_ip),(unsigned long)h->m_port);
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


	// are we the first?
	bool firstInLine = s_rt.isEmpty ( &r->m_cacheKey );
	// wait in line cuz someone else downloading it now
	if ( ! s_rt.addKey ( &r->m_cacheKey , &r ) ) {
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}

	// this means our callback will be called
	if ( ! firstInLine ) return;

	// do not get .google.com/ crap
	//if ( strstr(r->m_url,".google.com/") ) { char *xx=NULL;*xx=0; }

	// flag this
	r->m_addToTestCache = true;
	// note it here
	if ( g_conf.m_logDebugSpider )
		log("spider: downloading %s (%s)",
		    r->m_url,iptoa(r->m_urlIp) );
	// download it
	if ( ! g_httpServer.getDoc ( r->m_url             ,
				     r->m_urlIp           ,
				     0                    , // offset
				     -1                   ,
				     r->m_ifModifiedSince ,
				     r                    , // state
				     gotHttpReply         , // callback
				     30*1000              , // 30 sec timeout
				     r->m_httpProxyIp     ,
				     r->m_httpProxyPort   ,
				     r->m_maxTextDocLen   ,
				     r->m_maxOtherDocLen  ,
				     agent                ) )
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

void gotHttpReply ( void *state , TcpSocket *ts ) {
	// if we had no error, TcpSocket should be legit
	if ( ts ) {
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
		     long  replySize ,
		     long  replyAllocSize ,
		     TcpSocket *ts ) {

	// save error
	long savedErr = g_errno;

	Msg13Request *r    = (Msg13Request *) state;
	UdpSlot      *slot = r->m_udpSlot;

	// error?
	if ( g_errno && g_conf.m_logDebugSpider )
		log("spider: http reply (msg13) had error = %s "
		    "for %s at ip %s",
		    mstrerror(g_errno),r->m_url,iptoa(r->m_urlIp));

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
	long originalSize = replySize;

	// . add the reply to our test cache
	// . if g_errno is set to something like "TCP Timed Out" then
	//   we end up saving a blank robots.txt or doc here...
	if ( r->m_useTestCache && r->m_addToTestCache )
		addTestDoc ( r->m_urlHash64,reply,replySize,
			     savedErr , r );

	// note it
	if ( r->m_useTestCache && g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: got reply for %s firstIp=%s uh48=%llu",
		     r->m_url,iptoa(r->m_firstIp),r->m_urlHash48);

	long niceness = r->m_niceness;

	// sanity check
	if ( replySize>0 && reply[replySize-1]!= '\0') { char *xx=NULL;*xx=0; }

	// assume http status is 200
	bool goodStatus = true;

	long long *docsPtr     = NULL;
	long long *bytesInPtr  = NULL;
	long long *bytesOutPtr = NULL;

	// use this mime
	HttpMime mime;
	long httpStatus = 0; // 200;

	// do not do any of the content analysis routines below if we
	// had a g_errno like ETCPTIMEDOUT or EBADMIME or whatever...
	if ( savedErr ) goodStatus = false;

	// no, its on the content only, NOT including mime
	long mimeLen = 0;

	// only bother rewriting the error mime if user wanted compression
	// otherwise, don't bother rewriting it.
	// DO NOT do this if savedErr is set because we end up calling
	// sendErorrReply() below for that!
	if ( replySize>0 && r->m_compressReply && ! savedErr ) {
		// exclude the \0 i guess. use NULL for url.
		mime.set ( reply , replySize - 1, NULL );
		// no, its on the content only, NOT including mime
		mimeLen = mime.getMimeLen();
		// get this
		httpStatus = mime.getHttpStatus();
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
				      "HTTP/1.0 %li\r\n"
				      "Content-Length: 0\r\n" ,
				      httpStatus );
			// convery redirect urls back to requester
			char *loc    = mime.getLocationField();
			long  locLen = mime.getLocationFieldLen();
			// if too big, forget it! otherwise we breach tmpBuf
			if ( loc && locLen > 0 && locLen < 1024 ) {
				p += sprintf ( p , "Location: " );
				memcpy ( p , loc , locLen );
				p += locLen;
				memcpy ( p , "\r\n", 2 );
				p += 2;
			}
			// close it up
			p += sprintf ( p , "\r\n" );
			// copy it over as new reply, include \0
			long newSize = p - tmpBuf + 1;
			if ( newSize >= 2048 ) { char *xx=NULL;*xx=0; }
			// record in the stats
			docsPtr     = &g_stats.m_compressMimeErrorDocs;
			bytesInPtr  = &g_stats.m_compressMimeErrorBytesIn;
			bytesOutPtr = &g_stats.m_compressMimeErrorBytesOut;
			// only replace orig reply if we are smaller
			if ( newSize < replySize ) {
				memcpy ( reply , tmpBuf , newSize );
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
	long contentLen = replySize - 1 - mimeLen;
	// fix bad crap
	if ( contentLen < 0 ) contentLen = 0;

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
		long cs = getCharsetFast ( &mime,
					   r->m_url,
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
		long score;
		// returns -1 and sets g_errno on error, 
		// because 0 means langUnknown
		long langid = words.getLanguage(NULL,1000,niceness,&score);
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

	if ( hasIframe2 && ! r->m_attemptedIframeExpansion ) {
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
		if ( ! getIframeExpandedContent ( r , ts ) )
			return;
		// ok, did we have an error?
		if ( g_errno )
			log("scproxy: xml set for %s had error: %s",
			    r->m_url,mstrerror(g_errno));
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
		long ch32 = getContentHash32Fast( (unsigned char *)content ,
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
	if ( r->m_url &&
	     replySize>0 &&
	     goodStatus &&
	     strstr ( r->m_url,"flurbit.com/" ) ) {
		// note it in log
		log("msg13: got flurbit url: %s",r->m_url);
		// record in the stats
		docsPtr     = &g_stats.m_compressUnchangedDocs;
		bytesInPtr  = &g_stats.m_compressUnchangedBytesIn;
		bytesOutPtr = &g_stats.m_compressUnchangedBytesOut;
		// do not send anything back
		replySize = 0;
	}


	// by default assume it has a good date
	long status = 1;

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
		char *ua = "Gigabot";
		long uaLen = gbstrlen(ua);
		replySize = filterRobotsTxt (reply,replySize,&mime,niceness,
					     ua,uaLen);
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
		long need = sizeof(long) +        // unzipped size
			(long)(replySize * 1.01) + // worst case size
			25;                       // for zlib
		// for 7-zip
		need += 300;
		// back buffer to hold compressed reply
		unsigned long compressedLen;
		char *compressedBuf = (char*)mmalloc(need, "Msg13Zip");
		if ( ! compressedBuf ) {
			g_errno = ENOMEM;
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}

		// store uncompressed length as first four bytes in the
		// compressedBuf
		*(long *)compressedBuf = replySize;
		// the remaining bytes are for data
		compressedLen = need - 4;
		// leave the first 4 bytes to hold the uncompressed size
		int zipErr = gbcompress( (unsigned char*)compressedBuf+4,
					 &compressedLen,
					 (unsigned char*)reply, 
					 replySize);
		if(zipErr != Z_OK) {
			log("spider: had error zipping Msg13 reply.");
			mfree (compressedBuf, need, "Msg13ZipError");
			g_errno = ECORRUPTDATA;
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

	// shortcut
	UdpServer *us = &g_udpServer;

	// how many have this key?
	long count = s_rt.getCount ( &r->m_cacheKey );
	// sanity check
	if ( count < 1 ) { char *xx=NULL;*xx=0; }

	// send a reply for all waiting in line
	long tableSlot;
	// loop
	for ( ; ( tableSlot = s_rt.getSlot ( &r->m_cacheKey) ) >= 0 ; ) {
		// use this
		long err = 0;
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
			     // connection reset by peer
			     err != ECONNRESET ) {
				char*xx=NULL;*xx=0;}
		}
		// replicate the reply. might return NULL and set g_errno
		char *copy          = reply;
		long  copyAllocSize = replyAllocSize;
		// . only copy it if we are not the last guy in the table
		// . no, now always copy it
		if ( --count > 0 && ! err ) {
			copy          = (char *)mdup(reply,replySize,"msg13d");
			copyAllocSize = replySize;
		}
		// get request
		Msg13Request *r2;
		r2 = *(Msg13Request **)s_rt.getValueFromSlot(tableSlot);
		// get udp slot for this transaction
		UdpSlot *slot = r2->m_udpSlot;
		// remove from list
		s_rt.removeSlot ( tableSlot );
		// send back error?  maybe...
		if ( err ) {
			if ( g_conf.m_logDebugSpider )
				log("proxy: msg13: sending back error: %s "
				    "for url %s with ip %s",
				    mstrerror(err),
				    r2->m_url,
				    iptoa(r2->m_urlIp));
			g_udpServer.sendErrorReply ( slot , err );
			continue;
		}
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
		    r->m_url,mstrerror(g_errno));
		g_udpServer.sendErrorReply(r->m_udpSlot, g_errno);
		return;
	}

	// what did he give us?
	char *reply          = slot->m_readBuf;
	long  replySize      = slot->m_readBufSize;
	long  replyAllocSize = slot->m_readBufMaxSize;
	// do not allow "slot" to free the read buf since it is being used
	// as the send buf for "udpSlot"
	slot->m_readBuf     = NULL;
	slot->m_readBufSize = 0;
	//long  replyAllocSize = slot->m_readBufSize;
	// just forward it on
	g_udpServer.sendReply_ass (reply,replySize,
				   reply,replyAllocSize,
				   r->m_udpSlot);
}

//
//
// . UTILITY FUNCTIONS for injecting into the "test" collection
// . we need to ensure that the web pages remain constant so we store them
//
//

// . returns true if found on disk in the test subdir
// . returns false with g_errno set on error
// . now that we are lower level in Msg13.cpp, set "ts" not "slot"
bool getTestDoc ( char *u , TcpSocket *ts , Msg13Request *r ) {
	// sanity check
	//if ( strcmp(m_coll,"test") ) { char *xx=NULL;*xx=0; }
	// hash the url into 64 bits
	long long h = hash64 ( u , gbstrlen(u) );
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
	sprintf(fn,"%s/%s/doc.%llu.html",g_hostdb.m_dir,td,h);
	// look it up
	f.set ( fn );
	// try to get it
	if ( ! f.doesExist() ) {
		//if ( g_conf.m_logDebugSpider )
			log("test: doc not found in test cache: %s (%llu)",
			    u,h);
		return false;
	}
	// get size
	long fs = f.getFileSize();
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
	long rs = f.read ( buf , fs , 0 );
	// not read enough?
	if ( rs != fs ) {
		mfree ( buf,fs,"gtd");
		return log("test: read returned %li != %li",rs,fs);
	}
	f.close();
	// null term it
	buf[fs] = '\0';

	// was it error=%lu ?
	if ( ! strncmp(buf,"errno=",6) ) {
		ts->m_readBuf     = NULL;
		ts->m_readBufSize = 0;
		ts->m_readOffset  = 0;
		g_errno = atol(buf+6);
		// fix mem leak
		mfree ( buf , fs+1 , "gtd" );
		// log it for now
		if ( g_conf.m_logDebugSpider )
			log("test: GOT ERROR doc in test cache: %s (%llu) "
			    "[%s]",u,h, mstrerror(g_errno));
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		return true;
	}

	// log it for now
	//if ( g_conf.m_logDebugSpider )
		log("test: GOT doc in test cache: %s (%llu)",u,h);
		
	//fprintf(stderr,"scp gk252:/e/test-spider/doc.%llu.* /home/mwells/gigablast/test-parser/\n",h);

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

bool getTestSpideredDate ( Url *u , long *origSpideredDate , char *testDir ) {
	// hash the url into 64 bits
	long long uh64 = hash64(u->getUrl(),u->getUrlLen());
	// read the spider date file first
	char fn[300]; 
	File f;
	// get the spider date then
	sprintf(fn,"%s/%s/doc.%llu.spiderdate.txt",
		g_hostdb.m_dir,testDir,uh64);
	// look it up
	f.set ( fn );
	// try to get it
	if ( ! f.doesExist() ) return false;
	// get size
	long fs = f.getFileSize();
	// error?
	if ( fs == -1 ) return log("test: error getting file size from test");
	// open it
	f.open ( O_RDWR );
	// make a buf
	char dbuf[200];
	// read the date in (int format)
	long rs = f.read ( dbuf , fs , 0 );
	// sanity check
	if ( rs <= 0 ) { char *xx=NULL;*xx=0; }
	// get it
	*origSpideredDate = atoi ( dbuf );
	// close it
	f.close();
	// note it
	//log("test: read spiderdate of %lu for %s",*origSpideredDate,
	//    u->getUrl());
	// good to go
	return true;
}

bool addTestSpideredDate ( Url *u , long spideredTime , char *testDir ) {
	// set this
	long long uh64 = hash64(u->getUrl(),u->getUrlLen());
	// make that into a filename
	char fn[300]; 
	sprintf(fn,"%s/%s/doc.%llu.spiderdate.txt",
		g_hostdb.m_dir,testDir,uh64);
	// look it up
	File f; f.set ( fn );
	// if already there, return now
	if ( f.doesExist() ) return true;
	// make it into buf
	char dbuf[200]; sprintf ( dbuf ,"%lu\n",spideredTime);
	// open it
	f.open ( O_RDWR | O_CREAT );
	// write it now
	long ws = f.write ( dbuf , gbstrlen(dbuf) , 0 );
	// close it
	f.close();
	// panic?
	if ( ws != (long)gbstrlen(dbuf) )
		return log("test: error writing %li != %li to %s",ws,
			   (long)gbstrlen(dbuf),fn);
	// close it up
	//f.close();
	return true;
}

// add it to our "test" subdir
bool addTestDoc ( long long urlHash64 , char *httpReply , long httpReplySize ,
		  long err , Msg13Request *r ) {

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
	sprintf(fn,"%s/%s/doc.%llu.html",g_hostdb.m_dir,td,urlHash64);
	// look it up
	File f; f.set ( fn );
	// if already there, return now
	if ( f.doesExist() ) return true;
	// open it
	f.open ( O_RDWR | O_CREAT );
	// log it for now
	if ( g_conf.m_logDebugSpider )
		log("test: ADDING doc to test cache: %llu",urlHash64);

	// write error only?
	if ( err ) {
		char ebuf[256];
		sprintf(ebuf,"errno=%lu\n",err);
		f.write(ebuf,gbstrlen(ebuf),0);
		f.close();
		return true;
	}

	// write it now
	long ws = f.write ( httpReply , httpReplySize , 0 );
	// close it
	f.close();
	// panic?
	if ( ws != httpReplySize )
		return log("test: error writing %li != %li to %s",ws,
			   httpReplySize,fn);
	// all done, success
	return true;
}

// . convert html/xml doc in place into a buffer of links, \n separated
// . return new reply size
// . return -1 on error w/ g_errno set on error
// . replySize includes terminating \0??? i dunno
long convertIntoLinks ( char *reply , 
			long replySize , 
			Xml *xml ,
			long niceness ) {
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
	// . use memcpy() because it deal with potential overlap issues
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
	//memcpy ( dst , "<!--links-->\n", 13 );
	//dst += 13;
	// iterate over the links
	for ( long i = 0 ; i < links.m_numLinks ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// get link
		char *str = links.getLink(i);
		// get size
		long len = links.getLinkLen(i);
		// ensure no breach. if so, return now
		if ( dst + len + 2 > dstEnd ) return dst - reply;
		// lead it
		memcpy ( dst, "<a href=", 8 );
		dst += 8;
		// copy over, should be ok with overlaps
		memcpy ( dst , str , len );
		dst += len;
		// end tag and line
		memcpy ( dst , "></a>\n", 6 );
		dst += 6;
	}
	// null term it!
	*dst++ = '\0';
	// content length
	long clen = dst - content - 1;
	// the last digit
	char *dptr = saved + 7;
	// store it up top in the mime header
	for ( long x = 0 ; x < 8 ; x++ ) {
		//if ( clen == 0 ) *dptr-- = ' ';
		if ( clen == 0 ) break;
		*dptr-- = '0' + (clen % 10);
		clen /= 10;
	}
	// the new replysize is just this plain list of links
	return dst - reply;
}

// returns true if <iframe> tag in there
bool hasIframe ( char *reply, long replySize , long niceness ) {
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
long hasGoodDates ( char *content ,
		    long  contentLen , 
		    Xml *xml , 
		    Words *words,
		    char ctype ,
		    long niceness ) {
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
	long now = getTimeLocal();
	struct tm *timeStruct = gmtime ( &now );
	long year = 1900 + timeStruct->tm_year;
	// day of month. starts at 1.
	long day  = timeStruct->tm_mday;
	// 0 is january. but we use 1 for january in Dates.cpp, so add 1.
	long month = timeStruct->tm_mon + 1;

	bool gotTOD      = false;
	bool gotMonthDow = false;

	Date *d1 = NULL;
	Date *d2 = NULL;

	// scan the dates we got, looking for certain types
	for ( long i = 0 ; i < dates.m_numDatePtrs ; i++ ) {
		// shortcut
		Date *di = dates.m_datePtrs[i];
		// skip if nuked
		if ( ! di ) continue;
		// shortcut
		datetype_t dt = di->m_hasType;
		// must be a tod month or dow
		if ( !(dt & (DT_TOD|DT_MONTH|DT_DOW)) ) continue;
		// get the date's year
		long diyear = di->m_maxYear;
		if ( (long)di->m_year <= 0 ) diyear = 0;
		// if it has a year but it is old, forget it
		if ( diyear > 0 && diyear < year ) continue;
		// get the date's month
		long dimonth = di->m_month;
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
			NULL     , // coll
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
	for ( long i = 0 ; i < aa.m_am.getNumPtrs() ; i++ ) {
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
			   long replySize , 
			   long niceness ) {
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
long filterRobotsTxt ( char *reply , 
		       long replySize , 
		       HttpMime *mime ,
		       long niceness ,
		       char *userAgent ,
		       long  userAgentLen ) {
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
			// the user-agent line we were going to memcpy()
			if ( reply + 16 > agent ) return replySize;
			if ( dst == reply ) {
				memcpy ( dst , "HTTP/1.0 200\r\n\r\n", 16 );
				dst += 16;
			}
			// store the user-agent and following allows/disallows
			memcpy ( dst, agent , start - agent );
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

	long niceness = r->m_niceness;

	// ok, we've an attempt now
	r->m_attemptedIframeExpansion = true;

	// we are doing something to destroy reply, so make a copy of it!
	long copySize = ts->m_readOffset + 1;
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
	strcpy(sreq.m_url,r->m_url);
	long firstIp = hash32n(r->m_url);
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
		log("scproxy: expanding iframes for %s",r->m_url);

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
	xd->m_firstIp = 0;
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
		//log("scproxy: waiting for %s",r->m_url);
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
		    " err=%s",r->m_url,mstrerror(g_errno));

	// save g_errno for returning
	long saved = g_errno;

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
	long saved = g_errno;

	XmlDoc *xd = (XmlDoc *)state;
	// this was stored in xd
	Msg13Request *r = xd->m_r;

	//log("scproxy: done waiting for %s",r->m_url);

	// note it
	if ( g_conf.m_logDebugBuild ) 
		log("scproxy: got iframe expansion for url=%s",r->m_url);

	// assume we had no expansion or there was an error
	char *reply          = NULL;
	long  replySize      = 0;

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
		    r->m_url,mstrerror(g_errno));
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


	
