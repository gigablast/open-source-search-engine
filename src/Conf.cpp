#include "gb-include.h"

#include <cstring>

#include "Conf.h"
#include "Parms.h"
//#include "CollectionRec.h"
#include "Indexdb.h"
#include "Users.h"
#include "Proxy.h"

Conf g_conf;

static bool s_setUmask = false;;

mode_t getFileCreationFlags() {
	if ( ! s_setUmask ) {
		s_setUmask = true;
		umask  ( 0 );
	}
	return  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH ;
}

mode_t getDirCreationFlags() {
	if ( ! s_setUmask ) {
		s_setUmask = true;
		umask  ( 0 );
	}
	return  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH |
		S_IXUSR | S_IXGRP;
}

Conf::Conf ( ) {
	std::memset(m_confFilename, 0, sizeof(m_confFilename));
	m_maxMem = 0;
	m_save = 0;
	m_runAsDaemon = false;
	m_logToFile = false;
	m_isLocal = false;
	std::memset(m_stripeDir, 0, sizeof(m_stripeDir));
	std::memset(m_defaultColl, 0, sizeof(m_defaultColl));
	std::memset(m_dirColl, 0, sizeof(m_dirColl));
	std::memset(m_dirHost, 0, sizeof(m_dirHost));
	std::memset(m_clusterName, 0, sizeof(m_clusterName));
	m_numDns = 0;
	std::memset(m_dnsIps, 0, sizeof(m_dnsIps));
	std::memset(m_dnsPorts, 0, sizeof(m_dnsPorts));
	m_dnsMaxCacheMem = 0;
	m_dnsSaveCache = false;
	std::memset(m_geocoderIps, 0, sizeof(m_geocoderIps));
	m_wikiProxyIp = 0;
	m_wikiProxyPort = 0;
	m_useRandAgents = false;
	m_useProxyIps = false;
	m_automaticallyUseProxyIps = false;
	m_askRootNameservers = 0;
	m_numRns = 0;
	std::memset(m_rnsIps, 0, sizeof(m_rnsIps));
	std::memset(m_rnsPorts, 0, sizeof(m_rnsPorts));
	m_mergeBufSize = 0;
	m_tagdbMaxTreeMem = 0;
	m_catdbMaxTreeMem = 0;
	m_catdbMaxCacheMem = 0;
	m_revdbMaxTreeMem = 0;
	m_timedbMaxTreeMem = 0;
	m_clusterdbMaxTreeMem = 0;
	m_clusterdbMinFilesToMerge = 0;
	m_clusterdbSaveCache = false;
	m_doingCommandLine = false;
	m_linkdbMaxTreeMem = 0;
	m_linkdbMinFilesToMerge = 0;
	m_maxVectorCacheMem = 0;
	m_maxCpuThreads = 0;
	m_maxCpuMergeThreads = 0;
	m_deadHostTimeout = 0;
	m_sendEmailTimeout = 0;
	m_pingSpacer = 0;
	m_maxWriteThreads = 0;
	m_spiderMaxDiskThreads = 0;
	m_separateDiskReads = 0;
	m_statsdbMaxTreeMem = 0;
	m_statsdbMaxCacheMem = 0;
	m_useStatsdb = false;
	m_spideringEnabled = false;
	m_turkingEnabled = false;
	m_injectionsEnabled = false;
	m_queryingEnabled = false;
	m_returnResultsAnyway = false;
	m_testParserEnabled = false;
	m_testSpiderEnabled = false;
	m_testSearchEnabled = false;
	m_addUrlEnabled = false;
	m_adFeedEnabled = false;
	m_doStripeBalancing = false;
	m_isLive = false;
	m_isWikipedia = false;
	m_robotdbMaxCacheMem = 0;
	m_robotdbSaveCache = false;
	m_maxTotalSpiders = 0;
	m_indexdbMaxTreeMem = 0;
	m_indexdbMaxCacheMem = 0;
	m_indexdbMaxIndexListAge = 0;
	m_indexdbTruncationLimit = 0;
	m_indexdbMinFilesToMerge = 0;
	m_indexdbSaveCache = false;
	m_datedbMaxTreeMem = 0;
	m_datedbMaxCacheMem = 0;
	m_datedbMaxIndexListAge = 0;
	m_datedbTruncationLimit = 0;
	m_datedbMinFilesToMerge = 0;
	m_datedbSaveCache = false;
	m_udpPort = 0;
	m_httpMaxSockets = 0;
	m_httpsMaxSockets = 0;
	m_httpMaxSendBufSize = 0;
	m_searchResultsMaxCacheMem = 0;
	m_searchResultsMaxCacheAge = 0;
	m_searchResultsSaveCache = false;
	m_siteLinkInfoMaxCacheMem = 0;
	m_siteLinkInfoMaxCacheAge = 0;
	m_siteLinkInfoSaveCache = false;
	m_siteQualityMaxCacheMem = 0;
	m_siteQualityMaxCacheAge = 0;
	m_siteQualitySaveCache = false;
	m_maxIncomingLinksToSample = 0;
	m_queryPhraseWeight = 0.0f;
	m_sliderParm = 0;
	std::memset(m_sendmailIp, 0, sizeof(m_sendmailIp));
	m_sendEmailAlerts = false;
	m_delayNonCriticalEmailAlerts = false;
	std::memset(m_delayEmailsAfter, 0, sizeof(m_delayEmailsAfter));
	std::memset(m_delayEmailsBefore, 0, sizeof(m_delayEmailsBefore));
	m_sendEmailAlertsToSysadmin = false;
	m_sendEmailAlertsToEmail1 = false;
	std::memset(m_email1MX, 0, sizeof(m_email1MX));
	std::memset(m_email1Addr, 0, sizeof(m_email1Addr));
	std::memset(m_email1From, 0, sizeof(m_email1From));
	m_sendEmailAlertsToEmail2 = false;
	std::memset(m_email2MX, 0, sizeof(m_email2MX));
	std::memset(m_email2Addr, 0, sizeof(m_email2Addr));
	std::memset(m_email2From, 0, sizeof(m_email2From));
	m_sendEmailAlertsToEmail3 = false;
	std::memset(m_email3MX, 0, sizeof(m_email3MX));
	std::memset(m_email3Addr, 0, sizeof(m_email3Addr));
	std::memset(m_email3From, 0, sizeof(m_email3From));
	m_sendEmailAlertsToEmail4 = false;
	std::memset(m_email4MX, 0, sizeof(m_email4MX));
	std::memset(m_email4Addr, 0, sizeof(m_email4Addr));
	std::memset(m_email4From, 0, sizeof(m_email4From));
	std::memset(m_errstr1, 0, sizeof(m_errstr1));
	std::memset(m_errstr2, 0, sizeof(m_errstr2));
	std::memset(m_errstr3, 0, sizeof(m_errstr3));
	m_sendParmChangeAlertsToEmail1 = 0;
	m_sendParmChangeAlertsToEmail2 = 0;
	m_sendParmChangeAlertsToEmail3 = 0;
	m_sendParmChangeAlertsToEmail4 = 0;
	m_avgQueryTimeThreshold = 0.0f;
	m_querySuccessThreshold = 0.0f;
	m_numQueryTimes = 0;
	m_maxCorruptLists = 0;
	m_maxSerializedQuerySize = 0;
	m_maxIncomingKbps = 0.0f;
	m_maxPagesPerSecond = 0.0f;
	m_maxLoadAvg = 0.0f;
	m_allowAsyncSignals = false;
	m_useCollectionPasswords = false;
	m_allowCloudUsers = false;
	m_readOnlyMode = false;
	m_useEtcHosts = false;
	m_useMergeToken = false;
	m_preferLocalReads = false;
	m_verifyDumpedLists = false;
	m_flushWrites = false;
	m_verifyWrites = false;
	m_corruptRetries = 0;
	m_detectMemLeaks = false;
	m_doSpellChecking = false;
	m_doNarrowSearch = false;
	m_isMattWells = false;
	m_forceIt = false;
	m_syncEnabled = false;
	m_syncIndexdb = false;
	m_syncTitledb = false;
	m_syncSpiderdb = false;
	m_syncSitedb = false;
	m_syncLogging = false;
	m_syncDoUnion = false;
	m_syncDryRun = false;
	std::memset(m_syncHostIds, 0, sizeof(m_syncHostIds));
	m_syncBytesPerSecond = 0;
	m_doIncrementalUpdating = false;
	m_indexDeletes = false;
	m_splitTwins = false;
	m_useThreads = false;
	m_useThreadsForDisk = false;
	m_useThreadsForIndexOps = false;
	m_useThreadsForSystemCalls = false;
	m_useSHM = false;
	m_useQuickpoll = false;
	m_posdbFileCacheSize = 0;
	m_tagdbFileCacheSize = 0;
	m_clusterdbFileCacheSize = 0;
	m_titledbFileCacheSize = 0;
	m_spiderdbFileCacheSize = 0;
	m_useShotgun = false;
	m_testMem = false;
	m_doConsistencyTesting = false;
	m_hackFixWords = false;
	m_hackFixPhrases = false;
	m_scanAllIfNotFound = false;
	std::memset(m_spiderUserAgent, 0, sizeof(m_spiderUserAgent));
	m_autoSaveFrequency = 0;
	m_docCountAdjustment = 0;
	m_profilingEnabled = false;
	m_dynamicPerfGraph = false;
	m_minProfThreshold = 0;
	m_sequentialProfiling = false;
	m_realTimeProfilerMinQuickPollDelta = 0;
	m_logHttpRequests = false;
	m_logAutobannedQueries = false;
	m_logQueryTimeThreshold = 0;
	m_logQueryReply = false;
	m_logQueryDebug = false;
	m_logSpideredUrls = false;
	m_logInfo = false;
	m_logNetCongestion = false;
	m_logLimits = false;
	m_logDebugAddurl = false;
	m_logDebugAdmin = false;
	m_logDebugBuild = false;
	m_logDebugBuildTime = false;
	m_logDebugDb = false;
	m_logDebugDirty = false;
	m_logDebugDisk = false;
	m_logDebugDiskPageCache = false;
	m_logDebugDns = false;
	m_logDebugDownloads = false;
	m_logDebugFacebook = false;
	m_logDebugHttp = false;
	m_logDebugImage = false;
	m_logDebugLoop = false;
	m_logDebugLang = false;
	m_logDebugLinkInfo = false;
	m_logDebugMem = false;
	m_logDebugMemUsage = false;
	m_logDebugMerge = false;
	m_logDebugNet = false;
	m_logDebugPQR = false;
	m_logDebugProxies = false;
	m_logDebugQuery = false;
	m_logDebugQuota = false;
	m_logDebugRobots = false;
	m_logDebugSpcache = false;
	m_logDebugSpeller = false;
	m_logDebugTagdb = false;
	m_logDebugSections = false;
	m_logDebugSEO = false;
	m_logDebugSEOInserts = false;
	m_logDebugStats = false;
	m_logDebugSummary = false;
	m_logDebugSpider = false;
	m_logDebugMsg13 = false;
	m_diffbotMsg13Hack = false;
	m_logDebugUrlAttempts = false;
	m_logDebugTcp = false;
	m_logDebugTcpBuf = false;
	m_logDebugThread = false;
	m_logDebugTimedb = false;
	m_logDebugTitle = false;
	m_logDebugTopics = false;
	m_logDebugTopDocs = false;
	m_logDebugUdp = false;
	m_logDebugUnicode = false;
	m_logDebugRepair = false;
	m_logDebugDate = false;
	m_logTimingAddurl = false;
	m_logTimingAdmin = false;
	m_logTimingBuild = false;
	m_logTimingDb = false;
	m_logTimingNet = false;
	m_logTimingQuery = false;
	m_logTimingSpcache = false;
	m_logTimingTopics = false;
	m_logReminders = false;
	m_generateVectorAtQueryTime = false;
	std::memset(m_users, 0, sizeof(m_users));
	m_usersLen = 0;
	m_maxYippyOut = 0;
	m_doAutoBan = 0;
	m_banIpsLen = 0;
	std::memset(m_banIps, 0, sizeof(m_banIps));
	m_allowIpsLen = 0;
	std::memset(m_allowIps, 0, sizeof(m_allowIps));
	m_validCodesLen = 0;
	std::memset(m_validCodes, 0, sizeof(m_validCodes));
	m_banRegexLen = 0;
	std::memset(m_banRegex, 0, sizeof(m_banRegex));
	m_extraParmsLen = 0;
	std::memset(m_extraParms, 0, sizeof(m_extraParms));
	m_numFreeQueriesPerMinute = 0;
	m_numFreeQueriesPerDay = 0;
	std::memset(m_redirect, 0, sizeof(m_redirect));
	m_useCompressionProxy = 0;
	m_gzipDownloads = 0;
	m_useTmpCluster = 0;
	m_timeSyncProxy = 0;
	std::memset(m_buf, 0, sizeof(m_buf));
	m_bufSize = 0;
	m_interfaceMachine = false;
	m_queryExp = 0.0f;
	m_minPopForSpeller = 0.0f;
	m_catdbMinRecSizes = 0;
	m_allowScale = 0;
	m_giveupOnDeadHosts = 0;
	m_bypassValidation = 0;
	m_maxHardDriveTemp = 0;
	m_maxHeartbeatDelay = 0;
	m_maxCallbackDelay = 0;
	m_repairingEnabled = 0;
	m_maxRepairSpiders = 0;
	m_repairMem = 0;
	m_rebuildAllCollections = 0;
	m_fullRebuild = 0;
	m_rebuildAddOutlinks = 0;
	m_rebuildRecycleLinkInfo = 0;
	m_rebuildTitledb = 0;
	m_rebuildPosdb = 0;
	m_rebuildClusterdb = 0;
	m_rebuildSpiderdb = 0;
	m_rebuildLinkdb = 0;
	m_rebuildTimedb = 0;
	m_rebuildSectiondb = 0;
	m_rebuildRoots = 0;
	m_rebuildNonRoots = 0;
	m_maxQualityCacheAge = 0;
	m_save = true;
	m_doingCommandLine = false;
	m_maxMem = 16000000000;
}

