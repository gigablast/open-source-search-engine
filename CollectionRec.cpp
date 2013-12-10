#include "gb-include.h"

#include "CollectionRec.h"
#include "Collectiondb.h"
#include "HttpServer.h"     // printColors2()
#include "Msg5.h"
#include "Threads.h"
#include "Datedb.h"
#include "Timedb.h"
#include "Spider.h"
#include "Process.h"

static CollectionRec g_default;


CollectionRec::CollectionRec() {
	//m_numSearchPwds = 0;
	//m_numBanIps     = 0;
	//m_numSearchIps  = 0;
	//m_numSpamIps    = 0;
	//m_numAdminPwds  = 0;
	//m_numAdminIps   = 0;
	memset ( m_bases , 0 , 4*RDB_END );
	// how many keys in the tree of each rdb? we now store this stuff
	// here and not in RdbTree.cpp because we no longer have a maximum
	// # of collection recs... MAX_COLLS. each is a 32-bit "long" so
	// it is 4 * RDB_END...
	memset ( m_numNegKeysInTree , 0 , 4*RDB_END );
	memset ( m_numPosKeysInTree , 0 , 4*RDB_END );
	m_spiderColl = NULL;
	m_overflow  = 0x12345678;
	m_overflow2 = 0x12345678;
	// the spiders are currently uninhibited i guess
	m_spiderStatus = SP_INITIALIZING; // this is 0
	//m_spiderStatusMsg = NULL;
	// for Url::getSite()
	m_updateSiteRulesTable = 1;
	m_lastUpdateTime = 0LL;
	m_clickNScrollEnabled = false;
	// inits for sortbydatetable
	m_inProgress = false;
	m_msg5       = NULL;
	// JAB - track which regex parsers have been initialized
	//log(LOG_DEBUG,"regex: %p initalizing empty parsers", m_pRegExParser);

	// clear these out so Parms::calcChecksum can work:
	memset( m_spiderFreqs, 0, MAX_FILTERS*sizeof(*m_spiderFreqs) );
	//for ( int i = 0; i < MAX_FILTERS ; i++ ) 
	//	m_spiderQuotas[i] = -1;
	memset( m_spiderPriorities, 0, 
		MAX_FILTERS*sizeof(*m_spiderPriorities) );
	//memset( m_rulesets, 0, MAX_FILTERS*sizeof(*m_rulesets) );
	//for ( int i = 0; i < MAX_SEARCH_PASSWORDS; i++ ) {
	//	*(m_searchPwds[i]) = '\0';
	//}
	//for ( int i = 0; i < MAX_ADMIN_PASSWORDS; i++ ) {
	//	*(m_adminPwds[i]) = '\0';
	//}
	//memset( m_banIps, 0, MAX_BANNED_IPS*sizeof(*m_banIps) );
	//memset( m_searchIps, 0, MAX_SEARCH_IPS*sizeof(*m_searchIps) );
	//memset( m_spamIps, 0, MAX_SPAM_IPS*sizeof(*m_spamIps) );
	//memset( m_adminIps, 0, MAX_ADMIN_IPS*sizeof(*m_adminIps) );

	//for ( int i = 0; i < MAX_FILTERS; i++ ) {
	//	//m_pRegExParser[i] = NULL;
	//	*(m_regExs[i]) = '\0';
	//}
	m_numRegExs = 0;

	//m_requests = 0;
	//m_replies = 0;
	//m_doingCallbacks = false;

	m_lastResetCount = 0;

	// regex_t types
	m_hasucr = false;
	m_hasupr = false;

	// for diffbot caching the global spider stats
	reset();

	// add default reg ex if we do not have one
	setUrlFiltersToDefaults();
}

CollectionRec::~CollectionRec() {
	//invalidateRegEx ();
        reset();
}

// new collection recs get this called on them
void CollectionRec::setToDefaults ( ) {
	g_parms.setFromFile ( this , NULL , NULL  );
	// add default reg ex
	setUrlFiltersToDefaults();
}

