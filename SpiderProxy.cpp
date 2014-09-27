#include "gb-include.h"

#include "Pages.h"
#include "TcpSocket.h"
#include "HttpServer.h"
#include "SpiderProxy.h"

#define LOADPOINT_EXPIRE_MS (10*60*1000)

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
	long long m_lastDownloadTestAttemptMS;
	// use -1 to indicate timed out when downloading test url
	long   m_lastDownloadTookMS;
	// 0 means none... use mstrerror()
	long   m_lastDownloadError;
	// use -1 to indicate never
	long long m_lastSuccessfulTestMS;

	// how many times have we told a requesting host to use this proxy
	// to download their url with.
	long m_numDownloadRequests;

	// how many are outstanding? everytime a host requests a proxyip
	// it also tells us its outstanding counts for each proxy ip
	// so we can ensure this is accurate even though a host may die
	// and come back up.
	long m_numOutstandingDownloads;

	// waiting on test url to be downloaded
	bool m_isWaiting;

	long long m_timesUsed;

	long m_lastBytesDownloaded;

	// special things used by LoadBucket algo to determine which
	// SpiderProxy to use to download from a particular IP
	long m_countForThisIp;
	long long m_lastTimeUsedForThisIp;

};

// hashtable that maps an ip:port key (64-bits) to a SpiderProxy
static HashTableX s_iptab;

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
class LoadBucket {
public:
	// ip address of the url being downloaded
	long m_urlIp;
	// the time it started
	long long m_downloadStartTimeMS;
	long long m_downloadEndTimeMS;
	// the host using the proxy
	long m_hostId;
	// key is this for m_prTable
	long m_proxyIp;
	long m_proxyPort;
	// id of this loadbucket in case same host is using the same
	// proxy to download the same urlip
	long m_id;
};

// . similar to s_ipTable but maps a URL's ip to a LoadBucket
// . every download request in the last 10 minutes is represented by one
//   LoadBucket
// . that way we can ensure when downloading multiple urls of the same IP
//   that we splay them out evenly over all the proxies
static HashTableX s_loadTable;

// . when the Conf::m_proxyIps parm is updated we call this to rebuild
//   s_iptab, our table of SpiderProxy instances, which has the proxies and 
//   their performance statistics.
// . we try to maintain stats of ip/ports that did NOT change when rebuilding.
bool buildProxyTable ( ) {

	// scan the NEW list of proxy ip/port pairs in g_conf
	char *p = g_conf.m_proxyIps.getBufStart();

	HashTableX tmptab;
	tmptab.set(8,0,16,NULL,0,false,0,"tmptab");

	// scan the user inputted space-separated list of ip:ports
	for ( ; *p ; ) {
		// skip white space
		if ( is_wspace_a(*p) ) { p++; continue; }
		// scan in an ip:port
		char *s = p; char *portStr = NULL;
		long dc = 0, pc = 0, gc = 0, bc = 0;
		// scan all characters until we hit \0 or another whitespace
		for ( ; *s && !is_wspace_a(*s); s++) {
			if ( *s == '.' ) { dc++; continue; }
			if ( *s == ':' ) { portStr=s; pc++; continue; }
			if ( is_digit(*s) ) { gc++; continue; }
			bc++;
			continue;
		}
		// ensure it is a legit ip:port combo
		char *msg = NULL;
		if ( gc < 4 ) 
			msg = "not enough digits for an ip";
		if ( pc > 1 )
			msg = "too many colons";
		if ( dc != 3 )
			msg = "need 3 dots for an ip address";
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
		if ( portStr ) iplen = portStr - p;
		long ip = atoip(p,iplen);
		// another sanity check
		if ( ip == 0 || ip == -1 ) {
			log("spider: got bad proxy ip for %s",p);
			return false;
		}

		// and the port default is 80
		long port = 80;
		if ( portStr ) port = atol2(portStr+1,s-portStr-1);
		if ( port < 0 || port > 65535 ) {
			log("spider: got bad proxy port for %s",p);
			return false;
		}


		// . we got a legit ip:port
		// . see if already in our table
		unsigned long long ipKey = (unsigned long)ip;
		ipKey <<= 16;
		ipKey |= (unsigned short)(port & 0xffff);

		// also store into tmptable to see what we need to remove
		tmptab.addKey(&ipKey);

		// see if in table
		long islot = s_iptab.getSlot( &ipKey);

		// advance p
		p = s;

		// if in there, keep it as is
		if ( islot >= 0 ) continue;

		// otherwise add new entry
		SpiderProxy newThing;
		memset ( &newThing , 0 , sizeof(SpiderProxy));
		newThing.m_ip = ip;
		newThing.m_port = port;
		newThing.m_lastDownloadTookMS = -1;
		newThing.m_lastSuccessfulTestMS = -1;
		if ( ! s_iptab.addKey ( &ipKey, &newThing ) )
			return false;
	}		

 redo:
	// scan all SpiderProxies in tmptab
	for ( long i = 0 ; i < s_iptab.getNumSlots() ; i++ ) {
		// skip empty buckets in hashtable s_iptab
		if ( ! s_iptab.m_flags[i] ) continue;
		// get the key
		long long key = *(long long *)s_iptab.getKey(i);
		// must also exist in tmptab, otherwise it got removed by user
		if ( tmptab.isInTable ( &key ) ) continue;
		// shoot, it got removed. not in the new list of ip:ports
		s_iptab.removeKey ( &key );
		// hashtable is messed up now, start over
		goto redo;
	}

	return true;
}

