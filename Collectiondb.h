// Matt Wells, copyright Feb 2001

// maintains a simple array of CollectionRecs

#ifndef COLLECTIONDB_H
#define COLLECTIONDB_H

// . max # of collections we're serving
// . may have to update if business gets going (or make dynamic)
// . lowered to 16 to save some mem
//#define MAX_COLL_RECS 16 // 256
//#define MAX_COLLS (MAX_COLL_RECS)

#include "SafeBuf.h"

bool addCollToTable ( char *coll , collnum_t collnum ) ;

class WaitEntry {
public:
	void (* m_callback) (void *state);
	void *m_state;
	char *m_coll;
	bool  m_purgeSeeds;
	class CollectionRec *m_cr;
	// ptr to list of parm recs for Parms.cpp
	char *m_parmPtr;
	char *m_parmEnd;
	class UdpSlot *m_slot;
	bool m_doRebuilds;
	bool m_rebuildActiveList;
	bool m_doProxyRebuild;
	bool m_updatedRound;
	collnum_t m_collnum;
	bool m_registered;
	int32_t m_errno;
	bool m_sentReply;
};

class Collectiondb  {

 public:
	Collectiondb();

	// does nothing
	void reset() ;

	// . this loads all the recs from host #0 
	// . returns false and sets errno on error
	// . each collection as a CollectionRec class for it and
	//   is loaded up from the appropriate config file
	bool init ( );

	// this loads all the recs from host #0 
	//bool load ( bool isDump = false );

	// called by main.cpp to fill in our m_recs[] array with
	// all the coll.*.*/coll.conf info
	bool loadAllCollRecs ( );

	// after main.cpp loads all rdb trees it calls this to remove
	// bogus collnums from the trees i guess
	bool cleanTrees ( ) ;

	// . this will save all conf files back to disk that need it
	// . returns false and sets g_errno on error, true on success
	bool save ( );
	bool m_needsSave;

	// returns i so that m_recs[i].m_coll = coll
	collnum_t getCollnum ( char *coll , int32_t collLen );
	collnum_t getCollnum ( char *coll ); // coll is NULL terminated here

	char *getCollName ( collnum_t collnum );
	char *getColl     ( collnum_t collnum ) {return getCollName(collnum);};

	// get coll rec specified in the HTTP request
	class CollectionRec *getRec ( class HttpRequest *r ,
				      bool useDefaultRec = true );

	// do not support diffbot style token/name style for this one:
	char *getDefaultColl ( HttpRequest *r ) ;

	//class CollectionRec *getRec2 ( class HttpRequest *r ,
	//			       bool useDefaultRec = true );
	
	// . get collectionRec from name
	// returns NULL if not available
	class CollectionRec *getRec ( char *coll );

	class CollectionRec *getRec ( char *coll , int32_t collLen );

	class CollectionRec *getRec ( collnum_t collnum);

	//class CollectionRec *getDefaultRec ( ) ;

	class CollectionRec *getFirstRec      ( ) ;
	char                *getFirstCollName ( ) ;
	collnum_t            getFirstCollnum  ( ) ;

	// . how many collections we have in here
	// . only counts valid existing collections
	int32_t getNumRecsUsed() { return m_numRecsUsed; };

	// . does this requester have root admin privledges???
	// . uses the root collection record!
	//bool isAdmin ( class HttpRequest *r , class TcpSocket *s );

	//collnum_t getNextCollnum ( collnum_t collnum );

	// what collnum will be used the next time a coll is added?
	collnum_t reserveCollNum ( ) ;

	//int64_t getLastUpdateTime () { return m_lastUpdateTime; };
	// updates m_lastUpdateTime so g_spiderCache know when to reload
	//void     updateTime         ();

	// private:

	// . these are called by handleRequest
	// . based on "action" cgi var, 1-->add,2-->delete,3-->update
	//bool addRec     ( char *coll , char *cc , int32_t cclen , bool isNew ,
	//		  collnum_t collnum , bool isDump , //  = false );
	//		  bool saveRec ); // = true


	bool addExistingColl ( char *coll, collnum_t collnum );

	bool addNewColl ( char *coll , 
			  char customCrawl ,
			  char *cpc , 
			  int32_t cpclen , 
			  bool saveIt ,
			  collnum_t newCollnum ) ;

	bool registerCollRec ( CollectionRec *cr ,  bool isNew ) ;

	bool addRdbBaseToAllRdbsForEachCollRec ( ) ;
	bool addRdbBasesForCollRec ( CollectionRec *cr ) ;

	bool growRecPtrBuf ( collnum_t collnum ) ;
	bool setRecPtr ( collnum_t collnum , CollectionRec *cr ) ;

	// returns false if blocked, true otherwise. 
	//bool deleteRec  ( char *coll , WaitEntry *we );
	bool deleteRec2 ( collnum_t collnum );//, WaitEntry *we ) ;

	//bool updateRec ( CollectionRec *newrec );
	bool deleteRecs ( class HttpRequest *r ) ;

	//void deleteSpiderColl ( class SpiderColl *sc );

	// returns false if blocked, true otherwise. 
	//bool resetColl ( char *coll , WaitEntry *we , bool purgeSeeds );
	bool resetColl2 ( collnum_t oldCollnum, 
			  collnum_t newCollnum,
			  //WaitEntry *we , 
			  bool purgeSeeds );

	// . keep up to 128 of them, these reference into m_list
	// . COllectionRec now includes m_needsSave and m_lastUpdateTime
	class CollectionRec  **m_recs;//           [ MAX_COLLS ];

	// now m_recs[] points into a safebuf that is just an array
	// of collectionrec ptrs. so we have to grow that safebuf possibly
	// in order to add a new collection rec ptr to m_recs
	SafeBuf m_recPtrBuf;

	//bool            m_needsSave      [ MAX_COLLS ];
	//int64_t       m_lastUpdateTime [ MAX_COLLS ];
	int32_t            m_numRecs;
	int32_t            m_numRecsUsed;
	
	int32_t m_wrapped;

	int32_t m_numCollsSwappedOut;

	bool m_initializing;
	//int64_t            m_lastUpdateTime;
};

extern class Collectiondb g_collectiondb;

// Matt Wells, copyright Feb 2002

