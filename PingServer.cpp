#include "gb-include.h"

// cygwin and apple mac os x does not support klogctl
#if defined(CYGWIN) || defined(__APPLE__)
// use a stub
int32_t klogctl( int, char *,int ) { return 0; }
#else
// otherwise, use the real one
#include <sys/klog.h> // for klogctl
#endif

#include "PingServer.h"
#include "UdpServer.h"
//#include "Sync.h"
#include "Conf.h"
#include "HttpServer.h"
#include "HttpMime.h"
#include "Proxy.h"
#include "Repair.h" 
#include "Process.h"
#include "DailyMerge.h"
#include "Spider.h"
#include "Test.h"
#include "Rebalance.h"
#include "Version.h"

#define PAGER_BUF_SIZE (10*1024)

// from main.cpp. when keepalive script restarts us this is true
extern bool g_recoveryMode;
extern int32_t g_recoveryLevel;

// a global class extern'd in .h file
PingServer g_pingServer;

char s_kernelRingBuf[4097];
int32_t s_kernelRingBufLen = 0;

static void sleepWrapper ( int fd , void *state ) ;
//static void sleepWrapper10 ( int fd , void *state );
static void checkKernelErrors( int fd, void *state );
static void gotReplyWrapperP ( void *state , UdpSlot *slot ) ;
static void handleRequest11 ( UdpSlot *slot , int32_t niceness ) ;
static void gotReplyWrapperP2 ( void *state , UdpSlot *slot );
static void gotReplyWrapperP3 ( void *state , UdpSlot *slot );
static void updatePingTime ( Host *h , int32_t *pingPtr , int32_t tripTime ) ;
// JAB: warning abatement
//static bool pageTMobile ( Host *h , char *errmsg ) ;
//static bool pageAlltel  ( Host *h , char *errmsg , char *num ) ;
//static bool pageVerizon ( Host *h , char *errmsg ) ;
//static void verizonWrapper ( void *state , TcpSocket *ts ) ;
//static bool pageVerizon2 ( void *state , TcpSocket *s ) ;
// JAB: warning abatement
//static bool pageSprintPCS ( Host *h , char *errmsg , char *num ) ;
//static void sprintPCSWrapper ( void *state , TcpSocket *ts ) ;
//static bool pageSprintPCS2 ( void *state , TcpSocket *ts ) ;
static bool sendAdminEmail ( Host  *h, char  *fromAddress,
			     char  *toAddress, char  *body ,
			     char  *emailServIp );//= "mail.gigablast.com" );
static void emailSleepWrapper ( int fd, void *state );

bool PingServer::registerHandler ( ) {
	// . we'll handle msgTypes of 0x11 for pings
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x0A
	// . it is now hot..., no, not hot anymore
	//if ( ! g_udpServer2.registerHandler ( 0x11, handleRequest11 )) 
	//	return false;
	// register on low priority server to make transition easier
	if ( ! g_udpServer.registerHandler ( 0x11, handleRequest11 )) 
		return false;
	// limit this to 1000ms
	if ( g_conf.m_pingSpacer > 1000 ) g_conf.m_pingSpacer = 1000;
	// save this
	m_pingSpacer = g_conf.m_pingSpacer;
	// this starts off at zero
	m_callnum = 0;
	// . this was 500ms but now when a host shuts downs it sends all other
	//   hosts a msg saying so... PingServer::broadcastShutdownNotes() ...
	// . so i put it up to 2000ms to save bandwidth some
	// . but these packets are small and cheap so now let's send out 10
	//   every second to keep on top of things
	// . so in a network of 128 hosts we'll cycle through in 12.8 seconds
	//   and set a host to dead on avg will take about 13 seconds
	if ( ! g_loop.registerSleepCallback ( g_conf.m_pingSpacer , // ~100ms
					      (void *)(PTRTYPE)m_callnum,//NULL
					      sleepWrapper , 0 ) )
		return false;
	// if not host #0, we're done
	if ( g_hostdb.m_hostId != 0 ) return true;

	// we have disabled syncing for now
	return true;

	// . this is called ever 10 minutes
	// . it tells all hosts to store a sync point at about the same time
	// . the sync point is a fixed value in time used as a reference
	//   for performing incremental synchronization by Sync.cpp should
	//   a host go down without saving. one of its twins must of course
	//   remain up in order to sync him.
	//if ( ! g_loop.registerSleepCallback ( SYNC_TIME*1000     , // ms
	//				      NULL           , 
	//				      sleepWrapper10 ) )
	//	return false;
	return true;
}

static int32_t s_outstandingPings = 0;

// . gets filename that contains the hosts from the Conf file
// . return false on errro
// . g_errno may NOT be set
bool PingServer::init ( ) {
	// reset how many pings we've launched
	//m_launched      =  0;
	//m_totalLaunched =  0;
	m_i             =  0;
	m_numRequests2  =  0;
	m_numReplies2   =  0;
	m_useShotgun    =  0;
	m_pingProxy     =  0;
	m_minRepairMode             = -1;
	m_maxRepairMode             = -1;
	m_minRepairModeBesides0     = -1;
	m_minRepairModeHost         = NULL;
	m_maxRepairModeHost         = NULL;
	m_minRepairModeBesides0Host = NULL;

	initKernelErrorCheck();

	// invalid info init
	m_currentPing  = -1;
	m_bestPing     = -1;
	m_bestPingDate =  0;

	m_numHostsWithForeignRecs = 0;
	m_numHostsDead = 0;
	m_hostsConfInDisagreement = false;
	m_hostsConfInAgreement = false;

	// . send a ping request to everybody on the network right now!
	// . no, sleepWrapper needs to do it when g_loop is working
	//sendPingsToAll();
	// we're done
	return true;
}

void PingServer::initKernelErrorCheck(){
	// Most of the important logfiles are gathered in a single 
	// directory - /var/log

	// * /var/log/dmesg - boot time hardware detection and driver setup
	// * /var/log/messages - general system messages, includes most of
	//   what is in dmesg if it hasn't "rolled over".
	// * /var/log/syslog - like the "messages" log, but sometimes includes
	//   other details
	// * /var/log/mythtv/mythbackend.log - status logs from the MythTV
	//   backend server
	// * /var/log/mysql/mysql.err - errors from the database server
	//   (usually empty!)
	// * /var/log/daemon.log - messages from service tasks like lircd
	// * /var/log/kern.log - if something has gone wrong with a kernel
	//   module, you may find something here.
	// * /var/log/Xorg.0.log - start up log from the X server (GUI
	//   environment), including hardware detection and modes (resolution)
	//   selected

	// BUT FOUND A BETTER WAY THAN TO READ LOG FILES!
	// directly read the kernel ring buffer every x sec using klogctl.
	// look for errors directly in the kernel ring buffer.

	// fill the kernel ring buf with info that was available before startup
	s_kernelRingBufLen = klogctl( 3, s_kernelRingBuf, 4096 );
	if ( s_kernelRingBufLen < 0 ){
		log ("db: klogctl returned error");
	}


	// clear the kernel Errors 
	for ( int32_t i = 0; i < g_hostdb.m_numHosts; i++ ){
		g_hostdb.m_hosts[i].m_pingInfo.m_kernelErrors = 0;
		g_hostdb.m_hosts[i].m_kernelErrorReported = false;
	}

	if ( !g_loop.registerSleepCallback( 30000, NULL, checkKernelErrors,0))
		log ("registering kern.log failed");
	return;
}


void PingServer::sendPingsToAll ( ) {

	//g_conf.m_logDebugUdp = 1;
	// stop for now
	//return;

	if ( g_isYippy ) return;

	// if we are the query/spider compr. proxy then do not send out pings
	if ( g_hostdb.m_myHost->m_type & HT_QCPROXY ) return;
	if ( g_hostdb.m_myHost->m_type & HT_SCPROXY ) return;


	// do not ping ourselves if we are host #0, because wrapper
	// will not be called...
	//if ( g_hostdb.m_hostId == 0 ) g_host0Replied = true;

	// get host #0
	Host *hz = g_hostdb.getHost ( 0 );
	// sanity check
	if ( hz->m_hostId != 0 ) { char *xx=NULL;*xx=0; }


	// do a quick send to host 0 out of band if we have never
	// got a reply from him. we need him to sync our clock!
	static int32_t s_lastTime = 0;
	if ( ! hz->m_inProgress1 && hz->m_numPingReplies == 0 &&
	     time(NULL) - s_lastTime > 2 ) {
		// update clock to avoid oversending
		s_lastTime = time(NULL);
		// ping him
		pingHost ( hz , hz->m_ip , hz->m_port );
	}

	// once we do a full round, drop out. use firsti to determine this
	int32_t firsti = -1;

	for ( ; m_i != firsti && s_outstandingPings < 5 ; ) {
		// store it
		if ( firsti == -1 ) firsti = m_i;
		// get the next host in line
		Host     *h    = g_listHosts [ m_i ];
		uint32_t  ip   = g_listIps   [ m_i ];
		uint16_t  port = g_listPorts [ m_i ];
		// point to next ip/port, and wrap if we should
		if ( ++m_i >= g_listNumTotal ) m_i = 0;
		// skip if not udp port. might be http, dns or https.
		if ( port != h->m_port ) continue;
		// if he is in progress, skip as well. check for shotgun ip.
		if ( ip == h->m_ip && h->m_inProgress1 ) continue;
		if ( ip != h->m_ip && h->m_inProgress2 ) continue;
		// do not ping query compression proxies or spider comp proxies
		if ( h->m_type & HT_QCPROXY ) continue;
		if ( h->m_type & HT_SCPROXY ) continue;
		// try to launch the request
		pingHost ( h , ip , port ) ;
	}


	
	// . check if pingSpacer was updated via master controls and fix our
	//   sleep wrapper callback interval if so
	// . limit this to 1000ms
	if ( g_conf.m_pingSpacer > 1000 ) g_conf.m_pingSpacer = 1000;
	// update pingSpacer callback tick if it changed since last time
	if ( m_pingSpacer == g_conf.m_pingSpacer ) return;
	// register new one
	if ( ! g_loop.registerSleepCallback ( m_pingSpacer , // ~100ms
					      (void *)(PTRTYPE)(m_callnum+1) ,
					      sleepWrapper , 0 ) ) {
		static char logged = 0;
		if ( ! logged )
			log("net: Could not update ping spacer.");
		logged = 1;
		return;
	}
	// now it is safe to unregister last callback then
	g_loop.unregisterSleepCallback((void *)(PTRTYPE)m_callnum,
				       sleepWrapper);
	// point to next one
	m_callnum++;
}


// class HostStatus {
// public:
// 	int64_t m_lastPing;
// 	char m_repairMode;
// 	char m_kernelError;
// 	char m_loadAvg;
// 	char m_percentMemUsed;

// };

// from Loop.cpp
extern float g_cpuUsage;

