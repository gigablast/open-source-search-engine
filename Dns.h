// Copyright Matt Wells Nov 2000

// . uses UDP only (we'll do the TCP part later)
// . derived from UdpServer 
// . always sends lookup to fastest dns
// . periodically pings other servers
// . if our primary dns goes down it's speed will be changed in hostmap
//   and we'll have a new primary
// . if a request times out we try the next dns

#ifndef _DNSSERVER_H_
#define _DNSSERVER_H_

#include "UdpServer.h"
#include "DnsProtocol.h"
#include "Hostdb.h"
#include "RdbCache.h"
#include "Conf.h"

#define MAX_DEPTH 18 // we can have a lot of CNAME aliases w/ akamai
#define MAX_DNS_IPS 32
#define MAX_TRIED_IPS 32 // stop after asking 32 nameservers, return timed out
#define LOOP_BUF_SIZE 26100
//#define LOOP_BUF_SIZE (64*1024)

// use a default of 1 day for both caches
#define DNS_CACHE_MAX_AGE       60*60*24
#define DNS_CACHE_MAX_AGE_LOCAL 60*60*24

// structure for TLD root name servers
typedef struct {
	int32_t		expiry;
	int32_t		numTLDIPs;
	uint32_t	TLDIP[MAX_DNS_IPS];
} TLDIPEntry;

class Dns;

struct DnsState {
	key_t       m_hostnameKey;
	// key for lookup into s_dnsTable hash table
	int64_t   m_tableKey; 
	Dns        *m_this  ;
	void       *m_state ;
	void      (*m_callback) ( void *state , int32_t ip ) ;
	char        m_freeit;
	bool        m_cacheNotFounds;
	char        m_hostname[128];

	// . point to the replies received from dns servers
	// . m_dnsNames[] should point into these reply buffers
	//char *m_replyBufPtrs[6];
	//int32_t  m_numReplies;

	// we can do a recursion up to 5 levels deep. sometimes the reply
	// we get back is a list of ips of nameservers we need to ask.
	// that can happen a few times in a row, and we have to keep track
	// of the depth here. initially we set these ips to those of the
	// root servers (or sometimes the local bind servers).
	bool m_rootTLD  [MAX_DEPTH];
	int32_t m_fallbacks[MAX_DEPTH];
	int32_t m_dnsIps   [MAX_DEPTH][MAX_DNS_IPS];
	int32_t m_numDnsIps[MAX_DEPTH];
	int32_t m_depth;  // current depth

	// . use these nameservers to do the lookup
	// . if not provided, they default to the root nameservers
	// . the first one we ask is based on hash of hostname % m_numDns,
	//   if that times out, the 2nd is right after the first, etc. so we
	//   always stay in order.
	// . m_dnsNames point into m_nameBuf, m_namePtr pts to the end of
	//   m_nameBuf so you can add new names to it. 
	//   m_dnsNames are NULLified when getIPOfDNS() get their ip address
	//   which is then added to m_dnsIps[]
	char *m_dnsNames    [MAX_DEPTH][MAX_DNS_IPS];
	int32_t  m_numDnsNames [MAX_DEPTH];
	char  m_nameBuf     [512];
	char *m_nameBufPtr;
	char *m_nameBufEnd;

	// this holds the one and only dns request
	char  m_request[512];
	int32_t  m_requestSize;

	// after we send to a nameserver add its ip to this list so we don't 
	// send to it again. or so we do not even add it to m_dnsIps again.
	int32_t m_triedIps[MAX_TRIED_IPS];
	int32_t m_numTried;

	// if we have to get the ip of the dns, then we get back more dns
	// that refer to that dns and we have to get the ip of those, ... etc.
	// for getting the ip of a dns we cast m_buf as a DnsState and use
	// that to avoid having to allocate more memory. however, we have to
	// keep track of how many times we do that recursively until we run
	// out of m_buf.
	int32_t m_loopCount;

	// set to EDNSDEAD (hostname does not exist) if we encounter that
	// error, however, we continue to ask other dns servers about the
	// hostname because we can often uncover the ip address that way.
	// but if we never do, we want to return this error, not ETIMEDOUT.
	int32_t m_errno;

	// we have to turn it off in some requests for some reason
	// like for www.fsis.usda.gov, othewise we get a refused to talk error
	char m_recursionDesired;