// . a collection record specifies the spider/index/search parms of a 
//   collection of web pages
// . there's a Msg class to send an update signal to all the hosts once
//   we've used Msg1 to add a new rec or delete an old.  The update signal
//   will make the receiving hosts flush their CollectionRec buf so they
//   have to send out a Msg0 to get it again
// . we have a default collection record, a main collection record and
//   then other collection records
// . the default collection record values override all
// . but the collection record values can override SiteRec values
// . so if spider is disabled in default collection record, then nobody
//   can spider!
// . override the g_conf.* vars where * is in this class to use
//   Collection db's default values
// . then add in the values of the specialzed collection record
// . so change "if ( g_conf.m_spideringEnabled )" to something like
//   Msg33 msg33;
//   if ( ! msg33.getCollectionRec ( m_coll, m_collLen ) ) return false;
//   CollectionRec *r = msg33.getRec();
//   CollectoinRec *d = msg33.getDefaultRec();
//   if ( ! r->m_spideringEnabled || ! d->m_spideringEnabled ) continue;
//   ... otherwise, spider for the m_coll collection
//   ... pass msg33 to Msg14::spiderDoc(), etc...

// how many url filtering patterns?
#define MAX_FILTERS    96  // up to 96 url regular expression patterns
//#define MAX_PRIORITY_QUEUES MAX_SPIDER_PRIORITIES * 2//each can be old or new
#define MAX_REGEX_LEN  256 // each regex can be up to this many bytes
// max html head length
//#define MAX_HTML_LEN (4*1024)
// max chars the executable path+name can be
#define MAX_FILTER_LEN 64
// max length of a tagdb filter, typically just a domain/site
//#define MAX_TAG_FILTER_LEN 128

//#define MAX_SEARCH_PASSWORDS 5
//#define MAX_BANNED_IPS       400
//#define MAX_SEARCH_IPS       32
//#define MAX_SPAM_IPS         5
//#define MAX_ADMIN_IPS        15
//#define MAX_ADMIN_PASSWORDS  10
//#define MAX_SITEDB_FILTERS 256

#define MAX_AD_FEEDS         10
#define MAX_CGI_URL          1024
#define MAX_XML_LEN          256

#define SUMMARYHIGHLIGHTTAGMAXSIZE 128

// max length of a sitedb filter, typically just a domain/site
//#define MAX_SITE_EXPRESSION_LEN 128
//#define MAX_SITE_EXPRESSIONS    256

#include "regex.h"

#include "Url.h"  // MAX_COLL_LEN
//#include "Sync.h"
//#include "Parms.h"       // for MAX_PARMS
//#include "HttpRequest.h"
//#include "Collectiondb.h" // PASSWORD_MAX_LEN
//#include "Spider.h" //MAX_SPIDER_PRIORITIES
//#include "HashTable.h"
#include "HashTableX.h"
//#include "RdbList.h"
//#include "Rdb.h" // for RdbBase

// fake this for now
#define RDB_END2 80

#include "PingServer.h" // EmailInfo

// how many counts are in CrawlInfo below????
#define NUMCRAWLSTATS 8

// used by diffbot to control spidering per collection
class CrawlInfo {
 public:

	//
	// WARNING!! Add NEW stats below the LAST member variable in
	// this class so that it can still load the OLD file on disk
	// which is in the OLD format!
	//

	int64_t m_objectsDeleted;        // 1
	int64_t m_objectsAdded;          // 2
	int64_t m_urlsConsideredNOTUSED; // 3
	int64_t m_pageDownloadAttempts;  // 4
	int64_t m_pageDownloadSuccesses; // 5
	int64_t m_pageProcessAttempts;   // 6
	int64_t m_pageProcessSuccesses;  // 7
	int64_t m_urlsHarvested;         // 8


	int32_t m_lastUpdateTime;

	// this is non-zero if urls are available to be spidered right now.
	int32_t m_hasUrlsReadyToSpider;

	// last time we launched a spider. 0 on startup.
	uint32_t m_lastSpiderAttempt; // time_t
	// time we had or might have had a url available for spidering
	uint32_t m_lastSpiderCouldLaunch; // time_t

	int32_t m_collnum;

	// have we sent out email/webhook notifications crawl has no urls
	// currently in the ready queue (doledb) to spider?
	char m_sentCrawlDoneAlert;

	//int32_t m_numUrlsLaunched;
	int32_t m_dummy1;

	// keep separate because when we receive a crawlinfo struct from
	// a host we only add these in if it matches our round #
	int64_t m_pageDownloadSuccessesThisRound;
	int64_t m_pageProcessSuccessesThisRound;


	void reset() { memset ( this , 0 , sizeof(CrawlInfo) ); };
	//bool print (class SafeBuf *sb ) ;
	//bool setFromSafeBuf (class SafeBuf *sb ) ;
};


class CollectionRec {

 public:

	// active linked list of collectionrecs used by spider.cpp
	class CollectionRec *m_nextActive;

	// these just set m_xml to NULL
	CollectionRec();
	virtual ~CollectionRec();
	
	//char *getDiffbotToken ( int32_t *tokenLen );

	// . set ourselves from serialized raw binary
	// . returns false and sets errno on error
	bool set ( char *data , int32_t dataSize );

	// . set ourselves the cgi parms in an http request
	// . unspecified cgi parms will be assigned default values
	// . returns false and sets errno on error
	bool set ( class HttpRequest *r , class TcpSocket *s );

	// calls hasPermission() below
	bool hasPermission ( class HttpRequest *r , class TcpSocket *s ) ;

	// . does this user have permission for editing this collection?
	// . "p" is the password for this collection in question
	// . "ip" is the connecting ip
	bool hasPermission ( char *p, int32_t plen , int32_t ip ) ;

	// is this ip from a spam assassin?
	bool isAssassin ( int32_t ip );

	int64_t getNumDocsIndexed();

	// messes with m_spiderColl->m_sendLocalCrawlInfoToHost[MAX_HOSTS]
	// so we do not have to keep sending this huge msg!
	bool shouldSendLocalCrawlInfoToHost ( int32_t hostId );
	void sentLocalCrawlInfoToHost ( int32_t hostId );
	void localCrawlInfoUpdate();

	// . can this ip perform a search or add url on this collection?
	// . mamma.com provides encapsulated ips of their queriers so we
	//   can ban them by ip
	bool hasSearchPermission ( class TcpSocket *s , int32_t encapIp = 0 );

	// how many bytes would this record occupy in raw binary format?
	//int32_t getStoredSize () { return m_recSize; };

	// . serialize ourselves into the provided buffer
	// . used by Collectiondb::addRec()
	// . return # of bytes stored
	// . first 4 bytes in "buf" will also be the size of all the data
	//   which should be what is returned - 4
	//int32_t store ( char *buf , int32_t bufMaxSize );

	// . deserialize from a buf
	// . first 4 bytes must be the total size
	// . returns false and sets g_errno on error
	//bool set ( char *buf );

	// . store it in raw binary format
	// . returns # of bytes stored into "buf"
	// . returs -1 and sets errno on error
	//int32_t store ( char *buf , char *bufEnd );

	// reset to default values
	void setToDefaults () ;

	// . stuff used by Collectiondb
	// . do we need a save or not?
	bool      save ();
	bool      m_needsSave;

