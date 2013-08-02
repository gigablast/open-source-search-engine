// Copyright Apr 2005 Matt Wells

// . parameters from CollectionRec that can be overriden on a per query basis

// . use m_qbuf1 for the query. it has all the min/plus/quote1/quote2 advanced
//   search cgi parms appended to it. it is the complete query, which is 
//   probably what you are looking for. m_q should also be set to that, too.
//   m_query is simply the "query=" string in the url request.
// . use m_qbuf2 for the spell checker. it is missing url: link: etc fields.
// . use m_displayQuery for the query displayed in the text box. it is missing
//   values from the "sites=" and "site=" cgi parms, since they show up with
//   radio buttons below the search text box on the web page.

#include "Query.h" // MAX_QUERY_LEN
//#include "Msg24.h" // MAX_TOPIC_GROUPS

#ifndef _SEARCHINPUT_H_
#define _SEARCHINPUT_H_

//#define MAX_URLPARMS_LEN (MAX_URL_LEN + MAX_QUERY_LEN+ PASSWORD_MAX_LEN + 5000)
#define SI_TMPBUF_SIZE   (16*1024)

#define MAX_TOPIC_GROUPS 10

// . parameters used to generate a set of related topics (gigabits)
// . you can have Msg24 generate multiple sets of related topics in one call
class TopicGroup {
 public:
        long m_numTopics;
        long m_maxTopics;
        long m_docsToScanForTopics;
        long m_minTopicScore;
        long m_maxWordsPerTopic;
        char m_meta[32];
        char m_delimeter;
        bool m_useIdfForTopics;
        bool m_dedup;
        long m_minDocCount ;
        bool m_ipRestrict ;
        char m_dedupSamplePercent; // -1 means no deduping
        bool m_topicRemoveOverlaps;
        long m_topicSampleSize;
        long m_topicMaxPunctLen;
};

class SearchInput {

 public:

	bool set ( class TcpSocket *s , class HttpRequest *r , Query *q );

	void  test    ( );
	key_t makeKey ( ) ;

	// private
	void setTopicGroups  ( class HttpRequest *r , 
			       class CollectionRec *cr ) ;
	bool setQueryBuffers ( ) ;

	void setToDefaults ( class CollectionRec *cr , long niceness ) ;

	// Msg40 likes to use this to pass the parms to a remote host
	SearchInput      ( );
	~SearchInput     ( );
	void  reset                 ( );
	long  getStoredSizeForMsg40 ( ) ;
	//char *serializeForMsg40   ( long *size ) ;
	//void  deserializeForMsg40 ( char *buf, long bufSize, bool ownBuf ) ;
	void  copy                  ( class SearchInput *si ) ;

	// Language support for Msg40
	uint8_t detectQueryLanguage(void);

	bool addFacebookInterests ( char *list ) ;
	bool addInterests ( char *list , char delim, bool hasNums ) ;
	bool addInterest (char *s, long slen, char valc, bool overwrite );


	// used as indicator by SearchInput::makeKey() for generating a
	// key by hashing the parms between m_START and m_END
	long   m_START;

	// general parameters
        char   m_dedupURL;
	long   m_percentSimilarSummary;   // msg40
	char   m_showBanned;
	char   m_allowPunctInPhrase;
	char   m_excludeLinkText;
	char   m_excludeMetaText;
	char   m_doBotDetection;
	long   m_includeCachedCopy;
	char   m_familyFilter;            // msg40
	char   m_restrictIndexdbForQuery; // msg40
	char   m_doSiteClustering;        // msg40
	char   m_doDupContentRemoval;     // msg40
	char   m_getDocIdScoringInfo;

	// ranking algos
	char   m_useMinAlgo;
	char   m_useNewAlgo;
	char   m_doMaxScoreAlgo;

	char   m_adFeedEnabled;

	// intersection speed up shortcut? "&fi=1", defaults to on
	char   m_fastIntersection;

	// . related topic (gigabits) parameters
	// . TODO: prepend m_top_ to these var names
	long   m_docsToScanForTopics;     // msg40
	long   m_minTopicScore;           // msg40
	long   m_minDocCount;             // msg40
	long   m_dedupSamplePercent;      // msg40
	long   m_maxWordsPerTopic;        // msg40
	long   m_ipRestrictForTopics;     // msg40
	char   m_returnDocIdCount;        // msg40
	char   m_returnDocIds;            // msg40
	char   m_returnPops;              // msg40

	// . reference page parameters
	// . copied from CollectionRec.h
	long   m_refs_numToGenerate;          // msg40
	long   m_refs_docsToScan;             // msg40
	long   m_refs_minQuality;             // msg40
	long   m_refs_minLinksPerReference;   // msg40
	long   m_refs_maxLinkers;             // msg40
	float  m_refs_additionalTRFetch;      // msg40
	long   m_refs_numLinksCoefficient;    // msg40
	long   m_refs_qualityCoefficient;     // msg40
	long   m_refs_linkDensityCoefficient; // msg40
	char   m_refs_multiplyRefScore;       // msg40