static bool s_init = true;
HashTableX s_proxyBannedTable;
HashTableX s_banCountTable;

bool initProxyTables ( ) {
	// initialize proxy/urlip ban table?
	if ( ! s_init ) return true;
	s_init = false;
	s_proxyBannedTable.set(8,0,0,NULL,0,false,1,"proxban");
	s_banCountTable.set(4,4,0,NULL,0,false,1,"bancnt");
	return true;
}

// save the stats
bool saveSpiderProxyStats ( ) {

	// save hashtable
	s_proxyBannedTable.save(g_hostdb.m_dir,"proxybantable.dat");

	s_banCountTable.save(g_hostdb.m_dir,"proxybancounttable.dat");

	// save hash table
	return s_iptab.save(g_hostdb.m_dir,"spiderproxystats.dat");
}

bool loadSpiderProxyStats ( ) {

	initProxyTables();

	// save hashtable
	s_proxyBannedTable.load(g_hostdb.m_dir,"proxybantable.dat");

	s_banCountTable.load(g_hostdb.m_dir,"proxybancounttable.dat");

	// save hash table. this also returns false if does not exist.
	if ( ! s_iptab.load(g_hostdb.m_dir,"spiderproxystats.dat") ) 
		return false;

	// unset some flags
	for ( long i = 0 ; i < s_iptab.getNumSlots() ; i++ ) {
		// skip empty slots
		if ( ! s_iptab.m_flags[i] ) continue;
		SpiderProxy *sp = (SpiderProxy *)s_iptab.getValueFromSlot(i);
		sp->m_isWaiting = false;
	}
	return true;
}

// . sets *current to how many downloads are CURRENTly outstanding for 
//   this proxy
// . returns total # of load points, i.e. downloads, in the last 
//   LOADPOINT_EXPIRE_MS milliseconds (currently 10 minutes)
long getNumLoadPoints ( SpiderProxy *sp , long *current ) {
	// currently outstanding downloads on proxy
	*current = 0;
	long count = 0;
	// scan all proxies that have this urlip outstanding
	for ( long i = 0 ; i < s_loadTable.m_numSlots ; i++ ) {
		// skip if empty
		if ( ! s_loadTable.m_flags[i] ) continue;
		// get the bucket
		LoadBucket *lb= (LoadBucket *)s_loadTable.getValueFromSlot(i);
		// get the spider proxy this load point was for
		if ( lb->m_proxyIp != sp->m_ip ) continue;
		if ( lb->m_proxyPort != sp->m_port ) continue;

		// currently outstanding downloads on proxy
		if (  lb->m_downloadEndTimeMS == 0LL ) 
			*current = *current + 1;

		count++;
	}
	return count;
}

