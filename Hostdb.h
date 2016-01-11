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
//#include "../../rsa/rsa.h"        // public_key private_key vint32_t (seals)
//#include "Conf.h"       // for getting m_groupId/m_groupMask
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
#define PFLAG_REBALANCING    0x20
#define PFLAG_FOREIGNRECS    0x40
#define PFLAG_RECOVERYMODE   0x80
#define PFLAG_OUTOFSYNC      0x100

// added slow disk reads to it, 4 bytes (was 52)
// 21 bytes for the gbversion (see getVersionSize())
//#define MAX_PING_SIZE (44+4+4+21)

#define HT_GRUNT   0x01
#define HT_SPARE   0x02
#define HT_PROXY   0x04
#define HT_QCPROXY 0x08
#define HT_SCPROXY 0x10
#define HT_ALL_PROXIES (HT_PROXY|HT_QCPROXY|HT_SCPROXY)

int32_t *getLocalIps ( ) ;

class EventStats {
public:
	int32_t m_expired;
	int32_t m_active;

	// these are all active
	int32_t m_experimental;
	int32_t m_resultSet1;
	int32_t m_resultSetOther;
	int32_t m_facebook;
	int32_t m_badGeocoder;

	void clear ( ) { memset ( this , 0 , sizeof(EventStats) ); };
};


class PingInfo {
 public:
	// m_lastPing MUST be on top for now...
	//int64_t m_lastPing;
	// this timestamp MUST be on top because we set requestSize to 8
	// and treat it like an old 8-byte ping in PingServer.cpp
	int64_t m_localHostTimeMS;
	int32_t m_hostId;
	int32_t m_loadAvg;
	float m_percentMemUsed;
	float m_cpuUsage;
	int32_t m_totalDocsIndexed;
	int32_t m_slowDiskReads;
	int32_t m_hostsConfCRC;
	float m_diskUsage;
	int32_t m_flags;
	// some new stuff
	int32_t m_numCorruptDiskReads;
	int32_t m_numOutOfMems;
	int32_t m_socketsClosedFromHittingLimit;

	int32_t m_totalResends;
	int32_t m_etryagains;

	int32_t m_udpSlotsInUseIncoming;
	int32_t m_tcpSocketsInUse;

	int16_t m_currentSpiders;
	collnum_t m_dailyMergeCollnum;
	int16_t m_hdtemps[4];
	char m_gbVersionStr[21];
	char m_repairMode;
	char m_kernelErrors;
	uint8_t m_recoveryLevel;
};

class Host {

 public:

	//bool isDead ( ) { return m_hostdb->m_isDead (); };

	int32_t           m_hostId ;
	//uint32_t  m_groupId ;

	// shards and groups are basically the same, but let's keep it simple.
	// since we use m_map in Hostdb.cpp to map the bits of a key to
	// the shard # now (not a groupId anymore, since groupId does not
	// work for non binary powers of shards)
	uint32_t m_shardNum;

	//int32_t           m_groupNum;
//	char       m_pubKey[20];     // 128bit(16 byte) and a int16_t/int32_t
	uint32_t  m_ip ;        // used for internal communcation (udp)

	// what stripe are we in? i.e. what twin number are we?
	int32_t           m_stripe;

	// . these are used for talking w/ a particular host from the outside
	// . usually m_extHttpPort = m_httpPort, but if you are going through
	//   a router it can do portmapping. It might portmap 80 to 8000 so
	//   you should use 80 here then.
	//uint32_t  m_externalIp; // used for web-based GUI, to the world
	// this ip is now the shotgun ip. the problem with an "external" ip
	// is that people internal to the network cannot usually see it!!
	// so now this is just a secondary regular ip address for servers with
	// dual gigabit ethernet ports. make performance much better.
	uint32_t  m_ipShotgun;  
	uint16_t m_externalHttpPort;
	uint16_t m_externalHttpsPort;

	// his checksum of his hosts.conf so we can ensure we have the
	// same hosts.conf file! 0 means not legit.
	//int32_t m_hostsConfCRC;