	bool      load ( char *coll , int32_t collNum ) ;
	void reset();

	//void setUrlFiltersToDefaults();

	// for customcrawls
	bool rebuildUrlFilters();

	// for regular crawls
	bool rebuildUrlFilters2();
  
	// for diffbot crawl or bulk jobs
	bool rebuildUrlFiltersDiffbot();

	// rebuild the regexes related to diffbot, such as the one for the URL pattern
	bool rebuildDiffbotRegexes();

	bool rebuildLangRules( char *lang , char *tld );

	bool rebuildShallowRules();

	bool m_urlFiltersHavePageCounts;

	// moved from SpiderColl so we can load up at startup
	//HashTableX m_pageCountTable;

	// . when was the last time we changed?
	//int64_t m_lastUpdateTime;

	// the all important collection name, NULL terminated
	char  m_coll [ MAX_COLL_LEN + 1 ] ;
	int32_t  m_collLen;

	// used by SpiderCache.cpp. g_collectiondb.m_recs[m_collnum] = this
	collnum_t m_collnum;

	// for doing DailyMerge.cpp stuff
	int32_t m_dailyMergeStarted; // time_t
	int32_t m_dailyMergeTrigger;

	class CollectionRec *m_nextLink;
	class CollectionRec *m_prevLink;

	char m_dailyMergeDOWList[48];

	int32_t m_treeCount;

	bool swapOut();
	bool m_swappedOut;

	int64_t m_spiderCorruptCount;

	// holds ips that have been detected as being throttled and we need
	// to backoff and use proxies on
	HashTableX m_twitchyTable;

	//
	// CLOUD SEARCH ENGINE SUPPORT
	//
	// ip of user adding the collection
	char m_userIp[16];

	// spider controls for this collection
	//char  m_oldSpideringEnabled     ;
	//char  m_newSpideringEnabled     ;
	char m_spideringEnabled ;
	float m_newSpiderWeight         ;
	// m_inDeleteMode is no longer used, just a place holder now
	//char  m_inDeleteMode            ;
	//char  m_restrictTitledbForQuery ; // obsoleted
	//char  m_recycleVotes            ;
	int32_t  m_spiderDelayInMilliseconds;

	// is in active list in spider.cpp?
	bool m_isActive;

	// . at what time did the spiders start?
	// . this is incremented when all urls have been spidered and
	//   the next round begins
	uint32_t m_spiderRoundStartTime; // time_t
	// this begins at 0, and increments when all the urls have been
	// spidered and begin the next round
	int32_t   m_spiderRoundNum;

	char  m_makeImageThumbnails;

	int32_t m_thumbnailMaxWidthHeight ;

	char  m_indexSpiderReplies;
	char  m_indexBody;

	//char  m_useDatedb               ;
	//char  m_addUrlEnabled           ; // TODO: use at http interface lvl
	//char  m_spiderLinks             ; use url filters now!
	char  m_sameHostLinks           ; // spider links from same host only?
	char  m_scrapingEnabledWeb      ;
	char  m_scrapingEnabledNews     ;
	char  m_scrapingEnabledBlogs    ;
	char  m_scrapingEnabledProCog   ;
	//char  m_subsiteDetectionEnabled ;

	// do not re-add outlinks to spiderdb if less than this many days
	// have elapsed since the last time we added them to spiderdb
	float m_outlinksRecycleFrequencyDays ;

	//char  m_onlySpiderRoots         ; // only spider root urls?
	//	char  m_maxNumHops              ; // hops from parent page
	char  m_dedupingEnabled         ; // dedup content on same hostname
	char  m_dupCheckWWW             ;
	char  m_detectCustomErrorPages  ;
	char  m_useSimplifiedRedirects  ;
	char  m_useIfModifiedSince      ;
	char  m_useTimeAxis             ;
	char  m_indexWarcs;
	char  m_buildVecFromCont        ;
	int32_t  m_maxPercentSimilarPublishDate;
	char  m_useSimilarityPublishDate;
	char  m_oneVotePerIpDom         ;
	char  m_doUrlSpamCheck          ; //filter urls w/ naughty hostnames
	int32_t  m_deadWaitMaxAge          ;
	char  m_doLinkSpamCheck         ; //filters dynamically generated pages
	int32_t  m_linkTextAnomalyThresh   ; //filters linktext that is unique
	//char  m_tagdbEnabled          ;
	char  m_tagdbColl [MAX_COLL_LEN+1]; // coll to use for tagdb lookups
	char  m_catdbEnabled            ;
	char  m_catdbPagesCanBeBanned   ;
	char  m_doChineseDetection      ;
	//char  m_breakWebRings           ;
	char  m_delete404s              ;
	//char  m_enforceOldQuotas        ;
	//char  m_exactQuotas             ;
	//char  m_sequentialTitledbLookup ; // obsoleted
	//char  m_restrictVotesToRoots    ;
	char  m_restrictIndexdbForQuery ;
	char  m_restrictIndexdbForXML   ;
	char  m_defaultRatForXML        ;
	char  m_defaultRatForHTML       ;
	//char  m_indexLinkText         ;
	//char  m_restrictIndexdbForQueryRaw ;
	//char  m_restrictIndexdbForSpider;
	char  m_siteClusterByDefault    ;
	char  m_doInnerLoopSiteClustering;
	char  m_enforceNewQuotas        ;
	char  m_doIpLookups             ; // considered iff using proxy
	char  m_useRobotsTxt            ;
	char  m_obeyRelNoFollowLinks    ;
	char  m_forceUseFloaters        ;
	char  m_automaticallyUseProxies ;
	char  m_automaticallyBackOff    ;
	//char  m_restrictDomain          ; // say on same domain as seeds?
	char  m_doTuringTest            ; // for addurl
	char  m_applyFilterToText       ; // speeds us up
	char  m_allowHttps              ; // read HTTPS using SSL
	char  m_recycleContent          ;
	char  m_recycleCatdb            ;
	char  m_getLinkInfo             ; // turn off to save seeks
	char  m_computeSiteNumInlinks   ;
	//char  m_recycleLinkInfo2        ; // ALWAYS recycle linkInfo2?
	//char  m_useLinkInfo2ForQuality  ;
	char  m_indexInlinkNeighborhoods;
	char  m_doRobotChecking         ;
	char  m_needDollarSign          ;
	char  m_getNewsTopic            ;
	char  m_newAlgo                 ; // use new links: termlist algo
	char  m_useGigabitVector        ;
	char  m_allowXmlDocs            ;
	char  m_removeBannedPages       ;
	//char  m_needNumbersInUrl        ;

	float m_inlinkNeighborhoodsScoreScalar;