// . we call this from Parms.cpp which prints out the proxy related controls
//   and this table below them...
// . allows user to see the stats of each spider proxy
bool printSpiderProxyTable ( SafeBuf *sb ) {

	// only host #0 will have the stats ... so print that link
	if ( g_hostdb.m_myHost->m_hostId != 0 ) {
		Host *h = g_hostdb.getHost(0);
		sb->safePrintf("<br>"
			       "<b>See table on <a href=http://%s:%li/"
			       "admin/proxies>"
			       "host #0</a></b>"
			       "<br>"
			       , iptoa(h->m_ip)
			       , (long)(h->m_httpPort)
			       );
		//return true;
	}

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

		       "<td><b>times used</b></td>"

		       "<td><b># website IPs banning</b></td>"

		       "<td><b>load points</b></td>"

		       "<td><b>currently out</b></td>"

		       // time of last successful download. print "none"
		       // if never successfully used
		       "<td><b>test url last successful download</b></td>"
		       // we fetch a test url every minute or so through
		       // each proxy to ensure it is up. typically this should
		       // be your website so you do not make someone angry.
		       "<td><b>test url last download attempt</b></td>"
		       // print "FAILED" in red if it failed to download
		       "<td><b>test url download took</b></td>"

		       "<td><b>last bytes downloaded</b></td>"

		       "<td><b>last test url error</b></td>"

		       "</tr>"
		       
		       , TABLE_STYLE
		       , DARK_BLUE 
			);

	long now = getTimeLocal();

	// print it
	for ( long i = 0 ; i < s_iptab.getNumSlots() ; i++ ) {
		// skip empty slots
		if ( ! s_iptab.m_flags[i] ) continue;

		SpiderProxy *sp = (SpiderProxy *)s_iptab.getValueFromSlot(i);

		char *bg = LIGHT_BLUE;
		// mark with light red bg if last test url attempt failed
		if ( sp->m_lastDownloadTookMS == -1 &&
		     sp->m_lastDownloadTestAttemptMS>0 )
			bg = "ffa6a6";

		// or a perm denied error (as opposed to a timeout above)
		if ( sp->m_lastDownloadError )
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

		sb->safePrintf("<td>%lli</td>",sp->m_timesUsed);

		long banCount = s_banCountTable.getScore32 ( &sp->m_ip );
		if ( banCount < 0 ) banCount = 0;
		sb->safePrintf("<td>%li</td>",banCount);

		long currentLoad;

		// get # times it appears in loadtable
		long np = getNumLoadPoints ( sp , &currentLoad );
		sb->safePrintf("<td>%li</td>",np);

		// currently outstanding downloads on this proxy
		sb->safePrintf("<td>%li</td>",currentLoad);

		// last SUCCESSFUL download time ago. when it completed.
		long ago = now - sp->m_lastSuccessfulTestMS/1000;
		sb->safePrintf("<td>");
		// like 1 minute ago etc.
		if ( sp->m_lastSuccessfulTestMS <= 0 )
			sb->safePrintf("none");
		else
			sb->printTimeAgo ( ago , now , true );
		sb->safePrintf("</td>");

		// last download time ago
		ago = now - sp->m_lastDownloadTestAttemptMS/1000;
		sb->safePrintf("<td>");
		// like 1 minute ago etc.
		if ( sp->m_lastDownloadTestAttemptMS<= 0 )
			sb->safePrintf("none");
		else
			sb->printTimeAgo ( ago , now , true );
		sb->safePrintf("</td>");

		// how long to download the test url?
		if ( sp->m_lastDownloadTookMS != -1 )
			sb->safePrintf("<td>%lims</td>",
				       (long)sp->m_lastDownloadTookMS);
		else if ( sp->m_lastDownloadTestAttemptMS<= 0 )
			sb->safePrintf("<td>unknown</td>");
		else
			sb->safePrintf("<td>"
				       "<font color=red>FAILED</font>"
				       "</td>");

		sb->safePrintf("<td>%li</td>",sp->m_lastBytesDownloaded);

		if ( sp->m_lastDownloadError )
			sb->safePrintf("<td><font color=red>%s</font></td>",
				       mstrerror(sp->m_lastDownloadError));
		else
			sb->safePrintf("<td>none</td>");

		sb->safePrintf("</tr>\n");
	}

	sb->safePrintf("</table><br>");
	return true;
}

// class spip {
// public:
// 	long m_ip;
// 	long m_port;
// };

