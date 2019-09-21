#include "gb-include.h"

#include "UdpServer.h"
#include "Dns.h"      // g_dnsDistributed.extractHostname()
#include "Threads.h"
#include "Profiler.h"
#include "Stats.h"
#include "Proxy.h"
#include "Process.h"
#include "Loop.h"

// . any changes made to the slots should only be done without risk of
//   interruption because makeCallbacks_ass() reads from the slots to call
//   callbacks, we don't want it reading garbage


// types.h uses key_t type that shmget uses
#undef key_t

int32_t g_dropped = 0;
int32_t g_corruptPackets = 0;
bool g_inHandler = false;

// main process id
static pid_t s_pid = 0;

// . making a hot udp server (realtime signal based)
// . caller calls to sendRequest() or sendReply() should turn off interrupts
//   before messing with our data
// . timepoll should turn off interrupts, too
// . we should call sigqueue if a callback needs to be made if we're hot


//#include <sys/ipc.h>  // shmget()
//#include <sys/shm.h>  // shmget()

// the philosophy for sending/receiving LARGE replies:
// 1. sender sends 1 dgram passed the last ack he got 
// 2. recevier sends ack back iff s_token is free
// 3. sender gets ack, claims his s_token, and blasts away
// 4. recevier gets dgrams and claims s_token if it's free
// rules:
// 1. you cannot send more than 1 dgram passed # of acks you got if you
//    do not have s_token
// 2. you can send up to the ack window limit if you have the token
// 3. local transactions should follow these rules NOW TODO!
// 4. you can only send an ack back for large reply if token is not in use
//    or you possess it
// problems:
// 1. if A needs to send to B. and B needs to send to C. and C to D.
//    if B sends to C. then A->B and C->D must wait. it's better if
//    A sends to B and C sends to D.  integer programming contest?


// a global class extern'd in .h file
UdpServer g_udpServer;

// this is the high priority udpServer, it's requests are handled first
UdpServer g_udpServer2;

// . how many dgrams constitute a big msg that should use the token system?
// . if msg use this or more dgrams, use the token system
// . i've effectively disabled the token scheme by setting this to 1000
// . seems like performace is better w/o tokens!!! damn it!!!!!!!!
//#define LARGE_MSG 4
//#define LARGE_MSG 6
//#define LARGE_MSG 100000
//#define LARGE_MSG 64

// . TODO: we should at least ack these guys even though s_token not for them
// . TODO: FIX! token can be stolen by local processes cuz we have no locking!
// . TODO: resendCount goes up without actually resending!!!
// . the startTime of the slot that has the token 
// . the last 4 bytes of the time of day in milliseconds
//static UdpSlot       **s_token;
//static uint32_t  *s_tokenTime;

// if we're doing a local transaction then keep our slot ptr here
// since s_token is shared mem
//static UdpSlot        *s_local;
//static uint32_t   s_localTime;


static void readPollWrapper_ass ( int fd , void *state ) ;
//static void sendPollWrapper_ass ( int fd , void *state ) ;
static void timePollWrapper     ( int fd , void *state ) ;
static void defaultCallbackWrapper ( void *state , UdpSlot *slot );

// used when sendRequest() is called with a NULL callback
void defaultCallbackWrapper ( void *state , UdpSlot *slot ) {
}

//#define SHMKEY  75
//#define SHMKEY2 76
//static void cleanup(int x);
//int shmid;
//int shmid2;

//void cleanup( int x) {
//	fprintf(stderr,"CALLED*****\n");
//	shmctl(shmid, IPC_RMID, 0);
//	exit(0);
//}

/*
bool UdpServer::setupSharedMem() {
	// clear out local slots
	//s_local      = NULL;
	//s_localTime  = 0;
	// . let's create shared memory segment to hold the token slot ptr
	// . only do this if we're the lowest #'d host on this machine
	// . get other hosts with this ip
	for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
		Host *h = g_hostdb.getHost (i);
		if ( h->m_ip     != g_hostdb.getMyIp() ) continue;
		if ( h->m_hostId <  g_hostdb.m_hostId    ) return true;
	}
	//for ( int32_t i = 0; i < 20; ++i) signal(i, cleanup);
	// if we made it here, we should create the shared memory
	//int shmid = shmget(SHMKEY,sizeof(UdpSlot *),0777|IPC_EXCL|IPC_CREAT);
	shmid  = shmget(SHMKEY ,sizeof(UdpSlot *),0777|IPC_CREAT);
	shmid2 = shmget(SHMKEY2,sizeof(UdpSlot *),0777|IPC_CREAT);
	if ( shmid < 0 || shmid2 < 0 ) 
		return log("udp:  setup: shmget: %s",mstrerror(errno));
	//log("new shmid  is %"INT32"",shmid );
	//log("new shmid2 is %"INT32"",shmid2);
	// init to NULL
	s_token     = (UdpSlot      **) shmat(shmid , 0, 0);
	s_tokenTime = (uint32_t *) shmat(shmid2, 0, 0);
	// ensure it's not NULL
	if (!s_token    )return log("udp: setupSharedMem: ptr  is NULL");
	if (!s_tokenTime)return log("udp: setupSharedMem: ptr2 is NULL");
	// no sender to us has the token yet
	*s_token     = NULL;
	// set time to 0
	*s_tokenTime = 0;
	// success
	return true;
}	

// . point s_token to shared mem set by process with lowest hostId on this ip
// . sleep until we get the shared mem in case we're waiting for the init host
bool UdpServer::useSharedMem() {
	while ( 1 == 1 ) {
		shmid  = shmget(SHMKEY ,sizeof(UdpSlot *),0777 );
		shmid2 = shmget(SHMKEY2,sizeof(UdpSlot *),0777 );
		// break sloop loop on success
		if ( shmid >= 0 && shmid2 >= 0 ) break;
		log("udp:  use: shmget: %s. sleeping.",mstrerror(errno));
		sleep(1);
	}
	//log("shmid  is %"INT32"",shmid );
	//log("shmid2 is %"INT32"",shmid2);
	// success!
	s_token     = (UdpSlot       **) shmat(shmid , 0, 0);
	s_tokenTime = (uint32_t  *) shmat(shmid2, 0, 0);
	// ensure it's not NULL
	if ( ! s_token ) return log("udp: useSharedMem: ptr is NULL");
	return true;
}
*/

// now redine key_t as our types.h should have it
#define key_t  u_int96_t

// free send/readBufs
void UdpServer::reset() {

	// sometimes itimer interrupt goes off in Loop.cpp when we are exiting
	// so fix that core. it happened when running the ./gb stop cmd b/c
	// it exited while in the middle of a udp handler, so g_callSlot
	// was non-null but invalid and the sigalrmhander() in Loop.cpp puked.
	g_callSlot = NULL;

	// clear our slots
	if ( ! m_slots ) return;
	log(LOG_DEBUG,"db: resetting udp server");
	mfree ( m_slots , m_maxSlots * sizeof(UdpSlot) , "UdpServer" );
	m_slots = NULL;
	if ( m_buf ) mfree ( m_buf , m_bufSize , "UdpServer");
	m_buf = NULL;
	/*
	// clear this
	m_isShuttingDown = false;
	// free send/read bufs 
	for ( int32_t i = 0 ; i <= m_topUsedSlot ; i++ ) {
		// skip empty nodes
		if ( isEmpty(i) ) continue;
		// get slot
		UdpSlot *slot = &m_slots[i];
		// . free bufs
		// . this may NOT be ours to free!!
		if ( slot->m_readBuf ) 
			mfree ( slot->m_readBuf,slot->m_readBufSize,"Udp");
		//slot->m_readBuf = NULL;
		// . a multicast may own this sendBuf and be sending it over
		//   2+ slots, so we should not free it!!!
		//if ( slot->m_sendBuf ) 
		//	mfree (slot->m_sendBuf,slot->m_sendBufAllocSize,"Udp");
	}
	// *s_token = NULL;
	  */
}

UdpServer::UdpServer ( ) {
	m_sock = -1;
	m_slots = NULL;
	m_maxSlots = 0;
	m_buf = NULL;
	m_outstandingConverts = 0;
	m_writeRegistered = false;
}

UdpServer::~UdpServer() {
	reset();
}

//static int32_t s_udpMem    = 0;

// . returns false and sets g_errno on error
// . port will be incremented if already in use
// . use 1 socket for recving and sending
// . niceness typically goes from 0 to 2, 0 being the highest priority
// . pollTime is how often to call timePollWrapper() (in milliseconds)
// . it should be at least the minimal slot timeout
bool UdpServer::init ( uint16_t port, UdpProtocol *proto, int32_t niceness,
		       int32_t readBufSize , int32_t writeBufSize , 
		       int32_t pollTime , int32_t maxSlots , bool isDns ){

	// save this
	m_isDns = isDns;
	// we now alloc so we don't blow up stack
	if ( m_slots ) { char *xx = NULL; *xx = 0; }
	//if ( maxSlots > MAX_UDP_SLOTS ) maxSlots = MAX_UDP_SLOTS;
	if ( maxSlots < 100           ) maxSlots = 100;
	m_slots =(UdpSlot *)mmalloc(maxSlots*sizeof(UdpSlot),"UdpServer");
	if ( ! m_slots ) return log("udp: Failed to allocate %"INT32" bytes.",
				    maxSlots*(int32_t)sizeof(UdpSlot));
	log(LOG_DEBUG,"udp: Allocated %"INT32" bytes for %"INT32" sockets.",
	     maxSlots*(int32_t)sizeof(UdpSlot),maxSlots);
	m_maxSlots = maxSlots;
	// dgram size
	log(LOG_DEBUG,"udp: Using dgram size of %"INT32" bytes.",
	    (int32_t)DGRAM_SIZE);
	// set up linked list of available slots
	m_head = &m_slots[0];
	for ( int32_t i = 0 ; i < m_maxSlots - 1 ; i++ )
		m_slots[i].m_next = &m_slots[i+1];
	m_slots [ m_maxSlots - 1].m_next = NULL;
	// the linked list of slots in use
	m_head2 = NULL;
	m_tail2 = NULL;
	// linked list of callback candidates
	m_head3 = NULL;
	// . set up hash table that converts key (ip/port/transId) to a slot
	// . m_numBuckets must be power of 2
	m_numBuckets = getHighestLitBitValue ( m_maxSlots * 6 );
	m_bucketMask = m_numBuckets - 1;
	// alloc space for hash table
	m_bufSize = m_numBuckets * sizeof(UdpSlot *);
	m_buf     = (char *)mmalloc ( m_bufSize , "UdpServer" );
	if ( ! m_buf ) return log("udp: Failed to allocate %"INT32" bytes for "
				  "table.",m_bufSize);
	m_ptrs = (UdpSlot **)m_buf;
	// clear
	memset ( m_ptrs , 0 , sizeof(UdpSlot *)*m_numBuckets );
	log(LOG_DEBUG,"udp: Allocated %"INT32" bytes for table.",m_bufSize);

	m_numUsedSlots   = 0;
	m_numUsedSlotsIncoming   = 0;
	// clear this
	m_isShuttingDown = false;
	// and this
	m_isSuspended    = false;
	// set up shared mem
	//if ( ! useSharedMem() ) return false;
	// . TODO: IMPORTANT: FIX this to read and save from disk!!!!
	// . NOTE: only need to fix if doing incremental sync/storage??
	m_nextTransId = 0;
	// clear handlers
	memset ( m_handlers, 0 , sizeof(void(* )(UdpSlot *slot,int32_t)) * 128);
	//memset ( m_droppedNiceness0 , 0 , sizeof(int32_t) * 128);
	//memset ( m_droppedNiceness1 , 0 , sizeof(int32_t) * 128);
        // save the port in case we need it later
        m_port    = port;
	// no requests waiting yet
	m_requestsInWaiting = 0;
	// special count
	m_msg07sInWaiting = 0;
	m_msg10sInWaiting = 0;
	m_msgc1sInWaiting = 0;
	//m_msgDsInWaiting = 0;
	//m_msg23sInWaiting = 0;
	m_msg25sInWaiting = 0;
	m_msg50sInWaiting = 0;
	m_msg39sInWaiting = 0;
	m_msg20sInWaiting = 0;
	m_msg2csInWaiting = 0;
	m_msg0csInWaiting = 0;
	m_msg0sInWaiting  = 0;
	// maintain a ptr to the protocol
	m_proto   = proto;
	// sanity test so we can peek at the rdbid in a msg0 request
	if( ! m_isDns &&
	    RDBIDOFFSET +1 > m_proto->getMaxPeekSize() ) {
		char *xx=NULL;*xx=0; }
	// set the main process id
	if ( s_pid == 0 ) s_pid = getpid();
	// remember our level of niceness
	//m_niceness = niceness;
	// don't allow negatives for other transactions, that's unmasked
	//if ( m_niceness < 0 ) m_niceness = 0;
	// are we real live?
	if ( niceness == -1 && g_isHot ) m_isRealTime = true;
	else                             m_isRealTime = false;
	// init slots array
	//m_topUsedSlot = -1;
	//memset ( m_keys , 0 , sizeof(key_t) * m_maxSlots );
        // set up our socket
        m_sock  = socket ( AF_INET, SOCK_DGRAM , 0 );

        if ( m_sock < 0 ) {
		// copy errno to g_errno
		g_errno = errno;
		return log("udp: Failed to create socket: %s.",
			   mstrerror(g_errno));
	}
        // sockaddr_in provides interface to sockaddr
        struct sockaddr_in name; 
        // reset it all just to be safe
        bzero((char *)&name, sizeof(name));
        name.sin_family      = AF_INET;
        name.sin_addr.s_addr = 0; /*INADDR_ANY;*/
        name.sin_port        = htons(port);
        // we want to re-use port it if we need to restart
        int options ;
	options = 1;
        if ( setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR,
			&options,sizeof(options)) < 0 ) {
		// copy errno to g_errno
		g_errno = errno;
		return log("udp: Call to  setsockopt: %s.",mstrerror(g_errno));
	}
	options = 1;
	// only do this if not dns!!! some dns servers require it and will
	// just drop the packets if it doesn't match, because this will make
	// it always 0
	// NO! we really need this now that we use roadrunner wirless which
	// has bad udp packet checksums all the time!
	//if ( ! m_isDns && setsockopt(m_sock, SOL_SOCKET, SO_NO_CHECK, 
	//		&options,sizeof(options)) < 0 ) {
	//	// copy errno to g_errno
	//	g_errno = errno;
	//	return log("udp: Call to  setsockopt: %s.",mstrerror(g_errno));
	//}
	// the lower the RT signal we use, the higher our priority

	// . before we start getting signals on this socket let's make sure
	//   we have a handler registered with the Loop class
	// . this makes m_sock non-blocking, too
	// . use the original niceness for this
	if ( ! g_loop.registerReadCallback ( m_sock,
					     this,
					     readPollWrapper_ass,
					     0 )) //niceness ) )
		return false;
	// . also register for writing to the socket, when it's ready
	// . use the original niceness for this, too
	// . what does this really mean? shouldn't we only use it
	//   when we try to write but the write buf is full so we have
	//   to try again later when it becomes unfull?
	// if ( ! g_loop.registerWriteCallback ( m_sock,
	// 					     this,
	// 					     sendPollWrapper_ass,
	// 					     0 )) // niceness ) )
	// 		return false;	
	// . also register for 30 ms tix (was 15ms)
        //   but we aren't using tokens any more so I raised it
	// . it's low so we can claim any unclaimed tokens!
        // . now resends are at 20ms... i'd go lower, but sigtimedqueue() only
        //   has a timer resolution of 20ms, probably due to kernel time slicin
	if ( ! g_loop.registerSleepCallback ( pollTime,
					      this,
					      timePollWrapper , 0 ))
		return false;	
	// . set the read buffer size to 256k for high priority socket
	//   so our indexlists don't have to be re-transmitted so much in case
	//   we delay a bit
	// . set after calling socket() but before calling bind() for tcp
	//   because of http://jes.home.cern.ch/jes/gige/acenic.html
	// . do these cmds on the cmd line as root for gigabit ethernet
	// . echo 262144 > /proc/sys/net/core/rmem_max
	// . echo 262144 > /proc/sys/net/core/wmem_max
	//if ( niceness == 0 ) opt = 2*1024*1024 ;
	// print the size of the buffers
	int opt = readBufSize;
	socklen_t optLen = 4;
	// set the buffer space
	if ( setsockopt ( m_sock , SOL_SOCKET , SO_RCVBUF , &opt , optLen ) ) 
		log("udp: Call to setsockopt (%d) failed: %s.",
		     opt,mstrerror(errno));
	opt = writeBufSize;
	if ( setsockopt ( m_sock , SOL_SOCKET , SO_SNDBUF, &opt , optLen ) )
		log("udp: Call to setsockopt (%d) failed: %s.",
		     opt,mstrerror(errno));
	// log the buffer sizes
	getsockopt( m_sock , SOL_SOCKET , SO_RCVBUF , &opt , &optLen );
	log(LOG_DEBUG,"udp: Receive buffer size is %i bytes.",opt);
	getsockopt( m_sock , SOL_SOCKET , SO_SNDBUF , &opt , &optLen );
	log(LOG_DEBUG,"udp: Send    buffer size is %i bytes.",opt);
        // bind this name to the socket
        if ( bind ( m_sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
		// copy errno to g_errno
		g_errno = errno;
                //if ( g_errno == EINVAL ) { port++; goto again; }
                close ( m_sock );
                return log("udp: Failed to bind to port %hu: %s.",
			     port,strerror(g_errno));
        }

	// get ip address of socket
	/*
	struct ifreq ifr;
	int fd = socket (AF_INET, SOCK_PACKET, htons (3050));
	ioctl ( fd , SIOCGIFADDR, &ifr );
	struct in_addr ip_source;	
	gbmemcpy (&ip_source, &((struct sockaddr_in *) &ifr.ifr_ifru.ifru_addr)->sin_addr, sizeof (struct sockaddr_in)); 
	log ("My IP address: %s\n", inet_ntoa (ip_source)); 
        //struct sockaddr_in *me = (sockaddr_in *)&ifr.ifr_ifru.ifru_addr;
	//struct in_addr *me = &((struct sockaddr_in *) 
	//		       &ifr.ifr_ifru.ifru_addr)->sin_addr;
	//log ("My IP address: %s", inet_ntoa (*me)); 
	uint32_t myip1 = 0; //me->sin_addr.s_addr;
	int32_t myip2 = g_hostdb.getHost ( g_hostdb.m_hostId )->m_ip;
	int32_t myip3 = g_hostdb.getHost ( g_hostdb.m_hostId )->m_externalIp;
	if ( myip1 != myip2 && myip1 != myip3 ) {
		log("conf: Ip address of machine, %s, does not agree "
		    "with ip address %s or %s in hosts.conf for hostId #%"INT32".",
		    iptoa(myip1),"foo","boo",
		    //iptoa(myip2),iptoa(myip3),
		    g_hostdb.m_hostId);
		//return false;
		sleep(10);
		exit(0);
	}
	*/

	// init stats
	m_eth0BytesIn    = 0LL;
	m_eth0BytesOut   = 0LL;
	m_eth0PacketsIn  = 0LL;
	m_eth0PacketsOut = 0LL;
	m_eth1BytesIn    = 0LL;
	m_eth1BytesOut   = 0LL;
	m_eth1PacketsIn  = 0LL;
	m_eth1PacketsOut = 0LL;

	// for packets coming in from other clusters usually for importing
	// link information
	m_outsiderPacketsIn  = 0LL;
	m_outsiderBytesIn    = 0LL;
	m_outsiderPacketsOut = 0LL;
	m_outsiderBytesOut   = 0LL;

	// log an innocent msg
	//log ( 0, "udp: listening on port %hu with sd=%"INT32" and "
	//      "niceness=%"INT32"", m_port, m_sock, m_niceness );
	log ( LOG_INIT, "udp: Listening on UDP port %hu with niceness=%"INT32" "
	      "and fd=%i.", 
	      m_port, niceness , m_sock );
	// print dgram sizes
	//log("udp:  using max dgram size of %"INT32" bytes", DGRAM_SIZE );
	return true;
}

