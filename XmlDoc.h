// Matt Wells, copyright Apr 2009

// . 2. you can also call setTitleRec() and then call getMetaList()
// . this class is used by Repair.cpp and by Msg7 (inject) and SpiderLoop.cpp
// . Msg7 and Repair.cpp and injections can also set more than just 
//   m_firstUrl, like m_content, etc. or whatever elements are known, but
//   they must also set the corresponding "valid" flags of those elements
// . both methods must yield exactly the same result, the same "meta list"
// . after setting the contained classes XmlDoc::setMetaList() makes the list
//   of rdb records to be added to all the rdbs, this is the "meta list"
// . the meta list is made by hashing all the termIds/scores into some hash
//   tables in order to accumulate scores, then the hash table are serialized
//   into the "meta list"
// . the meta list is added to all rdbs with a simple call to 
//   Msg4::addMetaList(), which is only called by Msg14 or Repair.cpp for now


#ifndef _XMLDOC_H_
#define _XMLDOC_H_

//#include "HashTableX.h"
#include "Lang.h"
#include "Words.h"
#include "Bits.h"
#include "Pos.h"
#include "Phrases.h"
//#include "Synonyms.h"
//#include "Weights.h"
#include "Xml.h"
#include "LangList.h"
#include "SafeBuf.h"
#include "Images.h"
#include "Sections.h"
#include "Msge0.h"
#include "Msge1.h"
//#include "Msge2.h"
#include "Msg4.h"
#include "Msg8b.h"

#include "SearchInput.h"
#include "Msg40.h"
#include "Dates.h"
//#include "IndexList.h"
#include "Msg0.h"
#include "Msg22.h"
#include "Tagdb.h"
#include "Url.h"
#include "Linkdb.h"
//#include "LinkInfo.h"
//#include "Msg25.h"
#include "MsgC.h"
#include "Msg13.h"
#include "RdbList.h"
#include "SiteGetter.h"
//#include "CollectionRec.h"
#include "Msg20.h"
#include "Matches.h"
#include "Query.h"
#include "Title.h"
#include "Summary.h"
#include "Msg8b.h"
#include "Address.h"
#include "zlib.h" // Z_OK
#include "Spider.h" // SpiderRequest/SpiderReply definitions
#include "HttpMime.h" // ET_DEFLAT
#include "Msg1.h"
#include "PingServer.h"
#include "Json.h"

//#define XMLDOC_MAX_AD_IDS 4
//#define XMLDOC_ADLEN      64

#define MAXFRAGWORDS 80000

#define MAX_WIKI_DOCIDS 20

#define MAX_TAG_PAIR_HASHES 100

#include "Msg40.h"
//#define SAMPLE_VECTOR_SIZE (32*4)

#define POST_VECTOR_SIZE   (32*4)

#define XD_GQ_MAX_SIZE        1000
#define XD_MAX_GIGABIT_HASHES 48

#define XD_MAX_AD_IDS         5

double getTrafficPercent ( int32_t rank ) ;

bool setLangVec ( class Words *words , 
		  class SafeBuf *langBuf , 
		  class Sections *sections ,
		  int32_t niceness ) ;

char *getJSONFieldValue ( char *json, char *field , int32_t *valueLen ) ;

bool logQueryLogs ( );

bool checkRegex ( SafeBuf *regex , 
		  char    *target ,
		  bool    *boolVal ,
		  bool    *boolValValid ,
		  int32_t    *compileError ,
		  CollectionRec *cr ) ;

// Address.cpp calls this to make a vector from the "place name" for comparing
// to other places in placedb using the computeSimilarity() function. if
// we got a >75% similarity we set the AF_VERIFIED_PLACE_NAME bit in the
// Address::m_flags for that address on the web page.
int32_t makeSimpleWordVector ( char *s, int32_t *vbuf, int32_t vbufSize, int32_t niceness);

// this is used for making the event summary/title vectors as well as in
// Msg40.cpp where it merges events and does not want to repetitively display
// the same summary lines for an event
bool getWordVector ( char *s , 
		     HashTableX *ht , 
		     uint32_t *d ,
		     int32_t *nd ,
		     int32_t ndmax ) ;

bool getDensityRanks ( int64_t *wids , 
		       int32_t nw,
		       //int32_t wordStart , 
		       //int32_t wordEnd ,
		       int32_t hashGroup ,
		       SafeBuf *densBuf ,
		       Sections *sections ,
		       int32_t niceness );

// diversity vector
bool getDiversityVec ( class Words *words ,
		       class Phrases *phrases ,
		       class HashTableX *countTable ,
		       class SafeBuf *sbWordVec ,
		       //class SafeBuf *sbPhraseVec ,
		       int32_t niceness );

float computeSimilarity ( int32_t   *vec0 , 
			  int32_t   *vec1 ,
			  // corresponding scores vectors
			  int32_t   *s0   , 
			  int32_t   *s1   , 
			  class Query  *q    ,
			  int32_t  niceness ,
			  // only Sections::addDateBasedImpliedSections()
			  // sets this to true right now. if set to true
			  // we essentially dedup each vector, although
			  // the score is compounded into the remaining 
			  // occurence. i'm not sure if that is the right
			  // behavior though.
			  bool dedupVecs = false );

bool isSimilar_sorted ( int32_t   *vec0 , 
			int32_t   *vec1 ,
			int32_t nv0 , // how many int32_ts in vec?
			int32_t nv1 , // how many int32_ts in vec?
			// they must be this similar or more to return true
			int32_t percentSimilar,
			int32_t    niceness ) ;

// this is called by Msg40.cpp to set "top"
int32_t intersectGigabits ( Msg20       **mp          ,   // search results
			 int32_t          nmp         ,
			 uint8_t       langId      ,   // searcher's langId
			 int32_t          maxTop      ,
			 int32_t          docsToScan  ,
			 int32_t          minDocCount , // must be in this # docs
			 class GigabitInfo  *top   ,
			 int32_t          niceness    ) ;

int32_t getDirtyPoints ( char *s , int32_t len , int32_t niceness , char *logUrl ) ;

bool storeTerm ( char             *s        ,
                 int32_t              slen     ,
                 int64_t         termId   ,
                 class HashInfo   *hi       ,
                 int32_t              wordNum  ,
		 int32_t              wordPos  ,
		 char densityRank   ,
		 char diversityRank ,
		 char wordSpamRank  ,
		 char hashGroup ,
		 //bool              isPhrase ,
                 class SafeBuf    *wbuf     ,
                 class HashTableX *wts      ,
		 char              synSrc   ,
		 char              langId   ,
		 POSDBKEY key ) ;

// tell zlib to use our malloc/free functions
int gbuncompress ( unsigned char *dest      ,
		   uint32_t *destLen   ,
		   unsigned char *source    ,
		   uint32_t  sourceLen );

int gbcompress   ( unsigned char *dest      ,
		   uint32_t *destLen   ,
		   unsigned char *source    ,
		   uint32_t  sourceLen ,
		   int32_t encoding = ET_DEFLATE);

int gbcompress7  ( unsigned char *dest      ,
		   uint32_t *destLen   ,
		   unsigned char *source    ,
		   uint32_t  sourceLen ,
		   bool compress = true );

int gbuncompress7  ( unsigned char *dest      ,
		     uint32_t *destLen   ,
		     unsigned char *source    ,
		     uint32_t  sourceLen ) ;


uint32_t score8to32 ( uint8_t score8 );

// for Msg13.cpp
char getContentTypeFromContent ( char *p , int32_t niceness ) ;

// . for Msg13.cpp
// . *pend must equal \0
int32_t getContentHash32Fast ( unsigned char *p , 
			    int32_t plen ,
			    int32_t niceness ) ;

uint16_t getCharsetFast ( class HttpMime *mime, 
			  char *url ,
			  char *s , 
			  int32_t slen , 
			  int32_t niceness );

//#define MAX_CONTACT_OUTLINKS 5

#define MAX_CONTACT_ADDRESSES 20
#define EMAILBUFSIZE 512

#define ROOT_TITLE_BUF_MAX 512

// store the subsentences in an array now
class SubSent {
public:
	sentflags_t m_subSentFlags;
	//esflags_t   m_esflags;
	int32_t        m_senta;
	int32_t        m_sentb;
	int32_t        m_subEnding;
	float       m_titleScore;
};

#define MAX_XML_DOCS 4

#define MAXMSG7S 50

class XmlDoc {

 public:

	// . variable size rdb records all start with key then dataSize
	// . do not do that here since we compress our record's data!!
	//key_t m_titleRecKey;
	//int32_t  m_dataSize;

	//
	// BEGIN WHAT IS STORED IN THE TITLE REC (Titledb.h)
	//


	// headerSize = this->ptr_firstUrl - this->m_headerSize
	uint16_t  m_headerSize; 
	uint16_t  m_version;
	// these flags are used to indicate which ptr_ members are present:
	uint32_t  m_internalFlags1;
	int32_t      m_ip;
	int32_t      m_crawlDelay;
	// . use this to quickly detect if doc is unchanged
	// . we can avoid setting Xml and Words classes etc...
	int32_t      m_contentHash32;
	// like the above but hash of all tags in TagRec for this url
	//int32_t      m_tagHash32;
	// this is a hash of all adjacent tag pairs for templated identificatn
	uint32_t  m_tagPairHash32;
	int32_t      m_siteNumInlinks;
	//int32_t      m_siteNumInlinksUniqueIp; // m_siteNumInlinksFresh
	//int32_t      m_siteNumInlinksUniqueCBlock; // m_sitePop;
	int32_t    m_reserved1;
	int32_t    m_reserved2;
	uint32_t   m_spideredTime; // time_t
	// just don't throw away any relevant SpiderRequests and we have
	// the data that m_minPubDate and m_maxPubDate provided
	//time_t    m_minPubDate;
	//time_t    m_maxPubDate;
	uint32_t  m_indexedTime; // slightly > m_spideredTime (time_t)
	uint32_t  m_reserved32;
	uint32_t  m_pubDate;    // aka m_datedbDate // time_t
	//time_t  m_nextSpiderTime;
	uint32_t    m_firstIndexedDate; // time_t
	uint32_t    m_outlinksAddedDate; // time_t
	uint16_t  m_charset; // the ORIGINAL charset, we are always utf8!
	uint16_t  m_countryId;
	//uint16_t  m_reserved1;//titleWeight;
	//uint16_t  m_reserved2;//headerWeight;
	//int32_t      m_siteNumInlinksTotal;
	int32_t      m_reserved3;
	//uint16_t  m_reserved3;//urlPathWeight;
	uint8_t   m_metaListCheckSum8; // bring it back!!
	char      m_reserved3b;
	uint16_t  m_bodyStartPos;//m_reserved4;//externalLinkTextWeight;
	uint16_t  m_reserved5;//internalLinkTextWeight;

	// a new parm from reserved6. need to know the count so we can
	// delete the json objects derived from this page if we want to
	// delete this page. or if this page is respidered then we get the
	// json objects for it, REject the old json object urls, and inject
	// the new ones i guess.
	uint16_t  m_diffbotJSONCount;

	// these do not include header/footer (dup) addresses
	//int16_t   m_numAddresses;
	int16_t   m_httpStatus; // -1 if not found (empty http reply)
	
	//int8_t  m_nextSpiderPriority;
	int8_t    m_hopCount;
	//int8_t  m_metalistChecksum; // parser checksum
	//uint8_t m_numBannedOutlinks8;
	uint8_t   m_langId;
	uint8_t   m_rootLangId;
	uint8_t   m_contentType;


	// bit flags
	uint16_t  m_isRSS:1;
	uint16_t  m_isPermalink:1;
	uint16_t  m_isAdult:1;
	uint16_t  m_wasContentInjected:1;//eliminateMenus:1;
	uint16_t  m_spiderLinks:1;
	uint16_t  m_isContentTruncated:1;
	uint16_t  m_isLinkSpam:1;
	uint16_t  m_hasAddress:1;
	uint16_t  m_hasTOD:1;
	uint16_t  m_reserved_sv:1;//hasSiteVenue:1;
	uint16_t  m_hasContactInfo:1;
	uint16_t  m_isSiteRoot:1;

	uint16_t  m_isDiffbotJSONObject:1;
	uint16_t  m_sentToDiffbot:1;
	uint16_t  m_gotDiffbotSuccessfulReply:1;
	uint16_t  m_useTimeAxis:1; // m_reserved804:1;
	uint16_t  m_hasMetadata:1;
	uint16_t  m_reserved806:1;
	uint16_t  m_reserved807:1;
	uint16_t  m_reserved808:1;
	uint16_t  m_reserved809:1;
	uint16_t  m_reserved810:1;
	uint16_t  m_reserved811:1;
	uint16_t  m_reserved812:1;
	uint16_t  m_reserved813:1;
	uint16_t  m_reserved814:1;
	uint16_t  m_reserved815:1;
	uint16_t  m_reserved816:1;