	// . related page parameters
	// . copied from CollectionRec.h
	long   m_rp_numToGenerate;            // msg40
	long   m_rp_numLinksPerDoc;           // msg40
	long   m_rp_minQuality;               // msg40
	long   m_rp_minScore;                 // msg40
	long   m_rp_minLinks;                 // msg40
	long   m_rp_numLinksCoeff;            // msg40
	long   m_rp_avgLnkrQualCoeff;         // msg40
	long   m_rp_qualCoeff;                // msg40
	long   m_rp_srpLinkCoeff;             // msg40
	long   m_rp_numSummaryLines;          // msg40
	long   m_rp_titleTruncateLimit;       // msg40
	char   m_rp_useResultsAsReferences;   // msg40
	char   m_rp_getExternalPages;         // msg40

	char   m_getTitleRec;

	// buzz uses this
	char   m_getSitePops;

	// search result knobs
	long   m_realMaxTop;

	// general parameters
	long   m_numLinesInSummary;           // msg40
	long   m_summaryMaxWidth;             // msg40
	long   m_docsWanted;                  // msg40
	long   m_firstResultNum;              // msg40
	long   m_boolFlag;                    // msg40
	long   m_numResultsToImport;          // msg40
	float  m_importWeight;
	long   m_numLinkerWeight;
	long   m_minLinkersPerImportedResult; // msg40
	char   m_doQueryHighlighting;         // msg40
	long   m_highlightQueryLen;
	char  *m_highlightQuery;
	Query  m_hqq;
	long   m_queryMatchOffsets;
	long   m_summaryMode;

	float  m_pqr_demFactSubPhrase;
	float  m_pqr_demFactCommonInlinks;
	float  m_pqr_demFactLocTitle;
	float  m_pqr_demFactLocSummary;
	float  m_pqr_demFactLocDmoz;
	float  m_pqr_demFactProximity;
	float  m_pqr_demFactInSection;
	float  m_pqr_demFactOrigScore;
	bool   m_pqr_demInTopics;
	// . buzz stuff (buzz)
	// . these controls the set of results, so should be in the makeKey()
	//   as it is, in between the start and end hash vars
	long   m_displayInlinks;
	long   m_displayOutlinks;
	char   m_displayTermFreqs;
	char   m_justMarkClusterLevels;

	// for selecting a language
	//long   m_languageCodeLen;
	char   m_queryLang;
	long   m_gblang;

	// new sort/constrain by date stuff
	char   m_useDateLists;

	long   m_maxQueryTerms;

	// for the news collection really
	char   m_considerTitlesFromBody;
	long   m_maxClusterByTopicResults;
	long   m_numExtraClusterByTopicResults;

	// we do not do summary deduping, and other filtering with docids
	// only, so can change the result and should be part of the key
	char   m_docIdsOnly;                 // msg40
	
	// tier sizes can change with different "raw" values, therefore,
	// so can search results
	long   m_xml;                        // msg40

	// this should be part of the key because it will affect the results!
	char   m_queryExpansion;

	long   m_maxRealTimeInlinks;
	//char   m_hideAllClustered;

	// Language stuff
	int8_t			m_langHint;
	float			m_languageUnknownWeight;
	float			m_languageWeightFactor;
	char			m_enableLanguageSorting;
	uint8_t                 m_countryHint;
	char                    m_useLanguagePages;

	// . end the section we hash in SearchInput::makeKey()
	// . we also hash displayMetas, TopicGroups and Query into the key
	long   m_END_HASH;

	time_t m_nowUTC;

	char   m_fromProxy;
	char  *m_languageCode;
	char  *m_botDetectionQuery;                      

	// general parms, not part of makeKey(), but should be serialized
	long   m_useCache;                   // msg40
	long   m_rcache;                     // msg40
	long   m_wcache;                     // msg40
	long   m_niceness;                   // msg40
	long   m_compoundListMaxSize;        // msg40
	char   m_debug;                      // msg40
	char   m_debugGigabits;
	char   m_useTopTree;                 // msg40
	//char   m_restrictTitledbForQuery;    // msg40 (obsolete)
	char   m_doIpClustering;             // msg40 (obsolete)
	//double m_dpf;                        // msg40 (obsolete)

	// source IP for language sorting
	long m_queryIP;

	//long   m_END_SERIALIZE;

	long   m_spiderResults;
	long   m_spiderResultRoots;

	char   m_spellCheck;

	// do not include these in makeKey()
	long   m_numTopicsToDisplay;
	long   m_refs_numToDisplay;
	long   m_rp_numToDisplay;  

