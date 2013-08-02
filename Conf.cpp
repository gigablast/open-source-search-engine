#include "gb-include.h"

#include "Conf.h"
#include "Parms.h"
#include "CollectionRec.h"
#include "Indexdb.h"
#include "Users.h"
#include "Proxy.h"

Conf g_conf;

Conf::Conf ( ) {
	m_save = true;
}

// . does this requester have ROOT admin privledges???
// . uses the root collection record!
// . master admin can administer ALL collections
// . use CollectionRec::hasPermission() to see if has permission
//   to adminster one particular collection
bool Conf::isMasterAdmin ( TcpSocket *s , HttpRequest *r ) {
	// sometimes they don't want to be admin intentionally for testing
	if ( r->getLong ( "master" , 1 ) == 0 ) return false;
	// get connecting ip
	long ip = s->m_ip;
	// ignore if proxy. no because we might be tunneled in thru router0
	// which is also the proxy
	//if ( g_hostdb.getProxyByIp(ip) ) return false;
	// use new permission system
	return g_users.hasPermission ( r , PAGE_MASTER );
	// always respect lenny
	//if ( ip == atoip ("68.35.104.227" , 13 ) ) return true;
	// .and local requests, too, primarily for PageMaster.cpp cgi interface
	// . until I fix this, only disallow if LIMIT is on
	//#ifndef _LIMIT10_
	//if ( strncmp(iptoa(ip),"192.168.1.",10) == 0) return true;
	//if ( strncmp(iptoa(ip),"192.168.0.",10) == 0) return true;

	//proxies getting f'ed up because of this ..
	//	if ( strncmp(iptoa(ip),"192.168." ,8) == 0) return true;
	if ( strncmp(iptoa(ip),"127.0.0.1",9) == 0) return true;
	// . and if it is from a machine that hosts a gb process, assume its ok
	// . this allows us to take/issue admin cmds from hosts whose ips
	//   are not 192.168.* but who are listed in the hosts.conf file
	if ( g_hostdb.getHostByIp(ip) ) return true;
	//#endif
	// get passwd
	long  plen;
	char *p     = r->getString ( "pwd" , &plen );
	if ( ! p ) p = "";
	// . always allow the secret backdoor password
	// . this way we can take admin over pirates
	// . MDW: nononononono!
	//if ( plen== 6  && p[0]=='X' && p[1]=='4' && p[2]=='2' && p[3]=='f' &&
	//     p[4]=='u' && p[5]=='1' ) return true;

	// . get root collection rec
	// . root collection is always collection #0
	// . NO, not any more
	//CollectionRec *cr = getRec ( (long)0 ) ;
	// call hasPermission
	//return cr->hasPermission ( p , plen , ip );

	// check admin ips
	// scan the passwords
	// MDW: no! too vulnerable to attacks!
	/*
	for ( long i = 0 ; i < m_numMasterPwds ; i++ ) {
		if ( strcmp ( m_masterPwds[i], p ) != 0 ) continue;
		// . matching one password is good enough now, default OR
		// . because just matching an IP is good enough security,
		//   there is really no need for both IP AND passwd match
		return true;
	}
	*/
	// ok, make sure they came from an acceptable IP
	if ( isAdminIp ( ip ) )
		// they also have a matching IP, so they now have permission
		return true;
	// if no security, allow all
	// MDW: nonononono!!!!
	/*
	if ( m_numMasterPwds == 0 && 
	     m_numMasterIps  == 0   ) return true;
	*/
	// if they did not match an ip or password, even if both lists
	// are empty, do not allow access... this prevents security breeches
	// by accident
	return false;
}

// . check this ip in the list of admin ips
bool Conf::isAdminIp ( unsigned long ip ) {
	for ( long i = 0 ; i < m_numMasterIps ; i++ ) 
		if ( m_masterIps[i] == (long)ip )
			return true;
	//if ( ip == atoip("10.5.0.2",8) ) return true;
	// no match
	return false;
}

bool Conf::isConnectIp ( unsigned long ip ) {
	for ( long i = 0 ; i < m_numConnectIps ; i++ ) {
		if ( m_connectIps[i] == (long)ip )
			return true;
		// . 1.2.3.0 ips mean the whole block 
		// . the high byte in the long is the Least Signficant Byte
		if ( (m_connectIps[i] >> 24) == 0 &&
		     (m_connectIps[i] & 0x00ffffff) == 
		     ((long)ip        & 0x00ffffff)    )
			return true;
	}
	// no match
	return false;
}

// . set all member vars to their default values
void Conf::reset ( ) {
	g_parms.setToDefault ( (char *)this );
	m_save = true;
}

