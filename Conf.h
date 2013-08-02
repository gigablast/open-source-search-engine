// Copyright Matt Wells, Apr 2001

// . every host has a config record
// . like tagdb, record in 100% xml
// . allows remote configuration of hosts through Msg4 class
// . remote user sends some xml, we set our member vars using that xml
// . when we save to disk we convert our mem vars to xml
// . is global so everybody can see it
// . conf record can be changed by director OR with the host's priv key
// . use Conf remotely to get setup info about a specific host
// . get your local ip/port/groupMask/etc. from this class not HostMap

#ifndef _CONF_H_
#define _CONF_H_

//#include "../../rsa/rsa.h"      // for private_key and public_key types
#include "Xml.h"         // Xml class
#include "File.h"        // File class
#include "ip.h"          // atoip()
#include "Hostdb.h"      // g_hostdb.makeGroupId(),makeGroupMask()
#include "HttpRequest.h"
#include "TcpSocket.h"
#include "Url.h"  // MAX_COLL_LEN
#include "Collectiondb.h"

#define MAX_MASTER_IPS        15
#define MAX_MASTER_PASSWORDS  10

#define USERAGENTMAXSIZE      128

#define PASSWORD_MAX_LEN      12

#define MAX_CONNECT_IPS       128

#define AUTOBAN_TEXT_SIZE     (32*8192)

#define MAX_DNSIPS            16
#define MAX_RNSIPS            13
#define MAX_MX_LEN            128
#define MAX_EMAIL_LEN         64

#define USERS_TEXT_SIZE       500000

#define MAX_GEOCODERS         4

class Conf {

  public:
	
	Conf();

	bool isMasterAdmin  ( class TcpSocket *s , class HttpRequest *r );
	bool isSpamAssassin ( class TcpSocket *s , class HttpRequest *r );
	bool isAdminIp      ( unsigned long ip );
	bool isConnectIp    ( unsigned long ip );

	// loads conf parms from this file "{dir}/gb.conf"
	bool init ( char *dir );

	void setRootIps();

	// set from a buffer of null-terminated xml
	bool add ( char *xml );

	// saves any changes to the conf file
	bool save ( );

	// reset all values to their defaults
	void reset();

	// verify that some values are ok
	bool verify();

	// . get the default collection based on hostname
	//   will look for the hostname in each collection for a match
	//   no match defaults to default collection
	char *getDefaultColl ( char *hostname, long hostnameLen );

	// hold the filename of this conf file
	char        m_confFilename[256];

	// general info
	//bool        m_isTrustedNet;
	//char        m_dir[256];     // our mattster root working dir
	//long        m_ip;           // now in hostdb conf file
	//bool        m_isTrusted;    // is the whole network trusted?
	//private_key m_privKey;      // our private key for this host

	// max amount of memory we can use
	long long        m_maxMem;

	// if this is false, we do not save, used by dump routines
	// in main.cpp so they can change parms here and not worry about
	// a core dump saving them
	char m_save;

	//director info (optional) (used iff m_isTrustedNet is false)
	//public_key  m_dirPubKey;  // everyone should know director's pub key
	//private_key m_dirPrivKey;   // this is 0 if we don't know it

	// . external ip of our firewall/router/...
	// . regular users use this to connect
	// . Host::m_externalIp/Port is used by admin
	// . Host::m_ip/port is for machine to machine communication or
	//   if admin is coming from a local machine
	//unsigned long  m_mainExternalIp;
	//unsigned short m_mainExternalPort;

	// . our group info
	//long m_hostId;       // our hostId
	//long m_numGroups;
	//unsigned long m_groupId;     // hi bits are set before low bits
	//unsigned long m_groupMask;   // hi bits are set before low bits

	// the main directory
	//char m_dir[256];

	// an additional strip directory on a different drive
	char m_stripeDir[256];

	char m_defaultColl [ MAX_COLL_LEN + 1 ];
	char m_dirColl [ MAX_COLL_LEN + 1];
	char m_dirHost [ MAX_URL_LEN ];

	char m_clusterName[32];

	// . dns parameters
	// . dnsDir should hold our saved cached (TODO: save the dns cache)
	//short m_dnsClientPort;            
	long  m_numDns ;
	long  m_dnsIps[MAX_DNSIPS];
	short m_dnsPorts[MAX_DNSIPS];            