	float m_updateVotesFreq         ; // in days. replaced m_recycleVotes
	float m_sortByDateWeight        ;

        char  m_dedupURLDefault             ;
	int32_t  m_topicSimilarCutoffDefault   ;
	char  m_useNewDeduping              ;
	char  m_doTierJumping               ;
	float m_numDocsMultiplier           ;
	//int32_t  m_maxDocIdsToCompute          ;
	int32_t  m_percentSimilarSummary       ; // Dedup by summary similiarity
	int32_t  m_summDedupNumLines           ;
	int32_t  m_contentLenMaxForSummary     ;

	int32_t  m_maxQueryTerms;

	char  m_spiderStatus;
	//char *m_spiderStatusMsg;

	float m_sameLangWeight;

	// Language stuff
	float			m_languageUnknownWeight;
	float			m_languageWeightFactor;
	char			m_enableLanguageSorting;
	char 			m_defaultSortLanguage2[6];
	char 			m_languageMethodWeights[10];
	int32_t 			m_languageBailout;
	int32_t 			m_languageThreshold;
	int32_t 			m_languageSamples;
	int32_t 			m_langPageLimit;
	char			m_useLanguagePages;
	char 			m_defaultSortCountry[3];

	int32_t  m_filterTimeout;                // kill filter pid after X secs

	// for Spider.cpp
	int32_t m_updateRoundNum;

	// IMPORT PARMS
	char    m_importEnabled;
	SafeBuf m_importDir;
	int32_t    m_numImportInjects;
	class ImportState *m_importState;

	SafeBuf m_collectionPasswords;
	SafeBuf m_collectionIps;

	// from Conf.h
	int32_t m_posdbMinFilesToMerge ;
	int32_t m_titledbMinFilesToMerge ;
	int32_t m_sectiondbMinFilesToMerge ;
	//int32_t m_indexdbMinFilesToMerge ;
	//int32_t m_indexdbMinTotalFilesToMerge ;
	//int32_t m_spiderdbMinFilesToMerge ;
	//int32_t m_checksumdbMinFilesToMerge ;
	//int32_t m_clusterdbMinFilesToMerge ;
	//int32_t m_datedbMinFilesToMerge ;
	int32_t m_linkdbMinFilesToMerge ;
	int32_t m_tagdbMinFilesToMerge ;

	//char  m_spiderdbRootUrlPriority   ; // 0-(MAX_SPIDER_PRIORITIES-1)
	//char  m_spiderdbAddUrlPriority    ;
	//char  m_newMinSpiderPriority      ; // min priority to spider
	//char  m_newMaxSpiderPriority      ; // max priority to spider
	//unsigned char  m_spiderNewBits;
	//char  m_spiderNewBits[MAX_SPIDER_PRIORITIES];
	//char  m_spiderOldBits[MAX_SPIDER_PRIORITIES];
	// bit 0 corresponds to spider priority 0, bit 1 to priority 1, etc...
	//char  m_spiderLinksByPriority[MAX_SPIDER_PRIORITIES];

	
	int32_t m_numCols; // number of columns for results page
	int32_t m_screenWidth; // screen width to balance columns
	int32_t m_adWidth; // how wide the ad Column is in pixels

	char  m_dedupResultsByDefault   ;
	char  m_doTagdbLookups        ;
	char  m_clusterByTopicDefault    ;
	char  m_restrictTitledbForQuery ; // move this down here
	char  m_useOldIps               ;
	char  m_banDomains              ;
	char  m_requireAllTerms         ;
	int32_t  m_summaryMode		;
	char  m_deleteTimeouts          ; // can delete docs that time out?
	char  m_allowAsianDocs          ;
	char  m_allowAdultDocs          ;
	char  m_doSerpDetection         ;
	char  m_useCanonicalRedirects   ;

	//char  m_trustIsNew              ; // trust spider rec's isNew bit?

	//charm_minLinkPriority           ; // don't add links under this prty
	//float m_minRespiderWait           ; // in days to re-spider a pg
	//float m_maxRespiderWait           ; // in days to re-spider a pg
	//float m_firstRespiderWait         ; // in days to wait 1st time
	//float m_errorRespiderWait         ; // in days
	//float m_docNotFoundErrorRespiderWait; // in days
	int32_t  m_maxNumSpiders             ; // per local spider host
	float m_spiderNewPct;             ; // appx. percentage new documents

	int32_t m_lastResetCount;

	// . in seconds
	// . shift all spiderTimes for urls in spider queue down this many secs
	//int32_t m_spiderTimeShift;

	// start another set of flags using the old m_spiderTimeShift
	char  m_useCurrentTime          ; // ... for m_spiderTime2

	// max # of pages for this collection
	int64_t  m_maxNumPages;

	//double m_maxPagesPerSecond;
	float m_maxPagesPerSecond;

	int32_t  m_maxSimilarityToIndex;

	// . only the root admin can set the % of spider time this coll. gets
	// . OBSOLETE: this has been replaced by max pages per second var!!
	int32_t m_spiderTimePercent;

	// controls for query-dependent summary/title generation
	int32_t m_titleMaxLen;
	int32_t m_minTitleInLinkers;
	int32_t m_maxTitleInLinkers;
	int32_t m_summaryMaxLen;
	int32_t m_summaryMaxNumLines;
	int32_t m_summaryMaxNumCharsPerLine;
	char m_useNewSummaries;

	char m_getDocIdScoringInfo;

	// # of times to retry url b4 nuke
	//char  m_numRetries ;

	// priority of urls being retried, usually higher than normal
	//char  m_retryPriority; 

  /***** 
   * !! Start Diffbot paramamters !! *
   *****/
  
  SafeBuf m_diffbotToken;
	SafeBuf m_diffbotCrawlName;
	// email for emailing when crawl limit hit
	SafeBuf m_notifyEmail;
	// fetch this url when crawl limit hit
	SafeBuf m_notifyUrl;
	// the default respider frequency for all rows in url filters
	float   m_collectiveRespiderFrequency;
	float   m_collectiveCrawlDelay;//SpiderWait;
	// an alternate name for the collection. we tend to create
	// collection names as a random sequence of hex digits. this
	// will allow a user to give them an alternate name.
	//SafeBuf m_collectionNameAlias;
	SafeBuf m_diffbotSeeds;
	// this will be NULL or "none" to not pass off to diffbot
	//SafeBuf m_diffbotApi;
	//SafeBuf m_diffbotApiList;//QueryString;
	//SafeBuf m_diffbotUrlCrawlPattern;
	//SafeBuf m_diffbotUrlProcessPattern;

	// use for all now...
	SafeBuf m_diffbotApiUrl;