	char      *ptr_firstUrl;
	char      *ptr_redirUrl;
	//char    *ptr_tagRecData;
	char      *ptr_rootTitleBuf;
	int32_t      *ptr_gigabitHashes;
	int32_t      *ptr_gigabitScores;
	int64_t *ptr_adVector;
	int64_t *ptr_wikiDocIds;
	rscore_t  *ptr_wikiScores;
	char      *ptr_imageData;
	int32_t      *ptr_catIds;
	int32_t      *ptr_indCatIds;
	char      *ptr_dmozTitles;
	char      *ptr_dmozSumms;
	char      *ptr_dmozAnchors;
	char      *ptr_utf8Content;
	//char    *ptr_sectionsReply; // votes read from sectiondb - m_osvt
	//char    *ptr_sectionsVotes; // our local votes - m_nsvt
	//char    *ptr_addressReply;
	//char      *ptr_clockCandidatesData;
	char      *ptr_metadata;
	// . serialization of the sectiondb and placedb lists
	// . that way we can store just these and not have to store the content
	//   of the entire page if we do not need to
	//char    *ptr_sectiondbData;
	//char    *ptr_placedbData;
	// do not let SiteGetter change this when we re-parse!
	char      *ptr_site;
	LinkInfo  *ptr_linkInfo1;
	char      *ptr_linkdbData;
	char      *ptr_sectiondbData;
	char      *ptr_tagRecData;
	LinkInfo  *ptr_linkInfo2;


	int32_t       size_firstUrl;
	int32_t       size_redirUrl;
	//int32_t     size_tagRecData;
	int32_t       size_rootTitleBuf;
	int32_t       size_gigabitHashes;
	int32_t       size_gigabitScores;
	int32_t       size_adVector;
	int32_t       size_wikiDocIds;
	int32_t       size_wikiScores;
	int32_t       size_imageData;
	int32_t       size_catIds;
	int32_t       size_indCatIds;
	int32_t       size_dmozTitles;
	int32_t       size_dmozSumms;
	int32_t       size_dmozAnchors;
	int32_t       size_utf8Content;
	//int32_t     size_sectionsReply;
	//int32_t     size_sectionsVotes;
	//int32_t     size_addressReply;
	//int32_t       size_clockCandidatesData;
	int32_t       size_metadata;
	//int32_t     size_sectiondbData;
	//int32_t     size_placedbData;
	int32_t       size_site;
	int32_t       size_linkInfo1;
	int32_t       size_linkdbData;
	int32_t       size_sectiondbData;
	int32_t       size_tagRecData;
	int32_t       size_linkInfo2;

	char      m_dummyEnd;

	//
	// END WHAT IS STORED IN THE TITLE REC (Titledb.h)
	//

 public:

	// . returns false and sets errno on error
	// . once you call this you can call setMetaList() below
	// . sets all the contained parser classes, Words, Xml, etc. if they
	//   have not already been set! that way Msg16/Msg14 can set bits
	//   and pieces here and there and we do not reset what it's done
	// . our m_xml will contain ptrs into titleRec's content, be careful
	// . if titleRec gets freed we should be freed too
	//bool set ( char           *titleRec                    ,
	//	   class SafeBuf  *pbuf         = NULL         ,
	//	   int32_t            niceness     = MAX_NICENESS ,
	//	   bool            justSetLinks = false        );

	// . used by Msg16 to set the Xml to get meta redirect tag's content
	// . used by Msg16 to get <META NAME="ROBOTS" CONTENT="index,follow">
	// . this should be set by Msg16 so it can get meta redirect url


	void print   ( );

	bool set1 ( char *url ,
		    char *coll,
		    SafeBuf *pbuf ,
		    int32_t niceness );


	bool set2 ( char *titleRec,
		    int32_t maxSize, 
		    char *coll,
		    class SafeBuf *p,
		    int32_t niceness ,
		    class SpiderRequest *sreq = NULL );

	// . since being set from a docId, we will load the old title rec
	//   and use that!
	// . used by PageGet.cpp
	bool set3 ( int64_t  docId       , 
		    char      *coll        ,
		    int32_t       niceness    );

	bool set4 ( class SpiderRequest *sreq  , 
		    key_t           *doledbKey ,
		    char            *coll      , 
		    class SafeBuf   *pbuf      , 
		    int32_t          niceness  ,
		    char            *utf8Content = NULL ,
		    bool             deleteFromIndex = false ,
		    int32_t             forcedIp = 0 ,
		    uint8_t          contentType = CT_HTML ,
		    uint32_t           spideredTime = 0 , // time_t
		    bool             contentHasMime = false ,
		    // for container docs, what is the separator of subdocs?
		    char            *contentDelim = NULL,
			char *metadata = NULL,
			uint32_t metadataLen = 0,
			// for injected docs we have the recv, buffer size don't exceed that
			int32_t payloadLen = -1) ;

	// we now call this right away rather than at download time!
	int32_t getSpideredTime();

	// time right before adding the termlists to the index, etc.
	// whereas spider time is the download time
	int32_t getIndexedTime();

	// another entry point, like set3() kinda
	bool loadFromOldTitleRec ();

	XmlDoc() ; 
	~XmlDoc() ; 
	void nukeDoc ( class XmlDoc *);
	void reset ( ) ;
	bool setFirstUrl ( char *u , bool addWWW , Url *base = NULL ) ;
	bool setRedirUrl ( char *u , bool addWWW ) ;
	void setStatus ( char *s ) ;
	void setCallback ( void *state, void (*callback) (void *state) ) ;
	void setCallback ( void *state, bool (*callback) (void *state) ) ;
	bool addToSpiderdb ( ) ;
	void getRevisedSpiderRequest ( class SpiderRequest *revisedReq );
	void getRebuiltSpiderRequest ( class SpiderRequest *sreq ) ;
	bool indexDoc ( );
	bool indexDoc2 ( );
	bool isContainerDoc ( );
	bool indexContainerDoc ( );

	bool readMoreWarc();
	bool indexWarcOrArc ( ) ;
	key_t *getTitleRecKey() ;
	//char *getSkipIndexing ( );
	char *prepareToMakeTitleRec ( ) ;
	// store TitleRec into "buf" so it can be added to metalist
	bool setTitleRecBuf ( SafeBuf *buf , int64_t docId, int64_t uh48 );
	// sets m_titleRecBuf/m_titleRecBufValid/m_titleRecKey[Valid]
	SafeBuf *getTitleRecBuf ( );
	bool appendNewMetaInfo ( SafeBuf *metaList , bool forDelete ) ;
	SafeBuf *getSpiderStatusDocMetaList ( class SpiderReply *reply ,
					      bool forDelete ) ;
	SafeBuf *getSpiderStatusDocMetaList2 ( class SpiderReply *reply ) ;
	bool setSpiderStatusDocMetaList ( SafeBuf *jd , int64_t ssDocId ) ;
	SafeBuf m_spiderStatusDocMetaList;
	char *getIsAdult ( ) ;
	int32_t **getIndCatIds ( ) ;
	int32_t **getCatIds ( ) ;
	class CatRec *getCatRec ( ) ;

	int32_t *getNumDmozEntries() ;
	char **getDmozTitles ( ) ;
	char **getDmozSummaries ( ) ;
	char **getDmozAnchors ( ) ;
	bool setDmozInfo () ;

	int64_t **getWikiDocIds ( ) ;
	void gotWikiResults ( class UdpSlot *slot );
	int32_t *getPubDate ( ) ;
	//class DateParse2 *getDateParse2 ( ) ;
	class Dates *getSimpleDates();
	class Dates *getDates();
	//class HashTableX *getClockCandidatesTable();
	int32_t getUrlPubDate ( ) ;
	int32_t getOutlinkAge ( int32_t outlinkNum ) ;
	char *getIsPermalink ( ) ;
	char *getIsUrlPermalinkFormat ( ) ;
	char *getIsRSS ( ) ;
	char *getIsSiteMap ( ) ;
	class Xml *getXml ( ) ;
	uint8_t *getLangVector ( ) ;	
	uint8_t *getLangId ( ) ;
	char computeLangId ( Sections *sections ,Words *words , char *lv ) ;
	class Words *getWords ( ) ;
	class Bits *getBits ( ) ;
	class Bits *getBitsForSummary ( ) ;
	class Pos *getPos ( );
	class Phrases *getPhrases ( ) ;
	//class Synonyms *getSynonyms ( );
	class Sections *getExplicitSections ( ) ;
	class Sections *getImpliedSections ( ) ;
	class Sections *getSections ( ) ;
	class Sections *getSectionsWithDupStats ( );
	class SafeBuf  *getInlineSectionVotingBuf();
	bool gotSectionFacets( class Multicast *mcast );
	class SectionStats *getSectionStats ( uint32_t secHash32 ,
					      uint32_t sentHash32 ,
					      bool cacheOnly );
	class SectionVotingTable *getOldSectionVotingTable();
	class SectionVotingTable *getNewSectionVotingTable();
	char **getSectionsReply ( ) ;
	char **getSectionsVotes ( ) ;
	HashTableX *getSectionVotingTable();
	int32_t *getLinkSiteHashes ( );
	class Links *getLinks ( bool doQuickSet = false ) ;
	class HashTableX *getCountTable ( ) ;
	bool hashString_ct ( class HashTableX *ht, char *s , int32_t slen ) ;
	uint8_t *getSummaryLangId ( ) ;
	int32_t *getSummaryVector ( ) ;
	int32_t *getPageSampleVector ( ) ;
	int32_t *getPostLinkTextVector ( int32_t linkNode ) ;
	int32_t computeVector ( class Sections *sections, class Words *words, 
			     uint32_t *vec , int32_t start = 0 , int32_t end = -1 );
	float *getTagSimilarity ( class XmlDoc *xd2 ) ;
	float *getGigabitSimilarity ( class XmlDoc *xd2 ) ;
	float *getPageSimilarity ( class XmlDoc *xd2 ) ;
	float *getPercentChanged ( );
	uint64_t *getFuzzyDupHash ( );
	int64_t *getExactContentHash64();
	int64_t *getLooseContentHash64();
	class RdbList *getDupList ( ) ;
	class RdbList *getLikedbListForReq ( );
	class RdbList *getLikedbListForIndexing ( );
	int32_t addLikedbRecords ( bool justGetSize ) ;
	char *getIsDup ( ) ;
	char *isDupOfUs ( int64_t d ) ;
	uint32_t *getGigabitVectorScorelessHash ( ) ;
	int32_t **getGigabitHashes ( );
	char *getGigabitQuery ( ) ;
	char *getMetaDescription( int32_t *mdlen ) ;
	char *getMetaSummary ( int32_t *mslen ) ;
	char *getMetaKeywords( int32_t *mklen ) ;
	char *getMetadata(int32_t* retlen);
	bool addGigabits ( char *s , int64_t docId , uint8_t langId ) ;
	bool addGigabits2 ( char *s,int32_t slen,int64_t docId,uint8_t langId);
	bool addGigabits ( class Words *ww , 
			   int64_t docId,
			   class Sections *sections,
			   //class Weights  *we ,
			   uint8_t langId );