	// used by Process.cpp to do midnight stat dumps and emails
	EventStats m_eventStats;

//	time_t     m_lastComm ;      // time of last communication
//	time_t     m_lastAttempt ;   // time of last attempted communication
//	int32_t       m_bandwidth;      // bytes per second
//	int64_t  m_bytesRecvdFrom; // why do we need this?
//	int64_t  m_bytesSentTo;    // why do we need this?
	uint16_t m_port ;          // Mattster Protocol (MP) UDP port
	//uint16_t m_portShotgun;    // for shotgunning
	//uint16_t m_port2;          // the high priority port
	uint16_t m_httpPort ;      // http port
	uint16_t m_httpsPort;
	//int32_t       m_pingAvg ;       // in ms
	//int32_t       m_pingStdDev;     // in ms
	//int32_t       m_pings[4];       // our window of the last 4 pings
	int32_t           m_ping;
	int32_t           m_pingShotgun;
	int32_t           m_pingMax;
	// have we ever got a ping reply from him?
	char           m_gotPingReply;
	double         m_loadAvg;
	//float          m_percentMemUsed;
	// the first time we went OOM (out of mem, i.e. >= 99% mem used)
	int64_t      m_firstOOMTime;
	// cpu usage
	//float          m_cpuUsage;

	//float          m_diskUsage;

	//int32_t          m_slowDiskReads;

	// doc count
	//int32_t          m_docsIndexed;
	int32_t          m_urlsIndexed;

	int32_t          m_eventsIndexed;

	// did gb log system errors that were given in g_conf.m_errstr ?
	//char           m_kernelErrors;
	bool           m_kernelErrorReported;

	//int32_t           m_flags;

	// used be SEO pipeline in xmldoc.cpp
	int32_t           m_numOutstandingRequests;

	// used by DailyMerge.cpp exclusively
	//collnum_t      m_dailyMergeCollnum;

	// last time g_hostdb.ping(i) was called for this host in milliseconds.
	int64_t      m_lastPing;

	char m_tmpBuf[4];
	int16_t m_tmpCount;

	// . first time we sent an unanswered ping request to this host
	// . used so we can determine when to send an email alert
	int64_t      m_startTime;
	// is a ping in progress for this host?
	char           m_inProgress1;
	// shotgun
	char           m_inProgress2;
	int64_t      m_numPingReplies;

	// send to eth 0 or 1 when sending to this host?
	char           m_preferEth;

	// machine #, a machine can have several hosts on it
	int32_t           m_machineNum;
	// . this is used for sending email alerts to admin about dead hosts
	// .  0 means we can send an email for this host if he goes dead on us
	// . +1 means we already sent an email for him since he was down and 
	//      he hasn't come back up since
	// . -1 means he went down gracefully so no alert is needed
	// . -2 means he went down while g_conf.m_sendEmailAlert was false
	// . -3 means another host was responsible for sending to him, not us
	// . -4 means we are currently in progress sending an email for him
	// . -5 means he went down before host we alerted admin about revived
	int32_t           m_emailCode;
	// . ide channel
	// . Msg34 will add disk load from other gigablast process that are
	//   using your channel and your ip
	int32_t           m_ideChannel;
	//int32_t           m_tokenGroupNum;
	// 0 means no, 1 means yes, 2 means unknown
	char           m_syncStatus;

	// we now include the working dir in the hosts.conf file
	// so main.cpp can do gb --install and gb --allstart
	char           m_dir [ 128 ];

	char           m_hostname[16];

	// the secondary ethernet interface hostname
	char           m_hostname2[16];

	// client port
	uint16_t m_dnsClientPort;

	// . what set the host is in
	// . its redundant twins are always in different sets
	//int32_t           m_group;
	// was host in gk0 cluster and retired because its twin got
	// ssds, so it was no longer really needed.
	bool           m_retired;
	// used for logging when a host goes dead for the first time
	bool           m_wasAlive;
	bool           m_wasEverAlive;
	int64_t      m_timeOfDeath;
	// this toggles between 0 and 1 for alternating packet sends to
	// eth0 and eth1 of this host
	char           m_shotgunBit;
	// how many ETRYAGAINs we received as replies from this host
	//int32_t           m_etryagains;
	// how many resends total we had to do to this host
	//int32_t           m_totalResends;
	// how many total error replies we got from this host
	int32_t           m_errorReplies;

	// now include a note for every host
	char           m_note[128];
	char           m_doingSync;
	char           m_switchId;
	char           m_onProperSwitch;

	bool           m_isProxy;

