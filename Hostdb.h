// Matt Wells, Copyright Nov 2000

// . every host (aka server) has a unique positive 32 bit hostId
// . to get the groupId of a hostId we must reverse the bits and then
//   AND the result with g_hostdb.m_groupMask
// . host record is about 32 bytes 
// . we keep a list of all hosts in the conf file
// . this new model is much easier to work with
// . format of hosts in the xml conf file
// . <host0> <ip> 1.2.3.4 </> <port> 1234 </>

#ifndef _HOSTDB_H_
#define _HOSTDB_H_

#include <sys/ioctl.h>            // ioctl() - get our ip address from a socket
#include <net/if.h>               // for struct ifreq passed to ioctl()    
//#include "../../rsa/rsa.h"        // public_key private_key vlong (seals)
#include "Conf.h"       // for getting m_groupId/m_groupMask
#include "Xml.h" // host file in xml

// the default mattster udp port (also re-defined in conf/Conf.cpp) TODO: unify
//#define DEFAULTPORT  55
// the default high priority udp port (these requests are handled first)
//#define DEFAULTPORT2 56

// what ping time constitutes a dead host (in milliseconds)?
//#define DEAD_HOST_PING 5500
// when renaming over 100 titledb files on ide drive it seems to take
// more than 10 seconds some times, i think just when the neighboring ide
// drive is also busy reading/writing. let's try 16.5 seconds.
// But now we use threaded renames and unlinks so put back down to 7500
// I get too many false positives when at 9500 for some reason... i think 
// when we get hit with a query bomb... so go from 9500 to 16500
// This is now g_conf.m_deadHostTimeout and configurable.
//#define DEAD_HOST_PING 16500

enum {
	ME_IOERR = 1,
	ME_100MBPS,
	ME_UNKNWN
};

// for the Host::m_flags
#define PFLAG_HASSPIDERS     0x01
#define PFLAG_MERGING        0x02
#define PFLAG_DUMPING        0x04
// these two flags are used by DailyMerge.cpp to sync the daily merge
// between all the hosts in the cluster
#define PFLAG_MERGEMODE0     0x08
#define PFLAG_MERGEMODE0OR6  0x10

// added slow disk reads to it, 4 bytes (was 52)
#define MAX_PING_SIZE 44

#define HT_GRUNT   0x01
#define HT_SPARE   0x02
#define HT_PROXY   0x04
#define HT_QCPROXY 0x08
#define HT_SCPROXY 0x10
#define HT_ALL_PROXIES (HT_PROXY|HT_QCPROXY|HT_SCPROXY)

class EventStats {
public:
	long m_expired;
	long m_active;

	// these are all active
	long m_experimental;
	long m_resultSet1;
	long m_resultSetOther;
	long m_facebook;
	long m_badGeocoder;

	void clear ( ) { memset ( this , 0 , sizeof(EventStats) ); };
};

class Host {

 public:

	//bool isDead ( ) { return m_hostdb->m_isDead ( this ); };

	long           m_hostId ;
	unsigned long  m_groupId ;
	//long           m_groupNum;
//	char       m_pubKey[20];     // 128bit(16 byte) and a short/long
	unsigned long  m_ip ;        // used for internal communcation (udp)

	// what stripe are we in? i.e. what twin number are we?
	long           m_stripe;

	// . these are used for talking w/ a particular host from the outside
	// . usually m_extHttpPort = m_httpPort, but if you are going through
	//   a router it can do portmapping. It might portmap 80 to 8000 so
	//   you should use 80 here then.
	//unsigned long  m_externalIp; // used for web-based GUI, to the world
	// this ip is now the shotgun ip. the problem with an "external" ip
	// is that people internal to the network cannot usually see it!!
	// so now this is just a secondary regular ip address for servers with
	// dual gigabit ethernet ports. make performance much better.
	unsigned long  m_ipShotgun;  
	unsigned short m_externalHttpPort;
	unsigned short m_externalHttpsPort;

