#include "gb-include.h"

#include "TcpSocket.h"
#include "HttpServer.h"
#include "Pages.h"

//
// BASIC DETAILS
//
// . host #0 is solely responsible for testing the proxies and keeping
//   the results of the tests, using the user-defined test url which
//   each proxy downloads every 60 seconds or so.
// . host #0 also saves these stats to the spiderproxies.dat file
//   in the working dir (just an array of SpiderProxy instances)
// . any host needing a proxy server to use should ask host #0 for one
//   but if host #0 is dead then it should ask host #1, etc.
// . host #1 (etc.) will take over if it sees host #0 went dead
// . Conf::m_proxyIps (safebuf) holds space-sep'd list of proxyip:port pairs
// . since host #0 is responsible for giving you a proxy ip it can
//   do proxy load balancing for the whole cluster
// . TODO: to prevent host #0 from getting too slammed we can also recruit
//   other hosts to act just like host #0.

// host #0 breaks Conf::m_spiderIps safebuf into an array of
// SpiderProxy classes and saves to disk as spoderproxies.dat to ensure 
// persistence
class SpiderProxy {
public:
	// ip/port of the spider proxy
	long m_ip;
	short m_port;
	// last time we attempted to download the test url through this proxy
	time_t m_lastDownloadTestAttemptUTC;
	// use -1 to indicate timed out when downloading test url
	long   m_lastDownloadTookMS;
	// use -1 to indicate never
	time_t m_lastSuccessfulTestUTC;

	// how many times have we told a requesting host to use this proxy
	// to download their url with.
	long m_numDownloadRequests;

	// how many are outstanding? everytime a host requests a proxyip
	// it also tells us its outstanding counts for each proxy ip
	// so we can ensure this is accurate even though a host may die
	// and come back up.
	long m_numOutstandingDownloads;
};

// array of SpiderProxy instances
static SafeBuf s_proxyBuf;

// when the Conf::m_proxyIps parm is updated we call this to rebuild
// s_proxyBuf. we try to maintain stats of ip/ports that did NOT change.
bool rebuildProxyBuf ( ) {

	// scan the NEW list of proxy ip/port pairs in g_conf
	SafeBuf *p = g_conf.m_proxyIps.getBufStart();
	
	for ( ; *p ; ) {
		// skip white space
		if ( is_wspace(*p) ) continue;
		// scan in an ip:port
		char *s = p; char *portStr = NULL;
		long dc = 0, pc = 0;
		for ( ; *s && !is_wspace(*s); s++) {
			if ( *s == '.' ) { dc++; continue; }
			if ( *s == ':' ) { portStr=s; pc++; continue; }
			if ( is_digit(*s) ) { gc++; continue; }
			bc++;
			continue;
		}
		char *msg = NULL;
		if ( gc < 4 ) 
			msg = "not enough digits for an ip";
		if ( pc > 1 )
			msg = "too many colons";
		if ( dc != 4 )
			msg = "need 4 dots for an ip address";
		if ( bc )
			msg = "got illegal char in ip:port listing";
		if ( msg ) {
			char c = *s;
			*s = '\0';
			log("buf: %s for %s",msg,p);
			*s = c;
			return false;
		}

		// convert it
		long iplen = s - p;
		if ( portStr ) iplen = port - s;
		long ip = atoip(p,iplen);

		// and the port default is 80
		long port = 80;
		if ( portStr ) port = atoi(port,s-port);

		// . we got a legit ip:port
		// . see if already in our table
		long long ipkey = ip;
		ipKey <<= 16;
		ipKey |= (port & 0xffff);

		// also store into tmptable to see what we need to remove
		tmptab.addSlot(&ipKey);

		// see if in table
		long islot = itab.getSlot( &ipKey);

		// if in there, keep it as is
		continue;

		// otherwise add new entry
		SpiderProxy newThing;
		newThing.m_ip = ip;
		newThing.m_port = port;
		m_lastDownloadTestAttemptUTC = 0;
		m_lastDownloadTookMS = -1;
		m_lastSuccessfulTestUTC = -1;
		m_numDownloadRequests = 0;
		m_numOutstandingDownloads = 0;
		itab.addSlot ( &ipKey, newThing );
	}		

	// scan all SpiderProxies in tmptab
	for ( long i = 0 ; i < tmptab.getNumSlots() ; i++ ) {
		if ( ! tmptab.m_flags[i] ) continue;
		// get the key
		long long key = *(long long *)tmptab.getKey(i);
		// must also exist in itab
		if ( itab.isInTab ( &key ) ) continue;
		// shoot, it got removed. not in the new list of ip:ports
		itab.removeKey ( &key );
	}

	return true;
}

