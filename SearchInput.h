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

char getFormatFromRequest ( class HttpRequest *hr ) ;

// . parameters used to generate a set of related topics (gigabits)
// . you can have Msg24 generate multiple sets of related topics in one call
class TopicGroup {
 public:
        int32_t m_numTopics;
        int32_t m_maxTopics;
        int32_t m_docsToScanForTopics;
        int32_t m_minTopicScore;
        int32_t m_maxWordsPerTopic;
        char m_meta[32];
        char m_delimeter;
        bool m_useIdfForTopics;
        bool m_dedup;
        int32_t m_minDocCount ;
        bool m_ipRestrict ;
        char m_dedupSamplePercent; // -1 means no deduping
        bool m_topicRemoveOverlaps;
        int32_t m_topicSampleSize;
        int32_t m_topicMaxPunctLen;
};

class SearchInput {

 public:

	// why provide query here, it is in "hr"
	bool set ( class TcpSocket *s , class HttpRequest *hr );

	void  test    ( );
	key_t makeKey ( ) ;

	// private
	void setTopicGroups  ( class HttpRequest *r , 
			       class CollectionRec *cr ) ;
	bool setQueryBuffers ( class HttpRequest *hr ) ;

	//void setToDefaults ( class CollectionRec *cr , int32_t niceness ) ;
	void clear ( int32_t niceness ) ;

	// Msg40 likes to use this to pass the parms to a remote host
	SearchInput      ( );
	~SearchInput     ( );
	void  reset                 ( );
	int32_t  getStoredSizeForMsg40 ( ) ;
	//char *serializeForMsg40   ( int32_t *size ) ;
	//void  deserializeForMsg40 ( char *buf, int32_t bufSize, bool ownBuf ) ;
	void  copy                  ( class SearchInput *si ) ;

	// Language support for Msg40
	uint8_t detectQueryLanguage(void);

	//bool addFacebookInterests ( char *list ) ;
	//bool addInterests ( char *list , char delim, bool hasNums ) ;
	//bool addInterest (char *s, int32_t slen, char valc, bool overwrite );

	///////////
	//
	// BEGIN COMPUTED THINGS
	//
	///////////

	// we basically steal the original HttpRequest buffer and keep
	// here since the original one is on the stack
	HttpRequest  m_hr;

	TcpSocket   *m_sock;

	int32_t   m_niceness;                   // msg40

	// array of collnum_t's to search... usually just one
	SafeBuf m_collnumBuf;
	// first collection # listed in m_collnumBuf
	collnum_t m_firstCollnum;

	// reset this
	int32_t      m_numTopicGroups;   // msg40

	char          *m_displayQuery;     // pts into m_qbuf1
	//class Hostdb  *m_hostdb;

	// urlencoded display query
	//char m_qe [ MAX_QUERY_LEN *2 + 1 ];

	// urlencoded display query. everything is compiled into this.
	SafeBuf m_qe;

	CollectionRec *m_cr;

	// the final compiled query goes here
	Query          m_q;

	Query         *m_q2;

	char           m_isMasterAdmin;
	char           m_isCollAdmin;

	// these are set from things above
	TopicGroup     m_topicGroups [ MAX_TOPIC_GROUPS ];// msg40
	SafeBuf m_sbuf1;
	SafeBuf m_sbuf2;
	SafeBuf m_sbuf3;

	int32_t m_catId;
	bool m_isRTL;

	// make a cookie from parms with m_flags of PF_COOKIE set
	SafeBuf m_cookieBuf;

	// we convert m_defaultSortLang to this number, like langEnglish
	// or langFrench or langUnknown...
	int32_t m_queryLangId;

	// can be 1 for FORMAT_HTML, 2 = FORMAT_XML, 3=FORMAT_JSON, 4=csv
	int32_t m_format;

	// used as indicator by SearchInput::makeKey() for generating a
	// key by hashing the parms between m_START and m_END
	int32_t   m_START;


	//////
	//
	// BEGIN USER PARMS set from HttpRequest, m_hr
	//
	//////

	char *m_coll;
	char *m_query;
	
	char *m_prepend;

	// ip address of searcher used for banning abusive IPs "uip"
	char *m_userIpStr;