	// used by Process.cpp to do midnight stat dumps and emails
	EventStats m_eventStats;

//	time_t     m_lastComm ;      // time of last communication
//	time_t     m_lastAttempt ;   // time of last attempted communication
//	long       m_bandwidth;      // bytes per second
//	long long  m_bytesRecvdFrom; // why do we need this?
//	long long  m_bytesSentTo;    // why do we need this?
	unsigned short m_port ;          // Mattster Protocol (MP) UDP port
	//unsigned short m_portShotgun;    // for shotgunning
	//unsigned short m_port2;          // the high priority port
	unsigned short m_httpPort ;      // http port
	unsigned short m_httpsPort;
	//long       m_pingAvg ;       // in ms
	//long       m_pingStdDev;     // in ms
	//long       m_pings[4];       // our window of the last 4 pings
	long           m_ping;
	long           m_pingShotgun;
	long           m_pingMax;
	// have we ever got a ping reply from him?
	char           m_gotPingReply;
	double         m_loadAvg;
	float          m_percentMemUsed;
	// the first time we went OOM (out of mem, i.e. >= 99% mem used)
	long long      m_firstOOMTime;
	// cpu usage
	float          m_cpuUsage;

	long          m_slowDiskReads;

	// doc count
	long          m_docsIndexed;
	long          m_urlsIndexed;

	long          m_eventsIndexed;

	// did gb log system errors that were given in g_conf.m_errstr ?
	char           m_kernelErrors;
	bool           m_kernelErrorReported;

	long           m_flags;

	// used be SEO pipeline in xmldoc.cpp
	long           m_numOutstandingRequests;

	// used by DailyMerge.cpp exclusively
	collnum_t      m_dailyMergeCollnum;

	// last time g_hostdb.ping(i) was called for this host in milliseconds.
	long long      m_lastPing;
	// . first time we sent an unanswered ping request to this host
	// . used so we can determine when to send an email alert
	long long      m_startTime;
	// is a ping in progress for this host?
	char           m_inProgress1;
	// shotgun
	char           m_inProgress2;
	long long      m_numPingReplies;

	// send to eth 0 or 1 when sending to this host?
	char           m_preferEth;

	// machine #, a machine can have several hosts on it
	long           m_machineNum;
	// . this is used for sending email alerts to admin about dead hosts
	// .  0 means we can send an email for this host if he goes dead on us
	// . +1 means we already sent an email for him since he was down and 
	//      he hasn't come back up since
	// . -1 means he went down gracefully so no alert is needed
	// . -2 means he went down while g_conf.m_sendEmailAlert was false
	// . -3 means another host was responsible for sending to him, not us
	// . -4 means we are currently in progress sending an email for him
	// . -5 means he went down before host we alerted admin about revived
	long           m_emailCode;
	// . ide channel
	// . Msg34 will add disk load from other gigablast process that are
	//   using your channel and your ip
	long           m_ideChannel;
	long           m_tokenGroupNum;
	// 0 means no, 1 means yes, 2 means unknown
	char           m_syncStatus;

	// we now include the working dir in the hosts.conf file
	// so main.cpp can do gb --install and gb --allstart
	char           m_dir [ 128 ];

	char           m_hostname[16];

	// the secondary ethernet interface hostname
	char           m_hostname2[16];

	// client port
	unsigned short m_dnsClientPort;

	// . what set the host is in
	// . its redundant twins are always in different sets
	long           m_group;
	// was host in gk0 cluster and retired because its twin got
	// ssds, so it was no longer really needed.
	bool           m_retired;
	// used for logging when a host goes dead for the first time
	bool           m_wasAlive;
	bool           m_wasEverAlive;
	long long      m_timeOfDeath;
	// this toggles between 0 and 1 for alternating packet sends to
	// eth0 and eth1 of this host
	char           m_shotgunBit;
	// how many ETRYAGAINs we received as replies from this host
	long           m_etryagains;
	// how many resends total we had to do to this host
	long           m_totalResends;
	// how many total error replies we got from this host
	long           m_errorReplies;

	// now include a note for every host
	char           m_note[128];
	char           m_doingSync;
	char           m_switchId;
	char           m_onProperSwitch;

	bool           m_isProxy;

	// used for compressing spidered docs before sending back to local
	// network to save on local loop bandwidth costs
	char           m_type;

	// for m_type == HT_QCPROXY, we forward the query to the regular proxy
	// at this Ip:Port. we should receive a compressed 0xfd reply and
	// we uncompress it and return it to the browser.
	long           m_forwardIp;
	short          m_forwardPort;

	long long      m_dgramsTo;
	long long      m_dgramsFrom;

	char           m_repairMode;

	// for timing how long the msg39 takes from this host
	long           m_splitsDone;
	long long      m_splitTimes;