	// have a total timeout function
	int32_t m_startTime;

	char m_buf [ LOOP_BUF_SIZE ];
};

// this is for storing callbacks. need to keep a queue of ppl waiting for
// the ip of the same hostname so we don't send dup requests to the same
// dns server.
class CallbackEntry {
public:
	void *m_state;
	void (* m_callback ) ( void *state , int32_t ip );
	struct DnsState *m_ds;
	//class CallbackEntry *m_next;
	// we can't use a data ptr because slots get moved around when one
	// is deleted.
	int64_t m_nextKey;
	// debug info
	int32_t m_listSize;
	int32_t m_listId;
};

class Dns { 

 public:

	Dns();

	// reset the udp server and rdb cache
	void reset();

	// . we create our own udpServer in here since we can't share that
	//   because of the protocol differences
	// . read our dns servers from the conf file
	bool init ( uint16_t clientPort );

	// . check errno to on return or on callback to ensure no error
	// . we set ip to 0 if not found
	// . returns -1 and sets errno on error
	// . returns 0 if transaction blocked, 1 if completed
	bool getIp ( char  *hostname    , 
		     int32_t   hostnameLen ,
		     int32_t  *ip ,
		     void  *state ,
		     void (* callback) ( void *state , int32_t ip ) ,
		     DnsState *ds = NULL ,
		     //char *dnsNames = NULL ,
		     //int32_t *numDnsNames = 0 ,
		     //int32_t  *dnsIps   = NULL ,
		     //int32_t   numDnsIps = 0 ,
		     int32_t   timeout   = 60    ,
		     bool   dnsLookup = false ,
		     // monitor.cpp passes in false for this:
		     bool   cacheNotFounds = true );

	bool sendToNextDNS ( struct DnsState *ds , int32_t timeout = 60 ) ;

	bool getIpOfDNS ( DnsState *ds ) ;

	// . returns the ip
	// . called to parse the ip out of the reply in "slot"
	// . must be public so gotIpWrapper() can call it
	// . also update the timestamps in our private hostmap
	// . returns -1 on error
	// . returns 0 if ip does not exist
	int32_t gotIp ( UdpSlot *slot , struct DnsState *dnsState );

	// . we have our own udp server
	// . it contains our HostMap and DnsProtocol ptrs
	// . keep public so gotIpWrapper() can use it to destroy the slot
	UdpServer m_udpServer;

	RdbCache *getCache () { return &m_rdbCache; };
	RdbCache *getCacheLocal () { return &m_rdbCacheLocal; };

	// . pull the hostname out of a dns reply packet's query resource rec.
	bool extractHostname ( char *dgram    , 
			       char *record   , 
			       char *hostname );

	void cancel ( void *state ) { m_udpServer.cancel ( state , -1 ); }



	// returns true if in cache, and sets *ip
	bool isInCache (key_t key , int32_t *ip );

	// add this hostnamekey/ip pair to the cache
	void addToCache ( key_t hostnameKey , int32_t ip , int32_t ttl = -1 ) ;

	// is it in the /etc/hosts file?
	bool isInFile (key_t key , int32_t *ip );

	key_t getKey ( char *hostname , int32_t hostnameLen ) ;

	Host *getResponsibleHost ( key_t key ) ;

 private:

	bool loadFile ( );

	//uint16_t m_dnsTransId;

	// . key is a hash of hostname
	// . record/slot contains a 4 byte ip entry (if in key is in cache)
	// . cache is shared with other dbs
	RdbCache  m_rdbCache;
	RdbCache  m_rdbCacheLocal;

	DnsProtocol m_proto;

	int16_t  m_dnsClientPort;
	//char  *m_dnsDir;
	int32_t   m_dnsIp;
	int16_t  m_dnsPort; // we talk to dns thru this port

	int32_t m_numBind9Dns;

	// /etc/hosts in hashed into this table
	int32_t   *m_ips;
	key_t  *m_keys;
	int32_t    m_numSlots;
};

//This stores the ip's for the machine where 
//hash96(hostname) % g_hostdb.m_numHosts = cluster(group)
extern class Dns g_dns;

//This is MsgC's local machine cache
//extern class Dns g_dnsLocal;

#endif