	// only process pages whose content matches this pattern
	SafeBuf m_diffbotPageProcessPattern;
	// only process urls that match this pattern
	SafeBuf m_diffbotUrlProcessPattern;
	// only CRAWL urls that match this pattern
	SafeBuf m_diffbotUrlCrawlPattern;
  
	// regex support
	SafeBuf m_diffbotUrlCrawlRegEx;
	SafeBuf m_diffbotUrlProcessRegEx;
	regex_t m_ucr;
	regex_t m_upr;
	int32_t m_hasucr:1;
	int32_t m_hasupr:1;
  
	// only crawl pages within hopcount of a seed. 0 for no limit 
	int32_t m_diffbotMaxHops;

	char    m_diffbotOnlyProcessIfNewUrl;

	//SafeBuf m_diffbotClassify;
	//char m_diffbotClassify;
	//char m_useDiffbot;
	char m_isCustomCrawl;
	//char m_isDiffbotCollection;
	// format of output. "csv" or "xml" or "json" or null
	//SafeBuf m_diffbotFormat;
	// what fields to return in the json output: (api dependent)
	//SafeBuf m_diffbotFields;
	int64_t m_maxToCrawl;
	int64_t m_maxToProcess;
	int32_t      m_maxCrawlRounds;

	// in seconds now
	uint32_t m_diffbotCrawlStartTime;
	uint32_t m_diffbotCrawlEndTime;

	// for testing their regexes etc...
	//char m_isDiffbotTestCrawl;

	// our local crawling stats
	CrawlInfo m_localCrawlInfo;
	// total crawling stats summed up from all hosts in network
	CrawlInfo m_globalCrawlInfo;

	//CrawlInfo m_tmpCrawlInfo;

	// holds the latest CrawlInfo for each host for this collrec
	SafeBuf m_crawlInfoBuf;

	// last time we computed global crawl info
	//time_t m_globalCrawlInfoUpdateTime;
	//EmailInfo m_emailInfo;
	// for counting replies
	//int32_t m_replies;
	//int32_t m_requests;
	//bool m_doingCallbacks;
	// for storing callbacks waiting in line for freshest crawl info
	//SafeBuf m_callbackQueue;

  /***** 
   * !! End of Diffbot paramamters !! *
   *****/

	// list of url patterns to be indexed.
	SafeBuf m_siteListBuf;
	char m_spiderToo;

	// can be "web" "english" "romantic" "german" etc.
	SafeBuf m_urlFiltersProfile;

	// . now the url regular expressions
	// . we chain down the regular expressions
	// . if a url matches we use that tagdb rec #
	// . if it doesn't match any of the patterns, we use the default site #
	// . just one regexp per Pattern
	// . all of these arrays should be the same size, but we need to 
	//   include a count because Parms.cpp expects a count before each
	//   array since it handle them each individually
	int32_t      m_numRegExs  ;
	// make this now use g_collectiondb.m_stringBuf safebuf and
	// make Parms.cpp use that stringbuf rather than store into here...
	//char      m_regExs           [ MAX_FILTERS ] [ MAX_REGEX_LEN+1 ];
	SafeBuf   m_regExs           [ MAX_FILTERS ];

	int32_t      m_numRegExs2 ; // useless, just for Parms::setParm()
	float     m_spiderFreqs      [ MAX_FILTERS ];

	int32_t      m_numRegExs3 ; // useless, just for Parms::setParm()
	char      m_spiderPriorities [ MAX_FILTERS ];

	int32_t      m_numRegExs10 ; // useless, just for Parms::setParm()
	int32_t      m_maxSpidersPerRule [ MAX_FILTERS ];

	// same ip waits now here instead of "page priority"
	int32_t      m_numRegExs5 ; // useless, just for Parms::setParm()
	int32_t      m_spiderIpWaits    [ MAX_FILTERS ];
	// same goes for max spiders per ip
	int32_t      m_numRegExs6;
	int32_t      m_spiderIpMaxSpiders [ MAX_FILTERS ];
	// how long to wait before respidering
	//int32_t      m_respiderWaits      [ MAX_FILTERS ];
	//int32_t      m_numRegExs8;
	// spidering on or off?
	//int32_t      m_numRegExs7;
	//char      m_spidersEnabled     [ MAX_FILTERS ];

	// should urls in this queue be sent to diffbot for processing
	// when we are trying to index them?
	//int32_t      m_numRegExs11;
	//char      m_spiderDiffbotApiNum [ MAX_FILTERS ];

	//int32_t      m_numRegExs11;
	//SafeBuf   m_spiderDiffbotApiUrl [ MAX_FILTERS ];

	int32_t      m_numRegExs8;
	char      m_harvestLinks     [ MAX_FILTERS ];

	int32_t      m_numRegExs7;
	char      m_forceDelete  [ MAX_FILTERS ];

	// dummy?
	int32_t      m_numRegExs9;

	//int32_t      m_rulesets         [ MAX_FILTERS ];

	/*
	// if no reg expression matches a url use this default site rec #
	char      m_defaultRegEx [ MAX_REGEX_LEN+1 ]; // just a placeholder
	//int32_t      m_defaultSiteFileNum;
	char      m_defaultSpiderPriority;
	float     m_defaultSpiderFrequency ;
	int64_t m_defaultSpiderQuota;
	*/

	//this is the current default siterec.
	//int32_t  m_defaultSiteRec;
	//int32_t  m_rssSiteRec;
	//int32_t  m_tocSiteRec;

	//
	// the priority controls page parms
	//
	/*
	int32_t  m_pq_numSpideringEnabled;
	char  m_pq_spideringEnabled        [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numTimeSlice;
	float m_pq_timeSlice               [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numSpidered;
	int32_t  m_pq_spidered                [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numSpiderLinks;
	char  m_pq_spiderLinks             [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numSpiderSameHostnameLinks;	
	char  m_pq_spiderSameHostnameLinks [ MAX_PRIORITY_QUEUES ];
	// is this queue a "force queue". i.e. anytime a url is
	// supposed to go into it we FORCE it in even if it is
	// in another queue. then we keep a cache to make sure
	// we do not over-add the same url to that priority
	int32_t  m_pq_numAutoForceQueue;
	char  m_pq_autoForceQueue          [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numMaxSpidersPerIp;
	int32_t  m_pq_maxSpidersPerIp         [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numMaxSpidersPerDom;
	int32_t  m_pq_maxSpidersPerDom        [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numMaxRespiderWait;
	float m_pq_maxRespiderWait         [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numFirstRespiderWait;
	float m_pq_firstRespiderWait       [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numSameIpWait;
	int32_t  m_pq_sameIpWait              [ MAX_PRIORITY_QUEUES ];
	int32_t  m_pq_numSameDomainWait;
	int32_t  m_pq_sameDomainWait          [ MAX_PRIORITY_QUEUES ];
	*/

	char m_doQueryHighlighting;

