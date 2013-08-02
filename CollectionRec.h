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

#ifndef _COLLRECTIONREC_H_
#define _COLLRECTIONREC_H_

// how many url filtering patterns?
#define MAX_FILTERS    96  // up to 96 url regular expression patterns
//#define MAX_PRIORITY_QUEUES MAX_SPIDER_PRIORITIES * 2//each can be old or new
#define MAX_REGEX_LEN  256 // each regex can be up to this many bytes
// max html head length
#define MAX_HTML_LEN (4*1024)
// max chars the executable path+name can be
#define MAX_FILTER_LEN 64
// max length of a tagdb filter, typically just a domain/site
//#define MAX_TAG_FILTER_LEN 128

#define MAX_SEARCH_PASSWORDS 5
#define MAX_BANNED_IPS       400
#define MAX_SEARCH_IPS       32
#define MAX_SPAM_IPS         5
#define MAX_ADMIN_IPS        15
#define MAX_ADMIN_PASSWORDS  10
//#define MAX_SITEDB_FILTERS 256

#define MAX_AD_FEEDS         10
#define MAX_CGI_URL          1024
#define MAX_XML_LEN          256

#define SUMMARYHIGHLIGHTTAGMAXSIZE 128

// max length of a sitedb filter, typically just a domain/site
#define MAX_SITE_EXPRESSION_LEN 128
#define MAX_SITE_EXPRESSIONS    256

//#include "regex.h"

#include "Url.h"  // MAX_COLL_LEN
//#include "Sync.h"
#include "Parms.h"       // for MAX_PARMS
#include "HttpRequest.h"
#include "Collectiondb.h" // PASSWORD_MAX_LEN
//#include "Spider.h" //MAX_SPIDER_PRIORITIES
#include "HashTable.h"
#include "HashTableX.h"
#include "RdbList.h"

class CollectionRec {

 public:

	// these just set m_xml to NULL
	CollectionRec();
	virtual ~CollectionRec();

	// . set ourselves from serialized raw binary
	// . returns false and sets errno on error
	bool set ( char *data , long dataSize );

	// . set ourselves the cgi parms in an http request
	// . unspecified cgi parms will be assigned default values
	// . returns false and sets errno on error
	bool set ( HttpRequest *r , TcpSocket *s );

	// calls hasPermission() below
	bool hasPermission ( HttpRequest *r , TcpSocket *s ) ;

	// . does this user have permission for editing this collection?
	// . "p" is the password for this collection in question
	// . "ip" is the connecting ip
	bool hasPermission ( char *p, long plen , long ip ) ;

	// is this ip from a spam assassin?
	bool isAssassin ( long ip );

	// . can this ip perform a search or add url on this collection?
	// . mamma.com provides encapsulated ips of their queriers so we
	//   can ban them by ip
	bool hasSearchPermission ( TcpSocket *s , long encapIp = 0 );

	// how many bytes would this record occupy in raw binary format?
	//long getStoredSize () { return m_recSize; };

	// . serialize ourselves into the provided buffer
	// . used by Collectiondb::addRec()
	// . return # of bytes stored
	// . first 4 bytes in "buf" will also be the size of all the data
	//   which should be what is returned - 4
	//long store ( char *buf , long bufMaxSize );

	// . deserialize from a buf
	// . first 4 bytes must be the total size
	// . returns false and sets g_errno on error
	//bool set ( char *buf );

	// . store it in raw binary format
	// . returns # of bytes stored into "buf"
	// . returs -1 and sets errno on error
	//long store ( char *buf , char *bufEnd );

	// reset to default values
	void setToDefaults () ;

	// . stuff used by Collectiondb
	// . do we need a save or not?
	bool      save ();
	bool      m_needsSave;

	bool      load ( char *coll , long collNum ) ;

	void fixRec ( );

	// . when was the last time we changed?
	//long long m_lastUpdateTime;