// . use a backoff of -1 for the default
// . use maxWait of -1 for the default
// . returns false and sets g_errno on error
// . returns true on success
// . TODO: this is actually async signal safe... TODO: append _ass
bool UdpServer::sendRequest ( char     *msg          ,
			      int32_t      msgSize      ,
			      unsigned char    msgType      ,
			      uint32_t      ip           ,
			      uint16_t     port         ,
			      int32_t      hostId       ,
			      UdpSlot **retslot      , // can be NULL
			      void     *state        ,
			      void    (* callback)(void *state,UdpSlot *slot),
			      int32_t      timeout      , // in seconds
			      int16_t     backoff      ,
			      int16_t     maxWait      ,
			      char     *replyBuf     ,
			      int32_t      replyBufMaxSize ,
			      int32_t      niceness     ,
			      int32_t      maxResends   ) {
	// sanity check
	if ( ! m_handlers[msgType] && this == &g_udpServer &&
	     // proxy forwards the msg10 to a host in the cluster
	     ! g_proxy.isProxy() ) { 
		char *xx = NULL; *xx = 0; }
	// NULLify slot if any
	if ( retslot ) *retslot = NULL;
	// if shutting down return an error
	if ( m_isShuttingDown ) { 
		g_errno = ESHUTTINGDOWN; 
		return false; 
	}
	// ensure timeout ok
	if ( timeout < 0 ) { 
		//g_errno = EBADENGINEER;
		log(LOG_LOGIC,"udp: sendrequest: Timeout is negative. "
		    "Making 9999999.");
		timeout = 9999999;
		char *xx=NULL;*xx=0;
	}
	// if we're hot request size is limited
	if ( this == &g_udpServer2  && msgSize > TMPBUFSIZE ) {
		g_errno = EBUFTOOSMALL;
		return log(LOG_LOGIC,"udp: sendrequest: Request too big for "
			   "asynchronous udp server to read.");
	}
	// . we only allow niceness 0 or 1 now
	// . this niceness is only used for makeCallbacks_ass()
	if ( niceness > 1 ) niceness = 1;
	if ( niceness < 0 ) niceness = 0;
	// . don't allow interruption in here 
	// . we don't want ::process_ass() processing our new, half ready slot
	bool flipped = interruptsOff();
	// get a new transId
	int32_t transId = getTransId();

	// set up shotgunning for this hostId
	Host *h = NULL;
	uint32_t ip2 = ip;
	//if ( g_conf.m_useShotgun && hostId >= 0 ) {
	// . now we always set UdpSlot::m_host
	// . hostId is -1 when sending to a host in g_hostdb2 (hosts2.conf)
	if ( hostId >= 0 ) h = g_hostdb.getHost ( hostId );
	// get it from g_hostdb2 then via ip lookup if still NULL
	if ( ! h ) h = g_hostdb.getHost ( ip , port );
	// sanity check
	if ( h && ip && ip != (uint32_t)-1 && h->m_ip != ip &&
	     h->m_ipShotgun != ip && ip != 0x0100007f ) { // "127.0.0.1"
		log(LOG_LOGIC,"udp: provided hostid does not match ip");
		char *xx=NULL;*xx=0;
	}
	// ok, we are probably sending a dns request to a dns server...
	//if ( ! h ) { char *xx = NULL; *xx = 0; }
	// always use the primary ip for making the key, 
	// do not use the shotgun ip. because we can be getting packets
	// from either ip for the same transaction.
	if ( h ) ip2 = h->m_ip;

	// make a key for this new slot
	key_t key = m_proto->makeKey (ip2,port,transId,true/*weInitiated?*/);

	// . create a new slot to control the transmission of this request
	// . should set g_errno on failure
	UdpSlot *slot = getEmptyUdpSlot_ass ( key , false );
	if ( ! slot ) {
		if ( flipped ) interruptsOn();
		return log("udp: All %"INT32" slots are in use.",m_maxSlots);
	}
	// announce it
	if ( g_conf.m_logDebugUdp )
		log(LOG_DEBUG,
		    "udp: sendrequest: ip2=%s port=%"INT32" "
		    "msgType=0x%hhx msgSize=%"INT32" "
		    "transId=%"INT32" (niceness=%"INT32") slot=%"PTRFMT".",
		    iptoa(ip2),(int32_t)port,
		    (unsigned char)msgType, 
		    (int32_t)msgSize, 
		    (int32_t)transId, 
		    (int32_t)niceness ,
		    (PTRTYPE)slot );
	
	// . get time 
	// . returns g_now if we're in a signal handler
	int64_t now = gettimeofdayInMillisecondsLocal();
	// connect to the ip/port (udp-style: does not do much)
	slot->connect ( m_proto, ip, port, h, hostId, transId, timeout, now ,
			niceness );
	// . use default callback if none provided
	// . slot has a callback iff it's an outgoing request
	if ( ! callback ) callback = defaultCallbackWrapper;
	// set up for a send
	if ( ! slot->sendSetup( msg             ,
				msgSize         ,
				msg             ,
				msgSize         ,
				msgType         ,
				now             ,
				state           ,
				callback        ,
				niceness        , 
				backoff         , 
				maxWait         ,
				replyBuf        , 
				replyBufMaxSize ) ) {
		freeUdpSlot_ass ( slot );
		if ( flipped ) interruptsOn();
		return log("udp: Failed to initialize udp socket for "
			   "sending req: %s",mstrerror(g_errno));
	}

	if ( slot->m_next3 || slot->m_prev3 ) { char *xx=NULL;*xx=0; }
	// set this
	slot->m_maxResends = maxResends;
	// keep sending dgrams until we have no more or hit ACK_WINDOW limit
	if ( ! doSending_ass ( slot , true /*allow resends?*/ , now ) ) {
		freeUdpSlot_ass ( slot );
		if ( flipped ) interruptsOn();
		return log("udp: Failed to send dgrams for udp socket.");
	}
	// mark as used memory
	//s_udpMem += msgSize;

	// debug msg
	//int64_t  now = gettimeofdayInMilliseconds();
	//log("***added node #%"INT32", isTimedOut=%"INT32"\n",node,
	//slot->isTimedOut(now));
	// let caller know the slot if he wants to
	if ( retslot ) *retslot = slot;
	// debug msg
	//log("UdpServer added slot to send on, key={%"INT32",%"INT64"},"
	//"msgType=0x%hhx\n",
	//key.n1,key.n0, msgType );
	// turn 'em back on
	if ( flipped ) interruptsOn();
	// success
	return true;
}

// returns false and sets g_errno on error, true otherwise
void UdpServer::sendErrorReply ( UdpSlot *slot     , 
				 int32_t     errnum   , 
				 int32_t     timeout  ) {
	// not in sig handler
	//if ( g_inSigHandler ) return;
	// bitch if it is 0
	if ( errnum == 0 ) {
		log(LOG_LOGIC,"udp: sendErrorReply: errnum is 0.");
		char *xx = NULL; *xx = 0; 
	}
	// clear g_errno in case it was set
	g_errno = 0;
	// make a little msg
	char *msg = slot->m_tmpBuf; //(char *)mmalloc(4,"UdpServer");
	// make sure to destroy slot to free read/send bufs if this fails
	//if ( ! msg ) { 
	//	log("udp: sendErrorReply: %s",mstrerror(g_errno));
	//	destroySlot(slot); 
	//	return; 
	//}
	*(int32_t *)msg = htonl(errnum) ;
	// set the m_localErrno in "slot" so it will set the dgrams error bit
	slot->m_localErrno = errnum;
	sendReply_ass ( msg , 4 , msg , 4 , slot , timeout );
}

// . destroys slot on error or completion (frees m_readBuf,m_sendBuf)
// . use a backoff of -1 for the default
void UdpServer::sendReply_ass ( char    *msg        ,
				int32_t     msgSize    ,
				char    *alloc      ,
				int32_t     allocSize  ,
				UdpSlot *slot       ,
				int32_t     timeout    , // in seconds
				void    *state      ,
				void (* callback2)(void *state, UdpSlot *slot),
				int16_t    backoff    ,
				int16_t    maxWait    ,
				bool     isCallback2Hot ,
				bool     useSameSwitch  ) {
	// the callback should be NULL
	if ( slot->m_callback ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"udp: sendReply_ass: Callback is non-NULL.");
		return;
	}
	if ( ! msg && msgSize > 0 )
		log("udp: calling sendreply with null send buffer and "
		    "positive size! will probably core.");
	// record some statistics on how long these msg handlers are taking
	int64_t now = gettimeofdayInMillisecondsLocal();
	// m_queuedTime should have been set before m_handlers[] was called
	int32_t delta = now - slot->m_queuedTime;
	int32_t n = slot->m_niceness;
	if ( n < 0 ) n = 0;
	if ( n > 1 ) n = 1;
	// add to average, this is now the reply GENERATION, not handler time
	g_stats.m_msgTotalOfHandlerTimes [slot->m_msgType][n] += delta;
	g_stats.m_msgTotalHandlersCalled [slot->m_msgType][n]++;
	// bucket number is log base 2 of the delta
	if ( delta > 64000 ) delta = 64000;
	int32_t bucket = getHighestLitBit ( (uint16_t)delta );
	// MAX_BUCKETS is probably 16 and #define'd in Stats.h
	if ( bucket >= MAX_BUCKETS ) bucket = MAX_BUCKETS-1;
	g_stats.m_msgTotalHandlersByTime [slot->m_msgType][n][bucket]++;
	// we have to use a different clock for measuring how long to
	// send the reply now
	slot->m_queuedTime = now;


	// . get hostid from slot so we can shotgun the reply back
	// . but if sending a ping reply back for PingServer, he wants us
	//   to use the shotgun port iff he did, and not if he did not.
	//   so just make sure slot->m_host is NULL so we send back to the same
	//   ip/port that sent to us.
	//if ( g_conf.m_useShotgun && ! useSameSwitch )
	// now we always set m_host, we use s_shotgun to toggle
	slot->m_host = g_hostdb.getHost ( slot->m_ip , slot->m_port );
	//else slot->m_host = NULL;

	// discount this
	if ( slot->m_convertedNiceness == 1 && slot->m_niceness == 0 ) {
		// note it
		if ( g_conf.m_logDebugUdp )
			log("udp: unconverting slot=%"PTRFMT"",
			    (PTRTYPE)slot);
		// go back to niceness 1 for sending back, otherwise their
		// the callback will be called with niceness 0!!
		//slot->m_niceness = 1;
		slot->m_convertedNiceness = 2;
		m_outstandingConverts--;
	}

	// if msgMaxSize is -1 use msgSize
	//if ( msgMaxSize == -1 ) msgMaxSize = msgSize;
	// . turn off interrupts to be safe
	// . unless we're in a sighandler or they're already off
	bool flipped = interruptsOff();
	// use the msg type that's already in there
	unsigned char msgType = slot->getMsgType();
	// get time 
	//int64_t now = gettimeofdayInMilliseconds();
	// . use a NULL callback since we're sending a reply
	// . set up for a send
	if ( ! slot->sendSetup ( msg        ,
				 msgSize    ,
				 alloc      ,
				 allocSize  ,
				 msgType    ,
				 now        ,
				 NULL       ,
				 NULL       ,
				 slot->m_niceness   , 
				 backoff    ,
				 maxWait    ,
				 NULL       , 
				 0          ) ) {
		log("udp: Failed to initialize udp socket for sending "
		    "reply: %s", mstrerror(g_errno));
		mfree ( alloc , allocSize , "UdpServer");
		if ( flipped ) interruptsOn();
		// was EBADENGINEER
		if ( ! g_inSigHandler ) sendErrorReply ( slot , g_errno);
		return ;
	}
	// set the callback2 , it might not be NULL if we're recording stats
	// OR we need to call Msg21::freeBandwidth() after sending
	slot->m_state          = state;
	slot->m_callback2      = callback2;
	slot->m_isCallback2Hot = isCallback2Hot;
	// set this
	slot->m_maxResends = -1;
	// log it
	if ( g_conf.m_logDebugUdp )
		log("udp: Sending reply transId=%"INT32" msgType=0x%hhx "
		     "(niceness=%"INT32").", slot->m_transId,msgType,
		    (int32_t)slot->m_niceness);
	// mark as used memory
	//s_udpMem += msgMaxSize;
	// keep sending dgrams until we have no more or hit ACK_WINDOW limit
	if ( ! doSending_ass ( slot , true /*allow resends?*/, now) ) {
		// . on error deal with that
		// . errors from doSending() are from 
		//   UdpSlot::sendDatagramOrAck()
		//   which are usually errors from sendto() or something
		// . TODO: we may have to destroy this slot ourselves now...
		log("udp: Got error sending dgrams.");
		// destroy it i guess
		destroySlot ( slot );
	}
	// back to it
	if ( flipped ) g_loop.interruptsOn();
	// status is 0 if this blocked
	//if ( status == 0 ) return;
	// destroy slot on completion of send or on error
	// mdw destroySlot ( slot );
	// return if send completed
	//if ( status != -1) return;
	// make a log note on send failure
	//return true;
}

// . this wrapper is called when m_sock is ready for writing
// . should only be called by Loop.cpp since it calls callbacks
// . should only be called if in an interrupt or interrupts are off!!
void sendPollWrapper_ass ( int fd , void *state ) { 
	UdpServer *THIS  = (UdpServer *)state;
	// begin the read/send/callback loop
	//THIS->process_ass ( g_now );
	THIS->sendPoll_ass ( true , g_now );
}

// . returns false and sets g_errno on error, true otherwise
// . will send an ACK or dgram
// . you need to occupy s_token  to do large reads/sends on a slot
// . this is called by sendRequest() which is not async safe
//   and by sendPollWrapper_ass()
// . that means we can be calling doSending() on a slot made in
//   sendRequest() and then be interrupted by sendPollWrapper_ass()
// . Fortunately, we have a lock around it in sendRequest()!
bool UdpServer::doSending_ass (UdpSlot *slot,bool allowResends,int64_t now) {

	// if UdpServer::cancel() was called and this slot's callback was
	// called, make sure to hault sending if we are in a quickpoll
	// interrupt...
	if ( slot->m_calledCallback ) {
		log("udp: trying to send on called callback slot");
		return true;
	}

	// . turn off interrupts to be safe
	// . unless we're in a sighandler or they're already off
	bool flipped = interruptsOff();
	// get time
	//int64_t now = gettimeofdayInMilliseconds();
	// . TODO: why this bug?
	// . before we had dead lock, I guess *s_tokenTime was fucked up
	// . this will ensure that it doesn't happend again
	/*
	uint32_t now2 = (uint32_t) now;
	if ( *s_token && now2 < *s_tokenTime && *s_tokenTime - now2 > 5000 ) 
		*s_token = NULL;
	if ( *s_token && now2 > *s_tokenTime && now2 - *s_tokenTime > 5000 ) 
		*s_token = NULL;
	*/
 loop:
	int32_t status = 0;
	// . don't do any sending until we leave the wait state
	// . we may get suspended at ANY time since suspender is HOT
	if ( m_isSuspended ) return true;

	// if it is suspended, don't allow any thru except Msg0's that are
	// sending replies because they need permission from Msg21 to do that
	// and they might deadlock with this permission token if we suspend 
	// them. HACK!
	//if ( m_isSuspended ) goto done;
	//     slot->m_msgType != 0x00 ) goto done;
	//     slot->m_msgType != 0x30 ) return true;

	// . if the score of this slot is -1, don't send on it!
	// . this now will allow one dgram to be resent even if we don't
	//   have the token
	/*
	if ( ! s_local ) {
		if ( slot->getScore(now,*s_token,*s_tokenTime,LARGE_MSG) < 0 ) 
			return true;
	}
	else {
		if ( slot->getScore(now, s_local, s_localTime,LARGE_MSG) < 0 ) 
			return true;
	}
	*/
	//int32_t score = slot->getScore(now);
	//log("score is %"INT32"", score);
	if ( slot->getScore(now) < 0 ) goto done;
	//if ( score < 0 ) return true;
	// . returns -2 if nothing to send, -1 on error, 0 if blocked, 
	//   1 if sent something
	// . it will send a dgram or an ACK
	status = slot->sendDatagramOrAck ( m_sock , allowResends , now );
	// return 1 if nothing to send
	if ( status == -2 ) goto done;
	// return -1 on error
	if ( status == -1 ) {
		log("udp: Had error sending dgram: %s.",mstrerror(g_errno));
		goto done;
	}
	// return 0 if we blocked on this dgram
	if ( status ==  0 ) {
		// but Loop should call us again asap because I don't think
		// we'll get a ready to write signal... don't count on it
		m_needToSend = true;
		// ok, now it should
		if ( ! m_writeRegistered ) {
			g_loop.registerWriteCallback ( m_sock,
						       this,
						       sendPollWrapper_ass,
						       0 ); // niceness
			m_writeRegistered = true;
		}
		goto done;
	}
	// otherwise keep looping, we might be able to send more
	goto loop;
	// come here to turn the interrupts back on if we turned them off
 done:
	if ( flipped ) interruptsOn();
	if ( status == -1 ) return false;
	return true;
}