	char  m_summaryFrontHighlightTag[SUMMARYHIGHLIGHTTAGMAXSIZE] ;
	char  m_summaryBackHighlightTag [SUMMARYHIGHLIGHTTAGMAXSIZE] ;

	// . http header and tail for search results page for this collection
	// . allows custom html wraps around search results for your collection
	//char  m_htmlHead [ MAX_HTML_LEN + 1 ];
	//char  m_htmlTail [ MAX_HTML_LEN + 1 ];
	//char  m_htmlRoot [ MAX_HTML_LEN + 1 ];
	//int32_t  m_htmlHeadLen;
	//int32_t  m_htmlTailLen;
	//int32_t  m_htmlRootLen;

	SafeBuf m_htmlRoot;
	SafeBuf m_htmlHead;
	SafeBuf m_htmlTail;

	// . some users allowed to access this collection parameters
	// . TODO: have permission bits for various levels of access
	// . email, phone #, etc. can be in m_description
	//int32_t  m_numSearchPwds;
	//char  m_searchPwds [ MAX_SEARCH_PASSWORDS ][ PASSWORD_MAX_LEN+1 ];

	//int32_t  m_numBanIps;
	//int32_t  m_banIps [ MAX_BANNED_IPS ];

	//int32_t  m_numSearchIps;
	//int32_t  m_searchIps [ MAX_SEARCH_IPS ];

	// spam assassin
	//int32_t  m_numSpamIps;
	//int32_t  m_spamIps [ MAX_SPAM_IPS ];
	
	//int32_t  m_numAdminPwds;
	//char  m_adminPwds [ MAX_ADMIN_PASSWORDS ][ PASSWORD_MAX_LEN+1 ];

	//int32_t  m_numAdminIps;
	//int32_t  m_adminIps [ MAX_ADMIN_IPS ];

	// match this content-type exactly (txt/html/pdf/doc)
	char  m_filter [ MAX_FILTER_LEN + 1 ];

	// append to the turk query, something like gbcity:albuquerque, to
	// restrict what we turk on! like if we just want to turk a city
	// or something
	//char  m_supplementalTurkQuery [ 512 ];

	// more control
	int32_t m_maxSearchResultsPerQuery;
	int32_t m_maxSearchResultsPerQueryForClients; // more for paying clients
	/*
	int32_t  m_tierStage0;
	int32_t  m_tierStage1;
	int32_t  m_tierStage2;
	int32_t  m_tierStage0Raw;
	int32_t  m_tierStage1Raw;
	int32_t  m_tierStage2Raw;
	int32_t  m_tierStage0RawSite;
	int32_t  m_tierStage1RawSite;
	int32_t  m_tierStage2RawSite;
	*/
	int32_t  m_compoundListMaxSize;
        //dictionary lookup controls
        //char m_dictionarySite[SUMMARYHIGHLIGHTTAGMAXSIZE];

	// . related topics control
	// . this can all be overridden by passing in your own cgi parms
	//   for the query request
	int32_t  m_numTopics;           // how many do they want by default?
	int32_t  m_minTopicScore;
	int32_t  m_docsToScanForTopics; // how many to scan by default?
	int32_t  m_maxWordsPerTopic;
	int32_t  m_minDocCount;         // min docs that must contain topic
	char  m_ipRestrict;
	int32_t  m_dedupSamplePercent;
	char  m_topicRemoveOverlaps; // this is generally a good thing
	int32_t  m_topicSampleSize;     // sample about 5k per document
	int32_t  m_topicMaxPunctLen;    // keep it set to 1 for speed

	// SPELL CHECK
	char  m_spellCheck;

	// NARROW SEARCH
	char  m_doNarrowSearch;

	char m_sendingAlertInProgress;

        // Allow Links: searches on the collection
	//char  m_allowLinksSearch;
	// . reference pages parameters
	// . copied from Parms.cpp
	int32_t  m_refs_numToGenerate;         // total # wanted by default.
	int32_t  m_refs_numToDisplay;          // how many will be displayed?
	int32_t  m_refs_docsToScan;            // how many to scan by default?
	int32_t  m_refs_minQuality;            // min qual(b4 # links factored in)
	int32_t  m_refs_minLinksPerReference;  // links required to be a reference
	int32_t  m_refs_maxLinkers;            // max number of linkers to process
        float m_refs_additionalTRFetch;
        int32_t  m_refs_numLinksCoefficient;
        int32_t  m_refs_qualityCoefficient;
        int32_t  m_refs_linkDensityCoefficient;
        char  m_refs_multiplyRefScore;
	// reference ceilings parameters
	int32_t  m_refs_numToGenerateCeiling;   
	int32_t  m_refs_docsToScanCeiling;
	int32_t  m_refs_maxLinkersCeiling;
	float m_refs_additionalTRFetchCeiling;

	class SpiderColl *m_spiderColl;

	int32_t m_overflow;
	int32_t m_overflow2;

	HashTableX m_seedHashTable;

	// . related pages parameters
	// . copied from Parms.cpp
        int32_t  m_rp_numToGenerate;
        int32_t  m_rp_numToDisplay;
        int32_t  m_rp_numLinksPerDoc;
        int32_t  m_rp_minQuality;
	int32_t  m_rp_minScore;
        int32_t  m_rp_minLinks;
	int32_t  m_rp_numLinksCoeff;
	int32_t  m_rp_avgLnkrQualCoeff;
	int32_t  m_rp_qualCoeff;
	int32_t  m_rp_srpLinkCoeff;
	int32_t  m_rp_numSummaryLines;
	int32_t  m_rp_titleTruncateLimit;
	char  m_rp_useResultsAsReferences;
	char  m_rp_getExternalPages; // from another cluster?
	char  m_rp_externalColl [MAX_COLL_LEN+1]; //coll in cluster
	// related pages ceiling parameters
	int32_t  m_rp_numToGenerateCeiling;
	int32_t  m_rp_numLinksPerDocCeiling;
	int32_t  m_rp_numSummaryLinesCeiling;
	char  m_rp_doRelatedPageSumHighlight;

	char  m_familyFilter;

	char      	m_qualityAgentEnabled;
	char      	m_qualityAgentLoop;
	char            m_qualityAgentBanSubSites;
	int64_t 	m_qualityAgentStartDoc;
	int32_t      	m_tagdbRefreshRate;
	int32_t      	m_linkSamplesToGet;	//was 256
	int32_t    	m_linkQualityDivisor;	//was 8
	int32_t    	m_negPointsPerBannedLink;	// was 1
	int32_t    	m_numSitesOnIpToSample;	//100
	int32_t    	m_negPointsPerBannedSiteOnIp;	// was 1
	int32_t    	m_siteOnIpQualityDivisor;	//was 8
	int32_t    	m_maxPenaltyFromIp;	        //was 30
	int32_t            m_qualityAgentBanRuleset;
	int32_t            m_minPagesToEvaluate;

