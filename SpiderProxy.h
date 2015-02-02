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


// value for m_opCode. get a proxy to use from host #0:
#define OP_GETPROXY 1

// value for m_opCode. tell host #0 we are done using a proxy:
#define OP_RETPROXY 2

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
	
#define MAXUSERNAMEPWD 64

// host #0 gives us a proxy to use:
class ProxyReply {
public:
	// proxy ip to use
	int32_t  m_proxyIp;
	// id of the transaction
	int32_t  m_lbId;
	// proxy port to use
	int16_t m_proxyPort;
	// if this proxy fails us are there more proxies to try?
	bool  m_hasMoreProxiesToTry;
	// how many proxies do we have that are banned by the urlip?
	int32_t  m_numBannedProxies;
	// the username/pwd for authentication
	char m_usernamePwd[MAXUSERNAMEPWD];
};

#endif