// . should only be called from process_ass() since this is not re-entrant
// . sends all the awaiting dgrams it can
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . tries to send msgs that are the "most caught up" to their ACKs first
// . call the callback of slots that are TIMEDOUT or get an error!
// . verified that this is not interruptible
// . MDW: THIS IS NOW called by Loop.cpp when our udp socket is ready for
//   sending on, and a previous sendto() would have blocked.
bool UdpServer::sendPoll_ass ( bool allowResends , int64_t now ) {
	// . turn off interrupts to be safe
	// . unless we're in a sighandler or they're already off
	bool flipped = interruptsOff();
	// just so caller knows we don't need to send again yet
	m_needToSend = false;
	// if we don'thave anything to send, or we're waiting on ACKS, then
	// just return false, we didn't do anything.
	//mdw int32_t status;
	// assume we didn't process anything
	bool something = false;
 getNextSlot:
	// . don't do any sending until we leave the wait state
	// . we can be suspended in the middle of this loop by a HOT high
	//   priority server
	if ( m_isSuspended ) return false;
	// or if is shutting down
	if ( m_isShuttingDown ) return false;
	// . get the next slot to send on
	// . it sets "isResend" to true if it's a resend
	// . this sets g_errno to ETIMEOUT if the slot it returns has timed out
	// . in that case we'll destroy that slot
	UdpSlot *slot = getBestSlotToSend ( now );
	// . slot is NULL if no more slots need sending
	// . return true if we processed something
	if ( ! slot ) {
		if ( flipped ) interruptsOn();
		// if nobody needs to send now unregister write callback
		// so select() loop in Loop.cpp does not keep freaking out
		if ( ! m_needToSend && m_writeRegistered ) {
			g_loop.unregisterWriteCallback(m_sock,
						       this,
						       sendPollWrapper_ass);
			m_writeRegistered = false;
		}
		return something;
	}
	// otherwise, we can send something
	something = true;
	// . if this slot timed out because we haven't written a reply yet
	//   then DO NOT call the callback again, just wait for the handler
	//   to timeout and send a reply
	// . otherwise, you'll just keep looping the same request to the
	//   same handler and cause problems (mdw)
	// if timed out then nuke it
	//if ( g_errno == ETIMEDOUT ) goto slotDone;
	// . tell slot to send a datagram OR ACK for us
	// . returns -2 if nothing to send, -1 on error, 0 if blocked, 
	//   1 if sent something
	//if(slot->sendDatagramOrAck (m_sock, true, m_niceness) == 0 ) return ;
	// . send all we can from this slot
	// . when shutting down during a dump we can get EBADF during a send
	//   so do not loop forever
	// . this returns false on error, i haven't seen it happen though
	if ( ! doSending_ass ( slot , allowResends , now ) ) return true;
	// if the send
	// return if it blocked
	//mdw if ( status == 0  ) return;
	// if it had an error then nuke it
	// if ( status == -1 ) goto slotDone;
	// . otherwise, it sent a dgram or an ACK
	// . if the transaction is now completed then call callbacks
	// . if not, keep looping
	// if ( ! slot->isTransactionComplete() ) goto getNextSlot;
	//mdw slotDone:
	// . MAY make callback
	// . this MAY call destroy the "slot"
	// . this may also free up s_token so another can send
	// . this will just queue a signal for GB_SIGRTMIN + 1 queue if 
	//   g_inSigHandler is true
	//makeCallback_ass ( slot );
	// reset g_errno in case callback set it
	//g_errno = 0;
	// keep looping
	goto getNextSlot;
}

// . returns NULL if no slots need sending
// . otherwise returns a slot
// . slot may have dgrams or ACKs to send
// . sets g_errno to ETIMEDOUT if that slot is timed out as well as set
//   that slot's m_doneSending to true
// . let's send the int16_test first, but weight by how long it's been waiting!
// . f(x) = a*(now - startTime) + b/msgSize
// . verified that this is not interruptible
UdpSlot *UdpServer::getBestSlotToSend ( int64_t now ) {
	// . we send msgs that are mostly "caught up" with their acks first
	// . the slot with the lowest score gets sent
	// . re-sends have priority over NONre-sends(ACK was not recvd in time)
	int32_t     maxScore = -1;
	UdpSlot *maxi     = NULL;
	int32_t     score;  
	//UdpSlot *slot;
  	// . we send dgrams with the lowest "score" first
	// . the "score" is just number of ACKs you're waiting for
	// . that way transmissions that are the most caught up to their ACKs
	//   are considered faster so we send to them first
	// . we set the hi bit in the score for non-resends so dgrams that 
	//   are being resent take precedence
	for ( UdpSlot *slot = m_head2 ; slot ; slot = slot->m_next2 ) {
		// continue if slot empty
		//if ( isEmpty(i) ) continue;
		// get the ith slot
		//slot = &m_slots[i];
		// if we're suspended, only allow Msg0 slots
		//if ( m_isSuspended ) continue;
		//     slot->m_msgType != 0x00 ) continue;
		//     slot->m_msgType != 0x30  ) continue;
		// . we don't allow time out on slots waiting for us to send
		//   stuff, because we'd just end up calling the handler
		//   too many times. we could invent a "stop" cmd or something.
		// . mdw
		// if it's timedout then nuke it
		//if ( slot->isTimedOut(now) ) {
		//g_errno = ETIMEDOUT;
		//return slot;
		//}
		// . how many acks are we currently waiting for from dgrams
		//   that have already been sent?
		// . can be up to ACK_WINDOW_SIZE (16?).
		// . we're a "Fastest First" (FF) protocol stack.
		/*
		if ( ! s_local )
			score = slot->getScore (now, *s_token,*s_tokenTime,
						LARGE_MSG );
		else
			score = slot->getScore (now,  s_local, s_localTime,
						LARGE_MSG );
		*/
		score = slot->getScore ( now );
		// a negative score means it's not a candidate
		if ( score < 0 ) continue;
		// see if we're a winner
		if ( score > maxScore ) { maxi = slot; maxScore = score; }
	}
	// if nothing left to send return NULL cuz we didn't do anything
	//if ( ! maxi ) return NULL;
	// return the winning slot
	return maxi;
}

// . must give level of niceness for continuing the transaction at that lvl
bool UdpServer::registerHandler ( unsigned char msgType , 
				  void (* handler)(UdpSlot *, int32_t niceness) ,
				  bool isHandlerHot ){
	if ( m_handlers[msgType] ) 
	   return log(LOG_LOGIC,"udp: msgType %02x already in use.",msgType);
	// we now support types 0x00 to 0xff
	//if ( msgType >= 0x40 ) {
	//	log(LOG_LOGIC,"udp: msg type must be <= 0x3f.");
	//	char *xx = NULL; *xx = 0;
	//}
	m_handlers     [ msgType ] = handler; 
	m_isHandlerHot [ msgType ] = isHandlerHot;
	return true;
}

// . read and send as much as we can before calling any callbacks
// . if forceCallbacks is true we call them regardless if we read/sent anything
void UdpServer::process_ass ( int64_t now , int32_t maxNiceness) {
	// bail if no main sock
	if ( m_sock < 0 ) return ;

	//log("process_ass");

	// if we call this while in the sighandler it crashes since
	// gettimeofdayInMillisecondsLocal() is not async safe
	int64_t startTimer;
	if ( ! g_inSigHandler )
		startTimer = gettimeofdayInMillisecondsLocal();
 bigloop:
	// . if we're real time, and not in a sig handler, turn 'em off
	// . readSock() and doSending() are not Async Signal Safe (ass)
	bool flipped = interruptsOff();
	bool needCallback = false;
 loop:
	// did we read or send something?
	bool something = false;
	// a common var
	UdpSlot *slot;
	// read loop
 readAgain:
	// bail if no main sock, could have been shutdown in the middle
	if ( m_sock < 0 ) return ;
	// . returns -1 on error, 0 if blocked, 1 if completed reading dgram
	// . *slot is set to the slot on which the dgram was read
	// . *slot will be NULL on some errors (read errors or alloc errors)
	// . *slot will be NULL if we read and processed a slotless ACK
	// . *slot will be NULL if we read nothing (0 bytes read & 0 returned)
	int32_t status = readSock_ass ( &slot , now ) ;
	// if we read something
	if ( status != 0 ) {
		// if no slot was set, it was a slotless read so keep looping
		if ( ! slot ) { g_errno = 0; goto readAgain; }
		// if there was a read error let makeCallback() know about it
		if ( status == -1 ) {
			slot->m_errno = g_errno;
			// prepare to call the callback by adding it to this
			// special linked list
			if ( g_errno )
				addToCallbackLinkedList ( slot );
			// sanity
			if ( ! g_errno )
				log("udp: missing g_errno from read error");
		}
		// we read something
		something = true;
		// try sending an ACK on the slot we read something from
		doSending_ass ( slot , false , now );
	}
	// if we read something, try for more
	if ( something ) { 
		//if ( slot->m_errno || slot->isTransactionComplete())
		//log("got something");
		needCallback = true; 
		goto loop; 
	}
	// if we read nothing this round, reinstate interrupts
	if ( flipped ) interruptsOn();
	// if we don't need a callback, bail
	if ( ! needCallback ) {
		if ( m_needBottom ) goto callBottom;
		else              return;
	}
	// . set flag to call low priority callbacks 
	// . need to force it on here because makeCallbacks_ass() may
	//   return false when there are only low priority (high niceness)
	//   callbacks to call...
	m_needBottom = true;
	// . TODO: if we read/sent nothing don't bother calling callbacks
	// . call any outstanding callbacks
	// . now we have a niceness bit in the dgram header. if set, those 
	//   callback will only be called after all unset dgram's callbacks are
	// . this returns true if we called one
	if ( makeCallbacks_ass ( /*niceness level*/ 0 ) ) {
		// set flag to call low priority callbacks 
		m_needBottom = true;
		// note it
		//log("made callback");
		// but not now, only when we don't call any high priorities
		goto bigloop;
	}
 callBottom:
	if(maxNiceness < 1) return;
	// if we call this while in the sighandler it crashes since
	// gettimeofdayInMillisecondsLocal() is not async safe
	int64_t elapsed = 0;
	if ( ! g_inSigHandler )
	 	elapsed = gettimeofdayInMillisecondsLocal() - startTimer;
	if(elapsed < 10) {
		// we did not call any, so resort to nice callbacks
		// . only go to bigloop if we called a callback
		if ( makeCallbacks_ass ( /*niceness level*/ 1 ) )
			goto bigloop;
		// no longer need to be called
		// if we did anything loop back up
		// . but only if we haven't been looping forever,
		// . if so we need to relinquish control to loop.
		// 		log(LOG_WARN, "udp: give back control. after %"INT64"", 
		// 		    elapsed);
		//goto bigloop;	
	}
	else {
		m_needBottom = true;
		//g_loop.m_needToPoll = true;
	}
}

// . this wrapper is called when the Loop class has found that m_sock
//   needs to be read from (it got a SIGIO/GB_SIGRTMIN signal for it)
// . should only be called if in an interrupt or interrupts are off!!
void readPollWrapper_ass ( int fd , void *state ) { 
	// let everyone we're in a sigHandler
	UdpServer *THIS  = (UdpServer *)state;
	// begin the read/send/callback loop
	THIS->process_ass ( g_now );
}

// . reads everything from the network card
// . then sends everything it can
// . should only be called from process_ass() since this is not re-entrant
// . verified that this is not interruptible
/*
bool UdpServer::readPoll ( int64_t now ) {
	// if m_sock shutdown, don't bother
	if ( m_sock < 0 ) return false;
	// a common var
	UdpSlot *slot;
	// . returns -1 on error, 0 if blocked, 1 if completed reading dgram
	// . *slot is set to the slot on which the dgram was read
	// . *slot will be NULL on some errors (read errors or alloc errors)
	// . *slot will be NULL if we read and processed a slotless ACK
	// . *slot will be NULL if we read nothing (0 bytes read & 0 returned)
	int32_t status ;
	// assume we didn't process anything
	bool something = false;
	// do the main read loop
	while ( ( status = readSock ( &slot , now ) ) != 0 ) {
		// if no slot was set, it was a slotless read so keep looping
		if ( ! slot ) { g_errno = 0; continue; }
		// if there was a read error let makeCallback() know about it
		if ( status == -1 ) slot->m_errno = g_errno;
		// we read something
		something = true;
	}
	// return true if we processed something
	return something;
	// if no change in token states, don't bother sending more
	//if ( save == *s_token && save2 == s_local ) return;
	// . if one is not available, bail
	// . only one can be claimed at a time
	//if ( *s_token || s_local ) return;
	// maybe we can get the token now that's it's free
	//sendPoll(true);
}
*/

void UdpServer::dumpdgram ( char *dgram , int32_t dgramSize ) {
	for ( int32_t i = 0 ; i < dgramSize ; i++ ) 
		log(LOG_INFO,"%"INT32") %"INT32"\n",i,(int32_t)dgram[i]);
}
	
