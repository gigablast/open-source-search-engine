#include "gb-include.h"

#include "Hostdb.h"
#include "HashTableT.h"
#include "UdpServer.h"
#include "Threads.h"
#include "Process.h"
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "sort.h"
#include "Rdb.h" // RDB_INDEXDB,...
//#include "Indexdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Spider.h"
#include "Clusterdb.h"
#include "Datedb.h"
#include "Tfndb.h"
#include "Dns.h"
#include "Revdb.h"

//extern bool g_isYippy;

// a global class extern'd in .h file
Hostdb g_hostdb;
// the supplemental network used to get titleRecs from by gov.gigablast.com
// for harvesting link text from the larger index
Hostdb g_hostdb2;

HashTableT <unsigned long long, unsigned long> g_hostTableUdp;
HashTableT <unsigned long long, unsigned long> g_hostTableTcp;

Host     *g_listHosts [ MAX_HOSTS * 4 ];
uint32_t  g_listIps   [ MAX_HOSTS * 4 ];
uint16_t  g_listPorts [ MAX_HOSTS * 4 ];
long      g_listNumTotal = 0;


void Hostdb::resetPortTables () {
	g_hostTableUdp.reset();
	g_hostTableTcp.reset();
}

static int cmp  ( const void *h1 , const void *h2 ) ;
//static int cmp2 ( const void *h1 , const void *h2 ) ;

//static void *syncStartWrapper_r ( void *state );
//static void  syncDoneWrapper    ( void *state );
//static void  syncWrapper        ( int fd, void *state );

//pid_t g_syncpid     = -1;
//long  g_syncticker  = 0;
//long  g_syncTimeout = -1;

Hostdb::Hostdb ( ) {
	m_hosts = NULL;
	m_numHosts = 0;
	m_ips = NULL;
	m_syncHost = NULL;
	m_initialized = false;
	m_crcValid = false;
	m_crc = 0;
}

Hostdb::~Hostdb () {
	reset();
}

void Hostdb::reset ( ) {

	for ( long i = 0 ; m_hosts && i < m_numHosts ; i++ ) {
		Host *h = &m_hosts[i];
		// if nothing do not try to free it
		if ( ! h->m_lastKnownGoodCrawlInfoReply ) continue;
		mfree ( h->m_lastKnownGoodCrawlInfoReply ,
			h->m_replyAllocSize , 
			"lknown" );
		// do not re-free
		h->m_lastKnownGoodCrawlInfoReply = NULL;
	}

	if ( m_hosts ) 
		mfree ( m_hosts, m_allocSize,"Hostdb" );
	if ( m_ips   ) mfree ( m_ips  , m_numIps * 4, "Hostdb" );
	m_hosts = NULL;
	m_ips               = NULL;
	m_numIps            = 0;
	m_syncHost          = NULL;
}

char *Hostdb::getNetName ( ) {
	if ( this == &g_hostdb ) return "default";
	return m_netName;
}