void CollectionRec::reset() {

	// regex_t types
	if ( m_hasucr ) regfree ( &m_ucr );
	if ( m_hasupr ) regfree ( &m_upr );

	// make sure we do not leave spiders "hanging" waiting for their
	// callback to be called... and it never gets called
	//if ( m_callbackQueue.length() > 0 ) { char *xx=NULL;*xx=0; }
	//if ( m_doingCallbacks ) { char *xx=NULL;*xx=0; }
	//if ( m_replies != m_requests  ) { char *xx=NULL;*xx=0; }
	m_localCrawlInfo.reset();
	m_globalCrawlInfo.reset();
	//m_requests = 0;
	//m_replies = 0;
	// free all RdbBases in each rdb
	for ( long i = 0 ; i < g_process.m_numRdbs ; i++ ) {
	     Rdb *rdb = g_process.m_rdbs[i];
	     rdb->resetBase ( m_collnum );
	}

}

CollectionRec *g_cr = NULL;

// . load this data from a conf file
// . values we do not explicitly have will be taken from "default",
//   collection config file. if it does not have them then we use
//   the value we received from call to setToDefaults()
// . returns false and sets g_errno on load error
bool CollectionRec::load ( char *coll , long i ) {
	// also reset some counts not included in parms list
	reset();
	// before we load, set to defaults in case some are not in xml file
	g_parms.setToDefault ( (char *)this );
	// get the filename with that id
	File f;
	char tmp2[1024];
	sprintf ( tmp2 , "%scoll.%s.%li/coll.conf", g_hostdb.m_dir , coll,i);
	f.set ( tmp2 );
	if ( ! f.doesExist () ) return log("admin: %s does not exist.",tmp2);
	// set our collection number
	m_collnum = i;
	// set our collection name
	m_collLen = gbstrlen ( coll );
	strcpy ( m_coll , coll );

	// collection name HACK for backwards compatibility
	//if ( strcmp ( coll , "main" ) == 0 ) {
	//	m_coll[0] = '\0';
	//	m_collLen = 0;
	//}

	// the default conf file
	char tmp1[1024];
	sprintf ( tmp1 , "%sdefault.conf" , g_hostdb.m_dir );

	// . set our parms from the file.
	// . accepts OBJ_COLLECTIONREC or OBJ_CONF
	g_parms.setFromFile ( this , tmp2 , tmp1 );

	// add default reg ex IFF there are no url filters there now
	if ( m_numRegExs == 0 ) setUrlFiltersToDefaults();

	// compile regexs here
	char *rx = m_diffbotUrlCrawlRegEx.getBufStart();
	if ( rx && ! rx[0] ) rx = NULL;
	if ( rx ) m_hasucr = true;
	if ( rx && regcomp ( &m_ucr , rx ,
		       REG_EXTENDED|REG_ICASE|
		       REG_NEWLINE|REG_NOSUB) ) {
			// error!
			return log("xmldoc: regcomp %s failed: %s. "
				   "Ignoring.",
				   rx,mstrerror(errno));
	}

	rx = m_diffbotUrlProcessRegEx.getBufStart();
	if ( rx && ! rx[0] ) rx = NULL;
	if ( rx ) m_hasupr = true;
	if ( rx && regcomp ( &m_upr , rx ,
		       REG_EXTENDED|REG_ICASE|
		       REG_NEWLINE|REG_NOSUB) ) {
			// error!
			return log("xmldoc: regcomp %s failed: %s. "
				   "Ignoring.",
				   rx,mstrerror(errno));
	}


	//
	// LOAD the crawlinfo class in the collectionrec for diffbot
	//
	// LOAD LOCAL
	sprintf ( tmp1 , "%scoll.%s.%li/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	log("coll: loading %s",tmp1);
	m_localCrawlInfo.reset();
	SafeBuf sb;
	// fillfromfile returns 0 if does not exist, -1 on read error
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_localCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		memcpy ( &m_localCrawlInfo , sb.getBufStart(),sb.length() );
	// LOAD GLOBAL
	sprintf ( tmp1 , "%scoll.%s.%li/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	log("coll: loading %s",tmp1);
	m_globalCrawlInfo.reset();
	sb.reset();
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_globalCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		memcpy ( &m_globalCrawlInfo , sb.getBufStart(),sb.length() );

	// ignore errors i guess
	g_errno = 0;


	// fix for diffbot
	if ( m_isCustomCrawl ) m_dedupingEnabled = true;

	// always turn on distributed spider locking because otherwise
	// we end up calling Msg50 which calls Msg25 for the same root url
	// at the same time, thereby wasting massive resources. it is also
	// dangerous to run without this because webmaster get pissed when
	// we slam their servers.
	// This is now deprecated...
	//m_useSpiderLocks      = false;
	// and all pages downloaded from a particular ip should be done
	// by the same host in our cluster to prevent webmaster rage
	//m_distributeSpiderGet = true;

	//initSortByDateTable(m_coll);

	return true;
}

/*
bool CollectionRec::countEvents ( ) {
	// set our m_numEventsOnHost value
	log("coll: loading event count termlist gbeventcount");
	// temporarily turn off threads
	bool enabled = g_threads.areThreadsEnabled();
	g_threads.disableThreads();
	// count them
	m_numEventsOnHost = 0;
	// 1MB at a time
	long minRecSizes = 1000000;
	// look up this termlist, gbeventcount which we index in XmlDoc.cpp
	long long termId = hash64n("gbeventcount") & TERMID_MASK;
	// make datedb key from it
	key128_t startKey = g_datedb.makeStartKey ( termId , 0xffffffff );
	key128_t endKey   = g_datedb.makeEndKey ( termId , 0 );
	
	Msg5 msg5;
	RdbList list;

	// . init m_numEventsOnHost by getting the exact length of that 
	//   termlist on this host
	// . send in the ping request packet so all hosts can total up
	// . Rdb.cpp should be added to incrementally so we should have no
	//   double positives.
	// . Rdb.cpp should inspect each datedb rec for this termid in
	//   a fast an efficient manner
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_DATEDB    ,
			      m_coll        ,
			      &list         ,
			      (char *)&startKey      ,
			      (char *)&endKey        ,
			      minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      NULL          )){// msg5b
		// not allowed to block!
		char *xx=NULL;*xx=0; }
	// scan the list, score is how many valid events from that docid
	unsigned long total = 0;
	for ( ; ! list.isExhausted() ; list.skipCurrentRec() ) {
		unsigned char *rec = (unsigned char *)list.getCurrentRec();
		// in datedb score is byte #5
		total += (255-rec[5]);
	}
	// declare
	char    *lastKeyPtr;
	key128_t  newStartKey;
	// add to count. datedb uses half keys so subtract 6 bytes
	// since the termids will be the same...
	//m_numEventsOnHost += list.getListSize() / (sizeof(key128_t)-6);
	m_numEventsOnHost += total;
	// bail if under limit
	if ( list.getListSize() < minRecSizes ) goto done;
	// update key
	lastKeyPtr = list.m_listEnd - 10;
	// we make a new start key
	list.getKey ( lastKeyPtr , (char *)&newStartKey );
	// maxxed out?
	if ( newStartKey.n0==0xffffffffffffffffLL &&
	     newStartKey.n1==0xffffffffffffffffLL )
		goto done;
	// sanity check
	if ( newStartKey < startKey ) { char *xx=NULL;*xx=0; }
	if ( newStartKey > endKey   ) { char *xx=NULL;*xx=0; }
	// inc it
	newStartKey.n0++;
	// in the top if the bottom wrapped
	if ( newStartKey.n0 == 0LL ) newStartKey.n1++;
	// assign
	startKey = newStartKey;
	// and loop back up for more now
	goto loop;

 done:

	// update all colls count
	g_collectiondb.m_numEventsAllColls += m_numEventsOnHost;

	if ( enabled ) g_threads.enableThreads();
	log("coll: got %li local events in termlist",m_numEventsOnHost);

	// set "m_hasDocQualityFiler"
	//updateFilters();

	return true;
}
*/

void CollectionRec::setUrlFiltersToDefaults ( ) {
	bool addDefault = false;
	if ( m_numRegExs == 0 ) 
		addDefault = true;
	//if ( m_numRegExs > 0 && strcmp(m_regExs[m_numRegExs-1],"default") )
	//	addDefault = true;
	if ( ! addDefault ) return;

	long n = 0;

	//strcpy(m_regExs   [n],"default");
	m_regExs[n].set("default");
	m_regExs[n].nullTerm();
	m_numRegExs++;

	m_spiderFreqs     [n] = 30; // 30 days default
	m_numRegExs2++;

	m_spiderPriorities[n] = 0;
	m_numRegExs3++;

	m_maxSpidersPerRule[n] = 99;
	m_numRegExs10++;

	m_spiderIpWaits[n] = 1000;
	m_numRegExs5++;

	m_spiderIpMaxSpiders[n] = 1;
	m_numRegExs6++;

	m_spidersEnabled[n] = 1;
	m_numRegExs7++;

	//m_spiderDiffbotApiNum[n] = 1;
	//m_numRegExs11++;
	m_spiderDiffbotApiUrl[n].set("");
	m_spiderDiffbotApiUrl[n].nullTerm();
	m_numRegExs11++;
}

/*
bool CrawlInfo::print (SafeBuf *sb ) {
	return sb->safePrintf("objectsAdded:%lli\n"
			      "objectsDeleted:%lli\n"
			      "urlsConsidered:%lli\n"
			      "downloadAttempts:%lli\n"
			      "downloadSuccesses:%lli\n"
			      "processAttempts:%lli\n"
			      "processSuccesses:%lli\n"
			      "lastupdate:%lu\n"
			      , m_objectsAdded
			      , m_objectsDeleted
			      , m_urlsConsidered
			      , m_pageDownloadAttempts
			      , m_pageDownloadSuccesses
			      , m_pageProcessAttempts
			      , m_pageProcessSuccesses
			      , m_lastUpdateTime
			      );
}

bool CrawlInfo::setFromSafeBuf (SafeBuf *sb ) {
	return sscanf(sb->getBufStart(),
		      "objectsAdded:%lli\n"
		      "objectsDeleted:%lli\n"
		      "urlsConsidered:%lli\n"
		      "downloadAttempts:%lli\n"
		      "downloadSuccesses:%lli\n"
		      "processAttempts:%lli\n"
		      "processSuccesses:%lli\n"
		      "lastupdate:%lu\n"
		      , &m_objectsAdded
		      , &m_objectsDeleted
		      , &m_urlsConsidered
		      , &m_pageDownloadAttempts
		      , &m_pageDownloadSuccesses
		      , &m_pageProcessAttempts
		      , &m_pageProcessSuccesses
		      , &m_lastUpdateTime
		      );
}
*/
	
// returns false on failure and sets g_errno, true otherwise
bool CollectionRec::save ( ) {
	if ( g_conf.m_readOnlyMode ) return true;
	//File f;
	char tmp[1024];
	//sprintf ( tmp , "%scollections/%li.%s/c.conf",
	//	  g_hostdb.m_dir,m_id,m_coll);
	// collection name HACK for backwards compatibility
	//if ( m_collLen == 0 )
	//	sprintf ( tmp , "%scoll.main/coll.conf", g_hostdb.m_dir);
	//else
	sprintf ( tmp , "%scoll.%s.%li/coll.conf", 
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	if ( ! g_parms.saveToXml ( (char *)this , tmp ) ) return false;
	// log msg
	//log (LOG_INFO,"db: Saved %s.",tmp);//f.getFilename());

	//
	// save the crawlinfo class in the collectionrec for diffbot
	//
	// SAVE LOCAL
	sprintf ( tmp , "%scoll.%s.%li/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	//log("coll: saving %s",tmp);
	SafeBuf sb;
	//m_localCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_localCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.dumpToFile ( tmp ) == -1 ) {
		log("coll: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}
	// SAVE GLOBAL
	sprintf ( tmp , "%scoll.%s.%li/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (long)m_collnum );
	//log("coll: saving %s",tmp);
	sb.reset();
	//m_globalCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_globalCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.dumpToFile ( tmp ) == -1 ) {
		log("coll: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}
	
	// do not need a save now
	m_needsSave = false;
	return true;
}

// calls hasPermissin() below
bool CollectionRec::hasPermission ( HttpRequest *r , TcpSocket *s ) {
	long  plen;
	char *p     = r->getString ( "pwd" , &plen );
	long  ip    = s->m_ip;
	return hasPermission ( p , plen , ip );
}


// . does this password work for this collection?
bool CollectionRec::isAssassin ( long ip ) {
	// ok, make sure they came from an acceptable IP
	//for ( long i = 0 ; i < m_numSpamIps ; i++ ) 
	//	// they also have a matching IP, so they now have permission
	//	if ( m_spamIps[i] == ip ) return true;
	return false;
}

// . does this password work for this collection?
bool CollectionRec::hasPermission ( char *p, long plen , long ip ) {
	// just return true
	// collection permission is checked from Users::verifyColl 
	// in User::getUserType for every request
	return true;

	// scan the passwords
	// MDW: no longer, this is too vulnerable!!!
	/*
	for ( long i = 0 ; i < m_numAdminPwds ; i++ ) {
		long len = gbstrlen ( m_adminPwds[i] );
		if ( len != plen ) continue;
		if ( strncmp ( m_adminPwds[i] , p , plen ) != 0 ) continue;
		// otherwise it's a match!
		//goto checkIp;
		// . matching one password is good enough now, default OR
		// . because just matching an IP is good enough security,
		//   there is really no need for both IP AND passwd match
		return true;
	}
	*/
	// . if had passwords but the provided one didn't match, return false
	// . matching one password is good enough now, default OR
	//if ( m_numPasswords > 0 ) return false;
	// checkIp:
	// ok, make sure they came from an acceptable IP
	//for ( long i = 0 ; i < m_numAdminIps ; i++ ) 
	//	// they also have a matching IP, so they now have permission
	//	if ( m_adminIps[i] == ip ) return true;
	// if no security, allow all NONONONONONONONONO!!!!!!!!!!!!!!
	//if ( m_numAdminPwds == 0 && m_numAdminIps == 0 ) return true;
	// if they did not match an ip or password, even if both lists
	// are empty, do not allow access... this prevents security breeches
	// by accident
	return false;
	// if there were IPs then they failed to get in
	//if ( m_numAdminIps > 0 ) return false;
	// otherwise, they made it
	//return true;
}

// can this ip perform a search or add url on this collection?
bool CollectionRec::hasSearchPermission ( TcpSocket *s , long encapIp ) {
	// get the ip
	long ip = 0; if ( s ) ip = s->m_ip;
	// and the ip domain
	long ipd = 0; if ( s ) ipd = ipdom ( s->m_ip );
	// and top 2 bytes for the israel isp that has this huge block
	long ipt = 0; if ( s ) ipt = iptop ( s->m_ip );
	// is it in the ban list?
	/*
	for ( long i = 0 ; i < m_numBanIps ; i++ ) {
		if ( isIpTop ( m_banIps[i] ) ) {
			if ( m_banIps[i] == ipt ) return false;
			continue;
		}
		// check for ip domain match if this banned ip is an ip domain
		if ( isIpDom ( m_banIps[i] ) ) {
			if ( m_banIps[i] == ipd ) return false; 
			continue;
		}
		// otherwise it's just a single banned ip
		if ( m_banIps[i] == ip ) return false;
	}
	*/
	// check the encapsulate ip if any
	// 1091771468731 0 Aug 05 23:51:08 63.236.25.77 GET 
	// /search?code=mammaXbG&uip=65.87.190.39&n=15&raw=8&q=farm+insurance
	// +nj+state HTTP/1.0
	/*
	if ( encapIp ) {
		ipd = ipdom ( encapIp );
		ip  = encapIp;
		for ( long i = 0 ; i < m_numBanIps ; i++ ) {
			if ( isIpDom ( m_banIps[i] ) ) {
				if ( m_banIps[i] == ipd ) return false; 
				continue;
			}
			if ( isIpTop ( m_banIps[i] ) ) {
				if ( m_banIps[i] == ipt ) return false;
				continue;
			}
			if ( m_banIps[i] == ip ) return false;
		}
	}
	*/

	return true;
	/*
	// do we have an "only" list?
	if ( m_numSearchIps == 0 ) return true;
	// it must be in that list if we do
	for ( long i = 0 ; i < m_numSearchIps ; i++ ) {
		// check for ip domain match if this banned ip is an ip domain
		if ( isIpDom ( m_searchIps[i] ) ) {
			if ( m_searchIps[i] == ipd ) return true;
			continue;
		}
		// otherwise it's just a single ip
		if ( m_searchIps[i] == ip ) return true;
	}
	*/

	// otherwise no permission
	return false;
}

bool CollectionRec::rebuildUrlFilters ( ) {

	// only for diffbot custom crawls
	if ( m_isCustomCrawl != 1 && // crawl api
	     m_isCustomCrawl != 2 )  // bulk api
		return true;

	char *ucp = m_diffbotUrlCrawlPattern.getBufStart();
	if ( ucp && ! ucp[0] ) ucp = NULL;

	// if we had a regex, that works for this purpose as well
	if ( ! ucp ) ucp = m_diffbotUrlCrawlRegEx.getBufStart();
	if ( ucp && ! ucp[0] ) ucp = NULL;



	char *upp = m_diffbotUrlProcessPattern.getBufStart();
	if ( upp && ! upp[0] ) upp = NULL;

	// if we had a regex, that works for this purpose as well
	if ( ! upp ) upp = m_diffbotUrlProcessRegEx.getBufStart();
	if ( upp && ! upp[0] ) upp = NULL;


	// what diffbot url to use for processing
	char *api = m_diffbotApiUrl.getBufStart();
	if ( api && ! api[0] ) api = NULL;

	// convert from seconds to milliseconds. default is 250ms?
	long wait = (long)(m_collectiveCrawlDelay * 1000.0);
	// default to 250ms i guess. -1 means unset i think.
	if ( m_collectiveCrawlDelay < 0.0 ) wait = 250;

	// make the gigablast regex table just "default" so it does not
	// filtering, but accepts all urls. we will add code to pass the urls
	// through m_diffbotUrlCrawlPattern alternatively. if that itself
	// is empty, we will just restrict to the seed urls subdomain.
	for ( long i = 0 ; i < MAX_FILTERS ; i++ ) {
		m_regExs[i].purge();
		m_spiderPriorities[i] = 0;
		m_maxSpidersPerRule [i] = 10;
		m_spiderIpWaits     [i] = wait;
		m_spiderIpMaxSpiders[i] = 7; // keep it respectful
		m_spidersEnabled    [i] = 1;
		m_spiderFreqs       [i] =m_collectiveRespiderFrequency;
		m_spiderDiffbotApiUrl[i].purge();
		m_harvestLinks[i] = true;
	}

	long i = 0;


	// 1st default url filter
	m_regExs[i].set("ismedia && !ismanualadd");
	m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
	i++;

	// 2nd default filter
	if ( m_restrictDomain ) {
		m_regExs[i].set("!isonsamedomain && !ismanualadd");
		m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
		i++;
	}

	// 3rd rule for respidering
	if ( m_collectiveRespiderFrequency > 0.0 ) {
		m_regExs[i].set("lastspidertime>={roundstart}");
		// do not "remove" from index
		m_spiderPriorities   [i] = 10;
		// just turn off spidering. if we were to set priority to
		// filtered it would be removed from index!
		m_spidersEnabled     [i] = 0;
		i++;
	}
	// if collectiverespiderfreq is 0 or less then do not RE-spider
	// documents already indexed.
	else {
		// this does NOT work! error docs continuosly respider
		// because they are never indexed!!! like EDOCSIMPLIFIEDREDIR
		//m_regExs[i].set("isindexed");
		m_regExs[i].set("hasreply");
		m_spiderPriorities   [i] = 10;
		// just turn off spidering. if we were to set priority to
		// filtered it would be removed from index!
		m_spidersEnabled     [i] = 0;
		i++;
	}

	// and for docs that have errors respider once every 5 hours
	m_regExs[i].set("errorcount>0 && errcount<3");
	m_spiderPriorities   [i] = 40;
	m_spiderFreqs        [i] = 0.2; // half a day
	i++;

	// excessive errors? (tcp/dns timed out, etc.) retry once per month?
	m_regExs[i].set("errorcount>=3");
	m_spiderPriorities   [i] = 30;
	m_spiderFreqs        [i] = 30; // 30 days
	i++;

	// url crawl and process pattern
	if ( ucp && upp ) {
		m_regExs[i].set("matchesucp && matchesupp");
		m_spiderPriorities   [i] = 55;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// if just matches ucp, just crawl it, do not process
		m_regExs[i].set("matchesucp");
		m_spiderPriorities   [i] = 54;
		i++;
		// just process, do not spider links if does not match ucp
		m_regExs[i].set("matchesupp");
		m_spiderPriorities   [i] = 53;
		m_harvestLinks       [i] = false;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// do not crawl anything else
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
		i++;
	}

	// harvest links if we should crawl it
	if ( ucp && ! upp ) {
		m_regExs[i].set("matchesucp");
		m_spiderPriorities   [i] = 54;
		// process everything since upp is empty
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// do not crawl anything else
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = SPIDER_PRIORITY_FILTERED;
		i++;
	}

	// just process
	if ( upp && ! ucp ) {
		m_regExs[i].set("matchesupp");
		m_spiderPriorities   [i] = 53;
		//m_harvestLinks       [i] = false;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// crawl everything by default, no processing
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 50;
		i++;
	}

	// no restraints
	if ( ! upp && ! ucp ) {
		// crawl everything by default, no processing
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 50;
		m_spiderDiffbotApiUrl[i].set ( api );
		i++;
	}

	m_numRegExs   = i;
	m_numRegExs2  = i;
	m_numRegExs3  = i;
	m_numRegExs10 = i;
	m_numRegExs5  = i;
	m_numRegExs6  = i;
	m_numRegExs7  = i;
	m_numRegExs8  = i;
	m_numRegExs11 = i;

	return true;
}