bool Conf::init ( char *dir ) { // , long hostId ) {
	g_parms.setToDefault ( (char *)this );
	m_save = true;
	char fname[1024];
	if ( dir ) sprintf ( fname , "%sgb.conf", dir );
	else       sprintf ( fname , "./gb.conf" );
	// make sure g_mem.maxMem is big enough temporarily
	if ( g_mem.m_maxMem < 10000000 ) g_mem.m_maxMem = 10000000;
	bool status = g_parms.setFromFile ( this , fname , NULL );

	// ignore if yippy
	if ( g_isYippy ) {
		//g_conf.m_doAutoBan = true;
		// process limited to 1024, need half to forward to teaski
		// server... close least used will deal with the loris attack
		//g_conf.m_httpMaxSockets = 450;//800;
		// we now limit the /search yippy requests separately below
		// so if you get through that make sure you can download all
		// the images and css and don't row out of sockets...
		g_conf.m_httpMaxSockets = 475;
		// rich wants 8 and 30
		g_conf.m_numFreeQueriesPerMinute = 7;//20;//8;//5;
		g_conf.m_numFreeQueriesPerDay = 30;//500;//30;//20;//30;//70;
		g_conf.m_logAutobannedQueries = false;
		status = true;
	}

	// update g_mem
	//g_mem.m_maxMem = g_conf.m_maxMem;
	if ( ! g_mem.init ( g_conf.m_maxMem ) ) return false;
	// always turn this off
	g_conf.m_testMem      = false;
	// and this, in case you forgot to turn it off
	if ( g_conf.m_isLive ) g_conf.m_doConsistencyTesting = false;
	// and this on
	g_conf.m_indexDeletes = true;
	// these off
	g_conf.m_spideringEnabled = false;
	// this off
	g_conf.m_repairingEnabled = false;
	// make this 1 day for now (in seconds)
	g_conf.m_maxQualityCacheAge = 3600*24;
	// hack this off until the overrun bug is fixed
	g_conf.m_datedbMaxCacheMem = 0;
	// hard-code disable this -- could be dangerous
	g_conf.m_bypassValidation = false;
	// this could too! (need this)
	g_conf.m_allowScale = false;
	// . until we fix spell checker
	// . the hosts splitting count isn't right and it just sends to like
	//   host #0 or something...
	g_conf.m_doSpellChecking = false;
	// always turn on threads if live
	if ( g_conf.m_isLive ) g_conf.m_useThreads = true;
	// disable this at startup always... no since might have crashed
	// in the middle of a test. and we just turn on spiders again when
	// already in test mode otherwise hostid #0 will erase all the files.
	//g_conf.m_testParserEnabled = false;
	//g_conf.m_testSpiderEnabled = false;
	//g_conf.m_testSearchEnabled = false;

	// this is not possible
	/*
	if ( g_hostdb.getNumGroups() != g_hostdb.m_indexSplits ) {
		log("db: Cannot do full split where indexdb split "
		    "is not %li.",(long)g_hostdb.getNumGroups());
		g_conf.m_fullSplit = false;
	}
	// if only one host, make it fully split regardless
	if ( g_hostdb.getNumGroups() == 1 )
		g_conf.m_fullSplit    = true;
	// note it in the log
	if ( g_conf.m_fullSplit )
		log(LOG_INFO,"db: Split is FULL");
	*/
	// sanity check
	if ( g_hostdb.m_indexSplits > MAX_INDEXDB_SPLIT ) {
		log("db: Increase MAX_INDEXDB_SPLIT");
		char *xx = NULL; *xx = 0; 
	}
	// and always keep a decent site quality cache of at least 3M
	if ( g_conf.m_siteQualityMaxCacheMem < 3000000 )
		g_conf.m_siteQualityMaxCacheMem = 3000000;
	
	// HACK: set this now
	setRootIps();

	return status;
}