	// the all important collection name, NULL terminated
	char  m_coll [ MAX_COLL_LEN + 1 ] ;
	long  m_collLen;

	// used by SpiderCache.cpp. g_collectiondb.m_recs[m_collnum] = this
	collnum_t m_collnum;

	// for doing DailyMerge.cpp stuff
	int32_t m_dailyMergeStarted; // time_t
	int32_t m_dailyMergeTrigger;

	char m_dailyMergeDOWList[48];

	// spider controls for this collection
	//char  m_oldSpideringEnabled     ;
	//char  m_newSpideringEnabled     ;
	char m_spideringEnabled ;
	float m_newSpiderWeight         ;
	// m_inDeleteMode is no longer used, just a place holder now
	//char  m_inDeleteMode            ;
	//char  m_restrictTitledbForQuery ; // obsoleted
	//char  m_recycleVotes            ;
	long  m_spiderDelayInMilliseconds;

	char  m_useDatedb               ;
	char  m_addUrlEnabled           ; // TODO: use at http interface lvl
	char  m_spiderLinks             ;
	char  m_sameHostLinks           ; // spider links from same host only?
	char  m_scrapingEnabledWeb      ;
	char  m_scrapingEnabledNews     ;
	char  m_scrapingEnabledBlogs    ;
	char  m_scrapingEnabledProCog   ;
	char  m_subsiteDetectionEnabled ;

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
	char  m_buildVecFromCont        ;
	long  m_maxPercentSimilarPublishDate;
	char  m_useSimilarityPublishDate;
	char  m_oneVotePerIpDom         ;
	char  m_doUrlSpamCheck          ; //filter urls w/ naughty hostnames
	long  m_deadWaitMaxAge          ;
	char  m_doLinkSpamCheck         ; //filters dynamically generated pages
	long  m_linkTextAnomalyThresh   ; //filters linktext that is unique
	//char  m_tagdbEnabled          ;
	char  m_tagdbColl [MAX_COLL_LEN+1]; // coll to use for tagdb lookups
	char  m_catdbEnabled            ;
	char  m_catdbPagesCanBeBanned   ;
	char  m_doChineseDetection      ;
	//char  m_breakWebRings           ;
	char  m_delete404s              ;
	char  m_enforceOldQuotas        ;
	char  m_exactQuotas             ;
	//char  m_sequentialTitledbLookup ; // obsoleted
	//char  m_restrictVotesToRoots    ;
	char  m_restrictIndexdbForQuery ;
	char  m_restrictIndexdbForXML   ;
	char  m_defaultRatForXML        ;
	char  m_defaultRatForHTML       ;
	//char  m_indexLinkText         ;
	//char  m_restrictIndexdbForQueryRaw ;
	char  m_restrictIndexdbForSpider;
	char  m_siteClusterByDefault    ;
	char  m_doInnerLoopSiteClustering;
	char  m_enforceNewQuotas        ;
	char  m_doIpLookups             ; // considered iff using proxy
	char  m_useRobotsTxt            ;
	char  m_doTuringTest            ; // for addurl
	char  m_applyFilterToText       ; // speeds us up
	char  m_allowHttps              ; // read HTTPS using SSL
	char  m_recycleContent          ;
	char  m_recycleCatdb            ;
	char  m_getLinkInfo             ; // turn off to save seeks
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
	long  m_topicSimilarCutoffDefault   ;
	char  m_useNewDeduping              ;
	char  m_doTierJumping               ;
	float m_numDocsMultiplier           ;
	//long  m_maxDocIdsToCompute          ;
	long  m_percentSimilarSummary       ; // Dedup by summary similiarity
	long  m_summDedupNumLines           ;
	long  m_contentLenMaxForSummary     ;

	long  m_maxQueryTerms;