	// . the hostdb to which this host belongs!
	// . getHost(ip,port) will return a Host ptr from either 
	//   g_hostdb or g_hostdb2, so UdpServer.cpp needs to know which it
	//   is from when making the UdpSlot key.
	class Hostdb  *m_hostdb;

	// . temps in celsius of the hard drives
	// . set in Process.cpp
	short          m_hdtemps[4];

	// Syncdb.cpp uses these
	char           m_inSync ;
	char           m_isPermanentOutOfSync ;

	char  m_requestBuf[MAX_PING_SIZE];
};

#define MAX_HOSTS 512
#define MAX_SPARES 64
#define MAX_PROXIES 6

// . this is used by Bandwidth.h and Msg21.cpp
// . it should really be about the same as MAX_HOSTS if we have one host
//   per machine
#define MAX_MACHINES 256

// for table for mapping key to a groupid
#define MAX_KSLOTS 8192

class Hostdb {

 public:

	// . set up our Hostdb
	// . sets itself from g_conf (our configuration class)
	// . returns false on fatal error
	// . gets filename from Conf.h class
	bool init ( char *filename , long hostId , char *netname = NULL,
		    bool proxyHost = false , char useTempCluster = 0 );

	// for dealing with pings
	bool registerHandler ( );

	// . to prevent script kiddies from sending bogus udp packets to
	//   our udp servers we make sure they are from an IP in our hosts.conf
	//   file. If not, then we drop it. If the script kiddie spoofed us
	//   then we just have to deal with malformed packets in the handlers.
	// . this just hashes each ip into m_ipHashes at startup and checks 
	//   for the ip in there
	//bool isIpInNetwork ( long ip );

	// . add ips from all hosts to our validation table so isIpInNetwork()
	//   returns true for them
	// . this will add the dns ips from the Conf file
	//bool validateIps ( class Conf *conf ) ;

	char *getNetName ( );

	Hostdb();
	~Hostdb();
	void reset();

	unsigned long  getMyIp         ( ) { return m_myIp; };
	unsigned short getMyPort       ( ) { return m_myPort; };
	//unsigned short getMyPort2      ( ) { return m_myPort2; };
	long           getMyHostId     ( ) { return m_hostId; };
	long           getMyMachineNum ( ) { return m_myMachineNum; };
	unsigned long  getLoopbackIp   ( ) { return m_loopbackIp; };
	Host          *getMyHost       ( ) { return m_myHost; };
	Host          *getMyGroup      ( ) { return m_myGroup; };
	bool           isMyIp ( unsigned long ip ) {
		if ( ip == m_myIp        ) return true;
		if ( ip == m_myIpShotgun ) return true;
		if ( ip == 0x0100007f    ) return true;
		return false;
	};

	// . one machine may have several hosts
	// . get the machine # the hostId resides on
	long getMachineNum ( long hostId ) {
		return getHost(hostId)->m_machineNum; };

	long getNumMachines ( ) { return m_numMachines; };

	// . some utility routines
	// . hostIds go from 0 to N-1 where N is # of particiapting hosts
	// . groupIds have hi bits set first & depend on # of groups
	// . groupMask is just the highest groupId
	long          makeHostId     ( unsigned long groupId ) ;
	unsigned long makeGroupId    ( long hostId    , long numGroups );
	unsigned long makeGroupMask  ( long numGroups ) ;

	// uses a table
	long          makeHostIdFast ( unsigned long groupId ) ;

	// we consider the host dead if we didn't get a ping reply back
	// after 10 seconds
	bool  isDead ( long hostId ) ;

	bool  isDead ( Host *h ) ;

	long getAliveIp ( Host *h ) ;

	bool hasDeadHost ( );

	bool kernelErrors (Host *h) { return h->m_kernelErrors; };

	long long getNumGlobalRecs ( );

	long long getNumGlobalEvents ( );

	// . returns false if blocks and will call your callback later
	// . returns true if doesn't block
	// . sets errno on error
	// . used to get array of hosts in 1 group usually for transmitting to
	// . returned group includes all hosts in host "hostId"'s group
	// . RdbList will be populated with the hosts in that group
	// . we do not create an RdbList, you must do that
	// . callback passes your RdbList back to you
	Host *getGroup ( unsigned long groupId , long *numHosts = NULL );

	Host *getGroupFromGroupId ( unsigned long gid ) {
		return getGroup ( gid ); 
	};