	long  m_dnsMaxCacheMem;
	bool  m_dnsSaveCache;

	long  m_geocoderIps[MAX_GEOCODERS];

	long m_wikiProxyIp;
	long m_wikiProxyPort;

	// built-in dns parameters using name servers
	char  m_askRootNameservers;
	long  m_numRns;
	long  m_rnsIps[MAX_RNSIPS];
	short m_rnsPorts[MAX_RNSIPS];

	// log absolute filename
	//char  m_logFilename[256];
	// hostdb absolute conf filename
	//char  m_hostdbFilename[256];
	// used to limit all rdb's to one merge per machine at a time
	long  m_mergeBufSize;

	// tagdb parameters
	long  m_tagdbMaxTreeMem;
	long  m_tagdbMaxDiskPageCacheMem;
	//long  m_tagdbMaxCacheMem;
	//bool  m_tagdbUseSeals;
	//long  m_tagdbMinFilesToMerge;
	//bool  m_tagdbSaveCache;
	
	// catdb parameters
	long  m_catdbMaxTreeMem;
	long  m_catdbMaxDiskPageCacheMem;
	long  m_catdbMaxCacheMem;
	long  m_catdbMinFilesToMerge;

	long  m_revdbMaxTreeMem;
	long  m_timedbMaxTreeMem;

	// titledb parameters
	//long  m_titledbMaxTreeMem; // why isn't this used?
	//long  m_titledbMaxCacheMem;
	//long  m_titledbMinFilesToMerge;
	//long  m_titledbMaxCacheAge;
	//bool  m_titledbSaveCache;

	// clusterdb for site clustering, each rec is 16 bytes
	long  m_clusterdbMaxTreeMem; 
	//long  m_clusterdbMaxCacheMem;
	//long  m_clusterdbMaxDiskPageCacheMem;
	long  m_clusterdbMinFilesToMerge;
	bool  m_clusterdbSaveCache;

	//bool  m_indexEventsOnly;

	// linkdb for storing linking relations
	long  m_linkdbMaxTreeMem;
	//	long  m_linkdbMaxCacheMem;
	long  m_linkdbMaxDiskPageCacheMem;
	long  m_linkdbMinFilesToMerge;
	//	bool  m_linkdbSaveCache;

	// dup vector cache max mem
	long  m_maxVectorCacheMem;

	// checksumdb for doc deduping, each rec is 12-16 bytes
	//long  m_checksumdbMaxTreeMem; 
	//long  m_checksumdbMaxCacheMem; 
	//long  m_checksumdbMaxDiskPageCacheMem; 
	//long  m_checksumdbMinFilesToMerge;
	// size of Checksumdb keys for this host
	//long m_checksumdbKeySize;
	//bool  m_checksumdbSaveCache;

	// for holding urls that have been entered into the spider queue
	//long  m_tfndbMaxTreeMem   ;
	long  m_tfndbMaxDiskPageCacheMem ; // for the DiskPageCache class only
	//long  m_tfndbMinFilesToMerge;
	//bool  m_tfndbSaveCache;
	//long long  m_tfndbMaxUrls;

	long  m_maxCpuThreads;

	long  m_deadHostTimeout;
	long  m_sendEmailTimeout;
	long  m_pingSpacer;

	// the spiderdb holds url records for spidering, when to spider, etc..
	long  m_maxWriteThreads ;
	//long  m_spiderdbMaxTreeMem   ;
	//long  m_spiderdbMaxCacheMem ;
	//long  m_spiderdbMaxDiskPageCacheMem ;
	//long  m_spiderdbMinFilesToMerge;
	long  m_spiderMaxDiskThreads    ;
	long  m_spiderMaxBigDiskThreads ; // > 1M read
	long  m_spiderMaxMedDiskThreads ; // 100k - 1M read
	long  m_spiderMaxSmaDiskThreads ; // < 100k read
	long  m_queryMaxDiskThreads     ;
	long  m_queryMaxBigDiskThreads  ; // > 1M read
	long  m_queryMaxMedDiskThreads  ; // 100k - 1M read
	long  m_queryMaxSmaDiskThreads  ; // < 100k per read
	// categorize the disk read sizes by these here
	long  m_bigReadSize;
	long  m_medReadSize;
	long  m_smaReadSize;