	int32_t 		m_siteQualityBanThreshold;
	int32_t 		m_siteQualityReindexThreshold;
	float 		m_maxSitesPerSecond;
	int32_t            m_linkBonusDivisor;
	int32_t            m_penaltyForLinksToDifferentSiteSameIp;

	// only spider urls due to be spidered in this time range
	int32_t  m_spiderTimeMin;
	int32_t  m_spiderTimeMax;

	int32_t  m_maxSearchResults;
	int32_t  m_maxSearchResultsForClients;


	int32_t  m_maxAddUrlsPerIpDomPerDay;

	float m_maxKbps;

	// . max content length of text/html or text/plain document
	// . we will not download, index or store more than this many bytes
	int32_t  m_maxTextDocLen;
	// . max content length of other (pdf, word, xls, ppt, ps)
	// . we will not download, index or store more than this many bytes
	// . if content would be truncated, we will not even download at all
	//   because the html converter needs 100% of the doc otherwise it
	//   will have an error
	int32_t  m_maxOtherDocLen;

	// the proxy ip, 0 if none
	//int32_t  m_proxyIp;
	// and proxy port
	//int32_t m_proxyPort;

	// . puts <br>s in the summary to keep its width below this
	// . but we exceed this width before we would split a word
	int32_t m_summaryMaxWidth;

	int32_t m_proxCarveRadius;

	// . ptr to buf in Collectiondb, m_buf that contains this rec in binary
	// . we parse out the above info from this rec
	//char *m_rec;
	//char  m_oldMinSpiderPriority;
	//char  m_oldMaxSpiderPriority;

	// how many milliseconds to wait between spidering urls from same IP
	//int32_t m_sameIpWait;
	// in milliseconds
	//int32_t m_sameDomainWait;
	//int32_t  m_maxSpidersPerDomain; 

	// how long a robots.txt can be in the cache (Msg13.cpp/Robotdb.cpp)
	int32_t m_maxRobotsCacheAge;

	//char m_orig [ MAX_PARMS ];

	// we no longer truncate termlists on disk, so this is obsolete
	//int32_t m_indexdbTruncationLimit;

	// collection name in the other/external cluster from which we
	// fetch link information. (gov.gigablast.com)
	char m_getExternalLinkInfo;
	// use hosts2.conf (otherwise uses hosts.conf for import)
	char m_importFromHosts2Conf;
	// should we ask the external collection to RECOMPUTE the link info
	// before giving it to us. we are using this to incorporate new info
	// not yet fully soaked through into gk, for slingshot.
	//char m_getExternalLinkInfoFresh;
	char m_externalColl [ MAX_COLL_LEN + 1 ] ;

	// turk tags 
	// for asking question on that tag
	// comma seperated list of tags, no space allowed
	//char m_turkTags [256];

	// collection name in the other/external cluster from which we
	// fetch related pages titleRecs. (gov.gigablast.com)
	char m_getExternalRelatedPages;
	char m_externalRelatedPagesColl [ MAX_COLL_LEN + 1 ] ;
	// for importing search results from another cluster
	int32_t  m_numResultsToImport ;
	float m_importWeight;
	int32_t  m_numLinkerWeight;
	int32_t  m_minLinkersPerImportedResult ;
	char  m_importColl [ MAX_COLL_LEN + 1 ];

	char m_classificationMode;

	// for news collection. uses changes in Msg20.cpp.
	char m_onlyUseLinkTextForTitle;

	// dmoz title and summary display options
	char m_useDmozForUntitled;
	char m_showDmozSummary;
	char m_overrideSpiderErrorsForCatdb;
	char m_showAdultCategoryOnTop;
	char m_displayDmozCategories;
	char m_displayIndirectDmozCategories;
	char m_displaySearchCategoryLink;

	// show <docsInColl>, <tag>s, etc. in PageResults::printXml()
	//char m_showSensitiveStuff;

	// enable the page turk?
	//char m_pageTurkEnabled;

	// use query expansion for this collection?
	char m_queryExpansion;

	// usedful for the news collection where <title>s aren't that good
	char m_considerTitlesFromBody;

	// read from cache
	char m_rcache;

	char m_hideAllClustered;
	int32_t m_maxRealTimeInlinks;

	// collection hostname
	//char m_collectionHostname  [ MAX_URL_LEN ];
	//char m_collectionHostname1 [ MAX_URL_LEN ];
	//char m_collectionHostname2 [ MAX_URL_LEN ];

	// . cut off age in number of days old 
	// . if pub date is older than this we do not add to datedb
	//   and we remove it from datedb during a datedb merge
	int32_t  m_datedbCutoff;
	// date parsing parms
	int32_t  m_datedbDefaultTimezone;
	//float m_datedbDaysBeforeNow;

	// display indexed date, last modified date, datedb (published) date
	char m_displayIndexedDate;
	char m_displayLastModDate;
	char m_displayPublishDate;

        // data feed parms
        char m_useDFAcctServer; 
        int32_t m_dfAcctIp;
        int32_t m_dfAcctPort;
        //char m_dfAcctColl[MAX_COLL_LEN];

	// ad feed parms
	int32_t m_adFeedServerIp;
	int32_t m_adFeedServerPort;
	//char m_adFeedCgiParms[MAX_URL_LEN];
	int32_t m_adFeedNumAds;
	int32_t m_adFeedTimeOut;

	// use the new clusterdb?
	//char m_useClusterdb;

	// number of similar results for cluster by topic
	int32_t m_maxClusterByTopicResults;
	int32_t m_numExtraClusterByTopicResults;

	// RAID options
	//char m_allowRaidLookup;
	//char m_allowRaidListRead;
	//int32_t m_maxRaidMercenaries;
	//int32_t m_minRaidListSize;

	// . for restricting this collection to a particular language
	// . replaced by lang== lang!= in url filters
	//int32_t m_language;

	// rss options
	char m_followRSSLinks;
	char m_onlyIndexRSS;

	// msg6 spider lock option
	//char m_useSpiderLocks;
	//char m_distributeSpiderGet;

	// percent of the water level to reload at
	//int32_t m_reloadQueuePercent;
	// enable click 'n' scroll
	char m_clickNScrollEnabled;

	// JAB - only compile a regex one time... lazy algorithm...
	//regex_t* m_pRegExParser  [ MAX_FILTERS ];
       