	int32_t *getSiteSpiderQuota ( ) ;
	class Url *getCurrentUrl ( ) ;
	class Url *getFirstUrl() ;
	int64_t getFirstUrlHash48();
	int64_t getFirstUrlHash64();
	class Url **getLastRedirUrl() ;
	class Url **getRedirUrl() ;
	class Url **getMetaRedirUrl() ;
	class Url **getCanonicalRedirUrl ( ) ;
	int32_t *getFirstIndexedDate ( ) ;
	int32_t *getOutlinksAddedDate ( ) ;
	//int32_t *getNumBannedOutlinks ( ) ;
	uint16_t *getCountryId ( ) ;
	class XmlDoc **getOldXmlDoc ( ) ;
	//bool isRobotsTxtFile ( char *url , int32_t urlLen ) ;
	class XmlDoc **getExtraDoc ( char *url , int32_t maxCacheAge = 0 ) ;
	bool getIsPageParser ( ) ;
	class XmlDoc **getRootXmlDoc ( int32_t maxCacheAge = 0 ) ;
	//class XmlDoc **getGatewayXmlDoc ( ) ;
	// . returns false if blocked, true otherwise.
	// . returns true and sets g_errno on error
	//bool setFromOldTitleRec ( ) ;
	//RdbList *getOldMetaList ( ) ;
	char **getOldTitleRec ( );
	uint8_t *getRootLangId ();
	//bool *updateRootLangId ( );
	char **getRootTitleRec ( ) ;
	//char **getContactTitleRec ( char *url ) ;
	int64_t *getAvailDocIdOnly ( int64_t preferredDocId ) ;
	int64_t *getDocId ( ) ;
	char *getIsIndexed ( ) ;
	class TagRec *getTagRec ( ) ;
	char *getHasContactInfo ( ) ;
	char *getIsThisDocContacty ( );
	bool *getHasTOD();
	bool *getHasSiteVenue();
	// non-dup/nondup addresses only
	bool *getHasAddress();
	class Addresses *getAddresses ( ) ;
	Address **getContactAddresses ( );
	int32_t *getNumOfficialEmails ( ) ;
	char *getEmailBuf ( ) ;
	int32_t *getNumContactAddresses ( );
	int32_t addEmailTags ( class Xml *xml , class Words *ww , 
			    class TagRec *gr , int32_t ip ) ;
	//class Url *getContactUsLink ( ) ;
	//class Url *getAboutUsLink ( ) ;
	int32_t *getFirstIp ( ) ;
	bool *updateFirstIp ( ) ;
	//int32_t *getSiteNumInlinksUniqueIp ( ) ;
	//int32_t *getSiteNumInlinksUniqueCBlock ( ) ;
	//int32_t *getSiteNumInlinksTotal ( );
	//int32_t *getSiteNumInlinksFresh ( ) ;
	//int32_t *getSitePop ( ) ;
	uint8_t *getSiteNumInlinks8 () ;
	int32_t *getSiteNumInlinks ( ) ;
	class LinkInfo *getSiteLinkInfo() ;
	int32_t *getIp ( ) ;
	int32_t *gotIp ( bool save ) ;
	bool *getIsAllowed ( ) ;
	int32_t *getFinalCrawlDelay();
	int32_t      m_finalCrawlDelay;
	//int32_t getTryAgainTimeDelta() { 
	//	if ( ! m_tryAgainTimeDeltaValid ) { char *xx=NULL;*xx=0;}
	//	return m_tryAgainTimeDelta;
	//};
	char *getIsWWWDup ( ) ;
	class LinkInfo *getLinkInfo1 ( ) ;
	class LinkInfo **getLinkInfo2 ( ) ;
	char *getSite ( ) ;
	void  gotSite ( ) ;
	int64_t *getSiteHash64 ( ) ;
	int32_t *getSiteHash32 ( ) ;
	char **getHttpReply ( ) ;
	char **getHttpReply2 ( ) ;
	char **gotHttpReply ( ) ;
	char *getIsContentTruncated ( );
	int32_t *getDownloadStatus ( ) ;
	int64_t *getDownloadEndTime ( ) ;
	int16_t *getHttpStatus ( );
	char waitForTimeSync ( ) ;
	bool m_alreadyRegistered;
	class HttpMime *getMime () ;
	char **getContent ( ) ;
	uint8_t *getContentType ( ) ;
	uint16_t *getCharset ( ) ;
	char *getIsBinary ( ) ;
	char **getFilteredContent ( ) ;
	void filterStart_r ( bool amThread ) ;
	char **getRawUtf8Content ( ) ;
	char **getExpandedUtf8Content ( ) ;
	char **getUtf8Content ( ) ;
	// we download large files to a file on disk, like warcs and arcs
	FILE *getUtf8ContentInFile ( );
	int32_t *getContentHash32 ( ) ;
	int32_t *getContentHashJson32 ( ) ;
	//int32_t *getTagHash32 ( ) ;
	int32_t     *getTagPairHashVector ( ) ;
	uint32_t *getTagPairHash32 ( ) ;
	int32_t getHostHash32a ( ) ;
	int32_t getHostHash32b ( ) ;
	int32_t getDomHash32 ( );
	char **getThumbnailData();
	class Images *getImages ( ) ;
	int8_t *getNextSpiderPriority ( ) ;
	int32_t *getPriorityQueueNum ( ) ;
	class TagRec ***getOutlinkTagRecVector () ;
	char *hasNoIndexMetaTag();
	char *hasFakeIpsMetaTag ( );
	int32_t **getOutlinkFirstIpVector () ;
	//char **getOutlinkIsIndexedVector () ;
	int32_t *getRegExpNum ( int32_t outlinkNum ) ;
	int32_t *getRegExpNum2 ( int32_t outlinkNum ) ;
	char *getIsSiteRoot ( ) ;
	bool getIsOutlinkSiteRoot ( char *u , class TagRec *gr ) ;
	int8_t *getHopCount ( ) ;
	//int8_t *getOutlinkHopCountVector ( ) ;
	char *getSpiderLinks ( ) ;
	int32_t *getNextSpiderTime ( ) ;
	//char *getIsSpam() ;
	char *getIsFiltered ();
	bool getIsInjecting();
	int32_t *getSpiderPriority ( ) ;
	int32_t *getIndexCode ( ) ;
	int32_t *getIndexCode2 ( ) ;
	SafeBuf *getNewTagBuf ( ) ;

	char *updateTagdb ( ) ;
	bool logIt ( class SafeBuf *bb = NULL ) ;
	bool m_doConsistencyTesting;
	bool doConsistencyTest ( bool forceTest ) ;
	int32_t printMetaList ( ) ;
	void printMetaList ( char *metaList , char *metaListEnd ,
			     class SafeBuf *pbuf );
	bool verifyMetaList ( char *p , char *pend , bool forDelete ) ;
	bool hashMetaList ( class HashTableX *ht        ,
			    char       *p         ,
			    char       *pend      ,
			    bool        checkList ) ;

	char *getMetaList ( bool forDelete = false );

	char *getDiffbotParentUrl( char *myUrl );

	int64_t m_diffbotReplyEndTime;
	int64_t m_diffbotReplyStartTime;
	int32_t m_diffbotReplyRetries;

	bool m_sentToDiffbotThisTime;

	uint64_t m_downloadStartTime;
	//uint64_t m_downloadEndTime;

	uint64_t m_ipStartTime;
	uint64_t m_ipEndTime;

	bool m_updatedMetaData;

	void copyFromOldDoc ( class XmlDoc *od ) ;

	class SpiderReply *getFakeSpiderReply ( );

	// we add a SpiderReply to spiderdb when done spidering, even if
	// m_indexCode or g_errno was set!
	class SpiderReply *getNewSpiderReply ( );


	SpiderRequest **getRedirSpiderRequest ( );
	SpiderRequest m_redirSpiderRequest;
	SpiderRequest *m_redirSpiderRequestPtr;


	void  setSpiderReqForMsg20 ( class SpiderRequest *sreq , 
				     class SpiderReply   *srep );


	char *addOutlinkSpiderRecsToMetaList ( );

	//bool addTable96 ( class HashTableX *tt1     , 
	//		  int32_t       date1   ,
	//		  bool       nosplit ) ;

	int32_t getSiteRank ();
	bool addTable144 ( class HashTableX *tt1 , 
			   int64_t docId ,
			   class SafeBuf *buf = NULL );

	bool addTable224 ( HashTableX *tt1 ) ;

	//bool addTableDate ( class HashTableX *tt1     , //T<key128_t,char> *tt1
	//                           uint64_t    docId   ,
	//                           uint8_t     rdbId   ,
	//                           bool        nosplit ) ;

	bool addTable128 ( class HashTableX *tt1     , // T <key128_t,char>*tt1
                           uint8_t     rdbId   ,
			   bool        forDelete ) ;

	bool hashNoSplit ( class HashTableX *tt ) ;
	char *hashAll ( class HashTableX *table ) ;
	int32_t getBoostFromSiteNumInlinks ( int32_t inlinks ) ;
	bool hashSpiderReply (class SpiderReply *reply ,class HashTableX *tt) ;
	bool hashMetaTags ( class HashTableX *table ) ;
	bool hashMetaData ( class HashTableX *table ) ;
	bool hashIsClean ( class HashTableX *table ) ;
	bool hashZipCodes ( class HashTableX *table ) ;
	bool hashMetaZip ( class HashTableX *table ) ;
	bool hashContentType ( class HashTableX *table ) ;
	bool hashDMOZCategories ( class HashTableX *table ) ;
	bool hashLinks ( class HashTableX *table ) ;
	bool getUseTimeAxis ( ) ;
	SafeBuf *getTimeAxisUrl ( );
	bool hashUrl ( class HashTableX *table );
	bool hashDateNumbers ( class HashTableX *tt );
	bool hashSections ( class HashTableX *table ) ;
	bool hashIncomingLinkText ( class HashTableX *table            ,
				    bool       hashAnomalies    ,
                                    bool       hashNonAnomalies ) ;

	bool hashLinksForLinkdb ( class HashTableX *table ) ;
	bool hashNeighborhoods ( class HashTableX *table ) ;
	bool hashRSSInfo ( class HashTableX *table ) ;
	bool hashRSSTerm ( class HashTableX *table , bool inRSS ) ;
	bool hashTitle ( class HashTableX *table );
	bool hashBody2 ( class HashTableX *table );
	bool hashMetaKeywords ( class HashTableX *table );
	bool hashMetaSummary ( class HashTableX *table );
	bool linksToGigablast ( ) ;
	bool searchboxToGigablast ( ) ;
	bool hashLanguage ( class HashTableX *table ) ;
	bool hashLanguageString ( class HashTableX *table ) ;
	bool hashCountry ( class HashTableX *table ) ;
	bool hashSiteNumInlinks ( class HashTableX *table ) ;
	bool hashCharset ( class HashTableX *table ) ;
	bool hashTagRec ( class HashTableX *table ) ;
	bool hashPermalink ( class HashTableX *table ) ;
	bool hashVectors(class HashTableX *table ) ;
	bool hashAds(class HashTableX *table ) ;
	class Url *getBaseUrl ( ) ;
	bool hashSubmitUrls ( class HashTableX *table ) ;
	bool hashImageStuff ( class HashTableX *table ) ;
	bool hashIsAdult    ( class HashTableX *table ) ;

	void set20 ( Msg20Request *req ) ;
	class Msg20Reply *getMsg20Reply ( ) ;
	char **getDiffbotPrimaryImageUrl ( ) ;
	char **getImageUrl() ;
	class MatchOffsets *getMatchOffsets () ;
	Query *getQuery() ;
	Matches *getMatches () ;
	char *getDescriptionBuf ( char *displayMetas , int32_t *dlen ) ;
	SafeBuf *getHeaderTagBuf();
	class Title *getTitle ();
	class Summary *getSummary () ;
	char *getHighlightedSummary ();
	SafeBuf *getSampleForGigabits ( ) ;
	SafeBuf *getSampleForGigabitsJSON ( ) ;
	char *getIsCompromised ( ) ;
	char *getIsNoArchive ( ) ;
	int32_t *getUrlFilterNum();
	//int32_t *getDiffbotApiNum();
	SafeBuf *getDiffbotApiUrl();
	int64_t **getAdVector ( ) ;
	char *getIsLinkSpam ( ) ;
	char *getIsHijacked();
	char *getIsErrorPage ( ) ;
	char* matchErrorMsg(char* p, char* pend );

	bool hashWords  ( //int32_t            wordStart ,
			  //int32_t            wordEnd   ,
			  class HashInfo *hi        ) ;
	bool hashSingleTerm ( int64_t       termId , 
			      class HashInfo *hi     ) ;
	bool hashSingleTerm ( char            *s    ,
			      int32_t             slen ,
			      class HashInfo  *hi   );
	bool hashString ( class HashTableX *ht   ,
			  //class Weights    *we   ,
			  class Bits       *bits ,
			  char             *s    ,
			  int32_t              slen ) ;
	bool hashString ( char             *s    ,
			  int32_t              slen ,
			  class HashInfo   *hi   ) ;
	bool hashString ( char             *s    ,
			  class HashInfo   *hi   ) ;



	bool hashWords3 ( //int32_t              wordStart     ,
			  //int32_t              wordEnd       ,
			  class HashInfo   *hi            ,
			  class Words      *words         , 
			  class Phrases    *phrases       , 
			  class Synonyms   *synonyms      , 
			  class Sections   *sections      ,
			  class HashTableX *countTable    ,
			  char *fragVec ,
			  char *wordSpamVec ,
			  char *langVec ,
			  char  docLangId , // default lang id
			  class SafeBuf    *pbuf          ,
			  class HashTableX *wts           ,
			  class SafeBuf    *wbuf          ,
			  int32_t              niceness      );
	
	bool hashString3 ( char             *s              ,
			  int32_t              slen           ,
			  class HashInfo   *hi             ,
			  class HashTableX *countTable     ,
			  class SafeBuf    *pbuf           ,
			  class HashTableX *wts            ,
			  class SafeBuf    *wbuf           ,
			  int32_t              version        ,
			  int32_t              siteNumInlinks ,
			  int32_t              niceness       );


	//bool hashSectionTerm ( char *term , 
	//		       class HashInfo *hi , 
	//		       int32_t sentHash32 ) ;

	bool hashFacet1 ( char *term, class Words *words , HashTableX *dt) ;

	bool hashFacet2 ( char *prefix,char *term,int32_t val32, HashTableX *dt,
			  bool shardByTermId = false ) ;

	// gbfieldmatch:
	bool hashFieldMatchTerm ( char *val, int32_t vlen, class HashInfo *hi);