	long m_statsdbMaxTreeMem;
	long m_statsdbMaxCacheMem;
	long m_statsdbMaxDiskPageCacheMem;
	//long m_statsdbMinFilesToMerge;
	bool m_useStatsdb;
	//bool m_statsdbSnapshots;
	//bool m_statsdbPageEnabled;

	//long  m_spiderdbRootUrlPriority; // 0-7
	//long  m_spiderdbAddUrlPriority ;
	//long  m_minSpiderPriority    ; // min spiderRec priority to spider
	//long  m_maxSpidersPerDomain  ; // per foreign domain
	//long  m_maxRespiderWait      ; // in seconds to re-spider a page
	//long  m_minRespiderWait      ; // in seconds to re-spider a page
	// this is now in the root collection record
	//long  m_maxNumSpiders        ; // per local spider host
	bool  m_spideringEnabled     ;
	bool  m_turkingEnabled     ;
	//bool  m_webSpideringEnabled;
	//bool  m_facebookSpideringEnabled;
	//bool  m_stubHubSpideringEnabled;
	//bool  m_eventBriteSpideringEnabled;
	//bool  m_refreshFacebookUsersEnabled;
	//bool  m_injectionEnabled     ;
	// qa testing loop going on? uses "test" subdir
	bool  m_testParserEnabled     ;
	bool  m_testSpiderEnabled     ;
	//bool  m_doDocIdRangeSplitting ;
	bool  m_testSearchEnabled     ;
	//bool  m_spiderLoggingEnabled ;
	//bool  m_logWarnings          ; // generally small problems
	//bool  m_logCongestion        ; // ENOSLOTS
	bool  m_addUrlEnabled        ; // TODO: use at http interface level
	bool  m_adFeedEnabled        ;
	//bool  m_timingDebugEnabled   ;
	//bool  m_threadDebugEnabled   ;
	//bool  m_httpServerEnabled    ;// don't allow seo bots on all machines
	bool  m_doStripeBalancing    ;

	// . true if the server is on the production cluster
	// . we enforce the 'elvtune -w 32 /dev/sd?' cmd on all drives because
	//   that yields higher performance when dumping/merging on disk
	bool  m_isLive;
	
	// is this a buzzlogic cluster?
	//bool m_isBuzzLogic;

	// is this a wikipedia cluster?
	bool   m_isWikipedia;

	//bool  m_spiderLinks          ;
	//bool  m_dedupingEnabled      ; // dedup content on same mid domain
	//long  m_retryNum             ; // how many times to retry url b4 nuke
	//bool  m_useIfModifiedSince   ;
	//bool  m_doUrlSpamCheck       ; // disallow urls w/ naughty hostnames
	//bool  m_timeBetweenUrls      ; // for urls from same domain only
	// for holding robot.txt files for various hostnames
	long  m_robotdbMaxCacheMem  ;
	bool  m_robotdbSaveCache;

	// indexdb has a max cached age for getting IndexLists (10 mins deflt)
	long  m_indexdbMaxTreeMem   ;
	long  m_indexdbMaxCacheMem;
	long  m_indexdbMaxDiskPageCacheMem; // for DiskPageCache class only
	long  m_indexdbMaxIndexListAge;
	long  m_indexdbTruncationLimit;
	long  m_indexdbMinFilesToMerge;
	bool  m_indexdbSaveCache;

	long  m_datedbMaxTreeMem   ;
	long  m_datedbMaxCacheMem;
	long  m_datedbMaxDiskPageCacheMem; // for DiskPageCache class only
	long  m_datedbMaxIndexListAge;
	long  m_datedbTruncationLimit;
	long  m_datedbMinFilesToMerge;
	bool  m_datedbSaveCache;
	// for caching exact quotas in Msg36.cpp
	//long  m_quotaTableMaxMem;

	//bool  m_useBuckets;

	// port of the main udp server
	short m_udpPort;

	// TODO: parse these out!!!!
	//char  m_httpRootDir[256]  ;
	//short m_httpPort           ; now in hosts.conf only
	long  m_httpMaxSockets     ;
	long  m_httpsMaxSockets    ;
	//long  m_httpMaxReadBufSize ;
	long  m_httpMaxSendBufSize ;
	//long  m_httpMaxDownloadSockets ;

