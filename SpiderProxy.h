#ifndef SPIDERPROXYH
#define SPIDERPROXYH

// called by main.cpp to set msg handlers for 0x54 and 0x55
bool initSpiderProxyStuff();

// called by process.cpp every 30 secs or so to try to download test urls
// to evaluate the spider proxies
bool downloadTestUrlFromProxies();

// called by Parms.cpp when user changes the list of proxyips
bool buildProxyTable ( );

// show spider proxy stats, called by Parms.cpp
bool printSpiderProxyTable ( SafeBuf *sb ) ;

bool resetProxyStats ( ) ;

// save stats on the spider proxies if any
bool saveSpiderProxyStats();

#define MAXUSERNAMEPWD 64

// host #0 breaks Conf::m_spiderIps safebuf into an array of
// SpiderProxy classes and saves to disk as spoderproxies.dat to ensure 
// persistence
class SpiderProxy {
public:
	// ip/port of the spider proxy
	int32_t m_ip;
	uint16_t m_port;
	// last time we attempted to download the test url through this proxy
	int64_t m_lastDownloadTestAttemptMS;
	// use -1 to indicate timed out when downloading test url
	int32_t   m_lastDownloadTookMS;
	// 0 means none... use mstrerror()
	int32_t   m_lastDownloadError;
	// use -1 to indicate never
	int64_t m_lastSuccessfulTestMS;

	// how many times have we told a requesting host to use this proxy
	// to download their url with.
	int32_t m_numDownloadRequests;

	// how many are outstanding? everytime a host requests a proxyip
	// it also tells us its outstanding counts for each proxy ip
	// so we can ensure this is accurate even though a host may die
	// and come back up.
	int32_t m_numOutstandingDownloads;

	// waiting on test url to be downloaded
	bool m_isWaiting;

	int64_t m_timesUsed;

	int32_t m_lastBytesDownloaded;

	// special things used by LoadBucket algo to determine which
	// SpiderProxy to use to download from a particular IP
	int32_t m_countForThisIp;
	int64_t m_lastTimeUsedForThisIp;

	char m_usernamePwd[MAXUSERNAMEPWD];
};

class SpiderProxy *getSpiderProxyByIpPort ( int32_t ip , uint16_t port ) ;

// value for m_opCode. get a proxy to use from host #0:
#define OP_GETPROXY 1

// value for m_opCode. tell host #0 we are done using a proxy:
#define OP_RETPROXY 2

// do not do load balancing for this request:
#define OP_GETPROXYFORDIFFBOT 3

// ask host #0 for a proxy to use:
// we now just use Msg13Request for this...
//class ProxyRequest {
//public:
//	// ip of url we want to download
//	int32_t m_urlIp;
//	// retry count
//	int32_t m_retryCount;
//	// OP_GETPROXY or OP_RETPROXY (return proxy)
//	char m_opCode;
//};
	
// host #0 gives us a proxy to use:
class ProxyReply {
public:
	// proxy ip to use
	int32_t  m_proxyIp;
	// id of the transaction
	int32_t  m_lbId;
	// proxy port to use
	int32_t m_proxyPort;
	// if this proxy fails us are there more proxies to try?
	bool  m_hasMoreProxiesToTry;
	// how many proxies do we have that are banned by the urlip?
	int32_t  m_numBannedProxies;
	// the username/pwd for authentication
	char m_usernamePwd[MAXUSERNAMEPWD];
};

#endif