void gotTestUrlReplyWrapper ( void *state , TcpSocket *s ) {

	//spip *ss = (spip *)state;
	// free that thing
	//mfree ( ss , sizeof(spip) ,"spip" );

	// note it
	if ( g_conf.m_logDebugProxies )
		log("sproxy: got test url reply (%s): %s",
		    mstrerror(g_errno),s->m_readBuf);

	// we can get the spider proxy ip/port from the socket because
	// we sent this url download request to that spider proxy
	unsigned long long key = (unsigned long)s->m_ip;
	key <<= 16;
	key |= (unsigned short)(s->m_port & 0xffff);

	SpiderProxy *sp = (SpiderProxy *)s_iptab.getValue ( &key );

	// did user remove it from the list before we could finish testing it?
	if ( ! sp ) return;

	sp->m_isWaiting = false;

	// ok, update how long it took to do the download
	long long nowms = gettimeofdayInMillisecondsLocal();
	long long took = nowms - sp->m_lastDownloadTestAttemptMS;
	sp->m_lastDownloadTookMS = (long)took;

	HttpMime mime;
	mime.set ( s->m_readBuf , s->m_readOffset , NULL );

	// tiny proxy permission denied is 403
	long status = mime.getHttpStatus();
	if ( status == 403 && g_conf.m_logDebugProxies ) {
		log("sproxy: got bad http status from proxy: %li",status);
		g_errno = EPERMDENIED;
	}

	sp->m_lastBytesDownloaded = s->m_readOffset;

	// ETCPTIMEDOUT?
	sp->m_lastDownloadError = g_errno;

	// if we had no error, that was our last successful test
	if ( ! g_errno )
		sp->m_lastSuccessfulTestMS = nowms;

}

// . Process.cpp should call this from its timeout wrapper
// . updates the stats of each proxy
// . returns false and sets g_errno on error
bool downloadTestUrlFromProxies ( ) {

	// only host #0 should do the testing i guess
	//if ( g_hostdb.m_myHost->m_hostId != 0 ) return true;

	// if host #0 dies then host #1 must take its place managing the
	// spider proxies
	Host *h0 = g_hostdb.getFirstAliveHost();
	if ( g_hostdb.m_myHost != h0 ) return true;

	long long nowms = gettimeofdayInMillisecondsLocal();

	for ( long i = 0 ; i < s_iptab.getNumSlots() ; i++ ) {

		// skip empty slots
		if ( ! s_iptab.m_flags[i] ) continue;

		SpiderProxy *sp = (SpiderProxy *)s_iptab.getValueFromSlot(i);

		long long elapsed  = nowms - sp->m_lastDownloadTestAttemptMS;

		// hit test url once per 31 seconds
		if ( elapsed < 31000 ) continue;

		// or if never came back yet!
		if ( sp->m_isWaiting ) continue;

		char *tu = g_conf.m_proxyTestUrl.getBufStart();
		if ( ! tu ) continue;

		//spip *ss = (spip *)mmalloc(8,"sptb");
		//	if ( ! ss ) return false;
		//	ss->m_ip = sp->m_ip;
		//	ss->m_port = sp->m_port;
		

		sp->m_isWaiting = true;

		sp->m_lastDownloadTestAttemptMS = nowms;

		// returns false if blocked
		if ( ! g_httpServer.getDoc( tu ,
					    0 , // ip
					    0 , // offset
					    -1 , // size
					    false , // useifmodsince
					    NULL ,// state
					    gotTestUrlReplyWrapper ,
					    30*1000, // 30 sec timeout
					    sp->m_ip, // proxyip
					    sp->m_port, // proxyport
					    -1, // maxtextdoclen
					    -1 // maxtextotherlen
					    ) ) {
			//blocked++;
			continue;
		}
		// did not block somehow
		sp->m_isWaiting = false;
		// must have been an error then
		sp->m_lastDownloadError = g_errno;
		// free that thing
		//mfree ( ss , sizeof(spip) ,"spip" );
		// log it
		log("sproxy: error downloading test url %s through %s:%li"
		    ,tu,iptoa(sp->m_ip),(long)sp->m_port);
		    
	}
	return true;
}

#include "Msg13.h"

// when a host is done using a proxy to download a url it sends a signal
// and we process that signal here. to reduce the load count of that proxy.
static void returnProxy ( Msg13Request *preq , UdpSlot *udpSlot ) ;