	// a search results cache (for Msg40)
	long  m_searchResultsMaxCacheMem    ;
	long  m_searchResultsMaxCacheAge    ; // in seconds
	bool  m_searchResultsSaveCache;

	// a sitelinkinfo cache (for Msg25)
	long  m_siteLinkInfoMaxCacheMem;
	long  m_siteLinkInfoMaxCacheAge;
	bool  m_siteLinkInfoSaveCache;

	// a sitelinkinfo cache (for MsgD)
	long  m_siteQualityMaxCacheMem;
	long  m_siteQualityMaxCacheAge;
	bool  m_siteQualitySaveCache;

	// a sitelinkinfo cache (for Msg25)

	// for downloading an rdb
	//long  m_downloadBufSize; // how big should hosts read buf be?

	// . how many incoming links should we sample?
	// . used for linkText and quality weighting from number of links
	//   and their total base quality
	long  m_maxIncomingLinksToSample;

	// phrase weighting
	float  m_queryPhraseWeight;

	// for Weights.cpp
	long   m_sliderParm;

	//long   m_indexTableIntersectionAlgo;
	// . maxmimum relative weight of a query term (1.0 to inf)
	// . default about 8?
	//float  m_queryMaxMultiplier;

	// send emails when a host goes down?
	bool   m_sendEmailAlerts;
	//should we delay when only 1 host goes down out of twins till 9 30 am?
	bool   m_delayNonCriticalEmailAlerts;
	//delay emails after
	char   m_delayEmailsAfter[6];
	//delay emails before
	char   m_delayEmailsBefore[6];
	//bool   m_sendEmailAlertsToMattTmobile;
	//bool   m_sendEmailAlertsToMattAlltell;
	//bool   m_sendEmailAlertsToJavier;
	//bool   m_sendEmailAlertsToMelissa;
	//bool   m_sendEmailAlertsToPartap;
	//bool   m_sendEmailAlertsToCinco;
	bool   m_sendEmailAlertsToSysadmin;
	//bool   m_sendEmailAlertsToZak;

	bool   m_sendEmailAlertsToEmail1;
	char   m_email1MX[MAX_MX_LEN]; 
	char   m_email1Addr[MAX_EMAIL_LEN];
	char   m_email1From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail2;
	char   m_email2MX[MAX_MX_LEN];
	char   m_email2Addr[MAX_EMAIL_LEN];
	char   m_email2From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail3;
	char   m_email3MX[MAX_MX_LEN];
	char   m_email3Addr[MAX_EMAIL_LEN];
	char   m_email3From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail4;
	char   m_email4MX[MAX_MX_LEN];
	char   m_email4Addr[MAX_EMAIL_LEN];
	char   m_email4From[MAX_EMAIL_LEN];

	//bool   m_sendEmailAlertsToSabino;

	char   m_errstr1[MAX_URL_LEN];
	char   m_errstr2[MAX_URL_LEN];	
	char   m_errstr3[MAX_URL_LEN];

	char   m_sendParmChangeAlertsToEmail1;
	char   m_sendParmChangeAlertsToEmail2;
	char   m_sendParmChangeAlertsToEmail3;
	char   m_sendParmChangeAlertsToEmail4;

	float m_avgQueryTimeThreshold;
	//float m_maxQueryTime;
	float m_querySuccessThreshold;
	long  m_numQueryTimes;
	long m_maxCorruptLists;

	// limit to how big a serialized query can be before just storing
	// the raw string instead, keeps network traffic down at the expense
	// of processing time, used by Msg serialization
	long  m_maxSerializedQuerySize;

	// the spider won't go if this bandiwdth rate is currently exceeded
	float  m_maxIncomingKbps;

	// max pgs/sec to index and delete from index. guards resources.
	float  m_maxPagesPerSecond;

	float  m_maxLoadAvg;

	// redhat 9's NPTL doesn't like our async signals
	bool   m_allowAsyncSignals;

	// if in read-only mode we do no spidering and load no saved trees
	// so we can use all mem for caching index lists
	bool   m_readOnlyMode;

	// if this is true we use /etc/hosts for hostname lookup before dns
	bool   m_useEtcHosts;

	bool   m_useMergeToken;