// ping host #i
void PingServer::pingHost ( Host *h , uint32_t ip , uint16_t port ) {
	// don't ping on interface machines
	//if ( g_conf.m_interfaceMachine ) return;

	// do not ping ourselves
	//if ( h == g_hostdb.m_myHost ) return;
        
	// return if NULL
	if ( ! h ) return;

        int32_t hostId = h->m_hostId;

	// every time this is hostid 0, do a sanity check to make sure
	// g_hostdb.m_numHostsAlive is accurate
	if ( hostId == 0 ) {
                int32_t numHosts = g_hostdb.getNumHosts();
                if( h->m_isProxy )
                        numHosts = g_hostdb.getNumProxy();
		// do not do more than once every 10 seconds
		static int32_t lastTime = 0;
		int32_t now = getTime();
		if ( now - lastTime > 10 ) {
			lastTime = now;
			int32_t count = 0;
			for ( int32_t i = 0 ; i < numHosts; i++ ) {
				// count if not dead
                                Host *host;
                                if ( h->m_isProxy )
                                        host = g_hostdb.getProxy(i);
                                else 
                                        host = g_hostdb.getHost(i);
				if ( !g_hostdb.isDead(host))
					count++;
			}
			// make sure count matches
			if ( (!h->m_isProxy && 
			      count != g_hostdb.getNumHostsAlive()) ) {
                             //( h->m_isProxy &&
                             //count != g_hostdb.getNumProxyAlive()) ) { 
				char *xx = NULL; *xx = 0; }
		}
	}

	// watch for out of bounds
	//if ( hostId < 0 || hostId >= g_hostdb.getNumHosts() ) return;
	// don't ping ourselves, we know we're alive
	// only if we aren't the proxy
	// MDW: no, let's ping ourselves so all the Host vars can be
	//      set properly in here and we don't have to repeat that
	//      code.
	/*
	if ( hostId == g_hostdb.m_hostId && h == g_hostdb.m_myHost ){
                //&& !g_proxy.isProxyRunning() ) {
		// set loadavg and return
		h->m_loadAvg = g_process.getLoadAvg();
		// set our percent mem used
		h->m_percentMemUsed = ((float)g_mem.getUsedMem()*100.0)/
			(float)g_mem.getMaxMem();
		return;
	}
	*/
	// don't ping again if already in progress
	if ( ip == h->m_ip && h->m_inProgress1 ) return;
	if ( ip != h->m_ip && h->m_inProgress2 ) return;
	// time now
	int64_t nowmsLocal = gettimeofdayInMillisecondsLocal();
	//int64_t now2 = gettimeofdayInMillisecondsLocal();
	// only ping a host once every 5 seconds tops
	//if ( now - h->m_lastPing < 5000 ) return;
	// stamp it
	//h->m_pingInfo.m_lastPing = nowmsLocal;
	h->m_lastPing = nowmsLocal;
	// count it
	s_outstandingPings++;
	// consider it in progress
	if ( ip == h->m_ip ) h->m_inProgress1 = true;
	else                 h->m_inProgress2 = true;
	// . if he is not dead, set this first send time
	// . this can be the first time we sent an unanswered ping
	// . this is only used to determine when to send emails since we
	//   can now send the email a configurable amount of seconds after the
	//   host as already been registered as dead. i hope this will fix the
	//   problem of the hitachis causing bus resets all the time. because
	//   we make the dead threshold only a few seconds to protect 
	//   performance, but make the email time much higher so i can sleep
	//   at night.
	// . in the beginning all hosts are considered dead, but the email code
	//   should not be 0, it should be -2
	// . BOTH eth ports must be dead for host to be dead
	bool isAlive = false;
	if ( h->m_ping        < g_conf.m_deadHostTimeout ) isAlive = true;
	if ( g_conf.m_useShotgun && 
	     h->m_pingShotgun < g_conf.m_deadHostTimeout ) isAlive = true;
	if ( isAlive && h->m_emailCode == 0 ) h->m_startTime =nowmsLocal;//now2
	// log it only the first time he is registered as dead
	if ( isAlive ) h->m_wasAlive = true;
	else           h->m_wasAlive = false;
	// note it. wait 30 seconds cuz they are all high at the start when
	// everyone is just coming up.
	//if ( h->m_ping > 1000 && 
	//     gettimeofdayInMillisecondsLocal()-g_process.m_processStartTime>
	//     30000 )
	//	log("gb: Got high ping of %"INT32" ms to hostid #%"INT32"",
	//	    (int32_t)h->m_ping,(int32_t)h->m_hostId);
	// . use the shotgun port to send the request? or the original port?
	// . the reply will be sent back to the same ip/port as it was sent
	//   from because in PingServer::handleRequest() we set the
	//   "useSameSwitch" flag to true when passing to 
	//   UdpServer::sendReply_ass()
	//uint32_t ip;
	//if ( useShotgunIp ) ip   = h->m_ipShotgun;
	//else                ip   = h->m_ip;
	Host *me = g_hostdb.m_myHost;
	// use the tmp buf
	//char request[14+4+4+1];
	//char *p = m_request;

	// we can have multiple outstanding pings, so keep request bufs
	// independent...
	//char *request = h->m_requestBuf;
	//char *p       = h->m_requestBuf;

	// we send our stats to host "h"
	PingInfo *pi = &me->m_pingInfo;//RequestBuf;

	pi->m_numCorruptDiskReads = g_numCorrupt;
	pi->m_numOutOfMems = g_mem.m_outOfMems;
	pi->m_socketsClosedFromHittingLimit = g_stats.m_closedSockets;
	pi->m_currentSpiders = g_spiderLoop.m_numSpidersOut;

	// store the last ping we got from it first
	//*(int64_t *)p = h->m_lastPing; p += sizeof(int64_t);

	// i don't think this should be in pinginfo
	//pi->m_lastPing = h->m_pingInfo.m_lastPing;

	// let the receiver know our repair mode
	//*p = g_repairMode; p++;
	pi->m_repairMode = g_repairMode;

	// problem is that we know when the error occurs, but don't know when 
	// the error has been fixed. So just consider this host as dead unless
	// gb is restarted and the problem is fixed
	//*p = me->m_kernelErrors; p++;
	//pi->m_kernelErrors = me->m_pingInfo.m_kernelErrors;

	//if ( me->m_kernelErrors ){
	//char *xx = NULL; *xx = 0;
	//}
	int32_t l_loadavg = (int32_t) (g_process.getLoadAvg() * 100.0);
	//gbmemcpy(p, &l_loadavg, sizeof(int32_t));	p += sizeof(int32_t);
	pi->m_loadAvg = l_loadavg ;

	// then our percent mem used
	float mem = ((float)g_mem.getUsedMem()*100.0)/(float)g_mem.getMaxMem();
	//*(float *)p = mem ; p += sizeof(float); // 4 bytes
	pi->m_percentMemUsed = mem;

	// our cpu usage
	//*(float *)p = me->m_cpuUsage ; p += sizeof(float); // 4 bytes
	//pi->m_cpuUsage = me->m_pingInfo.m_cpuUsage;

	// our num recs, docsIndexed
	//*(int32_t*)p = (int32_t)g_clusterdb.getRdb()->getNumTotalRecs();
	// *(int32_t*)p = (int32_t)g_process.getTotalDocsIndexed();
	// p += sizeof(int32_t);
	pi->m_totalDocsIndexed = (int32_t)g_process.getTotalDocsIndexed();

	// urls indexed since startup
	//*(int32_t*)p = (int32_t)g_test.m_urlsIndexed;
	//p += sizeof(int32_t);
	// our num recs, eventsIndexed
	//*(int32_t*)p = g_timedb.getNumTotalEvents();//g_coldb.m_numEventsAllColl
	//*(int32_t *)p = 0;
	//p += sizeof(int32_t);
	// slow disk reads
	// *(int32_t*)p = g_stats.m_slowDiskReads;
	// p += sizeof(int32_t);
	pi->m_slowDiskReads = g_stats.m_slowDiskReads;


	// and hosts.conf crc
	//*(int32_t *)p = g_hostdb.getCRC(); p += 4;
	pi->m_hostsConfCRC = g_hostdb.getCRC();

	// ensure crc is legit
	if ( g_hostdb.getCRC() == 0 ) { char *xx=NULL;*xx=0; }

	// disk usage (df -ka)
	//*(float *)p = g_process.m_diskUsage; p += 4;
	pi->m_diskUsage = g_process.m_diskUsage;

	// flags indicating our state
	int32_t flags = 0;
	// let others know we are doing our daily merge and have turned off
	// our spiders. when host #0 indicates this state it will wait
	// for all other hosts to enter the mergeMode. when other hosts
	// receive this state from host #0, they will start their daily merge.
	if ( g_spiderLoop.m_numSpidersOut > 0 ) flags |= PFLAG_HASSPIDERS;
	if ( g_process.isRdbMerging()         ) flags |= PFLAG_MERGING;
	if ( g_process.isRdbDumping()         ) flags |= PFLAG_DUMPING;
	if ( g_rebalance.m_isScanning         ) flags |= PFLAG_REBALANCING;
	if ( g_recoveryMode                   ) flags |= PFLAG_RECOVERYMODE;
	if ( g_rebalance.m_numForeignRecs     ) flags |= PFLAG_FOREIGNRECS;
	if ( g_dailyMerge.m_mergeMode    == 0 ) flags |= PFLAG_MERGEMODE0;
	if ( g_dailyMerge.m_mergeMode ==0 || g_dailyMerge.m_mergeMode == 6 )
		flags |= PFLAG_MERGEMODE0OR6;
	if ( ! isClockInSync() ) flags |= PFLAG_OUTOFSYNC;

	uint8_t rv8 = (uint8_t)g_recoveryLevel;
	if ( g_recoveryLevel > 255 ) rv8 = 255;
	pi->m_recoveryLevel = rv8;

	//*(int32_t *)p = flags; p += 4; // 4 bytes
	pi->m_flags = flags;

	// the collection number we are daily merging (currently 2 bytes)
	collnum_t cn = -1;
	if ( g_dailyMerge.m_cr ) cn = g_dailyMerge.m_cr->m_collnum;
	//*(collnum_t *)p = cn ; p += sizeof(collnum_t);
	pi->m_dailyMergeCollnum = cn;

	pi->m_hostId = me->m_hostId;

	pi->m_localHostTimeMS = gettimeofdayInMillisecondsLocal();

	pi->m_udpSlotsInUseIncoming = g_udpServer.getNumUsedSlotsIncoming();

	pi->m_tcpSocketsInUse = g_httpServer.m_tcp.m_numUsed;

	// from Loop.cpp
	pi->m_cpuUsage = g_cpuUsage;

	// store hd temps
	// gbmemcpy ( p , me->m_hdtemps , 4 * 2 );
	// p += 4 * 2;
	//gbmemcpy ( &pi->m_hdtemps , me->m_pingInfo.m_hdtemps , 4 * 2 );

	// store the gbVersionStrBuf now, just a date with a \0 included
	char *v = getVersion();
	int32_t vsize = getVersionSize(); // 21 bytes
	// gbmemcpy ( p , v , vsize );
	// p += vsize;
	if ( vsize != 21 ) { char *xx=NULL;*xx=0; }
	gbmemcpy ( pi->m_gbVersionStr , v , vsize );

	// int32_t requestSize = sizeof(PingRequest);//p - request;

	// // sanity check
	// if ( requestSize != sizeof(PingRequest) ) { 
	// 	// (44+4+4+21) ) { // MAX_PING_SIZE ) { 
	// 	log("ping: "
	// 	    "YOU ARE MIXING MULTIPLE GB VERSIONS IN YOUR CLUSTER. "
	// 	    "MAKE SURE THEY ARE ALL THE SAME GB BINARY");
	// 	char *xx = NULL; *xx = 0; }


	//char *request = (char *)pi;

	// debug msg
	//logf(LOG_DEBUG,"net: Sending ping request to hid=%"INT32" ip=%s.",
	//     h->m_hostId,iptoa(ip));
	// . launch one
	// . returns false and sets errno on error
	// . returns true if request sent and it blocked
	// . use MsgType of 0x11 for pinging
	// . we now use the high-prioirty server, g_udpServer2
	// . now we send over our current time so remote host will sync up
	//   with us
	// . only sync up with hostId #0
	// . if he goes down its ok because time is mostly important for 
	//   spidering and spidering is suspended if a host is down
	//if ( g_udpServer.sendRequest ( (char *)(&h->m_lastPing)  ,
	//				8             ,

	// the proxy may be interfacing with the temporary cluster while
	// we update the main cluster...
	//int32_t port = h->m_port;
	if ( g_proxy.isProxyRunning() && g_conf.m_useTmpCluster )
		port++;

        if ( h->m_isProxy ) hostId = -1;
	if (  g_udpServer.sendRequest ( (char *)pi , //request       ,
					sizeof(PingInfo),//requestSize   ,
					0x11          ,
					ip            ,//h->m_ip       ,
					port          ,//h->m_port     ,
					hostId        ,
					NULL          ,
					(void *)h     , // callback state
					gotReplyWrapperP ,
					// timeout
					(g_conf.m_deadHostTimeout/1000)+1 ,
					1000          ,           // backoff
					2000          ,  // max wait
					NULL          ,  // reply buf
					0             ,  // reply buf size
					0             )) // niceness
		return;
	// it had an error, so dec the count
	s_outstandingPings--;
	// consider it out of progress
	if ( ip == h->m_ip ) h->m_inProgress1 = false;
	else                 h->m_inProgress2 = false;
	// had an error
	log("net: Pinging host #%"INT32" had error: %s.",
	    h->m_hostId,mstrerror(g_errno) );
	// reset it cuz it's not a showstopper
	g_errno = 0;
}

// . this is the last hostId we sent an alert about
// . it is -1 if he has come back up since
// . it is -1 if we've sent no alerts
// . it is so we only send out one email even though a bunch of consecutive
//   hosts right before us (in hostid ranking) may have gone down. The admin
//   only needs one notification.
static int32_t s_lastSentHostId = -1;

void gotReplyWrapperP ( void *state , UdpSlot *slot ) {
	// state is the host
        Host *h = (Host *)state;
	int32_t hid = h->m_hostId;
	// if host 0 special case.
	//if ( hid == 0 && g_sendingToHost0 ) {
	//	// no longer sending to him
	//	g_sendingToHost0 = false;
	//	// if he sent a reply, don't bother him so much any more
	//	if ( ! g_errno ) g_host0Replied = true;
	//}
	// un-count it
	s_outstandingPings--;
	// don't let udp server free our send buf, we own it
	slot->m_sendBufAlloc = NULL;
	// update ping time
	int64_t nowms    = gettimeofdayInMillisecondsLocal();
	//int64_t now2     = gettimeofdayInMillisecondsLocal();
	int64_t tripTime = nowms - slot->m_firstSendTime ;
	// what port were we sending to?
	//uint16_t port = slot->m_port;
	// ensure not negative, clock might have been adjusted!
	if ( tripTime < 0 ) tripTime = 0;
	// get the Host ptr
	//Host *h = g_hostdb.getHost ( hid );
	// bail if none
	if ( ! h ) { log(LOG_LOGIC,"net: pingserver: bad hostId."); return; }
	// point to the right ping time, for the original port or for the 
	// shotgun port
	int32_t *pingPtr = NULL;
	if ( slot->m_ip == h->m_ipShotgun ) pingPtr = &h->m_pingShotgun;
	// original overrides shotgun, in case ips match
	if ( slot->m_ip == h->m_ip        ) pingPtr = &h->m_ping;
	// otherwise... wierd!!
	if ( ! pingPtr ) pingPtr = &h->m_ping;
	// debug msg
	//logf(LOG_DEBUG,"net: Got ping reply for hid=%"INT32" ip=%s "
	//"tripTime=%"INT64".", hid,iptoa(slot->m_ip),tripTime);
	// update ping times for this host
	//*pingPtr = tripTime;
	if ( g_errno == EUDPTIMEDOUT ) tripTime = g_conf.m_deadHostTimeout;
	updatePingTime ( h , pingPtr , tripTime );
	// sanity checks
	if ( slot->m_ip==h->m_ip && !h->m_inProgress1) {char *xx=NULL;*xx=0;}
	if ( slot->m_ip!=h->m_ip && !h->m_inProgress2) {char *xx=NULL;*xx=0;}
	// consider it out of progress
	if ( slot->m_ip == h->m_ip ) h->m_inProgress1 = false;
	else                         h->m_inProgress2 = false;
	// count all replies
	if ( ! g_errno ) h->m_numPingReplies++;

	// but if g_errno == EUDPTIMEDOUT, mark the host as dead
	if ( g_errno == EUDPTIMEDOUT ) {
		// if not first time...
		if ( ! h->m_wasAlive && 
		     // we must have been alive at some time
		     h->m_wasEverAlive &&
		     // and this is lefit
		     h->m_timeOfDeath != 0 &&
		     // and in our group
		     //h->m_groupId == g_hostdb.m_myHost->m_groupId ) {
		     h->m_shardNum == getMyShardNum() ) {
			// how long dead for?
			int64_t delta = nowms - h->m_timeOfDeath;
			// we did it once, do not repeat
			h->m_timeOfDeath = 0;
			// num collections
			int32_t nc = g_collectiondb.m_numRecs;
			// if 5 minutes, issue reload if in our group
			for ( int32_t i = 0 ; i < nc && delta > 2*60*1000 ; i++ ){
				// get coll
				SpiderColl *sc=g_spiderCache.getSpiderColl(i);
				// skip if empty
				if ( ! sc ) continue;
				// flag it
				sc->m_twinDied = true;
				// flag it for a reload since our twin died
				//sc->m_needsReload = true;
				// reload m_sniTable/m_cdTable/etc.
				//sc->m_doInitialScan = true;
			}
		}
		//*pingPtr = g_conf.m_deadHostTimeout;
		if ( h->m_wasAlive ) {
                        char *buf = "Host";
                        if(h->m_isProxy)
                                buf = "Proxy";
			log("net: %s #%"INT32" ip=%s is dead. Has not responded to "
			    "ping in %"INT32" ms.", buf, h->m_hostId,
			    iptoa(slot->m_ip),
			    (int32_t)g_conf.m_deadHostTimeout);
			// set dead time
			h->m_timeOfDeath = nowms;
			// send a kill -SEGV to the gb process on that
			// host to see why he froze up!
			// ALSO get a snapshot of the ps -aux on that machine
			/*
			char sbuf[1024];
			sprintf (sbuf,"ssh %s '"
				 "ps auxww > /tmp/psout & "
				 "killall -SEGV gb "
				 "'",iptoa(h->m_ip));
			log("net: system(\"%s\")",sbuf);
			static bool s_count = 0
			// only call system() once and only if we are host #0
			if (g_hostdb.m_myHost->m_hostId == 0 && s_count == 0)
				system ( sbuf );
			// count how many times we do it
			s_count++;
			*/
                }
		
	}

	// clear g_errno so we don't think any functions below set it
	g_errno = 0;

	// is the whole host alive? if either port is up it is still alive
	bool isAlive = false;
	if ( g_conf.m_useShotgun && 
	     h->m_pingShotgun < g_conf.m_deadHostTimeout ) isAlive = true;
	if ( h->m_ping < g_conf.m_deadHostTimeout        ) isAlive = true;
		
	// if host is not dead clear his sentEmail bit so if he goes
	// down again we may be obliged to send another email, provided
	// we are the hostid right after him
	if ( isAlive ) {
		// allow him to receive emails again
		h->m_emailCode = 0;
		// if he was the subject of the last alert we emailed and now
		// he is back up then we are free to send another alert about
		// any other host that goes down
		if ( h->m_hostId == s_lastSentHostId ) s_lastSentHostId = -1;
		// mark this
		h->m_wasEverAlive = true;
	}

	if ( isAlive && h->m_pingInfo.m_percentMemUsed >= 99.0 &&
	     h->m_firstOOMTime == 0 )
		h->m_firstOOMTime = nowms;
	if ( isAlive && h->m_pingInfo.m_percentMemUsed < 99.0 )
		h->m_firstOOMTime = 0LL;
	// if this host is alive and has been at 99% or more mem usage
	// for the last X minutes, and we have got at least 10 ping replies
	// from him, then send an email alert.
	if ( isAlive &&
	     h->m_pingInfo.m_percentMemUsed >= 99.0 &&
	     nowms - h->m_firstOOMTime >= g_conf.m_sendEmailTimeout )
		g_pingServer.sendEmail ( h , NULL , true , true );

	// . if his ping was dead, try to send an email alert to the admin 
	// . returns false if blocked, true otherwise
	// . sets g_errno on erro
	// . send it iff both ports (if using shotgun) are dead
	if ( ! isAlive && nowms - h->m_startTime >= g_conf.m_sendEmailTimeout) 
		g_pingServer.sendEmail ( h ) ;


	PingInfo *pi = &h->m_pingInfo;

	// if this host is alive but has some kernel error, then send an
	// email alert.
	if ( pi->m_kernelErrors && !h->m_kernelErrorReported ){
		log("net: Host #%"INT32" is reporting kernel errors.",
		    h->m_hostId);
		// MDW: disable for now, so does not wake us up at 3am
		//g_pingServer.sendEmail ( h, NULL, true, false, true );
		h->m_kernelErrorReported = true;
	}
	
	// reset if the machine has come back up
	if ( !pi->m_kernelErrors && h->m_kernelErrorReported )
		h->m_kernelErrorReported = false;

	// reset in case sendEmail() set it
	g_errno = 0;

	Host *myHost = g_hostdb.m_myHost;
	// . the one byte response has the sync status
	// . i think we cored on this when it wasn't checking to see if
	//   slot->m_readBuf was NULL. how can that happen? it seemed to have,
	//   when it tried to allocate one byte of mem and failed...
	if ( slot->m_readBufSize == 1 && slot->m_readBuf ) 
		h->m_syncStatus = *(slot->m_readBuf);
        /*
        else if( myHost && myHost->m_isProxy && hid == 0 &&
                 slot->m_readBufSize == 9 && slot->m_readBuf ) {
		h->m_syncStatus = *(slot->m_readBuf);
                if ( *pingPtr < 200 ) {
                        int64_t time = *(int64_t*)(slot->m_readBuf+1);
                        settimeofdayInMillisecondsGlobal(time);
                        //time_t tmt = time/1000;
                        //log("net: Proxy got time of %s", ctime(&tmt));
                }
        }
        */

	// debug msg
	//log("slot=%"INT32", g_errno=%"INT32" (%s) "
	//     "hostId %"INT32" has ping of %"INT32"",
	//     (int32_t)slot,g_errno,mstrerror(g_errno),hid,tripTime);
	// if we're host #0 and he responded really quickly, 
	// tell him to sync to our time
        //int32_t h0MachineNum = g_hostdb.getMachineNum(0);
	if (myHost && myHost->m_isProxy) return;
	if ( g_hostdb.m_hostId != 0 ) return;
	if ( *pingPtr > 200         ) return;
	// debug
	//log("PingServer:: uncomment me");
	// don't send sync request to same machine either, we share the clock
	//if((!h->m_isProxy && g_hostdb.getMachineNum(hid) == h0MachineNum) ||
	//   ( h->m_isProxy && h->m_ip                     == myHost->m_ip)  )
	//	return;
	// count this one too
	s_outstandingPings++;
	// record this time
	g_pingServer.m_currentPing = *pingPtr;

	// . ok, his ping was under half a second so he should sync with us
	// . he should recognize empty requests as a request to sync
	// . send reply back to the same ip/port that sent to us
        if ( h->m_isProxy ) hid = -1;

	// send back what his ping was so he knows
	//*(int32_t *)h->m_requestBuf = *pingPtr;
	//h->m_pingInfo.m_lastPing = *pingPtr;
	*(int32_t *)h->m_tmpBuf = *pingPtr;


	if ( g_udpServer.sendRequest (h->m_tmpBuf,//RequestBuf,
				      //h->m_requestBuf ,
				       4               , // 4 byte request
				       0x11          ,
				       slot->m_ip    , // h->m_ip       ,
				       slot->m_port  , // h->m_port2    ,
				       hid   ,
				       NULL          ,
				       (void *)(PTRTYPE)h->m_hostId, //cb state
				       gotReplyWrapperP3 ,
				       // timeout
				       (g_conf.m_deadHostTimeout/1000)+1 , 
				       1000          ,           // backoff
				       2000          ,  // max wait
				       NULL          ,  // reply buf
				       0             ,  // reply buf size
				       0             )) // niceness
		return;
	// he came back right away
	s_outstandingPings--;
	// had an error
	log("net: Got error sending time sync request: %s.", 
	    mstrerror(g_errno) );
	// reset it cuz it's not a showstopper
	g_errno = 0;

	// . come down here to launch another ping if we need to
	// . this is really only used when we first come up
	// launchMore:
	//g_pingServer.sendPingsToAll();
}