// . does this requester have ROOT admin privledges???
// . uses the root collection record!
// . master admin can administer ALL collections
// . use CollectionRec::hasPermission() to see if has permission
//   to adminster one particular collection
/*
bool Conf::isMasterAdmin ( TcpSocket *s , HttpRequest *r ) {
	// sometimes they don't want to be admin intentionally for testing
	if ( r->getLong ( "master" , 1 ) == 0 ) return false;
	// get connecting ip
	int32_t ip = s->m_ip;
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
	int32_t  plen;
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
	//CollectionRec *cr = getRec ( (int32_t)0 ) ;
	// call hasPermission
	//return cr->hasPermission ( p , plen , ip );

	// check admin ips
	// scan the passwords
	// MDW: no! too vulnerable to attacks!
	//for ( int32_t i = 0 ; i < m_numMasterPwds ; i++ ) {
	//	if ( strcmp ( m_masterPwds[i], p ) != 0 ) continue;
	//	// . matching one password is good enough now, default OR
	//	// . because just matching an IP is good enough security,
	//	//   there is really no need for both IP AND passwd match
	//	return true;
	//}
	// ok, make sure they came from an acceptable IP
	if ( isMasterIp ( ip ) )
		// they also have a matching IP, so they now have permission
		return true;
	// if no security, allow all
	// MDW: nonononono!!!!
	//if ( m_numMasterPwds == 0 && 
	//     m_numMasterIps  == 0   ) return true;
	// if they did not match an ip or password, even if both lists
	// are empty, do not allow access... this prevents security breeches
	// by accident
	return false;
}
*/