// . returns -1 on error, 0 if blocked, 1 if completed reading dgram
int32_t UdpServer::readSock_ass ( UdpSlot **slotPtr , int64_t now ) {
	// turn em off
	bool flipped = interruptsOff();
	// NULLify slot
	*slotPtr = NULL;
	// now peek at the first few bytes of the dgram to get some info
	char        peek[32]; 
	sockaddr_in from;
	socklen_t fromLen = sizeof ( struct sockaddr );
	// how many bytes should we peek at to get basic info about the msg
	int32_t maxPeekSize = m_proto->getMaxPeekSize();
	// watch out for overflow
	//if ( maxPeekSize > 32 ) maxPeekSize = 32;
	// peak so we can read directly into the right slot, zero-copy
	int peekSize = recvfrom ( m_sock            , 
				  peek              , 
				  maxPeekSize       ,
				  MSG_PEEK          ,
				  (sockaddr *)&from , 
				  &fromLen          );	

	// note it
	if ( g_conf.m_logDebugLoop )
		log("loop: readsock_ass: peekSize=%i m_sock/fd=%i",
		    peekSize,m_sock);

	//static int s_ss = 0;

	// cancel silly g_errnos and return 0 since we blocked
	if ( peekSize < 0 ) {
		g_errno = errno;
		if ( flipped ) interruptsOn();
		if ( g_errno == EAGAIN || g_errno == 0 ) { 
			// if ( s_ss++ == 100 ) {
			// 	log("foo");char *xx=NULL;*xx=0; }
			// log("udp: EAGAIN");
			g_errno = 0; return 0; }
		if ( g_errno == EILSEQ ) { 
			g_errno = 0; return 0; }
		// Interrupted system call (4) (from valgrind)
#ifdef _VALGRIND_
		if ( g_errno == 4 ) { g_errno = 0; return 0;}
#endif		
		return log("udp: readDgram: %s (%d).",mstrerror(g_errno), g_errno) - 1;
	}
	// the discard buffer, for reading dgram into
	char tmpbuf [DGRAM_SIZE_CEILING];
	uint32_t ip2 ;
	Host    *h        ;
	key_t    key      ;
	UdpSlot *slot     ;
	int32_t     dgramNum ;
	bool     wasAck   ;
	int32_t     transId  ;
	bool     discard  = true;
	bool     status   ;
	int32_t     readSize ;
	unsigned char msgType  ;
	int32_t          niceness ;

	// get the ip
	uint32_t ip = from.sin_addr.s_addr;
	// if it's 127.0.0.1 then change it to our ip
	if ( ip == g_hostdb.getLoopbackIp() ) ip = g_hostdb.getMyIp();
	// is it local?
	bool isLocal = false;
	// int16_tcut
        uint8_t *p = (uint8_t *)&ip;
        // this is local
        if ( p[0] == 10 ) isLocal = true;
        // this is local
        if ( p[0] == 192 && p[1] == 168 ) isLocal = true;
        // if we match top two ips, then its local
	if ( (ip&0x0000ffff) == (g_hostdb.m_myIp&0x0000ffff)) isLocal = true;
	// . if ip is not from a host in hosts.conf, discard it
	// . don't bother checking for dns server, who knows where that is
	// . now also allow all admin ips
	else if ( m_proto->useAcks() &&
		  ! isLocal &&
		  ! g_hostdb.isIpInNetwork ( ip ) &&
		  ! g_conf.isMasterIp ( ip ) &&
		  ! g_hostdb2.isIpInNetwork ( ip ) &&
		  ! g_conf.isConnectIp ( ip ) ) {
		// bitch, wait at least 5 seconds though
		static int32_t s_lastTime = 0;
		static int64_t s_count = 0LL;
		s_count++;
		if ( getTime() - s_lastTime > 5 ) {
			s_lastTime = getTime();
			log("udp: Received unauthorized udp packet from %s. "
			    "Count=%"INT64".",iptoa(ip),s_count);
		}
		// make it return 1 cuz we did read something
		status = true;
		// not an ack? assume not
		wasAck = false;
		// assume no shotgun
		h      = NULL;
		// discard it
		discard = true;
		// read it into the temporary discard buf
		goto discard;
	}
	// get hostid of the ip, use that instead of ip to make the key
	// since shotgunning may change the ip
	ip2 = ip;
	// i modified Hostdb::hashHosts() to hash the loopback ip now!
	h   = g_hostdb.getHost ( ip , ntohs(from.sin_port) );
	// . just use ip for hosts from hosts2.conf
	// . because sendReques() usually gets a hostId of -1 when sending
	//   to a host in hosts2.conf and therefore makeKey() initially uses
	//   the ip address of the hosts2.conf host
	//if ( h && h->m_hostdb != &g_hostdb ) h = NULL;
	// probably a reply from a dns server?
	//if ( ! h ) { char *xx = NULL; *xx = 0; }
	// always use the primary ip for making the key, 
	// do not use the shotgun ip. because we can be getting packets
	// from either ip for the same transaction. h can be NULL if the packet
	// is from a dns server.
	if ( h ) ip2 = h->m_ip;
	//logf(LOG_DEBUG,"net: got h=%"UINT32"",(int32_t)h);
	// generate a unique KEY for this TRANSACTION 
	key = m_proto->makeKey ( peek                  , 
				 peekSize              ,
				 //from.sin_addr.s_addr, // network order
				 ip2                   , // ip        ,
				 //ip                  , // network order
				 ntohs(from.sin_port)  );// host order
	// get the corresponding slot for this key, if it exists
	slot = getUdpSlot ( key );
	// get the dgram number on this dgram
	dgramNum = m_proto->getDgramNum ( peek , peekSize );
	// was it an ack?
	wasAck   = m_proto->isAck       ( peek , peekSize );
	// everybody has a transId
	transId  = m_proto->getTransId  ( peek , peekSize );
	// other vars we'll use later
	discard = true;
	status  = true;
	// if we don't already have a slot set up for it then it can be:
	// #1) a new incoming request
	// #2) a reply we ACKed but it didn't get our ACK and we've closed
	// #3) a stray ACK???
	// #4) a reply but we timed out and our slot is gone
	msgType  = m_proto->getMsgType ( peek , peekSize );
	niceness = m_proto->isNice     ( peek , peekSize );
	// general count
	if ( niceness == 0 ) {
		g_stats.m_packetsIn[msgType][0]++;
		if ( wasAck ) g_stats.m_acksIn[msgType][0]++;
	}
	else {
		g_stats.m_packetsIn[msgType][1]++;
		if ( wasAck ) g_stats.m_acksIn[msgType][1]++;
	}
	// if we're shutting down do not accept new connections, discard
	if ( m_isShuttingDown ) goto discard; 
	if ( ! slot ) {
		// condition #3
		if ( wasAck ) {
			if ( g_conf.m_logDebugUdp )
				log(LOG_DEBUG,
				    "udp: Read stray ACK, transId=%"INT32", "
				    "ip2=%s "
				    "port=%"INT32" "
				    "dgram=%"INT32" "
				    "dst=%s:%hu "
				    "k.n1=%"UINT32" n0=%"UINT64".",
				    transId,
				    iptoa(ip2),
				    (int32_t)ntohs(from.sin_port) ,
				    dgramNum,
				    iptoa(ip)+6,
				    (uint16_t)ntohs(from.sin_port),
				    key.n1,key.n0);
			// tmp debug
			//char *xx = NULL; *xx = 0;
			//return 1;
			goto discard;
		}
		// condition #2
		if ( m_proto->isReply ( peek , peekSize ) ) {
			// if we don't use ACK then do nothing!
			if ( ! m_proto->useAcks () ) {
				// print out the domain in the packet
				/*
				char tmp[512];
				g_dnsDistributed.extractHostname(header,dgram+12,tmp);
				// iptoa not async sig safe
				if ( ! g_inSigHandler ) 
					log("udp: dns reply too late "
					     "or reply from a resend "
					     "(host=%s,dnsip=%s)",
					     tmp, iptoa(ip)); 
				*/
				log(LOG_REMIND,"dns: Dns reply too late "
				     "or reply from a resend.");
				//return 1; 
				goto discard;
			}
			// . if they didn't get our ACK they might resend to us
			//   even though we think the transaction is completed
			// . either our send is slow or their read buf is slow
			// . to avoid these msg crank up the resend time
			// . Multicast likes to send you AND your groupees
			//   the same request, take the first reply it gets
			//   and dump the rest, this is probably why we get 
			//   this often
			if ( g_conf.m_logDebugUdp )
				log(LOG_DEBUG,
				    "udp: got dgram we acked, but we closed, "
				    "transId=%"INT32" dgram=%"INT32" dgramSize=%i "
				    "fromIp=%s fromPort=%i msgType=0x%hhx",
				    transId, dgramNum , peekSize,
				    iptoa((int32_t)from.sin_addr.s_addr) , 
				    ntohs(from.sin_port) , msgType );
		cancelTrans:
			// temporary slot for sending back bogus ack
			UdpSlot tmp;
			// . send them another ACK so they shut up
			// . they might not have gotten due to network error
			// . this will clear "tmp" with memset
			tmp.connect (m_proto,&from,NULL,-1,transId, 10/*time*/,
				      now , 0 ); // m_niceness );
			// . if this blocks, that sucks, we'll probably get
			//   another untethered read... oh well...
			// . ack from 0 to infinite to prevent more from coming
			tmp.sendAck(m_sock,now,dgramNum,true/*weInit'ed?*/,
				    true/*cancelTrans?*/);
			//return 1;
			goto discard;
		}
		// . if we're shutting down do not accept new connections
		// . just ignore
		if ( m_isShuttingDown ) goto discard; // return 1;
		// int16_tcut
		bool isProxy = g_proxy.isProxy();
		// do not read any incoming request if half the slots are
		// being used for incoming requests right now. we don't want
		// to lose all of our memory just to hold Msg10 requests
		// which are about 25k each. restrict this to Msg10s fo rnow.
		// these are like 1MB each NOW!!! WHY??? reduce from 100 to 20.
		// these seem to be 227k each now, so raised from 20 to 50
		// especially since the g_alreadyAdded cache has a 84% hit
		// rate, these are pretty lightweight. msg 0x10 reply gen times
		// are VERY low. MDW
		bool getSlot = true;
		if ( msgType == 0x07 && m_msg07sInWaiting >= 100 )
			getSlot = false;

		if ( msgType == 0x10 && m_msg10sInWaiting >= 50 ) 
			getSlot = false;
		// crawl update info from Spider.cpp
		if ( msgType == 0xc1 && m_msgc1sInWaiting >= 100 ) 
			getSlot = false;
		//batch url lookup for siterec, rootQuality and ips, so spawns 
		//msg8 and msgc and msg50
		//if ( msgType == 0xd && m_msgDsInWaiting >= 100 ) 
		//	getSlot = false;
		// . a request to get link text, msg23, will spawn a msg22
		//   which often comes back to us
		// . don't accept any requests if over half full, because we
		//   may have to forward them, and we'll need a slot for that
		//if ( msgType == 0x23 && m_msg23sInWaiting >= 100 ) 
		//	getSlot = false;
		// msg25 spawns an indexdb request lookup and unless we limit
		// the msg25 requests we can jam ourslves if all the indexdb
		// lookups hit ourselves... we won't have enough free slots
		// to answer the msg0 indexdb lookups!
		if ( msgType == 0x25 && m_msg25sInWaiting >= 70 )
			getSlot = false;
		// . Msg50 can spawn Msg25s to compute the root quality if it
		//   does not have it in its cache...
		// . each one of these can take 10's of MBs of memory for
		//   holding the inlinker termlist. i had 29 out taking 364MB
		//   of mem, so stop that! only allow 15, tops.
		// . TODO: make this more efficient some how... it should be
		//   less of a problem on slingshot, the mem prob was on gk
		if ( msgType == 0x50 && m_msg50sInWaiting >= 10 )
			getSlot = false;
		// . i've seen us freeze up from this too
		// . but only drop spider's msg39s
		if ( msgType == 0x39 && m_msg39sInWaiting >= 10 && niceness )
			getSlot = false;
		// try to prevent another lockup condition of msg20 spawing
		// a msg22 request to self but failing...
		if ( msgType == 0x20 && m_msg20sInWaiting >= 50 && niceness )
			getSlot = false;
		// if running int16_t on mem, do not accept any more requests
		// because we can lock up from that, too
		//if ( msgType == 0x23 && g_mem.m_memAvail < 10*1024*1024 )
		//if ( g_mem.m_maxMem - g_mem.m_used < 20*1024*1024 &&
		//     // let adds slide through, otherwise, msg10 chokes up
		//     // trying to add to is own tfndb. we end up with a 
		//     // bunch of msg10s repeatedly sending msg1's to add to
		//     // the tfndb.
		//     msgType != 0x01 )
		//	getSlot = false;
		// . msg13 is clogging thiings up when we synchost a host
		//   and it comes back up
		// . allow spider compression proxy to have a bunch
		// . MDW: do we need this one anymore? relax it a little.
		if ( msgType == 0x13 && m_numUsedSlotsIncoming>400 && 
		     m_numUsedSlots>800 && !isProxy)
			getSlot = false;
		// 2c is clogging crap up
		if ( msgType == 0x2c && m_msg2csInWaiting >= 100 && niceness )
			getSlot = false;

		// . avoid slamming thread queues with sectiondb disk reads
		// . mdw 1/22/2014 take this out now too, we got ssds
		//   let's see if taking this out fixes the jam described
		//   below
		// . mdw 1/31/2014 got stuck doing linktext 0x20 lookups 
		//   leading to tagdb lookups with not enough slots left!!! 
		//   so decrease 0x20
		//   and/or increase 0x00. ill boost from 500 to 1500 
		//   although i
		//   think we should limit the msg20 niceness 1 requests really
		//   when slot usage is high... ok, i changed Msg25.cpp to only
		//   allow 1 msg20 out if 300+ sockets are in use.
		// . these kinds of techniques ultimately just end up
		//   in loop, the proper way is to throttle back the # of
		//   outstanding tagdb lookups or whatever at the source
		//   otherwise we jam up
		// . tagdb lookups were being dropped because of this being
		//   500 so i raised to 900. a lot of them were from
		//   'get outlink tag recs' or 'get link info' (0x20)
		if ( msgType == 0x00 && m_numUsedSlots > 1500 && niceness ) {
			// allow a ton of those tagdb lookups to come in
			char rdbId = 0;
			if ( peekSize > RDBIDOFFSET )
				rdbId = peek[RDBIDOFFSET];
			if ( rdbId != RDB_TAGDB )
				getSlot = false;
		}

		// added this because host #14 was clogging on
		// State00's and ThreadReadBuf taking all the mem.
		//
		// mdw 1/22/2014 seems to be jamming up now with 50 crawlers
		// per host on 16 hosts on tagdb lookups using msg8a so
		// take this out for now...
		//if ( msgType == 0x00 && m_msg0sInWaiting> 70 && niceness )
		//	getSlot = false;

		// really avoid slamming if we're trying to merge some stuff
		//if ( msgType == 0x00 && m_numUsedSlots > 100 && niceness &&
		//     g_numUrgentMerges )
		//	getSlot = false;
		// msgc for getting ip 
		//if ( msgType == 0x0c && m_msg0csInWaiting >= 200 && niceness)
		//	getSlot = false;
		// we always need to reserve some slots for sending our
		// requests out on. do this regardless of msg23 or not.
		//if ( m_numUsedSlots >= (m_maxSlots>>1) ) getSlot = false;
		//int32_t niceness = m_proto->isNice ( peek , peekSize );
		// lower priorty slots are dropped first
		if ( m_numUsedSlots >= 1300 && niceness > 0 && ! isProxy &&
		     // we dealt with special tagdb msg00's above so
		     // do not deal with them here
		     msgType != 0x00 ) 
			getSlot = false;

		// . reserve 300 slots for outgoing query-related requests
		// . this was 1700, but the max udp slots is set to 3500
		//   in main.cpp, so let's up this to 2300. i don't want to
		//   drop stuff like Msg39 because it takes 8 seconds before
		//   it is re-routed in Multicast.cpp! now that we show what
		//   msgtypes are being dropped exactly in PageStats.cpp we
		//   will know if this is hurting us.
		if ( m_numUsedSlots >= 2300 && ! isProxy ) getSlot = false;
		// never drop ping packets! they do not send out requests
		if ( msgType == 0x11 ) getSlot = true;
		// and getting results from the cache is always zippy
		if ( msgType == 0x17 ) getSlot = true;
		// spellchecker is fast
		if ( msgType == 0x3d ) getSlot = true;
		// . msg36 is done quickly and does not send out a 2nd request
		// . iff "exact" (the first byte) is false, because if it 
		//   requires an exact count it may have to hit disk
		// . use niceness of 0 instead of "exact count", same thing
		if ( msgType == 0x36 && niceness == 0 )	getSlot = true;
		// getting the "load" does not send out a 2nd request
		if ( msgType == 0x34 ) getSlot = true;
		// getting a titlerec does not send out a 2nd request. i really
		// hate those title rec timeout msgs.
		if ( msgType == 0x22 && niceness == 0 ) getSlot = true;
		
		if ( getSlot ) 
			// get a new UdpSlot
			slot = getEmptyUdpSlot_ass ( key , true );
		// return -1 on failure
		if ( ! slot ) {
			// return -1
			status = false;
			// discard it!
			// only log this message up to once per second to avoid
			// flooding the log
			static int64_t s_lastTime = 0LL;
			g_dropped++;
			// count each msgType we drop
			if ( niceness == 0 ) g_stats.m_dropped[msgType][0]++;
			else                 g_stats.m_dropped[msgType][1]++;
			if ( now - s_lastTime >= 1000 ) {
				s_lastTime = now;
				log("udp: No udp slots to handle datagram.  "
				    "(msgType=0x%x niceness=%"INT32") "
				    "Discarding. It should be resent. Dropped "
				    "dgrams=%"INT32".", msgType,niceness,g_dropped);
			}
			goto discard;
		}
		// default timeout, sender has 60 seconds to send request!
		int32_t timeout = 60;
		// not if msg8e! they are huge requests!
		if ( msgType == 0x8e ) timeout = 999999;
		// connect this slot (callback should be NULL)
		slot->connect ( m_proto ,  
				&from   ,  // ip/port
				// we now put in the host, which may be NULL
				// if not in cluster, but we need this for
				// keeping track of dgrams sent/read to/from
				// this host (Host::m_dgramsTo/From)
				h       , // NULL    ,  // hostPtr
				-1      ,  // hostId
				transId ,  
				timeout      ,  // timeout in 60 secs
				now     ,
				// . slot->m_niceness should be set to this now
				// . originally m_niceness is that of this udp
				//   server, and we were using it as the slot's
				//   but it should be correct now...
				niceness ); // 0 // m_niceness );
		// don't count ping towards this
		if ( msgType != 0x11 ) {
			// if we connected to a request slot, count it
			m_requestsInWaiting++;
			// special count
			if ( msgType == 0x07 ) m_msg07sInWaiting++;
			if ( msgType == 0x10 ) m_msg10sInWaiting++;
			if ( msgType == 0xc1 ) m_msgc1sInWaiting++;
			//if ( msgType == 0xd  ) m_msgDsInWaiting++;
			//if ( msgType == 0x23 ) m_msg23sInWaiting++;
			if ( msgType == 0x25 ) m_msg25sInWaiting++;
			if ( msgType == 0x50 ) m_msg50sInWaiting++;
			if ( msgType == 0x39 ) m_msg39sInWaiting++;
			if ( msgType == 0x20 ) m_msg20sInWaiting++;
			if ( msgType == 0x2c ) m_msg2csInWaiting++;
			if ( msgType == 0x0c ) m_msg0csInWaiting++;
			if ( msgType == 0x00 ) m_msg0sInWaiting++;
			// debug msg
			//log("in waiting up to %"INT32"",m_requestsInWaiting );
			//log("in waiting up to %"INT32" (0x%hhx) ",
			//     m_requestsInWaiting, slot->m_msgType );
			// suspend the low priority server
			if ( this == &g_udpServer2 ) g_udpServer.suspend();
		}

	}
	// let caller know the slot associated with reading this dgram
	*slotPtr = slot;
	// . otherwise read our dgram into the slot
	// . it returns false and sets g_errno on error
	readSize = 0;
	discard  = false;

	// . HACK: kinda. 
	// . change the ip we reply on to wherever the sender came from!
	// . because we know that that eth port is mostly likely the best
	// . that way if he resends a request on a different ip because we 
	//   did not ack him because the eth port went down, we need to send
	//   our ack on his changed src ip. really only the sendAck() routine
	//   uses this ip, because the send datagram thing will send on the
	//   preferred eth port, be it eth0 or eth1, based on if it got a 
	//   timely ACK or not.
	// . pings should never switch ips though... this was causing
	//   Host::m_inProgress1 to be unset instead of m_inProgress2 and
	//   we were never able to regain a dead host on eth1 in PingServer.cpp
	if ( ip != slot->m_ip && slot->m_msgType != 0x11 ) {
		if ( g_conf.m_logDebugUdp )
			log(LOG_DEBUG,"udp: changing ip to %s for acking",
			    iptoa(ip));
		slot->m_ip = ip;
	}

	//if ( ! slot->m_host ) { char *xx = NULL; *xx = 0;}
	status   = slot->readDatagramOrAck(m_sock,peek,peekSize,now,&discard,
					   &readSize);

	// we we could not allocate a read buffer to hold the request/reply
	// just send a cancel ack so the send will call its callback with
	// g_errno set
	// MDW: it won't make it into the m_head3 callback linked list with
	// this logic.... maybe it just times out or resends later...
	if ( ! status && g_errno == ENOMEM ) goto cancelTrans;

	// if it is now a complete REPLY, callback will need to be called
	// so insert into the callback linked list, m_head3.
	// we have to put slots with NULL callbacks in here since they
	// are incoming requests to handle.
	if ( //slot->m_callback && 
	     // if we got an error reading the reply (or sending req?) then
	     // consider it completed too?
	     // ( slot->isTransactionComplete() || slot->m_errno ) &&
	    ( slot->isDoneReading() || slot->m_errno ) ) {
		// prepare to call the callback by adding it to this
		// special linked list
		addToCallbackLinkedList ( slot );
	}


	//	if(g_conf.m_sequentialProfiling) {
	// 		if(slot->isDoneReading()) 
	// 			log(LOG_TIMING, "admin: read last dgram: "
	//                           "%"INT32" %s", slot->getNiceness(),peek);
	//	}

 discard:
	// discard if we should
	if ( discard ) {
	       readSize=recvfrom(m_sock,tmpbuf,DGRAM_SIZE_CEILING,0,NULL,NULL);
	       //log("udp: recvfrom3 = %i",(int)readSize);
	}
	// . update stats, just put them all in g_udpServer
	// . do not count acks
	// . do not count discarded dgrams here
	if ( ! wasAck && readSize > 0 ) {
		// in case shotgun ip equals ip, check this first
		if ( h && h->m_ip == ip ) {
			g_udpServer.m_eth0PacketsIn += 1;
			g_udpServer.m_eth0BytesIn   += readSize;
		}
		// it can come from outside the cluster so check this
		else if ( h && h->m_ipShotgun == ip ) {
			g_udpServer.m_eth1PacketsIn += 1;
			g_udpServer.m_eth1BytesIn   += readSize;
		}
		// count packets to/from hosts outside separately usually
		// for importing link information. this can be from the dns
		// quite often!!
		else {
			//log("ip=%s",iptoa(ip));
			g_udpServer.m_outsiderPacketsIn += 1;
			g_udpServer.m_outsiderBytesIn   += readSize;
		}
	}
	// turn off
	if ( flipped ) interruptsOn();
	// return -1 on error
	if ( ! status ) return -1;
	// . return 1 cuz we did read the dgram ok
	// . if we read a dgram, ACK will be sent in readPoll() after we return
	return 1;
	// come here if we don't want the dgram!
	/*
 discard:
	// read it into the temporary discard buf
	recvfrom(m_sock,tmpbuf,DGRAM_SIZE_CEILING,0,NULL,NULL);
	// turn off
	if ( flipped ) interruptsOn();
	// return 1 cuz we did read something
	return 1;
	*/

	// . if this slot has been waiting too long then steal the token
	// . if when sender is ready receiver is not, this contention can
	//   go one for over 10 seconds!
	// . can we steal the token?
	// . only query traffic (niceness of 0) can steal, spider traffic
	//   (niceness of 1) cannot
	// . we must be older than the token slot by 500 ms
	// . TODO: can just using lower 4 bytes of millisecond time be bad?
	// . if you change the 500 here change it in UdpSlot::getScore() too
	/*
	bool canSteal = false;
	if ( slot->m_niceness == 0 ) {
		if ( *s_token && *s_token != slot && 
		     (uint32_t)slot->m_startTime + 100 < *s_tokenTime )
			canSteal = true;
		if ( s_local  &&  s_local != slot && 
		     (uint32_t)slot->m_startTime + 100 <  s_localTime )
			canSteal = true;
	}
	// now try to claim the token for ourselves if we're a large reply
	if ( ! isAck                             &&
	       slot->m_callback                  && 
	     //g_hostdb.getMyIp() != slot->m_ip  &&
	       slot->m_dgramsToRead >= LARGE_MSG &&
	       ( canSteal || (! *s_token && ! s_local ) ) ) {
#ifdef _UDPDEBUG_
		// make a note of it
		char *a = "Gave";
		if ( *s_token || s_local ) a = "Stole";
		log("%s token to transId=%"INT32" msgType=0x%hhx callback=%08"XINT32""
		    " slot=%"UINT32"", a,slot->m_transId , slot->m_msgType,
		    (int32_t)slot->m_callback, (uint32_t)slot);
#endif
		// . claim s_local if we're local
		// . set the last 4 bytes of time in milliseconds
		if ( g_hostdb.getMyIp() == slot->m_ip ) {
			s_local     = slot;
			s_localTime = (uint32_t)slot->m_startTime;
		}
		// otherwise, claim s_token
		else {
			*s_token  = slot;
			*s_tokenTime = (uint32_t) slot->m_startTime;
		}
	}
	// if we read an ack we might be able to claim s_token so we can
	// send more dgrams to this host
	if (   isAck                             && 
	     ! slot->m_callback                  && 
	       slot->m_dgramsToSend >= LARGE_MSG && 
	       //g_hostdb.getMyIp() != slot->m_ip  &&
	       ( canSteal || ( ! *s_token && ! s_local ) ) ) {
		// make a note of it
		char *a = "Gave";
		if ( *s_token || s_local ) a = "Stole";
		log("%s token to transId=%"INT32" msgType=0x%hhx slot=%"UINT32"",
		    a,slot->m_transId , slot->m_msgType , (uint32_t)slot);
		// . claim s_local if we're local
		// . set the last 4 bytes of time in milliseconds
		if ( g_hostdb.getMyIp() == slot->m_ip ) {
			s_local     = slot;
			s_localTime = (uint32_t)slot->m_startTime;
		}
		// otherwise, claim s_token
		else {
			*s_token  = slot;
			*s_tokenTime = (uint32_t) slot->m_startTime;
		}
	}
	*/
	// . return 1 cuz we did read the dgram ok
	// . if we read a dgram, ACK will be sent in readPoll() after we return
	//return 1;
}		