void gotReplyWrapperP3 ( void *state , UdpSlot *slot ) {
	// do not free this!
	slot->m_sendBufAlloc = NULL;
	// un-count it
	s_outstandingPings--;
	// continue to launch more if we need to
	//g_pingServer.sendPingsToAll();
}

// record time in the ping request iff from hostId #0
static int64_t s_deltaTime = 0;

// this may be called from a signal handler now...
void handleRequest11 ( UdpSlot *slot , int32_t niceness ) {
	// get request 
	//char *request     = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;
	char *request     = slot->m_readBuf;
	// get the ip/port of requester
	uint32_t ip    = slot->m_ip;
	uint16_t port = slot->m_port;
	// get the host entry
	Host *h = g_hostdb.getHost ( ip , port );
	// we may be the temporary cluster (grep for useTmpCluster) and
	// the proxy is sending pings from its original port plus 1
	if ( ! h ) h = g_hostdb.getHost ( ip , port + 1 );
	// debug
	//fprintf(stderr,"GOT PING insighandler=%"INT32" rsize=%"INT32" h=%"INT32"\n",
	//	(int32_t)g_inSigHandler,requestSize,(int32_t)h);
	if ( ! h ) {
		// size of 3 means it is a debug ping from
		// proxy server is a connectIp, so don't display the message
		// ./gb ./hosts.conf <hid> --udp
		if ( requestSize != 3 && ! g_conf.isConnectIp(ip) )
			log(LOG_LOGIC,"net: pingserver: No host for "
			    "dstip=%s port=%hu tid=%"INT32" fromhostid=%"INT32"",
			    iptoa(ip),port,slot->m_transId,slot->m_hostId);
		//static int32_t s_count = 0;
		//fprintf(stderr,"GOT %"INT32"\n",s_count++);
		//g_udpServer2.sendErrorReply ( slot , 1 );
		// set "useSameSwitch" to true so even if shotgunning is on
		// the udp server will send the reply back to the same ip/port
		// from which we got the request
		g_udpServer.sendReply_ass ( NULL , // msg
					     0    , // msgSize
					     NULL , // alloc
					     0    , // alloc Size
					     slot ,
					     60   , // timeout in seconds
					     NULL , // state
					     NULL , // callback
					     500  , // backoff in ms
					     1000 , // max wait for backoff
					     true );// use same switch?
		return;
	}


	// point to the correct ping time. this request may have come from
	// the shotgun network, or the primary network.
	int32_t *pingPtr = NULL;
	if ( slot->m_ip == h->m_ipShotgun ) pingPtr = &h->m_pingShotgun;
	// original overrides shotgun, in case ips match
	if ( slot->m_ip == h->m_ip        ) pingPtr = &h->m_ping;
	// otherwise... wierd!!
	if ( ! pingPtr ) pingPtr = &h->m_ping;
	//logf(LOG_DEBUG,"net: Got ping request from hid=%"INT32" ip=%s",
	//     h->m_hostId,iptoa(slot->m_ip));
	// . he's definitely not dead since he sent us a request
	// . NO! he might not be able to see our packets but we can see his!
	//   so i am commenting this out... this seemed to happen on the
	//   cluster and caused craziness! "TX Unit Hang" errors in the
	//   kernel ring buffer (dmesg)
	//if ( *pingPtr >= g_conf.m_deadHostTimeout ) {
	//	//*pingPtr = g_conf.m_deadHostTimeout - 1;
	//	updatePingTime ( h , pingPtr, g_conf.m_deadHostTimeout - 1 );
	//	//if ( *pingPtr < 0 ) *pingPtr = 0;
	//}
	// reply msg
	char *reply     = NULL;
	int32_t  replySize = 0;

	// . a request size of 10 means to set g_repairMode to 1
	// . it can only be advanced to 2 when we receive ping replies from
	//   everyone that they are not spidering or merging titledb...
	if ( requestSize == sizeof(PingInfo)){
		//MAX_PING_SIZE
		//14+4+4+4+4+sizeof(collnum_t)+1 ) {

		// sanity
		PingInfo *pi2 = (PingInfo *)request;
		if ( pi2->m_hostId != h->m_hostId ) { 
			char *xx=NULL;*xx=0; }

		// now we just copy the class
		gbmemcpy ( &h->m_pingInfo , request , requestSize );

		/*
		char* p = request + 10;
		// fetch load avg...
		h->m_loadAvg = ((double)(*((int32_t*)(p)))) / 100.0;
		p += sizeof(int32_t);
		// and mem used
		h->m_percentMemUsed = *(float *)(p);
		p += sizeof(float);
		// and cpu usage
		h->m_cpuUsage = *(float *)(p); // 4 bytes
		p += sizeof(float);

		// the host's global doc count.
		h->m_docsIndexed = *(int32_t*)(p);
		p += sizeof(int32_t);
		*/

		// how many we indexed since startup
		//h->m_urlsIndexed = *(int32_t*)(p);
		//p += sizeof(int32_t);

		// the host's event count.
		//h->m_eventsIndexed = *(int32_t*)(p);
		//p += sizeof(int32_t);

		/*
		// slow disk reads is important
		h->m_slowDiskReads = *(int32_t*)(p);
		p += sizeof(int32_t);

		h->m_hostsConfCRC = *(int32_t*)(p);
		p += sizeof(int32_t);
		// sanity
		if ( h->m_hostsConfCRC == 0 ) { char *xx=NULL;*xx=0; }

		// disk usage
		h->m_diskUsage = *(float *)p;
		p += sizeof(float);

		// put the state flags
		h->m_flags = *(int32_t *)(p);
		p += sizeof(int32_t);

		// the collnum it is daily merging
		h->m_dailyMergeCollnum = *(collnum_t *)(p);
		p += sizeof(collnum_t);
		
		// the 4 hd temps
		gbmemcpy ( h->m_hdtemps , p , 4 * 2 );
		p += 4 * 2;

		// at the end the gbverstionstrbuf
		int32_t vsize = getVersionSize(); // 21
		gbmemcpy ( h->m_gbVersionStrBuf , p , vsize );
		p += vsize;
		*/

		// if any one of them is overheating, then turn off
		// spiders on ourselves (and thus the full cluster)
		for ( int32_t k = 0 ; k < 4 ; k++ )
			if ( h->m_pingInfo.m_hdtemps[k] > 
			     g_conf.m_maxHardDriveTemp )
				g_conf.m_spideringEnabled = 0;
			

		// we finally got a ping reply from him
		h->m_gotPingReply = true;

		// should we enter daily merge mode? only if hostid #0 is in it
		//if ( (h->m_flags & PFLAG_MERGEMODENOT0) && 
		//     h->m_hostId == 0 &&
		//     g_hostdb.m_hostId != 0 &&
		//     g_dailyMerge.m_mergeMode == 0 ) 
		//	// advance into the daily merge mode
		//	g_dailyMerge.m_mergeMode = 1;

		// . what repair mode is the requester in?
		// . 0 means not in repair mode
		// . 1 means in repair mode waiting for a local spider or 
		//   titledb merge to stop
		// . 2 means in repair mode and local spiders and local titledb
		//   merging have stopped and are will not start up again
		// . 3 means all hosts have entered mode 2 so we can start
		//   repairing
		// . 4 means done repairing and ready to exit repair mode,
		//   but waiting for other hosts to be mode 4 or 0 before
		//   it can exit, too, and go back to mode 0
		/*
		char mode = request[8];
		h->m_kernelErrors = request[9];
		*/
		char mode = h->m_pingInfo.m_repairMode;

		//if ( h->m_kernelErrors ){
		//char *xx = NULL; *xx = 0;
		//}
		
		// set his repair mode in the hosts table
		//if ( h ) {
		// if his mode is 2 he is ready to start repairing
		// because he has stopped all spiders and titledb
		// merges from happening. if he just entered mode
		// 2 now, check to see if all hosts are in mode 2 now.
		char oldMode = h->m_repairMode;
		// update his repair mode
		h->m_repairMode = mode;
		// . get the MIN repair mode of all hosts
		// . expensive, so only do if a host changes modes
		if ( oldMode != mode                    || 
		     g_pingServer.m_minRepairMode == -1   )
			g_pingServer.setMinRepairMode ( h );
		//}
		// make it a normal ping now
		requestSize = 8;
	}

	PingServer *ps = &g_pingServer;
	ps->m_numHostsWithForeignRecs = 0;
	ps->m_numHostsDead = 0;
	ps->m_hostsConfInDisagreement = false;
	ps->m_hostsConfInAgreement = false;


	//
	// do all hosts have the same hosts.conf????
	//
	// if some hosts are dead then we will not set either flag.
	//
	// scan all grunts for agreement. do this line once per sec?
	//
	int32_t agree = 0;
	int32_t i; for ( i = 0 ; i < g_hostdb.getNumGrunts() ; i++ ) {
		Host *h = &g_hostdb.m_hosts[i];			

		if ( h->m_pingInfo.m_flags & PFLAG_FOREIGNRECS )
			ps->m_numHostsWithForeignRecs++;

		if ( g_hostdb.isDead ( h ) )
			ps->m_numHostsDead++;

		// skip if not received yet
		if ( ! h->m_pingInfo.m_hostsConfCRC ) continue;
		// badness?
		if ( h->m_pingInfo.m_hostsConfCRC != g_hostdb.m_crc ) {
			ps->m_hostsConfInDisagreement = true;
			break;
		}
		// count towards agreement
		agree++;
	}

	// iff all in agreement, set this flag
	if ( agree == g_hostdb.getNumGrunts() )
		ps->m_hostsConfInAgreement = true;
			




	// if request is 5 bytes, must be a host telling us he's shutting down
	if ( requestSize == 5 ) {
		// his byte #4 is sendEmailAlert
		bool sendEmailAlert = request[4];
		// and make him dead
		if ( h ) {
			//h->m_ping        = g_conf.m_deadHostTimeout + 1;
			//h->m_pingShotgun = g_conf.m_deadHostTimeout + 1;
			int32_t deadTime = g_conf.m_deadHostTimeout + 1;
			updatePingTime ( h , &h->m_ping        , deadTime );
			updatePingTime ( h , &h->m_pingShotgun , deadTime );
			// don't email admin if sendEmailAlert is false
			if ( ! sendEmailAlert ) h->m_emailCode = -2;
			// otherwise, launch him one now
			else {              
				//h->m_emailCode =  0;
				// . this returns false if blocked, true otherw
				// . sets g_errno on erro
				g_pingServer.sendEmail ( h );
				// reset in case sendEmail() set it
				g_errno = 0;
			}
			// if we are a proxy and he had an outstanding 0xfd msg
			// then we need to re-route that! likewise any msg
			// that has not got a full reply needs to be timedout
			// right now so we can re-route it...
			g_udpServer.timeoutDeadHosts ( h );
		}
		else
			log(LOG_LOGIC,"net: pingserver: Got NULL host ptr.");

	}
	// . if size is 4 then he wants us to sync with him
	// . this was "0", but now we include what the ping was
	else if ( requestSize == 4 ) {
		// get the ping time
		int32_t ping = *(int32_t *)request;
		// store it
		g_pingServer.m_currentPing = ping;
		// should we update the clock?
		bool setClock = true;
		// . add 1ms DRIFT for every hour since last update
		// . use local clock time only
		int32_t nowLocal = getTime();
		// how many seconds since we last updated our clock?
		int32_t delta    = nowLocal - g_pingServer.m_bestPingDate;
		// drift it 1ms every 5 seconds, that seems somewhat typical
		int32_t drift    = delta / 5;
		// get best "drifted" ping, "dping"
		int32_t dping = g_pingServer.m_bestPing + drift;
		// no overflowing
		if ( dping < g_pingServer.m_bestPing ) dping = 0x7fffffff;
		// if this is our first time
		if ( g_pingServer.m_bestPingDate == 0 ) dping = 0x7fffffff;
		// . don't bother if not more accurate
		// . update the clock on "ping" ties because our local clock
		//   drifts a lot
		if ( g_pingServer.m_currentPing > dping )
			setClock = false;
		// ping must be < 200 ms to update
		if ( g_pingServer.m_currentPing > 200 ) setClock = false;
		// if no host... how can this happen?
		if ( ! h ) setClock = false;
		// only update if from host #0
		if ( h && h->m_hostId != 0 ) setClock = false;
		// proxy can be host #0, too! watch out...
		if ( h && h->m_isProxy ) setClock = false;
		// only commit sender's time if from hostId #0
		if ( setClock ) {
			// what time is it now?
			int64_t nowmsLocal=gettimeofdayInMillisecondsLocal();
			// log it
			log(LOG_DEBUG,"admin: Got ping of %"INT32" ms. Updating "
			     "clock. drift=%"INT32" delta=%"INT32" s_deltaTime=%"INT64"ms "
			     "nowmsLocal=%"INT64"ms",
			     (int32_t)g_pingServer.m_currentPing,drift,delta,
			     s_deltaTime,nowmsLocal);
			// what should the new time be? (local mobo time)
			int64_t newTime = s_deltaTime + nowmsLocal;
			// update clock
			settimeofdayInMillisecondsGlobal ( newTime );
			// time stamps
			g_pingServer.m_bestPingDate = nowLocal;
			// and the ping
			g_pingServer.m_bestPing = g_pingServer.m_currentPing;
			// clear this
			//s_deltaTime = 0;
		}
	}
	// all pings now deliver a timestamp of the sending host
	else if ( requestSize == 8 ) {
		//reply = g_pingServer.getReplyBuffer();
		// only record sender's time if from hostId #0
		if ( h && h->m_hostId == 0 && !h->m_isProxy) {
			// what time is it now?
			int64_t nowmsLocal=gettimeofdayInMillisecondsLocal();
			// . seems these servers drift by 1 ms every 5 secs
			// . or that is about 17 seconds a day
			// . we do NOT know how accurate host #0's supplied
			//   time is because the request may have been delayed
			log(LOG_DEBUG,"admin: host #0 time is %"INT64" ms and "
			    "our local time is %"INT64" ms, delta=%"INT64" ms",
			    *(int64_t *)request,nowmsLocal ,
			    *(int64_t *)request - nowmsLocal );
			// update s_delta in case host #0 sends us a 
			// request size of 4, telling us to sync up with this
			s_deltaTime =*(int64_t *)request - nowmsLocal;
		}
                // Reply to the proxy with a time stamp to sync with
                /*
                else if ( h && h->m_isProxy && g_hostdb.m_hostId == 0 &&
                          g_conf.m_timeSyncProxy ) {
                        int64_t time = gettimeofdayInMillisecondsLocal();
                        // skip the syncstatus byte and write the time
                        *(int64_t *)(reply+1) = time;
                        replySize = sizeof(int64_t);
                }
                */
		// send back a 1 byte msg with our sync status now
		//Host *me  = g_hostdb.getHost ( g_hostdb.m_hostId );
		//*(char*)reply = me->m_syncStatus;
		//replySize++;
		reply     = NULL;
		replySize = 0;
		// debug -- don't ack any pings for now
		//g_udpServer2.destroySlot ( slot );
		//return;
	}
	// a tap request -- to store the sync point in the sync file
	//else if ( requestSize == 9 )
	//	g_sync.addOp ( OP_SYNCPT , "" , *(int64_t *)request );
	// otherwise, unknown request size
	else {
		log(LOG_LOGIC,"net: pingserver: Unknown request size of "
		    "%"INT32" bytes. You are probably running a different gb "
		    "version on this host. check the hosts table for "
		    "version info.", requestSize);
	}
	// always send back an empty reply
	g_udpServer.sendReply_ass ( reply     , // msg
				     replySize , // msgSize
				     NULL , // alloc
				     0    , // alloc Size
				     slot ,
				     60   , // timeout in seconds
				     NULL , // state
				     NULL , // callback
				     500  , // backoff in ms
				     1000 , // max wait for backoff
				     true );// use same switch?


	// . now in PingServer.cpp for hostid 0 it checks
	//   the urlsindexed from each host if g_conf.m_testParserEnabled
	//   is true to see if we should call g_test.stopIt()
	// . add up each hosts urls indexed
	if ( ! g_conf.m_testParserEnabled     ) return;
	if ( g_hostdb.m_myHost->m_hostId != 0 ) return;
	int32_t total = 0;
	for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
		Host *h = &g_hostdb.m_hosts[i];
		total += h->m_urlsIndexed;
	}
	// all done?
	if ( total >= g_test.m_urlsAdded ) g_test.stopIt();

	return;
}