	// used for compressing spidered docs before sending back to local
	// network to save on local loop bandwidth costs
	char           m_type;

	bool isProxy() { return (m_type == HT_PROXY); };
	bool isGrunt() { return (m_type == HT_GRUNT); };

	// for m_type == HT_QCPROXY, we forward the query to the regular proxy
	// at this Ip:Port. we should receive a compressed 0xfd reply and
	// we uncompress it and return it to the browser.
	int32_t           m_forwardIp;
	int16_t          m_forwardPort;

	int64_t      m_dgramsTo;
	int64_t      m_dgramsFrom;

	char           m_repairMode;

	// for timing how long the msg39 takes from this host
	int32_t           m_splitsDone;
	int64_t      m_splitTimes;

	// . the hostdb to which this host belongs!
	// . getHost(ip,port) will return a Host ptr from either 
	//   g_hostdb or g_hostdb2, so UdpServer.cpp needs to know which it
	//   is from when making the UdpSlot key.
	class Hostdb  *m_hostdb;

	// . temps in celsius of the hard drives
	// . set in Process.cpp
	//int16_t          m_hdtemps[4];

	// 24 bytes including ending \0
	//char m_gbVersionStrBuf[24];

	// Syncdb.cpp uses these
	char           m_inSync ;
	char           m_isPermanentOutOfSync ;

	//char *m_lastKnownGoodCrawlInfoReply;
	//char *m_lastKnownGoodCrawlInfoReplyEnd;
	//int32_t  m_replyAllocSize;

	// . used by Parms.cpp for broadcasting parm change requests
	// . each parm change request has an id
	// . this let's us know which id is in progress and what the last
	//   id completed was
	int32_t m_currentParmIdInProgress;
	int32_t m_lastParmIdCompleted;
	class ParmNode *m_currentNodePtr;
	int32_t m_lastTryError;
	int32_t m_lastTryTime;

	bool m_spiderEnabled;
	bool m_queryEnabled;
	

	//char  m_requestBuf[MAX_PING_SIZE];
	PingInfo m_pingInfo;//RequestBuf;
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
	bool init ( int32_t hostId , char *netname = NULL,
		    bool proxyHost = false , char useTempCluster = 0 ,
		    char *cwd = NULL );

	// for dealing with pings
	bool registerHandler ( );

	// if config changes this *should* change
	int32_t getCRC();

	// . to prevent script kiddies from sending bogus udp packets to
	//   our udp servers we make sure they are from an IP in our hosts.conf
	//   file. If not, then we drop it. If the script kiddie spoofed us
	//   then we just have to deal with malformed packets in the handlers.
	// . this just hashes each ip into m_ipHashes at startup and checks 
	//   for the ip in there
	//bool isIpInNetwork ( int32_t ip );

	// . add ips from all hosts to our validation table so isIpInNetwork()
	//   returns true for them
	// . this will add the dns ips from the Conf file
	//bool validateIps ( class Conf *conf ) ;

	char *getNetName ( );

	Hostdb();
	~Hostdb();
	void reset();

	uint32_t  getMyIp         ( ) { return m_myIp; };
	uint16_t getMyPort       ( ) { return m_myPort; };
	//uint16_t getMyPort2      ( ) { return m_myPort2; };
	int32_t           getMyHostId     ( ) { return m_hostId; };
	int32_t           getMyMachineNum ( ) { return m_myMachineNum; };
	uint32_t  getLoopbackIp   ( ) { return m_loopbackIp; };
	Host          *getMyHost       ( ) { return m_myHost; };
	bool           amProxy         ( ) { return m_myHost->isProxy(); };
	Host          *getMyShard      ( ) { return m_myShard; };
	int32_t getMyShardNum ( ) { return m_myHost->m_shardNum; };
	bool           isMyIp ( uint32_t ip ) {
		if ( ip == m_myIp        ) return true;
		if ( ip == m_myIpShotgun ) return true;
		if ( ip == 0x0100007f    ) return true;
		return false;
	};

	// . one machine may have several hosts
	// . get the machine # the hostId resides on
	int32_t getMachineNum ( int32_t hostId ) {
		return getHost(hostId)->m_machineNum; };

	int32_t getNumMachines ( ) { return m_numMachines; };

