#include "gb-include.h"

#include "Dns.h"
#include "HashTableT.h"
//#include "Threads.h"

// comment out the following line to disable DNS TLD caching
// TLD caching seems to give about 15% performance increase over not caching.
// it has been pretty thoroughly tested, but if there is a problem,
// feel free to disable it.
#define DNS_TLD_CACHE

// See section 7. RESOLVER IMPLEMENTATION in the rfc 1035

// TODO: use the canonical name as a normalization!!

// a global class extern'd in .h file
Dns g_dns;

RdbCache g_timedoutCache;

static int64_t s_antiLockCount = 1LL;

#define TIMEOUT_SINGLE_HOST 30
#define TIMEOUT_TOTAL       90

static void gotIpWrapper ( void *state , UdpSlot *slot ) ;
static void gotIpOfDNSWrapper ( void *state , int32_t ip ) ;
static void returnIp ( DnsState *ds , int32_t ip ) ;

// CallbackEntry now defined in HashTableT.cpp
static HashTableT<int64_t,CallbackEntry> s_dnstable;
static HashTableT<uint32_t,TLDIPEntry> s_TLDIPtable;

Dns::Dns() {
	m_ips      = NULL;
	m_keys     = NULL;
	m_numSlots = 0;
}

// reset the udp server and rdb cache
void Dns::reset() {
	log("db: resetting dns");
	m_udpServer.reset();
	m_rdbCache.reset();
	g_timedoutCache.reset();
	s_dnstable.reset();
	s_TLDIPtable.reset();
	m_rdbCacheLocal.reset();
	// free hash table of /etc/hosts
	if ( m_ips  ) mfree ( m_ips  , m_numSlots*4            , "Dns");
	if ( m_keys ) mfree ( m_keys , m_numSlots*sizeof(key_t), "Dns");
	m_ips      = NULL;
	m_keys     = NULL;
	m_numSlots = 0;
}

// . port will be incremented if already in use
// . use 1 socket for recving and sending
// . we can use a shared rdb cache
// . we use the dbId to separate our cache entries from other db's entries
bool Dns::init ( uint16_t clientPort ) {
	// get primary dns server info from the conf class
	m_dnsClientPort = clientPort; // g_conf.m_dnsClientPort;
	// set the name of the cache. it will save to WORKDIR/{name}.dat
	int32_t maxMem = g_conf.m_dnsMaxCacheMem ;
	// . how many nodes in cache tree can we fit?
	// . each rec is key (12) and ip(4)
	// . overhead in cache is 56
	// . that makes 56 + 4 = 60
	// . not correct? stats suggest it's less than 25 bytes each
	int32_t maxCacheNodes = maxMem / 25;
	// make a copy of our protocol to pass to udp server
	// static DnsProtocol proto;
	// set the cache
	if ( ! m_rdbCache.init ( maxMem        ,
				 4             ,  // fixed data size of rec
				 false         ,  // support lists of recs?
				 maxCacheNodes ,
				 false         ,  // use half keys?
				 "dns"         ,  // dbname
				 true          )) // save cache to disk?
		return log("dns: Cache init failed.");

	// make a copy of our protocol to pass to udp server
	// static DnsProtocol proto;
	// set the cache
	int32_t maxMemLocal = 100000;
	if ( ! m_rdbCacheLocal.init ( maxMemLocal   ,
				      4             , // fixed data size of rec
				      false         , // support lists of recs?
				      maxMemLocal/25,
				      false         ,  // use half keys?
				      "dnsLocal"    ,  // dbname
				      true          )) // save cache?
		return log("dns: Cache local init failed.");

	// . set the port, proto and hostmap in our udpServer
	// . poll for timeouts every 11 seconds (11000 milliseconds)
	if ( ! m_udpServer.init ( m_dnsClientPort, 
				  &m_proto       ,
				  1              ,// niceness 
				  64000          ,// sock read buf
				  32000          ,// sock write buf
				  500            ,//polltime(.5secs)
				  500            ,//maxudpslots
				  true           ))// is dns?
		return log ("dns: Udp server init failed.");
	// innocent log msg
	log ( LOG_INIT,"dns: Sending requests on client port %"INT32" "
	      "using socket descriptor %i.", 
	      (int32_t)m_dnsClientPort , m_udpServer.m_sock );

	for ( int32_t i = 0 ; i < g_conf.m_numDns ; i++ ) {
		if ( !g_conf.m_dnsIps[i] ) continue;
		log ( LOG_INIT, "dns: Using nameserver %s:%i.",
		      iptoa(g_conf.m_dnsIps[i]) , g_conf.m_dnsPorts[i] );
	}

	// . only init the timedout cache once
	// . cache the dns servers' ips who timeout on us so we don't slow
	//   things down. later we can turn this into a "speed" cache, so
	//   we ask the fastest servers first.
	static bool s_init = false;
	if ( s_init ) return true;
	// just 30k for this little guy
	int32_t maxCacheMem = 30000;
	maxCacheNodes = maxCacheMem / 25;
	g_timedoutCache.init ( maxCacheMem ,
			       4     ,  // fixed data size of rec
			       false ,  // support lists of recs?
			       maxCacheNodes           ,
			       false                   ,  // use half keys?
			       "dnstimedout"           ,  // dbname
			       true                    ); // save cache?
	return true;
}

bool isTimedOut(int32_t ip) {
	// is this ip address in the "timed out" cache. if so,
	// then do not try again for at least 1 hour
	char *rec;
	int32_t  recSize;
	int32_t  maxAge = 3600; // 1 hour in seconds
	key_t k;
	k.n0 = 0LL;
	k.n1 = ip;
	bool  inCache = g_timedoutCache.getRecord ( (collnum_t)0 ,
							k       , // key
							&rec    ,
							&recSize,
							false   ,//do copy?
							maxAge  ,
							true    );//inc cnt
	return inCache;
}

inline bool parseTLD(DnsState* ds, char* buf, int32_t* len) {
	// parse out the TLD
	const char*	hostname=	ds->m_hostname;
	const char*	cbeg	=	hostname + gbstrlen(hostname);
	const char*	cend	=	cbeg;
	bool		found	=	false;
	char*		curs;
	while (cbeg > hostname) {
		if (*cbeg == '.') {
			cbeg++;
			found = true;
			if (cend - cbeg > *len - 1)
				return false;
			*len = cend - cbeg;
			gbmemcpy(buf, cbeg, *len);
			buf[*len] = '\0';
			for (curs = buf; *curs; curs++)
				*curs = to_lower_a(*curs);
			//log(LOG_DEBUG,"dns: parseTLD found '%s'", buf);
			return true;
		}
		cbeg--;
	}
	return found;
}

inline uint32_t TLDIPKey(char* buf, int32_t len) {
	// build "key" for TLD hash table
	uint32_t	key;
	key = 0;
	if (len > 4)
		len = 4;
	gbmemcpy(&key, buf, len);
	return key;
}

#ifdef DNS_TLD_CACHE
// returns NULL if we had a TLD cache miss
// returns ptr if we had a TLD cache hit
// adjusts ds->m_depth and ds->m_dnsIps on a hit
static const TLDIPEntry* getTLDIP(DnsState* ds) {

	//log(LOG_DEBUG, "dns: getTLDIP entry");

	char		buf[64];
	int32_t		len	=	sizeof(buf);
	if (!parseTLD(ds, buf, &len)) {
		log(LOG_WARN, "dns: unable to determine TLD for %s",
			ds->m_hostname);
		return NULL;
	}
	uint32_t	key	=	TLDIPKey(buf, len);
	if (key == 0) {
		log(LOG_WARN, "dns: getTLDIP invalid key");
		return NULL;
	}
	TLDIPEntry* tldip = s_TLDIPtable.getValuePointer(key);
	if (tldip == NULL) {
		//log(LOG_DEBUG, "dns: getTLDIP not in cache");
		return NULL;
	}

	// JAB: 2038
	if (tldip->expiry <= time(NULL)) {
		log(LOG_DEBUG, "dns: getTLDIP expired for %s", ds->m_hostname);
		return NULL;
	}

	log(LOG_DEBUG,"dns: TLD cache hit .%s NS depth %"INT32" for %s.",
		buf, (int32_t) ds->m_depth, ds->m_hostname);

	return tldip;
}

static void dumpTLDIP(	const char*	tld,
			TLDIPEntry*	tldip) {
	for (int32_t i = 0; i < tldip->numTLDIPs; i++) {
		log(LOG_DEBUG, "dns: .%s TLD IP %s",
			tld, iptoa(tldip->TLDIP[i]));
	}
}

static void setTLDIP(	DnsState*	ds,
			TLDIPEntry*	tldip) {

	// see if it's already in cache (i.e. poss. expired).
	const TLDIPEntry*	cached	=	getTLDIP(ds);
	int32_t			now	=	time(NULL);
	// expire TLD NS in 24 hours
	tldip->expiry = now + 3600 * 24;
	char			buf[64];
	int32_t			len	=	sizeof(buf);
	if (!parseTLD(ds, buf, &len)) {
		log(LOG_WARN, "dns: unable to determine TLD for %s",
			ds->m_hostname);
		return;
	}
	char			tld[64];
	gbmemcpy(tld, buf, len);
	tld[len] = '\0';
	if (cached == NULL) {
		uint32_t	key	=	TLDIPKey(buf, len);
		if (!s_TLDIPtable.addKey(key, *tldip)) {
			log(LOG_WARN, "dns: unable to add %s to TLD cache",
				ds->m_hostname);
			return;
		}
		log(LOG_DEBUG, "dns: TLD .%s NS cache add", buf);
		dumpTLDIP(tld, tldip);
	}
	else if (cached->expiry <= now) {
		// JAB: non-const cast...
		gbmemcpy((TLDIPEntry*) cached, tldip, sizeof(TLDIPEntry));
		log(LOG_DEBUG, "dns: TLD .%s NS cache update", buf);
		dumpTLDIP(tld, tldip);
	}
	else {
		//log(LOG_DEBUG, "dns: TLD cache up-to-date");
	}
}
#else	// DNS_TLD_CACHE

// code that is run when TLDIP cache is disabled...
static const TLDIPEntry* getTLDIP(DnsState* ds) {
	return NULL;
}
#endif	// DNS_TLD_CACHE