// . sets m_minRepairMode
// . only call this when host "h" changes repair mode
void PingServer::setMinRepairMode ( Host *hhh ) {
	// . this host is the holder of the min now, return if not a match
	// . do not return if m_minRepairMode has not been set at all though
	bool returnNow = true;
	if ( m_minRepairModeHost         == hhh ) returnNow = false;
	if ( m_maxRepairModeHost         == hhh ) returnNow = false;
	if ( m_minRepairModeBesides0Host == hhh ) returnNow = false;
	if ( m_minRepairMode             ==  -1 ) returnNow = false;
	if ( m_maxRepairMode             ==  -1 ) returnNow = false;
	if ( m_minRepairModeBesides0     ==  -1 ) returnNow = false;
	if ( returnNow ) return;
	// count the mins
	int32_t  min   = -1;
	int32_t  max   = -1;
	int32_t  min0  = -1;
	Host *minh  = NULL;
	Host *maxh  = NULL;
	Host *minh0 = NULL;
	// scan to find new min
	for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
		// count if not dead
		Host *h = &g_hostdb.m_hosts[i];
		// . if it is us, the repairMode is not updated because we
		//   do not ping ourselves.
		// . we check ourselves in the the getMinRepairMode() and
		//   getNumHostsInRepairMode7() functions defined in 
		//   PingServer.h
		if ( h == g_hostdb.m_myHost ) continue;
		// get repair mode
		int32_t repairMode = h->m_repairMode;
		// is it a min?
		if ( repairMode < min || min == -1 ) {
			// we got a new minner
			min  = repairMode;
			minh = h;
		}
		// is it a max?
		if ( repairMode > max || max == -1 ) {
			// we got a new minner
			max  = repairMode;
			maxh = h;
		}
		// . min0 is the lowest repair mode that is not 0
		// . if they are all 0, then it will be 0
		if ( repairMode == 0 ) continue;
		if ( repairMode < min0 || min0 == -1 ) {
			min0  = repairMode;
			minh0 = h;
		}
	}
	// set these guys to the min
	m_minRepairMode         = min;
	m_minRepairModeHost     = minh;
	// and these to max
	m_maxRepairMode         = max;
	m_maxRepairModeHost     = maxh;
	// if they are all 0 then this will be 0
	m_minRepairModeBesides0     = min0;
	m_minRepairModeBesides0Host = minh0;
}

//
// This code is used to cyclically ping all hosts in the network
//

// this wrapper is called once 100ms, or so...
void sleepWrapper ( int fd , void *state ) {
	// . do not do anything if still doing initial pinging above
	// . in fact, launch as many pings as we can right now
	//if ( ! g_host0Replied ||
	//     g_pingServer.m_totalLaunched < 
        //     (g_hostdb.getNumHosts() + g_hostdb.getNumProxy()) ) {
	//	g_pingServer.sendPingsToAll();
	//	return;
	//}
	g_pingServer.sendPingsToAll();//pingNextHost ( );
}


/*
void PingServer::pingNextHost ( ) {

 skip:
	// . don't use more than 32       UdpSlots for pinging
	// . don't use more than numHosts UdpSlots for pinging
	int32_t n = g_hostdb.getNumHosts();
        if ( m_pingProxy )
                n = g_hostdb.getNumProxy();
        int32_t max = n;
	if ( n > 32 ) max = 32;

	if ( s_outstandingPings >= max ) return;
	// the next hostid to ping
	static int32_t s_nextHostId = 0;
	// cycle through pinging different hosts
 	// get next host to ping
	if ( s_nextHostId >= n ) {
		s_nextHostId = 0;
		// toggle shotgun if we should
		if ( g_conf.m_useShotgun ) {
			if ( m_useShotgun ) {
                                m_useShotgun = 0;
                                if(g_hostdb.getNumProxy())
                                        m_pingProxy  ^= 1;
                        }
			else {
                                m_useShotgun = 1;
                        }
		}
                else {
                        if(g_hostdb.getNumProxy())
                                m_pingProxy ^= 1;
                }
	}
	// always turn off if we should
	if ( ! g_conf.m_useShotgun ) m_useShotgun = 0;
	// ping it
        Host *h = g_hostdb.getHost(s_nextHostId);
        if ( m_pingProxy )
                h = g_hostdb.getProxy(s_nextHostId);
	pingHost ( h, m_useShotgun );
	// inc next host to ping
	s_nextHostId++;
}
*/

//////////////////////////////////////////////////////
// email sending code
//////////////////////////////////////////////////////

// we can send emails when a host is detected as dead

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool PingServer::sendEmail ( Host *h            , 
			     char *errmsg       , 
			     bool  sendToAdmin  ,
			     bool  oom          , 
			     bool  kernelErrors , 
			     bool  parmChanged  ,
			     bool  forceIt      ,
			     int32_t  mxIP         ) { // 0 means none
	// clear this
	g_errno = 0;
	// not if we have outstanding requests
	if ( m_numReplies2 < m_numRequests2 ) {
		log("net: Email not sent since there are %"INT32" outstanding "
		    "replies.",m_numReplies2 - m_numRequests2);
		return true;
	}
	// throttle the oom sends
	if ( oom ) {
		static int32_t s_lastOOMTime = 0;
		int32_t now = getTimeLocal();
		// space 15 minutes apart
		if ( now - s_lastOOMTime < 15*60 ) return true;
		// set time
		s_lastOOMTime = now;
	}
	// always force these now because they are messing up our latency graph
	if ( oom ) forceIt = true;
	// . even if we don't send an email, log it
	// . return if alerts disabled
	if ( ! g_conf.m_sendEmailAlerts && ! forceIt ) {
		// . only log if this is his first time as being detected dead
		// . this is useful cuz it might hint at a down link
		if ( h != NULL && h->m_emailCode == 0 ) {
			h->m_emailCode = 1;
			//log("net: Host #%"INT32" is dead. Has not responded to "
			//    "ping in %"INT32" ms.",h->m_hostId,
			//    (int32_t)g_conf.m_deadHostTimeout);
		}
		return true;
	}
	// bitch and bail if h is NULL and this is a dead host msg
	if ( ! h && errmsg == NULL) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"net: pingserver: Host ptr is NULL 2.");
		return true;
	}
	// if he's not open for alerts,return (see PingServer.h) for alert defn
	if (  h && h->m_emailCode != 0 ) return true;

	// don't send another email until the last us we alerted revives
	if ( h && s_lastSentHostId >= 0 ) {
		// in fact, even after the lastSentHostId host comes back
		// up, don't send to these guys who were dead before it came
		// back up
		h->m_emailCode = -5;
		return true;
	}

	char msgbuf[2048];
	if( h ) { //as a special case construct the error message here if 
		//  we have a host.
		// . are we the designated host to send the email alert?
		// . our hostid must be the next alive hostId
		int32_t dhid = h->m_hostId;
		Host *dh = g_hostdb.getHost ( dhid );
		Host *origdh = dh;
		//while ( dh && dh->m_ping >= g_conf.m_deadHostTimeout ) {
		int32_t totalCount = 0;
		while ( dh && ( g_hostdb.isDead ( dh ) || dh == origdh ) ) {
			if ( ++dhid >= g_hostdb.getNumHosts() ) dhid = 0;
			dh = g_hostdb.getHost ( dhid );
			if ( totalCount++ >= g_hostdb.getNumHosts() ) break;
		}
		// . if we're not the next alive host in line to send, bail
		// . if next-in-line crashes before he sends then there will be
		//   a cascade affect and we could end up sending a BUNCH of 
		//   emails so prevent that now by setting his m_emailCode
		if ( dhid != g_hostdb.m_hostId ) { 
			h->m_emailCode = -3; 
			return true; 
		}

		// mark him as in progress
		h->m_emailCode = -4;
		// a host or proxy?
		char *nm = "Host";
		if ( h->m_isProxy ) nm = "Proxy";
		// note it in the log
		if ( oom ) 
			log("net: %s %s #%"INT32" is out of mem for %"INT32" ms. "
			    "Sending email alert.",h->m_hostname,nm,
			    h->m_hostId,
			    (int32_t)g_conf.m_sendEmailTimeout);
		else if ( kernelErrors )
			log("net: %s %s #%"INT32" has an error in the kernel. "
			    "Sending email alert.",h->m_hostname,nm,
			    h->m_hostId);
		else
			log("net: %s %s #%"INT32" is dead. Has not responded to "
			    "ping in %"INT32" ms. Sending email alert.",
			    h->m_hostname,nm,h->m_hostId,
			    (int32_t)g_conf.m_sendEmailTimeout);
		// . make the msg
		// . put host0 ip in ()'s so we know what network it was
		Host *h0 = g_hostdb.getHost ( 0 );
		int32_t ip0 = 0;
		if ( h0 ) ip0 = h0->m_ip;
		char *desc = "dead";
		if ( oom ) desc = "out of memory";
		else if ( kernelErrors ) desc = "having kernel errors";
		sprintf ( msgbuf , "%s %s %"INT32" is %s. cluster=%s (%s)",  
			  h->m_hostname,nm,
			  h->m_hostId, desc, g_conf.m_clusterName,iptoa(ip0));
		errmsg = msgbuf;
	} 

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool status = true;
	m_numRequests2 = 0;
	m_numReplies2  = 0;

	// sysadmin
	if ( g_conf.m_sendEmailAlertsToSysadmin && sendToAdmin ) {
		m_numRequests2++;
		if ( ! sendAdminEmail ( h,
					"sysadmin@gigablast.com",
					"sysadmin@gigablast.com",
					errmsg ,
					"mail.gigablast.com" ) )
			status = false;
	}

	// set the max for sanity checking in gotdoc
	m_maxRequests2 = m_numRequests2;

	bool delay = g_conf.m_delayNonCriticalEmailAlerts;
	// oom is always critical, as is kernel errors
	if ( oom || kernelErrors ) delay = false;

	// if delay non critical email alerts is true do not send email 
	// alerts about dead hosts to anyone except sysadmin@gigablast.com 
	// between 10:00pm and 9:30am unless all the other twins of the 
	// dead host are also dead. Instead, wait till after 9:30 am if 
	// the host is still dead.
	if ( delay && h && sendToAdmin ) {

		/*
		int32_t deaHr,deaMin,debHr,debMin;
		char hr[3],min[3];
		hr[2] = '\0';
		min[2] = '\0';
		gbmemcpy ( hr, g_conf.m_delayEmailsAfter, 2 );
		deaHr = atoi(hr);
		gbmemcpy ( min, g_conf.m_delayEmailsAfter + 3, 2 );
		deaMin = atoi(min);
		gbmemcpy ( hr, g_conf.m_delayEmailsBefore, 2 );
		debHr = atoi(hr);
		gbmemcpy ( min, g_conf.m_delayEmailsBefore + 3, 2 );
		debMin = atoi(min);
		//get the current time. use getTime() because
		//then it is sync'ed with host 0's time.
		time_t rawTime = getTime();
		struct tm *timeInfo;
		//timeInfo stores the lasthour time
		timeInfo = localtime ( &rawTime );
		char buf[64];
		strftime ( buf , 100 , "%b %d %T", timeInfo);
		//log ( LOG_WARN,"net: pingserver: local time is %s when "
		//    "host died",buf );
		//tm struct has hours from 0-23

		int32_t tmHr = timeInfo->tm_hour;
		int32_t tmMin = timeInfo->tm_min;
		
		bool delay = false;
		
		if ( ( tmHr > deaHr || ( tmHr == deaHr && tmMin > deaMin ) ) &&
		     ( tmHr < debHr || ( tmHr == debHr && tmMin < debMin ) ) )
			delay = true;
		
		else if ( ( tmHr > deaHr || 
			    ( tmHr == deaHr && tmMin > deaMin ) ) &&
			  ( debHr < deaHr || 
			    ( debHr == deaHr && debMin < deaMin ) ) )
			delay = true;

		else if ( ( tmHr < debHr || 
			    ( tmHr == debHr && tmMin < debMin ) ) &&
			  ( deaHr > debHr || 
			    ( deaHr == debHr && deaMin > debMin ) ) )
			delay = true;
		*/

		// always delay no matter the time now
		bool delay = true;

		if ( delay ) {

			//check if the hosts twins are dead too
			int32_t numTwins = 0;
			Host *hosts = g_hostdb.getShard( h->m_shardNum, 
							 &numTwins );
			int32_t i = 0;
			while ( i < numTwins ){
				if ( !g_hostdb.isDead ( hosts[i].m_hostId ) )
					break;
				i++;
			}

			//if no twin is alive, emergency ! send email !
			//if even one twin is alive, don't send now
			if ( i == numTwins ) goto skipSleep;
			/*
			time_t rawTime2 = getTime();
			struct tm *timeInfo2;
			//timeInfo stores the lasthour UTC time
			timeInfo2=localtime(&rawTime2);
			//change the time to delayemailsBefore
			timeInfo2->tm_hour = debHr;
			timeInfo2->tm_min = debMin;
			rawTime2 = mktime ( timeInfo2 );

			double diffTime = difftime ( rawTime2 , rawTime );

			// if difftime is neg, that means we were yesterday,
			// so add 1 day to get real diff
			if (diffTime < 0 )
				diffTime = (60 * 60 * 24 ) + diffTime;

			log( LOG_WARN,"net: pingserver: Delay non critical "
			     "email alerts on. Trying to send email after %"INT32" "
			     "seconds",(int32_t) diffTime );

			int32_t wait = (int32_t)diffTime * 1000 ;//ms
			// wake up after so many seconds
			g_loop.registerSleepCallback( wait, h,
						      emailSleepWrapper );
			*/
			return true;
		}
	}

 skipSleep:
	// matt tmobile
	//if ( g_conf.m_sendEmailAlertsToMattTmobile ) {
	//	m_numRequests2++; 
	//	if ( ! pageTMobile   ( h, errmsg)) 
	//	       status = false;
	//}
	// matt alltel
	/*
	if ( g_conf.m_sendEmailAlertsToMattAlltell ) {
		m_numRequests2++;
		if ( ! sendAdminEmail ( h,
					"sysadmin@gigablast.com",
					"5054503518@message.alltel.com",
					errmsg ,
					"mms-mta.alltel.net" ))
			status = false;
	}
	*/
	// matt alltel
