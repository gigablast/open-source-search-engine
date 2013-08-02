// Matt Wells, copyright Oct 2001

// . ask another host to download a url for you
// . the remote host will also use a cache if m_maxCacheAge > 0
// . used for downloading and caching robots.txt
// . if m_compressReply then the host compressed the http reply before
//   sending it back to you via udp

#ifndef _MSG13_H_
#define _MSG13_H_

#include "Url.h" // MAX_URL_LEN

void resetMsg13Caches ( ) ;

class Msg13Request {
public:
	long long m_urlHash48;
	long  m_urlIp;
	long  m_firstIp;
	long  m_httpProxyIp;
	short m_httpProxyPort;
	char  m_niceness;
	long  m_ifModifiedSince;
	long  m_maxCacheAge;
	long  m_maxTextDocLen;
	long  m_maxOtherDocLen;
	// if doing spider compression, compute contentHash32 of document
	// downloaded, and if it matches this then send back EDOCUNCHANGED
	long  m_contentHash32;
	// send back error ENOGOODDATE if it does not have one. but if
	// harvestLinks is true, just send back a filtered list of links
	long  m_requireGoodDate:1;
	long  m_harvestLinksIfNoGoodDate:1;
	long  m_compressReply:1;
	long  m_useCompressionProxy:1;
	// if m_forwardDownloadRequest is true then we pick the host to 
	// download this url based on the IP address, the idea being that
	// only one host is responsible for downloading from a particular
	// ip address. this keeps webmasters happier so they can block us
	// by just blocking one ip address. and it makes it easier for them
	// to analyze their web logs.
	long  m_forwardDownloadRequest:1;
	long  m_isScraping:1;
	// does url end in /robots.txt ?
	long  m_isRobotsTxt:1; 
	// should we call getTestDoc()/addTestDoc() like for the "test" coll
	// and for Test.cpp?
	long  m_useTestCache:1; 
	long  m_addToTestCache:1;
	long  m_skipHammerCheck:1;
	long  m_attemptedIframeExpansion:1;
	long  m_forEvents;
	//long  m_testParserEnabled:1;
	//long  m_testSpiderEnabled:1;
	//long  m_isPageParser:1;
	//long  m_isPageInject:1;

	// if we just end up calling HttpServer::getDoc() via calling
	// downloadDoc() then we set this for callback purposes
	class Msg13 *m_parent;

	// on the other hand, if we are called indirectly by handleRequest13()
	// then we set m_udpSlot.
	class UdpSlot *m_udpSlot;

	// used for addTestDoc() and caching. msg13 sets this
	long long m_urlHash64;	
	long      m_spideredTime;
	// used for caching (and for request table, wait in line table)
	long long m_cacheKey;
	char      m_testDir[32];
	// msg13 sets this too, so you don't have to worry about setting it
	long      m_urlLen;
	// includes \0 termination
	char      m_url[MAX_URL_LEN+1];

	long getSize() {
		return ((char *)m_url-(char *)this) +m_urlLen +1;};

	// zero it all out
	void reset() {
		memset (this,0,(char *)m_url - (char *)this + 1); 
		m_maxTextDocLen  = -1; // no limit
		m_maxOtherDocLen = -1; // no limit
	};
};

class Msg13 {

 public:

	Msg13() ;
	~Msg13();
	void reset() ;

	// register our request handler with g_udpServer (called by main.cpp)
	static bool registerHandler();

	static class RdbCache *getHttpCacheRobots();
	static class RdbCache *getHttpCacheOthers();

	bool getDoc ( Msg13Request *r ,
		      bool isTestColl ,
		      void   *state             ,
		      void  (*callback)(void *state) );

	bool forwardRequest();

	bool gotForwardedReply ( class UdpSlot *slot );
	bool gotFinalReply ( char *reply, long replySize, long replyAllocSize);

	// keep public so wrappers can access
	void *m_state;
	void  (* m_callback) (void *state );

	// we now store the uncompressed http reply in here
	char *m_replyBuf;
	long  m_replyBufSize;
	long  m_replyBufAllocSize;

	// point to it
	Msg13Request *m_request;
};

bool getTestSpideredDate ( Url *u , long *origSpideredDate , char *testDir ) ;
bool addTestSpideredDate ( Url *u , long  spideredTime     , char *testDir ) ;

extern RdbCache s_hammerCache;

#endif