// . g_udpServer2::getEmptyUdpSlot() calls g_udpServer.suspend() to  suspend 
//   the low priority udp server
// . !!!!!we might be in a signal handler, so be careful!!!!!!!!!!!!!!!
void UdpServer::suspend ( ) {
	// disable for now, i don't think its a good thing, instead
	// we should just not call low priority (niceness >= 1) msg callbacks
	// or handlers before those of high priority, ?or when a high
	// priority thread is launched?
	return;
	// return if already suspended
	if ( m_isSuspended ) return;
	// debug msg
	if ( g_conf.m_logDebugUdp )
		log(LOG_DEBUG,"udp: SUSPENDING UDPSERVER.");
	// otherwise suspend ourselves
	m_isSuspended = true;
	// suspend any merges going on, not just from indexdb
	//g_indexdb.getRdb()->suspendAllMerges();
	// we got a new request, so suspend any low priority threads
	// iff we're a high priority server
	//g_threads.suspendLowPriorityThreads();
}

// this is called by the high priority udp server when it's empty and
// the low priority udp server was waiting for it to be empty
void UdpServer::resume ( ) {
	// if we weren't suspended, ignore it
	if ( ! m_isSuspended ) return;
	// can't be called from signal handler!
	if ( g_inSigHandler ) return;
	// sanity check
	char *xx=NULL;*xx=0;
	// debug msg
	if ( g_conf.m_logDebugUdp ) 
		log(LOG_DEBUG,"udp: RESUMING UDPSERVER.");
	// we are no longer suspened
	m_isSuspended = false;
	// get time now
	int64_t now = gettimeofdayInMillisecondsLocal();
	// send as much as we can now that m_isSuspended is false
	sendPoll_ass ( true , now );
	// resume any merge that was going on
	//g_indexdb.getRdb()->resumeAllMerges();
	// resume the low priority threads
	//g_threads.resumeLowPriorityThreads();
	// now do reading/sending/timeouting/etc.
	timePoll();
	// call callbacks that may have been delayed
	makeCallbacks_ass ( -1 );
}

// . try calling makeCallback_ass() on all slots
// . return true if we called one
// . this is basically double entrant!!! CAUTION!!!
// . if niceness is 0 we may be in a quickpoll or may not be. but we
//   will not enter a quickpoll in that case.
// . however, if we are in a quickpoll and call makeCallbacks_ass then
//   it will use niceness 0 exclusively, but the function that was niceness
//   1 and called quickpoll may itself have been indirectly in 
//   makeCallbacks_ass(1), so we have to make sure that if we change the
//   linked list here, we make sure the parent adjusts.
// . the problem is when we call this with niceness 1 and we convert
//   a niceness 1 callback to 0...
bool UdpServer::makeCallbacks_ass ( int32_t niceness ) {

	// if nothing to call, forget it
	if ( ! m_head3 ) return false;

 	//if ( g_conf.m_logDebugUdp )
		log(LOG_DEBUG,"udp: makeCallbacks_ass: start. nice=%"INT32" "
		    "inquickpoll=%"INT32"",
		    niceness,(int32_t)g_loop.m_inQuickPoll);
	// bail if suspended
	if ( m_isSuspended ) return false;


	// . if there are active high priority threads, do not 
	//   call low priority callbacks. in that case
	// . This seems to block things up to much?
	// . try again...
	// . seems like it is hurting high niceness threads
	//   from completing!!!
	//int32_t active = g_threads.getNumActiveHighPriorityThreads() ;
	//if ( active ) {
	//	if ( niceness >=  1 ) return true;
	//	if ( niceness == -1 ) niceness = 0;
	//}

	// assume noone called
	int32_t numCalled = 0;
	if(niceness > 0) m_needBottom = false;

	// do not do niceness conversion if doing a qa.html run because it
	// messes up the order of writing/reading to/from placedb causing
	// like 30 pages to have inconsistencies in their addresses.
	// default this to off for now!
	bool doNicenessConversion = true;
	if ( g_conf.m_testParserEnabled ||
	     g_conf.m_testSpiderEnabled ||
	     g_conf.m_testSearchEnabled )
		doNicenessConversion = false;

	// this stops merges from getting done because the write threads never
	// get launched
	if ( g_numUrgentMerges )
		doNicenessConversion = false;

	// or if saving or something
	if ( g_process.m_mode )
		doNicenessConversion = false;

	int64_t startTime = gettimeofdayInMillisecondsLocal();

 fullRestart:

	// take care of certain handlers/callbacks before any others
	// regardless of niceness levels because these handlers are so fast
	int32_t pass = 0;

 nextPass:

	UdpSlot *nextSlot = NULL;

	// only scan those slots that are ready
	//for ( UdpSlot *slot = m_head2 ; slot ; slot = slot->m_next2 ) {
	for ( UdpSlot *slot = m_head3 ; slot ; slot = nextSlot ) {
		// because makeCallback_ass() can delete the slot, use this
		nextSlot = slot->m_next3;
		// call quick handlers in pass 0, they do not take any time
		// and if they do not get called right away can cause this host
		// to bottleneck many hosts
		if ( pass == 0 ) {
			// never call any high niceness handler/callback when
			// we are already in quickpoll
			if ( g_loop.m_inQuickPoll && 
			     slot->m_niceness != 0 ) continue;
			// never call a msg4 handler even if it has niceness 0
			// if we are in quickpoll, because we might be in 
			// the Msg4.cpp code already!
			if ( g_loop.m_inQuickPoll &&
			     slot->m_msgType == 0x04 ) continue;
			// only call handlers in pass 0, not reply callbacks
			if ( slot->m_callback ) continue;
			// only call certain msg handlers...
			if ( slot->m_msgType != 0x36 &&  // getTermFreq()
			     slot->m_msgType != 0x11 &&  // ping
			     slot->m_msgType != 0x3d &&  // speller.cpp
			     slot->m_msgType != 0x34 &&  // getLoad()
			     slot->m_msgType != 0x17 &&  // serp/dist cache
			     slot->m_msgType != 0x01 &&  // add  RdbList
			     slot->m_msgType != 0x00   ) // read RdbList
				continue;
			// BUT the Msg1 list to add has to be small! if it is
			// big then it should wait until later.
			if ( slot->m_msgType == 0x01 &&
			     slot->m_readBufSize > 150 ) continue;
			// only allow niceness 0 msg 0x00 requests here since
			// we call a msg8a from msg20.cpp summary generation
			// which uses msg0 to read tagdb list from disk
			if ( slot->m_msgType == 0x00 && slot->m_niceness ) {
				// to keep udp slots from clogging up with 
				// tagdb reads allow even niceness 1 tagdb 
				// reads through. cache rate should be super
				// higher and reads short.
				char rdbId = 0;
				if ( slot->m_readBuf &&
				     slot->m_readBufSize > RDBIDOFFSET ) 
					rdbId = slot->m_readBuf[RDBIDOFFSET];
				if ( rdbId != RDB_TAGDB )
					continue;
			}
		}
		// if slot niceness is 1 and we are in a quickpoll, then
		// change niceness to 0 if its a 0x2c or a get taglist handler
		// request. this makes it so a spider that is deep into
		// parsing sections or whatever will still handle some
		// popular niceness 1 requests and not hold all the other
		// spiders up.
		if ( g_loop.m_inQuickPoll &&  
		     ! slot->m_callback && // must be a handler request
		     // must have been sitting there for 500ms+
		     // also consider using slot->m_lastActionTime
		     startTime - slot->m_startTime > 500 &&
		     //slot->m_msgType != 0x20 &&
		     //slot->m_msgType != 0x04 &&
		     // only do it for these guys now to make sure it
		     // doesn't hurt the queries coming in
		     (slot->m_msgType == 0x13 ||
		      slot->m_msgType == 0x0c) &&
		     this != &g_dns.m_udpServer &&
		     slot->m_niceness == 1 &&
		     ! slot->m_convertedNiceness &&
		     // can't be in a quickpoll in its own handler!!!
		     // we now set this to true BEFORE calling the handler
		     // so if we are in the handler now but in a quickpoll
		     // then we do not re-enter the handler!! 
		     ! slot->m_calledHandler &&
		     slot->m_readBufSize > 0 &&
		     slot->m_sendBufSize == 0 &&
		     doNicenessConversion &&
		     m_outstandingConverts < 20 ) {
			// note it
			if ( g_conf.m_logDebugUdp )
				log("udp: converting slot from niceness 1 to "
				    "0. slot=%"PTRFMT" mmsgtype=0x%hhx",
				    (PTRTYPE)slot,
				    slot->m_msgType);
			// convert the niceness
			slot->m_niceness = 0;
			// count it
			m_outstandingConverts++;
			// flag it somehow so we can decrement
			// m_outstandingConverts after we call the handler
			// and send back the reply
			slot->m_convertedNiceness = 1;
		}
		// . conversion for dns callbacks
		// . usually the callback is gotIpWrapper() in MsgC.cpp i guess
		if ( g_loop.m_inQuickPoll &&  
		     ! slot->m_convertedNiceness &&
		     this == &g_dns.m_udpServer &&
		     slot->m_callback &&
		     slot->m_niceness == 1 &&
		     // can't be in a quickpoll in its own handler!!!
		     // we now set this to true BEFORE calling the handler
		     // so if we are in the handler now but in a quickpoll
		     // then we do not re-enter the handler!! 
		     ! slot->m_calledCallback &&
		     slot->m_readBufSize > 0 &&
		     slot->m_sendBufSize > 0 &&
		     doNicenessConversion &&
		     m_outstandingConverts < 20 ) {
			// note it
			if ( g_conf.m_logDebugUdp )
				log("udp: converting slot2 from niceness 1 to "
				    "0. slot=%"PTRFMT" mmsgtype=0x%hhx",
				    (PTRTYPE)slot,
				    slot->m_msgType);
			// convert the niceness
			slot->m_niceness = 0;
			// count it
			m_outstandingConverts++;
			// flag it somehow so we can decrement
			// m_outstandingConverts after we call the handler
			// and send back the reply
			slot->m_convertedNiceness = 1;
		}

		// never call any high niceness handler/callback when
		// we are already in quickpoll
		if ( g_loop.m_inQuickPoll &&  slot->m_niceness != 0 ) continue;
		// skip if not level we want
		if ( niceness <= 0 && slot->m_niceness > 0 && pass>0) continue;
		// set g_errno before calling
		g_errno = slot->m_errno;
		// if we got an error from him, set his stats
		Host *h = NULL;
		if ( g_errno && slot->m_hostId >= 0 ) 
			h = g_hostdb.getHost ( slot->m_hostId );
		if ( h ) {
			h->m_errorReplies++;
			if ( g_errno == ETRYAGAIN ) 
				h->m_pingInfo.m_etryagains++;
		}

		//int32_t cbAddr = (int32_t)slot->m_callback;
		// try to call the callback for this slot
		//g_loop.startBlockedCpuTimer();
		// time it now
		int64_t start2 = 0;
		bool logIt = false;
		if ( slot->m_niceness == 0 ) logIt = true;
		if ( logIt ) start2 = gettimeofdayInMillisecondsLocal();
		// log that
		if ( g_conf.m_logDebugUdp )
			log(LOG_DEBUG,"udp: calling callback/handler for "
			    "slot=%"PTRFMT" pass=%"INT32" nice=%"INT32"",
			    (PTRTYPE)slot,
			    (int32_t)pass,(int32_t)slot->m_niceness);

		// save it
		//UdpSlot *next3 = slot->m_next2;

		// . crap, this can alter the linked list we are scanning
		//   if it deletes the slot! yes, but now we use "nextSlot"
		// . return false on error and sets g_errno, true otherwise
		// . return true if we called one
		// . skip to next slot if did not call callback/handler
		if ( ! makeCallback_ass ( slot ) ) continue;

		// remove it from the callback list to avoid re-call
		removeFromCallbackLinkedList ( slot );

		int64_t took = 0;
		if ( logIt )
			took = gettimeofdayInMillisecondsLocal()-start2;
		if ( took > 1000 || (slot->m_niceness==0 && took>100))
			logf(LOG_DEBUG,"udp: took %"INT64" ms to call "
			     "callback/handler for "
			     "msgtype=0x%"XINT32" "
			     "nice=%"INT32" "
			     "callback=%"PTRFMT"",
			     took,
			     (int32_t)slot->m_msgType,
			     (int32_t)slot->m_niceness,
			     (PTRTYPE)slot->m_callback);
		int64_t elapsed;
		numCalled++;

		// log how long callback took
		if(niceness > 0 && 
		   (elapsed = gettimeofdayInMillisecondsLocal() - 
		    startTime) > 5 ) {
			//bail if we're taking too long and we're a 
			//low niceness request.  we can always come 
			//back.
			//TODO: call sigqueue if we need to
			//now we just tell loop to poll
			//if(g_conf.m_sequentialProfiling) {
			//	log(LOG_TIMING, "admin: UdpServer spent "
			//	    "%"INT64" ms doing"
			//	    " %"INT32" low priority callbacks."
			//	    " last was:  %s", 
			//	    elapsed, numCalled, 
			//	    g_profiler.getFnName(cbAddr));
			//}
			//g_loop.m_needToPoll = true;
			m_needBottom = true;
			// now we just finish out the list with a 
			// lower niceness
			//niceness = 0;
			return numCalled;
		}

		// CRAP, what happens is we are not in a quickpoll,
		// we call some handler/callback, we enter a quickpoll,
		// we convert him, send him, delete him, then return
		// back to this function and the linked list is
		// altered because we double entered this function
		// from within a quickpoll. so if we are not in a 
		// quickpoll, we have to reset the linked list scan after
		// calling makeCallback(slot) below.
		if ( ! g_loop.m_inQuickPoll ) goto fullRestart;

	}
	// clear
	g_errno = 0;

	// if we just did pass 0 now we do pass 1
	if ( ++pass == 1 ) goto nextPass;	
		
	return numCalled;
	// . call callbacks for slots that need it
	// . caution: sometimes callbacks really delay us! like msg 0x20
	// . well, for now comment this out again
	/*
	for ( int32_t i = 0 ; i <= m_topUsedSlot ; i++ ) {
		// skip if empty
		if ( isEmpty(i) ) continue;
		// save msg 0x20's for last
		if ( m_slots[i].m_msgType == 0x20 ) continue;
		// pull out old g_errno code since the sigHandler cannot
		// call callbacks
		g_errno = m_slots[i].m_errno;
		// then call it's callback
		makeCallback_ass ( &m_slots[i] );
	}
	// pick up the msg 0x20's we saved for last
	for ( int32_t i = 0 ; i <= m_topUsedSlot ; i++ ) {
		// skip if empty
		if ( isEmpty(i) ) continue;
		// save msg 0x20's for last
		if ( m_slots[i].m_msgType != 0x20 ) continue;
		// pull out old g_errno code since the sigHandler cannot
		// call callbacks
		g_errno = m_slots[i].m_errno;
		// then call it's callback
		makeCallback_ass ( &m_slots[i] );
	}
	*/
}