// . we call this from Parms.cpp which prints out the proxy related controls
//   and this table below them...
bool printSpiderProxyTable ( SafeBuf *sb ) {

	// only host #0 will have the stats ... so print that link
	if ( g_hostdb.m_myHostId != 0 ) {
		Host *h = g_hostdb.getHost(0);
		sb->safePrintf("<br>"
			       "<b>See table on <a href=http://%s:%li/"
			       "admin/proxies>"
			       "host #0</a></b>"
			       "<br>"
			       , iptoa(h->m_ip)
			       , (long)(h->m_httpPort)
			       );
		return true;
	}

	// get list of SpiderProxy instances
	SpiderProxy *spp = (SpiderProxy *)g_conf.m_proxyIps.getBufStart();
	if ( ! spp ) return true;
	long nsp = g_conf.m_proxyIps.length() / sizeof(SpiderProxy);


	// print host table
	sb->safePrintf ( 
		       "<table %s>"

		       "<tr><td colspan=10><center>"
		       "<b>Spider Proxies "
		       "</b>"
		       "</center></td></tr>" 

		       "<tr bgcolor=#%s>"
		       "<td>"
		       "<b>proxy IP</b></td>"
		       "<td><b>proxy port</b></td>"
		       // time of last successful download. print "none"
		       // if never successfully used
		       "<td><b>test url last successful download</b></td>"
		       // we fetch a test url every minute or so through
		       // each proxy to ensure it is up. typically this should
		       // be your website so you do not make someone angry.
		       "<td><b>test url last download</b></td>"
		       // print "FAILED" in red if it failed to download
		       "<td><b>test url download time</b></td>"

		       "</tr>"
		       
		       , TABLE_STYLE
		       , DARK_BLUE 
			);

	long now = getTimeLocal();

	// print it
	for ( long i = 0 ; i < nsp ; i++ ) {

		SpiderProxy *sp = &spp[i];

		char *bg = LIGHT_BLUE;
		// mark with light red bg if last test url attempt failed
		if ( sp->m_lastDownloadTookMS == -1 )
			bg = "ffa6a6";

		// print it
		sb->safePrintf (
			       "<tr bgcolor=#%s>"
			       "<td>%s</td>" // proxy ip
			       "<td>%li</td>" // port
			       , bg
			       , iptoa(sp->m_ip)
			       , (long)sp->m_port
			       );

		// last SUCCESSFUL download time ago
		long ago = now - sp->m_lastSuccessfulDownloadTestAttemptUTC;
		sb->safePrintf("<td>");
		// like 1 minute ago etc.
		sb->printTimeElapsed ( ago );
		sb->safePrintf("</td>");

		// last download time ago
		ago = now - sp->m_lastDownloadTestAttemptUTC;
		sb->safePrintf("<td>");
		// like 1 minute ago etc.
		sb->printTimeElapsed ( ago );
		sb->safePrintf("</td>");

		// how long to download the test url?
		if ( sp->m_lastDownloadTookMS != -1 )
			sb->safePrintf("<td>%lims</td>",
				       (long)sp->m_lastDownloadTookMS);
		else
			sb->safePrintf("<td>"
				       "<font color=red>FAILED</font>"
				       "</td>");

		sb->safePrintf("</tr>\n");
	}

	sb->safePrintf("</table>");
	return true;
}

// . Process.cpp should call this from its timeout wrapper
// . updates the stats of each proxy
bool downloadTestUrlFromProxies ( ) {

	// only host #0 should do the testing i guess
	if ( g_hostdb.m_myHostId != 0 ) return true;

	SpiderProxy *spp = (SpiderProxy *)g_conf.m_proxyIps.getBufStart();
	if ( ! spp ) return true;
	long nsp = g_conf.m_proxyIps.length() / sizeof(SpiderProxy);

	long now = getTimeLocal();

	for ( long i = 0 ; i < nsp ; i++ ) {

		SpiderProxy *sp = &spp[i];

		long elapsed  = now - sp->m_lastDownloadTestAttemptUTC;

		// hit test url once per minute
		if ( elapsed < 60 ) continue;

		char *tu = g_conf.m_proxyTestUrl.getBufStart();
		if ( ! tu ) continue;

		g_httpServer.blah();

	}
	return true;
}