// . returns true and sets g_errno on error
// . returns false if transaction blocked, true if completed
// . returns false if you must wait
// . calls gotIp with ip when it gets it or timesOut or errors out
// . sets *ip to 0 if none (does not exist)
// . sets *ip to -1 and sets g_errno if there was an error
bool Dns::getIp ( char *hostname , 
		  int32_t  hostnameLen ,
		  int32_t *ip       ,
		  void *state    ,
		  void (* callback ) ( void *state , int32_t ip ) ,
		  DnsState *ds ,
		  //char **dnsNames ,
		  //int32_t   numDnsNames ,
		  //int32_t  *dnsIps      ,
		  //int32_t   numDnsIps   ,
		  int32_t   timeout     ,
		  bool   dnsLookup   ,
		  // monitor.cpp passes in false for this:
		  bool   cacheNotFounds ) {

	// . don't accept large hostnames
	// . technically the limit is 255 but i'm stricter
	if ( hostnameLen >= 254 ) {
		g_errno = EHOSTNAMETOOBIG;
		log("dns: Asked to get IP of hostname over 253 "
		    "characters long.");
		*ip=0;
		return true;
	}
	// debug msg
	char tmp[256];
	gbmemcpy ( tmp , hostname , hostnameLen );
	tmp [ hostnameLen ] = '\0';

	log(LOG_DEBUG, "dns: hostname '%s'", tmp);

	// assume no error
	g_errno = 0;

	// only g_dnsDistributed should be calling this, not g_dnsLocal
	if ( this != &g_dns ) { char *xx = NULL; *xx = 0; }

	// not thread safe
	//if ( g_threads.amThread() ) { char *xx = NULL; *xx = 0; }

	if ( hostnameLen <= 0 ) {
		log(LOG_LOGIC,"dns: Asked to get IP of zero length hostname.");
		*ip = 0;
		return true;
	}
	// if url is already in a.b.c.d format return that
	*ip = atoip ( hostname , hostnameLen );
	if ( *ip != 0 ) {
		log(LOG_DEBUG, "dns: IP address passed into getIp '%s'", tmp);
		return true;
	}
	// key is hash of the hostname
	key_t  hostKey96  = hash96 ( hostname , hostnameLen );
	// . is it in the /etc/hosts file?
	// . BAD: could have a key collision!! TODO: fix..
	if ( g_conf.m_useEtcHosts && isInFile ( hostKey96 , ip ) ) return true;
	// . try getting from the cache first
	// . this returns true if was in the cache and sets *ip to the ip
	// . we now cached EDNSTIMEDOUT errors for a day, so *ip can be -1
	// . TODO: watchout for key collision
	if ( isInCache ( hostKey96 , ip ) ) {
		// return 1 to indicate we got it right away in *ip
		if ( ! g_conf.m_logDebugDns ) return true;
		//char *dd = "distributed";
		//if ( this == &g_dnsLocal ) dd = "local";
		// debug msg
		log(LOG_DEBUG,"dns: got ip of %s for %s in distributed cache.",
		    iptoa(*ip),tmp);
		return true;
	}

	
	// . if this hostname request is already in progress, wait for that
	//   reply to to come back rather than launching a duplicate request.
	// . each bucket in the s_dnstable hashtable is a possible head of a 
	//   linked list of callback/state pairs which are waiting for that 
	//   hostname's ip
	// . is the ip for this hostname already being fetched?
	// . if so, there will be a callback entry class that should match its 
	//   DnsState::m_callback in there and have a key of key.n0 (see below)
	// . TODO: we can have collisions and end up getting back the wrong ip
	//   how can we fix this? keep a ptr to ds->m_hostname? and if does
	//   not match then just error out?
	int64_t hostKey64 = hostKey96.n0 & 0x7fffffffffffffffLL;
	// never let this be zero
	if ( hostKey64 == 0 ) hostKey64 = 1;
	// see if we are already looking up this hostname
	CallbackEntry *ptr = s_dnstable.getValuePointer ( hostKey64 );
	// if he has our key see if his hostname matches ours, it should.
	if ( ptr && 
	     // we do not store hostnameLen in ds, so make sure this is 0 
	     ! ptr->m_ds->m_hostname[hostnameLen] &&
	     (int32_t)gbstrlen(ptr->m_ds->m_hostname) == hostnameLen && 
	     strncmp ( ptr->m_ds->m_hostname, hostname, hostnameLen ) != 0 ) {
		g_errno = EBADENGINEER;
		log("dns: Found key collision in wait queue. host %s has "
		    "same key as %s. key=%"UINT64".",
		    ptr->m_ds->m_hostname, tmp, hostKey64);
		//char *xx = NULL; *xx = 0;
		// we should just error out if this happens, it is better
		// than giving him the wrong ip, he will be retried later
		// by the spider.
		return true;
	}
	// regardless, add our "ce" to the table, but assume we are NOT first
	// in line for a hostname and use a bogus key. it  doesn't matter, 
	// we just need some memory to store our CallbackEntry class.
	static int64_t s_bogus = 0;
	// make a CallbackEntry class to add to a slot in the table
	CallbackEntry ce;
	ce.m_callback = callback;
	ce.m_state    = state;
	ce.m_nextKey  = 0LL; // assume we are the first for this hostname
	ce.m_ds       = NULL;
	ce.m_listSize = 0;
	// always inc now no matter what now so no danger of re-use
	s_bogus++;
	// if we are the first guy requesting the ip for this hostname
	// then use "hostKey" to get the slot to store "ce", 
	int64_t finalKey = hostKey64 ;
	// otherwise use "s_bogus" as the key. the bogus key is just for 
	// getting a slot to use to store "ce".
	if ( ptr ) {
		// let's hash it up for efficiency
		finalKey = hash64 ( (char *)&s_bogus,8);
		// never let this be 0
		if ( finalKey == 0 ) finalKey = 1LL;
		// bogus should never equal a key.n0 for any request, otherwise
		// that is a collision. to avoid this possibility keep its hi 
		// bit set, and hi bit clear on the key.n0 key (hostKey). this
		// way, a waiting slot can never collide with any other slot.
		finalKey |= 0x8000000000000000LL;
	}

	// BUT if we are looking up a dns server's ip, then NEVER wait in
	// line because we could deadlock!
	if ( dnsLookup ) {
	loop:
		finalKey  = hash64 ( hostKey64 , s_antiLockCount++ );
		// it is not waiting in anyone's line, so turn this bit off
		finalKey &= 0x7fffffffffffffffLL;	
		// ensure not 0
		if ( finalKey == 0 ) finalKey = 1;
		// assume hostKey is not in the table, even though it
		// may be, we cannot wait in line behind it
		ptr = NULL;
		// ensure no collision, if so, s_antiLockCount will be
		// different now so hash again until we do not collide
		if ( s_dnstable.getValuePointer ( finalKey ) )
			goto loop;
	}

	// assume we have no parent
	int64_t parentKey = 0;
	// if parent, set parentKey to "hostKey", the hash of the hostname
	if ( ptr ) parentKey = hostKey64;

	// make sure we do not have a circular dependency if we are looking
	// up the ip of a dns in order to ask him the ip of what we are
	// looking up.
	// EXAMPLE: 
	// 1. get ip of xyz.com
	// 2. have to ask dns1.xyz.com
	// 3. to get his ip we have to ask dns2.xyz.com 
	// 4. and to get his ip we have to as dns1.xyz.com
	// 5. which we'll see that it is already outstanding in the hashtable,
	//    i.e., it has a parent in there, and it will just wait in line
	//    never to get out of it, if it were not for the following circular
	//    dependency check:
	// example url: www.hagener-schulen.de
	int32_t loopCount = 0;
	// loopCount is how many times we've had to ask for the ip of a 
	// nameserver recursively.
	if ( ds ) loopCount = ds->m_loopCount;
	// point to the current DnsState
	char *parent = (char *)ds;
	// the DnsState was built to hold a few DnsStates in it for just
	// this purpose, so we can "backup" to our "parents" and make sure
	// they did not initiate this linked list. Search for "ds2" below
	// to see where we initiate the recursion.
	while ( ptr && loopCount-- > 0 ) {
		// the recursive "ds"es occupy DnsState::m_buf of their 
		// containing DnsState. go back one.
		parent -= ((char *)ds->m_buf - (char *)ds);
		// sanity check
		//if ( ((DnsState *)parent)->m_buf != (char *)ds ) {
		//	char *xx = NULL; *xx = 0; }
		// do we have the circular dependency?
		if ( parent == (char *)ptr->m_ds ) {
			g_errno = EBADENGINEER;
			log(LOG_DEBUG,"dns: Caught circular dependency.");
			return true;
		}
	}

	// debug msg
	log(LOG_DEBUG,"dns: Adding key %"UINT64" from table. "
	    "parentKey=%"UINT64" callback=%"PTRFMT" state=%"PTRFMT".",
	    finalKey,parentKey,(PTRTYPE)callback,(PTRTYPE)state);
	// ensure "bogus" key not already present in table, otherwise,
	// addKey will just overwrite the value!!
	while ( ptr && s_dnstable.getValuePointer ( finalKey ) ) {
		log("dns: Got collision on incremental key.");
		finalKey += 1LL;
		finalKey |= 0x8000000000000000LL;
	}
	// we need to be able to add ourselves to the table so our callback
	// can get called, otherwise it is pointless. this returns false
	// and sets g_errno on error.
	int32_t slotNum = -1;
	if ( ! s_dnstable.addKey ( finalKey , ce , &slotNum ) ) {
		log("dns: Failed to add key to table: %s.",mstrerror(g_errno));
		return true;
	}
	// get the value from the slot so we can insert into linked list.
	CallbackEntry *ppp = s_dnstable.getValuePointerFromSlot ( slotNum );
	// sanity check
	if ( ppp->m_callback != callback || ppp->m_state != state  ) {
		log("dns: Failed sanity check 3.");
		char *xx = NULL; *xx = 0; 
	}
	// adding a key may have changed the parent ptr... get again just
	// in case
	if ( ptr ) {
		ptr = s_dnstable.getValuePointer ( hostKey64 );
		// sanity check - it should still be there for sure
		if ( ! ptr ) { char *xx = NULL; *xx = 0; }
	}
	// . insert into beginning of the linked list to avoid having to scan
	// . "ptr" is a ptr to the parent CallbackEntry, head of linked list
	if ( ptr ) {
		int64_t oldNext = ptr->m_nextKey;
		ptr->m_nextKey    = finalKey;
		ppp->m_nextKey    = oldNext;
		// let parent know how big its linked list is
		ptr->m_listSize++;
		// propagate the list id, it is stored in the parent node
		// so put it into us, too
		ppp->m_listId = ptr->m_listId;
		if ( g_conf.m_logDebugDns )
			log(LOG_DEBUG,"dns: Waiting in line for %s. key=%"UINT64". "
			    "nextKey=%"UINT64" listSize=%"INT32" listId=%"INT32" "
			    "numSlots=%"INT32".",
			    tmp,finalKey,oldNext,
			    ptr->m_listSize,ptr->m_listId,
			    s_dnstable.getNumSlots());
		// ok, we block now, waiting for the initial callback
		return false;
	}

	// init our linked list size count
	ppp->m_listSize = 1;
	// it is the parent, use 0 to indicate none
	static int32_t s_listId = 0;
	ppp->m_listId = s_listId++;

	// . make a DnsState
	// . set g_errno and return true on malloc() error
	if ( ds ) 
		ds->m_freeit = false;
	else {
		ds = (DnsState *) mmalloc ( sizeof(DnsState ), "Dns" );
		if ( ! ds ) {
			log("dns: Failed to allocate mem for ip lookup.");
			// debug msg
			log(LOG_DEBUG,"dns: Removing2 key %"UINT64" from table. "
			    "parentKey=%"UINT64" callback=%"PTRFMT" state=%"PTRFMT".",
			    hostKey64,parentKey,
				(PTRTYPE)callback,(PTRTYPE)state);
			s_dnstable.removeKey ( finalKey );
			return true;
		}
		ds->m_freeit = true;
		// keep track of how many times we pluck out a DnsState
		// from DnsState::m_buf.
		ds->m_loopCount = 0;
		ds->m_startTime = getTime();//time(NULL);//getTimeLocal();
	}

	// so monitor.cpp can avoid caching not founds or timeouts in case
	// the network goes down on gk267
	ds->m_cacheNotFounds = cacheNotFounds;

	// set the ce.m_ds to our dns state so if a key collides later
	// we can check DnsState::m_hostname. actually i think this is only
	// used for sanity checking now.
	ppp->m_ds = ds;
	// reset this stuff
	ds->m_numDnsIps  [0] = 0;
	ds->m_numDnsNames[0] = 0;
	ds->m_depth        = 0;
	ds->m_numTried     = 0;
	ds->m_nameBufPtr   = ds->m_nameBuf ;
	ds->m_nameBufEnd   = ds->m_nameBuf + 512;
	ds->m_errno        = 0;
	ds->m_recursionDesired = 1;
	// debug msg
	//log("dns::getIp: %s (key=%"UINT64") NOT in cache...",tmp,key.n0);

	// reset m_loopCount and startTime if we are just starting
	if ( ds && callback != gotIpOfDNSWrapper ) {
		ds->m_loopCount = 0;
		ds->m_startTime = getTime();//time(NULL);//getTimeLocal();
	}

	// set caller callback info
	// hostKey96 and finalKey are basically the same thing for hostnames
	// that are NOT waiting in line. but finalKey is the lower 64 bits
	// of hostKey96, but finalKey should have its hi bit cleared to 
	// indicate it is not waiting in line. Also, if looking up the IP
	// of a dns, dnsLookup is true, and finalKey is hashed with a
	// special count to give a unique hash because we can not have
	// dnsLookups waiting in line because of deadlock issues.
	// search for "dns A" below to see what i'm talking about.
	ds->m_hostnameKey = hostKey96;
	ds->m_tableKey    = finalKey;
	ds->m_this        = this;
	ds->m_state       = state;
	ds->m_callback    = callback;	
	int32_t newlen = hostnameLen;
	if ( newlen > 127 ) newlen = 127;
	gbmemcpy ( ds->m_hostname , hostname , newlen );
	ds->m_hostname [ newlen ] = '\0';

	// copy the sendBuf cuz we need it in gotIp() to ensure hostnames match
	//char *copy = (char *) mdup ( msg , msgSize , "Dns" );
	//if ( ! copy ) { 
	//	if ( ds->m_freeit ) mfree (ds,sizeof(DnsState),"Dns"); 
	//	return -1; 
	//}
	// hack this for now
	//int32_t numDns = 0; 
	//int32_t dnsIps[MAX_DNSIPS];

	// copy the initial nameserver ips into ds->m_dnsIps[0] (depth 0)
	if ( g_conf.m_askRootNameservers ) {
		// ROOT TLD CACHE ATTEMPT GOES HERE...
		// this will fill in depth 1 in the query,
		// if we have the nameservers cached...
		log(LOG_DEBUG,"dns: hostname %s", ds->m_hostname);
		gbmemcpy(ds->m_dnsIps[0],g_conf.m_rnsIps, g_conf.m_numRns * 4);
		ds->m_numDnsIps[0] = g_conf.m_numRns;
		ds->m_numDnsNames[0] = 0;
		ds->m_rootTLD[0] = true;
		ds->m_fallbacks[0] = 0;
		// if a TLD is cached, copy it to depth 1
		const TLDIPEntry*	tldip	=	getTLDIP(ds);
		if (tldip) {
			gbmemcpy( ds->m_dnsIps[1],
				tldip->TLDIP,
				tldip->numTLDIPs * sizeof(uint32_t));
			ds->m_numDnsIps[1] = tldip->numTLDIPs;
			ds->m_numDnsNames[1] = 0;
			ds->m_rootTLD[1] = true;
			ds->m_fallbacks[1] = 0;
			ds->m_depth = 1;
		}
	}
	// otherwise, use the local bind9 servers
	else {
		//gbmemcpy(ds->m_dnsIps[0],g_conf.m_dnsIps,g_conf.m_numDns * 4);
		int32_t numDns = 0;
		for ( int32_t i = 0; i < MAX_DNSIPS; i++ ) {
			if ( g_conf.m_dnsIps[i] == 0 ) continue;
			ds->m_dnsIps[0][numDns] = g_conf.m_dnsIps[i];
			numDns++;
		}
		ds->m_numDnsIps[0] = numDns;
		ds->m_numDnsNames[0] = 0;
		ds->m_rootTLD[0] = false;
		ds->m_fallbacks[0] = 0;
	}

	// return 0 if we block on the reply
	//if ( ! sendToNextDNS ( ds , timeout ) ) return false;
	// timeout from an individual dns is 20 seconds
	if ( ! sendToNextDNS ( ds , TIMEOUT_SINGLE_HOST ) ) return false;

	// debug msg
	log(LOG_DEBUG,"dns: Removing3 key %"UINT64" from table. "
	    "parentKey=%"UINT64" callback=%"PTRFMT" state=%"PTRFMT".",
	    hostKey64,parentKey,
		(PTRTYPE)callback,(PTRTYPE)state);
	// if we made it here, remove from table
	s_dnstable.removeKey ( finalKey ) ;
	// should we free it
	if ( ds->m_freeit ) mfree ( ds , sizeof(DnsState) ,"Dns" );
	// ok, g_errno should still be set, return true as specified
	return true;
}