// a host is asking us (host #0) what proxy to use?
void handleRequest54 ( UdpSlot *udpSlot , long niceness ) {

	char *request     = udpSlot->m_readBuf;
	long  requestSize = udpSlot->m_readBufSize;

	// we now use the top part of the Msg13Request as the ProxyRequest
	Msg13Request *preq = (Msg13Request *)request;

	// sanity check
	if ( requestSize != preq->getProxyRequestSize() ) {
		log("db: Got bad request 0x54 size of %li bytes. bad",
		    requestSize );
		g_udpServer.sendErrorReply ( udpSlot , EBADREQUESTSIZE );
		return;
	}

	// is the request telling us it is done downloading through a proxy?
	if ( preq->m_opCode == OP_RETPROXY ) {
		returnProxy ( preq , udpSlot );
		return;
	}

	// if sender is asking for a new proxy and wants us to ban
	// the previous proxy we sent for this urlIp...
	if ( preq->m_banProxyIp ) {
		// these must match
		char *xx;
		if(preq->m_banProxyIp   != preq->m_proxyIp  ){xx=NULL;*xx=0;}
		if(preq->m_banProxyPort != preq->m_proxyPort){xx=NULL;*xx=0;}
		// this will "return" the banned proxy
		returnProxy ( preq , NULL );
		// now add it to the banned table
		long long uip = preq->m_urlIp;
		long long pip = preq->m_banProxyIp;
		long long h64 = hash64h ( uip , pip );
		if ( ! s_proxyBannedTable.isInTable ( &h64 ) ) {
			s_proxyBannedTable.addKey ( &h64 );
			// for stats counting. each proxy ip maps to #
			// of unique website IPs that have banned it.
			s_banCountTable.addTerm32 ( (long *)&pip );
		}
	}
	

	// shortcut
	long urlIp = preq->m_urlIp;

	// send to a proxy that is up and has the least amount
	// of LoadBuckets with this urlIp, if tied, go to least loaded.

	// clear counts for this url ip for scoring the best proxy to use
	for ( long i = 0 ; i < s_iptab.getNumSlots() ; i++ ) {
		// skip empty slots
		if ( ! s_iptab.m_flags[i] ) continue;
		SpiderProxy *sp = (SpiderProxy *)s_iptab.getValueFromSlot(i);
		sp->m_countForThisIp = 0;
		sp->m_lastTimeUsedForThisIp = 0LL;
	}

	// this table maps a url's current IP to a possibly MULTIPLE slots
	// which tell us what proxy is downloading a page from that IP.
	// so we can try to find a proxy that is not download a url from
	// this IP currently, or hasn't been for the longest time...
	long hslot = s_loadTable.getSlot ( &urlIp );
	// scan all proxies that have this urlip outstanding
	for ( long i = hslot ; i >= 0 ; i = s_loadTable.getNextSlot(i,&urlIp)){
		// get the bucket
		LoadBucket *lb;
		lb = (LoadBucket *)s_loadTable.getValueFromSlot(i);
		// get the spider proxy this load point was for
		unsigned long long key = (unsigned long)lb->m_proxyIp;
		key <<= 16;
		key |= (unsigned short)lb->m_proxyPort;
		SpiderProxy *sp = (SpiderProxy *)s_iptab.getValue(&key);
		// must be there unless user remove it from the list
		if ( ! sp ) continue;
		// count it up
		if (  lb->m_downloadEndTimeMS == 0LL ) 
			sp->m_countForThisIp++;
		// set the last time used to the most recently downloaded time
		// that this proxy has downloaded from this ip
		if ( lb->m_downloadEndTimeMS &&
		     lb->m_downloadEndTimeMS > sp->m_lastTimeUsedForThisIp )
			sp->m_lastTimeUsedForThisIp = lb->m_downloadEndTimeMS;
	}

	// first try to get a spider proxy that is not "dead"
	bool skipDead = true;

	long numBannedProxies = 0;
	long aliveProxyCandidates = 0;

 redo:
	// get the min of the counts
	long minCount = 999999;
	for ( long i = 0 ; i < s_iptab.getNumSlots() ; i++ ) {
		// skip empty slots
		if ( ! s_iptab.m_flags[i] ) continue;
		// get the spider proxy
		SpiderProxy *sp = (SpiderProxy *)s_iptab.getValueFromSlot(i);

		// if this proxy was banned by the url's ip... skip it. it is
		// not a candidate...
		if ( skipDead ) {
			long long uip = preq->m_urlIp;
			long long pip = sp->m_ip;
			long long h64 = hash64h ( uip , pip );
			if ( s_proxyBannedTable.isInTable ( &h64 ) ) {
				numBannedProxies++;
				continue;
			}
		}

		// if it failed the last test, skip it
		if ( skipDead && sp->m_lastDownloadError ) continue;

		if ( skipDead ) aliveProxyCandidates++;

		if ( sp->m_countForThisIp >= minCount ) continue;
		minCount = sp->m_countForThisIp;
	}

	// all dead? then get the best dead one
	if ( minCount == 999999 ) {
		skipDead = false;
		goto redo;
	}

	long long oldest = 0x7fffffffffffffffLL;
	SpiderProxy *winnersp = NULL;
	// now find the best proxy wih the minCount
	for ( long i = 0 ; i < s_iptab.getNumSlots() ; i++ ) {
		// skip empty slots
		if ( ! s_iptab.m_flags[i] ) continue;
		// get the spider proxy
		SpiderProxy *sp = (SpiderProxy *)s_iptab.getValueFromSlot(i);
		// if it failed the last test, skip it... not here...
		if ( skipDead && sp->m_lastDownloadError ) continue;
		// if all hosts were "dead" because they all had 
		// m_lastDownloadError set then minCount will be 999999
		// and nobody should continue from this statement:
		if ( sp->m_countForThisIp > minCount ) continue;
		// then go by last download time for this ip
		if ( sp->m_lastTimeUsedForThisIp >= oldest ) continue;
		// if this proxy was banned by the url's ip... skip it. it is
		// not a candidate...
		if ( skipDead ) {
			long long uip = preq->m_urlIp;
			long long pip = sp->m_ip;
			long long h64 = hash64h ( uip , pip );
			if ( s_proxyBannedTable.isInTable ( &h64 ) ) continue;
		}
		// pick the spider proxy used longest ago
		oldest = sp->m_lastTimeUsedForThisIp;
		// got a new winner
		winnersp = sp;
	}

	// we must have a winner
	if ( ! winnersp ) { char *xx=NULL;*xx=0; }

	long long nowms = gettimeofdayInMillisecondsLocal();

	// winner count update
	winnersp->m_timesUsed++;

	// add a new load bucket then!
	LoadBucket bb;
	bb.m_urlIp = urlIp;
	// the time it started
	bb.m_downloadStartTimeMS = nowms;
	// download has not ended yet
	bb.m_downloadEndTimeMS = 0LL;
	// the host using the proxy
	bb.m_hostId = udpSlot->m_hostId;
	// key is this for m_prTable
	bb.m_proxyIp   = winnersp->m_ip;
	bb.m_proxyPort = winnersp->m_port;
	// a new id. we use this to update the downloadEndTime when done
	static long s_lbid = 0;
	bb.m_id = s_lbid++;
	// add it now
	s_loadTable.addKey ( &urlIp , &bb );

	// sanity
	if ( (long)sizeof(ProxyReply) > TMPBUFSIZE ){char *xx=NULL;*xx=0;}

	// and give proxy ip/port back to the requester so they can
	// use that to download their url
	ProxyReply *prep = (ProxyReply *)udpSlot->m_tmpBuf;
	prep->m_proxyIp = winnersp->m_ip;
	prep->m_proxyPort = winnersp->m_port;
	// do not count the proxy we are returning as "more"
	prep->m_hasMoreProxiesToTry = ( aliveProxyCandidates > 1 );
	// and the loadbucket id, so requester can tell us it is done
	// downloading through the proxy and we can update the LoadBucket
	// for this transaction (m_lbId)
	prep->m_lbId = bb.m_id;
	// requester wants to know how many proxies have been banned by the
	// urlIp so it can increase a self-imposed crawl-delay to be more
	// sensitive to the spider policy.
	prep->m_numBannedProxies = numBannedProxies;

	//char *p = udpSlot->m_tmpBuf;
	//*(long  *)p = winnersp->m_ip  ; p += 4;
	//*(short *)p = winnersp->m_port; p += 2;
	// and the loadbucket id
	//*(long *)p = bb.m_id; p += 4;

	// now remove old entries from the load table. entries that
	// have completed and have a download end time more than 10 mins ago
	for ( long i = 0 ; i < s_loadTable.getNumSlots() ; i++ ) {
		// skip if empty
		if ( ! s_loadTable.m_flags[i] ) continue;
		// get the bucket
		LoadBucket *pp =(LoadBucket *)s_loadTable.getValueFromSlot(i);
		// skip if still active
		if ( pp->m_downloadEndTimeMS == 0LL ) continue;
		// delta t
		long long took = nowms - pp->m_downloadEndTimeMS;
		// < 10 mins?
		if ( took < LOADPOINT_EXPIRE_MS ) continue;
		// ok, its too old, nuke it to save memory
		s_loadTable.removeSlot(i);
		// the keys might have buried us but we really should not
		// mis out on analyzing any keys if we just keep looping here
		// should we? TODO: figure it out. if we miss a few it's not
		// a big deal.
		i--;
	}

	// send the proxy ip/port/LBid back to user
	g_udpServer.sendReply_ass ( udpSlot->m_tmpBuf , // msg
				    sizeof(ProxyReply) , // msgSize
				    udpSlot->m_tmpBuf , // alloc
				    sizeof(ProxyReply) , 
				    udpSlot , 
				    60 ) ; // 60s timeout
}
	