	char  m_showImages;

	// general parms, not part of makeKey(), but should be serialized
	char   m_useCache;                   // msg40
	char   m_rcache;                     // msg40
	char   m_wcache;                     // msg40

	char   m_debug;                      // msg40
	char   m_debugGigabits;

	char   m_spiderResults;
	char   m_spiderResultRoots;

	char   m_spellCheck;

	char  *m_displayMetas;               // msg40


	// do not include these in makeKey()
	int32_t   m_numTopicsToDisplay;
	int32_t   m_refs_numToDisplay;
	int32_t   m_rp_numToDisplay;  

	// these should all be hashed in makeKey()
	//char  *m_rp_externalColl;            // msg40
	//char  *m_importColl;                 // msg40

	char  *m_queryCharset;

	char  *m_gbcountry;
	uint8_t m_country;

	//char  *m_query2;                      

	// advanced query parms
	char  *m_url; // for url: search
	char  *m_sites;
	char  *m_plus;
	char  *m_minus;
	char  *m_link;
	char  *m_quote1;
	char  *m_quote2;

	// co-branding parms
	char  *m_imgUrl;
	char  *m_imgLink;
	int32_t   m_imgWidth;
	int32_t   m_imgHeight;

	int32_t   m_titleMaxLen;

	// for limiting results by score in the widget
	double    m_maxSerpScore;
	int64_t m_minSerpDocId;

	float m_sameLangWeight;

	// prefer what lang in the results. it gets a 20x boost. "en" "xx" "fr"
	char 	      *m_defaultSortLang;
	// prefer what country in the results. currently unused. support later.
	char 	      *m_defaultSortCountry;

	// general parameters
        char   m_dedupURL;
	int32_t   m_percentSimilarSummary;   // msg40
	char   m_showBanned;
	char   m_allowPunctInPhrase;
	char   m_excludeLinkText;
	char   m_excludeMetaText;
	char   m_doBotDetection;
	int32_t   m_includeCachedCopy;
	char   m_getSectionVotingInfo;
	char   m_familyFilter;            // msg40
	//char   m_restrictIndexdbForQuery; // msg40
	char   m_showErrors;
	char   m_doSiteClustering;        // msg40
	char   m_doDupContentRemoval;     // msg40
	char   m_getDocIdScoringInfo;

	char   m_hideAllClustered;

	// ranking algos
	char   m_useMinAlgo;
	char   m_useNewAlgo;
	char   m_doMaxScoreAlgo;

	char   m_adFeedEnabled;

	// intersection speed up int16_tcut? "&fi=1", defaults to on
	char   m_fastIntersection;

	// stream results back on socket in streaming mode, usefule when 
	// thousands of results are requested
	char   m_streamResults;

	// limit search results to pages spidered this many seconds ago
	int32_t   m_secsBack;

	// 0 relevance, 1 date, 2 reverse date
	char   m_sortBy;

	char *m_filetype;

	// . related topic (gigabits) parameters
	// . TODO: prepend m_top_ to these var names
	int32_t   m_docsToScanForTopics;     // msg40
	int32_t   m_minTopicScore;           // msg40
	int32_t   m_minDocCount;             // msg40
	int32_t   m_dedupSamplePercent;      // msg40
	int32_t   m_maxWordsPerTopic;        // msg40
	int32_t   m_ipRestrictForTopics;     // msg40
	char   m_returnDocIdCount;        // msg40
	char   m_returnDocIds;            // msg40
	char   m_returnPops;              // msg40

	// . reference page parameters
	// . copied from CollectionRec.h
	int32_t   m_refs_numToGenerate;          // msg40
	int32_t   m_refs_docsToScan;             // msg40
	int32_t   m_refs_minQuality;             // msg40
	int32_t   m_refs_minLinksPerReference;   // msg40
	int32_t   m_refs_maxLinkers;             // msg40
	float  m_refs_additionalTRFetch;      // msg40
	int32_t   m_refs_numLinksCoefficient;    // msg40
	int32_t   m_refs_qualityCoefficient;     // msg40
	int32_t   m_refs_linkDensityCoefficient; // msg40
	char   m_refs_multiplyRefScore;       // msg40