// . gets filename that contains the hosts from the Conf file
// . return false on errro
// . g_errno may NOT be set
bool Hostdb::init ( char *filename , long hostId , char *netName ,
		    bool proxyHost , char useTmpCluster ) {
	// reset my ip and port
	m_myIp             = 0;
	m_myIpShotgun      = 0;
	m_myPort           = 0;
	//m_myPort2          = 0;
	m_numHosts         = 0;
	m_numHostsPerShard = 0;
	m_loopbackIp       = atoip ( "127.0.0.1" , 9 );
	m_useTmpCluster    = useTmpCluster;
	m_initialized = true;

	// try localhosts.conf first
	if ( strcmp ( filename , "./hosts.conf" ) == 0 )
		filename = "./localhosts.conf";

 retry:

	/*
	// for yippy use host as port
	long yippyPort;
	if ( g_isYippy ) {
		yippyPort = hostId;
		hostId = 0;
	}
	*/

	// save the name of the network... we can have multiple networks now
	// since we need to get title recs from separate networks for getting
	// link text for gov.gigablast.com
	m_netName[0] = '\0';
	if ( netName ) strncpy ( m_netName , netName , 31 );
	// make sure our hostId is in our conf file
	if ( hostId < 0 ) 
		return log(
			   "conf: Negative hostId %li supplied",hostId);
	// set early for calling log()
	m_hostId = hostId;
	// set clock in sync in fctypes.cpp
	//if ( m_hostId == 0 ) g_clockInSync = true;
	// log it
	if (this == &g_hostdb) logf(LOG_INIT,"conf: HostId is %li.",m_hostId);
	// . File::open() open old if it exists, otherwise,
	File f;
	f.set ( filename );
	// . returns -1 on error and sets g_errno
	// . returns false if does not exist, true otherwise
	long status = f.doesExist();
	long numRead;

	// skip config file for yippy
	/*
	if ( g_isYippy ) {
		m_bufSize = sprintf(m_buf,
				    "port-offset: 0\n"
				    "index-splits: 1\n"
				    "working-dir: /home/emily/\n"
				    "proxy 10.36.14.10 \n"
				    );
		goto skip;
	}
	*/

	// return false on error (g_errno should be set)
	if ( status <= -1 ) return false;
	// return false if the conf file does not exist
	if ( status ==  0 ) { 
		// hosts2.conf is not necessary
		if ( this == &g_hostdb2 ) return true;
		g_errno = ENOHOSTSFILE; 
		log(
		    "conf: Filename %s does not exist." ,filename);
		// if doing localhosts.conf now try hosts.conf
		if ( strcmp(filename,"./localhosts.conf") == 0 ) {
			filename = "./hosts.conf";
			goto retry;
		}
		return false; 
	}
	// get file size
	m_bufSize = f.getFileSize();
	// return false if too big
	if ( m_bufSize > (MAX_HOSTS+MAX_SPARES) * 128 ) { 
		g_errno = EBUFTOOSMALL; 
		return log(
			   "conf: %s has filesize "
			   "of %li bytes, which is greater than %li max.",
			   filename,m_bufSize,
			   (long)(MAX_HOSTS+MAX_SPARES)*128);
	}
	// open the file
	if ( ! f.open ( O_RDONLY ) ) return false;
	// read in the file
	numRead = f.read ( m_buf , m_bufSize , 0 /*offset*/ );
	// ensure g_errno is now set if numRead != m_bufSize
	if ( numRead != m_bufSize ) 
		return log(
			   "conf: Error reading "
			   "%s : %s." , filename,mstrerror(g_errno));
	// NULL terminate what we read
	m_buf [ m_bufSize ] = '\0';

	// skip:

	// how many hosts do we have?
	char *p    = m_buf;
	char *pend = m_buf + m_bufSize;
	long  i = 0;
	m_numSpareHosts = 0;
	m_numProxyHosts = 0;
	m_numHosts      = 0;
	for ( ; *p ; p++ ) {
		if ( is_wspace_a (*p) ) continue;
		// skip comments
		if ( *p == '#' ) { while ( *p && *p != '\n' ) p++; continue; }
		// MUST be a number
		if ( ! is_digit ( *p ) ) {
			// skip known directives
			if ( ! strncmp(p,"port-offset:",12) ||
			     ! strncmp(p,"index-splits:",13) ||
			     ! strncmp(p,"num-mirrors:",12) ||
			     ! strncmp(p,"working-dir:",12) )
				p = p;
			// check if this is a spare host
			else if ( //pend - p < 5 && 
			     strncasecmp(p, "spare", 5) == 0 )
				// count as a spare
				m_numSpareHosts++;

			// check if this is a proxy host
			else if ( //pend - p < 5 && 
				  strncasecmp(p, "proxy", 5) == 0 )
				// count as a spare
				m_numProxyHosts++;
			// query compression proxies count as proxies
			else if ( strncasecmp(p, "qcproxy", 7) == 0 )
				m_numProxyHosts++;
			// spider compression proxies count as proxies
			else if ( strncasecmp(p, "scproxy", 7) == 0 )
				m_numProxyHosts++;

			else
				return log("conf: %s is malformed. First "
					   "item of each non-comment line "
					   "must be a NUMERIC hostId, "
					   "SPARE or PROXY. line=%s",filename,
					   p);
		}
		else
			// count it as a host
			m_numHosts++;
		i++;
		// skip line
		while ( *p && *p != '\n' ) p++;
	}

	// set g_errno, log and return false if no hosts found in the file
	if ( i == 0 ) { 
		log(
		    "conf: No host entries found in %s.",filename);
		g_errno = ENOHOSTS; 
		return false; 
	}
	// alloc space for this many Hosts structures
	// save buffer size
	m_allocSize = sizeof(Host) * i;
	m_hosts = (Host *) mcalloc ( m_allocSize ,"Hostdb");
	if ( ! m_hosts ) return log(
				    "conf: Memory allocation failed.");

	//unsigned long maxShard = 0;
	long numGrunts = 0;

	// now fill up m_hosts
	p = m_buf;
	i = 0;
	long line = 1;
	//unsigned long lastShard = 0;
	long proxyNum = 0;

	// assume defaults
	//long portOffset = -99999;
	long indexSplits = 0;
	char *wdir2 = NULL;
	long  wdirlen2 = 0;
	long numMirrors = -1;

	for ( ; *p ; p++ , line++ ) {
		if ( is_wspace_a (*p) ) continue;
		// skip comments
		if ( *p == '#' ) { while ( *p && *p != '\n' ) p++; continue; }

		// does the line say "port-offset: xxxx" ?
		if ( ! strncmp(p,"index-splits:",13) ) {
			p += 13;
			// skip spaces after the colon
			while (  is_wspace_a(*p) ) p++;			
			indexSplits = atol(p);
			while ( *p && *p != '\n' ) p++; 
			continue; 
		}

		if ( ! strncmp(p,"num-mirrors:",12) ) {
			p += 12;
			// skip spaces after the colon
			while (  is_wspace_a(*p) ) p++;			
			numMirrors = atol(p);
			while ( *p && *p != '\n' ) p++; 
			continue; 
		}

		// does the line say "working-dir: xxxx" ?
		if ( ! strncmp(p,"working-dir:",12) ) {
			p += 12;
			// skip spaces after the colon
			while (  is_wspace_a(*p) ) p++;			
			wdir2 = p;
			// skip until not space
			while ( *p && ! is_wspace_a(*p) ) p++;
			// set length
			wdirlen2 = p - wdir2;
			// mark the end
			char *end = p;
			while ( *p && *p != '\n' ) p++; 
			// null term it
			*end = '\0';
			continue; 
		}

		// skip any spaces at start of line
		while (   is_wspace_a(*p) ) p++;

		// get host in order
		Host *h = &m_hosts[i];

		// clear it
		memset ( h , 0 , sizeof(Host) );

		// . see what type of host this is
		// . proxies are not given numbers as yet in the hosts.conf
		//   so number them in the order in which they come
		if ( is_digit(*p) ) {
			h->m_type = HT_GRUNT;
			h->m_hostId = atoi(p);
		}
		else if ( strncasecmp(p,"spare",5)==0 ) {
			h->m_type = HT_SPARE;
			h->m_hostId = -1;
		}
		else if ( strncasecmp(p,"qcproxy",7)==0 ) {
			h->m_type = HT_QCPROXY;
			h->m_hostId = proxyNum++;
		}
		else if ( strncasecmp(p,"scproxy",7)==0 ) {
			h->m_type = HT_SCPROXY;
			h->m_hostId = proxyNum++;
		}
		else if ( strncasecmp(p,"proxy",5)==0 ) {
			h->m_type = HT_PROXY;
			h->m_hostId = proxyNum++;
		}
		// ignore old version "port-offset:"
		else if ( strncasecmp(p,"port-offset:",12)==0 ) {
			while ( *p && *p != '\n' ) p++;
			continue;
		}
		else {
			logf(LOG_INFO,"hosts: hosts.conf bad line: %s",p);
			g_errno = EBADENGINEER;
			return false;
		}
		
		//bool isSpare = false;
		//bool isProxy = false;
		//if ( i >= m_numHosts && i < (m_numHosts + m_numSpareHosts) )
		//	isSpare = true;
		//if (i >= (m_numHosts + m_numSpareHosts) )
		//	isProxy = true;

		char *wdir;
		long  wdirlen;

		// reset this
		h->m_pingMax = -1;
		h->m_retired = false;

		// skip numeric hostid or "proxy" keyword
		while ( ! is_wspace_a(*p) ) p++;

		// read in switch id
		//h->m_switchId = atoi(p);

		// skip spaces after hostid or "proxy" keyword
		//while ( ! is_wspace_a(*p) ) p++;

		// skip spaces after hostid/port/spare keyword
		while ( is_wspace_a(*p) ) p++;

		/*
		// if proxy get proxy id
		if ( ! ( h->m_type & HT_GRUNT ) ) {
			// get the hostid
			h->m_hostId = atol(p);
			// skip that
			for ( ; is_digit(*p) ; p++ );
			// skip spaces after it
			while ( is_wspace_a(*p) ) p++;
		}			
		*/

		long port1 = 6002;
		long port2 = 7002;
		long port3 = 8002;
		long port4 = 9002;

		// support old format "000 gk0" and use default ports above
		//if ( p[0] == 'g' && p[1] == 'k' ) goto skip;
		// sp1 is the proxy
		//if ( p[0] == 's' && p[1] == 'p' ) goto skip;
			

		// now the four ports
		port1 = atol(p);
		// skip digits
		for ( ; is_digit(*p) ; p++ );
		// skip spaces after it
		while ( is_wspace_a(*p) ) p++;

		port2 = atol(p);
		// skip digits
		for ( ; is_digit(*p) ; p++ );
		// skip spaces after it
		while ( is_wspace_a(*p) ) p++;

		port3 = atol(p);
		// skip digits
		for ( ; is_digit(*p) ; p++ );
		// skip spaces after it
		while ( is_wspace_a(*p) ) p++;

		port4 = atol(p);
		// skip digits
		for ( ; is_digit(*p) ; p++ );
		// skip spaces after it
		while ( is_wspace_a(*p) ) p++;

		//skip:

		// set our ports
		h->m_dnsClientPort = port1; // 6000
		h->m_httpsPort     = port2; // 7000
		h->m_httpPort      = port3; // 8000
		h->m_port          = port4; // 9000

		// then hostname
		char *host = p;
		// . sanity check, hostname must start with letter
		// . nah, it can be an ip now!
		//if ( ! is_alpha_a(*p) ) {
		//	log("admin: hostname %s is bad",p);
		//	g_errno = EBADENGINEER;
		//	return false;
		//}
		// skip hostname (can be an ip now)
		while ( *p && (*p=='.'||is_alnum_a(*p)) ) p++;
		// get length
		long hlen = p - host;
		// limit
		if ( hlen > 15 ) {
			g_errno = EBADENGINEER;
			log("admin: hostname too long in hosts.conf");
			return false;
		}
		// copy it
		memcpy ( h->m_hostname , host , hlen );
		// null term it
		h->m_hostname[hlen] = '\0';
		// need this for hashing
		hashinit();
		// if hostname is an ip that's ok i guess
		long ip = atoip ( h->m_hostname );
		// for localhost
		//if ( ! ip && host &&
		//     ! strncasecmp(host,"localhost",9) ) {
		//	ip = atoip("127.0.0.1");
		//}
		// if not an ip, look it up
		if ( ! ip ) {
			// get key
			key_t k = hash96 ( host , hlen );
			// get eth0 ip of hostname in /etc/hosts
			g_dns.isInFile ( k , &ip );
			//g_errno = EBADENGINEER;
			//log("admin: hostname \"%s\" in "
			//    "hosts.conf "
			//  "not in /etc/hosts",h->m_hostname);
			//return false;
			//}
		}
		// still bad?
		if ( ! ip ) {
			g_errno = EBADENGINEER;
			log("admin: no ip for hostname \"%s\" in "
			    "hosts.conf in /etc/hosts",
			    h->m_hostname);
			return false;
		}
		// store the ip
		h->m_ip = ip;
		
		// get possible 2nd hostname
		//p++;
		// skip spaces or until \n
		for ( ; *p == ' ' ; p++ );
		// must be a 2nd hostname
		char *hostname2 = NULL;
		long hlen2 = 0;
		if ( *p != '\n' ) {
			hostname2 = p;
			// find end of it
			for ( ; *p=='.' || 
				      is_digit(*p) || 
				      is_alnum_a(*p) ; p++ );
			hlen2 = p - hostname2;
		}
		long inc = 0;
		long ip2 = 0;
		// was it "retired"?
		if ( hostname2 && strncasecmp(hostname2,"retired",7) == 0 ) {
			h->m_retired = true;
			hostname2 = NULL;
			//goto retired;
		}
		// if no secondary hostname for "gk2" (e.g.) try "gki2"
		char tmp2[32];
		if ( ! hostname2 && host[0]=='g' && host[1]=='k') {
			long hn = atol(host+2);
			sprintf(tmp2,"gki%li",hn);
			hostname2 = tmp2;
		}
		// limit
		if ( hlen2 > 15 ) {
			g_errno = EBADENGINEER;
			log("admin: hostname too long in hosts.conf");
			return false;
		}
		// a direct ip address?
		if ( hostname2 ) {
			memcpy ( h->m_hostname2,hostname2,hlen2);
			h->m_hostname2[hlen2] = '\0';
			ip2 = atoip ( h->m_hostname2 );
		}
		if ( ! ip2 && hostname2 ) {
			// set this ip
			//long nextip;
			// now that must have the eth1 ip in /etc/hosts
			key_t k = hash96 ( h->m_hostname2 , hlen2 );
			// get eth1 ip of hostname in /etc/hosts
			if ( ! g_dns.isInFile ( k , &ip2 ) ) {
				log("admin: secondary host %s in hosts.conf "
				    "not in /etc/hosts. Using secondary "
				    "ethernet (eth1) ip "
				    "of %s",hostname2,iptoa(ip));
				//nextip = ip;
				// just use the old ip then!
				//g_errno = EBADENGINEER;
				//return false;
			}
		}
		//retired:		
		// if none, use initial ip as shotgun as well
		if ( ! ip2 ) ip2 = ip;
		// store the ip, the eth1 ip
		h->m_ipShotgun = ip2; // nextip;
		// . "p" should not point to first char after hostname
		// . a special inc
		inc = 0;
		if ( useTmpCluster ) inc = 1;
		// proxies never get their port inc'd
		if ( h->m_type & (HT_ALL_PROXIES) ) inc = 0;
		// . now p should point to first char after hostname
		// . skip spaces and tabs
		while ( *p && (*p==' '|| *p=='\t') )p++;

		// is "RETIRED" after hostname?
		if ( strncasecmp(p,"retired",7) == 0 )
			h->m_retired = true;
		
		// for qcproxies, the next thing is always an
		// ip:port of another proxy that we forward the
		// queries to.
		if ( h->m_type & HT_QCPROXY ) {
			char *s = p;
			for ( ; *s && *s!=':' ; s++ );
			long ip = 0;
			if ( *s == ':' ) ip = atoip(p,s-p);
			long port = 0;
			if ( *s ) port = atol(s+1);
			// sanity
			if ( ip == 0 || port == 0 ) {
				g_errno = EBADENGINEER;
				log("admin: bad qcproxy line. must "
				    "have ip:port after hostname.");
				return false;
			}
			h->m_forwardIp   = ip;
			h->m_forwardPort = port;
			// skip that to port offset now
			for ( ; *p && *p!=' ' && *p !='\t' ; p++);
			// then skip spaces
			for ( ; *p && (*p==' '|| *p=='\t') ; p++ );
		}
		// yippy hack
		//if ( g_isYippy ) {
		//	h->m_httpPort = yippyPort; // 80;
		//}
		// and other stuff
		//h->m_ideChannel    = 0;
		// our group is based on our split!
		//h->m_group = i % g_hostdb.m_indexSplits; // # grps
		//h->m_group = i % indexSplits; // # grps
		//h->m_shardNum = i % indexSplits;
		// i guess proxy and spares don't count
		if ( h->m_type != HT_GRUNT ) h->m_shardNum = 0;
		
		// are we a compression proxy?
		//h->m_isCompressionProxy = false;
		//bool isLocal = false;
		//uint8_t *p = (uint8_t *)&h->m_ip;
		//if ( p[0] == 10                 ) isLocal = true;
		//if ( p[0] == 192 && p[1] == 168 ) isLocal = true;
		// 127.0.0.1
		//if ( h->m_ip == 16777343 ) isLocal = true;
		// compression proxies are off network so they can
		// compress the spidered docs before sending to our
		// network to save our bandwidth costs since our local
		// fiber connection is expensive
		//if ( isProxy && ! isLocal ) 
		//	h->m_isCompressionProxy = false;
		
		// this is the same
		wdir = wdir2;
		wdirlen = wdirlen2; // gbstrlen ( wdir2 );
		// and skip the old parsing junk
		//goto skip;
		// check for working dir override
		if ( *p == '/' ) {
			wdir = p;
			while ( *p && ! isspace(*p) ) p++;
			wdirlen = p - wdir;
		}
		
		if ( ! wdir ) {
			g_errno = EBADENGINEER;
			log("admin: need working-dir for host "
			    "in hosts.conf line %li",line);
			return false;
		}
		
		//skip:

		// check for something after the working dir
		h->m_note[0] = '\0';
		if ( *p != '\n' ) {
			// save the note
			char *n = p;
			while ( *n && *n != '\n' && n < pend ) n++;
			long noteSize = n - p;
			if ( noteSize > 127 ) noteSize = 127;
			memcpy(h->m_note, p, noteSize);
			*p++ = '\0'; // NULL terminate for atoip
		}
		else
			*p   = '\0';

		/*
		sscanf ( p , 
			 "%li %s %s %hu %hu %hu %hu %hu %li %li %s" ,
			 &h->m_hostId ,
			 ipbuf1 ,
			 ipbuf2 ,
			 &h->m_port  ,
			 &h->m_port2 ,
			 &h->m_dnsClientPort  ,
			 &h->m_httpPort ,
			 &h->m_httpsPort,
			 &h->m_ideChannel ,
			 &h->m_group,
			 wdir );
		*/

		// keep these the same for now
		h->m_externalHttpPort  = h->m_httpPort;
		h->m_externalHttpsPort = h->m_httpsPort;

		// get max group number
		//if ( h->m_shardNum > maxShard && h->m_type==HT_GRUNT )
		//	maxShard = h->m_shardNum;
		if ( h->m_type == HT_GRUNT )
			numGrunts++;

		/*
		if ( h->m_shardNum <= lastShard && h->m_shardNum != 0 
		     && !(h->m_type&(HT_ALL_PROXIES)) ) {
		      g_errno = EBADENGINEER;
		      return log("conf: Host has bad shard # in %s line %li. "
				 "Shard #'s must be strictly increasing, with "
				 "the exception of going from the last "
				 "shard # to the shard # of zero.",
				 filename,line);
		}
		lastShard = h->m_shardNum;
		*/

		// skip line now
		while ( *p && *p != '\n' )
			p++;

		// ensure they're in proper order without gaps
		if ( h->m_type==HT_GRUNT && h->m_hostId != i ) {
		     g_errno = EBADHOSTID; 
		     return log(
				"conf: Unordered hostId of %li, should be %li "
				"in %s line %li.",
				h->m_hostId,i,filename,line);
		}

		// and working dir
		if ( wdirlen > 127 ) {
		      g_errno = EBADENGINEER;
		      return log(
				 "conf: Host working dir too long in "
				 "%s line %li.",filename,line);
		}
		if ( wdirlen <= 0 ) {
		      g_errno = EBADENGINEER;
		      return log(
				 "conf: No working dir supplied in "
				 "%s line %li.",filename,line);
		}
		// make sure it is legit
		if ( wdir[0] != '/' ) {
		      g_errno = EBADENGINEER;
		      return log(
				 "conf: working dir must start "
				 "with / in %s line %li",filename,line);
		}
		// add slash if none there
		if ( wdir[wdirlen-1] !='/' )
			wdir[wdirlen++] = '/';
		//wdir [ wdirlen ] = '\0';

		// copy it over
		//strcpy ( m_hosts[i].m_dir , wdir );
		memcpy(m_hosts[i].m_dir, wdir, wdirlen);
		m_hosts[i].m_dir[wdirlen] = '\0';
		
		// reset this
		m_hosts[i].m_lastPing = 0LL;
		// and don't send emails on him until we got a good ping
		m_hosts[i].m_emailCode = -2;
		// we do not know if it is in sync
		m_hosts[i].m_syncStatus = 2;
		// not doing a sync right now
		m_hosts[i].m_doingSync = 0;
		// so UdpServer.cpp knows if we are in g_hostdb or g_hostdb2
		m_hosts[i].m_hostdb = this;
		// reset these
		m_hosts[i].m_flags    = 0;
		m_hosts[i].m_cpuUsage = 0.0;
		m_hosts[i].m_loadAvg  = 0.0;
		// point to next one
		i++;
	}
	//m_numHosts = i;
	m_numTotalHosts = i;
	// how many shards are we configure for?
	//m_numShards = maxShard + 1; // g_conf.m_numGroups;


	// # of mirrors is zero if no mirrors,
	// if it is 1 then each host has ONE MIRROR host
	if ( numMirrors == 0 )
		indexSplits = numGrunts;
	if ( numMirrors > 0 )
		indexSplits = numGrunts / (numMirrors+1);

	if ( indexSplits == 0 ) {
		g_errno = EBADENGINEER;
		log("admin: need num-mirrors: xxx or "
		    "index-splits: xxx directive "
		    "in hosts.conf");
		return false;
	}

	numMirrors = (numGrunts / indexSplits) - 1 ;

	if ( numMirrors < 0 ) {
		g_errno = EBADENGINEER;
		log("admin: need num-mirrors: xxx or "
		    "index-splits: xxx directive "
		    "in hosts.conf (2)");
		return false;
	}

	m_indexSplits = indexSplits;

	m_numShards = numGrunts / (numMirrors+1);

	//
	// set Host::m_shardNum
	//
	for ( long i = 0 ; i < numGrunts ; i++ ) {
		Host *h = &m_hosts[i];
		h->m_shardNum = i % indexSplits;
	}

	// assign spare hosts
	if ( m_numSpareHosts > MAX_SPARES ) {
		log ( "conf: Number of spares (%li) exceeds max of %i, "
		      "truncating.", m_numSpareHosts, MAX_SPARES );
		m_numSpareHosts = MAX_SPARES;
	}
	for ( i = 0; i < m_numSpareHosts; i++ ) {
		m_spareHosts[i] = &m_hosts[m_numHosts + i];
	}
	
	// assign proxy hosts
	if ( m_numProxyHosts > MAX_PROXIES ) {
		log ( "conf: Number of proxies (%li) exceeds max of %i, "
		      "truncating.", m_numProxyHosts, MAX_PROXIES );
		char *xx=NULL;*xx=0;
		m_numProxyHosts = MAX_PROXIES;
	}
	for ( i = 0; i < m_numProxyHosts; i++ ) {
		m_proxyHosts[i] = &m_hosts[m_numHosts + m_numSpareHosts + i];
		m_proxyHosts[i]->m_isProxy = true;
		// sanity
		if ( m_proxyHosts[i]->m_type == 0  ) { char *xx=NULL;*xx=0; }
	}

	// log discovered hosts
	log ( LOG_INFO, "conf: Discovered %li hosts and %li spares and "
	      "%li proxies.",m_numHosts, m_numSpareHosts, m_numProxyHosts );

	// if we have m_numShards we must have 
	long hostsPerShard  = m_numHosts / m_numShards;
	// must be exact fit
	if ( hostsPerShard * m_numShards != m_numHosts ) {
		g_errno = EBADENGINEER;
		return log("conf: Bad number of hosts for %li shards "
			   "in hosts.conf.",m_numShards);
	}
	// count number of hosts in each shard
	for ( i = 0 ; i < m_numShards ; i++ ) {
		long count = 0;
		for ( long j = 0 ; j < m_numHosts ; j++ )
			if ( m_hosts[j].m_shardNum == (unsigned long)i ) 
				count++;
		if ( count != hostsPerShard ) {
			g_errno = EBADENGINEER;
			return log("conf: Number of hosts in each shard "
				   "in %s is not equal.",filename);
		}
	}


	// assume no ide sharing
	//m_ideSharing = false;
	// . print warning
	// . TODO: speed this up when we get a lot of hosts
	/*
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		long count = 0;
		for ( long j = 0 ; j < m_numHosts ; j++ ) {
			if ( m_hosts[i].m_ip == m_hosts[j].m_ip &&
			     m_hosts[i].m_ideChannel ==m_hosts[j].m_ideChannel)
				count++;
		}
		// do we have any ide sharing going on?
		if ( count >= 2 ) m_ideSharing = true;
	}
	*/

	// . make sure this is a legit # of hosts
	// . numGroups should divide it evenly
	// . TODO: actually should be a power of 2!!
	/*
	//if ( getNumBitsOn ( (unsigned long)m_numHosts ) != 1 ) { 
	//	g_errno = EBADNUMHOSTS; 
	//	return log(
	//		   "conf: Number of hosts in %s is not power "
	//		   "of 2",hostsPerGroup,filename);
	//}
	if ( getNumBitsOn ( (unsigned long)m_numHosts ) != 1 ) { 
		g_errno = EBADNUMHOSTS; 
		return log(
			   "conf: Number of hosts in %s is not power "
			   "of 2",filename);
	}
	*/
	// set the groupId for each host
	//for ( i = 0 ; i < m_numHosts ; i++ ) 
	//	m_hosts[i].m_groupId = g_hostdb.makeGroupId ( i, m_numGroups);
	// set group #
	//for ( i = 0 ; i < m_numHosts ; i++ ) 
	//	m_hosts[i].m_groupNum = i / hostsPerGroup;
	// now sort hosts by shard # then HOST id (both ascending order)
	gbsort ( m_hosts , m_numHosts , sizeof(Host), cmp );
	// ensure hosts in order of groupId then hostId
	//for ( i = 1 ; i < m_numHosts ; i++ ) {
	//	if ( m_hosts[i-1].m_groupId <  m_hosts[i].m_groupId)continue;
	//	if ( m_hosts[i-1].m_groupId == m_hosts[i].m_groupId &&
	//	     m_hosts[i-1].m_hostId  <  m_hosts[i].m_hostId )continue;
	//	return log(
	//		   "conf: Hosts in %s not sorted correctly. "
	//		   "Check order of hostId and groupId.",filename);
	//}
	// . set m_shards array
	// . m_shards[i] is the first host in shardId "i"
	// . any other hosts w/ same shardId immediately follow it
	// . loop through each shard
	long j;
	for ( i = 0 ; i < m_numShards ; i++ ) {
		for ( j = 0 ; j < m_numHosts ; j++ ) 
			if ( m_hosts[j].m_hostId == i ) break;
		// this points to list of all hosts in shard #j since
		// we sorted m_hosts by shardId
		m_shards[i] = &m_hosts[j];
	}
	// . set m_hostPtrs now so Hostdb::getHost() works
	// . the hosts are sorted by shard first so we must be careful
	for ( i = 0 ; i < m_numHosts ; i++ ) {
		long j = m_hosts[i].m_hostId;
		m_hostPtrs[j] = &m_hosts[i];
	}
	// reset this count to 1, 1 counts for ourselves
	if(proxyHost) {
		//m_numHostsAlive = 1;
		m_numProxyAlive = 1;
		//m_numProxyAlive = m_numProxyHosts;
	}
	else {
		m_numHostsAlive = 1;
		//m_numProxyAlive = m_numProxyHosts;
	}
	// sometimes g_conf is not loaded, so fake it
	long deadHostTimeout = g_conf.m_deadHostTimeout;
	// make sure it is bigger than anything
	if ( deadHostTimeout == 0 ) deadHostTimeout = 0x7fffffff;
	// reset ping/stdDev times
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		//m_hosts[i].m_pingAvg    = 100; // 100 ms
		//m_hosts[i].m_pingStdDev =  50; // with a std.dev of 50 ms
		// assume everybody is dead, except us
		m_hosts[i].m_ping        = deadHostTimeout;
		m_hosts[i].m_pingShotgun = deadHostTimeout;
		m_hosts[i].m_loadAvg     = 0.0;
		// no, we want Multicast to obey QueryRouter's first hosts
		// on startup
		//m_hosts[i].m_ping = 0;
		// assume dead until we can ping it
		//m_hosts[i].m_ping = DEAD_HOST_PING + 1;
		// but we're not dead
		//if ((this == &g_hostdb2) || (m_hosts[i].m_hostId == hostId)) {
		//	m_hosts[i].m_ping        = 0;
		//	m_hosts[i].m_pingShotgun = 0;
		//	m_hosts[i].m_loadAvg     = g_process.getLoadAvg();
		//}
		// not in progress
		m_hosts[i].m_inProgress1    = false;
		m_hosts[i].m_inProgress2    = false;
		m_hosts[i].m_numPingReplies = 0;
		m_hosts[i].m_preferEth      = 0;
		// reset m_ping
		//for ( long j = 0 ; j < 4; j++ ) m_hosts[i].m_pings[j] = 100;
	}

	// a debug note
	//log("########## FIX ME ###########");

	// . set the m_machineNum of each host
	// . hostPtrs are sorted by hostId which means should also be sorted
	//   by IP so we can get a good machine number assignment
	if ( m_numHosts > 0 ) m_hostPtrs[0]->m_machineNum = 0;
	long next = 1;
	for ( long i = 1 ; i < m_numHosts ; i++ ) {
		// see if on a machine we already numbered
		// debug comment out
		for ( j = 0 ; j < i ; j++ ) 
			if (m_hostPtrs[i]->m_ip == m_hostPtrs[j]->m_ip) break;
		// if it matches the ip of another host it's on the same machne
		if ( j < i ) {	
			m_hostPtrs[i]->m_machineNum = 
				m_hostPtrs[j]->m_machineNum; 
			continue;
		}
		// otherwise, a new one
		// put this back to the bootom!!!!!!!!!!!!!!!!
		// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		m_hostPtrs[i]->m_machineNum = next++;
		continue;
	}
	// set # of machines
	m_numMachines = next;

	// now set m_myIp, m_myPort, m_myPort2 and m_myMachineNum
	Host *host = getHost ( hostId );
	if ( proxyHost )
		host = getProxy ( hostId );
	if ( ! host ) 
		return log(
			   "conf: Could not find host with hostId %li in "
			   "%s.",hostId,filename);
	m_myIp         = host->m_ip;    // internal IP
	m_myIpShotgun  = host->m_ipShotgun;
	m_myPort       = host->m_port;  // low priority udp port
	//m_myPort2      = host->m_port2; // hi priority udp port
	m_myMachineNum = host->m_machineNum;
	m_myHost       = host;

	// set our ping to zero
	host->m_ping        = 0;
	host->m_pingShotgun = 0;
	host->m_loadAvg     = g_process.getLoadAvg();



	//
	// Token Groups for Merging
	//

	/*
	// set the m_tokenGroupNum member of each Host class we have
	for ( long i = 0 ; i < m_numHosts ; i++ ) 
		m_hosts[i].m_tokenGroupNum = -1;
	for ( long i = 0 ; i < m_numHosts ; i++ ) 
		m_hosts[i].m_tokenGroupNum = getTokenGroupNum ( &m_hosts[i] );
	// do a second pass to resolve indirections
	for ( long i = 0 ; i < m_numHosts ; i++ ) 
		m_hosts[i].m_tokenGroupNum = getTokenGroupNum ( &m_hosts[i] );

	// . order the hostIds by their token group num
	// . if they are on the same host (IP) using the same ide channel OR
	//   they are in the same mirror group, then they are in the same
	//   token group (only one host in group can have the token and the
	//   token is required for merging if "use merge token" is "YES")
	for ( long i = 0 ; i < m_numHosts ; i++ ) m_hostPtrs2[i] = &m_hosts[i];
	gbsort ( m_hostPtrs2 , m_numHosts , sizeof(Host *) , cmp2 );
	// now set m_hostIdToTokenGroupNum for fast lookups by Msg35.cpp
	// for call to Hostdb::getTokenGroup()
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		// find first host in token group #i
		long j = 0;
		for ( j = 0 ; j < m_numHosts ; j++ ) 
			if ( m_hostPtrs2[j]->m_tokenGroupNum == i ) break;
		// if nobody start with host #i, try next one
		if ( j >= m_numHosts ) continue;
		// mark him
		long start = j;
		// map all hosts in group #i to the jth host, the starter
		long gsize = 0;
		for ( j = 0 ; j < m_numHosts ; j++ ) 
			if ( m_hostPtrs2[j]->m_tokenGroupNum == i ) {
				long hid = m_hostPtrs2[j]->m_hostId;
				m_hostIdToTokenGroupNum[hid] = start;
				gsize++;
			}
		// otherwise, set the first ptr to this group
		m_groupSize [ start ] = gsize;
	}
	// debug the token groups out
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		long n;
		Host **g = getTokenGroup ( i , &n );
		char buf [ 1024 ];
		char *p = buf;
		sprintf ( p , "db: Token group of hid #%li is", i );
		p += gbstrlen ( p );
		for ( long j = 0 ; j < n ; j++ ) {
			sprintf ( p , " %li." , g[j]->m_hostId );
			p += gbstrlen ( p );
		}
		log ( LOG_DEBUG , "%s", buf );
	}
	*/

	// THIS hostId
	m_hostId = hostId;
	// set hosts per shard (mirror group)
	m_numHostsPerShard = m_numHosts / m_numShards;

	// groupId and groupmask should be -1 for proxy but they
	// are declared as unsigned :(

	// CALCULATE groupId of THIS host from hostId and numGroups
	//m_groupId = g_hostdb.makeGroupId ( m_hostId , m_numGroups );
	// make groupMask - same for the whole network
	//m_groupMask = g_hostdb.makeGroupMask ( m_numGroups );


	// set m_stripe (aka m_twinNum) for each host
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		// get this host
		Host *h = &m_hosts[i];
		// get his shard, array of hosts
		Host *shard = getShard ( h->m_shardNum );
		// how many hosts in the shard?
		long ng = getNumHostsPerShard();
		// hosts in shard should be sorted by hostid i think, anyway,
		// they *need* to be. see above, hosts are in order in the
		// m_hosts[] array by shard then by hostId, so we should be
		// good to go.
		for ( long j = 0 ; j < ng ; j++ ) {
			if ( &shard[j] != h ) continue;
			h->m_stripe = j;
			break;
		}
	}

	// get THIS host
	Host *h = getHost ( hostId );
	if ( proxyHost )
		h = getProxy ( hostId );
	if ( ! h ) return log(
			      "conf: HostId %li not found in %s.",
			      hostId,filename);
	// set m_dir to THIS host's working dir
	strcpy ( m_dir , h->m_dir );
	// likewise, set m_htmlDir to this host's html dir
	sprintf ( m_httpRootDir , "%shtml/" , m_dir );
	sprintf ( m_logFilename , "%slog%03li", m_dir , hostId );

	long gcount = 0;
	for ( long i = 0 ; i < MAX_KSLOTS && m_numHosts ; i++ ) {
		// set its group id from groupNum, which is "gcount"
		//m_map[i] = getGroupId ( gcount++ );
		//unsigned long gid = getGroupId_old(gcount);
		//long groupNum = getGroupNum (gid);
		// TODO: test this later
		//long oldVal = getGroupId_old ( gcount );
		// now just map to the shard # not the groupId... simpler...
		m_map[i] = gcount % m_numShards;
		// inc it
		gcount++;
		// wrap group Num
		//if ( gcount >= m_numShards ) gcount = 0;
	}

	// set our group
	m_myShard = getShard ( m_myHost->m_shardNum );

	// has the hosts
	return hashHosts();
}