	bool hashNumber ( char *beginBuf ,
			  char *buf , 
			  int32_t bufLen , 
			  class HashInfo *hi ) ;

	bool hashNumber2 ( float f , 
			   class HashInfo *hi ,
			   char *gbsortByStr ) ;

	bool hashNumber3 ( int32_t x,
			   class HashInfo *hi ,
			   char *gbsortByStr ) ;

	bool storeFacetValues         ( char *qs , class SafeBuf *sb ,
					FacetValHash_t fvh ) ;
	bool storeFacetValuesSite     ( char *qs , SafeBuf *sb , 
					FacetValHash_t fvh );
	bool storeFacetValuesSections ( char *qs , class SafeBuf *sb ,
					FacetValHash_t fvh ) ;
	bool storeFacetValuesHtml     ( char *qs , class SafeBuf *sb ,
					FacetValHash_t fvh ) ;
	bool storeFacetValuesXml      ( char *qs , class SafeBuf *sb ,
					FacetValHash_t fvh ) ;
	bool storeFacetValuesJSON     ( char *qs , class SafeBuf *sb ,
                                    FacetValHash_t fvh,
                                    Json* jp ) ;

	// print out for PageTitledb.cpp and PageParser.cpp
	bool printDoc ( class SafeBuf *pbuf );
	bool printMenu ( class SafeBuf *pbuf );
	bool printDocForProCog ( class SafeBuf *sb , HttpRequest *hr ) ;
	bool printGeneralInfo ( class SafeBuf *sb , HttpRequest *hr ) ;
	bool printRainbowSections ( class SafeBuf *sb , HttpRequest *hr );
	bool printSiteInlinks ( class SafeBuf *sb , HttpRequest *hr );
	bool printPageInlinks ( class SafeBuf *sb , HttpRequest *hr );
	bool printTermList ( class SafeBuf *sb , HttpRequest *hr );
	bool printSpiderStats ( class SafeBuf *sb , HttpRequest *hr );
	bool printCachedPage ( class SafeBuf *sb , HttpRequest *hr );

	bool printSerpFiltered ( class Section *sx , char *tagName ) ;

	char **getTitleBuf             ( );
	char **getRootTitleBuf         ( );
	char **getFilteredRootTitleBuf ( );

	// funcs that update our tagdb tagrec, m_tagRec, and also update tagdb
	bool *updateVenueAddresses ( );

	// called by msg0 handler to add posdb termlists into g_termListCache
	// for faster seo pipeline
	bool cacheTermLists();

 public:

	// stuff set from the key of the titleRec, above the compression area
	//key_t     m_key;
	int64_t m_docId;

	char     *m_ubuf;
	int32_t      m_ubufSize;
	int32_t      m_ubufAlloc;

	// does this page link to gigablast, or has a search form to it?
	//bool linksToGigablast();
	//bool searchboxToGigablast();

	// private:

	// we we started spidering it, in milliseconds since the epoch
	int64_t    m_startTime;
	int64_t    m_injectStartTime;

	class XmlDoc *m_prevInject;
	class XmlDoc *m_nextInject;

	// when set() was called by Msg20.cpp so we can time how long it took
	// to generate the summary
	int64_t    m_setTime;
	int64_t    m_cpuSummaryStartTime;

	// timers
	int64_t m_beginSEOTime;
	int64_t m_beginTimeAllMatch;
	int64_t m_beginTimeMatchUrl;
	int64_t m_beginTimeFullQueries;
	int64_t m_beginTimeLinks;
	//int64_t m_beginMsg98s;
	int64_t m_beginRelatedQueries;
	int64_t m_beginMsg95s;

	// . these should all be set using set*() function calls so their
	//   individual validity flags can bet set to true, and successive
	//   calls to their corresponding get*() functions will not core
	// . these particular guys are set immediately on set(char *titleRec)

	Url        m_redirUrl;
	Url       *m_redirUrlPtr;
	Url       *m_lastRedirUrlPtr;
	SafeBuf    m_redirCookieBuf;
	Url        m_metaRedirUrl;
	Url       *m_metaRedirUrlPtr;
	Url        m_canonicalRedirUrl;
	Url       *m_canonicalRedirUrlPtr;
	int32_t       m_redirError;
	char       m_allowSimplifiedRedirs;
	Url        m_firstUrl;
	int64_t  m_firstUrlHash48;
	int64_t  m_firstUrlHash64;
	Url        m_currentUrl;

	//char      *m_coll;
	//char       m_collBuf[MAX_COLL_LEN+1]; // include \0
	CollectionRec *m_lastcr;
	collnum_t      m_collnum;
	int32_t           m_lastCollRecResetCount;
	class CollectionRec *getCollRec ( ) ;
	bool setCollNum ( char *coll ) ;


	char      *m_content;
	int32_t       m_contentLen;

	char *m_metaList;
	int32_t  m_metaListSize;

	int32_t m_addedSpiderRequestSize;
	int32_t m_addedSpiderReplySize;
	int32_t m_addedStatusDocSize;
	int64_t m_addedStatusDocId;

	SafeBuf  m_metaList2;
	SafeBuf  m_zbuf;
	SafeBuf  m_kbuf;

	// warc parsing member vars
	class Msg7 *m_msg7;
	class Msg7 *m_msg7s[MAXMSG7S];
	char *m_warcContentPtr;
	char *m_arcContentPtr;
	char *m_anyContentPtr;
	char *m_contentDelim;
	SafeBuf m_injectUrlBuf;
	bool m_subDocsHaveMime;
	int32_t m_warcError ;
	int32_t m_arcError ;
	bool m_doneInjectingWarc ;

	int64_t m_bytesStreamed;
	char *m_fileBuf ;
	int32_t m_fileBufAllocSize;
	bool    m_registeredWgetReadCallback;
	char *m_fptr ;
	char *m_fptrEnd ;

	FILE* m_pipe;
	
	BigFile m_file;
	int64_t m_fileSize;
	FileState m_fileState;
	bool m_readThreadOut;
	bool m_hasMoreToRead;
	int32_t m_numInjectionsOut;
	bool m_calledWgetThread;

	// used by msg7 to store udp slot
	class UdpSlot *m_injectionSlot;

	// . same thing, a little more complicated
	// . these classes are only set on demand
	Xml        m_xml;
	Links      m_links;
	Words      m_words;
	Bits       m_bits;
	Bits       m_bits2;
	Pos        m_pos;
	Phrases    m_phrases;
	//Synonyms   m_synonyms;
	SafeBuf    m_synBuf;
	//Weights    m_weights;
	Sections   m_sections;

	// a hack storage thing used by Msg13.cpp
	class Msg13Request *m_hsr;

	Section *m_si;
	//Section *m_nextSection;
	//Section *m_lastSection;
	int32_t m_mcastRequestsOut;
	int32_t m_mcastRequestsIn;
	int32_t m_secStatsErrno;
	char *m_queryBuf;
	Msg39Request *m_msg39RequestArray;
	SafeBuf m_mcastBuf;
	Multicast *m_mcastArray;
	//char  *m_inUse;
	//Query *m_queryArray;
	//Query *m_sharedQuery;
	bool     m_gotDupStats;
	//Query    m_q4;
	//Msg3a    m_msg3a;
	//Msg39Request m_r39;
	Msg39Request m_mr2;
	SectionStats m_sectionStats;
	HashTableX m_sectionStatsTable;
	//char m_sectionHashQueryBuf[128];

	// also set in getSections()
	int32_t       m_maxVotesForDup;

	// . for rebuild logging of what's changed
	// . Repair.cpp sets these based on titlerec
	char m_logLangId;
	int32_t m_logSiteNumInlinks;

	SectionVotingTable m_nsvt;

	SectionVotingTable m_osvt;
	int32_t m_numSectiondbReads;
	int32_t m_numSectiondbNeeds;
	key128_t m_sectiondbStartKey;
	RdbList m_secdbList;
	int32_t m_sectiondbRecall;
	SafeBuf m_tmpBuf3;

	bool m_gotFacets;
	SafeBuf m_tmpBuf2;

	SafeBuf m_inlineSectionVotingBuf;

	//HashTableX m_rvt;
	//Msg17 m_msg17;
	//char *m_cachedRootVoteRec;
	//int32_t  m_cachedRootVoteRecSize;
	//bool  m_triedVoteCache;
	//bool  m_storedVoteCache;
	//SafeBuf m_cacheRecBuf;

	SafeBuf m_timeAxisUrl;

	HashTableX m_turkVotingTable;
	HashTableX m_turkBitsTable;
	uint32_t m_confirmedTitleContentHash ;
	uint32_t m_confirmedVenueContentHash ;
	uint32_t m_confirmedTitleTagHash     ;
	uint32_t m_confirmedVenueTagHash     ;

	// turk voting tag rec
	TagRec m_vtr;
	// tagrec of banned turks
	TagRec m_bannedTurkRec;
	// and the table of the hashed banned turk users
	HashTableX m_turkBanTable;

	// used for displaying turk votes...
	HashTableX m_vctab;
	HashTableX m_vcduptab;

	bool isFirstUrlRobotsTxt();
	bool m_isRobotsTxtUrl;

	Images     m_images;
	HashTableX m_countTable;
	HttpMime   m_mime;
	TagRec     m_tagRec;
	SafeBuf    m_tagRecBuf;
	// copy of m_oldTagRec but with our modifications, if any
	//TagRec     m_newTagRec;
	SafeBuf    m_newTagBuf;
	SafeBuf    m_fragBuf;
	SafeBuf    m_wordSpamBuf;
	SafeBuf    m_finalSummaryBuf;
	// this one is initially the same as m_tagRec, but we do not modify it
	// so that Address.cpp can reference into its buffer, m_buf, without
	// fear of getting the buffer overwritten by crap
	//TagRec     m_savedTagRec1;
	//char    *m_sampleVector  ;
	//uint32_t   m_tagPairHash32;
	int32_t       m_firstIp;

	class SafeBuf     *m_savedSb;
	class HttpRequest *m_savedHr;

	char m_savedChar;


	// validity flags. on reset() all these are set to false.
	char     m_VALIDSTART;
	// DO NOT add validity flags above this line!
	char     m_metaListValid;
	char     m_addedSpiderRequestSizeValid;
	char     m_addedSpiderReplySizeValid;
	char     m_addedStatusDocSizeValid;
	char     m_downloadStartTimeValid;
	char     m_contentDelimValid;
	char     m_fileValid;
	//char   m_docQualityValid;
	char     m_siteValid;
	char     m_startTimeValid;
	char     m_currentUrlValid;
	char     m_useTimeAxisValid;
	char     m_timeAxisUrlValid;
	char     m_firstUrlValid;
	char     m_firstUrlHash48Valid;
	char     m_firstUrlHash64Valid;
	char     m_lastUrlValid;
	char     m_docIdValid;
	char     m_availDocIdValid;
	//char     m_collValid;
	char     m_tagRecValid;
	char     m_robotsTxtLenValid;
	char     m_tagRecDataValid;
	char     m_newTagBufValid;
	char     m_rootTitleBufValid;
	char     m_filteredRootTitleBufValid;
	char     m_titleBufValid;
	char     m_fragBufValid;
	char     m_isRobotsTxtUrlValid;
	char     m_inlineSectionVotingBufValid;
	char     m_wordSpamBufValid;
	char     m_finalSummaryBufValid;
	char     m_matchingQueryBufValid;
	char     m_matchesCrawlPatternValid;
	char     m_relatedQueryBufValid;
	char     m_queryLinkBufValid;
	char     m_redirSpiderRequestValid;
	//char     m_queryPtrsValid;
	char     m_queryOffsetsValid;
	//char     m_queryPtrsSortedValid;
	char     m_queryPtrsWholeValid;
	char     m_relatedDocIdBufValid;
	char     m_topMatchingQueryBufValid;
	char     m_relatedDocIdsScoredBufValid;
	char     m_relatedDocIdsWithTitlesValid;
	char     m_relatedTitleBufValid;
	//char     m_queryLinkBufValid;
	char     m_missingTermBufValid;
	char     m_matchingTermBufValid;
	//char     m_relPtrsValid;
	char     m_sortedPosdbListBufValid;
	char     m_wpSortedPosdbListBufValid;
	char     m_termListBufValid;
	char     m_insertableTermsBufValid;
	char     m_scoredInsertableTermsBufValid;
	//char     m_iwfiBufValid; // for holding WordFreqInfo instances
	char     m_wordPosInfoBufValid;
	char     m_recommendedLinksBufValid;
	char     m_tempMsg25PageValid;
	char     m_tempMsg25SiteValid;