// . returns false if blocked, sets g_errno and returns true otherwise
// . this is called by sendToNextDNS() when it has to get the ip of the DNS to
//   send the request to.
bool Dns::getIpOfDNS ( DnsState *ds ) {
	// bail if none
	if ( ds->m_numDnsNames[ds->m_depth] <= 0 ) {
		log(LOG_DEBUG, "dns: no dnsnames for '%s'",
			ds->m_hostname);
		return true;
	}
	// use the secondary ds for doing this
	DnsState *ds2 = (DnsState *)ds->m_buf;
	// do not keep getting the ip of the ns which may require us to get
	// the ips of its ns, etc...
	if ( ds->m_loopCount >= 3 ) {
		addToCache ( ds->m_hostnameKey , -1 );
		g_errno = EBADENGINEER;
		log(LOG_INFO,"dns: Hit too many authority redirects for %s.",
		    ds->m_hostname);
		return true;
	}
	// sanity check
	if ( LOOP_BUF_SIZE / (sizeof(DnsState) - LOOP_BUF_SIZE) < 3 ) {
		log("dns: Increase LOOP_BUF_SIZE, %"INT32", in Dns.h.",
		    (int32_t)LOOP_BUF_SIZE);
		char *xx = NULL; *xx = 0; 
	}
	// increment the loop count, we can only use m_buf so many times
	// before running out of room.
	ds2->m_loopCount = ds->m_loopCount + 1;
	// set start time for timing out
	ds2->m_startTime = ds->m_startTime;
	// or if we have too many ips already, do not bother adding more
	if (ds->m_numDnsIps[ds->m_depth]>=MAX_DNS_IPS){
		log("dns: Already have %"INT32" ips at depth %"INT32".",
		    (int32_t)MAX_DNS_IPS,(int32_t)ds->m_depth);
		g_errno=EBUFTOOSMALL;
		return true;
	}
	// do not do this!  this will break!
	// int32_t n         = ds->m_hostnameKey.n0 % numNames;
	int32_t  n           = 0; // first is usually ns1, usually better
 loop:
	// get the name to get the ip for
	int32_t  depth       = ds->m_depth;
	int32_t  numNames    = ds->m_numDnsNames[depth];
	char *hostname    = ds->m_dnsNames[depth][n];
	int32_t  hostnameLen = gbstrlen(hostname);
	int32_t  ip          = 0;
	// loop over all dnsnames in case one causes a circular dependency
	// . remove him from the array so we do not do him again
	// . actually, this is not a guarantee, so we put a circular
	//   dependency check in getIP() above
	ds->m_dnsNames   [depth][n] = ds->m_dnsNames[depth][numNames-1];
	ds->m_numDnsNames[depth]--;
	//ds->m_numTried++;
	//if (ds->m_numTried > .....
	// debug note
	log(LOG_DEBUG,"dns: Getting ip address of dns, %s for %s.",
	    hostname,ds->m_hostname);
	// . returns -1 and sets g_errno on error
	// . returns 0 if transaction blocked, 1 if completed
	// . returns 0 if you must wait
	// . calls gotIp with ip when it gets it or timesOut or errors out
	// . set *ip to 0 if none (does not exist)
	// . keep the timeout down to only 5 secs
	// . do not set a mutual exclusion lock on ip lookups of dns servers
	//   in order to avoid having to lookups locking each other up. like
	// . 1. we are getting the ip of dns A for resolve of hostname #1
	// . 2. we are getting the ip of dns B for resolve of hostname #2
	// . 3. dns A says to ask B and B says to ask A, we end up in
	//      a deadlock
	if ( !g_dns.getIp ( hostname ,
			    hostnameLen ,
			    &ip         ,
			    ds         ,//state
			    gotIpOfDNSWrapper , //state,ip
			    ds2 ,
			    5   , // timeout
			    true )) { // dns lookup?
	     log(LOG_DEBUG, "dns: no block for getIp for '%s'", hostname);
	     return false;
	}

	// if that would cause a circulare dependency, try the next one
	if ( g_errno == EBADENGINEER ) {
		if ( ds->m_numDnsNames[depth] ) {
			log("dns: looping in getIpOfDns for '%s'",
				hostname);
			goto loop;
		}
		else
			log("dns: No names left to try after %s",hostname);
	}


	// did it have an error? g_errno will be set
	// . if ip is 0 it was a does not exist
	// . add it to the array of ips
	if ( ip != 0 && ds->m_numDnsIps[depth] + 1 < MAX_DNS_IPS) {
		if (isTimedOut(ip)) {
			log(LOG_DEBUG,
			    "dns: Not adding [1] ip %s - timed out",
			    iptoa(ip));
		}
		else {
			int32_t depth = ds->m_depth;
			log(LOG_DEBUG,
				"dns: Added ip [1-%"INT32"] %s to depth %"INT32" for %s.",
		    		ds->m_numDnsIps[depth],
		    		iptoa(ip),(int32_t)depth,ds->m_hostname);
			ds->m_dnsIps[depth][ds->m_numDnsIps[depth]++] = ip ;
		}
	}
	// we did not block
	return true;
}

void gotIpOfDNSWrapper ( void *state , int32_t ip ) {
	DnsState *ds = (DnsState *)state;
	// log debug msg
	//DnsState *ds2 = (DnsState *)ds->m_buf;
	log(LOG_DEBUG,"dns: Got ip of dns %s for %s.",
	    iptoa(ip),ds->m_hostname);
	// sanity check
	if ( ds->m_numDnsIps[ds->m_depth] + 1 >= MAX_DNS_IPS ) {
		log("dns: Wierd. Not enough buffer.");
		char *xx = NULL; *xx = 0; 
	}
	// . if ip is 0 it was a does not exist
	// . add it to the array of ips
	if ( ! g_errno && ip != 0 &&
	     ds->m_numDnsIps[ds->m_depth] + 1 < MAX_DNS_IPS) {
		if (isTimedOut(ip)) {
			log(LOG_DEBUG,
			    "dns: Not adding [2] ip %s - timed out",
			    iptoa(ip));
		}
		else {
			int32_t depth = ds->m_depth;
			ds->m_dnsIps[depth][ds->m_numDnsIps[depth]++] = ip ;
			log(LOG_DEBUG,
				"dns: Added ip [2-%"INT32"] %s to depth %"INT32" for %s.",
				ds->m_numDnsIps[depth],
		    		iptoa(ip),(int32_t)depth,ds->m_hostname);
		}
	}
	// disregard any g_errnos cuz we will try again
	g_errno = 0;
	// just return if this blocks
	if ( ! g_dns.sendToNextDNS ( ds , 20 ) ) {
		log(LOG_DEBUG, "dns: sendToNextDns blocking for '%s'",
			ds->m_hostname);
		return ;
	}
	// if that does not block, then we are done... we got the final ip
	// or g_errno is set. so call the callbacks.
	log(LOG_DEBUG, "dns: getIpOfDNSWrapper calling returnIp for '%s'",
		ds->m_hostname);
	returnIp ( ds , ip );
	// . otherwise, we must call the callback
	// . call the callback w/ state and ip if there is one
	// . g_errno may be set
	//if ( ds->m_callback ) ds->m_callback ( ds->m_state , ip );
	// free our state holding structure
	//if ( ds->m_freeit ) mfree ( ds , sizeof(DnsState) ,"Dns" );
}