bool Hostdb::hashHosts ( ) {

	// this also holds g_hosts2 as well as g_hosts so we cannot preallocate
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		Host *h = &m_hosts[i];
		// init shotgun bit here, 0 or 1 depending on our hostId
		h->m_shotgunBit = m_hostId & 0x01;
		long ip;
		ip = h->m_ip;
		if ( ! hashHost ( 1,h,ip, h->m_port     )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpPort )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpsPort)) return false;
		// . only hash this if not already in there
		// . just used to see if ip is in the network (local)
		if ( ! hashHost ( 1 , h , ip, 0 )) return false;

		// only hash shotgun ip if different
		if ( h->m_ip == h->m_ipShotgun ) continue;

		ip = h->m_ipShotgun;
		if ( ! hashHost ( 1,h,ip, h->m_port     )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpPort )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpsPort)) return false;
		// . only hash this if not already in there
		// . just used to see if ip is in the network (local)
		if ( ! hashHost ( 1 , h , ip, 0 )) return false;
	}

	// . hash loopback ip to point to us
	// . udpserver needs this?
	// . only do this if they did not already specify a 127.0.0.1 in
	//   the hosts.conf i guess
	long lbip = atoip("127.0.0.1");
	Host *hxx = getHost ( lbip , m_myHost->m_port );
	// only do this if not explicitly assigned to 127.0.0.1 in hosts.conf
	if ( ! hxx && (long)m_myHost->m_ip != lbip ) {
		long loopbackIP = atoip("127.0.0.1",9);
		if ( ! hashHost(1,m_myHost,loopbackIP,m_myHost->m_port)) 
			return false;
	}

	// and the proxies as well
	for ( long i = 0 ; i < m_numProxyHosts ; i++ ) {
		Host *h = getProxy(i);
		// init shotgun bit here, 0 or 1 depending on our hostId
		h->m_shotgunBit = m_hostId & 0x01;
		long ip;
		ip = h->m_ip;
		if ( ! hashHost ( 1,h,ip, h->m_port     )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpPort )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpsPort)) return false;
		// . only hash this if not already in there
		// . just used to see if ip is in the network (local)
		if ( ! hashHost ( 1 , h , ip, 0 )) return false;

		// only hash shotgun ip if different
		if ( h->m_ip == h->m_ipShotgun ) continue;

		ip = h->m_ipShotgun;
		if ( ! hashHost ( 1,h,ip, h->m_port     )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpPort )) return false;
		if ( ! hashHost ( 0,h,ip, h->m_httpsPort)) return false;
		// . only hash this if not already in there
		// . just used to see if ip is in the network (local)
		if ( ! hashHost ( 1 , h , ip, 0 )) return false;
	}

	// verify g_hostTableUdp
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		// get the ith host
		Host *h = &m_hosts[i];
		Host *h2 ;
		h2 = getUdpHost ( h->m_ip , h->m_port );
		if ( h != h2 ) 
			return log("db: Host lookup failed for hostId %li.",
				   h->m_hostId);
		h2 = getUdpHost ( h->m_ipShotgun , h->m_port );
		if ( h != h2 ) 
			return log("db: Host lookup2 failed for hostId %li.",
				   h->m_hostId);
		if ( ! isIpInNetwork ( h->m_ip ) )
			return log("db: Host lookup5 failed for hostId %li.",
				   h->m_hostId);
	}

	// verify g_hostTableTcp
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		// get the ith host
		Host *h = &m_hosts[i];
		Host *h2 ;
		h2 = getTcpHost ( h->m_ip , h->m_httpPort );
		if ( h != h2 ) 
			return log("db: Host lookup3 failed for hostId %li. "
				   "ip=%s port=%hu",
				   h->m_hostId,iptoa(h->m_ip),h->m_httpPort);
		h2 = getTcpHost ( h->m_ip , h->m_httpsPort );
		if ( h != h2 ) 
			return log("db: Host lookup4 failed for hostId %li.",
				   h->m_hostId);
	}

	return true;
}