// . use msg 0x55 to say you are done using the proxy
// . we now use the top part of the Msg13Request as the proxy request
void returnProxy ( Msg13Request *preq , UdpSlot *udpSlot ) {

	//char *p = request;
	//long  proxyIp   = *(long  *)p; p += 4;
	//short proxyPort = *(short *)p; p += 2;
	//long  lbId      = *(long  *)p; p += 4;

	long  urlIp     = preq->m_urlIp;

	//
	// update the load bucket
	//

	// scan over all that match to find lbid
	long hslot = s_loadTable.getSlot ( &urlIp );
	// scan all proxies that have this urlip outstanding
	long i;for (i=hslot ; i >= 0 ; i = s_loadTable.getNextSlot(i,&urlIp)){
		// get the bucket
		LoadBucket *lb= (LoadBucket *)s_loadTable.getValueFromSlot(i);
		// is it the right id?
		if ( lb->m_id != preq->m_lbId ) continue;
		if ( lb->m_proxyIp != preq->m_proxyIp ) continue;
		if ( lb->m_proxyPort != preq->m_proxyPort ) continue;
		// that's it. set the download end time
		long long nowms = gettimeofdayInMillisecondsLocal();
		lb->m_downloadEndTimeMS = nowms;
		break;
	}

	if ( i < 0 ) 
		log("sproxy: could not find load bucket id #%li",preq->m_lbId);

	// if no slot provided, return to called without sending back reply,
	// they are banning a proxy and need to also return it before
	// we send them back another proxy to try.
	if ( ! udpSlot ) return;

	// gotta send reply back
	g_udpServer.sendReply_ass ( 0,
				    0 , // msgSize
				    0 , // alloc
				    0 , 
				    udpSlot , 
				    60 ) ; // 60s timeout

}

// call this at startup to register the handlers
bool initSpiderProxyStuff() {
	
	// do this for all hosts in case host #0 goes dead, then everyone
	// will, according to Msg13.cpp, send to host #1, the next in line
	// if she is alive
	//if ( g_hostdb.m_myHostId != 0 ) return true;

	// only host #0 has handlers
	if ( ! g_udpServer.registerHandler ( 0x54, handleRequest54 )) 
		return false;

	// key is ip/port
	s_iptab.set(8,sizeof(SpiderProxy),0,NULL,0,false,0,"siptab");

	loadSpiderProxyStats();

	// build the s_iptab hashtable for the first time
	buildProxyTable ();

	// make the loadtable hashtable
	static bool s_flag = 0;
	if ( s_flag ) return true;
	s_flag = true;
	return s_loadTable.set(4,
			       sizeof(LoadBucket),
			       128,
			       NULL,
			       0,
			       true, // allow dups?
			       MAX_NICENESS,
			       "lbtab");

}