bool isInWhiteSpaceList ( char *p , char *buf ) {

	if ( ! p ) return false;

	char *match = strstr ( buf , p );
	if ( ! match ) return false;
	
	int32_t len = gbstrlen(p);

	// ensure book-ended by whitespace
	if (  match && 
	      (match == buf || is_wspace_a(match[-1])) &&
	      (!match[len] || is_wspace_a(match[len])) )
		return true;

	// no match
	return false;
}

bool Conf::isCollAdmin ( TcpSocket *sock , HttpRequest *hr ) {

	// until we have coll tokens use this...
	//return isMasterAdmin ( socket , hr );

	// master always does
	if ( isMasterAdmin ( sock , hr ) ) return true;

	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) return false;

	return isCollAdmin2 ( sock , hr , cr );

}

bool Conf::isCollAdminForColl ( TcpSocket *sock, HttpRequest *hr, char *coll ){

	CollectionRec *cr = g_collectiondb.getRec ( coll );

	if ( ! cr ) return false;

	return isCollAdmin2 ( sock , hr , cr );
}

bool Conf::isCollAdmin2 ( TcpSocket *sock , 
			  HttpRequest *hr ,
			  CollectionRec *cr ) {

	if ( ! cr ) return false;

	//int32_t page = g_pages.getDynamicPageNumber(hr);

	// never for main or dmoz! must be root!
	if ( strcmp(cr->m_coll,"main")==0 ) return false;
	if ( strcmp(cr->m_coll,"dmoz")==0 ) return false;

	if ( ! g_conf.m_useCollectionPasswords) return false;

	// empty password field? then allow them through
	if ( cr->m_collectionPasswords.length() <= 0 &&
	     cr->m_collectionIps      .length() <= 0 )
		return true;

	// a good ip?
	char *p   = iptoa(sock->m_ip);
	char *buf = cr->m_collectionIps.getBufStart();
	if ( isInWhiteSpaceList ( p , buf ) ) return true;

	// if they got the password, let them in
	p = hr->getString("pwd");
	if ( ! p ) p = hr->getString("password");
	if ( ! p ) p = hr->getStringFromCookie("pwd");
	if ( ! p ) return false;
	buf = cr->m_collectionPasswords.getBufStart();
	if ( isInWhiteSpaceList ( p , buf ) ) return true;

	// the very act of just knowing the collname of a guest account
	// is good enough to update it
	//if ( strncmp ( cr->m_coll , "guest_" , 6 ) == 0 )
	//	return true;

	return false;
}
	