bool Hostdb::hashHost (	bool udp , Host *h , uint32_t ip , uint16_t port ) {

	// if we are g_hostdb2, do not add if host is in g_hostdb
	// otherwise PingServer.cpp updates hdtemps, etc. on the wrong host
	Host *hh = NULL;
	if ( udp ) hh = getHost ( ip , port );
	if ( hh && this == &g_hostdb2 ) return true;

	// no proxies from hosts2.conf
	if ( this == &g_hostdb2 && h->m_isProxy ) return true;

	// debug
	char *hs = "unknown.conf";
	if ( this == &g_hostdb2 ) hs = "hosts2.conf";
	if ( this == &g_hostdb  ) hs = "hosts.conf";
	
	//logf(LOG_INFO,"db: adding %s ip=%s port=%li isUdp=%li",// (%s)",
	//     h->m_hostname,iptoa(ip),(long)port,(long)udp);//,hs);

	if ( hh && port ) { 
		log("db: Must hash hosts.conf first, then hosts2.conf.");
		log("db: or there is a repeated ip/port in hosts.conf.");
		log("db: repeated host ip=%s port=%li "
		    "name=%s",iptoa(ip),(long)port,h->m_hostname);
		return false;//char *xx=NULL;*xx=0;
	}

	// . keep a list of the udp ips for pinging
	// . do not ping hostdb2 hosts though!
	if ( udp && port != 0 && this == &g_hostdb ) {
		// add the ip port for pinging purposes
		g_listHosts [g_listNumTotal] = h;
		g_listIps   [g_listNumTotal] = ip;
		g_listPorts [g_listNumTotal] = port;
		g_listNumTotal++;
	}

	// shortcut
	HashTableT <unsigned long long, unsigned long> *t;
	if ( udp ) t = &g_hostTableUdp;
	else       t = &g_hostTableTcp;
	// get his key
	unsigned long long key = 0;
	// masking the low bits of the ip is not good because it is
	// the same for every host! so reverse the key to get good hash
	char *dst = (char *)&key;
	char *src = (char *)&ip;
	dst[0] = src[3];
	dst[1] = src[2];
	dst[2] = src[1];
	dst[3] = src[0];
	// port too
	char *src2 = (char *)&port;
	dst[4] = src2[1];
	dst[5] = src2[0];
	// look it up
	long slot = t->getSlot ( key );
	// see if there is a collision
	Host *old = NULL;
	if ( slot >= 0 ) {
		// ports of 0 mean we are just adding an ip, and we can
		// have multiple hosts on the same ip. this call was just
		// to make isIpInNetwork() function work.
		if ( port == 0 ) return true;
		old = (Host *)t->getValueFromSlot(slot);
		return log("db: Got collision between hostId %li and "
			   "%li(proxy=%li). Both have same ip/port. Does "
			   "hosts.conf match hosts2.conf?",
			   old->m_hostId,h->m_hostId,(long)h->m_isProxy);
	}
	// add the new key with a ptr to host using m_port
	return t->addKey ( key , (unsigned long)h ) ;
}