// 	if ( g_conf.m_sendEmailAlertsToMattAlltell ) {
// 		m_numRequests2++; 
// 		//if ( ! pageAlltel    ( h, errmsg , "5053626809" ) ) 
// 		if ( ! pageAlltel    ( h, errmsg , "5054503518" ) ) 
// 		       status = false;
// 	}
	// matt verizon
	//if ( g_conf.m_sendEmailAlertsToMattVerizon ) {
	//	m_numRequests2++; 
	//	if ( ! pageVerizon   ( h, errmsg ) ) 
	//	       status = false;
	//}



	// melissa
// 	if ( g_conf.m_sendEmailAlertsToMelissa ) {
// 		m_numRequests2++; 
// 		if ( ! pageAlltel    ( h, errmsg , "5054292121" ) ) // melissa
// 			status = false;
// 	}

	// partap
	/*
	if ( g_conf.m_sendEmailAlertsToPartap ) {
		m_numRequests2++; 
		//if ( ! pageSprintPCS ( h, errmsg , "5056889554") ) 
		//	status = false;
		if ( ! sendAdminEmail ( h,
					"sysadmin@gigablast.com",
				       "5056889554@mx.messaging.sprintpcs.com",
					errmsg ,
					"mx.messaging.sprintpcs.com") )
			status = false;
	}

	// javier
	if ( g_conf.m_sendEmailAlertsToJavier ) {
		m_numRequests2++; 
		//if ( ! pageSprintPCS ( h, errmsg , "5056959513") ) 
		//	status = false;
		if ( ! sendAdminEmail ( h,
					"sysadmin@gigablast.com",
				       "5056959513@mx.messaging.sprintpcs.com",
					errmsg ,
					"mx.messaging.sprintpcs.com") )
			status = false;
	}

	// cinco
// 	if ( g_conf.m_sendEmailAlertsToCinco ) {
// 		m_numRequests2++; 
// 		//if ( ! pageSprintPCS ( h, errmsg , "5054179933") ) 
// 		//	status = false;
// 		if ( ! sendAdminEmail ( h,
// 					"sysadmin@gigablast.com",
// 					"5054179933@mx.messaging.sprintpcs.com",
// 					errmsg ) )
// 			status = false;
// 	}

	// zak
	if ( g_conf.m_sendEmailAlertsToZak) {
		m_numRequests2++;
		if ( ! sendAdminEmail ( h,
					"zak@gigablast.com",
					"zak@gigablast.com",
					errmsg ,
					"mail.gigablast.com" ) )
			status = false;
		m_numRequests2++;
		if ( ! sendAdminEmail ( h,
					"sysadmin@gigablast.com",
				       	"5052204556@message.alltel.com",
					errmsg ,
					"mms-mta.alltel.net" ))
			status = false;
	}
	if ( g_conf.m_sendEmailAlertsToSabino ) {
		m_numRequests2++; 
		//if ( ! pageSprintPCS ( h, errmsg , "5056959513") ) 
		//	status = false;
		if ( ! sendAdminEmail ( h,
					"sysadmin@gigablast.com",
					"7082076550@mobile.mycingular.com",
					errmsg ,
					"mx.mycingular.net" ) )
			status = false;
	}
	*/

	bool e1 = g_conf.m_sendEmailAlertsToEmail1;
	bool e2 = g_conf.m_sendEmailAlertsToEmail2;
	bool e3 = g_conf.m_sendEmailAlertsToEmail3;
	bool e4 = g_conf.m_sendEmailAlertsToEmail4;

	// some people don't want parm change alerts
	if ( parmChanged && ! g_conf.m_sendParmChangeAlertsToEmail1) e1=false; 
	if ( parmChanged && ! g_conf.m_sendParmChangeAlertsToEmail2) e2=false; 
	if ( parmChanged && ! g_conf.m_sendParmChangeAlertsToEmail3) e3=false; 
	if ( parmChanged && ! g_conf.m_sendParmChangeAlertsToEmail4) e4=false; 

	// point to provided IP as string
	char *mxIPStr = NULL;
	char  ipBuf[64];
	if ( mxIP ) {
		sprintf(ipBuf,"%s",iptoa(mxIP));
		mxIPStr = ipBuf;
	}

	if ( e1 ) {
		m_numRequests2++;
		m_maxRequests2++;
		char *mxHost = g_conf.m_email1MX;
		if ( mxIP ) mxHost = mxIPStr;
		if ( ! sendAdminEmail ( h,
					g_conf.m_email1From,
					g_conf.m_email1Addr,
					errmsg,
					mxHost ) ) // g_conf.m_email1MX) )
			status = false;
	}
	if ( e2 ) {
		m_numRequests2++;
		m_maxRequests2++;
		char *mxHost = g_conf.m_email2MX;
		if ( mxIP ) mxHost = mxIPStr;
		if ( ! sendAdminEmail ( h,
					g_conf.m_email2From,
					g_conf.m_email2Addr,
					errmsg,
					mxHost ) ) // g_conf.m_email2MX) )
			status = false;
	}
	if ( e3 ) {
		m_numRequests2++;
		m_maxRequests2++;
		char *mxHost = g_conf.m_email3MX;
		if ( mxIP ) mxHost = mxIPStr;
		if ( ! sendAdminEmail ( h,
					g_conf.m_email3From,
					g_conf.m_email3Addr,
					errmsg,
					mxHost ) ) // g_conf.m_email3MX) )
			status = false;
	}
	if ( e4 ) {
		m_numRequests2++;
		m_maxRequests2++;
		char *mxHost = g_conf.m_email4MX;
		if ( mxIP ) mxHost = mxIPStr;
		if ( ! sendAdminEmail ( h,
					g_conf.m_email4From,
					g_conf.m_email4Addr,
					errmsg,
					mxHost ) ) // g_conf.m_email4MX) )
			status = false;
	}

	// set the max for sanity checking below
	//m_maxRequests2 = m_numRequests2;
	// did we block or not? return true if nobody blocked
	return status;
}

void emailSleepWrapper ( int fd, void *state ){
	g_loop.unregisterSleepCallback( state, emailSleepWrapper );
	//check if the host is still dead and if the last host to die is this
	//host
	Host *h = (Host *)state;
	//if he is still dead and no other host has died after it
	if ( g_hostdb.isDead( h ) && s_lastSentHostId == h->m_hostId ){
		//reset s_lastSentHostId for sendEmail()
		s_lastSentHostId = -1;
		//reset hosts emailcode so that it sends the email
		h->m_emailCode = 0;
		g_pingServer.sendEmail ( h , NULL , false );//sendToAdmin
	}
	//if we're alive then no need to send email. If some other host has
	//died, that host will send an email anyway.
}


#include "HttpServer.h"
static void gotDocWrapper ( void *state , TcpSocket *ts ) ;

// JAB: warning abatement
#if 0
// . returns false if blocks, true otherwise
// . sets g_errno on error
bool pageTMobile ( Host *h , char *errmsg) {
	// 	log("EMAIL SENT %s!!!!!!!!!!!!!!!",errmsg);
	// 	return true;
	// for debug without actually wasting our cell phone instant messages
	//log("EMAIL SENT hid=%"INT32"!!!!!!!!!!!!!!!",h->m_hostId);
	//gotDocWrapper ( h , NULL );
	//return true;
	// looks like TcpServer won't let us use a static sendBuf
	// cuz we cannot just set TcpSocket->m_sendBuf to NULL to stop
 // the freeing cuz if TcpServer::gotIp() has an error it
	// frees it sendBuf. This happens in other places too.
	void * state = h;
	char *buf = (char *) mmalloc ( PAGER_BUF_SIZE , "PingServer" );

	// bail on malloc error
	if ( ! buf ) {
		log("net: Could not allocate %"INT32" bytes to send email "
		    "to mobile." , (int32_t)PAGER_BUF_SIZE);
		g_pingServer.m_numReplies++;
		return true;
	}
	char *p    = buf;
	char *pend = buf + PAGER_BUF_SIZE;
	// send the request
	sprintf ( p , 
		  //"POST /mytmobile/login/default.asp?"
		  //"POST /messaging/default.asp?"
		  "POST /customer_site/jsp/messaging_lo.jsp?"
		  "To=5054503518 HTTP/1.0\r\n"
		  "Accept: image/gif, image/x-xbitmap, image/jpeg, "
		  "image/pjpeg, application/x-shockwave-flash, "
		  "application/msword, */*\r\n"
		  "Accept-Language: en-us\r\n"
		  "Content-Type: application/x-www-form-urlencoded\r\n"
		  "Accept-Encoding: gzip, deflate\r\n"
		  "User-Agent: Mozilla/4.0 "
		  "(compatible; MSIE 6.0; Windows 98; Win 9x 4.90)\r\n"
		  //"Host: www.t-mobile.com\r\n"
		  "Host: wmg.tmomail.net\r\n"
		  "Content-Length: xxx\r\n"
		  //"Connection: Keep-Alive\r\n"
		  "Connection: close\r\n"
		  "Cache-Control: no-cache\r\n\r\n"
		  //"txtNum=5054503518&txtFrom=gb&"
		  "DOMAIN_NAME=@tmomail.com&"
		  "min=5054503518&"
		  "require_sender=gb&"
		  //"hdnpublic=1&"
		  "msgTermsUse=on&"
		  //"txtMessage=");
		  //"trackResponses=No&"
		  //"Send.x=Yes&"
		  "text=");
	p += gbstrlen ( p );
	// append the err msg, but convert spaces to +'s
	int32_t i;
	for ( i = 0 ; errmsg[i] && p + 4 < pend ; i++ ) {
		if      ( errmsg[i] == ' ' ) 
			*p++ = '+';
		else if ( errmsg[i] == '.' ) { 
			p[0] = '%';
			p[1] = '2';
			p[2] = 'E';
			p += 3;
		}
		else    
			*p++ = errmsg[i];
	}
	*p = '\0';
	int32_t bufLen = p - buf;
	// replace xxx
	char *s = strstr ( buf , "xxx" );
	char *t = strstr ( buf , "\r\n\r\n" ) + 4;
	int32_t clen = bufLen - ( t - buf );
	sprintf ( s , "%3li" , clen );
	s [ 3 ] = '\r';

	// post it up
	// borrow our HttpServer's TcpServer
	//TcpServer *ts = g_httpServer.getTcp();
	TcpServer *ts = g_httpServer.getSSLTcp();
	if ( ! ts ) {
		log("db: Could not page alltel because "
		    "no gb.pem file in working dir so "
		    "SSL server is unavailable.");
		return true;
	}
	if ( ! ts->sendMsg ( //"65.161.188.152" , 
			    //gbstrlen("65.161.188.152") ,
			    "63.122.5.193",
			    gbstrlen("63.122.5.193"),
			    443 , // https, 80            ,
			     buf           ,
			     PAGER_BUF_SIZE        ,
			     bufLen        ,
			     bufLen        ,
			     (void *)h     ,
			     gotDocWrapper ,
			     60*1000       ,
			     100*1024      ,  // maxTextDocLen
			     100*1024      )) // maxOtherDocLen
		return false;
	// we did not block, so update h->m_emailCode
	gotDocWrapper (state, NULL );
	// we did not block
	return true;
}
#endif

// JAB: warning abatement
#if 0
// . returns false if blocks, true otherwise
// . sets g_errno on error
bool pageAlltel ( Host *h , char *errmsg , char *num ) {

	// for debug without actually wasting our cell phone instant messages
	//log("EMAIL SENT hid=%"INT32"!!!!!!!!!!!!!!!",h->m_hostId);
	//gotDocWrapper ( h , NULL );
	//return true;
	// looks like TcpServer won't let us use a static sendBuf
	// cuz we cannot just set TcpSocket->m_sendBuf to NULL to stop
	// the freeing cuz if TcpServer::gotIp() has an error it
	// frees it sendBuf. This happens in other places too.
	void * state = h;
	char *buf = (char *) mmalloc ( PAGER_BUF_SIZE , "PingServer" );
	// bail on malloc error
	if ( ! buf ) {
		log("net: Could not allocate %"INT32" bytes to send email "
		    "to alltel." , PAGER_BUF_SIZE);
		g_pingServer.m_numReplies++;
		return true;
	}
	char *p    = buf;
	char *pend = buf + PAGER_BUF_SIZE;
	// send the request
	char *host = "message.alltel.com";
	char *ip   = "166.102.172.2"; // message.alltel.com
	//char *num  = "5054292121"; // melissa

	sprintf ( p , 
		  //"POST /mytmobile/login/default.asp?"
		  "POST /customer_site/jsp/messaging.jsp"
		  " HTTP/1.0\r\n"
		  "Accept: image/gif, image/x-xbitmap, image/jpeg, "
		  "image/pjpeg, application/x-shockwave-flash, "
		  "application/msword, */*\r\n"
		  "Accept-Language: en-us\r\n"
		  "Content-Type: application/x-www-form-urlencoded\r\n"
		  "Accept-Encoding: gzip, deflate\r\n"
		  "User-Agent: Mozilla/4.0 "
		  "(compatible; MSIE 6.0; Windows 98; Win 9x 4.90)\r\n"
		  "Host: %s\r\n" // www.t-mobile.com
		  "Content-Length: xxx\r\n"
		  //"Connection: Keep-Alive\r\n"
		  "Connection: close\r\n"
		  "Cache-Control: no-cache\r\n\r\n"
		  // post data
		  "trackResponses=No&"
		  "devicetype=1&"
		  "DOMAIN_NAME=@vtext.com&"
		  "sender=gb&"
		  "min=%s&" // phone number
		  "callback=&"
		  "count=160&"
		  "type=1&" // 1=high priority, 0=normal
		  "text=" ,
		  host , num );
	p += gbstrlen ( p );
	// append the err msg, but convert spaces to +'s
	int32_t i;
	for ( i = 0 ; errmsg[i] && p + 4 < pend ; i++ ) {
		if      ( errmsg[i] == ' ' ) 
			*p++ = '+';
		else if ( errmsg[i] == '.' ) { 
			p[0] = '%';
			p[1] = '2';
			p[2] = 'E';
			p += 3;
		}
		else    
			*p++ = errmsg[i];
	}
	*p = '\0';
	int32_t bufLen = p - buf;
	// replace xxx
	char *s = strstr ( buf , "xxx" );
	char *t = strstr ( buf , "\r\n\r\n" ) + 4;
	int32_t clen = bufLen - ( t - buf );
	sprintf ( s , "%3li" , clen );
	s [ 3 ] = '\r';

	// post it up
	// borrow our HttpServer's TcpServer
	TcpServer *ts = g_httpServer.getTcp();
	if ( ! ts->sendMsg ( ip            ,
			     gbstrlen(ip)    ,
			     80            ,
			     buf           ,
			     PAGER_BUF_SIZE        ,
			     bufLen        ,
			     bufLen        ,
			     state         ,
			     gotDocWrapper ,
			     60*1000       ,
			     100*1024      ,  // maxTextDocLen
			     100*1024      )) // maxOtherDocLen
		return false;
	// we did not block, so update h->m_emailCode
	gotDocWrapper ( state , NULL );
	// we did not block
	return true;
}
#endif


