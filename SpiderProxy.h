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

// save stats on the spider proxies if any
bool saveSpiderProxyStats();

#endif