long Hostdb::getHostId ( uint32_t ip , uint16_t port ) {
	Host *h = getUdpHost ( ip , port );
	if ( ! h ) return -1;
	return h->m_hostId;
}

Host *Hostdb::getHostByIp ( uint32_t ip ) {
	return getHostFromTable ( 1 , ip , 0 );	
}

Host *Hostdb::getHost ( uint32_t ip , uint16_t port ) {
	return getHostFromTable ( 1 , ip , port );
}	

// . get Host entry from ip/port
// . port defaults to 0 for no port
Host *Hostdb::getUdpHost ( uint32_t ip , uint16_t port ) {
	return getHostFromTable ( 1 , ip , port );
}	

// . get Host entry from ip/port
// . port defaults to 0 for no port
Host *Hostdb::getTcpHost ( uint32_t ip , uint16_t port ) {
	return getHostFromTable ( 0 , ip , port );
}	

bool Hostdb::isIpInNetwork ( uint32_t ip ) {
	// use port of 0
	if ( getHostByIp ( ip ) ) return true;
	// not found
	return false;
}

// . get Host entry from ip/port
// . this works on proxy hosts as well!
// . use a port of 0 if we should disregard port
Host *Hostdb::getHostFromTable ( bool udp , uint32_t ip , uint16_t port ) {
	// shortcut
	HashTableT <unsigned long long, unsigned long> *t;
	if ( udp ) t = &g_hostTableUdp;
	else       t = &g_hostTableTcp;
	// reset key
	unsigned long long key = 0;
	// masking the low bits of the ip is not good because it is
	// the same for every host! so reverse the key to get good hash
	char *dst = (char *)&key;
	char *src = (char *)&ip;
	dst[0] = src[3];
	dst[1] = src[2];
	dst[2] = src[1];
	dst[3] = src[0];
	// port too
	char *src2 = (char *)&port;
	dst[4] = src2[1];
	dst[5] = src2[0];
	// look it up
	long slot = t->getSlot ( key );
	// return NULL if not found
	if ( slot < 0 ) return NULL;
	return (Host *) t->getValueFromSlot ( slot );
}




/*
Host **Hostdb::getTokenGroup ( unsigned long hostId , long *numHosts ) {
	// map groupId to hostId
	long hid = m_hostIdToTokenGroupNum [ hostId ];
	if ( numHosts ) *numHosts = m_groupSize [ hid ];
	return &m_hostPtrs2 [ hid ];
}

// . use the lowest hostId of the all the hosts that are in our token group
//   as our token group number
// . ha's must be passed in order of the hostId
long Hostdb::getTokenGroupNum ( Host *ha ) {
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		Host *hb = m_hostPtrs [ i ];
		// is he the lowest hostid so far? yes... return the hostid
		if ( hb->m_groupId    == ha->m_groupId   ) goto gotit;
		if ( hb->m_ideChannel != ha->m_ideChannel) continue;
		if ( hb->m_machineNum != ha->m_machineNum) continue;
	gotit:
		// start a new token group if we have to
		long tg1 = ha->m_tokenGroupNum;
		long tg2 = hb->m_tokenGroupNum;
		// if both negative, use hostId to start new token group
		if ( tg1 < 0 && tg2 < 0 ) return hb->m_hostId;
		// if it is us, skip
		if ( ha->m_hostId == hb->m_hostId ) continue;
		// pick the smallest, non-negative
		if ( tg1 < tg2 && tg1 >= 0 ) return tg1;
		else                         return tg2;
	}
	// if only one host, this happens on the second call
	if ( ha->m_tokenGroupNum >= 0 ) return ha->m_tokenGroupNum;
	// force seg fault if none found, that's bad
	char *xx = NULL; *xx = 0;
	return -1;
}

// . this is used by gbsort() above
// . sorts Hosts by their groupId
int cmp2 (const void *v1, const void *v2) {
	Host *h1 = *(Host **)v1;
	Host *h2 = *(Host **)v2;
	// return if groups differ
	return h1->m_tokenGroupNum - h2->m_tokenGroupNum;
}
*/

// . this is used by gbsort() above
// . sorts Hosts by their shard
int cmp (const void *v1, const void *v2) {
	Host *h1 = (Host *)v1;
	Host *h2 = (Host *)v2;
	// return if shards differ
	if ( h1->m_shardNum < h2->m_shardNum ) return -1; 
	if ( h1->m_shardNum > h2->m_shardNum ) return  1;
	// otherwise sort by hostId
	return h1->m_hostId - h2->m_hostId;
}
/*
// . returns the first host in the group "groupId"
// . this host has the lowest hostId of all hosts in that group
// . reverse bit order to get hostId from groupId
// . this hostId will be the lowest numbered hostId in the group
long Hostdb::makeHostId ( unsigned long groupId ) {
	return reverseBits ( groupId );
}


long Hostdb::makeHostIdFast ( unsigned long groupId ) {
	// sanity check
	if ( m_numHosts > 655536 ) { char *xx = NULL; *xx = 0; }
	// init a table that takes the top 2 bytes of the groupId
	// and maps to a table
	static bool           s_init = false;
	static unsigned short s_gidTable [ 64*1024 ];

	// sanity check -- temporary (REMOVE!!)
	if ( s_init ) {
		long tmpHostId = makeHostId ( groupId );
		if ( tmpHostId != s_gidTable [ groupId>>16 ] ) {
			char *xx = NULL; *xx = 0; }
	}

	if ( s_init ) return s_gidTable[ groupId>>16 ];

	memset ( s_gidTable , 0 , 2*64*1024 );
	// init s_gidTable
	for ( long i = 0 ; i < m_numGroups ; i++ ) {
		// get their groupId
		unsigned long groupId = m_groups[i]->m_groupId;
		// make the table entry using the top 2 bytes of the groupId,
		// those are the only bits that matter
		s_gidTable [ groupId>>16 ] = m_groups[i]->m_hostId;
	}		
	// table is initialized
	s_init = true;
	// return the answer
	return s_gidTable[ groupId>>16 ];
}

// . hi bits are set
// . we need to reverse the order of the bits in "hostId" to get the "groupId"
// . if we have 8 hosts but only 4 groups, we mask the hostId with (4 - 1):
//   hostId = 0 , groupId = 1 & 0x03 = 0
//   hostId = 1 , groupId = 1 & 0x03 = 1
//   hostId = 2 , groupId = 2 & 0x03 = 2
//   hostId = 3 , groupId = 3 & 0x03 = 3
//   hostId = 4 , groupId = 4 & 0x03 = 0
//   hostId = 5 , groupId = 5 & 0x03 = 1
//   hostId = 6 , groupId = 6 & 0x03 = 2
//   hostId = 7 , groupId = 7 & 0x03 = 3
unsigned long Hostdb::makeGroupId ( long hostId , long numGroups ) {
	return reverseBits ( hostId & (numGroups - 1) );
}

// hi bits are set
unsigned long Hostdb::makeGroupMask ( long numGroups ) {
	return makeGroupId ( numGroups - 1 , numGroups );
}
*/

// return first alive host in a shard
Host *Hostdb::getLiveHostInShard ( long shardNum ) {
	Host *shard = getShard ( shardNum );
	//Host *live = NULL;
	for ( long i = 0 ; i < m_numHostsPerShard ; i++ ) {
		// get it
		Host *h = &shard[i];
		// skip if dead
		if ( isDead(h->m_hostId) ) continue;
		// return it if alive
		return h;
	}
	// return first one if all dead
	return &shard[0];
}

/*
// . get the Hosts in group with "groupId"
Host *Hostdb::getGroup ( unsigned long groupId , long *numHosts ) {
	// set hosts per group
	if ( numHosts ) *numHosts = m_numHostsPerShard;
	// . translate groupId to a hostId
	// . this hostId should be the lowest hostId in this groupId
	long hostId = makeHostId ( groupId );
	// watch out for bogus groupIds
	if ( hostId >= m_numHosts ) {
		if ( numHosts ) *numHosts = 0;
		log("conf: Received request for "
		    "group of hosts based on bad groupId.");
		return NULL;
	}
	// . get first host that has this groupId (in the m_groups array)
	// . m_groups sorts the Hosts by groupid then hostId
	return m_groups [ hostId ];
}

Host *Hostdb::getFastestHostInGroup ( unsigned long groupId ) {
	Host *hosts = getGroup ( groupId );
	if ( ! hosts ) return NULL;
	// scan for smallest average roundtrip time (i.e. ping time)
	long minPing = 0x7fffffff;
	long mini    = -1;
	for ( long i = 0 ; i < m_numHostsPerShard ; i++ ) {
		//if ( hosts[i].m_pingAvg >= minPing ) continue;
		if ( hosts[i].m_ping >= minPing ) continue;
		//minPing = hosts[i].m_pingAvg;
		minPing = hosts[i].m_ping;
		mini    = i;
	}
	// how can this happen?
	if ( mini == -1 ) return NULL;
	// return the fastest host
	return &hosts[mini];
}
*/

// TODO: speed this up when we get a *lot* of hosts
/*
Host *Hostdb::getSharer ( Host *h ) {
	// return quickly if we know nobody shares an ide channel
	if ( ! m_ideSharing ) return NULL;
	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		// skip self
		if ( m_hosts[i].m_hostId     == h->m_hostId     ) continue;
		if ( m_hosts[i].m_ip         != h->m_ip         ) continue;
		if ( m_hosts[i].m_ideChannel != h->m_ideChannel ) continue;
		return &m_hosts[i];
	}
	return NULL;
}
*/

bool Hostdb::hasDeadHost ( ) {
	for ( long i = 0 ; i < m_numHosts ; i++ )
		if ( isDead ( i ) ) return true;
	return false;
}

bool Hostdb::isDead ( long hostId ) {
	Host *h = getHost ( hostId );
	return isDead ( h );
}

bool Hostdb::isDead ( Host *h ) {
	// retired is basically dead
	if ( h->m_retired ) return true;
	if ( g_hostdb.m_myHost == h ) return false;
	if ( ! g_conf.m_useShotgun )
		return ( h->m_ping >= g_conf.m_deadHostTimeout);
	if ( h->m_ping        < g_conf.m_deadHostTimeout ) return false;
	if ( h->m_pingShotgun < g_conf.m_deadHostTimeout ) return false;
	return true;
}

long Hostdb::getAliveIp ( Host *h ) {
	if ( ! g_conf.m_useShotgun ) 
		return h->m_ip;
	if ( h->m_ping        < g_conf.m_deadHostTimeout ) 
		return h->m_ip;
	if ( h->m_pingShotgun < g_conf.m_deadHostTimeout ) 
		return h->m_ipShotgun;
	return h->m_ip;
}

long long Hostdb::getNumGlobalRecs ( ) {
	long long n = 0;
	for ( long i = 0 ; i < m_numHosts ; i++ )
		n += getHost ( i )->m_docsIndexed;
	return n / m_numHostsPerShard;
}

long long Hostdb::getNumGlobalEvents ( ) {
	long long n = 0;
	for ( long i = 0 ; i < m_numHosts ; i++ )
		n += getHost ( i )->m_eventsIndexed;
	return n / m_numHostsPerShard;
}