	//char     m_queryHashTableValid;
	char     m_queryOffsetTableValid;
	//char     m_socketWriteBufValid;
	//char     m_numBannedOutlinksValid;
	char     m_hopCountValid;
	char     m_isInjectingValid;
	char     m_isImportingValid;
	char     m_metaListCheckSum8Valid;
	char     m_contentValid;
	char     m_filteredContentValid;
	char     m_charsetValid;
	char     m_langVectorValid;
	char     m_langIdValid;
	char     m_rootLangIdValid;
	char     m_datedbDateValid;
	char     m_isRSSValid;
	char     m_isSiteMapValid;
	char     m_spiderLinksArgValid;
	char     m_isContentTruncatedValid;
	char     m_xmlValid;
	char     m_linksValid;
	char     m_wordsValid;
	char     m_bitsValid;
	char     m_bits2Valid;
	char     m_posValid;
	char     m_isUrlBadYearValid;
	char     m_phrasesValid;
	//char     m_synonymsValid;
	//char     m_weightsValid;
	char     m_sectionsValid;
	char     m_subSentsValid;
	char     m_osvtValid;
	char     m_nsvtValid;
	//char   m_rvtValid;
	char     m_turkVotingTableValid;
	char     m_turkBitsTableValid;
	char     m_turkBanTableValid;
	char     m_vctabValid;
	char     m_explicitSectionsValid;
	char     m_impliedSectionsValid;
	char     m_sectionVotingTableValid;
	char     m_imageDataValid;
	char     m_imagesValid;
	char     m_msge0Valid;
	char     m_msge1Valid;
	//char     m_msge2Valid;
	//char   m_sampleVectorValid;
	char     m_gigabitHashesValid;
	//char     m_oldsrValid;
	char     m_sreqValid;
	char     m_srepValid;

	bool m_ipValid;
	bool m_firstIpValid;
	bool m_spideredTimeValid;
	//bool m_nextSpiderTimeValid;
	bool m_indexedTimeValid;
	bool m_firstIndexedValid;
	bool m_isInIndexValid;
	bool m_wasInIndexValid;
	bool m_outlinksAddedDateValid;
	bool m_countryIdValid;
	bool m_bodyStartPosValid;
	/*
	bool m_titleWeightValid;
	bool m_headerWeightValid;
	bool m_urlPathWeightValid;
	bool m_externalLinkTextWeightValid;
	bool m_internalLinkTextWeightValid;
	bool m_conceptWeightValid;
	*/
	bool m_httpStatusValid;
	bool m_crawlDelayValid;
	bool m_finalCrawlDelayValid;
	bool m_titleRecKeyValid;
	bool m_adVectorValid;
	bool m_wikiDocIdsValid;
	bool m_catIdsValid;
	bool m_versionValid;
	bool m_indCatIdsValid;
	bool m_dmozTitlesValid;
	bool m_dmozSummsValid;
	bool m_dmozAnchorsValid;
	bool m_dmozInfoValid;
	bool m_rawUtf8ContentValid;
	bool m_expandedUtf8ContentValid;
	bool m_utf8ContentValid;
	bool m_isAllowedValid;
	//bool m_tryAgainTimeDeltaValid;
	//bool m_eliminateMenusValid;
	bool m_redirUrlValid;
	bool m_redirCookieBufValid;
	bool m_metaRedirUrlValid;
	bool m_canonicalRedirUrlValid;
	bool m_statusMsgValid;
	bool m_mimeValid;
	bool m_pubDateValid;
	bool m_hostHash32aValid;
	bool m_hostHash32bValid;
	bool m_indexCodeValid;
	bool m_priorityValid;
	bool m_downloadStatusValid;
	bool m_downloadEndTimeValid;
	bool m_redirErrorValid;
	bool m_domHash32Valid;
	bool m_contentHash32Valid;
	//bool m_tagHash32Valid;
	bool m_tagPairHash32Valid;

	bool m_linkInfo2Valid;
	bool m_spiderLinksValid;
	//bool m_nextSpiderPriorityValid;
	bool m_firstIndexedDateValid;
	bool m_isPermalinkValid;

	bool m_isAdultValid;
	bool m_hasAddressValid;
	bool m_hasTODValid;
	//bool m_hasSiteVenueValid;
	bool m_catRecValid;
	bool m_urlPubDateValid;
	bool m_isUrlPermalinkFormatValid;
	bool m_percentChangedValid;
	bool m_unchangedValid;
	bool m_countTableValid;
	bool m_summaryLangIdValid;
	bool m_tagPairHashVecValid;
	bool m_summaryVecValid;
	bool m_titleVecValid;
	bool m_pageSampleVecValid;
	bool m_postVecValid;
	bool m_dupListValid;
	bool m_likedbListValid;
	bool m_isDupValid;
	bool m_gigabitVectorHashValid;
	bool m_gigabitQueryValid;
	bool m_metaDescValid;
	bool m_metaSummaryValid;
	bool m_metaKeywordsValid;
	bool m_siteSpiderQuotaValid;
	bool m_oldDocValid;
	bool m_extraDocValid;
	bool m_ahrefsDocValid;
	//bool m_contactDocValid;
	bool m_rootDocValid;
	//bool m_gatewayDocValid;
	bool m_oldMetaListValid;
	bool m_oldTitleRecValid;
	bool m_rootTitleRecValid;
	//bool m_contactTitleRecValid;
	bool m_isIndexedValid;
	bool m_hasContactInfoValid;
	bool m_isContactyValid;
	bool m_contactInfoTagRecValid;
	bool m_addressesValid;
	bool m_contactAddressesValid;
	bool m_emailBufValid;
	//bool m_contactUsLinkValid;
	//bool m_aboutUsLinkValid;
	//bool m_contactLinksValid;
	bool m_siteNumInlinksValid;
	//bool m_siteNumInlinksUniqueIpValid;//FreshValid;
	//bool m_siteNumInlinksUniqueCBlockValid;//sitePopValid
	//bool m_siteNumInlinksTotalValid;
	bool m_siteNumInlinks8Valid;
	bool m_siteLinkInfoValid;
	bool m_isWWWDupValid;
	bool m_linkInfo1Valid;
	bool m_linkSiteHashesValid;
	//bool m_dateParse2Valid;
	bool m_simpleDatesValid;
	bool m_datesValid;
	bool m_sectionsReplyValid;
	bool m_sectionsVotesValid;
	bool m_sectiondbDataValid;
	bool m_placedbDataValid;
	bool m_siteHash64Valid;
	bool m_siteHash32Valid;
	bool m_httpReplyValid;
	bool m_contentTypeValid;
	bool m_isBinaryValid;
	bool m_priorityQueueNumValid;
	bool m_outlinkTagRecVectorValid;
	bool m_outlinkIpVectorValid;
	bool m_hasNoIndexMetaTagValid;
	bool m_hasUseFakeIpsMetaTagValid;
	bool m_outlinkIsIndexedVectorValid;
	bool m_isSiteRootValid;
	bool m_wasContentInjectedValid;
	bool m_outlinkHopCountVectorValid;
	//bool m_isSpamValid;
	bool m_isFilteredValid;
	bool m_urlFilterNumValid;
	bool m_numOutlinksAddedValid;
	bool m_baseUrlValid;
	bool m_replyValid;
	bool m_recycleDiffbotReplyValid;
	bool m_diffbotReplyValid;
	bool m_tokenizedDiffbotReplyValid;
	//bool m_diffbotUrlCrawlPatternMatchValid;
	//bool m_diffbotUrlProcessPatternMatchValid;
	//bool m_diffbotPageProcessPatternMatchValid;
	//bool m_useDiffbotValid;
	//bool m_diffbotApiNumValid;
	bool m_diffbotApiUrlValid;
	bool m_diffbotTitleHashBufValid;
	bool m_crawlInfoValid;
	bool m_isPageParserValid;
	bool m_imageUrlValid;
	bool m_imageUrl2Valid;
	bool m_matchOffsetsValid;
	bool m_queryValid;
	bool m_diffbotProxyReplyValid;
	bool m_matchesValid;
	bool m_dbufValid;
	bool m_titleValid;
	bool m_htbValid;
	bool m_collnumValid;
	//bool m_twidsValid;
	bool m_termId32BufValid;
	bool m_termInfoBufValid;
	bool m_newTermInfoBufValid;
	bool m_summaryValid;
	bool m_gsbufValid;
	bool m_spiderStatusDocMetaListValid;
	bool m_isCompromisedValid;
	bool m_isNoArchiveValid;
	//bool m_isVisibleValid;
	//bool m_clockCandidatesTableValid;
	//bool m_clockCandidatesDataValid;
	bool m_titleRecBufValid;
	bool m_isLinkSpamValid;
	bool m_isErrorPageValid;
	bool m_isHijackedValid;
	bool m_dupHashValid;
	bool m_exactContentHash64Valid;
	bool m_looseContentHash64Valid;
	bool m_jpValid;

	char m_isSiteMap;

	// shadows
	char m_isRSS2;
	char m_isPermalink2;
	char m_isAdult2;
        char m_spiderLinks2;
	char m_isContentTruncated2;
	char m_isLinkSpam2;
	bool m_hasAddress2;
	bool m_hasTOD2;
	//bool m_hasSiteVenue2;
	char m_hasContactInfo2;
	char m_isSiteRoot2;

	// DO NOT add validity flags below this line!
	char     m_VALIDEND;

	// more stuff
	//char *m_utf8Content;
	//int32_t m_utf8ContentLen;
	CatRec m_catRec;
	// use this stuff for getting wiki docids that match our doc's gigabits
	//Query m_wq; 
	//SearchInput m_si;
	//Msg40 m_msg40;
	//DateParse2 m_dateParse2;
	bool m_printedMenu;
	Dates m_dates;
	//HashTableX m_clockCandidatesTable;
	//SafeBuf m_cctbuf;
	float m_ageInDays;
	int32_t m_urlPubDate;
	//int32_t m_urlAge;
	char m_isUrlPermalinkFormat;
	uint8_t m_summaryLangId;
	int32_t m_tagPairHashVec[MAX_TAG_PAIR_HASHES];
	int32_t m_tagPairHashVecSize;
	int32_t m_summaryVec [SAMPLE_VECTOR_SIZE/4];
	int32_t m_summaryVecSize;
	int32_t m_titleVec [SAMPLE_VECTOR_SIZE/4];
	int32_t m_titleVecSize;
	int32_t m_pageSampleVec[SAMPLE_VECTOR_SIZE/4];
	int32_t m_pageSampleVecSize;
	int32_t m_postVec[POST_VECTOR_SIZE/4];
	int32_t m_postVecSize;
	float m_tagSimilarity;
	float m_gigabitSimilarity;
	float m_pageSimilarity;
	float m_percentChanged;
	bool  m_unchanged;
	// what docids are similar to us? docids are in this list
	RdbList m_dupList;
	RdbList m_likedbList;
	uint64_t m_dupHash;
	int64_t m_exactContentHash64;
	int64_t m_looseContentHash64;
	Msg0 m_msg0;
	Msg5 m_msg5;
	char m_isDup;
	int64_t m_docIdWeAreADupOf;
	int32_t m_ei;
	int32_t m_lastLaunch;
	Msg22Request m_msg22Request;
	Msg22Request m_msg22Requestc;
	Msg22 m_msg22a;
	Msg22 m_msg22b;
	Msg22 m_msg22c;
	Msg22 m_msg22d;
	Msg22 m_msg22e;
	Msg22 m_msg22f;
	//int32_t m_collLen;
	uint32_t m_gigabitVectorHash;
	char m_gigabitQuery [XD_GQ_MAX_SIZE];
	int32_t m_gigabitHashes [XD_MAX_GIGABIT_HASHES];
	int32_t m_gigabitScores [XD_MAX_GIGABIT_HASHES];
	char *m_gigabitPtrs  [XD_MAX_GIGABIT_HASHES];
	// for debug printing really
	class GigabitInfo *m_top[100];
	int32_t               m_numTop;
	//char  m_metaDesc[1025];
	//char  m_metaKeywords[1025];
	// these now reference directly into the html src so our 
	// WordPosInfo::m_wordPtr algo works in seo.cpp
	char *m_metaDesc;
	int32_t  m_metaDescLen;
	char *m_metaSummary;
	int32_t  m_metaSummaryLen;
	char *m_metaKeywords;
	int32_t  m_metaKeywordsLen;
	int32_t  m_siteSpiderQuota;
	//int32_t m_numBannedOutlinks;
	class XmlDoc *m_oldDoc;
	class XmlDoc *m_extraDoc;
	class XmlDoc *m_ahrefsDoc;
	//class XmlDoc *m_contactDoc;
	class XmlDoc *m_rootDoc;
	//class XmlDoc *m_gatewayDoc;
	RdbList m_oldMetaList;
	char   *m_oldTitleRec;
	int32_t    m_oldTitleRecSize;
	char   *m_rootTitleRec;
	int32_t    m_rootTitleRecSize;
	//char   *m_contactTitleRec;
	//int32_t    m_contactTitleRecSize;
	char    m_isIndexed;

	// confusing, i know! these are used exclsusively by
	// getNewSpiderReply() for now
	char m_isInIndex;
	char m_wasInIndex;

	bool m_oldDocExistedButHadError;