// returns false if blocked, sets g_errno and returns true otherwise
bool Dns::sendToNextDNS ( DnsState *ds , int32_t timeout ) {
	//log(LOG_DEBUG, "dns: sendToNextDNS depth %d", ds->m_depth);
	// let's clear g_errno since caller may have set it in gotIp()
	g_errno = 0;
	// if we have been at this too long, give up
	int32_t now = getTime(); // time(NULL);//getTimeLocal();
	int32_t delta = now - ds->m_startTime;
	// quick fix if the system clock was changed on us
	if ( delta < 0   ) ds->m_startTime = now;
	//if ( delta > 100 ) ds->m_startTime = now;
	if ( delta > TIMEOUT_TOTAL  ) {
		log(LOG_DEBUG,"dns: Timing out the request for %s. Took over "
		    "%"INT32" seconds. delta=%"INT32". now=%"INT32".",
		    ds->m_hostname,(int32_t)TIMEOUT_TOTAL,delta,now);
		if ( ds->m_errno ) g_errno = ds->m_errno;
		else               g_errno = EDNSTIMEDOUT; 
		return true;
	}
	// if we have no more room to add to tried array, we're done,
	// we've tried to ask too many nameservers already
	if ( ds->m_numTried >= MAX_TRIED_IPS ) {
		log(LOG_INFO,"dns: Asked maximum number of name servers, "
		     "%"INT32", for %s. Timing out.",(int32_t)MAX_TRIED_IPS,
		    ds->m_hostname);
		if ( ds->m_errno ) g_errno = ds->m_errno;
		else               g_errno = EDNSTIMEDOUT; 
		return true;
	}
	// get the current depth. if we exhaust all nameserver ips at this
	// depth we may have to decrease it until we find some nameservers
	// we haven't yet asked.
	int32_t depth = ds->m_depth;

 top:
 	log(LOG_DEBUG, "dns: at 'top' for '%s'", ds->m_hostname);
	int32_t n = -1;
	// how many ip do we have at this depth level? save this for 
	// comparing down below.
	int32_t numDnsIps = ds->m_numDnsIps[depth];
	// each DnsState has a list of ips of the nameservers to ask
	// but which one we ask first depends on this hash
	if ( ds->m_numDnsIps[depth] > 0 ) {
		// easy var
		int32_t num = ds->m_numDnsIps[depth];
		// . pick the first candidate to send to
		// . this should not always be zero because picking the groupId
		//   and hostId to send to is now in Dns::getResponsibleHost()
		//   and uses key.n1 exclusively
		n = ds->m_hostnameKey.n0 % num;
		// conenvience ptr
		int32_t *ips = ds->m_dnsIps[depth];
		// save
		int32_t orign = n;
		do {
			if (!isTimedOut(ips[n]))
				break;
			// note it
			log(LOG_DEBUG,
		    		"dns: skipping ip %s - timed out",
		    		iptoa(ips[n]));
			// advance
			if ( ++n >= num )
				n = 0;
		} while (n != orign);
	}

	// . save n for wrap check below, to make sure we do not re-ask him
	// . this may be -1 if we did not have any dns ips to pick from
	int32_t startn = n;

	// is the nth ip the next best candidate to send the request to?
 checkip:
	// get the nth ip
	int32_t ip = 0;
	if ( n >= 0 ) ip = ds->m_dnsIps[depth][n];
	// loop over all the ips of nameservers we've already tried and make 
	// sure this one is not one of them. only check if n is valid (>=0)
	bool tried = false;
	for ( int32_t i = 0 ; n >=0 && i < ds->m_numTried ; i++ ) {
		// check next tried ip if this one does not match.
		if ( ip != ds->m_triedIps[i] ) continue;
		// we've already tried this ip, do not send to it again
		tried = true;
		break;
	}

	// advance n if we already tried its ip, or if its ip is bogus.
	// only do this if n >= 0 already, though. if it is -1 that means we
	// have no candidates at this depth level
	if ( n >= 0 && (tried || ip == 0 || ip == -1) ) {
		// advance n
		if ( ++n == ds->m_numDnsIps[depth] ) n = 0;
		// if still a bogus ip, keep advancing
		// but if we wrap, there are no valid n's
		if ( n != startn ) goto checkip;
		// set n to -1 to indicate no new and good ip available at
		// this depth level
		n = -1;
	}

	// if no ips to try, try the canonical names if there are some
	if ( n == -1 && ds->m_numDnsNames[depth] > 0 ) {
		// . return false if this blocked
		// . this will remove the names from m_numDnsNames and 
		//   put them in as ips into m_dnsIps.
		// . it will decrease m_numDnsNames
		// . this returns true if it does not block
		if ( ! getIpOfDNS ( ds ) ) {
			log(LOG_DEBUG, "dns: SendToNextDns blocked on "
				"getIpOfDNS for '%s'", ds->m_hostname);
			return false;
		}
		// getIpOfDNS may have set g_errno. it, as of this writing,
		// only does that if we have too many ips already, 
		// MAX_DNS_IPS, so in that case, just ignore it and go down a 
		// level, it will not have added a new ip.
		// NOTE: it will also set to EBADENGINEER if it caught
		// a circular dependency. (see above)
		g_errno = 0;
		// ok, did we gain a new ip to try? if so, try run it through
		// from the top.
		if ( ds->m_numDnsIps[depth] > numDnsIps ) {
			log(LOG_DEBUG, "dns: "
			    "SendToNextDNS going back to top 0 "
			    "for '%s'", ds->m_hostname);
			goto top;
		}
		// if you made it here, we did not gain a new ip, probably 
		// because of error.  n is still -1 to indicate no candidate 
		// at this depth level.
	}

	// ok, if we have no dns ips or hostnames to try at this point on
	// this depth level then go back up to the previous level and see if 
	// those nameservers can recommend another set of nameservers to try.
	// but this will be -1 if there are no more left to ask, and we will
	// send back a EDNSTIMEDOUT error above.
	if ( n == -1 ) {
		depth--;
		// decrease the depth, this may be the end of the chain... and
		// we do not want to chain through all the root servers at depth 0
		// because they are almost never wrong i guess.
		if ( depth <= 0 ) {
			log(LOG_DEBUG,"dns: Exhausted all chains except "
		    	"root. Giving up for %s",ds->m_hostname);
			if ( ds->m_errno ) g_errno = ds->m_errno;
			else               g_errno = EDNSTIMEDOUT; 
			return true;
		}

		// log this...
		ds->m_fallbacks[depth]++;
		log(LOG_DEBUG,
			"dns: depth %"INT32"/%"INT32" rootTLD %d #fb %"INT32" #ip %"INT32" #name %"INT32"",
			depth, ds->m_depth,
			ds->m_rootTLD[depth],
			ds->m_fallbacks[depth],
			ds->m_numDnsIps[depth],
			ds->m_numDnsNames[depth]);

		// too many fallbacks on root or TLD nameservers?
		if ( ds->m_rootTLD[depth] &&
		     ds->m_fallbacks[depth] > 2) {
			log(LOG_DEBUG,
				"dns: too many fallbacks on rootTLD. "
				"Giving up for %s.",
				ds->m_hostname);
			if ( ds->m_errno ) g_errno = ds->m_errno;
			else               g_errno = EDNSTIMEDOUT; 
			return true;
		}
	}

	// ok, we have more chains to explore starting at this decreased depth
	// so take it from the top, "depth" as been decreased.
	if ( n == -1 ) {
		log(LOG_DEBUG, "dns: "
		    "SendToNextDNS going back to top 1 for '%s'",
			ds->m_hostname);
		goto top;
	}

	// sanity check
	if ( ip != ds->m_dnsIps[depth][n] ) { char *xx = NULL; *xx = 0; }
	// alright, we got a valid ip to send the request to.
	// mark this ip we are about to ask as tried.
	ds->m_triedIps[ds->m_numTried++] = ip; //ds->m_dnsIps[depth][n];
	// record our current depth in case it changed
	ds->m_depth = depth;
	// this 512 byte buffer is now part of the DnsState
	char *msg = ds->m_request;

	// . the dns header has this format:
        // . u_int16_t dns_id;          /* client query ID number */
        // . u_int16_t dns_flags;       /* qualify contents <see below> */
        // . u_int16_t dns_q_count;     /* number of questions */
        // . u_int16_t dns_rr_count;    /* number of answer RRs */
        // . u_int16_t dns_auth_count;  /* number of authority RRs */
        // . u_int16_t dns_add_count;   /* number of additional RRs */

	// HACK: if udpserver's transId is too big for us, reset it
	if(m_udpServer.m_nextTransId > 65535 ) 
		m_udpServer.m_nextTransId =0;
	// . first word is id (not really that releveant since queried domain
	//   should also be in response)
	// . steal the transId from our g_udpServer
	uint16_t transId = m_udpServer.m_nextTransId;
	// . *(uint16_t *) msg = htons ( m_dnsTransId++ );
	// . UdpServer will inc it's m_transId in UdpServer::sendRequest()
	//   when it calls getTransId()
	*(uint16_t *) msg = htons ( transId );
	// . some fancy foot work (big endian here) this byte is msg[2]
	// .     qr: 1; /* response flag (high bit)*/
	// . opcode: 4; /* purpose of message */   0 = query, 1=invQuery, ...
	// .     aa: 1; /* authoritative answer */
	// .     tc: 1; /* truncated message */
	// .     rd: 1; /* recursion desired (low bit)*/	
	// ---------------------------------------- this byte is msg[3]
        // .     ra: 1; /* recursion available (high bit)*/
	// . unused: 1; /* unused bits (MBZ as of 4.9.3a3) */
	// .     ad: 1; /* authentic data from named */
	// .     cd: 1; /* checking disabled by resolver */
	// .  rcode: 4; /* response code (low bits)*/
	// ---------------------------------------- 
	// . values of rcode:
	// . "No Error",        /* 0: ok */
        // . "Format Error",    /* 1: bad query */
        // . "Server Failure",  /* 2: server is hosed */
        // . "Name Error",      /* 3: name doesn't exist (authoritative) */
        // . "Not Implemented", /* 4: server doesn't support query */
        // . "Refused"          /* 5: server refused request */
	// some dns'es REFUSE our request if this is set... so keep it 0
	// like www.fsis.usda.gov, however www.altx.com needs it set!
	if ( ds->m_recursionDesired ) msg[2] = 0x01 ;
	else                          msg[2] = 0x00 ;
	// if asking bind9 servers, always set it on
	if ( ! g_conf.m_askRootNameservers ) msg[2] = 0x01;
	// try this fix
	//msg[2] = 0x04;
	msg[3] = 0;
	// rr means resource record
	*(int16_t *)(msg + 4  ) = htons ( 1 ); // we have 1 question
	*(int16_t *)(msg + 6  ) = 0          ; // we have 0 answer    rr's
	*(int16_t *)(msg + 8  ) = 0          ; // we have 0 authority rr's
	*(int16_t *)(msg + 10 ) = 0          ; // we have 0 addition  rr's

	// ask for MX record? used by Emailer in Facebook.cpp.
	char *hostname = ds->m_hostname;
	bool  getmx = false;
	if ( strncmp(ds->m_hostname,"gbmxrec-",8) == 0 ) {
		hostname += 8;
		getmx = true;
	}

	// . we're done populating the dns request HEADER 
	// . now make the QNAME entry (the hostname to get ip for)
	// . break up the hostname by the dots
	// . make "msg" point passed header
	char *start = hostname;
	char *end   = hostname;
	// . point to where to store the length of each name and name itself
	// . a name is a component in the hostname, like the "com" in "x.com"
	u_char *len   = (u_char *)msg+12;
	char   *dest  = msg+13;
	// . now make the query entry
	// . break the hostname into labels and store in dns record style.
	// . basically store length/label pairs
	char *hostEnd = hostname + gbstrlen(hostname);
	while ( start < hostEnd ) {
		while ( *start != '.'  && *start && start < hostEnd ) {
			//log(LOG_DEBUG,"dns: name: %c", *start);
			*dest++ = *start++;
		}
		//log(LOG_DEBUG,"dns: name delimit");
		// . each "name" in the hostname must be less than 64 bytes
		// . the 2 high bits are set for compression
		int32_t nlen = start - end;
		if ( nlen >= 64 ) {
			g_errno = EHOSTNAMETOOBIG;
			log(LOG_INFO,"dns: Request's host component is %"INT32" bytes. "
			    "Must be under 64.",nlen);
			return true;
		}
		// store the length as a byte
		*len = (u_char)nlen ;
		// advance the length
		len = (u_char *)dest;
		// advance the dest over the length
		dest++;
		// skip start over the . or \0
		start++;
		// set the end to to the beginning of the next name
		end = start;
	}
	// store a 0 at the end
	len[0] = 0;
	// . now store queryType (2 bytes) and queryClass (2 bytes)
	// . a query type of 0 is "host address" query (see nameser.h)
	// . a queryClass of 1 means the arpa internet (see nameser.h)
	// . watch out for alignment
	len[1] = 0; // query type  (network order)
	if ( getmx ) len[2] = 15; // query type  (network order)
	else         len[2] = 1;
	len[3] = 0; // query class (network order)
	len[4] = 1; // query class (network order)
	// compute the msgSize
	//int32_t msgSize = (char *)len - msg + 5 ;
	ds->m_requestSize = (char *)len - msg + 5 ;
	// copy the msg into an alloc'd buffer
	// char *copy = mdup ( msg , msgSize );
	// if ( ! copy ) return true;

	// debug msg
	log(LOG_DEBUG,
		"dns: Asking %s (depth=%"INT32",%"INT32") att %"INT32" "
		"for ip of %s (tid=%"INT32")",
		iptoa(ds->m_dnsIps[depth][n]), (int32_t)depth,(int32_t)n,
		(int32_t) ds->m_numTried, ds->m_hostname , (int32_t)transId);

	UdpSlot *slotPtr = NULL;
	// . queue a send
	// . this returns false and sets g_errno on error
	// . calls callback when reply is received
	// . resend timeout for non-ack protocols (dns) is 10 secs in UdpSlot
	// . seems like 60 second timeout is about right, 
	// . seems like dns really slows down when we're looking up failed ips
	//   while doing url retries
	// . well, i went back to 30 seconds after i fixed the transId overflow
	//   bug
	// . resend time is set to 20 seconds in UdpSlot::setResendTime()
	if ( ! m_udpServer.sendRequest ( ds->m_request     ,//copy , 
					 ds->m_requestSize,//msgSize , 
					 0              , // msgType
					 //g_conf.m_dnsIps  [n] , 
					 ip , // ds->m_dnsIps[depth][n] ,
					 53       ,//g_conf.m_dnsPorts[n], 
					 -1             ,// invalid host id
					 &slotPtr       , // slot ptr
					 ds             , // cback state
					 gotIpWrapper   , // callback
					 TIMEOUT_SINGLE_HOST , // 20 secs?
					 -1, // backoff
					 -1, // maxWait
					 NULL, // replyBuf
					 0, // replyBufMaxSize
					 // use niceness 0 now so if the
					 // msgC slot gets converted from 1
					 // to 0 this will not hold it up!
					 0) ) {
		// g_errno should be set at this point and we will not try
		// any more nameservers because the error seemed too bad.
		log(LOG_DEBUG, "dns: errors seemed too bad for '%s'...",
			ds->m_hostname);
		return true;
	}
	// store a hack for PageSockets.cpp to print out the hostname
	slotPtr->m_tmpVar = ds->m_hostname;
	// return 0 cuz we're blocking on the reply
	log(LOG_DEBUG, "dns: SendToNextDNS blocking on reply for '%s'",
		ds->m_hostname);
	return false;
}

#define BACKUPDNS "8.8.8.8"
//#define BACKUPDNS "64.22.106.82"