bool Hostdb::setNote ( long hostId, char *note, long noteLen ) {
	// replace the note on the host
	if ( noteLen > 125 ) noteLen = 125;
	Host *h = getHost ( hostId );
	if ( !h ) return true;
	//h->m_note[0] = ' ';
	//h->m_note[1] = '#';
	memcpy(h->m_note, note, noteLen);
	h->m_note[noteLen] = '\0';
	// write this hosts conf out
	return saveHostsConf();
}

bool Hostdb::setSpareNote ( long spareId, char *note, long noteLen ) {
	// replace the note on the host
	if ( noteLen > 125 ) noteLen = 125;
	Host *h = getSpare ( spareId );
	if ( !h ) return true;
	//h->m_note[0] = ' ';
	//h->m_note[1] = '#';
	memcpy(h->m_note, note, noteLen);
	h->m_note[noteLen] = '\0';
	// write this hosts conf out
	return saveHostsConf();
}

bool Hostdb::replaceHost ( long origHostId, long spareHostId ) {
	Host *oldHost = getHost(origHostId);
	Host *spareHost = getSpare(spareHostId);
	if ( !oldHost || !spareHost )
		return log ( "init: Bad Host or Spare given. Aborting." );
	// host must be dead
	if ( !isDead(oldHost) )
		return log ( "init: Cannot replace live host. Aborting." );


	Host tmp;
	memcpy ( &tmp , oldHost , sizeof(Host) );
	memcpy ( oldHost , spareHost , sizeof(Host) );
	memcpy ( spareHost , &tmp , sizeof(Host) );

	// however, these values need to change
	oldHost->m_hostId      = origHostId;
	//oldHost->m_groupId   = spareHost->m_groupId;
	oldHost->m_shardNum    = spareHost->m_shardNum;
	//oldHost->m_groupNum  = spareHost->m_groupNum;
	oldHost->m_stripe      = spareHost->m_stripe;
	//oldHost->m_group     = spareHost->m_group;
	oldHost->m_isProxy     = spareHost->m_isProxy;
	oldHost->m_type        = HT_SPARE;
	oldHost->m_hostdb      = spareHost->m_hostdb;
	oldHost->m_inProgress1 = spareHost->m_inProgress1;
	oldHost->m_inProgress2 = spareHost->m_inProgress2;
	oldHost->m_lastPing    = spareHost->m_lastPing; // last ping timestamp

	// and the new spare gets a new hostid too
	spareHost->m_hostId = spareHostId;

	// reset these stats
	oldHost->m_pingMax             = 0;
	oldHost->m_gotPingReply        = false;
	oldHost->m_loadAvg             = 0;
	oldHost->m_percentMemUsed      = 0;
	oldHost->m_firstOOMTime        = 0;
	oldHost->m_cpuUsage            = 0;
	oldHost->m_docsIndexed         = 0;
	oldHost->m_eventsIndexed       = 0;
	oldHost->m_slowDiskReads       = 0;
	oldHost->m_kernelErrors        = 0;
	oldHost->m_kernelErrorReported = false;
	oldHost->m_flags               = 0;
	oldHost->m_dailyMergeCollnum   = 0;
	oldHost->m_ping                = g_conf.m_deadHostTimeout;
	oldHost->m_pingShotgun         = g_conf.m_deadHostTimeout;
	oldHost->m_emailCode           = 0;
	oldHost->m_wasAlive            = false;
	oldHost->m_etryagains          = 0;
	oldHost->m_totalResends        = 0;
	oldHost->m_errorReplies        = 0;
	oldHost->m_dgramsTo            = 0;
	oldHost->m_dgramsFrom          = 0;
	oldHost->m_repairMode          = 0;
	oldHost->m_splitsDone          = 0;
	oldHost->m_splitTimes          = 0;
	oldHost->m_hdtemps[0]          = 0;
	oldHost->m_hdtemps[1]          = 0;
	oldHost->m_hdtemps[2]          = 0;
	oldHost->m_hdtemps[3]          = 0;

	// . just swap ips and ports and directories
	// . first store all the old info so we can put it away
	/*
	unsigned long oldIp  = oldHost->m_ip;
	unsigned long oldIp2 = oldHost->m_ipShotgun;
	unsigned short oldExtHttpPort  = oldHost->m_externalHttpPort;
	unsigned short oldExtHttpsPort = oldHost->m_externalHttpsPort;
	unsigned short oldPort  = oldHost->m_port;
	unsigned short oldPort2 = oldHost->m_port2;
	unsigned short oldHttpPort = oldHost->m_httpPort;
	unsigned short oldHttpsPort = oldHost->m_httpsPort;
	long oldMachineNum = oldHost->m_machineNum;
	long oldIdeChannel = oldHost->m_ideChannel;
	char oldSwitchId  = oldHost->m_switchId;
	unsigned short oldDnsPort = oldHost->m_dnsClientPort;
	char oldDir[128];
	memcpy(oldDir, oldHost->m_dir, 128);
	char oldNote[128];
	memcpy(oldNote, oldHost->m_note, 128);
	// . now copy in the spare's info
	oldHost->m_ip = spareHost->m_ip;
	oldHost->m_ipShotgun = spareHost->m_ipShotgun;
	oldHost->m_externalHttpPort = spareHost->m_externalHttpPort;
	oldHost->m_externalHttpsPort = spareHost->m_externalHttpsPort;
	oldHost->m_port = spareHost->m_port;
	oldHost->m_port2 = spareHost->m_port2;
	oldHost->m_httpPort = spareHost->m_httpPort;
	oldHost->m_httpsPort = spareHost->m_httpsPort;
	oldHost->m_machineNum = spareHost->m_machineNum;
	oldHost->m_ideChannel = spareHost->m_ideChannel;
	oldHost->m_switchId  = spareHost->m_switchId;
	oldHost->m_dnsClientPort = spareHost->m_dnsClientPort;
	memcpy(oldHost->m_dir, spareHost->m_dir, 128);
	memcpy(oldHost->m_note, spareHost->m_note, 128);
	// . now store the old info off
	spareHost->m_ip = oldIp;
	spareHost->m_ipShotgun = oldIp2;
	spareHost->m_externalHttpPort = oldExtHttpPort;
	spareHost->m_externalHttpsPort = oldExtHttpsPort;
	spareHost->m_port = oldPort;
	spareHost->m_port2 = oldPort2;
	spareHost->m_httpPort = oldHttpPort;
	spareHost->m_httpsPort = oldHttpsPort;
	spareHost->m_machineNum = oldMachineNum;
	spareHost->m_ideChannel = oldIdeChannel;
	spareHost->m_switchId  = oldSwitchId;
	spareHost->m_dnsClientPort = oldDnsPort;
	memcpy(spareHost->m_dir, oldDir, 128);
	memcpy(spareHost->m_note, oldNote, 128);
	*/
	// write this hosts conf out
	saveHostsConf();
	//
	// . now we need to replace the ips and ports in the hash tables
	//   just clear the hash tables and rehash
	// 
	g_hostTableUdp.clear();
	g_hostTableTcp.clear();
	// reset pingserver's list too!
	g_listNumTotal = 0;
	// now restock everything
	g_hostdb.hashHosts();
	g_hostdb2.hashHosts();

	//validateIps(&g_conf);
	// replace ips in udp server
	g_udpServer.replaceHost ( spareHost, oldHost );
	//g_udpServer2.replaceHost ( spareHost, oldHost );
	// return success
	// update Host::m_onProperSwitch
	//setOnProperSwitchFlags();
	return true;
}

/*
void Hostdb::setOnProperSwitchFlags ( ) {

	long split = g_hostdb.m_indexSplits;

	for ( long i = 0 ; i < m_numHosts ; i++ ) {
		Host *h = &m_hosts[i];
		// get switch group
		long switchGroup = 0;
		if ( split > 0 ) switchGroup = h->m_group % split;
		// and switch id
		long switchId    = h->m_switchId;
		// reset counts
		long          diffSwitch = 0;
		long          sameSwitch = 0;
		char          inMajority = true;
		unsigned long minIp      = 0;
		// does his switch id match the majority of the hosts
		// with his same switch group? if so, he is on the proper
		// switch, otherwise, he is not
		for ( long j = 0 ; j < m_numHosts ; j++ ) {
			// get the jth host
			Host *h2 = &m_hosts[j];
			// get his switch group
			long switchGroup2 = 0;
			if ( split > 0 ) switchGroup2 = h2->m_group % split;
			// skip if not a match
			if ( switchGroup2 != switchGroup ) continue;
			// get group with lowest ip
			if ( minIp == 0 || h2->m_ip < minIp ) {
				// update minIp
				minIp = h2->m_ip;
				// if it switchId matches "switchId" then
				// "h" is in the majority group
				if ( h2->m_switchId == switchId )
					inMajority = true;
				else
					inMajority = false;
			}
			// if not on same switch do not count this one
			if ( h2->m_switchId != switchId ) diffSwitch++;
			// a good match
			else                              sameSwitch++;
		}
		// if "h" is in majority he is on proper switch
		// but, if no majority, (split 50/50), then the group with
		// the lowest ip is the correct group
		if ( sameSwitch > diffSwitch ) {
			h->m_onProperSwitch = true;
			continue;
		}
		if ( sameSwitch < diffSwitch ) {
			h->m_onProperSwitch = false;
			continue;
		}
		// otherwise, half are on a different switch, so the half
		// that has the lowest ip is the correct half
		h->m_onProperSwitch = inMajority;
	}
}
*/