	// . should we always read data from local machine if available?
	// . if your network is not gigabit, this may be a good idea
	bool   m_preferLocalReads;

	// should we bypass load balancing and always send titledb record
	// lookup requests to a host to maxmize tfndb page cache hits?
	//bool   m_useBiasedTfndb;

	// calls fsync(fd) if true after each write
	bool   m_flushWrites ; 
	bool   m_verifyWrites;
	long   m_corruptRetries;

	// log unfreed memory on exit
	bool   m_detectMemLeaks;

	// . if false we will not keep spelling information in memory
	// . we will keep the popularity info from dict though, since related
	//   topics requires that
	bool   m_doSpellChecking;

	// . give suggestions to narrow the search
	bool   m_doNarrowSearch;

	// maximum number of synonyms/stems to expand a word into
	//long   m_maxSynonyms;

	// default affinity for spelling suggestions/numbers
	//float  m_defaultAffinity;

	// threshold for synonym usage
	//float  m_frequencyThreshold;

	// thesaurus configuration
	//long   m_maxAffinityRequests;
	//long   m_maxAffinityErrors;
	//long   m_maxAffinityAge;
	//long   m_affinityTimeout;
	//char   m_affinityServer[MAX_URL_LEN];
	//char   m_affinityParms[MAX_URL_LEN];

	// new syncing information
	bool   m_syncEnabled;
	bool   m_syncIndexdb;
	bool   m_syncTitledb;
	bool   m_syncSpiderdb;
	//bool   m_syncChecksumdb;
	bool   m_syncSitedb;
	bool   m_syncLogging;
	bool   m_syncDoUnion;
	bool   m_syncDryRun;
	char   m_syncHostIds [ 256 ]; // restrict syncing to these host ids
	//long   m_syncReadBufSize;     // limit disk activity for syncing
	//long   m_syncSeeksPerSecond;  // limit disk activity for syncing
	long   m_syncBytesPerSecond;  // limit disk activity for syncing

	// if this is true we do not add indexdb keys that *should* already
	// be in indexdb. but if you recently upped the m_truncationLimit
	// then you can set this to false to add all indexdb keys.
	//bool   m_onlyAddUnchangedTermIds;
	bool   m_doIncrementalUpdating;

	// always true unless entire indexdb was deleted and we are rebuilding
	bool   m_indexDeletes;

	bool   m_splitTwins;
	bool   m_useThreads;
	bool   m_useSHM;
	bool   m_useQuickpoll;

	bool   m_useDiskPageCacheIndexdb;
	bool   m_useDiskPageCachePosdb;
	bool   m_useDiskPageCacheDatedb;
	bool   m_useDiskPageCacheTitledb;
	bool   m_useDiskPageCacheSpiderdb;
	bool   m_useDiskPageCacheTfndb;
	bool   m_useDiskPageCacheTagdb;
	bool   m_useDiskPageCacheChecksumdb;
	bool   m_useDiskPageCacheClusterdb;
	bool   m_useDiskPageCacheCatdb;
	bool   m_useDiskPageCacheLinkdb;

	//bool   m_quickpollCoreOnError;
	bool   m_useShotgun;
	bool   m_testMem;
	bool   m_doConsistencyTesting;

	// temporary hack for fixing docid collision resolution bug
	bool   m_hackFixWords;
	bool   m_hackFixPhrases;

	// flags for excluding docs with only linktext or meta text matches
	// for one or more query terms
	//bool   m_excludeLinkText;
	//bool   m_excludeMetaText;

	// deny robots access to the search results
	//bool   m_robotCheck;

	// scan all titledb files if we can't find the rec where it should be
	bool   m_scanAllIfNotFound;
	
	// defaults to "Gigabot/1.0"
	char m_spiderUserAgent [ USERAGENTMAXSIZE ];

	long m_autoSaveFrequency;

	long m_docCountAdjustment;

	bool m_profilingEnabled;
	bool m_dynamicPerfGraph;
	long m_minProfThreshold;
	bool m_sequentialProfiling;
	long m_realTimeProfilerMinQuickPollDelta;

	//long m_summaryMode;	// JAB: moved to CollectionRec