void gotIpWrapper ( void *state , UdpSlot *slot ) {
	DnsState *ds = (DnsState *) state;
	log(LOG_DEBUG, "dns: gotIpWrapper for '%s'", ds->m_hostname);
	//log(LOG_DEBUG, "dns: gotIpWrapper depth %d", ds->m_depth);
	// never let udpserver free the send buffer, we own that, it is
	// ds->m_request
	slot->m_sendBufAlloc = NULL;
	// get our Dns server
	// Dns *THIS = ds->m_this;
	// set ip to -1 to indicate dns transaction error
	int32_t ip = -1;
	// THIS is obsolete because we got s_dnstable now.
	// sometimes a parallel request might have thrown it in the cache
	// and we timeout because the dns is pissed at us hitting it with
	// requests all the time.
	//if (g_errno&& g_dnsDistributed.isInCache ( ds->m_hostnameKey, &ip ) )
	//	g_errno = 0;
	// might as well check local, too
	//if ( g_errno && g_dnsLocal.isInCache ( ds->m_hostnameKey, &ip ) )
	//	g_errno = 0;

	// get the ip from slot if no error
	if ( ! g_errno ) ip = g_dns.gotIp ( slot , ds );

	// . if we timed out change it to a more specific thing
	// . an ip of -1 (255.255.255.255) means it timed out i guess
	// . an ip of -1 could also be a SERVFAIL reply too, in which case
	//   g_errno will not be set...
	if ( g_errno == EUDPTIMEDOUT || ip == -1 ) {
		// log it so we know which dns server had the problem
		if ( g_errno ) {
			log(LOG_DEBUG,"dns: dns server at %s timed out.",
			    iptoa(slot->m_ip));
			g_errno = EDNSTIMEDOUT;
		}
		else {
			log(LOG_DEBUG,"dns: dns server at %s failed.",
			    iptoa(slot->m_ip));
		}
		// try again? yes, if we timed out on router1's bind9
		int32_t len = gbstrlen(BACKUPDNS);
		if ( ds->m_dnsIps[0][0] != atoip(BACKUPDNS,len) ) {
			g_errno = ETRYAGAIN;
			//ds->m_depth++;
			// note it
			log("dns: trying backup-dns %s (old=%s)",
			    BACKUPDNS,iptoa(ds->m_dnsIps[0][0]));
			// try google's public dns
			ds->m_dnsIps[0][0] = atoip(BACKUPDNS,len);
		}
	}
	// debug msg
	//log("got %s [%"UINT32"] for %s",iptoa(ip),ip,ds->m_hostname );
	// bitch on error
	if ( g_errno ) {
		//int32_t type = LOG_WARN;
		// . these can be those SERVFAIL messages we get from nslookup
		//   usually when a domain name has disappeared from radar
		// . might also happen if recursive-clients is set too low
		//   on a dns server
		//if ( g_errno == EDNSDEAD     ) type = LOG_INFO;
		// this happens so much... not necessarily an error, but it
		// certainly can be if our dns is dead
		//if ( g_errno == EDNSTIMEDOUT ) type = LOG_INFO;
		if ( g_errno != ETRYAGAIN )
			log(LOG_DEBUG,"dns: %s: %s.",
			    ds->m_hostname,mstrerror(g_errno));
		// save this for returning later
		if (g_errno != ETRYAGAIN &&
		    g_errno != EDNSTIMEDOUT &&
		    g_errno != EUDPTIMEDOUT )
		    //g_errno != ETIMEDOUT ) // == DNSDEAD
			ds->m_errno = g_errno;
		// mdw
		if ( (g_errno == EUDPTIMEDOUT || g_errno == EDNSTIMEDOUT) &&
		     // do not do this if we our hitting our local bind9 
		     // server. this was adding google's 8.8.8.8 and it
		     // was then logging "skipping ip 8.8.8.8 - timed out"
		     // and we were missing out!
		     g_conf.m_askRootNameservers ) {
			int32_t timestamp = getTime();
			key_t k;
			k.n0 = 0LL;
			k.n1 = slot->m_ip;
			static char *s_data = "1111";
			log(LOG_DEBUG,
			    "dns: adding ip %s to timedout cache: %s",
			    iptoa(slot->m_ip),mstrerror(g_errno));
			g_timedoutCache.addRecord((collnum_t)0,
						  k           , // key
						  s_data      , // value
						  4           , // value size
						  timestamp   );
		}

		// . send to the next nameserver in line
		// . this returns false if blocks, returns true and sets
		//   g_errno otherwise. does recursion. 
		// . do not send to next guy if we got EDNSDEAD because that
		//   means the hostname does not exist
		// . ok, i've seen too many false positives with EDNSDEAD,
		//   so let's try again on that too!
		// . set timeout to 10 seconds
		if ( //g_errno != EDNSDEAD &&
		     ! g_dns.sendToNextDNS(ds,20) )  {
		     	log(LOG_DEBUG, "dns: gotIpWrapper blocking on "
				"SendToNextDNS for '%s'", ds->m_hostname);
			return;
		}
	}
	// . otherwise, we must call the callback
	// . call the callback w/ state and ip if there is one
	// . g_errno may be set
	// . if we are just getting the ip of a dns server in the chain, then
	//   call the callback now, it is not the *final* callback.
	//if ( ds->m_callback == gotIpOfDNSWrapper ) {
	//	ds->m_callback ( ds->m_state , ip );
	//	return;
	//}

	log(LOG_DEBUG, "dns: gotIpWrapper calling returnIp for '%s'",
		ds->m_hostname);
	// call the callbacks
	returnIp ( ds , ip );
}

// caller should set g_errno because we call the callbacks here
void returnIp ( DnsState *ds , int32_t ip ) {
	// ok, we got the final answer at this point
	// debug msg
	char *pre = "";
	if ( ip == 0 ) pre = " [NXDOMAIN]";
	if ( ip == -1 ) pre = " [DNSTIMEDOUT|SERVFAIL]";
	if ( g_conf.m_logDebugDns ) {
		char ipbuf[128];
		sprintf(ipbuf,"%s",iptoa(ds->m_dnsIps[0][0]));
		log(LOG_DEBUG,"dns: Got FINAL ANSWER of %s for %s %s(%s) "
		    "from dns %s.",
		    iptoa(ip),ds->m_hostname,pre,mstrerror(g_errno),ipbuf);
	}
	// not thread safe
	//if ( g_threads.amThread() ) { char *xx = NULL; *xx = 0; }
	// save g_errno
	int32_t err = g_errno;
	// if we timed out, cache this so future lookups are fast
	// but only give it a ttl of about a day so we can retry a day later
	// with a fresh lookup. and add a -1 to the cache instead of a zero
	// so we know it is a cached error and not a cached EDNSDEAD, which
	// means the hostname does not exist for sure. (that is not an error
	// really) Well only cache for 10 minutes, we might really need it.
	// if the internet goes down briefly then we end up pretty useless
	// for 10 minutes then! and the spider recs stream on by. so let's
	// comment this out. actually, let's keep it here for 15 seconds
	// so when adding links if they are all from a non-responsive domain
	// like www.castleburyinn.com then we don't wait for a 30 second
	// timeout 100 times in a row. 
	bool cache = false;
	// no longer cache these! i think the spider should evenly sample
	// every other IP address before returning to the timed out IP address...
	// ideally. plus i added the google public dns 8.8.8.8 as a secondary
	// dns ip to fallback to in the case of timeouts i guess... so make
	// sure that's what it does.
	// CRAP, we lookup the dns entry of all the outlinks, so we need this,
	// so leave it there...
	if ( g_errno == EDNSTIMEDOUT || g_errno==EUDPTIMEDOUT ) cache = true;
	// and NEVER cache a timeout on a root server or root TLD server
	if ( g_conf.m_askRootNameservers && ds->m_rootTLD[ds->m_depth] )
		cache = false;
	// monitor.cpp should have option to not cache timeouts!!!!
	if ( ! ds->m_cacheNotFounds ) cache = false;

	// cache for 6 hrs, these things slow us down
	if ( cache ) g_dns.addToCache ( ds->m_hostnameKey, -1, 3600*6 );
	// . otherwise, we must call the callback
	// . call the callback w/ state and ip if there is one
	// . g_errno may be set
	// . no, it will be called below from the hash table
	//if ( ds->m_callback ) ds->m_callback ( ds->m_state , ip );

	// . if the ip request is in progress, wait for it to come back
	// . each bucket in the callback entry hashtable is a linked list of
	//   callback/state pairs (CallbackEntries) waiting for that ip.
	//int64_t key = ds->m_hostnameKey.n0 & 0x7fffffffffffffffLL;
	int64_t key = ds->m_tableKey & 0x7fffffffffffffffLL;
	// was it a dns lookup?
	bool dnsLookup = false;
	// these will be the same if not. i created ds->m_tableKey
	// just so we could make a special hash for such lookups so
	// they would not be waiting in lines and deadlock.
	if ( key != (int64_t)(ds->m_hostnameKey.n0 & 0x7fffffffffffffffLL) )
		dnsLookup = true;
	// look up the entry from the table
	CallbackEntry *ce = s_dnstable.getValuePointer ( key );
	// sanity check
	if ( ! ce ||
	     ce->m_callback != ds->m_callback ||
	     ce->m_state    != ds->m_state    ||
	     ce->m_ds       != ds              ) {
		log("dns: Failed sanity check 1.");
		char *xx = NULL; *xx = 0; 
	}
	// calling the callback for "ds" could free it, so record this now
	bool freeit = ds->m_freeit;
	// and record the hostname here, too, it should be NULL terminated
	char tmp[2048];
	if ( g_conf.m_logDebugDns )
		strncpy ( tmp , ds->m_hostname , 2040 );
	// save parent for debugging purposes
	int64_t parentKey = key;
	// how many in the list?
	int32_t listSize = ce->m_listSize;
	// only the nodes in this list have this id
	int32_t listId   = ce->m_listId;
	// go through each node in the linked list
	int32_t count = 0;
	while ( ce ) {
		// restore g_errno
		g_errno = err;
		// get the next one to call in the linked list
		int64_t nextKey = ce->m_nextKey;
		// debug msg
		log(LOG_DEBUG,"dns: Removing key %"UINT64" from table. "
		    "parentKey=%"UINT64" nextKey=%"UINT64" callback=%"PTRFMT" "
		    "state=0x%"PTRFMT".",
		    key,
		    parentKey,
		    nextKey,
		    (PTRTYPE)ce->m_callback,
		    (PTRTYPE)ce->m_state);
		// get stuff
		void (* callback ) ( void *state , int32_t ip );
		callback    = ce->m_callback;
		void *state = ce->m_state;
		// remove from hash table, so if this was the parent node
		// of the linked list, and "callback" calls getIp() again
		// for this hostname, it will be added to a SEPARATE linked
		// list and not mess us up. otherwise, it may get inserted
		// and NEVER get called.
		s_dnstable.removeKey ( key );
		// debug msg
		log(LOG_DEBUG,"dns: table now has %"INT32" used slots.",
		    (int32_t)s_dnstable.getNumSlotsUsed());
		// then call the callback
		// CAREFUL: calling this callback can alter the hash
		// table (if it is a dns wrapper callback) and move what
		// "ce" points to! SO, get nextKey before calling this...
		//if ( ce->m_callback ) ce->m_callback ( ce->m_state , ip );
		if ( callback ) callback ( state , ip );
		// count for debug purposes
		count++;
		// now that we secured "nextKey" we can delete this guy
		//s_dnstable.removeKey ( key );
		// if nextKey is 0 that is the end of the list. waiting
		// buckets (not firstInLine, that is) always use the "s_bogus"
		// key which starts 1 and is incremented from there
		if ( nextKey == 0LL ) break;
		// nobody is allowed to wait in line of a dns lookup
		if ( dnsLookup ) { 
			log("dns: Failed sanity check 8.");
			char *xx = NULL; *xx = 0;
		}
		// another sanity check
		if ( ! (nextKey & 0x8000000000000000LL) ) {
			log("dns: Failed sanity check 7.");
			char *xx = NULL; *xx = 0;
		}
		// get next guy's value
		ce = s_dnstable.getValuePointer ( nextKey );
		// he better be there, otherwise, core
		if ( ! ce ) {
			log("dns: Failed sanity check 5.");
			char *xx = NULL; *xx = 0;
		}
		// update key
		key = nextKey;
	}

	// debug msg
	if ( g_conf.m_logDebugDns ) 
		log(LOG_DEBUG,"dns: Called %"INT32" callbacks for %s.",
		    count,tmp);

	// free our state holding structure
	if ( freeit ) mfree ( ds , sizeof(DnsState) ,"Dns" );

	// debug check
	if ( count == listSize ) return;

	log("dns: Only called %"INT32" callbacks out of %"INT32". Critical "
	    "error. Scanning table for missing callback.",count,listSize);

 loop:
	// scan for the missing guys
	for ( int32_t i = 0 ; i < s_dnstable.getNumSlots() ; i++ ) {
		// get its key
		key = s_dnstable.getKey(i);
		// skip if empty. a key of 0LL signifies an empty bucket/slot.
		if ( key == 0LL ) continue;
		// get it
		CallbackEntry *e = s_dnstable.getValuePointerFromSlot(i);
		// skip if no match
		if ( e->m_listId != listId ) continue;
		// call its callback
		ce->m_callback ( ce->m_state , ip );
		// note it
		log("dns: Calling callback for slot #%"INT32".",i);
		// remove it. restart from top in case table shrank
		s_dnstable.removeKey ( key );
		goto loop;
	}
		

}