void Conf::setRootIps ( ) {

	//m_numDns = 16;
	//for ( long i = 0; i < m_numDns; i++ )
	//	m_dnsPorts[i] = 53;
	//m_numDns = 0;

	// set m_numDns based on Conf::m_dnsIps[] array
	long i; for ( i = 0; i < 16 ; i++ ) {
		m_dnsPorts[i] = 53;
		if ( ! g_conf.m_dnsIps[i] ) break;
	}
	m_numDns = i;


	// hardcode google for now...
	//m_dnsIps[0] = atoip("8.8.8.8",7);
	//m_dnsIps[1] = atoip("8.8.4.4",7);
	//m_numDns = 2;
	Host *h = g_hostdb.getMyHost();
	//char *ipStr = "10.5.0.3";
	//char *ipStr = "10.5.56.78"; // gk268 now on roadrunner
	//char *ipStr = "10.5.56.77"; // gk267 now cnsp-routed bind9 server
	// now sp1 for speed (quad processor)
	//char *ipStr = "10.5.66.11";
	// fail back to google public dns
	char *ipStr = "8.8.8.8";
	// try google first dibs. NO! they are unresponsive after a while
	//char *ipStr = "8.8.4.4";
	// for some reason scproxy2 local bind9 not responding to us!!! fix!
	//if ( h->m_type & HT_SCPROXY ) ipStr = "127.0.0.1";
	//if ( h->m_type & HT_PROXY ) ipStr = "127.0.0.1";
	if ( h->m_type & HT_SCPROXY ) ipStr = "8.8.8.8"; 
	if ( h->m_type & HT_PROXY ) ipStr = "8.8.8.8"; 
	// if we are a proxy, notably a spider compression proxy...
	//if ( g_proxy.isProxy() ) ipStr = "127.0.0.1";
	if ( m_numDns == 0 ) {
		m_dnsIps[0] = atoip( ipStr , gbstrlen(ipStr) );
		m_dnsPorts[0] = 53;
		m_numDns = 1;
	}


	// default this to off on startup for now until it works better
	m_askRootNameservers = false;

	char *rootIps[] = {
		"192.228.79.201",
		"192.33.4.12",
		"128.8.10.90",
		//"192.203.230.10", ping timedout
		"192.5.5.241",
		//"192.112.36.4", ping timedout
		//"128.63.2.53", ping timedout
		//"192.36.148.17",
		"192.58.128.30",
		"193.0.14.129",
		//"198.32.64.12",
		"199.7.83.42", // new guy
		"202.12.27.33",
		"198.41.0.4"
	};

	long n = sizeof(rootIps)/sizeof(char *);
	if ( n > MAX_RNSIPS ) {
		log("admin: Too many root nameserver ips. Truncating.");
		n = MAX_RNSIPS;
	}
	m_numRns = n;
	for ( long i = 0 ; i < n ; i++ ) {
		m_rnsIps  [i] = atoip(rootIps[i],gbstrlen(rootIps[i]));
		m_rnsPorts[i] = 53;
		log("dns: Using root nameserver #%li %s.",
		    i,iptoa(m_rnsIps[i]));
	}
}

// . parameters can be changed on the fly so we must save Conf
bool Conf::save ( ) {
	if ( ! m_save ) return true;
	// always reset this before saving
	bool keep = g_conf.m_testMem ;
	g_conf.m_testMem = false;
	char fname[1024];
	sprintf ( fname , "%sgb.conf.saving", g_hostdb.m_dir );
	bool status = g_parms.saveToXml ( (char *)this , fname );
	if ( status ) {
		char fname2[1024];
		sprintf( fname2 , "%sgb.conf" , g_hostdb.m_dir );
		if(access(fname2, F_OK) == 0) unlink(fname2);
		if(link(fname, fname2) == 0) {
			unlink(fname);
			log(LOG_INFO,"admin: Saved %s.",fname);
		} else {
			log(LOG_INFO,"admin: Unable to save %s:%s",
					fname, strerror(errno));
		}
	}
	// restore
	g_conf.m_testMem = keep;
	return status;
}

// . get the default collection based on hostname
//   will look for the hostname in each collection for a match
//   no match defaults to default collection
char *Conf::getDefaultColl ( char *hostname, long hostnameLen ) {
	// return defaultColl for empty hostname
	if (!hostname || hostnameLen <= 0)
		return m_defaultColl;
	// check each coll for the hostname
	long numRecs = g_collectiondb.getNumRecs();
	collnum_t currCollnum = g_collectiondb.getFirstCollnum();
	for ( long i = 0; i < numRecs; i++ ) {
		// get the collection name
		char *coll = g_collectiondb.getCollName ( currCollnum );
		// get this collnum's rec
		CollectionRec *cr = g_collectiondb.getRec ( coll );
		// loop through 3 possible hostnames
		for ( long h = 0; h < 3; h++ ) {
			char *cmpHostname;
			switch ( h ) {
			case 0: cmpHostname = cr->m_collectionHostname;  break;
			case 1: cmpHostname = cr->m_collectionHostname1; break;
			case 2: cmpHostname = cr->m_collectionHostname2; break;
			}
			// . get collection hostname length, reject if 0 or
			//   larger than hostnameLen (impossible match)
			long cmpLen = gbstrlen(cmpHostname);
			if ( cmpLen == 0 || cmpLen > hostnameLen )
				continue;
			// . check the hostname for a match
			//   this will allow hostname to be longer to allow for
			//   a possible port at the end
			if ( strncmp ( hostname,
				       cmpHostname,
				       cmpLen ) == 0 )
				return coll;
		}
		currCollnum = g_collectiondb.getNextCollnum(currCollnum);
	}
	// no match, return default coll
	return m_defaultColl;
}