	// . related page parameters
	// . copied from CollectionRec.h
	int32_t   m_rp_numToGenerate;            // msg40
	int32_t   m_rp_numLinksPerDoc;           // msg40
	int32_t   m_rp_minQuality;               // msg40
	int32_t   m_rp_minScore;                 // msg40
	int32_t   m_rp_minLinks;                 // msg40
	int32_t   m_rp_numLinksCoeff;            // msg40
	int32_t   m_rp_avgLnkrQualCoeff;         // msg40
	int32_t   m_rp_qualCoeff;                // msg40
	int32_t   m_rp_srpLinkCoeff;             // msg40
	int32_t   m_rp_numSummaryLines;          // msg40
	int32_t   m_rp_titleTruncateLimit;       // msg40
	char   m_rp_useResultsAsReferences;   // msg40
	char   m_rp_getExternalPages;         // msg40

	// unused pqr stuff
	int8_t			m_langHint;
	float			m_languageUnknownWeight;
	float			m_languageWeightFactor;
	char			m_enableLanguageSorting;
	uint8_t                 m_countryHint;
	char                    m_useLanguagePages;


	//char   m_getTitleRec;
	// buzz uses this
	//char   m_getSitePops;

	// search result knobs
	int32_t   m_realMaxTop;

	// general parameters
	int32_t   m_numLinesInSummary;           // msg40
	int32_t   m_summaryMaxWidth;             // msg40
	int32_t   m_summaryMaxNumCharsPerLine;

	int32_t   m_docsWanted;                  // msg40
	int32_t   m_firstResultNum;              // msg40
	int32_t   m_boolFlag;                    // msg40
	int32_t   m_numResultsToImport;          // msg40
	float  m_importWeight;
	int32_t   m_numLinkerWeight;
	int32_t   m_minLinkersPerImportedResult; // msg40
	char   m_doQueryHighlighting;         // msg40
	char  *m_highlightQuery;
	Query  m_hqq;
	int32_t   m_queryMatchOffsets;
	int32_t   m_summaryMode;

	int32_t m_maxFacets;

	// are we doing a QA query for quality assurance consistency
	char   m_qa;

	int32_t   m_docsToScanForReranking;
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
	int32_t   m_displayInlinks;
	int32_t   m_displayOutlinks;
	char   m_displayTermFreqs;
	char   m_justMarkClusterLevels;

	//char   m_queryPrepend[41];

	// new sort/constrain by date stuff
	char   m_useDateLists;

	int32_t   m_maxQueryTerms;

	// for the news collection really
	char   m_considerTitlesFromBody;
	int32_t   m_maxClusterByTopicResults;
	int32_t   m_numExtraClusterByTopicResults;

	// we do not do summary deduping, and other filtering with docids
	// only, so can change the result and should be part of the key
	char   m_docIdsOnly;                 // msg40


	char  *m_formatStr;

	// this should be part of the key because it will affect the results!
	char   m_queryExpansion;

	int32_t   m_maxRealTimeInlinks;

	////////
	//
	// END USER PARMS
	//
	////////

	// . end the section we hash in SearchInput::makeKey()
	// . we also hash displayMetas, TopicGroups and Query into the key
	int32_t   m_END_HASH;



	//////
	//
	// STUFF NOT REALLY USED NWO
	//
	//////

	// for selecting a language
	//char   m_queryLang;
	//int32_t   m_gblang;

	// post query reranking
	//int32_t          m_docsToScanForReranking;

	//char  *m_url;
	//SafeBuf m_whiteListBuf;
	// password is for all
	//char  *m_pwd;
	// for /addurl?u=www.foo.com
	//char  *m_url2;


	// for /get?d=xxxxx&strip=0&ih=1&qh=1
	//int64_t m_docId;
	//int32_t      m_strip;
	//char      m_includeHeader;
	//char      m_queryHighlighting; 
	//char      m_doDateHighlighting;
        //int32_t      m_useAdFeedNum;

	//char          *m_username;

	// true if query is directly from an end-user
	//char m_endUser;

	//int32_t           m_maxResults;       // msg40

	// a marker for SearchInput::test()
	int32_t      m_END_TEST;

};

#endif