// . return false on error and sets g_errno, true otherwise
// . g_errno may already be set when this is called... that's the reason why
//   it was called
// . this is also called by readTimeoutPoll()
// . IMPORTANT: call this every time after you read or send a dgram/ACK
// .            or when g_errno gets set
// . will just queue a signal for GB_SIGRTMIN + 1 queue if g_inSigHandler is true
// . return true if we called one
bool UdpServer::makeCallback_ass ( UdpSlot *slot ) {
	// get msgType
	unsigned char msgType = slot->m_msgType;
	// . if we are the low priority server we do not make callbacks
	//   until there are no ongoing transactions in the high priority 
	//   server
	// . BUT, we are always allowed to call Msg0's m_callback2 so we can
	//   give back the bandwidth token (via Msg21) HACK!
	// . undo this for now
	//if ( m_isSuspended && msgType != 0x01 ) return ;
	// mdw if ( m_isSuspended ) return ;

	/*
	if ( m_isSuspended ) {
		if ( slot->m_msgType   != 0x00  ) return;
		//     slot->m_msgType   != 0x30  ) return;
		if ( slot->m_callback2 == NULL  ) return;
	}
	*/
	// watch out for illegal msgTypes
	//if ( msgType < 0 ) {
	//	log(LOG_LOGIC,"udp: makeCallback_ass: Illegal msgType."); 
	//	return false; 
	//}

	//only allow a quickpoll if we are nice.
	//g_loop.canQuickPoll(slot->m_niceness);

	// for timing callbacks and handlers
	int64_t start = 0;
	int64_t took;
	//int32_t mt ;
	int64_t now ;
	int32_t delta , n , bucket;
	float mem;
	int32_t saved;
	bool saved2;
	//bool incInt;

	// debug timing
	if ( g_conf.m_logDebugUdp )
		start = gettimeofdayInMillisecondsLocal();
	// callback is non-NULL if we initiated the transaction 
	if ( slot->m_callback ) { 

		// assume the slot's error when making callback
		// like EUDPTIMEDOUT
		if ( ! g_errno ) g_errno = slot->m_errno;

		// . if transaction has not fully completed, bail
		// . unless there was an error
		// . g_errno could be ECANCELLED
		if ( ! g_errno && ! slot->isTransactionComplete()) {
			//log("udp: why calling callback when not ready???");
			return false;
		}
		/*
#ifdef _UDPDEBUG_		
		// if we had the token, give it up so others can send with it
		if ( *s_token == slot || s_local == slot ) 
			log("s_token  released");
		log("UdpServer doing callback for transId=%"INT32" "
		"msgType=0x%hhx g_errno=%s callback=%08"XINT32"",
		slot->m_transId , msgType, mstrerror(g_errno),
		(int32_t)slot->m_callback);
#endif
		// free the token if we were occupying it
		if ( *s_token == slot ) *s_token = NULL;
		if (  s_local == slot )  s_local = NULL;
		*/
		// debug msg
		if ( g_conf.m_logDebugUdp ) {
			int64_t now  = gettimeofdayInMillisecondsLocal();
			int64_t took = now - slot->m_startTime;
			//if ( took > 10 )
			int32_t Mbps = 0;
			if ( took > 0 ) Mbps = slot->m_readBufSize / took;
			Mbps = (Mbps * 1000) / (1024*1024);
			log(LOG_DEBUG,"udp: Got reply transId=%"INT32" "
			    "msgType=0x%hhx "
			    "g_errno=%s "
			    "niceness=%"INT32" "
			    "callback=%08"PTRFMT" "
			    "took %"INT64" ms (%"INT32" Mbps).",
			    slot->m_transId,msgType,
			    mstrerror(g_errno),
			    slot->m_niceness,
			    (PTRTYPE)slot->m_callback ,
			    took , Mbps );
			start = now;
		}
		// if we're in a sig handler, queue a signal and return
		if ( g_inSigHandler ) goto queueSig;
		// mark it in the stats for PageStats.cpp
		if      ( g_errno == EUDPTIMEDOUT )
			g_stats.m_timeouts[msgType][slot->m_niceness]++;
		else if ( g_errno == ENOMEM ) 
			g_stats.m_nomem[msgType][slot->m_niceness]++;
		else if ( g_errno ) 
			g_stats.m_errors[msgType][slot->m_niceness]++;

		if ( g_conf.m_maxCallbackDelay >= 0 )//&&slot->m_niceness==0) 
			start = gettimeofdayInMillisecondsLocal();

		// sanity check for double callbacks
		if ( slot->m_calledCallback ) { char *xx=NULL;*xx=0; }

		// now we got a reply or an g_errno so call the callback
		//if (g_conf.m_profilingEnabled){
		//	address=(int32_t)slot->m_callback;
		//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
		//}
		//g_loop.startBlockedCpuTimer();

		if ( g_conf.m_logDebugLoop && slot->m_msgType != 0x11 )
			log(LOG_DEBUG,"loop: enter callback for 0x%"XINT32" "
			    "nice=%"INT32"",(int32_t)slot->m_msgType,slot->m_niceness);

		// sanity check -- avoid double calls
		if ( slot->m_calledCallback ) { char *xx=NULL;*xx=0; }

		slot->m_calledCallback++;

		// sanity check -- avoid double calls
		if ( slot->m_calledCallback != 1 ) { char *xx=NULL;*xx=0; }

		// . sanity check - if in a high niceness callback, we should
		//   only be calling niceness 0 callbacks here
		//   NOTE: calling UdpServer::cancel() is an exception
		// . no, because Loop.cpp calls udpserver's callback on its
		//   fd with niceness 0, and it in turn can call niceness 1
		//   udp slots
		//if(g_niceness==0 && slot->m_niceness && g_errno!=ECANCELLED){
		//	char *xx=NULL;*xx=0;}

		// sanity check. has this slot been excised from linked list?
		if ( slot->m_prev2 && slot->m_prev2->m_next2 != slot ) {
			char *xx=NULL;*xx=0; }

		// sanity check. has this slot been excised from linked list?
		if ( slot->m_prev2 && slot->m_prev2->m_next2 != slot ) {
			char *xx=NULL;*xx=0; }

		// save niceness
		saved = g_niceness;
		// set it
		g_niceness = slot->m_niceness;
		// make sure not 2
		if ( g_niceness >= 2 ) g_niceness = 1;

		// if quickpoll notices we are in the same callback for
		// more than 4 ticks, it core dump to let us know!! it
		// use the transId of the slot to count!
		g_callSlot = slot;

		slot->m_callback ( slot->m_state , slot ); 

		g_callSlot = NULL;

		// restore it
		g_niceness = saved;

		if ( g_conf.m_logDebugLoop && slot->m_msgType != 0x11 )
			log(LOG_DEBUG,"loop: exit callback for 0x%"XINT32" "
			    "nice=%"INT32"",(int32_t)slot->m_msgType,slot->m_niceness);

		if ( g_conf.m_maxCallbackDelay >= 0 ) {
			int64_t elapsed = gettimeofdayInMillisecondsLocal()-
				start;
			if ( slot->m_niceness == 0 &&
			     elapsed >= g_conf.m_maxCallbackDelay )
				log("udp: Took %"INT64" ms to call "
				    "callback for msgType=0x%hhx niceness=%"INT32"",
				    elapsed,slot->m_msgType,
				    (int32_t)slot->m_niceness);
		}

		//if (g_conf.m_profilingEnabled){
		//	if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
		//		log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
		//		    (int32_t)address);
		//}
		// time it
		if ( g_conf.m_logDebugUdp )
			log(LOG_DEBUG,"udp: Reply callback took %"INT64" ms.",
			    gettimeofdayInMillisecondsLocal() - start );
		// clear any g_errno that may have been set
		g_errno = 0;
		// . now lets destroy the slot, bufs and all
		// . if the caller wanted to hang on to request or reply then
		//   it should NULLify slot->m_sendBuf and/or slot->m_readBuf
		destroySlot ( slot );
		return true;
	}
	// don't repeat call the handler if we already called it
	if ( slot->m_calledHandler ) {
		// . if transaction has not fully completed, keep sending
		// . unless there was an error
		if ( ! g_errno && 
		     ! slot->isTransactionComplete() &&
		     ! slot->m_errno ) {
			if ( g_conf.m_logDebugUdp )
				log("udp: why calling handler "
				    "when not ready?");
			return false;
		}
		// we should not destroy the slot here on ENOMEM error,
		// because handler might be referencing the slot's read buffer
		// still. that is what Msg20 does... the first dgram was
		// probably ENOMEM and set our m_errno field, but the 2nd was
		// ok. we should reset m_errno before calling the handler.
		//if ( g_errno == ENOMEM && slot->m_msgType == 0x20 &&
		//     ! slot->isTransactionComplete() ) 
		//	return false;
		// if we had the blast token, give it up now so someone else
		// on this machine can send a large reply
		/*
#ifdef _UDPDEBUG_		
		if ( *s_token == slot || s_local == slot ) {
			// debug msgs
			log("udp: makeCallback_ass: done sending "
			    "slot=%"UINT32" bytes=%"INT32"", (uint32_t)slot , 
			    slot->m_sendBufSize);
			log("s_token released");
		}
#endif
		// free the token if we were occupying it
		if ( *s_token == slot ) *s_token = NULL;
		if (  s_local == slot )  s_local = NULL;
		*/
		// . now we sent the reply so try calling callback2
		// . this is usually NULL, but so I could make pretty graphs
		//   of transmission time it won't be
		// . if callback2 is hot it will be called here, possibly,
		//   more than once, but we also call m_callback2 later, too,
		//   since we cannot call destroySlot() in a hot sig handler
		if ( slot->m_callback2 ) {
			// if we're in a sig handler and not hot, queue it
			if ( g_inSigHandler && ! slot->m_isCallback2Hot )
				goto queueSig;
			// . since we can be re-entered by the sig handler
			//   make sure he doesn't call this callback while
			//   we are in the middle of it
			// . but if we're in a sig handler now, this will
			//   have to be called again to destroy the slot, so
			//   this only prevents an extra callback from a 
			//   sig handler really
			slot->m_isCallback2Hot = false;

			if ( g_conf.m_logDebugLoop )
				log(LOG_DEBUG,"loop: enter callback2 for "
				    "0x%"XINT32"",(int32_t)slot->m_msgType);

			// call it
			slot->m_callback2 ( slot->m_state , slot ); 

			if ( g_conf.m_logDebugLoop )
				log(LOG_DEBUG,"loop: exit callback2 for 0x%"XINT32"",
				    (int32_t)slot->m_msgType);

			// debug msg
			if ( g_conf.m_logDebugUdp ) {
				int64_t now  = 
					gettimeofdayInMillisecondsLocal();
				int64_t took = now - start ;
				//if ( took > 10 )
					log(LOG_DEBUG,
					    "udp: Callback2 transId=%"INT32" "
					    "msgType=0x%hhx "
					    "g_errno=%s callback2=%08"PTRFMT""
					    " took %"INT64" ms.",
					    slot->m_transId,msgType,
					    mstrerror(g_errno),
					    (PTRTYPE)slot->m_callback2,
					    took );
			}
			// clear any g_errno that may have been set
			g_errno = 0;
		}
		// . queue it if we're hot, m_callback2 may be called again ltr
		// . TODO: make destroySlot_ass()
		if ( g_inSigHandler ) goto queueSig;
		// nuke the slot, we gave them a reply...
		destroySlot ( slot );
		//log("udp: why double calling handler?");
		// this kind of callback doesn't count
		return false;
	}
	// . if we're not done reading the request, don't call the handler
	// . we now destroy it if the request timed out
	if ( ! slot->isDoneReading () ) {
		// . if g_errno not set, keep reading the new request
		// . otherwise it's usually EUDPTIMEOUT, set by readTimeoutPoll
		// . multicast will abandon sending a request if it doesn't
		//   get a response in X seconds, then it may move on to 
		//   using another transaction id to resend the request
		if ( ! g_errno ) return false;
		// queue it if we're hot
		if ( g_inSigHandler ) goto queueSig;
		// log a msg
		log(LOG_LOGIC,
		    "udp: makeCallback_ass: Requester stopped sending: %s.",
		    mstrerror(g_errno));
		// . nuke the half-ass request slot
		// . now if they continue later to send this request we
		//   will auto-ACK the dgrams, but we won't send a reply and
		//   the requester will time out waiting for the reply
		destroySlot ( slot );
		return false;
	}
	// . if we're in a sig handler, queue a signal and return
	// . now only queue it if handler is not hot
	if ( g_inSigHandler && ! m_isHandlerHot [ msgType ] ) goto queueSig;
	// save it
	saved2 = g_inHandler;
	// flag it so Loop.cpp does not re-nice quickpoll niceness
	g_inHandler = true;
	// . otherwise it was an incoming request we haven't answered yet
	// . call the registered handler to handle it
	// . bail if no handler
	if ( ! m_handlers [ msgType ] ) {
		log(LOG_LOGIC,
		    "udp: makeCallback_ass: Recvd unsupported msg type 0x%hhx."
		    " Did you forget to call registerHandler() for your "
		    "message class from main.cpp?", (char)msgType);
		g_inHandler = false;
		destroySlot ( slot );
		return false;
	}
	// let loop.cpp know we're done then
	g_inHandler = saved2;

	//#endif
	// . don't call the handler to satisfy the request if msgType is 0x00 
	//   or 0x39 AND memory is LOW
	// . instead, just return and try again some other time
	// . assume a max of 25 megs for now...
	//if (( msgType==0x00 || msgType==0x39) && s_udpMem >= 25*1024*1024 ) {
	//	log("udp: makeCallback_ass: no memory. waiting.");
	//	return;
	//}
	// . stats
	// . time how long for us to generate a reply
	//slot->m_calledHandlerTime = gettimeofdayInMillisecondsLocal();
	// debug msg
	if ( g_conf.m_logDebugUdp )
		log(LOG_DEBUG,"udp: Calling handler for transId=%"INT32" "
		    "msgType=0x%hhx.", slot->m_transId , msgType );


	// record some statistics on how long this was waiting to be called
	now = gettimeofdayInMillisecondsLocal();
	delta = now - slot->m_queuedTime;
	// sanity check
	if ( slot->m_queuedTime == -1 ) { char *xx = NULL; *xx = 0; }
	n = slot->m_niceness;
	if ( n < 0 ) n = 0;
	if ( n > 1 ) n = 1;
	// add to average
	g_stats.m_msgTotalOfQueuedTimes [msgType][n] += delta;
	g_stats.m_msgTotalQueued        [msgType][n]++;
	// bucket number is log base 2 of the delta
	if ( delta > 64000 ) delta = 64000;
	bucket = getHighestLitBit ( (uint16_t)delta );
	// MAX_BUCKETS is probably 16 and #define'd in Stats.h
	if ( bucket >= MAX_BUCKETS ) bucket = MAX_BUCKETS-1;
	g_stats.m_msgTotalQueuedByTime [msgType][n][bucket]++;


	// time it
	start = now; // gettimeofdayInMilliseconds();

	// use this for recording how long it takes to generate the reply
	slot->m_queuedTime = now;

	// . handler return value now obsolete
	// . handler must call sendErrorReply() or sendReply()
	// . send*Reply() will destroy slot on error or transaction completion
	//if (g_conf.m_profilingEnabled){
	//	address=(int32_t)m_handlers [slot->m_msgType];
	//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
	//}
	//	g_loop.startBlockedCpuTimer();

	// log it now
	if ( slot->m_msgType != 0x11 && g_conf.m_logDebugLoop )
		log(LOG_DEBUG,"loop: enter handler for 0x%"XINT32" nice=%"INT32"",
		    (int32_t)slot->m_msgType,(int32_t)slot->m_niceness);

	// . sanity check - if in a high niceness callback, we should
	//   only be calling niceness 0 callbacks here.
	// . no, because udpserver uses niceness 0 on its fd, and that will
	//   call niceness 1 slots here
	//if ( g_niceness==0 && slot->m_niceness ) { char *xx=NULL;*xx=0;}

	// save niceness
	saved = g_niceness;
	// set it
	g_niceness = slot->m_niceness;
	// make sure not 2
	if ( g_niceness >= 2 ) g_niceness = 1;

	// if quickpoll notices we are in the same callback for
	// more than 4 ticks, it core dump to let us know!! it
	// use the transId of the slot to count!
	g_callSlot = slot;

	// if we are out of mem basically, do not waste time fucking around
	if ( slot->m_msgType != 0x11 && slot->m_niceness == 0 &&
	     (mem = ((float)g_mem.getUsedMem())/(float)g_mem.getMaxMem()) >=
	     .990 ) {
		// log it
		static int32_t lcount = 0;
		if ( lcount == 0 )
			log(LOG_DEBUG,"loop: sending back enomem for "
			    "msg 0x%0hhx", slot->m_msgType);
		if ( ++lcount == 20 ) lcount = 0;
		sendErrorReply ( slot , ENOMEM );
	}
	else {
		// save it
		bool saved2 = g_inHandler;
		// flag it so Loop.cpp does not re-nice quickpoll niceness
		g_inHandler = true;
		// sanity
		if ( slot->m_calledHandler ) { char *xx=NULL;*xx=0; }
		// set this here now so it doesn't get its niceness converted
		// then it re-enters the same handler here but in a quickpoll!
		slot->m_calledHandler = true;
		// sanity so msg0.cpp hack works
		if ( slot->m_niceness == 99 ) { char *xx=NULL;*xx=0; }
		// . this is the niceness of the server, not the slot
		// . NO, now it is the slot's niceness. that makes sense.
		m_handlers [ slot->m_msgType ] ( slot , slot->m_niceness ) ;
		// let loop.cpp know we're done then
		g_inHandler = saved2;
	}

	g_callSlot = NULL;

	// restore
	g_niceness = saved;

	if ( slot->m_msgType != 0x11 && g_conf.m_logDebugLoop )
		log(LOG_DEBUG,"loop: exit handler for 0x%"XINT32" nice=%"INT32"",
		    (int32_t)slot->m_msgType,(int32_t)slot->m_niceness);

	// we called the handler, don't call it again
	slot->m_calledHandler = true;

	//if (g_conf.m_profilingEnabled){
	//	if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
	//		log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
	//		    (int32_t)address);
	//}

	// i've seen a bunch of msg20 handlers called in a row take over 
	// 10 seconds and the heartbeat gets starved and dumps core
	if ( slot->m_msgType == 0x20 )
		g_process.callHeartbeat();

	// g_errno was set from m_errno before calling the handler, but to
	// make sure the slot doesn't get destroyed now, reset this to 0. see
	// comment about Msg20 above.
	slot->m_errno = 0;

	if ( g_conf.m_maxCallbackDelay >= 0 ) {
		int64_t elapsed = gettimeofdayInMillisecondsLocal() - start;
		if ( elapsed >= g_conf.m_maxCallbackDelay &&
		     slot->m_niceness == 0 )
			log("udp: Took %"INT64" ms to call "
			    "HANDLER for msgType=0x%hhx niceness=%"INT32"",
			    elapsed,slot->m_msgType,(int32_t)slot->m_niceness);
	}

	// bitch if it blocked for too long
	//took = gettimeofdayInMilliseconds() - start;
	//mt = LOG_INFO;
	//if ( took <= 50 ) mt = LOG_TIMING;
	//if ( took > 10 )
	//	log(mt,"net: Handler transId=%"INT32" slot=%"UINT32" "
	// this is kinda obsolete now that we have the stats above
	if ( g_conf.m_logDebugNet ) {
		took = gettimeofdayInMillisecondsLocal() - start;
		log(LOG_DEBUG,"net: Handler transId=%"INT32" slot=%"PTRFMT" "
		    "msgType=0x%hhx msgSize=%"INT32" "
		    "g_errno=%s callback=%08"PTRFMT" "
		    "niceness=%"INT32" "
		    "took %"INT64" ms.",
		    (int32_t)slot->m_transId , (PTRTYPE)slot,
		    msgType, (int32_t)slot->m_readBufSize , mstrerror(g_errno),
		    (PTRTYPE)slot->m_callback,
		    (int32_t)slot->m_niceness,
		    took );
	}
	// clear any g_errno that may have been set
	g_errno = 0;
	// calling a handler counts
	return true;
	// come here if we can't make callbacks cuz we're in a sig handler
 queueSig:
	// don't double queue
	if ( slot->m_isQueued ) return false;
	// mark it as queued so we don't queue it again
	slot->m_isQueued = true;
	// store any error code in slot so when callback is called
	// it will be there
	slot->m_errno = g_errno;
	// make the signal data
	sigval_t svt; 
	// zero means to call g_udpServer2.makeCbacks()
	svt.sival_int = 0;
	// debug msg
	if ( g_conf.m_logDebugUdp ) 
		log(LOG_DEBUG,"udp: Queuing makeCallbacks_ass() sig for "
		    "msgType=0x%hhx slot=%"PTRFMT"", 
		    slot->m_msgType,(PTRTYPE)slot);
	// . if this fails it normally sends a SIGIO but I guess that won't
	//   happen since we're already in an interrupt handler, so we have
	//   to let g_loop know to poll
	// . TODO: won't he have to wakeup before he'll poll?????
// #ifndef _POLLONLY_	
// 	if ( ! g_loop.m_needToPoll && 
// 	     sigqueue ( s_pid, GB_SIGRTMIN + 1 , svt ) < 0 )
// 		g_loop.m_needToPoll = true;
// #else
// 	g_loop.m_needToPoll = true;
// #endif
	// . tell g_loop that we did a queue
	// . he sets this to false before calling our makeCallbacks_ass()
	g_someAreQueued = true;
	// nothing was called, no callback or handler
	return false;
}