bool Hostdb::saveHostsConf ( ) {
	// open the hosts.conf file
	char filename[1024];
	sprintf ( filename, "%shosts.conf", m_dir );
	log ( LOG_INFO, "conf: Writing hosts.conf file to: %s",
			filename );
	long fd = open ( filename, O_CREAT|O_WRONLY|O_TRUNC,
			 S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH );
	if ( !fd ) {
		log ( "conf: Failed to open %s for writing.", filename );
		return false;
	}
	char temp[1024];
	// write a header
	//             000xx 000.000.000.000 000.000.000.000 00000 00000
	sprintf(temp, "#ID   IP              LINKIP          UDP1  UDP2  "
		      "DNS   HTTP  HTTPS I N G   DIR\n");
	//             00000 00000 00000 0 0 000 ...

	sprintf(temp,
		"# the new hosts.conf format:\n"
		"\n"
		"# <hostId> <hostname> [portoffset] [# <comment>]\n"
		"# spare    <hostname> [portoffset] [# <comment>]\n"
		"# proxy    <hostname> [portoffset] [# <comment>]\n"
		"\n"
		"# we use /etc/hosts to get the ip of eth0\n"
		"# we insert an 'i' into hostname to get ip of eth1\n"
		"\n"
		"working-dir: %s\n"
		//"port-offset: %li\n"
		"index-splits: %li\n"
		"\n"
		,
		g_hostdb.m_dir,
		//(long)g_hostdb.m_myHost->m_httpPort - 8000,
		g_hostdb.m_indexSplits );
	write(fd, temp, gbstrlen(temp));
	// loop over each host and write the conf line
	for ( long i = 0; i < m_numTotalHosts; i++ ) {
		Host *h;
		if ( i < m_numHosts )
			h = getHost(i);
		else if ( i < m_numHosts + m_numSpareHosts )
			h = getSpare(i - m_numHosts);
		else
			h = getProxy(i - m_numHosts - m_numSpareHosts);
		// generate the host id
		if ( i >= m_numHosts + m_numSpareHosts )
			sprintf(temp, "proxy ");
		else if ( i >= m_numHosts )
			sprintf(temp, "spare ");

		else if ( i < 10 )
			sprintf(temp, "00%li   ", i);
		else if ( i < 100 )
			sprintf(temp, "0%li   ", i);
		else
			sprintf(temp, "%li   ", i);
		write(fd, temp, gbstrlen(temp));

		long spaces;
		//long g;

		// the new format is just the hostname then note
		sprintf(temp,"%s ",h->m_hostname);
		write(fd, temp, gbstrlen(temp));
		goto skip;

		// generate the ip string
		strcpy(temp, iptoa(h->m_ip));
		write(fd, temp, gbstrlen(temp));
		spaces = 16 - gbstrlen(temp);
		for ( long s = 0; s < spaces; s++ ) write(fd, " ", 1);
		// generate the ip2 string
		strcpy(temp, iptoa(h->m_ipShotgun));
		write(fd, temp, gbstrlen(temp));
		spaces = 16 - gbstrlen(temp);
		for ( long s = 0; s < spaces; s++ ) write(fd, " ", 1);
		// udp1 port
		sprintf(temp, "%hu ", h->m_port);
		write(fd, temp, gbstrlen(temp));
		spaces = 6 - gbstrlen(temp);
		for ( long s = 0; s < spaces; s++ ) write(fd, " ", 1);
		// udp2 port
		//sprintf(temp, "%hu ", h->m_port2);
		sprintf(temp, "0 " );
		write(fd, temp, gbstrlen(temp));
		spaces = 6 - gbstrlen(temp);
		for ( long s = 0; s < spaces; s++ ) write(fd, " ", 1);
		// dns port
		sprintf(temp, "%hu ", h->m_dnsClientPort);
		write(fd, temp, gbstrlen(temp));
		spaces = 6 - gbstrlen(temp);
		for ( long s = 0; s < spaces; s++ ) write(fd, " ", 1);
		// http port
		sprintf(temp, "%hu ", h->m_httpPort);
		write(fd, temp, gbstrlen(temp));
		spaces = 6 - gbstrlen(temp);
		for ( long s = 0; s < spaces; s++ ) write(fd, " ", 1);
		// https port
		sprintf(temp, "%hu ", h->m_httpsPort);
		write(fd, temp, gbstrlen(temp));
		spaces = 6 - gbstrlen(temp);
		for ( long s = 0; s < spaces; s++ ) write(fd, " ", 1);
		// IDE channel
		sprintf(temp, "%li ", h->m_ideChannel);
		write(fd, temp, gbstrlen(temp));
		// switch ID
		sprintf(temp, "%li ", (long)h->m_switchId);
		write(fd, temp, gbstrlen(temp));
		// Group ID
		/*
		g = h->m_group;
		if ( g < 10 )
			sprintf(temp, "00%li ", g);
		else if ( g < 100 )
			sprintf(temp, "0%li ", g);
		else
			sprintf(temp, "%li ", g);
		write(fd, temp, gbstrlen(temp));
		*/
		// directory
		write(fd, h->m_dir, gbstrlen(h->m_dir));
	skip:
		// note
		write(fd, h->m_note, gbstrlen(h->m_note));
		// end line
		write(fd, "\n", 1);
	}
	// close	else the file
	close(fd);
	return true;
}

void syncDoneWrapper ( void *state , ThreadEntry *t ) {
	Hostdb *THIS = (Hostdb*)state;
	THIS->syncDone();
}

void *syncStartWrapper_r ( void *state , ThreadEntry *t ) {
	Hostdb *THIS = (Hostdb*)state;
	THIS->syncStart_r(true);
	return NULL;
}

// sync a host with its twin
bool Hostdb::syncHost ( long syncHostId, bool useSecondaryIps ) {

	// can't do two syncs
	if ( m_syncHost )
		return log(LOG_WARN, "conf: Cannot manage two syncs on this "
				     "host. Aborting.");
	// log the start
	log ( LOG_INFO, "init: Syncing host %li with twin.", syncHostId );
	// if no twins, can't do it
	if ( m_numHostsPerShard == 1 )
		return log(LOG_WARN, "conf: Cannot sync host, no twins. "
				     "Aborting.");
        // spiders must be off
        if ( g_conf.m_spideringEnabled )
                return log(LOG_WARN, "conf: Syncing while spiders are on is "
                                     "disallowed. Aborting.");
	// first, the host must be marked as dead
	Host *h = getHost(syncHostId);
	if ( ! h )
		log("conf: Cannot get host with host id #%li",
		    (long)syncHostId);
	if ( !isDead(h) )
		return log(LOG_WARN, "conf: Cannot sync live host. Aborting.");
	// now check it for a clean directory
	long ip1 = h->m_ip;
	if ( useSecondaryIps ) ip1 = h->m_ipShotgun;
	char ip1str[32];
	sprintf ( ip1str, "%hhu.%hhu.%hhu.%hhu",
		  (unsigned char)(ip1 >>  0)&0xff,
		  (unsigned char)(ip1 >>  8)&0xff,
		  (unsigned char)(ip1 >> 16)&0xff,
		  (unsigned char)(ip1 >> 24)&0xff );
	char cmd[1024];
	sprintf ( cmd, "ssh %s \"cd %s; du -b | tail -n 1\" > ./synccheck.txt",
		  ip1str, h->m_dir );
	log ( LOG_INFO, "init: %s", cmd );
	system(cmd);
	long fd = open ( "./synccheck.txt", O_RDONLY );
	if ( fd < 0 )
		return log(LOG_WARN, "conf: Unable to open synccheck.txt. "
				     "Aborting.");
	long len = read ( fd, cmd, 1023 );
	cmd[len] = '\0';
	close(fd);
	// delete the file to make sure we don't reuse it
	system ( "rm ./synccheck.txt" );
	// check the size
	long checkSize = atol(cmd);
	if ( checkSize > 4096 || checkSize <= 0 )
		return log(LOG_WARN, "conf: Detected %li bytes in "
			   "directory to "
			   "sync.  Must be empty.  Aborting.",
			   checkSize);
        // set the sync host
        m_syncHost = h;
        m_syncSecondaryIps = useSecondaryIps;
        h->m_doingSync = 1;
	// start the sync in a thread, complete when it's done
	if ( g_threads.call ( GENERIC_THREAD     ,
			      MAX_NICENESS       ,
			      this               ,
			      syncDoneWrapper    ,
			      syncStartWrapper_r ) ) return true;
	// error
        h->m_doingSync = 0;
	m_syncHost = NULL;
        return log ( LOG_WARN, "conf: Could not spawn thread for call to sync "
		     "host. Aborting." );
}

int my_system_r ( char *cmd , long timeout );
int startUp ( void *cmd );

void Hostdb::syncStart_r ( bool amThread ) {
	// get the twin we'll copy from
	long numHostsInShard;
	//Host *hostGroup = getGroup(m_syncHost->m_groupId, &numHostsInGroup);
	Host *shard = getShard(m_syncHost->m_shardNum, &numHostsInShard);
	if ( numHostsInShard == 1 ) {
		m_syncHost->m_doingSync = 0;
		m_syncHost = NULL;
                log (LOG_WARN, "sync: Could not Sync, Host has no twin.");
		return;
	}
	Host *srcHost = &shard[numHostsInShard - 1];
	if ( srcHost == m_syncHost ) srcHost = &shard[numHostsInShard-2];
	// create the rcp command
	char cmd[1024];
	long ip1 = m_syncHost->m_ip;
	if ( m_syncSecondaryIps ) ip1 = m_syncHost->m_ipShotgun;
	char ip1str[32];
	sprintf ( ip1str, "%hhu.%hhu.%hhu.%hhu",
		  (unsigned char)(ip1 >>  0)&0xff,
		  (unsigned char)(ip1 >>  8)&0xff,
		  (unsigned char)(ip1 >> 16)&0xff,
		  (unsigned char)(ip1 >> 24)&0xff );
	long ip2 = srcHost->m_ip;
	if ( m_syncSecondaryIps ) ip2 = srcHost->m_ipShotgun;
	char ip2str[32];
	sprintf ( ip2str, "%hhu.%hhu.%hhu.%hhu",
		  (unsigned char)(ip2 >>  0)&0xff,
		  (unsigned char)(ip2 >>  8)&0xff,
		  (unsigned char)(ip2 >> 16)&0xff,
		  (unsigned char)(ip2 >> 24)&0xff );
	// now we also remove the old log files and *.cache files because
	// they do not apply to this new host
	// . TODO :
	// need the -f flag for rm in case those files do not exist, it
	// would error out then
	sprintf ( cmd, "ssh %s \"rcp -pr %s:%s* %s ; "
		  "rcp -pr %s:%s.antiword %s ; "
		  "rm -f %slog* %s*.cache %s*~ %stmplog* ; "
		  "rm -f %scoll.*.*/waiting* ;" // waitingtree & waitingtable
		  "rm -f %scoll.*.*/doleiptable.dat* ;"
		  // the new guy is NOT in sync!
		  "echo 0 > %sinsync.dat\"",
		  ip1str,

		  ip2str,
		  srcHost->m_dir,
		  m_syncHost->m_dir ,

		  ip2str,
		  srcHost->m_dir,
		  m_syncHost->m_dir ,

		  m_syncHost->m_dir ,
		  m_syncHost->m_dir ,
		  m_syncHost->m_dir ,
		  m_syncHost->m_dir ,
		  m_syncHost->m_dir ,
		  m_syncHost->m_dir ,
		  m_syncHost->m_dir );

	log ( LOG_INFO, "init: %s", cmd );
	int err = my_system_r ( cmd, 3600*24 );
	if ( err != 0 ) {
		log ( "conf: Call to system(\"%s\") had error %s.", cmd ,
		      strerror(err) );
		m_syncHost->m_doingSync = 0;
		m_syncHost = NULL;
		return;
	}
}

void Hostdb::syncDone ( ) {
	// now make a call to startup the newly synced host
	if ( !m_syncHost ) {
		log ( "conf: SyncHost is invalid. Most likely a problem "
		      "during the sync. Ending synchost." );
		return;
	}
	log ( LOG_INFO, "init: Sync copy done.  Starting host." );
	m_syncHost->m_doingSync = 0;
	char cmd[1024];
	sprintf(cmd, "./gb start %li", m_syncHost->m_hostId);
	log ( LOG_INFO, "init: %s", cmd );
	system(cmd);
	m_syncHost = NULL;
}

// use the ip that is not dead, prefer eth0
long Hostdb::getBestIp ( Host *h , long fromIp ) {
	// if shotgun/eth1 ip is dead, returh eth0 ip
	if ( h->m_pingShotgun >= g_conf.m_deadHostTimeout ) return h->m_ip;
	// if eth0 dead, return shotgun ip
	if ( h->m_ping >= g_conf.m_deadHostTimeout ) return h->m_ipShotgun;
	// default to eth0 if both dead
	return h->m_ip;
	/*	
	unsigned char *a = (unsigned char *)&h->m_ipShotgun;
	unsigned char *b = (unsigned char *)&fromIp;
	// we assume shotgun is the internal ip address, but it may not be
	unsigned char *c = (unsigned char *)&h->m_ip;

	// return the closest match, iff there is a match
	if ( a[3] == b[3] && a[2] == b[2] )
		return h->m_ipShotgun;
	if ( c[3] == b[3] && c[2] == b[2] )
		return h->m_ip;

	// if our primary ip is internal, return the shotgun
	bool isPrimaryInternal = false;
	bool isShotgunInternal = false;
	if ( a[0]==192 && a[1]==168 ) isShotgunInternal = true;
	if ( a[0]==10  && a[1]==1   ) isShotgunInternal = true;
	if ( a[0]==127 && a[1]==0   ) isShotgunInternal = true;
	if ( c[0]==192 && c[1]==168 ) isPrimaryInternal = true;
	if ( c[0]==10  && c[1]==1   ) isPrimaryInternal = true;
	if ( c[0]==127 && c[1]==0   ) isPrimaryInternal = true;
	// if primary is not internal, use that then
	if ( ! isPrimaryInternal ) return h->m_ip;
	// if primary is internal, and so is shotgun, prefer primary
	if ( isShotgunInternal ) return h->m_ip;
	// if primary is internal, and shotgun is not, use shotgun
	return h->m_ipShotgun;
	*/
}