	// Language stuff
	float			m_languageUnknownWeight;
	float			m_languageWeightFactor;
	char			m_enableLanguageSorting;
	char 			m_defaultSortLanguage[6];
	char 			m_languageMethodWeights[10];
	long 			m_languageBailout;
	long 			m_languageThreshold;
	long 			m_languageSamples;
	long 			m_langPageLimit;
	char			m_useLanguagePages;
	char 			m_defaultSortCountry[3];

	long  m_filterTimeout;                // kill filter pid after X secs

	// from Conf.h
	long m_posdbMinFilesToMerge ;
	long m_titledbMinFilesToMerge ;
	long m_sectiondbMinFilesToMerge ;
	//long m_indexdbMinFilesToMerge ;
	//long m_indexdbMinTotalFilesToMerge ;
	long m_spiderdbMinFilesToMerge ;
	long m_checksumdbMinFilesToMerge ;
	long m_clusterdbMinFilesToMerge ;
	long m_datedbMinFilesToMerge ;
	long m_linkdbMinFilesToMerge ;
	//long m_tagdbMinFilesToMerge ;

	//char  m_spiderdbRootUrlPriority   ; // 0-(MAX_SPIDER_PRIORITIES-1)
	//char  m_spiderdbAddUrlPriority    ;
	//char  m_newMinSpiderPriority      ; // min priority to spider
	//char  m_newMaxSpiderPriority      ; // max priority to spider
	//unsigned char  m_spiderNewBits;
	//char  m_spiderNewBits[MAX_SPIDER_PRIORITIES];
	//char  m_spiderOldBits[MAX_SPIDER_PRIORITIES];
	// bit 0 corresponds to spider priority 0, bit 1 to priority 1, etc...
	//char  m_spiderLinksByPriority[MAX_SPIDER_PRIORITIES];

	
	long m_numCols; // number of columns for results page
	long m_screenWidth; // screen width to balance columns
	long m_adWidth; // how wide the ad Column is in pixels

	char  m_dedupResultsByDefault   ;
	char  m_clusterByTopicDefault    ;
	char  m_restrictTitledbForQuery ; // move this down here
	char  m_useOldIps               ;
	char  m_banDomains              ;
	char  m_requireAllTerms         ;
	long  m_summaryMode		;
	char  m_deleteTimeouts          ; // can delete docs that time out?
	char  m_allowAsianDocs          ;
	char  m_allowAdultDocs          ;
	char  m_doSerpDetection         ;
	//char  m_trustIsNew              ; // trust spider rec's isNew bit?

	//charm_minLinkPriority           ; // don't add links under this prty
	//float m_minRespiderWait           ; // in days to re-spider a pg
	//float m_maxRespiderWait           ; // in days to re-spider a pg
	//float m_firstRespiderWait         ; // in days to wait 1st time
	//float m_errorRespiderWait         ; // in days
	//float m_docNotFoundErrorRespiderWait; // in days
	long  m_maxNumSpiders             ; // per local spider host
	float m_spiderNewPct;             ; // appx. percentage new documents

	// . in seconds
	// . shift all spiderTimes for urls in spider queue down this many secs
	//long m_spiderTimeShift;

	// start another set of flags using the old m_spiderTimeShift
	char  m_useCurrentTime          ; // ... for m_spiderTime2

	// max # of pages for this collection
	long long  m_maxNumPages;

	//double m_maxPagesPerSecond;
	float m_maxPagesPerSecond;

	long  m_maxSimilarityToIndex;

	// . only the root admin can set the % of spider time this coll. gets
	// . OBSOLETE: this has been replaced by max pages per second var!!
	long m_spiderTimePercent;

	// controls for query-dependent summary/title generation
	long m_titleMaxLen;
	long m_minTitleInLinkers;
	long m_maxTitleInLinkers;
	long m_summaryMaxLen;
	long m_summaryMaxNumLines;
	long m_summaryMaxNumCharsPerLine;
	long m_summaryDefaultNumLines;
	char m_useNewSummaries;

	char m_getDocIdScoringInfo;

	// # of times to retry url b4 nuke
	char  m_numRetries ;