// . is user a root administrator?
// . only need to be from root IP *OR* have password, not both
bool Conf::isMasterAdmin ( TcpSocket *socket , HttpRequest *hr ) {

	bool isAdmin = false;

	// totally open access?
	//if ( m_numConnectIps  <= 0 && m_numMasterPwds <= 0 )
	if ( m_connectIps.length() <= 0 &&
	     m_masterPwds.length() <= 0 )
		isAdmin = true;

	// coming from root gets you in
	if ( socket && isMasterIp ( socket->m_ip ) ) 
		isAdmin = true;

	//if ( isConnectIp ( socket->m_ip ) ) return true;

	if ( hasMasterPwd ( hr ) ) 
		isAdmin = true;

	if ( ! isAdmin )
		return false;

	// default this to true so if user specifies &admin=0 then it 
	// cancels our admin view
	if ( hr && ! hr->getLong("admin",1) )
		return false;
	
	return true;
}


bool Conf::hasMasterPwd ( HttpRequest *hr ) {

	//if ( m_numMasterPwds == 0 ) return false;
	if ( m_masterPwds.length() <= 0 )
		return false;

	char *p = hr->getString("pwd");

	if ( ! p ) p = hr->getString("password");

	if ( ! p ) p = hr->getStringFromCookie("pwd");

	if ( ! p ) return false;

	char *buf = m_masterPwds.getBufStart();

	return isInWhiteSpaceList ( p , buf );
}