	// these should all be hashed in makeKey()
	long   m_displayMetasLen;            // msg40
	char  *m_displayMetas;               // msg40
	long   m_rp_externalCollLen;         // msg40
	char  *m_rp_externalColl;            // msg40
	long   m_importCollLen;              // msg40
	char  *m_importColl;                 // msg40

	long   m_queryCharsetLen;
	char  *m_queryCharset;

	long   m_gbcountryLen;
	char  *m_gbcountry;
	uint8_t m_country;

	// general string parms
	long   m_queryLen;                   
	char  *m_query;                      
	long   m_query2Len;                   
	char  *m_query2;                      
	long   m_collLen2;                    // msg40
	char  *m_coll2;                       // msg40

	// . "special query"
	// . list of docids to restrict results to, i.e. "124+4564+6752+..."
	// . NULL terminated
	long   m_sqLen;
	char  *m_sq;
	// exclude these docids
	long   m_noDocIdsLen;
	char  *m_noDocIds;
	// exclude these 32-bit site hashes (site ids)
	long   m_noSiteIdsLen;
	char  *m_noSiteIds;

	long   m_htmlHeadLen;
	char  *m_htmlHead;
	long   m_htmlTailLen;
	char  *m_htmlTail;
	long   m_siteLen;
	char  *m_site;
	long   m_sitesLen;
	char  *m_sites;
	long   m_plusLen;
	char  *m_plus;
	long   m_minusLen;
	char  *m_minus;
	long   m_linkLen;
	char  *m_link;
	long   m_quoteLen1;
	char  *m_quote1;
	long   m_quoteLen2;
	char  *m_quote2;
	long   m_imgUrlLen;
	char  *m_imgUrl;
	long   m_imgLinkLen;
	char  *m_imgLink;
	long   m_urlLen;
	char  *m_url;
	long   m_imgWidth;
	long   m_imgHeight;
	long   m_sitesQueryLen;

	// password is for all
	long   m_pwdLen;
	char  *m_pwd;

	// for /addurl?u=www.foo.com
	long   m_urlLen2;
	char  *m_url2;

	// for /get?d=xxxxx&strip=0&ih=1&qh=1
	long long m_docId;
	long      m_strip;
	char      m_includeHeader;
	char      m_queryHighlighting; 
	char      m_doDateHighlighting;
        long      m_useAdFeedNum;

	// reset this
	long      m_numTopicGroups;   // msg40

	//long           m_qbufLen1;         // msg40
	//long           m_qbufLen2;
	//long           m_qbufLen3;
	long           m_displayQueryLen;
	long           m_urlParmsLen;
	long           m_postParmsLen;
	// post query reranking
	long          m_docsToScanForReranking;

	// Language stuff
	long           m_defaultSortLanguageLen;
	char 	      *m_defaultSortLanguage;
	long           m_defaultSortCountryLen;
	char 	      *m_defaultSortCountry;

	char          *m_username;
	char          *m_displayQuery;     // pts into m_qbuf1
	class Hostdb  *m_hostdb;

	// urlencoded display query
	char m_qe [ MAX_QUERY_LEN *2 + 1 ];

	CollectionRec *m_cr;
	Query         *m_q;
	Query         *m_q2;

	char           m_isAdmin;
	//char           m_isAdminOverride;
	//char           m_isFriend;
	//char           m_isAssassin;

	// Saved pointer to httpRequest for lang detection
	HttpRequest * m_hr;

	// true if query is directly from an end-user
	char m_endUser;

	long           m_maxResults;       // msg40

	// a marker for SearchInput::test()
	long      m_END_TEST;

	// these are set from things above
	TopicGroup     m_topicGroups [ MAX_TOPIC_GROUPS ];// msg40
	//long           m_user;             // USER_ADMIN, ...
	//char           m_qbuf1     [ MAX_QUERY_LEN ];
	//char           m_qbuf2     [ MAX_QUERY_LEN ];
	// qbuf3 is for q2, strangly
	//char           m_qbuf3     [ MAX_QUERY_LEN ];
	SafeBuf m_sbuf1;
	SafeBuf m_sbuf2;
	SafeBuf m_sbuf3;

	// make a cookie from parms with m_flags of PF_COOKIE set
	SafeBuf m_cookieBuf;

	//char           m_urlParms  [ MAX_URLPARMS_LEN ];
	//char           m_postParms [ MAX_URLPARMS_LEN ];

	
	//long  m_turkUserLen;
	//char *m_turkUser;//[128];

	//char          *m_buf;
	//char          *m_ptr; // into m_buf
	//long           m_bufSize;
	//char           m_stack [ SI_STACK_SIZE ]; // may pt'ed to by m_buf

	// . all the parms serialized...
	// . we own this buf
	//char  m_tmpBuf [ SI_TMPBUF_SIZE ];
};

#endif