	// priority of urls being retried, usually higher than normal
	char  m_retryPriority; 

	// . now the url regular expressions
	// . we chain down the regular expressions
	// . if a url matches we use that tagdb rec #
	// . if it doesn't match any of the patterns, we use the default site #
	// . just one regexp per Pattern
	// . all of these arrays should be the same size, but we need to 
	//   include a count because Parms.cpp expects a count before each
	//   array since it handle them each individually
	long      m_numRegExs  ;
	char      m_regExs           [ MAX_FILTERS ] [ MAX_REGEX_LEN+1 ];

	long      m_numRegExs2 ; // useless, just for Parms::setParm()
	float     m_spiderFreqs      [ MAX_FILTERS ];

	long      m_numRegExs3 ; // useless, just for Parms::setParm()
	char      m_spiderPriorities [ MAX_FILTERS ];

	long      m_numRegExs10 ; // useless, just for Parms::setParm()
	long      m_maxSpidersPerRule [ MAX_FILTERS ];

	// same ip waits now here instead of "page priority"
	long      m_numRegExs5 ; // useless, just for Parms::setParm()
	long      m_spiderIpWaits    [ MAX_FILTERS ];
	// same goes for max spiders per ip
	long      m_numRegExs6;
	long      m_spiderIpMaxSpiders [ MAX_FILTERS ];
	// how long to wait before respidering
	//long      m_respiderWaits      [ MAX_FILTERS ];
	//long      m_numRegExs8;
	// spidering on or off?
	long      m_numRegExs7;
	char      m_spidersEnabled     [ MAX_FILTERS ];

	// dummy?
	long      m_numRegExs9;

	//long      m_rulesets         [ MAX_FILTERS ];

	/*
	// if no reg expression matches a url use this default site rec #
	char      m_defaultRegEx [ MAX_REGEX_LEN+1 ]; // just a placeholder
	//long      m_defaultSiteFileNum;
	char      m_defaultSpiderPriority;
	float     m_defaultSpiderFrequency ;
	long long m_defaultSpiderQuota;
	*/

	//this is the current default siterec.
	//long  m_defaultSiteRec;
	//long  m_rssSiteRec;
	//long  m_tocSiteRec;

	//
	// the priority controls page parms
	//
	/*
	long  m_pq_numSpideringEnabled;
	char  m_pq_spideringEnabled        [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numTimeSlice;
	float m_pq_timeSlice               [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numSpidered;
	long  m_pq_spidered                [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numSpiderLinks;
	char  m_pq_spiderLinks             [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numSpiderSameHostnameLinks;	
	char  m_pq_spiderSameHostnameLinks [ MAX_PRIORITY_QUEUES ];
	// is this queue a "force queue". i.e. anytime a url is
	// supposed to go into it we FORCE it in even if it is
	// in another queue. then we keep a cache to make sure
	// we do not over-add the same url to that priority
	long  m_pq_numAutoForceQueue;
	char  m_pq_autoForceQueue          [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numMaxSpidersPerIp;
	long  m_pq_maxSpidersPerIp         [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numMaxSpidersPerDom;
	long  m_pq_maxSpidersPerDom        [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numMaxRespiderWait;
	float m_pq_maxRespiderWait         [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numFirstRespiderWait;
	float m_pq_firstRespiderWait       [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numSameIpWait;
	long  m_pq_sameIpWait              [ MAX_PRIORITY_QUEUES ];
	long  m_pq_numSameDomainWait;
	long  m_pq_sameDomainWait          [ MAX_PRIORITY_QUEUES ];
	*/

	char  m_summaryFrontHighlightTag[SUMMARYHIGHLIGHTTAGMAXSIZE] ;
	char  m_summaryBackHighlightTag [SUMMARYHIGHLIGHTTAGMAXSIZE] ;