	Host *getGroupFromGroupNum ( long groupNum ) {
		// the array of Hosts that this points into is sorted
		// by groupId first, so we should be ok
		return m_groups[groupNum]; 
	};

	// the row #
	long getStripe ( unsigned long groupId ) {
		Host *h = getGroup ( groupId );
		if ( ! h ) return -1;
		return h->m_stripe;//groupNum;
	};

	// the column #
	long getGroupNum ( unsigned long groupId ) {
		Host *h = getGroup ( groupId );
		if ( ! h ) return -1;
		return h->m_group;
	};

	// quickly get the lowest hostid in group "groupId"
	Host *getHostIdFast ( unsigned long groupId );

	// hosts in a token group share a token that is required for
	// performing merges (big read/writes on disk)
	Host **getTokenGroup ( unsigned long hostId , long *numHosts = NULL ) ;

	// this is used to set the Host::m_tokenGroupNum members
	long getTokenGroupNum ( Host *ha ) ;

	// . map a group num to a groupId
	// . used by titledb.h to find groupId for a docId
	unsigned long getGroupId ( long groupNum ) {
		return m_hostPtrs[groupNum]->m_groupId; }

	unsigned long getGroupIdFromHostId ( long hostId ) {
		return m_hostPtrs[hostId]->m_groupId; };

	// get the host in this group with the smallest avg roundtrip time
	Host *getFastestHostInGroup ( unsigned long groupId );

	// . like above but just gets one host
	// Host *getHost ( long hostId ) { return m_groups[hostId]; };
	Host *getHost ( long hostId ) { return m_hostPtrs[hostId]; };
	Host *getSpare ( long spareId ) {
		return m_spareHosts[spareId]; };

	Host *getProxy ( long proxyId ) {
		return m_proxyHosts[proxyId]; };

	long  getNumHosts ( ) { return m_numHosts; };
	long  getNumProxy ( ) { return m_numProxyHosts; };
	// how many of the hosts are non-dead?
	long  getNumHostsAlive ( ) { return m_numHostsAlive; };
	long  getNumProxyAlive ( ) { return m_numProxyAlive; };
	long  getNumGroups () { return m_numGroups; };
	long  getNumIndexSplits() { return m_indexSplits; };

	// how many hosts in this group?
	long  getNumHostsPerGroup ( ) { return m_numHostsPerGroup; };

	// goes with Host::m_stripe
	long  getNumStripes ( ) { return m_numHostsPerGroup; };

	// . get a host entry from ip/port
	// . returns NULL if no match
	//Host *getHost  ( unsigned long ip , unsigned short port );
	//Host *getProxy  ( unsigned long ip , unsigned short port );

	// from http/https ports
	//Host *getHostFromTcpPort  ( unsigned long ip , unsigned short port );
	//Host *getProxyFromTcpPort ( unsigned long ip , unsigned short port );

	// . get the Host sharing m_hosts[n]'s ip and ide channel
	// . used by Msg34.cpp to compute load on the channel
	// . TODO: speed this up when we get a *lot* of hosts
	// . right now it just does a linear scan
	//Host *getSharer ( Host *h ) ;

	// . add ip to allowed packets table, if we receive a udp packet
	//   from an ip not in this list, we discard it
	// . returns true on success
	bool addIp ( long ip ) ;

	// . hostId of -1 means unknown
	//void getTimes ( long hostId , long *avg , long *stdDev );

	// . update ping time info of this host
	// . uses a 10-ping running average
	// . "tripTime" is in milliseconds
	// . hostId of -1 means unknown (will just return true)
	//void  stampHost  ( long hostId , long tripTime , bool timedOut );
	
	// hash the hosts into the hash tables for lookup
	bool  hashHosts();
	bool  hashHost ( bool udp , Host *h , uint32_t ip , uint16_t port ) ;
	long  getHostId        ( uint32_t ip , uint16_t port ) ;
	Host *getHostByIp      ( uint32_t ip ) ;
	Host *getHost          ( uint32_t ip , uint16_t port ) ;
	Host *getUdpHost       ( uint32_t ip , uint16_t port ) ;
	Host *getTcpHost       ( uint32_t ip , uint16_t port ) ;
	bool isIpInNetwork     ( uint32_t ip ) ;
	Host *getHostFromTable ( bool udp , uint32_t ip , uint16_t port ) ;