	// . some utility routines
	// . hostIds go from 0 to N-1 where N is # of particiapting hosts
	// . groupIds have hi bits set first & depend on # of groups
	// . groupMask is just the highest groupId
	//int32_t          makeHostId     ( uint32_t groupId ) ;
	//uint32_t makeGroupId    ( int32_t hostId    , int32_t numGroups );
	//uint32_t makeGroupMask  ( int32_t numGroups ) ;

	// uses a table
	//int32_t          makeHostIdFast ( uint32_t groupId ) ;

	// we consider the host dead if we didn't get a ping reply back
	// after 10 seconds
	bool  isDead ( int32_t hostId ) ;

	bool  isDead ( Host *h ) ;

	int32_t getAliveIp ( Host *h ) ;

	bool hasDeadHost ( );

	bool kernelErrors (Host *h) { return h->m_pingInfo.m_kernelErrors; };

	int64_t getNumGlobalRecs ( );

	int64_t getNumGlobalEvents ( );

	bool isShardDead ( int32_t shardNum ) ;

	//Host *getLiveHostInGroup ( int32_t groupId );
	Host *getLiveHostInShard ( int32_t shardNum );
	Host *getLeastLoadedInShard ( uint32_t shardNum , char niceness );
	int32_t getHostIdWithSpideringEnabled ( uint32_t shardNum );

	// in the entire cluster. return host #0 if its alive, otherwise
	// host #1, etc.
	Host *getFirstAliveHost ();

	// . returns false if blocks and will call your callback later
	// . returns true if doesn't block
	// . sets errno on error
	// . used to get array of hosts in 1 group usually for transmitting to
	// . returned group includes all hosts in host "hostId"'s group
	// . RdbList will be populated with the hosts in that group
	// . we do not create an RdbList, you must do that
	// . callback passes your RdbList back to you
	//Host *getGroup ( uint32_t groupId , int32_t *numHosts = NULL );
	Host *getShard ( uint32_t shardNum , int32_t *numHosts = NULL ) {
		if ( numHosts ) *numHosts = m_numHostsPerShard;
		return &m_hosts[shardNum * m_numHostsPerShard]; 
	};


	//Host *getGroupFromGroupId ( uint32_t gid ) {
	//	return getGroup ( gid ); 
	//};

	//Host *getGroupFromGroupNum ( int32_t groupNum ) {
	//	// the array of Hosts that this points into is sorted
	//	// by groupId first, so we should be ok
	//	return m_groups[groupNum]; 
	//};

	// the row #
	//int32_t getStripe ( uint32_t groupId ) {
	//	Host *h = getGroup ( groupId );
	//	if ( ! h ) return -1;
	//	return h->m_stripe;//groupNum;
	//};

	// the column #
	//int32_t getGroupNum ( uint32_t groupId ) {
	//	Host *h = getGroup ( groupId );
	//	if ( ! h ) return -1;
	//	return h->m_group;
	//};

	// quickly get the lowest hostid in group "groupId"
	//Host *getHostIdFast ( uint32_t groupId );

	// hosts in a token group share a token that is required for
	// performing merges (big read/writes on disk)
	//Host **getTokenGroup ( uint32_t hostId , int32_t *numHosts = NULL);

	// this is used to set the Host::m_tokenGroupNum members
	//int32_t getTokenGroupNum ( Host *ha ) ;

	// . map a group num to a groupId
	// . used by titledb.h to find groupId for a docId
	//uint32_t getGroupId_old ( int32_t groupNum ) {
	//	return m_hostPtrs[groupNum]->m_groupId_old; }

	//uint32_t getGroupIdFromHostId ( int32_t hostId ) {
	//	return m_hostPtrs[hostId]->m_groupId; };

	// get the host in this group with the smallest avg roundtrip time
	//Host *getFastestHostInGroup ( uint32_t groupId );

	// get the host that has this path/ip
	Host *getHost2 ( char *cwd , int32_t *localIps ) ;
	Host *getProxy2 ( char *cwd , int32_t *localIps ) ;

	// . like above but just gets one host
	// Host *getHost ( int32_t hostId ) { return m_groups[hostId]; };
	Host *getHost ( int32_t hostId ) { 
		if ( hostId < 0 ) { char *xx=NULL;*xx=0; }
		return m_hostPtrs[hostId]; 
	};