// . "h" is from g_hostdb2, the "external" cluster
// . should we send to its primary or shotgun ip?
// . this returns which ip we should send to
long Hostdb::getBestHosts2IP ( Host  *h ) {
	// sanity check
	if ( this != &g_hostdb ) { char *xx = NULL; *xx = 0; }
	// get external ips
	unsigned char *a = (unsigned char *)&h->m_ipShotgun;
	unsigned char *c = (unsigned char *)&h->m_ip;

	char isShotgunInternal = false;
	char isPrimaryInternal = false;
	if ( a[0]==192 && a[1]==168 ) isShotgunInternal = true;
	if ( a[0]==10  && a[1]==1   ) isShotgunInternal = true;
	if ( a[0]==127 && a[1]==0   ) isShotgunInternal = true;
	if ( c[0]==192 && c[1]==168 ) isPrimaryInternal = true;
	if ( c[0]==10  && c[1]==1   ) isPrimaryInternal = true;
	if ( c[0]==127 && c[1]==0   ) isPrimaryInternal = true;

	// get this host
	Host *local = g_hostdb.getMyHost();
	unsigned char *b = (unsigned char *)&local->m_ipShotgun;
	unsigned char *d = (unsigned char *)&local->m_ip;

	char onSameNetwork = false;

	// if ip "a" in hosts2.conf is NOT INTERNAL (192.168.*) then see
	// if it matches any ip (top 2 bytes) in hosts.conf
	if ( ! isShotgunInternal ) {
		// it is PROBABLY on the same net if the top two bytes match!
		if ( a[0] == b[0] && a[1] == b[1] ) onSameNetwork = true;
		if ( a[0] == d[0] && a[1] == d[1] ) onSameNetwork = true;
	}

	// likewise, see if the shotgun ip in hosts2.conf matches the top two
	// bytes of either of our IPs
	if ( ! isPrimaryInternal ) {
		// it is PROBABLY on the same net if the top two bytes match!
		if ( c[0] == b[0] && c[1] == b[1] ) onSameNetwork = true;
		if ( c[0] == d[0] && c[1] == d[1] ) onSameNetwork = true;
	}

	// use internal ip if available and on same network
	if ( onSameNetwork && isPrimaryInternal ) return h->m_ip;        // c

	if ( onSameNetwork && isShotgunInternal ) return h->m_ipShotgun; // a

	// otherwise, if none are internal, just make it primary
	if ( onSameNetwork ) return h->m_ip;

	// ok, not on the same network, use external
	if ( ! isPrimaryInternal ) return h->m_ip;

	if ( ! isShotgunInternal ) return h->m_ipShotgun;

	// otherwise, make a guess, both are internal!!
	static time_t s_last = 0;
	// log it every 10 seconds
	time_t t = getTime();
	if ( t - s_last > 10 ) {
		log("db: All hosts2.conf IPs are internal! Please fix!");
		s_last = t;
	}

	// just try the primary then
	return h->m_ip;
}

// assume to be from posdb here
uint32_t Hostdb::getShardNumByTermId ( void *k ) {
	return m_map [(*(uint16_t *)((char *)k + 16))>>3];
}

// . if false, we don't split index and date lists, other dbs are unaffected
// . this obsolets the g_*.getGroupId() functions
// . this allows us to have any # of groups in a stripe, not just power of 2
// . now we can use 3 stripes of 96 hosts each so spiders will almost never
//   go down
//uint32_t Hostdb::getGroupId ( char rdbId,void *k,bool split ) {
uint32_t Hostdb::getShardNum ( char rdbId,void *k ) { // ,bool split ) {

	if ( (rdbId == RDB_POSDB || rdbId == RDB2_POSDB2) &&
	     // split by termid and not docid?
	     g_posdb.isShardedByTermId ( k ) ) {
		// based on termid NOT docid!!!!!!
		// good for page checksums so we only have to do disk
		// seek on one shard, not all shards.
		// use top 13 bits of key.
		return m_map [(*(uint16_t *)((char *)k + 16))>>3];
	}

	// try to put those most popular ones first for speed
	if      ( rdbId == RDB_POSDB || rdbId == RDB2_POSDB2 ) {
		unsigned long long d = g_posdb.getDocId ( k );
		return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	}
	//if      ( rdbId == RDB_INDEXDB || rdbId == RDB2_INDEXDB2 ) {
	//	unsigned long long d = g_indexdb.getDocId ( (key_t *)k );
	//	return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	//}
	else if ( rdbId == RDB_DATEDB || rdbId == RDB2_DATEDB2 ) {
		unsigned long long d = g_datedb.getDocId ( k );
		return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	}
	else if ( rdbId == RDB_LINKDB || rdbId == RDB2_LINKDB2 ) {
		return m_map [(*(uint16_t *)((char *)k + 26))>>3];	
	}
	else if ( rdbId == RDB_TFNDB || rdbId == RDB2_TFNDB2 ) {
		unsigned long long d = g_tfndb.getDocId ( (key_t *)k );
		return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	}
	else if ( rdbId == RDB_TITLEDB || rdbId == RDB2_TITLEDB2 ) {
		unsigned long long d = g_titledb.getDocId ( (key_t *)k );
		return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	}
	else if ( rdbId == RDB_SPIDERDB || rdbId == RDB2_SPIDERDB2 ) {
		//long long d = g_spiderdb.getDocId ( (key_t *)k );
		//return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
		// HACK:!!! only the responsible group can nuke a spiderdb key
		//if ( ( ((char *)k)[0] & 0x01 ) == 0x00 )
		//	return g_hostdb.m_myHost->m_groupId;
		// . otherwise, check the ip!
		// . this must be a full rec... cast it
		//SpiderRequest *sreq = (SpiderRequest *)k;
		long firstIp = g_spiderdb.getFirstIp((key128_t *)k);
		// do what Spider.h getGroupId() used to do so we are
		// backwards compatible
		unsigned long h = (unsigned long)hash32h(firstIp,0x123456);
		// use that for getting the group
		//return g_spiderdb.getGroupId( firstIp );
		return m_map [ h & (MAX_KSLOTS-1)];
	}
	else if ( rdbId == RDB_CLUSTERDB || rdbId == RDB2_CLUSTERDB2 ) {
		unsigned long long d = g_clusterdb.getDocId ( k );
		return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	}
	else if ( rdbId == RDB_TAGDB || 
		  rdbId == RDB2_TAGDB2 ) {
		return m_map [(*(uint16_t *)((char *)k + 10))>>3];
	}
	// based on url hash, top 32 bits
	else if ( rdbId == RDB_CACHEDB || rdbId == RDB_SERPDB ) {
		return m_map [(*(uint16_t *)((char *)k + 10))>>3];
	}		
	// . base this on city/adm1/city/streetname hash bits, 
	// . see Events::makePlacedbKey()
	// . we got 8+16+25+16 bits = 65 bits
	// . least significant bits are first
	else if ( rdbId == RDB_PLACEDB || rdbId == RDB2_PLACEDB2 ) {
		// hash those guys hashes into a single hash
		unsigned long h = hash32 ( ((char *)k) + 8 , 8 );
		// use that to lookup in m_map then
		return m_map [ h & (MAX_KSLOTS-1) ];
	}
	else if ( rdbId == RDB_DOLEDB ) { // || rdbId == RDB2_DOLEDB2 ) {
		// HACK:!!!!!!  this is a trick!!! it is us!!!
		//return g_hostdb.m_myHost->m_groupId;
		return g_hostdb.m_myHost->m_shardNum;
	}
	else if ( rdbId == RDB_SECTIONDB || rdbId == RDB2_SECTIONDB2 ) {
		// use top 13 bits of key
		return m_map [(*(uint16_t *)((char *)k + 14))>>3];
		//unsigned long long d = g_datedb.getDocId ( k );
		//return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	}
	else if ( rdbId == RDB_REVDB || rdbId == RDB2_REVDB2 ) {
		// key is formed like title key is
		//long long d = g_titledb.getDocId ( (key_t *)k );
		unsigned long long d = g_revdb.getDocId( (key_t *)k );
		return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
	}
	//else if ( rdbId == RDB_FAKEDB ) {
	//	// HACK:!!!!!!  this is a trick!!! it is us!!!
	//	return g_hostdb.m_myHost->m_groupId;
	//}

	//else if ( rdbId == RDB_CATDB || rdbId == RDB2_CATDB2 ) {
	//	return m_map [(*(uint16_t *)((char *)k + 10))>>3];
	//}
	else if ( rdbId == RDB_CATDB || rdbId == RDB2_CATDB2 ) {
		return m_map [(*(uint16_t *)((char *)k + 10))>>3];
	}
	// core -- must be provided
	char *xx = NULL; *xx = 0;
	//groupId=key.n1 & g_hostdb.m_groupMask;
	//return (((key_t *)key)->n1) & g_hostdb.m_groupMask;
	return 0;
}

uint32_t Hostdb::getShardNumFromDocId ( long long d ) {
	return m_map [ ((d>>14)^(d>>7)) & (MAX_KSLOTS-1) ];
}

Host *Hostdb::getBestSpiderCompressionProxy ( long *key ) {

	static long s_numTotal = 0;
	static long s_numAlive = 0;
	static Host *s_alive[64];
	static Host *s_lastResort = NULL;
	static bool s_aliveValid = false;

	if ( ! s_aliveValid ) {
		// come up to "redo" from below if a host goes dead
	redo:
		s_aliveValid = true;
		for ( long i = 0 ; i < m_numProxyHosts ; i++ ) {
			Host *h = getProxy(i);
			if ( ! (h->m_type & HT_SCPROXY ) ) continue;
			// if all dead use this
			s_lastResort = h;
			// count towards total even if not alive
			s_numTotal++;
			// now must be alive
			if ( g_hostdb.isDead (h) ) continue;
			// stop to avoid breach
			if ( s_numAlive >= 64 ) { char *xx=NULL;*xx=0; }
			// add it otherwise
			s_alive[s_numAlive++] = h;
		}
	}

	// if no scproxy in hosts.conf return NULL
	if ( s_numTotal == 0 ) return NULL;

	// if none alive, use last resort, a non-null dead host
	if ( s_numAlive == 0 ) return s_lastResort;

	// pick one based on the key
	long ni = hash32((char *)key , 4 ) % s_numAlive;
	// get it
	Host *h = s_alive[ni];
	// if dead, recompute alive[] table and try again!
	if ( g_hostdb.isDead(h) ) goto redo;
	// got a live one
	return h;
}

long Hostdb::getCRC ( ) {
	if ( m_crcValid ) return m_crc;
	// hash up all host entries, just the grunts really.
	SafeBuf str;
	for ( long i = 0 ; i < getNumGrunts() ; i++ ) {
		Host *h = &m_hosts[i];
		// dns client port not so important
		str.safePrintf("%li,", i);
		str.safePrintf("%s," , iptoa(h->m_ip));
		str.safePrintf("%s," , iptoa(h->m_ipShotgun));
		str.safePrintf("%li,", (long)h->m_httpPort);
		str.safePrintf("%li,", (long)h->m_httpsPort);
		str.safePrintf("%li,", (long)h->m_port);
		str.pushChar('\n');
	}
	str.nullTerm();

	m_crc = hash32n ( str.getBufStart() );

	// make sure it is legit
	if ( m_crc == 0 ) m_crc = 1;

	m_crcValid = true;
	return m_crc;
}


	