// JAB: warning abatement
#if 0
// . returns false if blocks, true otherwise
// . sets g_errno on error
bool pageSprintPCS ( Host *h , char *errmsg , char *num) {
	char *host = "messaging.sprintpcs.com";
	//char *num  = "2813004108"; // partap

	// for debug without actually wasting our cell phone instant messages
	//log("EMAIL SENT hid=%"INT32"!!!!!!!!!!!!!!!",h->m_hostId);
	//gotDocWrapper ( h , NULL );
	//return true;
	// looks like TcpServer won't let us use a static sendBuf
	// cuz we cannot just set TcpSocket->m_sendBuf to NULL to stop
	// the freeing cuz if TcpServer::gotIp() has an error it
	// frees it sendBuf. This happens in other places too.
	void * state = h;

	char *buf = (char *) mmalloc ( PAGER_BUF_SIZE , "PingServer" );
	// bail on malloc error
	if ( ! buf ) {
		log("net: Could not allocate %"INT32" bytes to send email "
		    "to sprint pcs." , PAGER_BUF_SIZE);
		g_pingServer.m_numReplies++;
		return true;
	}

	char *p    = buf;
	char *pend = buf + PAGER_BUF_SIZE;

	// save ptr to host or email type
	*(Host **)(p+PAGER_BUF_SIZE-4) = (Host*)state;

	// then the message to send
	sprintf ( p , 
		  //"POST /mytmobile/login/default.asp?"
		  "POST /textmessaging/composeconfirm"
		  //;jsessionid=CSRtp1YQo3WyzNL0edNEHsI4oN2IloYE9buvqYaO
		  //Pgijz4Ht1WUO!1942930617!182751426!5070!7002"
		  " HTTP/1.0\r\n"
		  "Accept: image/gif, image/x-xbitmap, image/jpeg, "
		  "image/pjpeg, application/x-shockwave-flash, "
		  "application/msword, */*\r\n"
		  "Accept-Language: en-us\r\n"
		  "Content-Type: application/x-www-form-urlencoded\r\n"
		  "Accept-Encoding: gzip, deflate\r\n"
		  "User-Agent: Mozilla/4.0 "
		  "(compatible; MSIE 6.0; Windows 98; Win 9x 4.90)\r\n"
		  "Host: %s\r\n" // www.t-mobile.com
		  "Content-Length: xxx\r\n"
		  //"Connection: Keep-Alive\r\n"
		  "Connection: close\r\n"
		  "Cookie: \r\n"
		  "Cache-Control: no-cache\r\n\r\n"
		  // post data
		  "randomToken=&"
		  "phoneNumber=%s&" // phone number
		  //"characters=%"INT32"&"
		  //"characters=160&"
		  //"callBackNumber=&"
		  "message=" ,
		  host , num );//, (int32_t)160-gbstrlen(errmsg) );
	p += gbstrlen ( p );
	// append the err msg, but convert spaces to +'s
	int32_t i;
	for ( i = 0 ; errmsg[i] && p + 4 < pend ; i++ ) {
		if      ( errmsg[i] == ' ' ) 
			*p++ = '+';
		else if ( errmsg[i] == '.' ) { 
			p[0] = '%';
			p[1] = '2';
			p[2] = 'E';
			p += 3;
		}
		else    
			*p++ = errmsg[i];
	}
	*p = '\0';
	//int32_t bufLen = p - buf;

	// gotta get the cookie
	Url url;
	//char *uu = "http://65.174.43.73/textmessaging/compose";
	char *uu = "http://144.230.162.49/textmessaging/compose";
	//char *uu = "http://messaging.sprintpcs.com/textmessaging/compose";
	url.set(uu,gbstrlen(uu));
	if ( ! g_httpServer.getDoc ( &url              , 
				     0                 , // offset
				     -1                , // size
				     false             , // m_ifModifiedSince
				     buf               , // state
				     sprintPCSWrapper    , // 
				     30*1000           , // timeout
				     0                 , // m_proxyIp
				     0                 , // m_proxyPort
				     100*1024          , // m_maxTextDocLen
				     100*1024          , // m_maxOtherDocLen  
				     "Mozilla/4.0 "      // user agent
				     "(compatible; MSIE 6.0; Windows 98; "
				     "Win 9x 4.90)\r\n" ) ) 
		return false;
	// must have been an error
	log("net: Got error getting page from Sprint PCS: %s.",
	    mstrerror(g_errno));
	// always call this at the end
	return pageSprintPCS2 ( buf , NULL );
}
#endif

// JAB: warning abatement
#if 0
void sprintPCSWrapper ( void *state , TcpSocket *ts) {
	pageSprintPCS2 ( state , ts );
}
#endif

// JAB: warning abatement
#if 0
bool pageSprintPCS2 ( void *state , TcpSocket *s) {

	char *ip   = "65.174.43.73";

	char *buf = (char *)state;
	int32_t  bufLen = gbstrlen(buf);

	char *p    = buf;
	char *pend = p + bufLen;

	// last 4 bytes of buffer is the Host ptr


	void* state2 = (p+PAGER_BUF_SIZE-4);

	if ( g_errno || ! s ) {
		mfree ( buf , PAGER_BUF_SIZE , "PingServer" );
		// always call this at the end
		gotDocWrapper( state2 , NULL );
		return true;
	}

	// sprint debug msg
	//log("net: Got reply from Sprint PCS:\n%s",s->m_readBuf);

	// get the cookie
	HttpMime mime;
	mime.set ( s->m_readBuf  , s->m_readOffset , NULL );
	char *cookie = mime.getCookie ( );
	int32_t  cookieLen = mime.getCookieLen ();
	bufLen += cookieLen;

	if ( ! cookie ) {
		log("net: No cookie to send to Sprint PCS");
		g_errno = EBADREPLY;
		mfree ( buf , PAGER_BUF_SIZE , "PingServer" );
		// always call this at the end
		gotDocWrapper( state2 , NULL );
		return true;
	}

	// part the request to make room for cookie
	char *ss = strstr ( p , "Cookie: ");
	if ( ! ss ) { char *xx = NULL; *xx = 0; }
	ss += 8; // points to right after the space
	// insert the cookie
	gbmemcpy ( ss+cookieLen , ss , pend - ss );
	gbmemcpy ( ss , cookie , cookieLen );
	pend += cookieLen;

	// replace xxx
	char *sx = strstr ( buf , "xxx" );
	char *t = strstr ( buf , "\r\n\r\n" ) + 4;
	int32_t clen = bufLen - ( t - buf );
	sprintf ( sx , "%3li" , clen );
	sx [ 3 ] = '\r';

	// sprint debug msg
	//log("net: Sending to Sprint PCS:\n%s",buf);

	// post it up
	// borrow our HttpServer's TcpServer
	TcpServer *ts = g_httpServer.getTcp();
	if ( ! ts->sendMsg ( ip            ,
			     gbstrlen(ip)    ,
			     80            ,
			     buf           ,
			     PAGER_BUF_SIZE        ,
			     gbstrlen(buf)   ,
			     gbstrlen(buf)   ,
			     state2     ,
			     gotDocWrapper ,
			     60*1000       ,
			     100*1024      ,  // maxTextDocLen
			     100*1024      )) // maxOtherDocLen
		return false;
	// we did not block, so update h->m_emailCode
	gotDocWrapper ( state2 , NULL );
	// we did not block
	return true;
}
#endif

// a bit of a hack for monitor.cpp
//int32_t g_emailMX1IPBackUp = 0;

bool sendAdminEmail ( Host  *h,
		      char  *fromAddress,
		      char  *toAddress,
		      char  *body , 
		      char  *emailServIp) {
	// create a new buffer
	char *buf = (char *) mmalloc ( PAGER_BUF_SIZE , "PingServer" );
	// fill the buffer
	char *p = buf;
	// helo line
	p += sprintf(p, "HELO gigablast.com\r\n");
	// mail line
	p += sprintf(p, "MAIL from:<%s>\r\n", fromAddress);
	// to line
	p += sprintf(p, "RCPT to:<%s>\r\n", toAddress);
	// data
	p += sprintf(p, "DATA\r\n");
	// body
	p += sprintf(p, "To: %s\r\n", toAddress);
	p += sprintf(p, "Subject: Sysadmin Event Message\r\n");
	// mime header must be separated from body by an extra \r\n
	p += sprintf(p, "\r\n");
	p += sprintf(p, "%s", body);
	// quit
	p += sprintf(p, "\r\n.\r\nQUIT\r\n");
	// get the length
	int32_t buffLen = (p - buf);
	// send the message
	TcpServer *ts = g_httpServer.getTcp();
	log ( LOG_WARN, "PingServer: Sending email to sysadmin:\n %s", buf );
	//if ( !ts->sendMsg ( g_conf.m_smtpHost,
	//		    gbstrlen(g_conf.m_smtpHost),
	//		    g_conf.m_smtpPort,
	char *ip = emailServIp;//"66.154.102.229"; // gf39, mail server ip
	// use backup if there
	//char ipString[64];
	//if ( g_emailServIPBackup ) {
	//	iptoa(ipString,g_emailMX1IPBackup);
	//	ip = ipString;
	//}
	if ( !ts->sendMsg ( ip,
			    gbstrlen(ip),
			    25, // smtp (send mail transfer protocol) port
			    buf,
			    PAGER_BUF_SIZE,
			    buffLen,
			    buffLen,
			    h,
			    gotDocWrapper,
			    60*1000,
			    100*1024,
			    100*1024 ) )
		return false;
	// we did not block, so update h->m_emailCode
	gotDocWrapper ( h , NULL );
	// we did not block
	return true;
}


void gotDocWrapper ( void *state , TcpSocket *s ) {
	// keep track of how many we got
	g_pingServer.m_numReplies2++;
	if ( g_pingServer.m_numReplies2 > g_pingServer.m_maxRequests2 ) {
		log(LOG_LOGIC,"net: too many replies received. "
		    "requests:%"INT32" replies:%"INT32" maxrequests:%"INT32"",
		    g_pingServer.m_numRequests2, 
		    g_pingServer.m_numReplies2, 
		    g_pingServer.m_maxRequests2);
		//char *xx = NULL; *xx = 0;
	}
	Host *h = (Host *)state;

	//	if ( ! h ) { log("net: h is NULL in pingserver."); return; }
	// don't let tcp server free the sendbuf, that's static
	//s->m_sendBuf = NULL;
	if ( g_errno ) { 
		if(h) {
			log("net: Had error sending email to mobile for dead "
			    "hostId "
			    "#%"INT32": %s.", h->m_hostId,mstrerror(g_errno));
		} else {
			log("net: Had error sending email to mobile for "
			    "int32_t latency: %s.", mstrerror(g_errno));
		}
		// mark as 0 on error to try sending again later
		//h->m_emailCode = 0; 
		// reset these errors just in case
		g_errno = 0;
		return;
	}
	// log it
	if(h)
		log("net: Email sent successfully for dead host #%"INT32".", 
		    h->m_hostId);
	else 
		log("net: Email sent successfully for int32_t latency.");
	// . show the reply
	// . seems to crash if we log the read buffer... no \0?
	if ( s && s->m_readBuf )
		log("net: Got messaging server reply #%"INT32".\n%s",
		    g_pingServer.m_numReplies2,s->m_readBuf );
	// otherwise, success
	if(h) {
		h->m_emailCode = 1;
		// . mark him as the one email we sent
		// . don't send another email until he comes back up!
		// . it's just a waste
		s_lastSentHostId = h->m_hostId;
	}
}

//////////////////////////////////////////////////
// the shutdown broadcast
//////////////////////////////////////////////////

// when a host goes down gracefully it lets all its peers know so they
// do not send requests to it.

// . broadcast shutdown notes
// . returns false if blocked, true otherwise
// . does not set g_errno
bool PingServer::broadcastShutdownNotes ( bool    sendEmailAlert          ,
					  void   *state                   ,
					  void  (* callback)(void *state) ) {
	// don't broadcast on interface machines
	if ( g_conf.m_interfaceMachine ) return true;
	// only call once
	if ( m_numRequests != m_numReplies ) return true;
	// keep track
	m_numRequests = 0;
	m_numReplies  = 0;
	// save callback info
	m_broadcastState    = state;
	m_broadcastCallback = callback;
	// use this buffer
	static char s_buf [5];
	*(int32_t *)s_buf = g_hostdb.m_hostId;
	// followed by sendEmailAlert
	s_buf[4] = (char)sendEmailAlert;

	int32_t np = g_hostdb.getNumProxy();
	// do not send to proxies if we are a proxy
	if ( g_hostdb.m_myHost->m_isProxy ) np = 0;
	// sent to proxy hosts too so they don't send to us
	for ( int32_t i = 0 ; i < np ; i++ ) {
		// get host
		Host *h = g_hostdb.getProxy(i);
		// skip ourselves
		//if ( h->m_hostId == g_hostdb.m_hostId ) continue;
		// count as sent
		m_numRequests++;
		// send it right now
		if ( g_udpServer.sendRequest ( s_buf         ,
						5             , // rqstSz
						0x11          , // msgType
						h->m_ip       ,
						h->m_port     ,
					       // we are sending to a proxy!
					       -1 , // h->m_hostId   ,
						NULL          , //
						NULL          , // state
						gotReplyWrapperP2 ,
						3     , // 3 sec timeout
						-1    , // default backoff
						-1    , // default maxwait
						NULL  , // reply buf
						0     , // reply buf size
						0     ))// niceness
			continue;
		// otherwise, had an error
		m_numReplies++;
		// reset g_errno
		g_errno = 0;
	}


	// send a high priority msg to each host in network, except us
	for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
		// get host
		Host *h = &g_hostdb.m_hosts[i];
		// skip ourselves
		if ( h->m_hostId == g_hostdb.m_hostId ) continue;
		// count as sent
		m_numRequests++;
		// request will be freed by UdpServer
		//char *r = (char *) mmalloc ( 4 , "PingServer" );
		//if ( ! r ) return true;
		//gbmemcpy ( r , (char *)(&h->m_hostId) , 4 );
		// send it right now
		if ( g_udpServer.sendRequest ( s_buf         ,
						5             , // rqstSz
						0x11          , // msgType
						h->m_ip       ,
						h->m_port     ,
						h->m_hostId   ,
						NULL          , //
						NULL          , // state
						gotReplyWrapperP2 ,
						3     , // 3 sec timeout
						-1    , // default backoff
						-1    , // default maxwait
						NULL  , // reply buf
						0     , // reply buf size
						0     ))// niceness
			continue;
		// otherwise, had an error
		m_numReplies++;
		// reset g_errno
		g_errno = 0;
	}
	// if already done return true
	if ( m_numReplies >= m_numRequests ) return true;
	// otherwise we blocked
	return false;
}