// return NULL and set g_errno on error
char *getRRName ( char *rr , char *dgram, char *end ) {
	static char s_buf[1024];
	char *bufEnd = s_buf + 1024;
	char *dst = s_buf;
	// store into our buffer
	char *p = rr;
	while ( *p && p < end ) {
		// is compression bit set?
		while ( *p & 0xc0 ) {
			// we are jumping within the dgram
			p = dgram +(ntohs(*(int16_t *)p)&0x3fff);
			// bust out of while loop if we should
			if ( !*p || p>=end || p<dgram ) break;
		}
		// watch out for corruption
		if ( dst + *p + 1 > bufEnd ) {
			g_errno = EBADREPLY;
			return NULL;
		}
		// watch out for corruption
		if ( *p < 0 ) {
			g_errno = EBADREPLY;
			return NULL;
		}
		// copy the hostname
		gbmemcpy ( dst , p+1 , *p );
		dst += *p;
		*dst++ = '.';
		p += ((u_char)*p) + 1;
	}
	if ( dst > s_buf && dst[-1] == '.' ) dst--;
	*dst = '\0';
	return s_buf;
}

// . function to parse out the smallest ip from DNS reply
// . returns -1  on error and sets g_errno (could be ETIMEOUT)
// . returns  0 if hostname does not have an ip (non-existant)
// . otherwise, returns the ip
// . NOTE: we also call HostMap::stampHost() here rather than override readPoll
// . TODO: update timestamp for this dns server
int32_t Dns::gotIp ( UdpSlot *slot , DnsState *ds ) {
	log(LOG_DEBUG, "dns: gotIp called for '%s'", ds->m_hostname);
	//log(LOG_DEBUG, "dns: gotIp depth %d", ds->m_depth);
	// get the hostname from the request's query record
	char  *dgram  = (char *) slot->m_sendBuf;
	if ( ! dgram ) {
		log(LOG_DEBUG, "dns: dgram is NULL for '%s'", ds->m_hostname);
		return 0;
	}
	// how many query records in the record heap?
	int16_t qdcount = ntohs (*(int16_t *)(dgram + 4 )) ; 
	// we better have 1 and only 1 query record
	if ( qdcount != 1 ) {
		log(LOG_DEBUG, "dns: more than one query record for '%s'",
			ds->m_hostname);
		return 0; 
	}
	// now get the hostname from reply's query record to see if the same
	dgram = slot->m_readBuf;
	// return -1 and set g_errno if no read buf
	if ( ! dgram ) { 
		g_errno = EBADREPLY; 
		log("dns: Nameserver (%s) returned empty."
		    "reply", iptoa(slot->m_ip));
		return -1; 
	}
	// get the size of the read buf
	int32_t dgramSize = slot->m_readBufSize;
	// return -1 if too small
	if ( dgramSize < 12 ) { 
		log(LOG_INFO,"dns: Nameserver (%s) returned bad "
		    "reply size of %"INT32" bytes which is less than 12 bytes.",
		    iptoa(slot->m_ip),dgramSize);
		g_errno = EBADREPLY; 
		return -1; 
	}
	// get the response code (lower 4 bits of byte #3)
	int32_t rcode = dgram[3] & 0x0f;
	// . return -1 if response code indicates an error
	// . set g_errno as well
	switch ( rcode ) {
	case 0: break; // valid
	case 1: log(LOG_DEBUG,"dns: Nameserver (%s) returned request "
		    "format error.", iptoa(slot->m_ip));
		g_errno = EBADREQUEST;
		return -1;
	case 2: //log("dns::gotIp: dns server failure"              ); 
		//    log("dns: Nameserver (%s) could not handle query. "
		//	"Should you add \"recursive-clients 2000\" in "
		//	"\"options { ... };\" bracket of "
		//	"/etc/bind/named.conf?");
		// we have to try another dns if we get this message!
		log(LOG_DEBUG,"dns: Nameserver (%s) returned SERVFAIL.",
		    iptoa(slot->m_ip));
		//g_errno = ETRYAGAIN;
		//break;
		return -1;
		// name does not exist
	case 3: log(LOG_DEBUG,
			"dns::gotIp: name does not exist, returning ip of 0"
			"for '%s'", ds->m_hostname);
		// cache a not-found ip entry for this hostname
		addToCache ( ds->m_hostnameKey , 0 );
		return 0;
	case 4: log(LOG_DEBUG,"dns: Nameserver (%s) does not support query.",
		    iptoa(slot->m_ip));
		g_errno = EBADREQUEST;
		return -1;
	case 5: log(LOG_DEBUG,"dns: Nameserver (%s) refused request.",
		    iptoa(slot->m_ip));
		// www.fsis.usda.gov will error here if recursion bit is set
		// so restart from the beginning with it turned off
		if ( ds->m_recursionDesired ) {
			log(LOG_DEBUG,"dns: Turning off recursion "
			    "desired bit and retrying.");
			ds->m_recursionDesired = false;
			// retry the same ips of dns'es again
			ds->m_numTried = 0;
			g_errno = ETRYAGAIN;
			return -1;
		}
		g_errno = EDNSREFUSED;
		return -1;
	default: log(LOG_INFO,"dns: Nameserver (%s) returned unknown "
		    "return code = %"INT32".",
		    iptoa(slot->m_ip), rcode ); 
		g_errno = EBADREPLY;
		return -1;
	}
	// does this name server support recursion?
	bool supportRecursion = dgram[3] & 0x80;
	// now if it said name does not exist but it did NOT set the
	// recursion is available flag, that's bad
	if ( ! supportRecursion && rcode == 3 ) {
		g_errno = EDNSBAD;
		log(LOG_INFO,"dns: Nameserver (%s) will not not recurse.",
		    iptoa(slot->m_ip));
		return -1;
	}
	// otherwise if rcode is 3 then the name really does not exist so ret 0
	if ( rcode == 3 ) return 0;
	// how many query & answer records in the record heap?
	qdcount = ntohs (*(int16_t *)(dgram + 4 )) ; 
	// . we better have 1 query record
	// . return -1 on error
	if ( qdcount != 1 ) { 
		g_errno = EBADREPLY; 
		log (LOG_INFO,"dns: Nameserver (%s) returned query count "
		     "of %"INT32" (not 1).", iptoa(slot->m_ip),(int32_t)qdcount);
		return -1; 
	}
	// . now we should have our answer here
	// . if no answer/resource records then that's an error
	// . however, if nscount is positive that is a referral... and
	//   we need to ask those servers. and the additional section
	//   will have the ip addresses of those servers sometimes. that is
	//   why we need to check that section first for ips.
	int16_t ancount = ntohs (*(int16_t *)(dgram + 6 )) ; // answer
	int16_t nscount = ntohs (*(int16_t *)(dgram + 8 )) ; // authority
        int16_t arcount = ntohs (*(int16_t *)(dgram + 10 )); // additional
 	if ( ancount < 0 ) { 
		g_errno = EBADREPLY; 
		log ("dns: Nameserver (%s) returned a negative answer count "
		     "of %"INT32".", iptoa(slot->m_ip),(int32_t)ancount);
		return -1; 
	}

	// if it does not exist and no CNAME in the answer, bail now
	if ( rcode == 2 && ancount == 0 ) {
		addToCache ( ds->m_hostnameKey , 0 );
		g_errno = EDNSDEAD;
		return 0;
	}

	// treat a 0 answer count like an rcode of 3, a non-existent domain
	//if ( ancount == 0 ) {
	//	log ("dns: Nameserver (%s) returned a 0 answer count. "
	//	     "Assuming domain name does not exist.",  
	//	     iptoa ( slot->m_ip ) );
	//	addToCache ( ds->m_hostnameKey , 0 );
	//	return 0;
	//}

	// point to the end of our reply dgram
	// char *end = dgram + dgramSize;

	// . get hostname this is the ip for
	// . dgram+12 points to the hostname in label format
	//char hostname[256];
	//if ( ! extractHostname ( dgram , dgram + 12 , hostname ) ) return -1;

	// . now we should point to to the meat of the resource record
	// . we should have "ancount" resource records
	// . here's the format of one resource record (length is 12 bytes)
	// . u_int16_t rr_type;     /* RR type code (e.g. A, MX, NS, etc.) */
        // . u_int16_t rr_class;    /* RR class code (IN for Internet) */
        // . u_int32_t  rr_ttl;      /* Time-to-live for resource */
        // . u_int16_t rr_rdlength; /* length of RDATA field (in octets) */
        // . u_int16_t rr_rdata;    /* (fieldname used as a ptr) */

	// . here's SOME values for rr_type field
	// . A     1   IP Address (32-bit IP version 4)
	// . NS    2   Name server QNAME (for referrals & recursive queries)
	// . CNAME 5   Canonical name of an alias (in QNAME format)
	// . SOA   6   Start of Zone Transfer (see definition below)
	// . WKS   11  Well-known services (see definition below)
	// . PTR   12  QNAME pointing to other nodes (e.g. in inverse lookups)
	// . HINFO 13  Host Information (CPU string, then OS string)
	// . MX    15  Mail server preference and QNAME (see below)

	// . here's values for the rr_class field
        // . "<invalid>",
        // . IN    1   Internet - used for most queries!
        // . CS    2   CSNET <obsolete>
        // . CH    3   CHAOS Net
        // . HS    4   Hesiod (mit?)

	// . point to heap of resource records (after the query record)
	// . TODO: what is this 2 byte thing?
	char *end = dgram + slot->m_readBufSize ;
	char *rr  = dgram ;
	// . skip rr over first canonical name which actually, may not be
	//   the name we sent!!!
	// . for instance, www.montigny78.fr first name is actually
	//   www.mairie-montigny78.fr ... strange ...
	// . pages.plaza-antique-mall.com has a different name too
	// . first point to start of canonical name
	rr += 12;
	// watch out for corruption
	if ( rr >= end ) {
		addToCache ( ds->m_hostnameKey , -1 );
		g_errno = EBADREPLY;
		log(LOG_INFO,"dns: Nameserver (%s) returned a "
		    "corrupt reply [0] for %s.",
		    iptoa(slot->m_ip),ds->m_hostname);
		return -1;
	}
	// jump over each label
	unsigned char len = *rr;
	while ( len > 0 ) { 
		// point to length byte of next label
		rr += len + 1;
		// break if we exceed the end
		if ( rr >= end ) break;
		// get length of next label
		len = *rr;
	}
	// skip over last byte, it's contents should be 0
	rr++;
	// skip 4 bytes after... for what?
	rr += 4;
	// watch out for corruption
	if ( rr >= end ) {
		addToCache ( ds->m_hostnameKey , -1 );
		g_errno = EBADREPLY;
		log(LOG_INFO,"dns: Nameserver (%s) returned a "
		    "corrupt reply [1] for %s.",
		    iptoa(slot->m_ip),ds->m_hostname);
		return -1;
	}
	// . store the ips of the nameservers we have to ask into "ips"
	// . throw these ips into the next depth level, though
	int32_t *ips = NULL;
	int32_t  numIps = 0;
	// but may be too deep, make sure we can increase the depth by 1
	if ( ds->m_depth + 1 < MAX_DEPTH ) {
		ips         = ds->m_dnsIps  [ds->m_depth+1];
		ds->m_rootTLD     [ ds->m_depth+1 ] = false;
		ds->m_fallbacks   [ ds->m_depth+1 ] = 0;
		ds->m_numDnsIps   [ ds->m_depth+1 ] = 0;
		ds->m_numDnsNames [ ds->m_depth+1 ] = 0;
	}
	
	// ask for MX record? used by Emailer in Facebook.cpp.
	bool getmx = false;
	if ( strncmp(ds->m_hostname,"gbmxrec-",8) == 0 ) 
		getmx = true;

	int16_t mailPreference = -1;
	// scan ALL answer records for ips and select the minimum
	uint32_t minIp = 0;
	int32_t          minttl = 15;
	bool gotIp = false;
	//while ( ancount-- ) {
	int32_t maxi = ancount;
	// seems like mx records themselves are type 15 and are only
	// text, the answer sections has the ips
	if ( getmx ) maxi = ancount + nscount + arcount;
	if ( maxi == 0 ) maxi = nscount + arcount;
	TLDIPEntry	tldip;
	tldip.numTLDIPs = 0;
	int64_t answerHash64 = 0LL;

	for ( int32_t i = 0 ; i < maxi; i++ ) {
		// well, this is the name of the record
		// kinda, so this is how we will map
		// an MX resource record to its A record
		// in the answer section
		char *s = getRRName ( rr , dgram, end );
		//log("dns: got rr name: %s",s);
		int64_t rrHash64 = 0LL;
		if ( s ) rrHash64 = hash64n ( s );
		// . now a domain name should follow
		// . but if the next byte has hi bit set then it means its
		//   a 2 byte pointer to another domain label
		// . if hi bit is clear the label follows here
		// . both bits must be on for message compression
		while ( rr < end ) {
			// parse out the label
			if ( ! *rr ) {
				rr += 1;
				break;
			}
			else if ( (*rr & 0xc0) != 0xc0 )
				rr += (u_char)(*rr)+1;			
			// skip over the compressed thingy
			else {
				rr += 2;
				break;
			}
		}
		// need to have room for the rdata
		if ( rr + 10 >= end ) {
			addToCache ( ds->m_hostnameKey , -1 );
			g_errno = EBADREPLY;
			log(LOG_INFO,"dns: Nameserver (%s) returned a "
			    "corrupt reply [2] for %s.", 
			    iptoa(slot->m_ip),ds->m_hostname);
			return -1;
		}
		// the type (A=1,CNAME=5,...)
		int16_t rrtype = ntohs ( *(int16_t *)rr);
		rr += 2;
		// skip the rr class(2)
		rr += 2;
		// skip the ttl(4)
		int32_t ttl = ntohl(*(int32_t *)rr);
		rr += 4;
		// get the length of the rr data (sometimes ip)
		int16_t rlen = ntohs ( *(int16_t *)rr);
		// skip to the actual resource data
		rr += 2;

		// ask for MX record? used by Emailer in Facebook.cpp.
		if ( getmx && rrtype == 15 && rlen == 4 ) goto extractIp;

		// a match by hash?
		if ( rrtype == 1 && rlen == 4 && answerHash64 &&
		     answerHash64 == rrHash64 )
			goto extractIp;

		// get the ip if we have an A record
		if ( rrtype == 1 && rlen == 4 && ! answerHash64 )
			goto extractIp;

		// watch our for negative rlens
		if ( rlen < 0 ) {
			g_errno = EBADREPLY;
			log(LOG_INFO,"dns: Nameserver (%s) returned "
			    "a negative resource len.", iptoa(slot->m_ip) );
			return -1;
			
		}
		// . TODO: rewrite this messy section of code nice and neat.
		// . do we have a hostname to ask for this ip?
		// . ns records always come after answers records, which we
		//   have none of if this is true, and they always come before
		//   "additional records" (arcount). Usually the additional
		//   records just contain the ip addresses of the ns records.
		//   but when they do not we may have to get the ips of the
		//   hostnames of these dns servers, so store the names into
		//   ds->m_nameBuf and have ds->m_dnsNames point into 
		//   ds->m_nameBuf.
		if ( //! getmx &&
		    // if we are in the NS section we have a referring name
		    // server, most likely without an IP since arcount<nscount
		    // so store that name into the name buffer
		    ((ancount == 0 && i < nscount && arcount < nscount &&
		      rrtype==2) ||
		      // OR, if we are in the answer section and we do not
		      // have an IP yet, we may just have an alias we have
		      // to use as the hostname
		      (!gotIp && i < ancount && (rrtype==5||rrtype==15)))
		     &&
		     ds->m_depth + 1 < MAX_DEPTH &&
		     ds->m_numDnsNames[ds->m_depth+1] < MAX_DNS_IPS &&
		     ds->m_nameBufPtr + rlen + 1 < ds->m_nameBufEnd ) {
			//log("got rr we want");
			char *dst = ds->m_nameBufPtr;
			// store into our buffer
			char *p = rr;
			char *pend = p + rlen;

			// for mx records, i think we got a int16_t mx #
			if ( rrtype == 15 ) {
				mailPreference = ntohs(*(int16_t *)p);
				p += 2;
			}

			while ( *p && p < pend ) {
				// is compression bit set?
				while ( *p & 0xc0 ) {
					// we are jumping within the dgram
					p = dgram +(ntohs(*(int16_t *)p)&0x3fff);
					// bust out of while loop if we should
					if ( !*p || p>=pend || p<dgram ) break;
				}
				// watch out for corruption
				if ( dst + *p + 1 > ds->m_nameBufEnd ) {
					addToCache ( ds->m_hostnameKey , -1);
					g_errno = EBADREPLY;
					log(LOG_INFO,
						"dns: Nameserver (%s) returned"
						" a corrupt reply [3] for %s.",
			    			iptoa(slot->m_ip),
						ds->m_hostname);
					return -1;
				}
				// watch out for corruption
				if ( *p < 0 ) {
					addToCache ( ds->m_hostnameKey , -1);
					g_errno = EBADREPLY;
					log(LOG_INFO,
						"dns: Nameserver (%s) returned"
						" a corrupt reply [4] for %s.",
			    			iptoa(slot->m_ip),
						ds->m_hostname);
					return -1;
				}
				// copy the hostname
				gbmemcpy ( dst , p+1 , *p );
				dst += *p;
				*dst++ = '.';
				p += ((u_char)*p) + 1;
			}
			// last '.' should be a 0 really
			dst[-1] = '\0';
			// make ptr to it
			// if it was a NS name, do that
			if ( rrtype == 2 ) {
				log(LOG_DEBUG,
					"dns: Added name [0-%"INT32"] %s to "
					"depth %"INT32" for %s.",
					ds->m_numDnsNames[ds->m_depth+1],
					ds->m_nameBufPtr,
					(int32_t)ds->m_depth+1,
					ds->m_hostname);
				ds->m_dnsNames[ds->m_depth+1]
					[ds->m_numDnsNames[ds->m_depth+1]++] = 
					ds->m_nameBufPtr;
			}
			// otherwise, a new hostname
			else {
				// note it
				if ( getmx || g_conf.m_logDebugDns )
					logf(LOG_DEBUG,"dns: Got CNAME "
					     "alias %s for %s",
					     ds->m_nameBufPtr, ds->m_hostname);
				// . get hash of it
				// . then if we are scanning the additional
				//   resource records that are A records
				//   and they match this hash, use that ip!
				answerHash64 = hash64n ( ds->m_nameBufPtr );
				// reset the tries, because we can retry the
				// ips of the nameservers again because now
				// we have a different hostname. otherwise,
				// we will fail looking up www.astronomy.org.nz
				ds->m_numTried = 0;
				// hostname changes now
				int32_t len = gbstrlen(ds->m_nameBufPtr);
				if ( len > 127 ) {
					/*
					  this spams the log!
					log(LOG_INFO,
					    "dns: Aliased hostname %s is %"INT32" > "
					    "127 chars long.",ds->m_nameBufPtr,len);
					*/
					g_errno = EBUFTOOSMALL;
					return -1;
				}
				strncpy ( ds->m_hostname , 
					  ds->m_nameBufPtr,
					  len );
				ds->m_hostname[len] = 0;

				// so we have to start over...
				int32_t d = ds->m_depth+1;
				const TLDIPEntry*	cached;
				if ( g_conf.m_askRootNameservers && 
				    (cached = getTLDIP(ds))) {
					gbmemcpy( ds->m_dnsIps[d],
						cached->TLDIP,
						cached->numTLDIPs *
						sizeof(uint32_t));
					ds->m_numDnsIps[d] = cached->numTLDIPs;
					ds->m_numDnsNames[d] = 0;
					ds->m_rootTLD[d] = true;
					ds->m_fallbacks[d] = 0;
					numIps = cached->numTLDIPs;

				} 
				else if ( g_conf.m_askRootNameservers ) {
					gbmemcpy ( ds->m_dnsIps[d],
						 g_conf.m_rnsIps,
						 g_conf.m_numRns * 4);
					ds->m_numDnsIps[d] = g_conf.m_numRns;
					ds->m_numDnsNames[d] = 0;
					ds->m_rootTLD[d] = true;
					ds->m_fallbacks[d] = 0;
					numIps             = g_conf.m_numRns;
				}
				// otherwise, use the local bind9 servers
				else {
					gbmemcpy ( ds->m_dnsIps[d],
						 g_conf.m_dnsIps,
						 g_conf.m_numDns * 4);
					ds->m_numDnsIps[d] = g_conf.m_numDns;
					ds->m_numDnsNames[d] = 0;
					ds->m_rootTLD[d] = false;
					ds->m_fallbacks[d] = 0;
					numIps             = g_conf.m_numDns;
				}
			}
			// update our current name buf pointer
			ds->m_nameBufPtr = (char *)dst;
		}
		// . if record is not type 5 (CNAME) then bitch
		// . we can be in an authority/additional section now
		//   so comment this out
		//if ( rrtype != 5 ) {
		//	g_errno = EBADREPLY;
		//	log("dns: Nameserver (%s) returned "
		//	    "a bad rr type of %"INT32".", iptoa(slot->m_ip),
		//	    (int32_t)rrtype);
		//	return -1;
		//}
	
		//HEY, see rfc1034... gota redo with this as the name i think


		// TODO: use the canonical name as a normalization!!
		// skip resource data
		rr += rlen;
		continue;
	extractIp:
		// . now "s" should pt to the resource data, hopefully the ip
		// . add another ip to our array and inc numIps
		// . ips should be in network order
		uint32_t ip ; gbmemcpy ( (char *)&ip , rr , 4 );

		// verisign's ip is a does not exist
		if ( (int32_t)ip == 0x0b6e5e40) { //atoip ( "64.94.110.11",12)) {
			log( LOG_INFO,
			     "dns: Nameserver (%s) got Verislime's IP. "
			     "Assuming domain name "
			     "does not exist.", iptoa ( slot->m_ip ) );
			addToCache ( ds->m_hostnameKey , 0 );
			return 0;
		}

		// this is no longer needed since ppl use gb to spider 
		// internal intranets now, not just the web. however, be
		// careful we don't spider sensitive gb info as a proxy!!!
		/*
		unsigned char *ipstr = (unsigned char *)&ip;
		if ( (int32_t)ip == 0x0100007f || // aotip("127.0.0.1")
		     (ipstr[0]==192 && ipstr[1]==168) ||
		     (ipstr[0]==172 && ipstr[1]>=16 && ipstr[1]<=31) ||
		     (ipstr[0]==10 ) ) {
			log ( LOG_INFO,
			     "dns: Nameserver (%s) got internal IP. "
			     "Assuming domain name "
			     "does not exist.", iptoa ( slot->m_ip ) );
			addToCache ( ds->m_hostnameKey , 0 );
			return 0;
		}
		*/

		// debug msg
		//fprintf(stderr,".... got ip=%s for %s\n",iptoa(ip),hostname);
		// get the smallest ip (or largest??? what is the byte order?)
		if ( ip < minIp || ! gotIp ) {
			minIp  = ip;
			minttl = ttl;
		}
		// we got one now
		gotIp = true;
		// . add to list of ips of nameservers to ask
		// . must not be in the answer section (i>=ancount)
		if ( ips && numIps + 1 < MAX_DNS_IPS ) {
			if (isTimedOut(ip)) {
				log(LOG_DEBUG,
			    	"dns: Not adding [0] ip %s - timed out",
			    	iptoa(ip));
			}
			else {
				ips[numIps] = ip;
				log(LOG_DEBUG,
					"dns: Added ip [0-%"INT32"] %s to depth %"INT32" "
					"for %s.",
					numIps, iptoa(ip),(int32_t)ds->m_depth+1,
					ds->m_hostname);
				numIps++;
			}
			// build TLDIP entry, regardless of timed-out status
			if ( g_conf.m_askRootNameservers && ds->m_depth == 0 &&
			     tldip.numTLDIPs < MAX_DNS_IPS ) {
				tldip.TLDIP[tldip.numTLDIPs++] = ip;
			}
		}
		// skip resource data and continue
		rr += rlen;
	}
	//log(LOG_DEBUG, "dns: gotIp depth %d done", ds->m_depth);
	#ifdef DNS_TLD_CACHE
	if ( g_conf.m_askRootNameservers && ds->m_depth == 0 && g_errno == 0 ) {
		setTLDIP(ds, &tldip);
		ds->m_rootTLD[1] = true;
	}
	#endif

	//if ( ! gotIp && ancount > 0 ) {
	//	log("got answer with no ip!");
	//}
	// debug msg
	//fprintf(stderr,".... using minip=%s for %s\n",iptoa(minIp),hostname);
	// if we did have an answer section, assume minIp is the answer
	if ( ancount > 0 && gotIp ) {
		log(LOG_DEBUG, "dns: got ip %s for '%s'",
			iptoa(minIp), ds->m_hostname);
		// . get the key of the hostname
		addToCache ( ds->m_hostnameKey , minIp , minttl );
		// return the minimum ip
		return minIp;
	}

	//
	// begin the new code. 
	//
	// if we did not have an ip address in our reply we probably got a 
	// list of nameservers we have to ask for the ip address. so our
	// caller should call sendToNextDNS() after we return.
	// 
	
	// bail if already went too deep
	if ( ! ips ) {
		g_errno = EBADREPLY;
		log(LOG_INFO,"dns: Exceeded max recursion depth of %"INT32" for %s",
		    (int32_t)MAX_DEPTH,ds->m_hostname);
		return -1;
	}
	// if we did not get more nameserver ips to ask, this was a dead end
	//if ( numIps <= 0 ) {
	//	//g_errno = EBADREPLY;
	//	log("dns: Got reply with no ip or nameserver ips.");
	//	//return -1;
	//}

	// ok, increase the depth since we got some more nameserver ips to ask
	ds->m_depth++;
	// set up ds2
	ds->m_numDnsIps[ds->m_depth] = numIps;
	log(LOG_DEBUG,
		"dns: depth +%"INT32" rootTLD %d #fb %"INT32" #ip %"INT32" #name %"INT32"",
		ds->m_depth,
		ds->m_rootTLD[ds->m_depth],
		ds->m_fallbacks[ds->m_depth],
		ds->m_numDnsIps[ds->m_depth],
		ds->m_numDnsNames[ds->m_depth]);

	// initialize next depth level
	int32_t d1 = ds->m_depth + 1;
	ds->m_rootTLD[d1] = false;
	ds->m_fallbacks[d1] = 0;
	ds->m_numDnsIps[d1] = 0;
	ds->m_numDnsNames[d1] = 0;

	// set g_errno so gotIp() will call sendToNextDNS(ds)
	log(LOG_DEBUG, "dns: gotIp returning ETRYAGAIN");
	g_errno = ETRYAGAIN;
	return -1;
}

