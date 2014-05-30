#ifndef SPIDERPROXYH
#define SPIDERPROXYH

// called by main.cpp to set msg handlers for 0x54 and 0x55
bool initSpiderProxyStuff();

// called by process.cpp every 30 secs or so to try to download test urls
// to evaluate the spider proxies
bool downloadTestUrlFromProxies();

#endif