	// . for query-dependent summary/title generation
	//long  m_titleMaxLen;
	//long  m_summaryMaxLen;
	//long  m_summaryMaxNumLines;
	//long  m_summaryMaxNumCharsPerLine;
	//long  m_summaryDefaultNumLines;
	//char  m_summaryFrontHighlightTag[128];
	//char  m_summaryBackHighlightTag [128];


	//
	// See Log.h for an explanation of the switches below
	//

	// GET and POST requests.
	bool  m_logHttpRequests;
	bool  m_logAutobannedQueries;
	//bool  m_logQueryTimes;
	// if query took this or more milliseconds, log its time
	long  m_logQueryTimeThreshold;
	bool  m_logQueryReply;
	bool  m_logQueryDebug;
	// log what gets into the index
	bool  m_logSpideredUrls;
	// log informational messages, they are not indicative of any error.
	bool  m_logInfo;
	// when out of udp slots
	bool  m_logNetCongestion;
	// doc quota limits, url truncation limits
	bool  m_logLimits;
	// log debug switches
	bool  m_logDebugAddurl  ;
	bool  m_logDebugAdmin   ;
	bool  m_logDebugBuild   ;
	bool  m_logDebugBuildTime ;
	bool  m_logDebugDb      ;
	bool  m_logDebugDisk    ;
	bool  m_logDebugDns     ;
	bool  m_logDebugDownloads;
	bool  m_logDebugFacebook;
	bool  m_logDebugHttp    ;
	bool  m_logDebugLoop    ;
	bool  m_logDebugLang    ;
	bool  m_logDebugLinkInfo ;
	bool  m_logDebugMem     ;
	bool  m_logDebugMemUsage;
	bool  m_logDebugMerge   ;
	bool  m_logDebugNet     ;
	bool  m_logDebugPQR     ; // post query rerank
	bool  m_logDebugQuery   ;
	bool  m_logDebugQuota   ;
	bool  m_logDebugRobots	;
	bool  m_logDebugSpcache ; // SpiderCache.cpp debug
	bool  m_logDebugSpeller ;
	bool  m_logDebugTagdb   ;
	bool  m_logDebugSections;
	bool  m_logDebugSEO;
	bool  m_logDebugSEOInserts;
	bool  m_logDebugStats   ;
	bool  m_logDebugSummary ;
	bool  m_logDebugSpider  ;
	bool  m_logDebugUrlAttempts ;
	bool  m_logDebugTcp     ;
	bool  m_logDebugThread  ;
	bool  m_logDebugTimedb  ;
	bool  m_logDebugTitle   ;
	bool  m_logDebugTopics  ;
	bool  m_logDebugTopDocs ;
	bool  m_logDebugUdp     ;
	bool  m_logDebugUnicode ;
	bool  m_logDebugRepair  ;
	bool  m_logDebugDate    ;
	// expensive timing messages
	bool m_logTimingAddurl  ;
	bool m_logTimingAdmin   ;
	bool m_logTimingBuild;
	bool m_logTimingDb;
	bool m_logTimingNet;
	bool m_logTimingQuery;
	bool m_logTimingSpcache;
	bool m_logTimingTopics;
	// programmer reminders.
	bool m_logReminders;

	long m_numMasterPwds;
	char m_masterPwds[MAX_MASTER_PASSWORDS][PASSWORD_MAX_LEN];
	long m_numMasterIps;
	long m_masterIps[MAX_MASTER_IPS];

	long  m_numConnectIps;
	long  m_connectIps [ MAX_CONNECT_IPS ];

	// should we generate similarity/content vector for titleRecs lacking?
	// this takes a ~100+ ms, very expensive, so it is just meant for
	// testing.
	bool m_generateVectorAtQueryTime;

	//Users
	char m_users [ USERS_TEXT_SIZE ];
	long m_usersLen;

	char m_superTurks [ USERS_TEXT_SIZE ];
	long m_superTurksLen;

	long m_maxYippyOut;

	char  m_doAutoBan;
	long  m_banIpsLen;
	char  m_banIps   [ AUTOBAN_TEXT_SIZE ];
	long  m_allowIpsLen;
	char  m_allowIps [ AUTOBAN_TEXT_SIZE ];
	long  m_validCodesLen;
	char  m_validCodes[ AUTOBAN_TEXT_SIZE ];
	long  m_banRegexLen;
	char  m_banRegex [ AUTOBAN_TEXT_SIZE ];
	long  m_extraParmsLen;
	char  m_extraParms [ AUTOBAN_TEXT_SIZE ];
	unsigned char  m_numFreeQueriesPerMinute;
	unsigned long  m_numFreeQueriesPerDay;