bool Dns::isInCache ( key_t key , int32_t *ip ) {
	// debug msg
	//log("dns::isInCache: checking");
	// . returns 0 if not in cache
	// . returns -1 if a cached not-found
	// . otherwise returns ip of "hostname"
	// . any cache entry over 24hrs is stale
	int32_t   maxAge = DNS_CACHE_MAX_AGE; // 60*60*24; 
	// . look up the ip in the cache
	// . TODO: can you pass a NULL ptr for the dataSize?
	// . TODO: is it ok to leave the caller hanging??
	// . the callback, gotIp(), can be NULL if we're just updating times
	// . TODO: ensure list owns the data
	RdbList list;
	// if not found, return false;
	char *rec;
	int32_t  recSize;
	// return false if not in cache
	if ( ! m_rdbCache.getRecord ( (collnum_t)0 ,
				      key      , 
				      &rec     ,
				      &recSize ,
				      false    ,  // do copy?
				      maxAge   ,
				      true     )) // inc count?
		return false;
	// recSize must be 4 -- sanity check
	if ( recSize != 4 ) 
		return log("dns: Got bad record of size %"INT32" from cache.",
			   recSize);
	// the data ptr itself is the ip
	*ip = *(int32_t *)rec ;
	// return true since we found it
	return true;
}