// . handle a udp request for msg 0x54 to get the best proxy to download
//   the url they are requesting to download. this should be ultra fast
//   because we might be downloading 1000+ urls a second, although in that
//   case we should have more hosts that do the proxy load balancing.
// . when a host is done using a proxy to download a url it should 
//   send another 0x54 msg to indicate that.
// . if a host dies then it might not indicate it is done using a proxy
//   so should timeout requests and decrement the proxy load count
//   or if we notice the host is dead we should remove all its load
// . so each outstanding request needs to be kept in a hash table, 
//   which identifies the proxyip/port and the hostid using it and
//   the time it made the request. the key is the proxy ip/port.
class ProxyBucket {
public:
	// the host using the proxy
	long m_hostId;
	// the time it started
	time_t m_startTime;
	// . ip address of the url being downloaded
	// . this is the key for the m_uipTable
	long m_urlIp;
	// key is this for m_prTable
	long m_proxyIp;
	long m_port;
	// the proxy #
	long m_proxyNum;
};

// . key for this is the proxyip/port
// . data bucket is an instance of a ProxyBucket
static HashTableX m_pipTable;

// . key for this is the urlip
// . data bucket is a ptr to a ProxyBucket
static HashTableX m_uipTable;

void handleRequest54 ( UdpSlot *udpSlot ) {

	char *request     = udpSlot->m_readBuf;
	long  requestSize = udpSlot->m_readBufSize;
	// sanity check
	if ( requestSize != 4 ) {
		log("db: Got bad request 0x54 size of %li bytes. bad",
		    requestSize );
		us->sendErrorReply ( udpSlot , EBADREQUESTSIZE );
		return;
	}

	// make sure hash tables are initialized
	initTables();
		
	long urlIp = *request;

	// send to a proxy that is up and has the least amount
	// of ProxyBuckets with this urlIp, if tied, go to least loaded.

	// scan all ProxyBuckets with this m_urlIp, we need a clear
	// count array 1-1 with proxies though
	long count[MAX_SPIDER_PROXIES];
	memset ( count , 0 , s_numProxies );

	long hslot = m_uipTable.getSlot ( &urlIp );
	// scan all proxies that have this urlip outstanding
	for ( long i = hslot ; i >= 0 ; i = m_uipTable.getNextSlot(i) ) {
		// get the bucket
		ProxyBucket **pp;
		pp = (ProxyBucket **)m_uipTable.getValueFromSlot(&i);
		// get proxy # that has this out
		ProxyBucket *pb = *pp;
		long pn = pb->m_proxyNum;
		// how can this be?
		if ( pn < 0 || pn >= s_numProxies ) continue;
		// count it up
		count[pn]++;
	}

	// get the min of the counts
	long minCount = 999999;
	for ( long i = 0 ; i < s_numProxies ; i++ ) {
		// skip dead proxies, they are not candidates
		if ( s_proxies[i].m_isDead ) continue;
		if ( count[i] < minCount ) minCount = count[i];
	}

	long winneri = -1;
	// now find the best proxy wih the minCount
	for ( long i = 0 ; i < s_numProxies ; i++ ) {
		// least loaded
		long out = s_proxies[i].m_numOutstandingDownload;
		// SEVERE penalty for dead proxies, but they are
		// still candidates i guess... in case all are dead
		if ( s_proxies[i].m_isDead ) out += 9999;
		// get min
		if ( out >= outMin ) continue;
		// got a new winner
		outMin = out;
		winneri = i;
	}

	// we must have a winner
	if ( winneri < 0 ) { char *xx=NULL;*xx=0; }

	// assume download happened
	s_proxies[winneri].m_numOutstandingDownloads++;
	
	// and give proxy ip/port back to the requester so they can
	// use that to download their url
	char *p = slot->m_tmpBuf;
	*(long *)p = s_proxies[winneri].m_ip; p += 4;
	*(short *)p = s_proxies[winneri].m_port; p += 2;
	g_udpServer.sendReply ( udpSlot , slot->m_tmpBuf , 6 );
}
	
// use msg 0x55 to say you are done using the proxy
void handleRequest55 ( UdpSlot *udpSlot ) {

	char *request     = udpSlot->m_readBuf;
	long  requestSize = udpSlot->m_readBufSize;
	// sanity check
	if ( requestSize != 10 ) {
		log("db: Got bad request 0x55 size of %li bytes. bad",
		    requestSize );
		us->sendErrorReply ( udpSlot , EBADREQUESTSIZE );
		return;
	}

	// make sure hash tables are initialized
	initTables();

	char *p = request;
	long  urlIp     = *(long  *)p; p += 4;
	long  proxyIp   = *(long  *)p; p += 4;
	short proxyPort = *(short *)p; p += 2;

	// . make key for uip
	// . scan table and remove the right slot
	
}