	// . http header and tail for search results page for this collection
	// . allows custom html wraps around search results for your collection
	char  m_htmlHead [ MAX_HTML_LEN + 1 ];
	char  m_htmlTail [ MAX_HTML_LEN + 1 ];
	char  m_htmlRoot [ MAX_HTML_LEN + 1 ];
	long  m_htmlHeadLen;
	long  m_htmlTailLen;
	long  m_htmlRootLen;

	// . some users allowed to access this collection parameters
	// . TODO: have permission bits for various levels of access
	// . email, phone #, etc. can be in m_description
	long  m_numSearchPwds;
	char  m_searchPwds [ MAX_SEARCH_PASSWORDS ][ PASSWORD_MAX_LEN+1 ];

	long  m_numBanIps;
	long  m_banIps [ MAX_BANNED_IPS ];

	long  m_numSearchIps;
	long  m_searchIps [ MAX_SEARCH_IPS ];

	// spam assassin
	long  m_numSpamIps;
	long  m_spamIps [ MAX_SPAM_IPS ];
	
	long  m_numAdminPwds;
	char  m_adminPwds [ MAX_ADMIN_PASSWORDS ][ PASSWORD_MAX_LEN+1 ];

	long  m_numAdminIps;
	long  m_adminIps [ MAX_ADMIN_IPS ];

	// match this content-type exactly (txt/html/pdf/doc)
	char  m_filter [ MAX_FILTER_LEN + 1 ];

	// append to the turk query, something like gbcity:albuquerque, to
	// restrict what we turk on! like if we just want to turk a city
	// or something
	char  m_supplementalTurkQuery [ 512 ];

	// more control
	long m_maxSearchResultsPerQuery;
	long m_maxSearchResultsPerQueryForClients; // more for paying clients

	long  m_tierStage0;
	long  m_tierStage1;
	long  m_tierStage2;
	long  m_tierStage0Raw;
	long  m_tierStage1Raw;
	long  m_tierStage2Raw;
	long  m_tierStage0RawSite;
	long  m_tierStage1RawSite;
	long  m_tierStage2RawSite;
	long  m_compoundListMaxSize;
        //dictionary lookup controls
        char m_dictionarySite[SUMMARYHIGHLIGHTTAGMAXSIZE];

	// . related topics control
	// . this can all be overridden by passing in your own cgi parms
	//   for the query request
	long  m_numTopics;           // how many do they want by default?
	long  m_minTopicScore;
	long  m_docsToScanForTopics; // how many to scan by default?
	long  m_maxWordsPerTopic;
	long  m_minDocCount;         // min docs that must contain topic
	char  m_ipRestrict;
	long  m_dedupSamplePercent;
	char  m_topicRemoveOverlaps; // this is generally a good thing
	long  m_topicSampleSize;     // sample about 5k per document
	long  m_topicMaxPunctLen;    // keep it set to 1 for speed

	// SPELL CHECK
	char  m_spellCheck;

	// NARROW SEARCH
	char  m_doNarrowSearch;

        // Allow Links: searches on the collection
	//char  m_allowLinksSearch;
	// . reference pages parameters
	// . copied from Parms.cpp
	long  m_refs_numToGenerate;         // total # wanted by default.
	long  m_refs_numToDisplay;          // how many will be displayed?
	long  m_refs_docsToScan;            // how many to scan by default?
	long  m_refs_minQuality;            // min qual(b4 # links factored in)
	long  m_refs_minLinksPerReference;  // links required to be a reference
	long  m_refs_maxLinkers;            // max number of linkers to process
        float m_refs_additionalTRFetch;
        long  m_refs_numLinksCoefficient;
        long  m_refs_qualityCoefficient;
        long  m_refs_linkDensityCoefficient;
        char  m_refs_multiplyRefScore;
	// reference ceilings parameters
	long  m_refs_numToGenerateCeiling;   
	long  m_refs_docsToScanCeiling;
	long  m_refs_maxLinkersCeiling;
	float m_refs_additionalTRFetchCeiling;