// ttl is in seconds
void Dns::addToCache ( key_t hostnameKey , int32_t ip , int32_t ttl ) {
	// debug msg
	log(LOG_DEBUG, "dns: addToCache added key %"UINT64" ip %s ttl %"INT32" to cache",
		hostnameKey.n0, iptoa(ip), ttl);
        // set timestamp to be maxTime (24 hours = 8640 sec) - ttl
        int32_t timestamp;
	// watch out for crazy ttls, bigger than 2 days
	if ( ttl > 60*60*24*2 ) ttl = 60*60*24*2;
	// if ttl is less than how long we trust the cached ip for, reduce
	// the timestamp to fool Dns::isInCache()
        if   ( ttl > 0 ) timestamp = getTime() - DNS_CACHE_MAX_AGE + ttl;
        else             timestamp = getTime();

	// . select the cache, we should add it to
	// . generally, local cache is used to save over-the-network lookups
	//   and is smaller than the distributed cache, m_rdbCache
	RdbCache *c ;
// 	if ( getResponsibleHost(hostnameKey) != g_hostdb.m_myHost )
// 		c = &m_rdbCacheLocal;
// 	else    
		c = &m_rdbCache;

 	// just add a record to the cache
	c->addRecord((collnum_t)0,hostnameKey,(char *)&ip,4,
		     timestamp);//rec size
	// reset g_errno in case it had an error (we don't care)
	g_errno = 0;	
}

// . s pts to the hostname in the len/label pair format
// . hostname will be filled with the hostname
bool Dns::extractHostname ( char *dgram    , 
			    char *record   , 
			    char *hostname ) {

	hostname[0] = '\0';
	int32_t    i = 0;
	char *end = dgram + DGRAM_SIZE; 

	while ( *record ) {
		int16_t  len   = (u_char)(*record);
		char  *src   =  record + 1;
		int32_t   times = 0;
		// if 2 hi bits on "len" are set it's a label offset
		while ( (len & 0xc0) && times++ < 5 ) {
			int16_t offset = (len & 0x3f) << 8 | src[0];
			if ( dgram + offset >= end ) {
				g_errno = EBADREPLY; return false; }
			len    = (u_char)(*(dgram + offset));
			src    = dgram + offset + 1;
		}
		// breech check
		if ( times     >= 5   ) { g_errno = EBADREPLY; return false; }
		if ( i   + len >= 255 ) { g_errno = EBADREPLY; return false; }
		if ( src + len >= end ) { g_errno = EBADREPLY; return false; }
		// copy to hostname
		gbmemcpy ( &hostname[i] , record + 1 , len );
		// if we had a ptr then just add 2
		if ( times > 0 ) record += 2;
		else             record += 1 + len;
		// advance the hostname
		i      += len;
		// there should be SOME room left over (enough for . and \0)
		if ( i >= 254 ) { g_errno = EBADREPLY; return false; }
		// add a .
		hostname [ i++ ] = '.';
	}

	// cover up that dot
	if ( i < 0 ) { g_errno = EBADREPLY; return false; }
	hostname [ i - 1 ] = '\0';

	return true;
}

// TODO: hostname key collision is possible, so watch out. 
//       should we also store ptr to the whole hostname in hash table?
bool Dns::isInFile ( key_t key , int32_t *ip ) {
	// flush and reload our /etc/hosts hash table every minute
	static int32_t s_lastTime = 0;
	int32_t now = getTime();
	if ( now - 60 >= s_lastTime ) {
		s_lastTime = now;
		loadFile ();
		// ignore any error from this
		g_errno = 0;
	}
	// bail if no hash table
	if ( m_numSlots <= 0 ) return false;
	// check hash table
	int32_t n = key.n1 % m_numSlots;
	while ( m_ips[n] && m_keys[n] != key )
		if ( ++n >= m_numSlots ) n = 0;
	// return false if not found in our hash table (/etc/hosts file)
	if ( ! m_ips[n] ) return false;
	// otherwise we got it
	*ip = m_ips[n];
	return true;
}


// returns false and sets g_errno on error
bool Dns::loadFile ( ) {
	File f;
	f.set ("/etc/","hosts");
	int32_t fsize = f.getFileSize();
	if ( fsize < 0 ) 
		return log("dns: Getting file size of /etc/hosts : %s.",
			     mstrerror(g_errno) );
	// add 1 so we can NULL terminate
	int32_t bufSize = fsize + 1;
	// make mem
	char *buf = (char *)mmalloc ( bufSize , "Dns" );
	if ( ! buf ) return log("dns: Could not read /etc/hosts : %s.",
				 mstrerror(g_errno));
	// pre-open the file
	f.open ( O_RDONLY );
	// read it all in
	if ( f.read ( buf , fsize , 0 ) < 0 ) {
		mfree ( buf , bufSize , "Dns" );
		f.close();
		return log("dns: Could not read /etc/hosts : %s.",
			    mstrerror(g_errno));
	}
	// NULL terminate it
	buf [ fsize ] = '\0';
	// free hash table
	mfree ( m_ips  , m_numSlots*4            , "Dns");
	mfree ( m_keys , m_numSlots*sizeof(key_t), "Dns");
	m_ips      = NULL;
	m_keys     = NULL;
	m_numSlots = 0;
	// count # of lines in buf as upper bound for number of entries
	char *p     = buf;
	int32_t  count = 0;
	for ( ; *p ; p++ ) if ( *p == '\n' ) count++;
	// alloc the hash table
	m_numSlots = count * 2;
	m_ips  = (int32_t  *) mmalloc ( 4             * m_numSlots , "Dns" );
	m_keys = (key_t *) mmalloc ( sizeof(key_t) * m_numSlots , "Dns" );
	if ( ! m_ips || ! m_keys ) {
		if ( m_ips ) mfree ( m_ips  , m_numSlots*4            , "Dns");
		if ( m_keys) mfree ( m_keys , m_numSlots*sizeof(key_t), "Dns");
		m_numSlots = 0;
		mfree ( buf , bufSize , "Dns" );
		f.close();
		return log("dns: Read /etc/hosts : %s.", mstrerror(g_errno));
	}
	// clear hash table
	memset ( m_ips , 0 , 4 * m_numSlots );
	// declare vars here
	char *e;
	int32_t  ip;
	key_t key;
	int32_t  n;
	// point to first line
	p = buf;
 loop:
	// skip comments
	if ( *p == '#' ) goto skipline;
	// skip spaces
	while ( isspace ( *p ) ) p++;
	// if this is not a digit, continue
	if ( ! isdigit(*p) ) goto skipline;
	// find the end, a space
	e = p;
	while ( *e && ! isspace(*e) ) e++;
	// get ip, will stop at first space
	ip = atoip ( p , e - p );
	// get the hostname
	p = e;
	// skip spaces after ip
	while ( isspace ( *p ) ) p++;
	// this should be the hostname, starting with an alnum
	if ( ! isalnum (*p) ) goto skipline;
	// get the end of the hostname
	e = p;
	while ( isalnum(*e) || *e=='.' || *e=='-' ) e++;
	// get the hash of the hostname
	key  = hash96 ( p , e - p );
	// advance p
	p = e;
	// store in hash table
	n = key.n1 % m_numSlots;
	while ( m_ips[n] && m_keys[n] != key )
		if ( ++n >= m_numSlots ) n = 0;
	// if already in there skip it
	if ( m_ips[n] ) goto skipline;
	// otherwise add it
	m_ips [n] = ip;
	m_keys[n] = key;
 skipline:
	while ( *p && *p != '\n' ) p++;
	while (       *p == '\n' ) p++;
	if    ( *p ) goto loop;
	// now all the hostname/ip pairs are hashed
	mfree ( buf , bufSize , "Dns" );
	f.close();
	return true;
}		

key_t Dns::getKey ( char *hostname , int32_t hostnameLen ) {
	// use the domain name name. so *.blogspot.com does not flood their dns
	return hash96 ( hostname , hostnameLen );
	//int32_t  dlen = 0;
	//char *dom  = getDomFastFromHostname ( hostname , &dlen );
	//if ( ! dom || dlen <=2 ) return hash96 ( hostname , hostnameLen );
	//return hash96 ( dom , dlen );
}

// . MsgC uses this to see which host is responsible for this key
//   which is just a hash96() of the hostname (see getKey() above)
// . returns -1 if not host available to send request to
Host *Dns::getResponsibleHost ( key_t key ) {
	// just keep this on this cluster now
	Hostdb *hostdb = &g_hostdb;
	// get the hostNum that should handle this
	int32_t hostId = key.n1 % hostdb->getNumHosts();
	// return it if it is alive
	Host* h = hostdb->getHost ( hostId );
	if ( h->m_spiderEnabled && ! hostdb->isDead ( hostId ) ) return h;
	// how many are up?
	int32_t numAlive = hostdb->getNumHostsAlive();
	// NULL if none
	if ( numAlive == 0 ) return NULL;
	// try another hostNum
	int32_t hostNum = key.n1 % numAlive;
	// otherwise, chain to him
	int32_t count = 0;
	for ( int32_t i = 0 ; i < hostdb->m_numHosts ; i++ ) {
		// get the ith host
		Host *host = &hostdb->m_hosts[i];
		if ( !host->m_spiderEnabled )  continue;
		// skip him if he is dead
		if ( hostdb->isDead ( host ) ) continue;
		// count it if alive, continue if not our number
		if ( count++ != hostNum ) continue;
		// we got a match, we cannot use hostNum as the hostId now
		// because the host with that hostId might be dead
		return host;
	}
	g_errno = EHOSTDEAD;
	return NULL;
}