void gotReplyWrapperP2 ( void *state , UdpSlot *slot ) {
	// count it
	g_pingServer.m_numReplies++;
	// don't let udp server free our send buf, we own it
	slot->m_sendBufAlloc = NULL;
	// discard errors
	g_errno = 0;
	// bail if not done
	if ( g_pingServer.m_numReplies < g_pingServer.m_numRequests ) return ;
	// call our wrapper
	if ( g_pingServer.m_broadcastCallback )
	       g_pingServer.m_broadcastCallback(g_pingServer.m_broadcastState);
}

//////////////////////////////////////////////////
// the sync point section
//////////////////////////////////////////////////

// every 10 minutes, host #0 tells all hosts to store a "sync point".
// this will force all hosts to dump a list of the names of all the files
// they have created since the last "sync point" was stored. in addition
// to dumping this list to disk, the "sync point" itself (a timestamp)
// will be appened to the list so we know at what approximate times all
// the files were created. this if for doing incremental synchronization.
// all "sync points" are from host #0's clock.

// ensure not too many sync point store requests off at once
static int32_t s_outstandingTaps = 0;
static char s_lastSyncPoint [ 9 ];
static int32_t s_nextTapHostId ;

static void tapLoop ( ) ;
static void gotTapReplyWrapper ( void *state , UdpSlot *slot ) ;

// this wrapper is called once every second, or so...
void sleepWrapper10 ( int fd , void *state ) {
	// can't do much if still waiting on a very old tap!
	if ( s_outstandingTaps ) {
		log("PingServer::sleepWrapper10: stuck tap. sync messed up.");
		return;
	}
	// request is 9 bytes to distinguish from 8-byte requests
	int64_t stamp = gettimeofdayInMilliseconds();
	// . keep incrementing this if already used to avoid repeat stamps
	// . should be extremely rare
	// . even with this we can still have repeat stamps if we are not
	//   in sync, so i check for that in Sync::getSyncPoint()
	// . -1 is reserved for meaning no stamp (see Msg0.h)
	//while ( g_sync.hasStamp ( stamp ) || stamp == -1 ) stamp++;
	// save it for distributing
	*(int64_t *) s_lastSyncPoint = stamp;
	// reset loop parms
	s_nextTapHostId = 0;
	// do the tap loop
	tapLoop ( );
}

void tapLoop ( ) {
	// . don't use more than 16       UdpSlots for tapping
	// . don't use more than numHosts UdpSlots for tapping
	int32_t max = g_hostdb.getNumHosts();
	if ( max > 16 ) max = 16;
 loop:
	if ( s_outstandingTaps >= max ) return;
	// cycle through pinging different hosts
	int32_t n = g_hostdb.getNumHosts();
 	// if done sending requests then just return
	if ( s_nextTapHostId >= n ) return;
	// otherwise, tap this guy
	g_pingServer.tapHost ( s_nextTapHostId );
	// inc next host to ping
	s_nextTapHostId++;
	// do as many in a row as we can
	goto loop;
}

void gotTapReplyWrapper ( void *state , UdpSlot *slot ) {
	// it came back
	s_outstandingTaps--;
	// don't let udp server free our send buf, we own it
	slot->m_sendBufAlloc = NULL;
	// discard errors
	g_errno = 0;
	// loop to do more if we need to
	tapLoop ( );
}

// ping host #i
void PingServer::tapHost ( int32_t hostId ) {
	// don't ping on interface machines
	if ( g_conf.m_interfaceMachine ) return;
	// don't tap ourselves
	//if ( hostId == g_hostdb.m_hostId ) return;
	// watch for out of bounds
	if ( hostId < 0 || hostId >= g_hostdb.getNumHosts() ) return;
	// get host ptr
	Host *h = g_hostdb.getHost ( hostId );
	// return if NULL
	if ( ! h ) return;
	// don't tap again if already in progress
	//if ( h->m_inTapProgress ) return;
	// count it
	s_outstandingTaps++;
	// consider it in progress
	//h->m_inProgress = true;
	// . launch one
	// . returns false and sets errno on error
	// . returns true if request sent and it blocked
	// . size of 2 is unique to a tap
	// . use MsgType of 0x11 for pinging
	// . we now use the high-prioirty server, g_udpServer2
	// . now we send over our current time so remote host will sync up
	//   with us
	// . only sync up with hostId #0
	// . if he goes down its ok because time is mostly important for 
	//   spidering and spidering is suspended if a host is down
	if ( g_udpServer.sendRequest ( s_lastSyncPoint ,
					9               ,
					0x11            ,
					h->m_ip         ,
					h->m_port       ,
					h->m_hostId     ,
					NULL            ,
				       (void *)(PTRTYPE)h->m_hostId,// cb state
					gotTapReplyWrapper ,
					30              , // timeout
					1000            , // backoff
					10000           , // max wait
					NULL            , // reply buf
					0               , // reply buf size
					0               ))// niceness
		return;
	// it had an error, so dec the count
	s_outstandingTaps--;
	// consider it out of progress
	//h->m_inTapProgress = false;
	// had an error
	log("net: Had error sending sync point request to host #%"INT32": %s.", 
	    h->m_hostId,mstrerror(g_errno) );
	// reset it cuz it's not a showstopper
	g_errno = 0;
}

// . this should be called on reception of an ACK to keep track of ping time.
// . this should be called on time out of a dgram reception.
// . "tripTime" is in milliseconds
// . "tripTime" should be the timeOut time if "timedOut" is true
// . returns false if you have to wait for your callback to be called
// . returns true if you don't have to wait... stamp happened immediately
// . sets g_errno on any error, regardless of whether true or false was returnd
/*
void PingServer::stampHost ( int32_t  hostId , int32_t tripTime , bool timedOut ) {
	// hostId of -1 means unknown, so we can't stamp it
	if ( hostId ==         -1 ) return;
	if ( hostId >= g_hostdb.getNumHosts() ) return;
	// ensure tripTime doesn't wrap around
	if ( tripTime < 0 ) return; // tripTime = 0x7fffffff;
	// sanity check
	if ( tripTime > 65000 ) {
		log("PingServer::stampHost: bad tripTime");
	}
	// get the host, return false on error
	Host *h = m_hostPtrs[hostId];
	// if he was considered timedout, only stamp if it makes his time worse
	if ( timedOut && tripTime < h->m_pingAvg ) return;
	// TODO: do we need this?
	// . don't update this host if this new ping time is < 10% different
	//   than the ping time we have recorded now
	//	int32_t diff = newPing - h->m_pingAvg;
	//	if ( diff < 0 ) diff *= -1;
	//	if ( ( 100 * diff ) / h->m_pingAvg < 10 ) return true;

	// . we keep track of the last 4 pings for each host
	// . we use the avg ping time to select fast hosts for querying
	// . we use the std. dev. to know when to re-send datagrams
	// shift the 5-ping window and insert the new ping
        for ( int32_t i = 0 ; i < 3 ; i++ ) h->m_pings[i] = h->m_pings[i+1];
	h->m_pings[3] = tripTime; // our latest ping
	// compute the avg
	int32_t avg = 0;
        for ( int32_t i = 0 ; i < 4 ; i++ ) avg += h->m_pings[i];
	avg /= 4;
	h->m_pingAvg = avg;
	// compute the std dev
	int32_t stdDev = 0;
        for ( int32_t i = 0 ; i < 4 ; i++ ) {
		int32_t diff = ( h->m_pings[i] - avg );
		if ( diff < 0 ) stdDev -= diff;
		else            stdDev += diff;
	}
	stdDev /= 4;
	h->m_pingStdDev = stdDev;
}
*/

/*
void PingServer::getTimes ( int32_t hostId , int32_t *avg , int32_t *stdDev ) {
	// make defaults (right now for dns server only)
	*avg    = 5000; // milliseconds = 5 seconds
	*stdDev =  500;
	// hostId of -1 means unknown, so we can't stamp it
	if ( hostId ==         -1 ) return;
	if ( hostId >= g_hostdb.getNumHosts() ) return;
	// get the host, return false on error
	Host *h = m_hostPtrs[hostId];
	*avg    = h->m_pingAvg;
	*stdDev = h->m_pingStdDev;
	return;
}
*/

// if its status changes from dead to alive or vice versa, we have to
// update g_hostdb.m_numHostsAlive. Dns.cpp and Msg17 will use this count
void updatePingTime ( Host *h , int32_t *pingPtr , int32_t tripTime ) {

	// sanity check
	if ( pingPtr != &h->m_ping && pingPtr != &h->m_pingShotgun ) { 
		char *xx = NULL; *xx = 0; }

	// . was it dead before this?
	// . both ips must be dead for it to be dead
	bool wasDead = g_hostdb.isDead ( h );

	// do the actual update
	*pingPtr = tripTime;
	// do not go negative on us
	if ( *pingPtr < 0 ) *pingPtr = 0;

	// . record max ping
	// . wait 60 seconds for everyone to come up if we just started up
	if ( tripTime > h->m_pingMax &&
	     // do not count shotgun ips!
	     pingPtr == &h->m_ping &&
	     gettimeofdayInMillisecondsLocal()-g_process.m_processStartTime>=
	     60000 ) {
		h->m_pingMax = tripTime;
		char *desc = "";
		if ( pingPtr == &h->m_pingShotgun ) desc = " (shotgun)";
		if ( tripTime > 50 )
			log("gb: got new max ping time of %"INT32" for "
			    "host #%"INT32"%s ",tripTime,h->m_hostId,desc);
	}

	// is it dead now?
	bool isDead = g_hostdb.isDead ( h );

	// force it out of sync
	if ( isDead ) h->m_inSync = false;

	//if ( ! wasDead && isDead ) 
	//	log("hey");
	//logf(LOG_DEBUG,"ping: hostid %"INT32" wasDead=%"INT32" isDead=%"INT32" "
	//               "numAlive=%"INT32"",
	//     (int32_t)h->m_hostId,(int32_t)wasDead,(int32_t)isDead,
	//     (int32_t)g_hostdb.m_numHostsAlive);

        if( h->m_isProxy ) {
                // maintain m_numProxyAlive if there was a change in state
                //if ( wasDead && ! isDead ) g_hostdb.m_numProxyAlive++;
                //if ( ! wasDead && isDead ) g_hostdb.m_numProxyAlive--;

                // sanity check, this should be at least 1 since we are alive
                //if ( g_hostdb.m_numProxyAlive < 0 ||
                //     g_hostdb.m_numProxyAlive > g_hostdb.getNumProxy() ) {
                //        char *xx = NULL; *xx =0; }
        }
        else {
                // maintain m_numHostsAlive if there was a change in state
                if ( wasDead && ! isDead ) g_hostdb.m_numHostsAlive++;
                if ( ! wasDead && isDead ) g_hostdb.m_numHostsAlive--;

                // sanity check, this should be at least 1 since we are alive
                if ( g_hostdb.m_numHostsAlive < 0 ||
                     g_hostdb.m_numHostsAlive > g_hostdb.m_numHosts ) {
                        char *xx = NULL; *xx =0; }
        }
}

void checkKernelErrors( int fd, void *state ){
	Host *me = g_hostdb.m_myHost;

	int64_t st = gettimeofdayInMilliseconds();
	char buf[4098];
	// klogctl reads the last 4k lines of the kernel ring buffer
	int16_t bufLen = klogctl(3,buf,4096);
	int64_t took = gettimeofdayInMilliseconds() - st;
	if ( took >= 3 ) {
		int32_t len = bufLen;
		if ( len > 200 ) len = 200;
		char c = buf[len];
		buf[len] = '\0';
		log("db: klogctl took %"INT64" ms to read %s",took, buf);
		buf[len] = c;
	}

	if ( bufLen < 0 ){
		log ("db: klogctl returned error: %s",mstrerror(errno));
		return;
	}
	// make sure not too big!
	if ( bufLen >= 4097 ) {
		log ("db: klogctl overflow");
		return;
	}
	buf[bufLen] = '\0';

	// compare it to the old buffer
	if ( s_kernelRingBufLen == bufLen && 
	     strncmp ( s_kernelRingBuf, buf, bufLen ) == 0 )
		// nothing has changed so return
		return;

	// somethings changed. find out what part has changed
	// sometimes the kernel ring buffer gets full and overwrites itself.
	// in that case compare only latter half of the old buffer
	char *oldKernBuf = s_kernelRingBuf;
	int32_t oldKernBufLen = s_kernelRingBufLen;
	if ( s_kernelRingBufLen > 3 * 1024 ){
		oldKernBuf = s_kernelRingBuf + s_kernelRingBufLen / 2;
		oldKernBufLen = gbstrlen( oldKernBuf );
	}

	// somethings changed. find out what part has changed
	char *changedBuf = strstr ( buf, oldKernBuf );
	// we found the old buf. skip it and go to the part that has changed
	if ( changedBuf ) 
		changedBuf += oldKernBufLen;
	// we couldn't find the old buf in the new buf!
	else 
		changedBuf = buf;
	int32_t changedBufLen = gbstrlen(changedBuf);
	
	// copy the new buf over to the old buf 
	strcpy ( s_kernelRingBuf, buf );
	s_kernelRingBufLen = bufLen;

	static int32_t s_lastCount = 0;

	// since we do not know if this host has been fixed or not by looking
	// at the kernel ring buffer, keep returning until someone clicks
	// 'clear kernel error message' control in Master Controls. 
	// but don't return before copying over the new buffer
	if ( me->m_pingInfo.m_kernelErrors > 0 ) return;

	// check if we match any error strings in master controls
	char *p = NULL;
	if ( gbstrlen(g_conf.m_errstr1) > 0 )
		p = strstr( changedBuf, g_conf.m_errstr1 );

	if ( !p && gbstrlen(g_conf.m_errstr2) > 0 )
		p = strstr( changedBuf, g_conf.m_errstr2 );

	if ( !p && gbstrlen(g_conf.m_errstr3) > 0 )
		p = strstr( changedBuf, g_conf.m_errstr3 );

	if ( p ){
		// a kernel error that we cared for has happened!
		// check what kind of an error is this
		// isolate that line from the rest of the buf
		while ( p < changedBuf + changedBufLen && *p != '\n' )
			p++;
		*p = '\0';

		while ( p > changedBuf && *(p-1) != '\n' )
			p--;
	
		if ( strncasestr ( p, gbstrlen(p) , "scsi" ) &&
		     g_numIOErrors > s_lastCount ) {
			me->m_pingInfo.m_kernelErrors = ME_IOERR;
			s_lastCount = g_numIOErrors;
		}
		else if ( strncasestr ( p, gbstrlen(p), "100 mbps" ) )
			me->m_pingInfo.m_kernelErrors = ME_100MBPS;
		// assume an I/O IO error here otherwise
		else if ( g_numIOErrors > s_lastCount ) {
			me->m_pingInfo.m_kernelErrors = ME_UNKNWN;
			s_lastCount = g_numIOErrors;
		}
		log ( LOG_DEBUG,"PingServer: error message in "
		      "kernel ring buffer, \"%s\"", p );
		log ( LOG_WARN,"PingServer: this host shall be "
		      "dead to all other hosts unless the problem is fixed "
		      "and kernel error message cleared in Master Controls." );
	}
	return;
}