	// . related pages parameters
	// . copied from Parms.cpp
        long  m_rp_numToGenerate;
        long  m_rp_numToDisplay;
        long  m_rp_numLinksPerDoc;
        long  m_rp_minQuality;
	long  m_rp_minScore;
        long  m_rp_minLinks;
	long  m_rp_numLinksCoeff;
	long  m_rp_avgLnkrQualCoeff;
	long  m_rp_qualCoeff;
	long  m_rp_srpLinkCoeff;
	long  m_rp_numSummaryLines;
	long  m_rp_titleTruncateLimit;
	char  m_rp_useResultsAsReferences;
	char  m_rp_getExternalPages; // from another cluster?
	char  m_rp_externalColl [MAX_COLL_LEN+1]; //coll in cluster
	// related pages ceiling parameters
	long  m_rp_numToGenerateCeiling;
	long  m_rp_numLinksPerDocCeiling;
	long  m_rp_numSummaryLinesCeiling;
	char  m_rp_doRelatedPageSumHighlight;

	char  m_familyFilter;

	char      	m_qualityAgentEnabled;
	char      	m_qualityAgentLoop;
	char            m_qualityAgentBanSubSites;
	long long 	m_qualityAgentStartDoc;
	long      	m_tagdbRefreshRate;
	long      	m_linkSamplesToGet;	//was 256
	long    	m_linkQualityDivisor;	//was 8
	long    	m_negPointsPerBannedLink;	// was 1
	long    	m_numSitesOnIpToSample;	//100
	long    	m_negPointsPerBannedSiteOnIp;	// was 1
	long    	m_siteOnIpQualityDivisor;	//was 8
	long    	m_maxPenaltyFromIp;	        //was 30
	long            m_qualityAgentBanRuleset;
	long            m_minPagesToEvaluate;

	long 		m_siteQualityBanThreshold;
	long 		m_siteQualityReindexThreshold;
	float 		m_maxSitesPerSecond;
	long            m_linkBonusDivisor;
	long            m_penaltyForLinksToDifferentSiteSameIp;

	// only spider urls due to be spidered in this time range
	long  m_spiderTimeMin;
	long  m_spiderTimeMax;

	long  m_maxSearchResults;
	long  m_maxSearchResultsForClients;


	long  m_maxAddUrlsPerIpDomPerDay;

	float m_maxKbps;

	// . max content length of text/html or text/plain document
	// . we will not download, index or store more than this many bytes
	//long  m_maxTextDocLen;
	// . max content length of other (pdf, word, xls, ppt, ps)
	// . we will not download, index or store more than this many bytes
	// . if content would be truncated, we will not even download at all
	//   because the html converter needs 100% of the doc otherwise it
	//   will have an error
	//long  m_maxOtherDocLen;

	// the proxy ip, 0 if none
	long  m_proxyIp;
	// and proxy port
	long m_proxyPort;

	// . puts <br>s in the summary to keep its width below this
	// . but we exceed this width before we would split a word
	long m_summaryMaxWidth;

	long m_proxCarveRadius;

	// . ptr to buf in Collectiondb, m_buf that contains this rec in binary
	// . we parse out the above info from this rec
	//char *m_rec;
	//char  m_oldMinSpiderPriority;
	//char  m_oldMaxSpiderPriority;

	// how many milliseconds to wait between spidering urls from same IP
	//long m_sameIpWait;
	// in milliseconds
	//long m_sameDomainWait;
	//long  m_maxSpidersPerDomain; 

	// how long a robots.txt can be in the cache (Msg13.cpp/Robotdb.cpp)
	long m_maxRobotsCacheAge;

	char m_orig [ MAX_PARMS ];

	// we no longer truncate termlists on disk, so this is obsolete
	//long m_indexdbTruncationLimit;

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
	char m_turkTags [256];