	Host *getSpare ( int32_t spareId ) {
		return m_spareHosts[spareId]; };

	Host *getProxy ( int32_t proxyId ) {
		return m_proxyHosts[proxyId]; };

	int32_t  getNumHosts ( ) { return m_numHosts; };
	int32_t  getNumProxy ( ) { return m_numProxyHosts; };
	int32_t getNumProxies ( ) { return m_numProxyHosts; };
	int32_t getNumGrunts  ( ) { return m_numHosts; };
	// how many of the hosts are non-dead?
	int32_t  getNumHostsAlive ( ) { return m_numHostsAlive; };
	int32_t  getNumProxyAlive ( ) { return m_numProxyAlive; };
	//int32_t  getNumGroups () { return m_numGroups; };
	int32_t  getNumShards () { return m_numShards; };
	int32_t  getNumIndexSplits() { return m_indexSplits; };

	// how many hosts in this group?
	//int32_t  getNumHostsPerShard ( ) { return m_numHostsPerShard; };
	int32_t  getNumHostsPerShard ( ) { return m_numHostsPerShard; };

	// goes with Host::m_stripe
	int32_t  getNumStripes ( ) { return m_numHostsPerShard; };

	// . get a host entry from ip/port
	// . returns NULL if no match
	//Host *getHost  ( uint32_t ip , uint16_t port );
	//Host *getProxy  ( uint32_t ip , uint16_t port );

	// from http/https ports
	//Host *getHostFromTcpPort  ( uint32_t ip , uint16_t port );
	//Host *getProxyFromTcpPort ( uint32_t ip , uint16_t port );

	// . get the Host sharing m_hosts[n]'s ip and ide channel
	// . used by Msg34.cpp to compute load on the channel
	// . TODO: speed this up when we get a *lot* of hosts
	// . right now it just does a linear scan
	//Host *getSharer ( Host *h ) ;

	// . add ip to allowed packets table, if we receive a udp packet
	//   from an ip not in this list, we discard it
	// . returns true on success
	bool addIp ( int32_t ip ) ;

	// . hostId of -1 means unknown
	//void getTimes ( int32_t hostId , int32_t *avg , int32_t *stdDev );

	// . update ping time info of this host
	// . uses a 10-ping running average
	// . "tripTime" is in milliseconds
	// . hostId of -1 means unknown (will just return true)
	//void  stampHost  ( int32_t hostId , int32_t tripTime , bool timedOut );
	
	// hash the hosts into the hash tables for lookup
	bool  hashHosts();
	bool  hashHost ( bool udp , Host *h , uint32_t ip , uint16_t port ) ;
	int32_t  getHostId        ( uint32_t ip , uint16_t port ) ;
	Host *getHostByIp      ( uint32_t ip ) ;
	Host *getHost          ( uint32_t ip , uint16_t port ) ;
	Host *getUdpHost       ( uint32_t ip , uint16_t port ) ;
	Host *getTcpHost       ( uint32_t ip , uint16_t port ) ;
	bool isIpInNetwork     ( uint32_t ip ) ;
	Host *getHostFromTable ( bool udp , uint32_t ip , uint16_t port ) ;



	// sets the note for a host
	bool setNote ( int32_t hostId, char *note, int32_t noteLen );
	bool setSpareNote ( int32_t spareId, char *note, int32_t noteLen );
	
	// replace a host with a spare
	bool replaceHost ( int32_t origHostId, int32_t spareHostId );

	void setOnProperSwitchFlags ( ) ;

	// write a hosts.conf file
	bool saveHostsConf ( );

	// sync a host with its twin
	bool syncHost ( int32_t syncHostId, bool useSecondaryIps );
	void syncStart_r ( bool amThread );
	void syncDone ( );

	int32_t getBestIp ( Host *h , int32_t fromIp ) ;
	
	Host *getBestSpiderCompressionProxy ( int32_t *key ) ;

	// only used by 'gb scale <hostdb.conf>' cmd for now
	void resetPortTables ();

	// returns best IP to use for "h" which is a host in hosts2.conf
	int32_t getBestHosts2IP ( Host *h );

	// our host's info used by Udp* classes for internal communication
	uint32_t  m_myIp;
	uint32_t  m_myIpShotgun;
	uint16_t m_myPort;
	//uint16_t m_myPort2;
	int32_t           m_myMachineNum;
	Host          *m_myHost;
	Host          *m_myShard;