// . check this ip in the list of admin ips
bool Conf::isMasterIp ( uint32_t ip ) {

	//if ( m_numMasterIps == 0 ) return false;
	//if ( m_numConnectIps == 0 ) return false;
	if ( m_connectIps.length() <= 0 ) return false;

	// for ( int32_t i = 0 ; i < m_numConnectIps ; i++ ) 
	// 	if ( m_connectIps[i] == (int32_t)ip )
	// 		return true;

	//if ( ip == atoip("10.5.0.2",8) ) return true;

	char *p = iptoa(ip);
	char *buf = m_connectIps.getBufStart();

	return isInWhiteSpaceList ( p , buf );
}

bool Conf::isConnectIp ( uint32_t ip ) {

	return isMasterIp(ip);

	// for ( int32_t i = 0 ; i < m_numConnectIps ; i++ ) {
	// 	if ( m_connectIps[i] == (int32_t)ip )
	// 		return true;
	// 	// . 1.2.3.0 ips mean the whole block 
	// 	// . the high byte in the int32_t is the Least Significant Byte
	// 	if ( (m_connectIps[i] >> 24) == 0 &&
	// 	     (m_connectIps[i] & 0x00ffffff) == 
	// 	     ((int32_t)ip        & 0x00ffffff)    )
	// 		return true;
	// }
	// no match
	//return false;
}

// . set all member vars to their default values
void Conf::reset ( ) {
	g_parms.setToDefault ( (char *)this , OBJ_CONF ,NULL);
	m_save = true;
}