// this wrapper is called every 15 ms by the Loop class
void timePollWrapper ( int fd , void *state ) { 
	UdpServer *THIS  = (UdpServer *)state;
	THIS->timePoll();
}

void UdpServer::timePoll ( ) {
	// debug msg
	//if ( g_conf.m_logDebugUdp ) 
	//	log(LOG_DEBUG,"udp: timepoll: inSigHandler=%"INT32", m_head2=%"INT32".",
	//	    (int32_t)g_inSigHandler,(int32_t)m_head2);
	// we cannot be in a live signal handler because readTimeoutPoll()
	// will return true in an infinite loop because process_ass() will not
	// be able to make callbacks to fix the situation
	if ( g_inSigHandler ) return;
	// timeout dead hosts if we should
	//if ( g_conf.m_giveupOnDeadHosts ) timeoutDeadHosts ( );
	// try shutting down
	//if ( m_isShuttingDown ) tryShuttingDown ( true );
	// bail if no slots in use
	//if ( m_topUsedSlot < 0 ) return;
	if ( ! m_head2 ) return;
	// return if suspended
	if ( m_isSuspended ) return;
	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("enter timePoll");
	// only repeat once
	//bool first = true;
	// get time now
	int64_t now = gettimeofdayInMillisecondsLocal();
	// before timing everyone out or starting resends, just to make
	// sure we read everything. we have have just been blocking on a int32_t
	// handler or callback or sequence of those things and have stuff
	// waiting to be read.
	process_ass ( now );
	// get again if changed
	now = gettimeofdayInMillisecondsLocal();
	// loop:
	// do read/send/callbacks
	//	process_ass ( now );
	// then do the timeout-ing
	if ( readTimeoutPoll ( now ) ) {
		// if we timed something out or reset it then call the
		// callbacks to do sending and loop back up
		makeCallbacks_ass ( MAX_NICENESS ); // -1
		// try sending on slots even though we haven't read from them
		//sendPoll ( true , now );
		// repeat in case the send got reset
		//		if ( first ) { first = false; goto loop; }
	}
	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("exit timePoll");
	/*
#ifdef _UDPDEBUG_
	// some debug info
	if ( *s_token ) 
		log("s_token  occupied by slot=%"UINT32" age=%"UINT32"",
		    (uint32_t)*s_token,
		    (uint32_t)now - *s_tokenTime );
	if (  s_local ) 
		log("s_local  occupied by slot=%"UINT32" age=%"UINT32"",
		    (uint32_t)s_local,
		    (uint32_t)now - s_localTime );
#endif
	*/
}

// every half second we check to see if 
//int64_t s_lastDeadCheck = 0LL;

// . this is called once per second
// . return false and sets g_errno on error
// . calls the callback of REPLY-reception slots that have timed out
// . just nuke the REQUEST-reception slots that have timed out
// . returns true if we timed one out OR reset one for resending
bool UdpServer::readTimeoutPoll ( int64_t now ) {
	// bail if we are in a wait state
	if ( m_isSuspended ) return false;
	// did we do something? assume not.
	bool something = false;
	// loop over occupied slots
	for ( UdpSlot *slot = m_head2 ; slot ; slot = slot->m_next2 ) {
		// clear g_errno
		g_errno = 0;
		// only deal with niceness 0 slots when in a quickpoll
		if ( g_loop.m_inQuickPoll && slot->m_niceness != 0 ) continue;
		// debug msg
		if ( g_conf.m_logDebugUdp && 1 == 0 ) 
			log(LOG_DEBUG,
			    "udp: resend TRY tid=%"INT32" "
			    "dst=%s:%hu " 
			    "doneReading=%"INT32" "
			    "dgramsToSend=%"INT32" "
			    "resendTime=%"INT32" "
			    "lastReadTime=%"UINT64" "
			    "delta=%"UINT64" "
			    "lastSendTime=%"UINT64" "
			    "delta=%"UINT64" "
			    "timeout=%"UINT32" "
			    "sentBitsOn=%"INT32" "
			    "readAckBitsOn=%"INT32" ",
			    slot->m_transId,
			    iptoa(slot->m_ip)+6,
			    (uint16_t)slot->m_port,
			    (int32_t)slot->isDoneReading(),
			    slot->m_dgramsToSend,
			    slot->m_resendTime,
			    (uint64_t)slot->m_lastReadTime,
			    (uint64_t)(now - slot->m_lastReadTime) ,
			    (uint64_t)slot->m_lastSendTime,
			    (uint64_t)(now - slot->m_lastSendTime) ,
			    (uint32_t)slot->m_timeout,
			    slot->m_sentBitsOn ,
			    slot->m_readAckBitsOn ) ;
		// skip empties
		//if ( isEmpty(i) ) continue;
		// get the slot
		//UdpSlot *slot = &m_slots[i];
		// skip if we're suspended, unless it is a special slot. HACK!
		//if ( m_isSuspended ) continue ;
		//     slot->m_msgType != 0x00 ) continue;
		//     slot->m_msgType != 0x30  ) continue;
		// if the reading is completed, but we haven't generated a
		// reply yet, then continue because when reply is generated
		// UdpServer::sendReply(slot) will be called and we don't
		// want slot to be destroyed because it timed out...
		if ( slot->isDoneReading() && slot->m_dgramsToSend <= 0 )
			continue;
		// fix if clock changed!
		if ( slot->m_lastReadTime > now ) slot->m_lastReadTime = now;
		if ( slot->m_lastSendTime > now ) slot->m_lastSendTime = now;
		// get time elapsed since last read
		int64_t elapsed = now - slot->m_lastReadTime;
		// set all timeouts to 4 secs if we are shutting down
		if ( m_isShuttingDown && slot->m_timeout > 4 ) 
			slot->m_timeout = 4;
		// if we don't get any activity on the slot for 30 ms
		// that often means the other side has lost the token
		/*
		if ( (slot == *s_token || slot == s_local) && elapsed >= 30 ) {
			//  slot->getNumAcksRead() <= 1 ) {
			log("Token freed up (no more acks/dgrams read) for "
			    "transId=%"INT32" msgType=0x%hhx weInitiated=%08"XINT32"",
			    slot->m_transId , slot->m_msgType,
			    (int32_t)slot->m_callback);
			// debug msg
			log("s_token released");
			// ok, release it so we can blast msgs to remote hosts
			*s_token = NULL;
			s_local  = NULL;
		}
		*/
		// . deal w/ slots that are timed out
		// . could be 1 of the 4 things:
		// . 1. they take too long to send their reply
		// . 2. they take too long to send their request
		// . 3. they take too long to ACK our reply 
		// . 4. they take too long to ACK our request
		// . only flag it if we haven't already...
		if ( elapsed >= ((int64_t)slot->m_timeout) * 1000LL &&
		     slot->m_errno != EUDPTIMEDOUT ) {
			// . set slot's m_errno field
			// . makeCallbacks_ass() should call its callback
			slot->m_errno = EUDPTIMEDOUT;
			// prepare to call the callback by adding it to this
			// special linked list
			addToCallbackLinkedList ( slot );
			// let caller know we did something
			something = true;
			// keep going
			continue;
		}
		// how long since last send?
		int64_t delta = now - slot->m_lastSendTime;
		// if elapsed is negative, then someone changed the system
		// clock on us, so it won't hurt to resend just to update
		// otherwise, we could be waiting years to resend
		if ( delta < 0 ) delta = slot->m_resendTime;
		// continue if we just sent something
		if ( delta < slot->m_resendTime ) continue;
		// . if this host went dead on us all of a sudden then force
		//   a time out
		// . only perform this check once every .5 seconds at most
		//   to prevent performance degradation
		// . REMOVED BECAUSE: this prevents msg 0x011 (pings) from 
		//   getting through!
		/*
		if ( now - s_lastDeadCheck >= 500 ) {
			// set for next time
			s_lastDeadCheck = now;
			// get Host entry
			Host *host = NULL;
			// if hostId provided use that
			if ( slot->m_hostId >= 0 ) 
				host=g_hostdb.getHost ( slot->m_hostId );
			// get host entry from ip/port
			else	host=g_hostdb.getHost(slot->m_ip,slot->m_port);
			// check if dead
			if ( host && g_hostdb.isDead ( host ) ) {
				// if so, destroy this slot
				g_errno = EHOSTDEAD;
				makeCallback_ass ( slot );
				return;
			}
		}
		*/
		// if we don't have anything ready to send continue
		if ( slot->m_dgramsToSend <= 0 ) continue;
		// if shutting down, rather than resending the reply, just
		// force it as if it were sent. then makeCallbacks can 
		// destroy it.
		if ( m_isShuttingDown ) {
			// do not let this function free the buffers, they
			// may not be allocated really. this may cause a memory
			// leak.
			slot->m_readBuf      = NULL;
			slot->m_sendBufAlloc = NULL;
			// just nuke the slot... this will leave the memory
			// leaked... (memleak, memory leak, memoryleak)
			destroySlot ( slot );
			continue;
		}
		// should we resend all dgrams?
		bool resendAll = false;
		// . HACK: if our request was sent but 30 seconds have passed
		//   and we got no reply, resend our whole request!
		// . this fixes the stuck Msg10 fiasco because it uses
		//   timeouts of 1 year
		// . this is mainly for msgs with infinite timeouts
		// . so if recpipient crashes and comes back up later then
		//   we can resend him EVERYTHING!!
		// . TODO: what if we get reply before we sent everything!?!?
		// . if over 30 secs has passed, resend it ALL!!
		// . this will reset the sent bits and read ack bits
		if ( slot->m_sentBitsOn == slot->m_readAckBitsOn ) {
			// give him 30 seconds to send a reply 
			if ( elapsed < 30000 ) continue;
			// otherwise, resend the whole thing, he
			resendAll = true;
		}

		//
		// SHIT, sometimes a summary generator on a huge asian lang
		// page takes over 1 second and we are unable to send acks
		// for an incoming msg20 request etc, and this code triggers..
		// maybe QUICKPOLL(0) should at least send/read the udp ports?
		//
		// FOR NOW though since hosts do not go down that much
		// let's also require that it has been 5 secs or more...
		//

		int32_t timeout = 5000;
		// spider time requests typically have timeouts of 1 year!
		// so we end up waiting for the host to come back online
		// before the spider can proceed.
		if ( slot->m_niceness ) timeout = slot->m_timeout;

		// check it
		if ( slot->m_maxResends >= 0 &&
		     // if maxResends it 0, do not do ANY resend! just err out.
		     slot->m_resendCount >= slot->m_maxResends &&
		     // did not get all acks
		     slot->m_sentBitsOn > slot->m_readAckBitsOn &&
		     // fix too many timing out slot msgs when a host is
		     // hogging the cpu on a niceness 0 thing...
		     //elapsed > 5000 &&
		     // respect slot's timeout too!
		     elapsed > timeout &&
		     // only do this when sending a request
		     slot->m_callback ) {
			// should this be ENOACK or something?
			slot->m_errno = EUDPTIMEDOUT;
			// prepare to call the callback by adding it to this
			// special linked list
			addToCallbackLinkedList ( slot );
			// let caller know we did something
			something = true;
			// note it
			log("udp: Timing out slot (msgType=0x%"XINT32") "
			    "after %"INT32" resends. hostid=%"INT32" "
			    "(elapsed=%"INT64")" ,
			    (int32_t)slot->m_msgType, 
			    (int32_t)slot->m_resendCount ,
			    slot->m_hostId,elapsed);
			// keep going
			continue;
		}			
		// . this should clear the sentBits of all unacked dgrams
		//   so they can be resent
		// . this doubles m_resendTime and updates m_resendCount
		slot->prepareForResend ( now , resendAll );
		// . we resend our first unACKed dgram if some time has passed
		// . send as much as we can on this slot
		doSending_ass ( slot , true /*allow resends?*/ , now );
		// return if we had an error sending, like EBADF we get
		// when we've shut down the servers...
		if ( g_errno == EBADF ) return something;
		//slot->sendDatagramOrAck(m_sock,true/*resends?*/,m_niceness);
		// always call this after every send/read
		//makeCallback_ass ( slot );
		something = true;
	}
	// return true if we did something
	return something;
}