	// the loopback ip (127.0.0.1)
	uint32_t  m_loopbackIp;

	// . the hosts are stored from g_conf in xml to here
	// . m_hosts[i] is the ith Host entry
	Host  *m_hosts;
	int32_t   m_numHosts;
	int32_t   m_numHostsAlive;

	int32_t   m_allocSize;

	// . this maps a hostId to the appropriate host!
	// . we can't use m_hosts[i] because we sort it by groupId for getGroup
	Host  *m_hostPtrs[MAX_HOSTS];

	// . m_hostIdToTokenGroupNum maps a host id to a "tgn"
	// . m_hostPtrs2 [ tgn ] is the list of Host ptrs in that host's
	//   merge token group and the size of the group is m_groupSize [ tgn ]
	// . used by getTokenGroup() which is used by Msg35.cpp
	//Host  *m_hostPtrs2             [ MAX_HOSTS ] ;
	//int32_t   m_hostIdToTokenGroupNum [ MAX_HOSTS ] ;
	//int32_t   m_groupSize             [ MAX_HOSTS ];

	// we must have the same number of hosts in each group
	int32_t   m_numHostsPerShard;

	// store the file in m_buf
	char m_buf [MAX_HOSTS * 128];
	int32_t m_bufSize;

	// this maps shard # to the array of hosts in that shard
	Host *m_shards[MAX_HOSTS];

	int32_t    m_numMachines;

	// the hash table of the ips in hosts.conf
	int32_t *m_ips;
	int32_t  m_numIps;

	// does a host share an ide channel with another host?
	//bool m_ideSharing;

	// . our group info
	int32_t          m_hostId;      // our hostId
	int32_t          m_numShards;
	//uint32_t m_groupId;     // hi bits are set before low bits
	//uint32_t m_groupMask;   // hi bits are set before low bits
	char          m_dir[256];
	char          m_httpRootDir[256];
	char          m_logFilename[256];

	int32_t          m_indexSplits; 

	char          m_netName[32];

	// spare hosts list
	Host *m_spareHosts[MAX_SPARES];
	int32_t  m_numSpareHosts;

	// proxy host list
	Host *m_proxyHosts[MAX_PROXIES];
	int32_t  m_numProxyHosts;
	int32_t  m_numProxyAlive;

	int32_t  m_numTotalHosts;

	bool m_initialized;

	bool createHostsConf( char *cwd );
	bool m_created;

	int32_t m_crc;
	int32_t m_crcValid;

	// for sync
	Host *m_syncHost;
	bool  m_syncSecondaryIps;

	char  m_useTmpCluster;

	//uint32_t getGroupId (char rdbId, void *key, bool split = true);
	//uint32_t getGroupIdFromDocId ( int64_t d ) ;

	uint32_t getShardNum (char rdbId, void *key );
	uint32_t getShardNumFromDocId ( int64_t d ) ;

	// assume to be for posdb here
	uint32_t getShardNumByTermId ( void *key );

	uint32_t m_map[MAX_KSLOTS];
};

extern class Hostdb g_hostdb;
extern class Hostdb g_hostdb2;

extern Host     *g_listHosts [ MAX_HOSTS * 4 ];
extern uint32_t  g_listIps   [ MAX_HOSTS * 4 ];
extern uint16_t  g_listPorts [ MAX_HOSTS * 4 ];
extern int32_t      g_listNumTotal;

inline uint32_t getShardNum ( char rdbId, void *key ) {
	return g_hostdb.getShardNum ( rdbId , key );
};

inline uint32_t getMyShardNum ( ) { 
	return g_hostdb.m_myHost->m_shardNum; 
};

inline uint32_t getShardNumFromDocId ( int64_t d ) {
	return g_hostdb.getShardNumFromDocId ( d );
};

int32_t getShardNumFromTermId ( int64_t termId );

//inline uint32_t getGroupId ( char rdbId, void *key,bool split = true) {
//	return g_hostdb.getGroupId ( rdbId , key , split );
//};

//inline uint32_t getGroupIdFromDocId ( int64_t d ) {
//	return g_hostdb.getGroupIdFromDocId ( d );
//};

#endif