bool Conf::init ( char *dir ) { // , int32_t hostId ) {
	g_parms.setToDefault ( (char *)this , OBJ_CONF ,NULL);
	m_save = true;
	char fname[1024];
	//if ( dir ) sprintf ( fname , "%slocalgb.conf", dir );
	//else       sprintf ( fname , "./localgb.conf" );
	File f;
	//f.set ( fname );
	//m_isLocal = true;
	//if ( ! f.doesExist() ) {
	m_isLocal = false;
	if ( dir ) sprintf ( fname , "%sgb.conf", dir );
	else       sprintf ( fname , "./gb.conf" );
	// try regular gb.conf then
	f.set ( fname );
	//}

	// make sure g_mem.maxMem is big enough temporarily
	g_conf.m_maxMem = 8000000000; // 8gb temp

	bool status = g_parms.setFromFile ( this , fname , NULL , OBJ_CONF );

	if ( g_conf.m_maxMem < 10000000 ) g_conf.m_maxMem = 10000000;

	// if not there, create it!
	if ( ! status ) {
		log("gb: Creating %s from defaults.",fname);
		g_errno = 0;
		// set to defaults
		g_conf.reset();
		// and save it
		//log("gb: Saving %s",fname);
		m_save = true;
		save();
		// clear errors
		g_errno = 0;
		status = true;
	}
		

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
	if ( ! g_mem.init ( ) ) return false;
	// always turn this off
	g_conf.m_testMem      = false;
	// and this, in case you forgot to turn it off
	if ( g_conf.m_isLive ) g_conf.m_doConsistencyTesting = false;
	// and this on
	g_conf.m_indexDeletes = true;

	// leave it turned off for diffbot since it always needs to be crawling
#ifdef DIFFBOT
	// force spiders on
	g_conf.m_spideringEnabled = true;
#else
	// always force off on startup if not diffbot
	//g_conf.m_spideringEnabled = false;
#endif
	// this off
	g_conf.m_repairingEnabled = false;
	// make this 1 day for now (in seconds)
	g_conf.m_maxQualityCacheAge = 3600*24;
	// hack this off until the overrun bug is fixed
	g_conf.m_datedbMaxCacheMem = 0;

	//g_conf.m_qaBuildMode = true;// false

	// force on for now
	g_conf.m_useStatsdb = true;

	// hard-code disable this -- could be dangerous
	g_conf.m_bypassValidation = true;//false;
	// this could too! (need this)
	g_conf.m_allowScale = true;//false;

	// . until we fix spell checker
	// . the hosts splitting count isn't right and it just sends to like
	//   host #0 or something...
	g_conf.m_doSpellChecking = false;

	g_conf.m_forceIt = false;

	// always turn on threads if live
	//if ( g_conf.m_isLive ) g_conf.m_useThreads = true;
	// disable this at startup always... no since might have crashed
	// in the middle of a test. and we just turn on spiders again when
	// already in test mode otherwise hostid #0 will erase all the files.
	//g_conf.m_testParserEnabled = false;
	//g_conf.m_testSpiderEnabled = false;
	//g_conf.m_testSearchEnabled = false;


	/*
	//
	// are we running in Matt Wells's data center?
	// if so, we want to be able to use the seo tools that are not part
	// of the open source. we also want to be able to control the
	// data center fans for optimal cooling.
	//
	// get hostname from /etc/host
	SafeBuf sb; 
	sb.fillFromFile("/etc/hostname");
	g_errno = 0;
	bool priv = false;
	char *hh = sb.getBufStart();
	// cut off tail
	sb.removeLastChar('\n');
	sb.removeLastChar('\n');
	if ( hh && strcmp(hh,"galileo") == 0) priv = true;
	if ( hh && strcmp(hh,"sputnik") == 0) priv = true;
	if ( hh && strcmp(hh,"titan") == 0) priv = true;
	if ( hh && hh[0]=='g' && hh[1]=='k' && is_digit(hh[2]) ) priv = true;
	//if(hh[0]=='s' && hh[1]=='p' && is_digit(hh[2])) ) priv = true;
	if ( priv ) g_conf.m_isMattWells = true;
	else        g_conf.m_isMattWells = false;
	*/
	g_conf.m_isMattWells = false;

#ifdef MATTWELLS
	g_conf.m_isMattWells = true;
#endif

	// this is not possible
	/*
	if ( g_hostdb.getNumGroups() != g_hostdb.m_indexSplits ) {
		log("db: Cannot do full split where indexdb split "
		    "is not %"INT32".",(int32_t)g_hostdb.getNumGroups());
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
	if ( g_hostdb.m_indexSplits > MAX_SHARDS ) {
		log("db: Increase MAX_SHARDS");
		gbassert(false); 
	}
	// and always keep a decent site quality cache of at least 3M
	if ( g_conf.m_siteQualityMaxCacheMem < 3000000 )
		g_conf.m_siteQualityMaxCacheMem = 3000000;


	//m_useDiffbot = false;

	//#ifdef DIFFBOT	
	// make sure all collections index into a single unified collection
	//m_useDiffbot = true;
	//#endif

	// HACK: set this now
	setRootIps();

	return status;
}

void Conf::setRootIps ( ) {

	//m_numDns = 16;
	//for ( int32_t i = 0; i < m_numDns; i++ )
	//	m_dnsPorts[i] = 53;
	//m_numDns = 0;

	// set m_numDns based on Conf::m_dnsIps[] array
	int32_t i; for ( i = 0; i < 16 ; i++ ) {
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
	// and return as well
	return;

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

	int32_t n = sizeof(rootIps)/sizeof(char *);
	if ( n > MAX_RNSIPS ) {
		log("admin: Too many root nameserver ips. Truncating.");
		n = MAX_RNSIPS;
	}
	m_numRns = n;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		m_rnsIps  [i] = atoip(rootIps[i],gbstrlen(rootIps[i]));
		m_rnsPorts[i] = 53;
		log(LOG_INIT,"dns: Using root nameserver #%" INT32 " %s.",
		    i,iptoa(m_rnsIps[i]));
	}
}

// . parameters can be changed on the fly so we must save Conf
bool Conf::save ( ) {
	if ( ! m_save ) return true;
	// always reset this before saving
	bool keep = g_conf.m_testMem ;
	g_conf.m_testMem = false;
	//char fname[1024];
	//sprintf ( fname , "%sgb.conf.saving", g_hostdb.m_dir );
	// fix so if we core in malloc/free we can still save conf
	char fnbuf[1024];
	SafeBuf fn(fnbuf,1024);
	fn.safePrintf("%sgb.conf",g_hostdb.m_dir);
	bool status = g_parms.saveToXml ( (char *)this , 
					  fn.getBufStart(),
					  OBJ_CONF);
	/*
	if ( status ) {
		char fname2[1024];
		char *local = "";
		if ( m_isLocal ) local = "local";
		sprintf( fname2 , "%s%sgb.conf" , g_hostdb.m_dir , local );
		if(access(fname2, F_OK) == 0) unlink(fname2);
		if(link(fname, fname2) == 0) {
			unlink(fname);
			log(LOG_INFO,"admin: Saved %s.",fname2);
		} else {
			log(LOG_INFO,"admin: Unable to save %s:%s",
					fname, strerror(errno));
		}
	}
	*/
	// restore
	g_conf.m_testMem = keep;
	return status;
}