        // . ad parameters
	/*
        int32_t m_adPINumAds;
        char m_adPIEnable;
        char m_adPIFormat   [MAX_HTML_LEN + 1];
        int32_t m_adPIFormatLen;
        int32_t m_adSSNumAds; 
        char m_adSSEnable;
        char m_adSSFormat   [MAX_HTML_LEN + 1];
        int32_t m_adSSFormatLen;
        char m_adSSSameasPI;
        char m_adBSSSameasBPI;
        char m_adCGI        [MAX_AD_FEEDS][MAX_CGI_URL];
        char m_adResultXml  [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adTitleXml   [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adDescXml    [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adLinkXml    [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adUrlXml     [MAX_AD_FEEDS][MAX_XML_LEN];
        */

	// . do not keep in ruleset, store in title rec at time of parsing
	// . allows admin to quickly and easily change how we parse docs
	int32_t  m_titleWeight;
	int32_t  m_headerWeight;
	int32_t  m_urlPathWeight;
	int32_t  m_externalLinkTextWeight;
	int32_t  m_internalLinkTextWeight;
	int32_t  m_conceptWeight;
	//float m_qualityBoostBase;
	float m_siteNumInlinksBoostBase;
	

	// zero out the scores of terms in menu sections?
	//char  m_eliminateMenus;

	// post query reranking
	int32_t  m_pqr_docsToScan; // also for # docs for language
	float m_pqr_demFactCountry; // demotion for foreign countries
	float m_pqr_demFactQTTopicsInUrl; // demotion factor fewer for query terms or gigabits in the url
	int32_t  m_pqr_maxValQTTopicsInUrl; // max value for fewer query terms or gigabits in the url
	float m_pqr_demFactQual; // demotion factor for lower quality
	int32_t  m_pqr_maxValQual; // max value for quality demotion
	float m_pqr_demFactPaths; // demotion factor for more paths
	int32_t  m_pqr_maxValPaths; // max value for more paths
	float m_pqr_demFactNoCatId; // demtion factor for no catid
	float m_pqr_demFactCatidHasSupers; // demotion factor for catids with many super topics
	int32_t  m_pqr_maxValCatidHasSupers; // max value for catids with many super topics
	float m_pqr_demFactPageSize; // demotion factor for higher page sizes
	int32_t  m_pqr_maxValPageSize; // max value for higher page sizes
	float m_pqr_demFactLocTitle; // demotion factor for non-location specific queries with location specific results
	float m_pqr_demFactLocSummary; // demotion factor for non-location specific queries with location specific results
	float m_pqr_demFactLocDmoz; // demotion factor for non-location specific queries with location specific results
	bool  m_pqr_demInTopics; // true to demote if location is in the gigabits, otherwise these locs won't be demoted
	int32_t  m_pqr_maxValLoc; // max value for non-location specific queries with location specific results
	float m_pqr_demFactNonHtml; // demotion factor for non-html content type
	float m_pqr_demFactXml; // demotion factor for xml content type
	float m_pqr_demFactOthFromHost; // demotion factor for no other pages from same host
	int32_t  m_pqr_maxValOthFromHost; // max value for no other pages from same host
	float m_pqr_demFactComTopicInDmoz; // demotion factor for fewer common topics in dmoz
	float m_pqr_decFactComTopicInDmoz; // decay factor for fewer common topics in dmoz
	int32_t  m_pqr_maxValComTopicInDmoz; // max value for fewer common topics in dmoz
	float m_pqr_demFactDmozCatNmNoQT; // demotion factor for dmoz category names that don't contain a query term
	int32_t  m_pqr_maxValDmozCatNmNoQT; // max value for dmoz category names that don't contain a query term
	float m_pqr_demFactDmozCatNmNoGigabits; // demotion factor for dmoz category names that don't contain a gigabit
	int32_t  m_pqr_maxValDmozCatNmNoGigabits; // max value for dmoz category names that don't contain a gigabit
	float m_pqr_demFactDatedbDate; // demotion for datedb date
	int32_t  m_pqr_minValDatedbDate; // dates earlier than this will be demoted to the max
	int32_t  m_pqr_maxValDatedbDate; // dates later than this will not be demoted
	float m_pqr_demFactProximity; // demotion for proximity of query terms
	int32_t  m_pqr_maxValProximity; // max value for proximity of query terms
	float m_pqr_demFactInSection; // demotion for section of query terms
	int32_t  m_pqr_maxValInSection; // max value for section of query terms
	float m_pqr_demFactOrigScore;

	float m_pqr_demFactSubPhrase;
	float m_pqr_demFactCommonInlinks;

        // sitedb filters
	/*
	int32_t m_numSiteExpressions;
	char m_siteExpressions[MAX_SITE_EXPRESSIONS]
		[MAX_SITE_EXPRESSION_LEN+1];
	int32_t m_numSiteFilters2;
	int32_t m_siteRules[MAX_SITE_EXPRESSIONS];
	*/

        // lookup table for sitedb filter
	char      m_updateSiteRulesTable;
	//HashTable m_siteRulesTable;

	// special var to prevent Collectiondb.cpp from copying the crap
	// below here
	char m_END_COPY;


	// use this not m_bases to get the RdbBase
	class RdbBase *getBase ( char rdbId );

	// Rdb.cpp uses this after deleting an RdbBase and adding new one
	void           setBasePtr ( char rdbId , class RdbBase *base ) ;
	class RdbBase *getBasePtr ( char rdbId ) ;

 private:
	// . now chuck this into CollectionRec instead of having a fixed
	//   array of them in Rdb.h called m_bases[]
	// . leave this out of any copy of course
	class RdbBase *m_bases[RDB_END2];

 public:

	// this is basically a cache on timedb, one per collection
	HashTableX m_sortByDateTable;
	// are we currently in the midst of updating the sortbydate table?
	bool m_inProgress;
	// last time we updates m_sortByDateTable (start time of the update)
	uint32_t m_lastUpdateTime; // time_t
	// for poulating the sortbydate table
	class Msg5 *m_msg5;
	key128_t m_timedbStartKey;
	key128_t m_timedbEndKey;
	//RdbList  m_timedbList;

	// used by Parms.cpp
	char m_hackFlag;

	// each Rdb has a tree, so keep the pos/neg key count here so
	// that RdbTree does not have to have its own array limited by
	// MAX_COLLS which we did away with because we made this dynamic.
	int32_t m_numPosKeysInTree[RDB_END2];
	int32_t m_numNegKeysInTree[RDB_END2];

	//int32_t m_numEventsOnHost;

	// do we have the doc:quality var in any url filter?
	//bool      m_hasDocQualityFilter;
	// do we have "isindexed" in any url filter?
	//bool      m_hasIsIndexedKeyword;

	// this means someone re-submitted some new filters or changes, so
	// we need to update "m_hasDocQualityFilter"
	//void updateFilters();

        // company name to use for cached page messages
	//char m_cachedPageName[MAX_NAME_LEN];
};

#endif