	Msg8a   m_msg8a;
	char   *m_tagdbColl;
	int32_t    m_tagdbCollLen;
	Addresses m_addresses;

	Address *m_contactAddresses[MAX_CONTACT_ADDRESSES];
	int32_t     m_numContactAddresses;

	char     m_isContacty;

	//Url     m_contactUsLink;
	//Url     m_aboutUsLink;
	/*
	char *m_contactLinks     [MAX_CONTACT_OUTLINKS];
	int32_t  m_contactLens      [MAX_CONTACT_OUTLINKS];
	int32_t  m_contactScores    [MAX_CONTACT_OUTLINKS];
	int32_t  m_contactFlags     [MAX_CONTACT_OUTLINKS];
	char  m_contactProcessed [MAX_CONTACT_OUTLINKS];
	char *m_contactText      [MAX_CONTACT_OUTLINKS];
	char *m_contactTextEnd   [MAX_CONTACT_OUTLINKS];
	int32_t  m_minContactScore;
	int32_t  m_minContactIndex;
	int32_t  m_numContactLinks;
	*/
	Url   m_extraUrl;
	//int32_t m_siteNumInlinksFresh;
	//int32_t m_sitePop;
	uint8_t m_siteNumInlinks8;
	//int32_t m_siteNumInlinks;
	LinkInfo m_siteLinkInfo;
	SafeBuf m_mySiteLinkInfoBuf;
	SafeBuf m_myPageLinkInfoBuf;
	SafeBuf m_myTempLinkInfoBuf;
	char m_isInjecting;
	char m_isImporting;
	char m_useFakeMime;
	char m_useSiteLinkBuf;
	char m_usePageLinkBuf;
	char m_printInXml;
	//Msg25 m_msg25;
	SafeBuf m_tmpBuf11;
	SafeBuf m_tmpBuf12;
	Multicast m_mcast11;
	Multicast m_mcast12;
	Msg25 *m_tempMsg25Page;
	Msg25 *m_tempMsg25Site;
	// for page or for site?
	Msg25 *getAllInlinks ( bool forSite );
	// lists from cachedb for msg25's msg20 replies serialized
	RdbList m_siteReplyList;
	RdbList m_pageReplyList;
	bool m_checkedCachedbForSite;
	bool m_checkedCachedbForPage;
	bool m_triedToAddWordPosInfoToCachedb;
	bool m_calledMsg25ForSite;
	bool m_calledMsg25ForPage;
	//void (* m_masterLoopWrapper) (void *state);
	MsgC m_msgc;
	bool m_isAllowed;
	bool m_forwardDownloadRequest;
	bool m_isChildDoc;
	class XmlDoc *m_parentDocPtr;
	Msg13 m_msg13;
	Msg13Request m_msg13Request;
	Msg13Request m_diffbotProxyRequest;
	ProxyReply *m_diffbotProxyReply;
	bool m_isSpiderProxy;
	// for limiting # of iframe tag expansions
	int32_t m_numExpansions;
	char m_newOnly;
	//int32_t m_tryAgainTimeDelta;
	//int32_t m_sameIpWait;
	//int32_t m_sameDomainWait;
	//int32_t m_maxSpidersPerDomain;
	char m_isWWWDup;
	char m_calledMsg0b;
	Url  m_tmpUrl;

	SafeBuf m_tmpsb1;
	SafeBuf m_tmpsb2;
	SafeBuf m_turkBuf;
	SafeBuf m_linkSiteHashBuf;
	SafeBuf m_linkdbDataBuf;
	SafeBuf m_langVec;
	Msg0 m_msg0b;
	class RdbList *m_ulist;
	void *m_hack;
	class XmlDoc *m_hackxd;
	//class LinkInfo *m_linkInfo1Ptr;
	char     *m_linkInfoColl;
	//char m_injectedReply;
	//int32_t m_minInlinkerHopCount;
	//class LinkInfo *m_linkInfo2Ptr;
	SiteGetter m_siteGetter;
	int64_t  m_siteHash64;
	//char *m_site;
	//int32_t m_siteLen;
	//Url m_siteUrl;
	int32_t m_siteHash32;
	char *m_httpReply;
	//char m_downloadAttempted;
	char m_incrementedAttemptsCount;
	char m_incrementedDownloadCount;
	char m_redirectFlag;
	//char m_isScraping;
	//char m_throttleDownload;
	char m_spamCheckDisabled;
	char m_useRobotsTxt;
	int32_t m_robotsTxtLen;
	int32_t m_httpReplySize;
	int32_t m_httpReplyAllocSize;
	char m_isBinary;
	char *m_filteredContent;
	int32_t m_filteredContentLen;
	char *m_filter;
	int32_t m_filteredContentAllocSize;
	int32_t m_filteredContentMaxSize;
	char m_calledThread;
	int32_t m_errno;
	//class CollectionRec *m_cr;
	//int32_t m_utf8ContentAllocSize;
	int32_t m_hostHash32a;
	int32_t m_hostHash32b;
	int32_t m_domHash32;
	int32_t m_priorityQueueNum;

	// this points into m_msge0 i guess
	//class TagRec **m_outlinkTagRecVector;
	Msge0 m_msge0;

	// this points into m_msge1 i guess
	int32_t *m_outlinkIpVector;
	SafeBuf m_outlinkTagRecPtrBuf;
	SafeBuf m_fakeIpBuf;
	char m_hasNoIndexMetaTag;
	char m_hasUseFakeIpsMetaTag;
	Msge1 m_msge1;
	TagRec **m_outlinkTagRecVector;
	SafeBuf m_fakeTagRecPtrBuf;
	TagRec m_fakeTagRec;

	//
	// diffbot parms for indexing diffbot's json output
	//
	XmlDoc *m_dx;
	char *m_diffbotObj;
	SafeBuf m_diffbotReply;
	SafeBuf m_v3buf;
	SafeBuf *m_tokenizedDiffbotReplyPtr;
	SafeBuf  m_tokenizedDiffbotReply;
	int32_t m_diffbotReplyError;
	bool m_recycleDiffbotReply;
	//bool m_diffbotUrlCrawlPatternMatch;
	//bool m_diffbotUrlProcessPatternMatch;
	//bool m_diffbotPageProcessPatternMatch;
	//int32_t m_diffbotApiNum;
	//bool m_useDiffbot;
	// url to access diffbot with
	SafeBuf m_diffbotApiUrl;
	SafeBuf m_diffbotUrl; // exact url used to fetch reply from diffbot

	bool *getRecycleDiffbotReply ( ) ;
	SafeBuf *getTokenizedDiffbotReply ( ) ;
	SafeBuf *getDiffbotReply ( ) ;
	bool doesUrlMatchDiffbotCrawlPattern() ;
	//bool doesUrlMatchDiffbotProcessPattern() ;
	bool doesPageContentMatchDiffbotProcessPattern() ;
	int32_t *getDiffbotTitleHashes ( int32_t *numHashes ) ;
	char *hashJSONFields ( HashTableX *table );
	char *hashJSONFields2 ( HashTableX *table , HashInfo *hi , Json *jp ,
				bool hashWithoutFieldNames ) ;

	char *hashXMLFields ( HashTableX *table );
	int32_t *reindexJSONObjects ( int32_t *newTitleHashes , 
				      int32_t numNewHashes ) ;
	int32_t *nukeJSONObjects ( int32_t *newTitleHashes , 
				   int32_t numNewHashes ) ;
	int32_t *redoJSONObjects ( int32_t *newTitleHashes , 
				   int32_t numNewHashes ,
				   bool deleteFromIndex ) ;

	int32_t m_joc;
	SafeBuf m_diffbotTitleHashBuf;

	Json *getParsedJson();
	// object that parses the json
	Json m_jp;


	//EmailInfo m_emailInfo;

	//
	// functions and vars for the seo query matching tool
	//
	bool loadTitleRecFromDiskOrSpider();
	//SafeBuf *getSEOQueryInfo ( );
	HashTableX *getTermIdBufDedupTable32();
	//int32_t  *getTopWordsVector( bool includeSynonyms );
	SafeBuf *getTermId32Buf();
	SafeBuf *getTermInfoBuf();
	SafeBuf *getNewTermInfoBuf();
	SafeBuf *getMatchingQueryBuf();
	SafeBuf *getQueryLinkBuf(SafeBuf *docIdListBuf,bool doMatchingQueries);
	//SafeBuf *getMatchingQueriesScored();
	SafeBuf *getMatchingQueriesScoredForFullQuery();
	SafeBuf *getRelatedDocIds();
	SafeBuf *getRelatedDocIdsScored();
	SafeBuf *getTopMatchingQueryBuf();
	bool     addRelatedDocIdInfo ( int64_t docId ,
				       int32_t queryNum , 
				       float score ,
				       int32_t  rank ,
				       int32_t  siteHash26 ) ;
	bool     setRelatedDocIdWeightAndRank ( class RelatedDocId *rd );
	SafeBuf *getRelatedDocIdsWithTitles();
	bool     setRelatedDocIdInfoFromMsg20Reply ( class RelatedDocId *rd ,
						     class Msg20Reply *reply );

	SafeBuf *getRelatedQueryBuf();
	//SafeBuf *getRelatedQueryLinksModPart ( int32_t modPart );

	bool addTermsFromQuery ( char *queryStr,
				 uint8_t queryLangId,
				 int32_t gigablastTraffic,
				 int32_t googleTraffic,
				 int32_t hackqoff,
				 class SafeBuf *tmpBuf , 
				 class HashTableX *scoreTable ,
				 class HashTableX *topWordsTable ,
				 float imp,
				 bool isRelatedQuery ) ;

	bool sortTermsIntoBuf ( class HashTableX *scoreTable ,
				class SafeBuf *tmpBuf ,
				class SafeBuf *missingTermBuf ) ;


	SafeBuf *getMissingTermBuf ();
	SafeBuf *getMatchingTermBuf ();
	SafeBuf *getTermIdSortedPosdbListBuf();
	SafeBuf *getWordPosSortedPosdbListBuf();
	SafeBuf *getTermListBuf(); // list of posdb termlists for caching
	SafeBuf *getWordPosInfoBuf ( ) ;


	//bool     sendBin ( int32_t i );
	//bool     scoreDocIdRestrictedQueries(class Msg99Reply **replyPtrs,
	//				     class QueryLink  *linkPtrs,
	//				     int32_t  numPtrs );

	// private like functions
	bool   addUniqueWordsToBuf ( SafeBuf *termInfoBuf,
				     HashTableX *dedupTable , 
				     HashTableX *filterTable , 
				     HashTableX *minCountTable ,
				     bool storeCounts,
				     Words *words ,
				     bool includeSynonyms );
	//void gotMsg99Reply ( UdpSlot *slot );
	//void gotMsg98Reply ( UdpSlot *slot );
	void gotMsg95Reply ( UdpSlot *slot );
	//void gotMsg3aReplyForMainUrl  ( );
	void gotMsg3aReplyForFullQuery( );
	//void gotMsg3aReplyForFullQueryCached ( char *cachedRec ,
	//				       class Msg99Reply *qp );
	//void gotMsg3aReplyForRelQuery ( class Msg3a *msg3a );
	void gotMsg3fReply ( class Bin *bin );
	//void pumpSocketWriteBuf ( );
	//HashTableX *getMatchingQueryHashTable();
	HashTableX *getMatchingQueryOffsetTable();

	int32_t getNumInsertableTerms ( );
	class SafeBuf *getInsertableTerms ( );
	class SafeBuf *getScoredInsertableTerms ( );
	//class SafeBuf *getInsertableWordFreqInfoBuf ();
	bool processMsg95Replies();
	void setWordPosInfosTrafficGain ( class InsertableTerm *it );
	int32_t getTrafficGain( class QueryChange *qc ) ;
	// print in xml
	bool printScoredInsertableTerms ( SafeBuf *sbuf ) ;


	HashTableX m_tidTable32;
	//int32_t *m_twids;
	//int32_t  m_numTwids;
	SafeBuf m_termId32Buf;
	SafeBuf m_termInfoBuf;
	SafeBuf m_newTermInfoBuf;
	//int32_t  m_maxQueries;
	//int32_t  m_maxRelatedQueries;
	//int32_t  m_maxRelatedUrls;
	//int32_t  m_numMsg99Requests;
	//int32_t  m_numMsg98Requests;
	//int32_t  m_numMsg99Replies;
	//int32_t  m_numMsg98Replies;
	//char *m_msg99ReplyPtrs [MAX_HOSTS];
	//int32_t  m_msg99ReplySizes[MAX_HOSTS];
	//int32_t  m_msg99ReplyAlloc[MAX_HOSTS];
	//int32_t  m_msg99HostIds   [MAX_HOSTS];
	char *m_msg95ReplyPtrs [MAX_HOSTS];
	int32_t  m_msg95ReplySizes[MAX_HOSTS];
	//HashTableX m_queryHashTable;
	HashTableX m_queryOffsetTable;
	HashTableX m_tmpTable;
	HashTableX m_fullQueryDedup;
	//SafeBuf m_twbuf;
	//SafeBuf m_queryPtrs;
	SafeBuf m_matchingQueryBuf;
	SafeBuf m_matchingQueryStringBuf;
	SafeBuf m_relatedQueryBuf;
	SafeBuf m_relatedQueryStringBuf;
	SafeBuf m_docIdListBuf;
	SafeBuf m_queryOffsets;
	SafeBuf m_extraQueryBuf;
	//SafeBuf m_socketWriteBuf;
	SafeBuf m_relatedDocIdBuf;
	SafeBuf m_relatedTitleBuf;
	SafeBuf m_commonQueryNumBuf;
	SafeBuf m_topMatchingQueryBuf;
	HashTableX m_rdtab;