void PingServer::sendEmailMsg ( int32_t *lastTimeStamp , char *msg ) {
	// leave if we already sent and alert within 5 mins
	//static int32_t s_lasttime = 0;
	int32_t now = getTimeGlobalNoCore();
	if ( now - *lastTimeStamp < 5*60 ) return;
	// prepare msg to send
	//Host *h0 = g_hostdb.getHost ( 0 );
	char msgbuf[1024];
	snprintf(msgbuf, 1024,
		 "cluster %s : proxy: %s",
		 g_conf.m_clusterName,
		 msg);
	// send it, force it, so even if email alerts off, it sends it
	g_pingServer.sendEmail ( NULL   , // Host *h
				 msgbuf , // char *errmsg = NULL , 
				 true   , // bool sendToAdmin = true ,
				 false  , // bool oom = false ,
				 false  , // bool kernelErrors = false ,
				 false  , // bool parmChanged  = false ,
				 true   );// bool forceIt      = false );
	*lastTimeStamp = now;
	return;
}

/////////////////
//
// for sending email notifications to external addresses
//
///////////////////

/*
bool gotMxIp ( EmailInfo *ei ) ;

void gotMxIpWrapper ( void *state , int32_t ip ) {
	EmailInfo *ei = (EmailInfo *)state;
	// i guess set it
	ei->m_mxIp = ip;
	// handle it
	if ( ! gotMxIp ( ei ) ) return;
	// did not block, call callback
	ei->m_callback ( ei->m_state );
}
*/

void doneSendingEmailWrapper ( void *state , TcpSocket *sock ) {
	if ( g_errno )
		log("crawlbot: error sending email = %s",mstrerror(g_errno));
	// log the reply
	if ( sock && sock->m_readBuf )
		log("crawlbot: got socket reply=%s", sock->m_readBuf);
	EmailInfo *ei = (EmailInfo *)state;
	ei->m_callback ( ei->m_state );
}


// returns false if blocked, true otherwise
bool sendEmail ( class EmailInfo *ei ) {

	// this is often set from XmlDoc.cpp::indexDoc()
	g_errno = 0;

	char *to = ei->m_toAddress.getBufStart();
	char *dom = strstr(to,"@");
	if ( ! dom || ! dom[1] ) {
		g_errno = EBADENGINEER;
		log("email: missing @ sign in email %s",to);
		return true;
	}
	// point to domain
	dom++;
	// ref that for printing HELO line
	ei->m_dom = dom;

	// just send it to a sendmail server which will forward it,
	// because a lot of email servers don't like us connecting directly
	// beause i think our IP address does not match that of our 
	// MX ip for our domain? sendmail must be configured to allow
	// forwarding if it receives an email from the IP of host #0
	// in the cluster.
	ei->m_mxIp = atoip(g_conf.m_sendmailIp);

	/*
	// prepend a special marker so Dns.cpp returns the mx record
	ei->m_mxDomain.safePrintf("gbmxrec-%s",dom);

	// get mx ip. returns false if would block.
	if ( ! g_dns.getIp ( ei->m_mxDomain.getBufStart() ,
			     ei->m_mxDomain.getLength() ,
			     &ei->m_mxIp,
			     ei ,
			     gotMxIpWrapper ) )
		return false;

	return gotMxIp ( ei );
}

// returns false if blocked, true otherwise
bool gotMxIp ( EmailInfo *ei ) {

	// error?
	if ( g_errno ) {
		log("crawlbot: error getting MX IP to send email alert for "
		    "%s = %s",
		    ei->m_mxDomain.getBufStart(),
		    mstrerror(g_errno));
		return true;
	}
	*/

	// wtf?
	if ( ei->m_mxIp == 0 ) {
		log("crawlbot: got bad MX ip of 0 for %s",
		    ei->m_mxDomain.getBufStart());
		return true;
	}

	// label alloc'd mem with gotmxip in case of mem leak
	SafeBuf sb;//("gotmxip");
	// helo line
	sb.safePrintf("HELO %s\r\n",ei->m_dom);
	// mail line
	sb.safePrintf( "MAIL FROM:<%s>\r\n", ei->m_fromAddress.getBufStart());
	// to line
	sb.safePrintf( "RCPT TO:<%s>\r\n", ei->m_toAddress.getBufStart());
	// data
	sb.safePrintf( "DATA\r\n");
	// body
	sb.safePrintf( "To: %s\r\n", ei->m_toAddress.getBufStart());
	sb.safePrintf( "Subject: %s\r\n",ei->m_subject.getBufStart());
	// mime header must be separated from body by an extra \r\n
	sb.safePrintf( "\r\n");
	sb.safePrintf( "%s", ei->m_body.getBufStart() );
	// quit
	sb.safePrintf( "\r\n.\r\nQUIT\r\n\r\n");
	// send the message
	TcpServer *ts = g_httpServer.getTcp();
	log ( LOG_WARN, "crawlbot: Sending email to %s (MX IP=%s):\n %s", 
	      ei->m_toAddress.getBufStart(),
	      iptoa(ei->m_mxIp),
	      sb.getBufStart() );
	// make a temp string
	SafeBuf mxIpStr;
	mxIpStr.safePrintf("%s",iptoa(ei->m_mxIp) );
	if ( !ts->sendMsg ( mxIpStr.getBufStart(),
			    mxIpStr.length(),
			    25, // smtp (send mail transfer protocol) port
			    sb.getBufStart(),
			    sb.getCapacity(),
			    sb.getLength(),
			    sb.getLength(),
			    ei,//NULL,//h,
			    doneSendingEmailWrapper,
			    60*1000,
			    100*1024,
			    100*1024 ) ) {
		// tcpserver will free it, so prevent double free with detach
		sb.detachBuf();
		return false;
	}
	// error? if no error, it was a successful write and it done,
	// otherwise we will want to free the sendbuf here so do not
	// call detachBuf()
	if ( ! g_errno )
		// tcpserver will free it, so prevent double free with detach
		sb.detachBuf();
	// we did not block
	return true;
}


static void gotMandrillReplyWrapper ( void *state , TcpSocket *s ) {
	// why core here with s NULL
	if ( ! s ) {
		// crap seems like we do not retry so they will not get
		// the notification... how to fix better?
		log("email: failed to lookup mandrill ip. sock is null.");
		g_errno = EBADIP;
	}
	else {
		// log the mandril reply
		log("email: got mandrill reply: %s",s->m_readBuf);
	}
	EmailInfo *ei = (EmailInfo *)state;
	if ( ei->m_callback ) ei->m_callback ( ei->m_state );
}


// mailchimp http mail api
bool sendEmailThroughMandrill ( class EmailInfo *ei ) {

	// this is often set from XmlDoc.cpp::indexDoc()
	g_errno = 0;

	SafeBuf sb;

	// then the message to send
	sb.safePrintf(
		  "POST /api/1.0/messages/send-template.json"
		  " HTTP/1.0\r\n"
		  "Accept: image/gif, image/x-xbitmap, image/jpeg, "
		  "image/pjpeg, application/x-shockwave-flash, "
		  "application/msword, */*\r\n"
		  "Accept-Language: en-us\r\n"
		  //"Content-Type: application/x-www-form-urlencoded\r\n"
		  "Content-Type: application/json\r\n"
		  //"Accept-Encoding: gzip, deflate\r\n"
		  "User-Agent: Mozilla/4.0 "
		  "(compatible; MSIE 6.0; Windows 98; Win 9x 4.90)\r\n"
		  "Host: mandrillapp.com\r\n" // www.t-mobile.com
		  "Content-Length: xxxx\r\n"
		  //"Connection: Keep-Alive\r\n"
		  "Connection: close\r\n"
		  //"Cookie: \r\n"
		  "Cache-Control: no-cache\r\n\r\n"
		  );
	//
	// post data
	//
	char *to = ei->m_toAddress.getBufStart();
	char *from = ei->m_fromAddress.getBufStart();

	char *crawl = "unknown2";
	CollectionRec *cr = g_collectiondb.m_recs[ei->m_collnum];
	if ( cr ) crawl = cr->m_diffbotCrawlName.getBufStart();

	SafeBuf ub;
	ub.safePrintf( "{\"key\":\"GhWT0UpcVBl7kmumrt9dqg\","
		       "\"template_name\":\"crawl-finished\","
		       "\"template_content\": [],"
		       "\"message\": {"
		       "\"to\": ["
		       "{"
		       "\"email\":\"%s\""
		       "}"
		       "],"

		       "\"from_email\":\"%s\","
		       "\"headers\": {"
		       "\"Reply-To\":\"%s\""
		       "},"
		       "\"bcc_address\":\"%s\","
		       "\"global_merge_vars\":["
		       "{"
		       "\"name\":\"CRAWLNAME\","
		       "\"content\":\"%s\""
		       "}"
		       "]"
		       "}"
		       "}"
		       , to
		       , from
		       , from
		       , from
		       , crawl
		       );
	// this is not for application/json content type in POST request
	//ub.urlEncode();
	// how big?
	int32_t contentLen = ub.length();
	// append the post data to the full request
	sb.safeMemcpy ( &ub );
	// make sure ends in \0
	sb.nullTerm();

	// set it
	char *needle = "Content-Length: ";
	int32_t needleLen = gbstrlen(needle);
	char *s = strstr(sb.getBufStart(),needle);
	s += needleLen;
	char c = s[4];
	sprintf(s,"%04"INT32"",contentLen);
	s[4] = c;
	

	// show it
	log("email: sending request to https://mandrillapp.com/ : %s",
	    sb.getBufStart() );

	// gotta get the cookie
	char *uu = "https://mandrillapp.com/";
	if ( ! g_httpServer.getDoc ( uu,
				     0, // ip
				     0                 , // offset
				     -1                , // size
				     false             , // m_ifModifiedSince
				     ei              , // state
				     gotMandrillReplyWrapper    , // 
				     60*1000           , // timeout
				     0                 , // m_proxyIp
				     0                 , // m_proxyPort
				     100*1024          , // m_maxTextDocLen
				     100*1024          , // m_maxOtherDocLen  
				     NULL,     // user agent
				     "HTTP/1.0" , //proto
				     true, // post?
				     NULL, // cookie
				     NULL, // additional header
				     sb.getBufStart() ) ) // full requesst
		return false;
	// must have been an error
	log("net: Got error getting page from mandrill: %s.",
	    mstrerror(g_errno));
	// ignore it
	g_errno = 0;
	// always call this at the end
	return true;
}
	
/////////////////////////////
//
// send two notifications, email and webhook
//
/////////////////////////////

void doneSendingNotifyEmailWrapper ( void *state ) {
	EmailInfo *ei = (EmailInfo *)state;
	ei->m_notifyBlocked--;
	// error?
	log("build: email notification status (count=%i) (ei=0x%"PTRFMT"): %s",
	    (int)ei->m_notifyBlocked,(PTRTYPE)ei,mstrerror(g_errno));
	// ignore it for rest
	g_errno = 0;
	// wait for post url to get done
	if ( ei->m_notifyBlocked > 0 ) return;
	// unmark it
	//ei->m_inUse = false;
	// all done
	ei->m_finalCallback ( ei->m_finalState );
	// nuke it
	mfree ( ei , sizeof(EmailInfo) ,"eialrt" );
}

void doneGettingNotifyUrlWrapper ( void *state , TcpSocket *sock ) {
	EmailInfo *ei = (EmailInfo *)state;
	ei->m_notifyBlocked--;
	// error?
	log("build: url notification status (count=%i) (ei=0x%"PTRFMT"): %s",
	    (int)ei->m_notifyBlocked,(PTRTYPE)ei,mstrerror(g_errno));
	// wait for email to get done
	if ( ei->m_notifyBlocked > 0 ) return;
	// unmark it
	//ei->m_inUse = false;
	// all done
	ei->m_finalCallback ( ei->m_finalState );
	// nuke it
	mfree ( ei , sizeof(EmailInfo) ,"eialrt" );
}

// for printCrawlDetailsInJson()
#include "PageCrawlBot.h"

// . return false if would block, true otherwise
// . used to send email and get a url when a crawl hits a maxToCrawl
//   or maxToProcess limitation.
bool sendNotification ( EmailInfo *ei ) {

	// disable for now
	//log("ping: NOT SENDING NOTIFICATION -- DEBUG!!");
	//return true;

	//if ( ei->m_inUse ) { char *xx=NULL;*xx=0; }

	// caller must set this, as well as m_finalCallback/m_finalState
	CollectionRec *cr = g_collectiondb.m_recs[ei->m_collnum];

	char *email = "";
	char *url   = "";
	char *crawl = "unknown2";

	if ( cr ) email = cr->m_notifyEmail.getBufStart();
	if ( cr ) url   = cr->m_notifyUrl.getBufStart();
	if ( cr ) crawl = cr->m_diffbotCrawlName.getBufStart();

	// sanity check, can only call once
	if ( ei->m_notifyBlocked != 0 ) { char *xx=NULL;*xx=0; }

	//ei->m_inUse = true;


	if ( email && email[0] ) {
		log("build: sending email notification to %s for "
		    "crawl \"%s\" : %s",
		    email,crawl,ei->m_spiderStatusMsg.getBufStart());
		SafeBuf msg;
		msg.safePrintf("Your crawl \"%s\" has a new status: %s"
			       , ei->m_spiderStatusMsg.getBufStart()
			       , crawl );

		// reset m_length otherwise it builds up
		ei->m_toAddress.reset();
		ei->m_toAddress.safeStrcpy ( email );
		ei->m_toAddress.nullTerm();
		
		// reset m_length otherwise it builds up
		ei->m_fromAddress.reset();
		ei->m_fromAddress.safePrintf("support@diffbot.com");

		/*
		ei->m_subject.safePrintf("crawl paused");
		ei->m_body.safePrintf("Your crawl for collection \"%s\" "
				      "has been paused because it hit "
				      "a maxPagesToCrawl or maxPagesToProcess "
				      "limitation."
				      , cr->m_coll);
		*/
		ei->m_state = ei;//this;
		ei->m_callback = doneSendingNotifyEmailWrapper;
		// this will usually block, unless error maybe
		if ( ! sendEmailThroughMandrill ( ei ) )
			ei->m_notifyBlocked++;
	}

	if ( url && url[0] ) {
		log("build: sending url notification to %s for coll \"%s\"",
		    url,crawl);

		Url uu; uu.set ( url );

		SafeBuf fullReq;
		fullReq.safePrintf("POST %s HTTP/1.0\r\n"
				   "User-Agent: Crawlbot/2.0\r\n"
				   "Accept: */*\r\n"
				   "Host: "
				   , uu.getPath()
				   );
		fullReq.safeMemcpy ( uu.getHost() , uu.getHostLen() );
		fullReq.safePrintf("\r\n");
		// make custom headers
		fullReq.safePrintf ("X-Crawl-Name: %s\r\n"
				    // last \r\n is added in HttpRequest.cpp
				    "X-Crawl-Status: %s\r\n" // hdrs
				    , cr->m_diffbotCrawlName.getBufStart()
				    , ei->m_spiderStatusMsg.getBufStart()
				    );
		// also in post body
		SafeBuf postContent;
		// the collection details
		printCrawlDetailsInJson ( &postContent , cr );
		// content-length of it
		fullReq.safePrintf("Content-Length: %"INT32"\r\n",
				   postContent.length());
		// type is json
		fullReq.safePrintf("Content-Type: application/json\r\n");
		fullReq.safePrintf("\r\n");
		// then the post content
		fullReq.safeMemcpy ( &postContent );
		fullReq.nullTerm();

		// GET request
		if ( ! g_httpServer.getDoc ( url ,
					     0 , // ip
					     0 , // offset
					    -1 , // size
					     false, // ifmodsince
					     ei,//this ,
					     doneGettingNotifyUrlWrapper ,
					     60*1000 , // timeout
					     0, // proxyip
					     0 , // proxyport
					     10000, // maxTextDocLen
					     10000, // maxOtherDocLen
					     "Crawlbot/2.0", // user agent
					     "HTTP/1.0", // proto
					     true , // doPost
					     NULL, // cookie
					     NULL , // custom hdrs
					     fullReq.getBufStart() ,
					     NULL ) )
			ei->m_notifyBlocked++;
	}

	if ( ei->m_notifyBlocked == 0 ) {
		//ei->m_inUse = false;
		// nuke it
		mfree ( ei , sizeof(EmailInfo) ,"eialrt" );
		return true;
	}

	// we blocked, wait
	return false;
}