// . get the default collection based on hostname
//   will look for the hostname in each collection for a match
//   no match defaults to default collection
char *Conf::getDefaultColl ( char *hostname, int32_t hostnameLen ) {
	if ( ! m_defaultColl || ! m_defaultColl[0] )
		return "main";
	// just use default coll for now to keep things simple
	return m_defaultColl;
	/*
	// return defaultColl for empty hostname
	if (!hostname || hostnameLen <= 0)
		return m_defaultColl;
	// check each coll for the hostname
	int32_t numRecs = g_collectiondb.getNumRecs();
	collnum_t currCollnum = g_collectiondb.getFirstCollnum();
	for ( int32_t i = 0; i < numRecs; i++ ) {
		// get the collection name
		char *coll = g_collectiondb.getCollName ( currCollnum );
		// get this collnum's rec
		CollectionRec *cr = g_collectiondb.getRec ( coll );
		// loop through 3 possible hostnames
		for ( int32_t h = 0; h < 3; h++ ) {
			char *cmpHostname;
			switch ( h ) {
			case 0: cmpHostname = cr->m_collectionHostname;  break;
			case 1: cmpHostname = cr->m_collectionHostname1; break;
			case 2: cmpHostname = cr->m_collectionHostname2; break;
			}
			// . get collection hostname length, reject if 0 or
			//   larger than hostnameLen (impossible match)
			int32_t cmpLen = gbstrlen(cmpHostname);
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
	*/
}