	// related query algo stuff
	SafeBuf m_queryLinkBuf;
	SafeBuf m_queryLinkStringBuf;
	char   *m_msg8eReply    [MAX_HOSTS];
	int32_t    m_msg8eReplySize[MAX_HOSTS];
	int32_t    m_numMsg8eRequests;
	int32_t    m_numMsg8eReplies;
	//bool    m_launchedAll;
	int64_t m_tlbufTimer;

	SafeBuf m_missingTermBuf;
	SafeBuf m_matchingTermBuf;
	//SafeBuf m_queryRelBuf;
	//SafeBuf m_relPtrs;
	SafeBuf m_sortedPosdbListBuf;
	SafeBuf m_wpSortedPosdbListBuf;
	SafeBuf m_termListBuf;
	SafeBuf m_insertableTermsBuf;
	//SafeBuf m_iwfiBuf;
	SafeBuf m_wordPosInfoBuf;
	//SafeBuf m_msg20ReplyPtrBuf;
	SafeBuf m_recommendedLinksBuf;
	SafeBuf m_tmpMsg0Buf;
	SafeBuf m_msg20Array;
	SafeBuf m_newLinkerBuf;

	//Msg17  m_msg17;
	//key_t  m_cacheKey;
	//char  *m_cacheRec;
	//int32_t   m_cacheRecSize;
	//bool   m_triedCache;
	
	//class TopDocIds *m_topDocIdsBuf;
	//int32_t             m_topDocIdsBufSize;
	SafeBuf m_topDocIdsBuf;
	//class TopDocIds *m_nextAvailTopDocIds;
	//int32_t m_nextAvailTopDocIdsOffset;

	//int32_t    m_maxFullQueries;
	//XmlDoc *m_newxd;
	//XmlDoc *m_newxd2;
	//bool    m_newxd2Blocked;
	//HashTableX m_tmpDupTable;
	//class Msg20  *m_newMsg20;
	Msg3a  *m_msg3a;
	Query  *m_query3a;
	int32_t m_numMsg3aRequests;
	int32_t m_numMsg3aReplies;

	int32_t m_numMsg3fRequests;
	int32_t m_numMsg3fReplies;
	int32_t m_numMsg4fRequests;
	int32_t m_numMsg4fReplies;
	bool m_sentMsg4fRequests;
	bool m_matchesCrawlPattern;
	class UdpSlot *m_savedSlot;
	int32_t m_numMsg95Requests;
	int32_t m_numMsg95Replies;
	int32_t m_qcursor;
	char m_seoDebug;
	char m_progressBar;
	bool m_readFromCachedb;
	bool m_writeToCachedb;
	//bool m_setForReplyPtrs;
	//bool m_setForLinkPtrs;

	SafeBuf *getRecommendedLinksBuf ( );
	bool processLinkInfoMsg20Reply ( class Msg25 *msg25 );
	bool printRecommendedLinksBuf ( class SafeBuf *sb ) ;

	// recommendedlinksbuf vars and functions
	int32_t m_numLinkRequestsOut;
	int32_t m_numLinkRequestsIn;
	int32_t m_hadLinkInfoError;
	int32_t m_numMsg20sIn;
	int32_t m_numMsg20sOut;
	int32_t m_numValidMsg20s;
	int32_t m_titleCursor;
	int32_t m_msg20Phase;
	int32_t m_recommendedLinkError;
	SafeBuf *lookupTitles();
	bool gotLinkerTitle ( class Msg20 *msg20 );

	// 1 *current* bin per host!
	//class Bin *m_currentBinPtrs[MAX_HOSTS];
	//int32_t       m_binError;
	//int32_t       m_msg98ReplyError;
	//int32_t       m_binErrorForReplyPtrs;
	//int32_t       m_binErrorForLinkPtrs;
	HashTableX m_qstringTable;

	// flow flags
	bool m_printedQueries;
	bool m_printedRelatedDocIds;
	bool m_printedRelatedQueries;
	bool m_printedScoredInsertableTerms;
	bool m_printedRecommendedLinks;
	bool m_loggedMsg3;
	int64_t m_lastPrintedDocId;
	//bool m_docIndexed;
	//bool m_sentMsg99Requests;
	bool m_didSet3;
	//bool m_didSet3b;
	bool m_registeredSocketCallback;
	// the caller's socket the expect the xml reply on
	TcpSocket *m_seoSocket;
	TcpSocket *m_hackSocket;
	bool m_doingSEO;


	bool clientClosedConnection ( );
	bool m_hadMatchError;
	bool m_clientClosed;
	bool m_lastCheckTime;
	int32_t m_msg3aErrno ;
	bool m_computedMetaListCheckSum;

	// cachedb related args
	//bool m_seoInfoSetFromCache;
	bool m_checkedCachedb;
	bool m_processedCachedbReply;
	//bool m_storedIntoCachedb;
	RdbList m_cacheList;
	//SafeBuf m_msg99ReplyBuf;
	SafeBuf m_queryChangeBuf;
	SafeBuf m_queryLogBuf;
	//SafeBuf m_itStrBuf;
	SafeBuf m_debugScoreInfoBuf;
	SafeBuf m_origScoreInfoBuf;
	RdbList m_storeList;
	Msg1    m_msg1;
	bool    m_allHashed;
	bool checkCachedb ( );
	bool storeScoredInsertableTermsIntoCachedb ( ) ;
	bool storeRelatedQueriesIntoCachedb ( ) ;
	bool storeRelatedDocIdsIntoCachedb ( ) ;
	bool storeMatchingQueriesIntoCachedb ( ) ; // only the top 1000 or so
	bool storeMissingTermBufIntoCachedb ( );
	bool storeWordPosInfoBufIntoCachedb ( );
	bool storeRecommendedLinksBuf ( );

	// cursors
	int32_t    m_socketWriteBufSent;
	int32_t    m_queryNum;
	int32_t    m_rdCursor;
	int32_t    m_relatedNum;
	int32_t    m_numRelatedAdded;

	// for getRelatedDocIdsWithTitles() launching msg20s
	int32_t m_relatedDocIdError;
	int32_t m_numMsg20Replies;
	int32_t m_numMsg20Requests;
	SafeBuf m_msg20Buf;

	// this points into m_msge2
	//char *m_outlinkIsIndexedVector;
	//Msge2 m_msge2;

	bool m_doneWithAhrefs;
	bool m_useAhrefs;
	bool m_reallyInjectLinks;
	int32_t m_downloadLevel;
	int32_t m_numRegExs;
	//char m_isSiteRoot;
	int8_t *m_outlinkHopCountVector;
	int32_t  m_outlinkHopCountVectorSize;
	//char m_isSpam;
	char m_isUrlBadYear;
	char m_isFiltered;
	int32_t m_urlFilterNum;
	int32_t m_numOutlinksAdded;
	int32_t m_numOutlinksAddedFromSameDomain;
	int32_t m_numOutlinksFiltered;
	int32_t m_numOutlinksBanned;
	int32_t m_numRedirects;
	bool m_isPageParser;
	Url m_baseUrl;
	Msg20Reply m_reply;
	Msg20Request *m_req;
	//char *m_gsbuf;
	SafeBuf m_surroundingTextBuf;
	SafeBuf m_rssItemBuf;
	SafeBuf m_gsbuf;
	//int32_t  m_gsbufSize;
	//int32_t  m_gsbufAllocSize;
	char *m_note;
	char *m_imageUrl;
	char *m_imageUrl2;
	//char  m_imageUrlBuf[100];
	SafeBuf m_imageUrlBuf;
	SafeBuf m_imageUrlBuf2;
	//int32_t  m_imageUrlSize;
	MatchOffsets m_matchOffsets;
	Query m_query;
	Matches m_matches;
	// meta description buf
	int32_t m_dbufSize;
	char m_dbuf[1024];
	SafeBuf m_htb;
	Title m_title;
	Summary m_summary;
	char m_isCompromised;
	char m_isNoArchive;
	char m_isErrorPage;
	char m_isHijacked;
	//char m_isVisible;
	//char m_dmozBuf[12000];
	SafeBuf m_dmozBuf;
	int32_t m_numDmozEntries;

	// stuff
	char *m_statusMsg;
	Msg4  m_msg4;
	Msg8b m_msg8b;
	bool  m_incCount;
	bool  m_decCount;

	bool  m_deleteFromIndex;

	// ptrs to stuff
	//char *m_titleRec;
	SafeBuf m_titleRecBuf;
	//int32_t  m_titleRecSize;
	//bool  m_freeTitleRec;
	//int32_t  m_titleRecAllocSize;
	key_t   m_titleRecKey;

	// for isDupOfUs()
	char *m_dupTrPtr;
	int32_t  m_dupTrSize;

	// parse these out of spider rec
	/*
	int32_t  m_retryNum                ;
	int32_t  m_spiderRecPriority       ;
	bool  m_spiderRecIsNew          ;
	int32_t  m_spiderRecSiteNumInlinks ;
	int32_t  m_spiderRecRetryCount     ;
	int32_t  m_spiderRecHopCount       ;
	key_t m_spiderRecKey            ;
	bool  m_spiderRecForced         ;
	int32_t  m_spiderRecTime           ;
	int32_t  m_srDataSize ;
	char  m_srData [ MAX_SPIDERREC_SIZE ];
	*/

	key_t     m_doledbKey;
	SpiderRequest m_sreq;
	SpiderReply   m_srep;//newsr;

	// bool flags for what procedures we have done
	bool m_checkedUrlFilters;
	
	bool m_listAdded                ;
	bool m_listFlushed              ;
	bool m_check1                   ;
	bool m_check2                   ;
	bool m_prepared                 ;
	bool m_updatedCounts            ;
	bool m_updatedCounts2           ;
	//bool m_updatedTagdb1            ;
	//bool m_updatedTagdb2            ;
	//bool m_updatedTagdb3            ;
	//bool m_updatedTagdb4            ;
	//bool m_updatedTagdb5            ;
	bool m_copied1                  ;
	bool m_updatingSiteLinkInfoTags ;
	bool m_addressSetCalled         ;

	//bool m_calledMsg22a             ;
	//bool m_calledMsg22b             ;
	//bool m_calledMsg22c             ;
	int64_t m_calledMsg22d             ;
	bool m_didDelay                 ;
	bool m_didDelayUnregister       ;
	bool m_calledMsg22e             ;
	bool m_calledMsg22f             ;
	bool m_calledMsg25              ;
	bool m_calledMsg25b             ;
	bool m_calledMsg8b              ;
	bool m_calledMsg40              ;
	bool m_calledSections           ;
	bool m_firstEntry               ;
	bool m_firstEntry2              ;
	bool m_launchedSpecialMsg8a     ;
	bool m_launchedMsg8a2           ;
	bool m_loaded                   ;

	// used for getHasContactInfo()
	bool m_processed0               ;

	// a lock to prevent infinite loops
	//bool m_checkForRedir            ;

	bool m_processedLang            ;

	bool m_doingConsistencyCheck ;

	int32_t    m_langIdScore;
	//int32_t    m_rootLangIdScore;
	//uint8_t m_rootLangId;

	// used for getting contact info
	//bool m_triedRoot                ;
	//int32_t m_winner                   ;

	int32_t m_dist;

	// the tags in this tagRec are just contact info based tags and
	// created in the addContactInfo() function. also, in that same
	// function we add/sub the tags in m_citr to the m_newTagRec tag rec.
	//TagRec m_citr ;

	char m_emailBuf[EMAILBUFSIZE];
	int32_t m_numOfficialEmails;

	// use to store a \0 list of "titles" of the root page so we can
	// see which if any are the venue name, and thus match that to
	// addresses of the venue on the site, and we can use those addresses
	// as default venue addresses when no venues are listed on a page
	// on that site.
	char   m_rootTitleBuf[ROOT_TITLE_BUF_MAX];
	int32_t   m_rootTitleBufSize;

	// . this is filtered
	// . certain punct is replaced with \0
	char   m_filteredRootTitleBuf[ROOT_TITLE_BUF_MAX];
	int32_t   m_filteredRootTitleBufSize;