// . IMPORTANT: only called for transactions that we initiated!!!
//   so we know to set the key.n0 hi bit
// . may be called twice on same slot by Multicast::destroySlotsInProgress()
void UdpServer::destroySlot ( UdpSlot *slot ) {
	// ensure not in a signal handler
	if ( g_inSigHandler ) {
		log(LOG_LOGIC,"udp: destroySlot: Called in sig handler.");
		return;
	}
	// return if no slot
	if ( ! slot ) return;
	// core if we should
	if ( slot->m_coreOnDestroy ) { char *xx = NULL; *xx = 0; }
	// if we're deleting a slot that was an incoming request then
	// decrement m_requestsInWaiting (exclude pings)
	if ( ! slot->m_callback && slot->m_msgType != 0x11 ) {
		// one less request in waiting
		m_requestsInWaiting--;
		// special count
		if ( slot->m_msgType == 0x07 ) m_msg07sInWaiting--;
		if ( slot->m_msgType == 0x10 ) m_msg10sInWaiting--;
		if ( slot->m_msgType == 0xc1 ) m_msgc1sInWaiting--;
		//if ( slot->m_msgType == 0xd  ) m_msgDsInWaiting--;
		//if ( slot->m_msgType == 0x23 ) m_msg23sInWaiting--;
		if ( slot->m_msgType == 0x25 ) m_msg25sInWaiting--;
		if ( slot->m_msgType == 0x50 ) m_msg50sInWaiting--;
		if ( slot->m_msgType == 0x39 ) m_msg39sInWaiting--;
		if ( slot->m_msgType == 0x20 ) m_msg20sInWaiting--;
		if ( slot->m_msgType == 0x2c ) m_msg2csInWaiting--;
		if ( slot->m_msgType == 0x0c ) m_msg0csInWaiting--;
		if ( slot->m_msgType == 0x00 ) m_msg0sInWaiting--;
		// debug msg, good for msg routing distribution, too
		//log("in waiting down to %"INT32" (0x%hhx) ",
		//     m_requestsInWaiting, slot->m_msgType );
		// resume the low priority udp server
		if ( m_requestsInWaiting <= 0 && this == &g_udpServer2 )
			g_udpServer.resume();	
	}

	// don't let sig handler look at slots while we are destroying them
	bool flipped = interruptsOff();
	// save buf ptrs so we can free them
	char *rbuf     = slot->m_readBuf;
	int32_t  rbufSize = slot->m_readBufMaxSize;
	char *sbuf     = slot->m_sendBufAlloc;
	int32_t  sbufSize = slot->m_sendBufAllocSize;
	// don't free our static buffer
	if ( rbuf == slot->m_tmpBuf ) rbuf = NULL;
	// sometimes handlers will use our slots m_tmpBuf to store the reply
	if ( sbuf == slot->m_tmpBuf ) sbuf = NULL;
	// nothing allocated. used by Msg13.cpp g_fakeBuf
	if ( sbufSize == 0 ) sbuf = NULL;
	// NULLify here now just in case
	slot->m_readBuf      = NULL;
	slot->m_sendBuf      = NULL;
	slot->m_sendBufAlloc = NULL;
	// . sig handler may allocate new read buf here!!!!... but not now
	//   since we turned interrupts off
	// . free this slot available right away so sig handler won't
	//   write into m_readBuf or use m_sendBuf, but it may claim it!
	freeUdpSlot_ass ( slot );
	// turn em back on if they were on before
	if ( flipped ) interruptsOn();
	// free the send/read buffers
	if ( rbuf ) mfree ( rbuf , rbufSize , "UdpServer");
	if ( sbuf ) mfree ( sbuf , sbufSize , "UdpServer");
	// mark down used memory
	//s_udpMem -= slot->m_readBufSize    ;
	//s_udpMem -= slot->m_sendBufAllocSize ;

	// get the key of this slot...
	//key_t key = slot->getKey();
	//#ifdef _UDPDEBUG_		
	//log("destroy slot=%"INT32" state=%"INT32" transId=%"INT32"",
	//    (int32_t)slot,(int32_t)slot->m_state,slot->m_transId);
	//#endif
	// now that we have one less slot we may be able to shutdown
	//if ( m_isShuttingDown ) tryShuttingDown ( true );
}



// . called once per second from Process.cpp::shutdown2() when we are trying
//   to shutdown
// . we'll stop answering ping requests
// . we'll wait for replies to those notes, but timeout is 3 seconds
//   we're shutting down so they won't bother sending requests to us
// . this will wait until all fully received requests have had their
//   reply sent to them
// . in the meantime it will send back error replies to all new 
//   incoming requests
// . this will do a blocking close on the listening socket descriptor
// . this will call the callback when shutdown was completed
// . returns false if blocked, true otherwise
// . set g_errno on error
bool UdpServer::shutdown ( bool urgent ) {

	if      ( ! m_isShuttingDown && m_port == 0 )
		log(LOG_INFO,"gb: Shutting down dns resolver.");
	else if ( ! m_isShuttingDown ) 
		log(LOG_INFO,"gb: Shutting down udp server port %hu.",m_port);

	// so we know not to accept new connections
	m_isShuttingDown = true;

	// wait for all transactions to complete
	time_t now = getTime();
	int32_t count = 0;
	if(!urgent) {
		//if ( m_head && m_head2->m_next2 ) return false;	      
		for ( UdpSlot *slot = m_head2 ; slot ; slot = slot->m_next2 ) {
			// if we initiated, then don't count it
			if ( slot->m_callback ) continue;
			// don't bother with pings or other hosts shutdown 
			if ( slot->m_msgType == 0x11 ) continue;
			// set all timeouts to 3 secs
			if ( slot->m_timeout > 3 ) slot->m_timeout = 3;
			// . don't count lagging slots that haven't got 
			//   a read in 5 sec
			if ( now - slot->m_lastReadTime > 5 ) continue;
			// don't count if timer fucked up
			if ( now - slot->m_lastReadTime < 0 ) continue;
			// count it
			count++;
		}
	}
	if ( count > 0 ) {
		log(LOG_LOGIC,"udp: stilll processing udp traffic after "
		    "shutdown note was sent.");
		return false;
	}

	if ( m_port == 0 )
		log(LOG_INFO,"gb: Closing dns resolver.");
	else
		log(LOG_INFO,"gb: Closing udp server socket port %hu.",m_port);

	// close our socket descriptor, may block to finish sending
	int s = m_sock;
	// . make it -1 so thread exits
	// . g_process.shutdown2() will wait untill all threads exit before
	//   exiting the main process
	// . the timepollwrapper should kick our udp thread out of its 
	//   lock on recvfrom so that it will see that m_sock is -1 and
	//   it will exit
	m_sock = -1;
	// then close it
	close ( s );

	if ( m_port == 0 )
		log(LOG_INFO,"gb: Shut down dns resolver successfully.");
	else
		log(LOG_INFO,"gb: Shut down udp server port %hu successfully.",
		    m_port);

	// all done
	return true;
}

bool UdpServer::timeoutDeadHosts ( Host *h ) {
	// signal handler cannot call this
	if ( g_inSigHandler ) return false;
	// we never have a request out to a proxy, and if we
	// do take the proxy down i don't want us timing out gk0
	// or gk1! which have hostIds 0 and 1, like the proxy0
	// and proxy1 do...
	if ( h->m_isProxy ) return true;
	// get time now
	//time_t now = getTime();
	// find sockets out to dead hosts and change the timeout
	for ( UdpSlot *slot = m_head2 ; slot ; slot = slot->m_next2 ) {
		// only change requests to dead hosts
		if ( slot->m_hostId < 0 ) continue;
		//! g_hostdb.isDead(slot->m_hostId) ) continue;
		if ( slot->m_hostId != h->m_hostId ) continue;
		// if we didn't initiate, then don't count it
		if ( ! slot->m_callback ) continue;
		// don't bother with pings or other hosts shutdown broadcasts
		if ( slot->m_msgType == 0x11 ) continue;
		// don't change msg1, Multicast will deal with this
		//if ( slot->m_msgType == 0x1 ) continue;
		// set all timeouts to 5 secs
		//if ( slot->m_timeout > 1 ) slot->m_timeout = 1;
		slot->m_timeout = 0;
	}
	return true;
}

// verified that this is not interruptible
UdpSlot *UdpServer::getEmptyUdpSlot_ass ( key_t k , bool incoming ) {
	// turn em off
	bool flipped = interruptsOff();
	// tmp debug
	//if ( (rand() % 10) == 1 ) slot = NULL
	// return NULL if none left
	if ( ! m_head ) { 
		g_errno = ENOSLOTS;
		log("udp: %"INT32" of %"INT32" udp slots occupied. None available to "
		    "handle this new transaction.",
		    (int32_t)m_numUsedSlots,(int32_t)m_maxSlots);
		if ( flipped ) interruptsOn();
		return NULL;
	}
	UdpSlot *slot = m_head;
	// remove from linked list of available slots
	m_head = m_head->m_next;
	// add to linked list of used slots
	//slot->m_next2    = m_head2;
	//slot->m_prev2    = NULL;
	//if ( m_head2 ) m_head2->m_prev2 = slot;
	//m_head2          = slot;
	// put the used slot at the tail so older slots are at the head and
	// makeCallbacks() can take care of the callbacks that have been
	// waiting the longest first...
	if ( m_tail2 ) {
		slot->m_next2    = NULL;
		slot->m_prev2    = m_tail2;
		m_tail2->m_next2 = slot;
		m_tail2          = slot;
	}
	else {
		slot->m_next2    = NULL;
		slot->m_prev2    = NULL;
		m_head2          = slot;
		m_tail2          = slot;
	}
	// also to callback candidates if we should
	// if ( hasCallback ) {
	// 	slot->m_next3    = m_head3;
	// 	slot->m_prev3    = NULL;
	// 	if ( m_head3 ) m_head3->m_prev3 = slot;
	// 	m_head3          = slot;
	// }
	// count it
	m_numUsedSlots++;

	if ( incoming ) m_numUsedSlotsIncoming++;

	slot->m_incoming = incoming;

	// now store ptr in hash table
	slot->m_key = k;
	addKey ( k , slot );
	if ( flipped ) interruptsOn();
	return slot;
}

void UdpServer::addKey ( key_t k , UdpSlot *ptr ) {

	// we assume that k.n1 is the transId. if this changes we should
	// change this to keep our hash lookups fast
	int32_t i = hashLong(k.n1) & m_bucketMask;
	while ( m_ptrs[i] )
		if ( ++i >= m_numBuckets ) i = 0;
	m_ptrs[i] = ptr;
}

// verify that interrupts are always off before calling this
UdpSlot *UdpServer::getUdpSlot ( key_t k ) {
	// . hash into table
	// . transId is key.n1, use that as hash
	// . m_numBuckets must be a power of 2
	int32_t i = hashLong(k.n1) & m_bucketMask;
	while ( m_ptrs[i] && m_ptrs[i]->m_key != k ) 
		if ( ++i >= m_numBuckets ) i = 0;
	// if empty, return NULL
	return m_ptrs[i];
}

void UdpServer::addToCallbackLinkedList ( UdpSlot *slot ) {
	// debug log
	if ( g_conf.m_logDebugUdp && slot->m_errno )
		log("udp: adding slot with err = %s to callback list"
		    , mstrerror(slot->m_errno) );
	if ( g_conf.m_logDebugUdp )
		log("udp: adding slot=%"PTRFMT" to callback list"
		    ,(PTRTYPE)slot);
	// must not be in there already, lest we double add it
	if ( isInCallbackLinkedList ( slot ) ) {
		if ( g_conf.m_logDebugUdp )
			log("udp: avoided double add slot=%"PTRFMT
			    ,(PTRTYPE)slot);
		return;
	}
	slot->m_next3 = NULL;
	slot->m_prev3 = NULL;
	if ( ! m_tail3 ) {
		m_head3 = slot;
		m_tail3 = slot;
	}
	else {
		// insert at end of linked list otherwise
		m_tail3->m_next3 = slot;
		slot->m_prev3 = m_tail3;
		m_tail3 = slot;
	}
}

bool UdpServer::isInCallbackLinkedList ( UdpSlot *slot ) {
	// return if not in the linked list
	if ( slot->m_prev3 ) return true;
	if ( slot->m_next3 ) return true;
	if ( m_head3 == slot ) return true;
	return false;
}

void UdpServer::removeFromCallbackLinkedList ( UdpSlot *slot ) {

	if ( g_conf.m_logDebugUdp )
		log("udp: removing slot=%"PTRFMT" from callback list"
		    ,(PTRTYPE)slot);

	// return if not in the linked list
	if ( slot->m_prev3 == NULL && 
	     slot->m_next3 == NULL && 
	     m_head3 != slot )
		return;

	// excise from linked list otherwise
	if ( m_head3 == slot )
		m_head3 = slot->m_next3;
	if ( m_tail3 == slot )
		m_tail3 = slot->m_prev3;

	if ( slot->m_prev3 ) 
		slot->m_prev3->m_next3 = slot->m_next3;
	if ( slot->m_next3 ) 
		slot->m_next3->m_prev3 = slot->m_prev3;

	// and so we do not try to re-excise it
	slot->m_prev3 = NULL;
	slot->m_next3 = NULL;
}

// verified that this is not interruptible
void UdpServer::freeUdpSlot_ass ( UdpSlot *slot ) {
	bool flipped = interruptsOff();
	// set the new head/tail if we were it
	if ( slot == m_tail2 ) m_tail2 = slot->m_prev2;
	if ( slot == m_head2 ) m_head2 = slot->m_next2;
	// remove from linked list of used slots
	if ( slot->m_prev2 ) slot->m_prev2->m_next2 = slot->m_next2;
	if ( slot->m_next2 ) slot->m_next2->m_prev2 = slot->m_prev2;
	// also from callback candidates if we should
	removeFromCallbackLinkedList ( slot );
	// discount it
	m_numUsedSlots--;

	if ( slot->m_incoming ) m_numUsedSlotsIncoming--;

	// add to linked list of available slots
	slot->m_next = m_head;
	m_head = slot;
	// . get bucket number in hash table
	// . may have change since table often gets rehashed
	key_t k = slot->m_key;
	int32_t i = hashLong(k.n1) & m_bucketMask;
	while ( m_ptrs[i] && m_ptrs[i]->m_key != k ) 
		if ( ++i >= m_numBuckets ) i = 0;
	// sanity check
	if ( ! m_ptrs[i] ) {
		log(LOG_LOGIC,"udp: freeUdpSlot_ass: Not in hash table.");
		char *xx = NULL; *xx = 0;
	}
	if ( g_conf.m_logDebugUdp )
		log(LOG_DEBUG,"udp: freeUdpSlot_ass: Freeing slot "
		    "tid=%"INT32" "
		    "dst=%s:%"UINT32" slot=%"PTRFMT"",
		    slot->m_transId,
		    iptoa(slot->m_ip)+6,
		    (uint32_t)slot->m_port,
		    (PTRTYPE)slot);
	// remove the bucket
	m_ptrs [ i ] = NULL;
	// rehash all buckets below
	if ( ++i >= m_numBuckets ) i = 0;
	// keep looping until we hit an empty slot
	while ( m_ptrs[i] ) {
		UdpSlot *ptr = m_ptrs[i];
		m_ptrs[i] = NULL;
		// re-hash it
		addKey ( ptr->m_key , ptr );
		if ( ++i >= m_numBuckets ) i = 0;		
	}
	// was it top? if so, fix it
	//if ( i >= m_topUsedSlot ) 
	//	while ( m_topUsedSlot >= 0 && isEmpty(m_topUsedSlot) ) 
	//		m_topUsedSlot--;
	// the low priority server may have been waiting for high to finish
	//if ( m_topUsedSlot < 0 && this==&g_udpServer2 ) g_udpServer.resume();
	// if we turned 'em off then turn 'em back on
	if ( flipped ) interruptsOn();
}

void UdpServer::cancel ( void *state , unsigned char msgType ) {
	// . if we have transactions in progress wait
	// . but if we're waiting for a reply, don't bother
	for ( UdpSlot *slot = m_head2 ; slot ; slot = slot->m_next2 ) {
		// skip if not a match
		if ( slot->m_state  != state ) continue;
		// -1 (0xff) means all messages from this state
		if ( msgType != 0xff && slot->m_msgType != msgType ) continue;
		// note it
		log(LOG_INFO,"udp: cancelled udp socket. msgType=0x%hhx.",
		    slot->m_msgType);
		// let them know why we are calling the callback prematurely
		g_errno = ECANCELLED;
		// stop waiting for reply, this will call destroySlot(), too
		makeCallback_ass ( slot );
	}
}

void UdpServer::replaceHost ( Host *oldHost, Host *newHost ) {
	bool flipped = interruptsOff();
	log ( LOG_INFO, "udp: Replacing slots for ip: "
	      "%"UINT32"/%"UINT32" port: %"UINT32"",
	      (uint32_t)oldHost->m_ip, 
	      (uint32_t)oldHost->m_ipShotgun,
	      (uint32_t)oldHost->m_port );//, oldHost->m_port2 );
	// . loop over outstanding transactions looking for ones to oldHost
	for ( UdpSlot *slot = m_head2; slot; slot = slot->m_next2 ) {
		// ignore incoming
		if ( ! slot->m_callback ) continue;
		// check for ip match
		if ( slot->m_ip != oldHost->m_ip &&
		     slot->m_ip != oldHost->m_ipShotgun )
			continue;
		// check for port match
		if ( this == &g_udpServer && slot->m_port != oldHost->m_port )
			continue;
		//if(this== &g_udpServer2 && slot->m_port != oldHost->m_port2 )
		//	continue;
		// . match, replace the slot ip/port with the newHost
		// . first remove the old hashed key for this slot
		// . get bucket number in hash table
		// . may have change since table often gets rehashed
		key_t k = slot->m_key;
		int32_t i = hashLong(k.n1) & m_bucketMask;
		while ( m_ptrs[i] && m_ptrs[i]->m_key != k ) 
			if ( ++i >= m_numBuckets ) i = 0;
		// sanity check
		if ( ! m_ptrs[i] ) {
			log(LOG_LOGIC,"udp: replaceHost: Slot not in hash "
				      "table.");
			char *xx = NULL; *xx = 0;
		}
		if ( g_conf.m_logDebugUdp )
			log(LOG_DEBUG,
			    "udp: replaceHost: Rehashing slot "
			    "tid=%"INT32" dst=%s:%"UINT32" "
			    "slot=%"PTRFMT"",
				      slot->m_transId,
				      iptoa(slot->m_ip)+6,
				      (uint32_t)slot->m_port,
				      (PTRTYPE)slot);
		// remove the bucket
		m_ptrs [ i ] = NULL;
		// rehash all buckets below
		if ( ++i >= m_numBuckets ) i = 0;
		// keep looping until we hit an empty slot
		while ( m_ptrs[i] ) {
			UdpSlot *ptr = m_ptrs[i];
			m_ptrs[i] = NULL;
			// re-hash it
			addKey ( ptr->m_key , ptr );
			if ( ++i >= m_numBuckets ) i = 0;		
		}

		// careful with this! if we were using shotgun, use that
		// otherwise We core in PingServer because the 
		// m_inProgress[1-2] does net mesh
		if ( slot->m_ip == oldHost->m_ip )
			slot->m_ip = newHost->m_ip;
		else
			slot->m_ip = newHost->m_ipShotgun;

		// replace the data in the slot
		slot->m_port = newHost->m_port;
		//if ( this == &g_udpServer ) slot->m_port = newHost->m_port;
		//else			      slot->m_port = newHost->m_port2;
		//slot->m_transId = getTransId();
		// . now readd the slot to the hash table
		key_t key = m_proto->makeKey ( slot->m_ip,
					       slot->m_port,
					       slot->m_transId,
					       true/*weInitiated?*/);
		addKey ( key, slot );
		slot->m_key = key;
		slot->resetConnect();
		// log it
		log ( LOG_INFO, "udp: Reset Slot For Replaced Host: "
				"transId=%"INT32" msgType=%i",
				slot->m_transId, slot->m_msgType );
	}
	if ( flipped ) interruptsOn();
}


void UdpServer::printState() {
	log(LOG_TIMING, 
	    "admin: UdpServer - ");

	for ( UdpSlot *slot = m_head2 ; slot ; slot = slot->m_next2 ) {
		slot->printState();
	}	
}