	char m_redirect[MAX_URL_LEN];
        char m_useCompressionProxy;
        char m_gzipDownloads;

	// used by proxy to make proxy point to the temp cluster while
	// the original cluster is updated
        char m_useTmpCluster;

        char m_timeSyncProxy;
        // For remote datafeed verification
	//char m_useDFAcctServer; 
        //long m_dfAcctIp;
        //long m_dfAcctPort;
        //char m_dfAcctColl[MAX_COLL_LEN];

	Xml   m_xml;
	char  m_buf[10*1024];
	long  m_bufSize;

	// . for specifying if this is an interface machine
	//   messages are rerouted from this machine to the main
	//   cluster set in the hosts.conf.
	bool m_interfaceMachine;

	// after we take the natural log of each query term's DF (doc freq.)
	// we 
	float m_queryExp;
	//char  m_useDynamicPhraseWeighting;

	float m_minPopForSpeller; // 0% to 100%

	// catdb min site rec size for LARGE but latent domains
	long  m_catdbMinRecSizes;

	// allow scaling up of hosts by removing recs not in the correct
	// group. otherwise a sanity check will happen.
	char  m_allowScale;
	// . timeout on dead hosts, only set when we know a host is dead and
	//   will not come back online.  Messages will timeout on the dead
	//   host, but not error, allowing outstanding spidering to finish
	//   to the twin
	char  m_giveupOnDeadHosts;
	char  m_bypassValidation;

	long  m_maxHardDriveTemp;

	long  m_maxHeartbeatDelay;
	long  m_maxCallbackDelay;

	// balance value for Msg6, each host can have this many ready domains
	// per global host
	//long m_distributedSpiderBalance;
	//long m_distributedIpWait;

	// parameters for indexdb spitting and tfndb extension bits
	//long  m_indexdbSplit;
	//char  m_fullSplit;
	//char  m_legacyIndexdbSplit;
	//long  m_tfndbExtBits;

	// used by Repair.cpp
	char  m_repairingEnabled  ;
	long  m_maxRepairSpiders  ;
	long  m_repairMem;
	char  m_collsToRepair[1024];
	char  m_fullRebuild       ;
	char  m_fullRebuildKeepNewSpiderRecs;
	char  m_rebuildRecycleLinkInfo  ;
	//char  m_rebuildRecycleLinkInfo2 ;
	//char  m_removeBadPages    ;
	char  m_rebuildTitledb    ;
	//char  m_rebuildTfndb      ;
	//char  m_rebuildIndexdb    ;
	char  m_rebuildPosdb    ;
	//char  m_rebuildNoSplits   ;
	//char  m_rebuildDatedb     ;
	//char  m_rebuildChecksumdb ;
	char  m_rebuildClusterdb  ;
	char  m_rebuildSpiderdb   ;
	//char  m_rebuildSitedb     ;
	char  m_rebuildLinkdb     ;
	//char  m_rebuildTagdb      ;
	//char  m_rebuildPlacedb    ;
	char  m_rebuildTimedb     ;
	char  m_rebuildSectiondb  ;
	//char  m_rebuildRevdb      ;
	char  m_rebuildRoots      ;
	char  m_rebuildNonRoots   ;

	char  m_rebuildSkipSitedbLookup ;

	// for caching the qualities of urls (see Msg20.cpp)
	long  m_maxQualityCacheAge ;
};

extern class Conf g_conf;

#endif

// old stuff:
// key is the hostId. hostId of -1 is the default conf record.
// here's the recognized fields:
// <dirPubKey>    // default rec only
// <groupMask>    // default rec only
// <rootDir>      // default rec only
// <numPolice>    // default rec only
// <isTrustedNet> // default rec only
// <hostId> -- stored in hostmap
// <ip>     -- stored in hostmap
// <port>   -- stored in hostmap
// <networkName>  // also in default rec
// <maxDiskSpace>
// <maxMem>
// <maxCpu>
// <maxBps>
// <pubKey>
// <isTrustedHost>  // director sealed