	// like m_rootTitleBuf but for the current page
	char   m_titleBuf[ROOT_TITLE_BUF_MAX];
	int32_t   m_titleBufSize;


	bool m_setTr                    ;
	//bool m_checkedRobots            ;
	bool m_triedTagRec              ;
	bool m_didGatewayPage           ;
	bool m_didQuickDupCheck         ;

	void (* m_masterLoop) ( void *state );
	void  * m_masterState;

	void (* m_callback1) ( void *state );	
	bool (* m_callback2) ( void *state );	
	void  *m_state;


	//void (* m_injectionCallback) ( void *state );	
	//void   *m_injectionState;

	// flags for spider
	//bool m_isAddUrl;
	//bool m_forceDelete;
	bool m_didDelete;

	bool m_skipIframeExpansion;

	// this is non-zero if we decided not to index the doc
	int32_t m_indexCode;

	// the spider priority
	int32_t m_priority;

	// the download error, like ETIMEDOUT, ENOROUTE, etc.
	int32_t m_downloadStatus;

	// . when the download was completed. will be zero if no download done
	// . used to set SpiderReply::m_downloadEndTime because we need
	//   high resolution for that so we can dole out the next spiderrequest
	//   from that IP quickly if the sameipwait is like 500ms.
	int64_t m_downloadEndTime;

	//char *m_metaListEnd;
	int32_t  m_metaListAllocSize;
	char *m_p;
	char *m_pend;

	int32_t  m_maxCacheAge;

	// a list of 32-bit ints followed by a zero 32-bit int to terminate
	int64_t m_adIds [ XD_MAX_AD_IDS ];
	//char *m_adVector;// [XMLDOC_MAX_AD_IDS];
	//int32_t  m_adVectorSize;

	char     *m_wikiqbuf;
	int32_t      m_wikiqbufSize;
	int64_t m_wikiDocIds [ MAX_WIKI_DOCIDS ];
	rscore_t  m_wikiScores [ MAX_WIKI_DOCIDS ];

	bool      m_registeredSleepCallback;
	bool      m_addedNegativeDoledbRec;
	
	bool          m_hashedTitle;
	bool          m_hashedMetas;

	int32_t          m_niceness;

	bool m_usePosdb     ;
	//bool m_useDatedb    ;
	bool m_useClusterdb ;
	bool m_useLinkdb    ;
	bool m_useSpiderdb  ;
	bool m_useTitledb   ;
	bool m_useTagdb     ;
	bool m_usePlacedb   ;
	//bool m_useTimedb    ;
	bool m_useSectiondb ;
	//bool m_useRevdb     ;
	bool m_useSecondaryRdbs ;

	int32_t          m_linkeeQualityBoost;

	SafeBuf *m_pbuf;
	// used by SpiderLoop to set m_pbuf to
	SafeBuf  m_sbuf;
	// store termlist into here if non-null
	bool     m_storeTermListInfo;
	char     m_sortTermListBy;

	SafeBuf m_sectiondbData;
	//char *m_sectiondbData;
	char *m_placedbData;
	//int32_t  m_sectiondbDataSize;
	int32_t  m_placedbDataSize;

	// we now have HashInfo to replace this
	//bool m_inHashNoSplit;

	// store the terms that we hash into this table so that PageParser.cpp
	// can print what was hashed and with what score and what description
	class HashTableX *m_wts;
	HashTableX m_wtsTable;
	SafeBuf m_wbuf;

	// used by addContactInfo() to keep track of what urls we have
	// processed for contact info to avoid re-processing them in the
	// recursive loop thing that we do
	//HashTableX m_pt;

	// Msg25.cpp stores its pageparser.cpp output into this one
	SafeBuf m_pageLinkBuf;
	SafeBuf m_siteLinkBuf;

	SafeBuf m_serpBuf;

	// which set() function was called above to set us?
	bool          m_setFromTitleRec;
	bool          m_setFromSpiderRec;
	bool          m_setFromUrl;
	bool          m_setFromDocId;
	bool          m_freeLinkInfo1;
	bool          m_freeLinkInfo2;
	bool          m_contentInjected;

	bool          m_recycleContent;
	//bool        m_loadFromOldTitleRec;

	char *m_rawUtf8Content;
	int32_t  m_rawUtf8ContentSize;
	int32_t  m_rawUtf8ContentAllocSize; // we overallocate sometimes
	char *m_expandedUtf8Content;
	int32_t  m_expandedUtf8ContentSize;
	char *m_savedp;
	char *m_oldp;
	bool  m_didExpansion;
	SafeBuf m_esbuf;
	SafeBuf m_xbuf;

	//bool m_useIpsTxtFile ;
	//bool m_readFromTestCache ;

	// used by msg13
	class Msg13Request *m_r;

	// Msg20 uses this to stash its TcpSlot
	void *m_slot;

	char *getTestDir();

	bool m_freed;

	bool m_msg4Waiting;
	bool m_msg4Launched;

	// word spam detection
	char *getWordSpamVec ( );
	bool setSpam ( int32_t *profile, int32_t plen , int32_t numWords , 
		       unsigned char *spam );
	int32_t  getProbSpam  ( int32_t *profile, int32_t plen , int32_t step );
	bool m_isRepeatSpammer;
	int32_t m_numRepeatSpam;
	bool m_totallySpammed;

	// frag vector (repeated fragments). 0 means repeated, 1 means not.
	// vector is 1-1 with words in the document body.
	char *getFragVec ( );

	bool injectDoc ( char *url ,
			 class CollectionRec *cr ,
			 char *content ,
			 char *diffbotReply, // usually null
			 bool contentHasMime ,
			 int32_t hopCount,
			 int32_t charset,

			 bool deleteUrl,
			 //char contentType, // CT_HTML, CT_XML
			 char *contentTypeStr, // text/html, text/xml etc.
			 bool spiderLinks ,
			 char newOnly, // index iff new

			 void *state,
			 void (*callback)(void *state) ,

			 uint32_t firstIndexedTime = 0,
			 uint32_t lastSpideredDate = 0 ,
			 int32_t  injectDocIp = 0 ,
			 // for container docs consisting of subdocs to inject
			 char *contentDelim = NULL,
			 char* metadata = NULL,
             uint32_t metadataLen = 0,
             int32_t  payloadLen = -1);


	bool injectLinks  ( HashTableX *linkDedupTable ,
			    HashTableX *domDedupTable ,
			    void *finalState , 
			    void (* finalCallback)(void *));
	bool injectAhrefsLinks();
	bool doInjectLoop ( );
	void doneInjecting ( class XmlDoc *xd );
	int32_t  m_i;
	int32_t  m_blocked;
	HashTableX  m_domDedupTable;
	HashTableX *m_linkDedupTablePtr;
	HashTableX *m_domDedupTablePtr;
	bool m_dedupLinkDomains;
	void *m_finalState;
	void (* m_finalCallback) ( void *state );
	char  m_used[MAX_XML_DOCS];
	class XmlDoc *m_xmlDocs[MAX_XML_DOCS];
	int64_t m_cacheStartTime;
};

// . PageParser.cpp uses this class for printing hashed terms out by calling
//   XmlDoc::print()
// . we store TermInfos into XmlDoc::m_wtsTable, a HashTableX
// . one for each term hashed
// . the key is the termId. dups are allowed
// . the term itself is stored into a separate buffer, m_wbuf, a SafeBuf, so
//   that TermInfo::m_term will reference that and it won't disappear on us
class TermDebugInfo {
 public:
	int32_t      m_termOff;
	int32_t      m_termLen;
	//uint32_t  m_score32;
	int32_t      m_descOff;   // the description offset
	int32_t      m_prefixOff; // the prefix offset, like "site" or "gbadid"
	int64_t m_termId;
	int32_t      m_date;
	bool      m_shardByTermId;

	//float     m_weight;
	char      m_langId;
	char      m_diversityRank;
	char      m_densityRank;
	char      m_wordSpamRank;
	char      m_hashGroup;
	int32_t      m_wordNum;
	int32_t      m_wordPos;
	POSDBKEY  m_key; // key144_t
	//bool      m_isSynonym;
	// 0 = not a syn, 1 = syn from presets,2=wikt,3=generated
	char      m_synSrc;
	int64_t  m_langBitVec64;
	// used for gbsectionhash:xxxx terms to hack in the inner content
	// hash, aka sentHash32 for doing xpath histograms on a site
	//int32_t m_sentHash32;
	//int32_t m_facetVal32;
	// this is copied from Weights::m_rvw or m_rvp
	//float     m_rv[MAX_RULES];
};

// a ptr to HashInfo is passed to hashString() and hashWords()
class HashInfo {
public:
	HashInfo() { 
		m_tt                      = NULL;
		m_prefix                  = NULL;
		m_desc                    = NULL;
		m_date                    = 0;
		// should we do sharding based on termid and not the usual docid???
		// in general this is false, but for checksum we want to shard
		// by the checksum and not docid to avoid having to do a 
		// gbchecksum:xxxxx search on ALL shards. much more efficient.
		m_shardByTermId = false;
		//m_useWeights              = false;
		m_useSynonyms             = false;
		m_hashGroup = -1;
		m_useCountTable = true;
		m_useSections = true;
		m_startDist = 0;
		//	m_facetVal32 = 0;
		// used for sectiondb stuff, but stored in posdb
		//m_sentHash32 = 0;
	};
	class HashTableX *m_tt;
	char             *m_prefix;
	// "m_desc" should detail the algorithm
	char             *m_desc;
	int32_t              m_date;
	char              m_shardByTermId;
	char              m_linkerSiteRank;
	//char              m_useWeights;
	char              m_useSynonyms;
	char              m_hashGroup;
	int32_t              m_startDist;
	//int32_t              m_facetVal32;
	bool              m_useCountTable;
	bool              m_useSections;
};


// g_tt is used for debugging
//extern class TermTable *g_tt;

extern uint8_t score32to8 ( uint32_t score ) ;

extern pid_t g_pid    ;
extern int32_t  g_ticker ;
extern int32_t  g_filterTimeout ;

// as recommended in the "man system" page we use our own
int my_system_r ( char *cmd , int32_t timeout ) ;

// . returns 0 to 100 , the probability of spam for this subprofile
// . a "profile" is an array of all the positions of a word in the document
// . a "position" is just the word #, like first word, word #8, etc...
// . we are passed a subprofile, "profile", of the actual profile
//   because some of the document may be more "spammy" than other parts
// . inlined to speed things up because this may be called multiple times
//   for each word in the document
// . if "step" is 1 we look at every       word position in the profile
// . if "step" is 2 we look at every other word position 
// . if "step" is 3 we look at every 3rd   word position, etc...
inline int32_t XmlDoc::getProbSpam(int32_t *profile, int32_t plen, int32_t step) {

	// you can spam 2 or 1 letter words all you want to
	if ( plen <= 2 ) return 0;

	// if our step is bigger than the profile return 0
	if ( step == plen ) return 0;

	register int32_t avgSpacing, stdDevSpacing;
	int32_t d,dev=0;
	register int32_t i;
	
	for (int32_t j = 0; j < step; j++) {

		// find avg. of gaps between consecutive tokens in subprofile
		// TODO: isn't profile[i] < profile[i+1]??
		int32_t istop = plen-1;
		avgSpacing = 0;
		for (i=0; i < istop; i += step ) 
			avgSpacing += ( profile[i] - profile[i+1] ); 
		// there's 1 less spacing than positions in the profile
		// so we divide by plen-1
		avgSpacing = (avgSpacing * 256) / istop;

		// compute standard deviation of the gaps in this sequence
		stdDevSpacing = 0;
		for (i = 0 ; i < istop; i += step ) {
			d = (( profile[i] - profile[i+1]) * 256 ) - avgSpacing;
			if ( d < 0 ) stdDevSpacing -= d;
			else         stdDevSpacing += d;
		}

		// TODO: should we divide by istop-1 for stdDev??
		stdDevSpacing /= istop;

		// average of the stddevs for all sequences
		dev += stdDevSpacing;
	}

	dev /= step;
	
	// if the plen is big we should expect dev to be big
	// here's some interpolation points:
	// plen >=  2  and  dev<= 0.2  --> 100%
	// plen  =  7  and  dev = 1.0  --> 100%
	// plen  = 14  and  dev = 2.0  --> 100%
	// plen  = 21  and  dev = 3.0  --> 100%
	// plen  = 7   and  dev = 2.0  -->  50%

	// NOTE: dev has been multiplied by 256 to avoid using floats
	if ( dev <= 51.2 ) return 100;  // (.2 * 256)
	int32_t prob = ( (256*100/7) * plen ) / dev;

	if (prob>100) prob=100;

	return prob;

	//if (prob>=0) {
	//	int32_t i;
	//printf("dev=%i,plen=%i,nseq=%i,prob=%i----\n",dev,plen,step,prob);
	//	for (i=0;i<plen;i++)
	//		printf("%i#",profile[i]);
	//	printf("\n");
	//}
}

#endif