	// collection name in the other/external cluster from which we
	// fetch related pages titleRecs. (gov.gigablast.com)
	char m_getExternalRelatedPages;
	char m_externalRelatedPagesColl [ MAX_COLL_LEN + 1 ] ;
	// for importing search results from another cluster
	long  m_numResultsToImport ;
	float m_importWeight;
	long  m_numLinkerWeight;
	long  m_minLinkersPerImportedResult ;
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
	long m_maxRealTimeInlinks;

	// collection hostname
	char m_collectionHostname  [ MAX_URL_LEN ];
	char m_collectionHostname1 [ MAX_URL_LEN ];
	char m_collectionHostname2 [ MAX_URL_LEN ];

	// . cut off age in number of days old 
	// . if pub date is older than this we do not add to datedb
	//   and we remove it from datedb during a datedb merge
	long  m_datedbCutoff;
	// date parsing parms
	long  m_datedbDefaultTimezone;
	//float m_datedbDaysBeforeNow;

	// display indexed date, last modified date, datedb (published) date
	char m_displayIndexedDate;
	char m_displayLastModDate;
	char m_displayPublishDate;

        // data feed parms
        char m_useDFAcctServer; 
        long m_dfAcctIp;
        long m_dfAcctPort;
        char m_dfAcctColl[MAX_COLL_LEN];

	// ad feed parms
	long m_adFeedServerIp;
	long m_adFeedServerPort;
	char m_adFeedCgiParms[MAX_URL_LEN];
	long m_adFeedNumAds;
	long m_adFeedTimeOut;

	// use the new clusterdb?
	//char m_useClusterdb;

	// number of similar results for cluster by topic
	long m_maxClusterByTopicResults;
	long m_numExtraClusterByTopicResults;

	// RAID options
	//char m_allowRaidLookup;
	//char m_allowRaidListRead;
	//long m_maxRaidMercenaries;
	//long m_minRaidListSize;

	// . for restricting this collection to a particular language
	// . replaced by lang== lang!= in url filters
	//long m_language;

	// rss options
	char m_followRSSLinks;
	char m_onlyIndexRSS;

	// msg6 spider lock option
	//char m_useSpiderLocks;
	//char m_distributeSpiderGet;

	// percent of the water level to reload at
	//long m_reloadQueuePercent;
	// enable click 'n' scroll
	char m_clickNScrollEnabled;

	// JAB - only compile a regex one time... lazy algorithm...
	//regex_t* m_pRegExParser  [ MAX_FILTERS ];
       