	// sets the note for a host
	bool setNote ( long hostId, char *note, long noteLen );
	bool setSpareNote ( long spareId, char *note, long noteLen );
	
	// replace a host with a spare
	bool replaceHost ( long origHostId, long spareHostId );

	void setOnProperSwitchFlags ( ) ;

	// write a hosts.conf file
	bool saveHostsConf ( );

	// sync a host with its twin
	bool syncHost ( long syncHostId, bool useSecondaryIps );
	void syncStart_r ( bool amThread );
	void syncDone ( );

	long getBestIp ( Host *h , long fromIp ) ;
	
	Host *getBestSpiderCompressionProxy ( long *key ) ;

	// only used by 'gb scale <hostdb.conf>' cmd for now
	void resetPortTables ();

	// returns best IP to use for "h" which is a host in hosts2.conf
	long getBestHosts2IP ( Host *h );

	// our host's info used by Udp* classes for internal communication
	unsigned long  m_myIp;
	unsigned long  m_myIpShotgun;
	unsigned short m_myPort;
	//unsigned short m_myPort2;
	long           m_myMachineNum;
	Host          *m_myHost;
	Host          *m_myGroup;

	// the loopback ip (127.0.0.1)
	unsigned long  m_loopbackIp;

	// . the hosts are stored from g_conf in xml to here
	// . m_hosts[i] is the ith Host entry
	Host  *m_hosts;
	long   m_numHosts;
	long   m_numHostsAlive;

	long   m_allocSize;

	// . this maps a hostId to the appropriate host!
	// . we can't use m_hosts[i] because we sort it by groupId for getGroup
	Host  *m_hostPtrs[MAX_HOSTS];

	// . m_hostIdToTokenGroupNum maps a host id to a "tgn"
	// . m_hostPtrs2 [ tgn ] is the list of Host ptrs in that host's
	//   merge token group and the size of the group is m_groupSize [ tgn ]
	// . used by getTokenGroup() which is used by Msg35.cpp
	Host  *m_hostPtrs2             [ MAX_HOSTS ] ;
	long   m_hostIdToTokenGroupNum [ MAX_HOSTS ] ;
	long   m_groupSize             [ MAX_HOSTS ];

	// we must have the same number of hosts in each group
	long   m_numHostsPerGroup;

	// store the file in m_buf
	char m_buf [MAX_HOSTS * 128];
	long m_bufSize;

	// this maps groupId to the array of hosts in that group
	Host *m_groups[MAX_HOSTS];

	long    m_numMachines;

	// the hash table of the ips in hosts.conf
	long *m_ips;
	long  m_numIps;

	// does a host share an ide channel with another host?
	//bool m_ideSharing;

	// . our group info
	long          m_hostId;      // our hostId
	long          m_numGroups;
	unsigned long m_groupId;     // hi bits are set before low bits
	unsigned long m_groupMask;   // hi bits are set before low bits
	char          m_dir[256];
	char          m_httpRootDir[256];
	char          m_logFilename[256];

	long          m_indexSplits; 

	char          m_netName[32];

	// spare hosts list
	Host *m_spareHosts[MAX_SPARES];
	long  m_numSpareHosts;

	// proxy host list
	Host *m_proxyHosts[MAX_PROXIES];
	long  m_numProxyHosts;
	long  m_numProxyAlive;

	long  m_numTotalHosts;

	bool m_initialized;

	// for sync
	Host *m_syncHost;
	bool  m_syncSecondaryIps;

	char  m_useTmpCluster;

	uint32_t getGroupId (char rdbId, void *key, bool split = true);
	uint32_t getGroupIdFromDocId ( long long d ) ;

	uint32_t m_map[MAX_KSLOTS];
};

extern class Hostdb g_hostdb;
extern class Hostdb g_hostdb2;

extern Host     *g_listHosts [ MAX_HOSTS * 4 ];
extern uint32_t  g_listIps   [ MAX_HOSTS * 4 ];
extern uint16_t  g_listPorts [ MAX_HOSTS * 4 ];
extern long      g_listNumTotal;

inline uint32_t getGroupId ( char rdbId, void *key,bool split = true) {
	return g_hostdb.getGroupId ( rdbId , key , split );
};

inline uint32_t getGroupIdFromDocId ( long long d ) {
	return g_hostdb.getGroupIdFromDocId ( d );
};

#endif