        // . ad parameters
        long m_adPINumAds;
        char m_adPIEnable;
        char m_adPIFormat   [MAX_HTML_LEN + 1];
        long m_adPIFormatLen;
        long m_adSSNumAds; 
        char m_adSSEnable;
        char m_adSSFormat   [MAX_HTML_LEN + 1];
        long m_adSSFormatLen;
        char m_adSSSameasPI;
        char m_adBSSSameasBPI;
        char m_adCGI        [MAX_AD_FEEDS][MAX_CGI_URL];
        char m_adResultXml  [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adTitleXml   [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adDescXml    [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adLinkXml    [MAX_AD_FEEDS][MAX_XML_LEN];
        char m_adUrlXml     [MAX_AD_FEEDS][MAX_XML_LEN];
        
	// . do not keep in ruleset, store in title rec at time of parsing
	// . allows admin to quickly and easily change how we parse docs
	long  m_titleWeight;
	long  m_headerWeight;
	long  m_urlPathWeight;
	long  m_externalLinkTextWeight;
	long  m_internalLinkTextWeight;
	long  m_conceptWeight;
	//float m_qualityBoostBase;
	float m_siteNumInlinksBoostBase;
	

	// zero out the scores of terms in menu sections?
	//char  m_eliminateMenus;

	// post query reranking
	long  m_pqr_docsToScan; // also for # docs for language
	float m_pqr_demFactCountry; // demotion for foreign countries
	float m_pqr_demFactQTTopicsInUrl; // demotion factor fewer for query terms or gigabits in the url
	long  m_pqr_maxValQTTopicsInUrl; // max value for fewer query terms or gigabits in the url
	float m_pqr_demFactQual; // demotion factor for lower quality
	long  m_pqr_maxValQual; // max value for quality demotion
	float m_pqr_demFactPaths; // demotion factor for more paths
	long  m_pqr_maxValPaths; // max value for more paths
	float m_pqr_demFactNoCatId; // demtion factor for no catid
	float m_pqr_demFactCatidHasSupers; // demotion factor for catids with many super topics
	long  m_pqr_maxValCatidHasSupers; // max value for catids with many super topics
	float m_pqr_demFactPageSize; // demotion factor for higher page sizes
	long  m_pqr_maxValPageSize; // max value for higher page sizes
	float m_pqr_demFactLocTitle; // demotion factor for non-location specific queries with location specific results
	float m_pqr_demFactLocSummary; // demotion factor for non-location specific queries with location specific results
	float m_pqr_demFactLocDmoz; // demotion factor for non-location specific queries with location specific results
	bool  m_pqr_demInTopics; // true to demote if location is in the gigabits, otherwise these locs won't be demoted
	long  m_pqr_maxValLoc; // max value for non-location specific queries with location specific results
	float m_pqr_demFactNonHtml; // demotion factor for non-html content type
	float m_pqr_demFactXml; // demotion factor for xml content type
	float m_pqr_demFactOthFromHost; // demotion factor for no other pages from same host
	long  m_pqr_maxValOthFromHost; // max value for no other pages from same host
	float m_pqr_demFactComTopicInDmoz; // demotion factor for fewer common topics in dmoz
	float m_pqr_decFactComTopicInDmoz; // decay factor for fewer common topics in dmoz
	long  m_pqr_maxValComTopicInDmoz; // max value for fewer common topics in dmoz
	float m_pqr_demFactDmozCatNmNoQT; // demotion factor for dmoz category names that don't contain a query term
	long  m_pqr_maxValDmozCatNmNoQT; // max value for dmoz category names that don't contain a query term
	float m_pqr_demFactDmozCatNmNoGigabits; // demotion factor for dmoz category names that don't contain a gigabit
	long  m_pqr_maxValDmozCatNmNoGigabits; // max value for dmoz category names that don't contain a gigabit
	float m_pqr_demFactDatedbDate; // demotion for datedb date
	long  m_pqr_minValDatedbDate; // dates earlier than this will be demoted to the max
	long  m_pqr_maxValDatedbDate; // dates later than this will not be demoted
	float m_pqr_demFactProximity; // demotion for proximity of query terms
	long  m_pqr_maxValProximity; // max value for proximity of query terms
	float m_pqr_demFactInSection; // demotion for section of query terms
	long  m_pqr_maxValInSection; // max value for section of query terms
	float m_pqr_demFactOrigScore;

	float m_pqr_demFactSubPhrase;
	float m_pqr_demFactCommonInlinks;

        // sitedb filters
	long m_numSiteExpressions;
	char m_siteExpressions[MAX_SITE_EXPRESSIONS]
		[MAX_SITE_EXPRESSION_LEN+1];
	long m_numSiteFilters2;
	long m_siteRules[MAX_SITE_EXPRESSIONS];

        // lookup table for sitedb filter
	char      m_updateSiteRulesTable;
	HashTable m_siteRulesTable;

	// special var to prevent Collectiondb.cpp from copying the crap
	// below here
	char m_END_COPY;

	// this is basically a cache on timedb, one per collection
	HashTableX m_sortByDateTable;
	// are we currently in the midst of updating the sortbydate table?
	bool m_inProgress;
	// last time we updates m_sortByDateTable (start time of the update)
	time_t m_lastUpdateTime;
	// for poulating the sortbydate table
	class Msg5 *m_msg5;
	key128_t m_timedbStartKey;
	key128_t m_timedbEndKey;
	RdbList  m_timedbList;


	//long m_numEventsOnHost;

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
