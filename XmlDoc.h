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
#include "IndexList.h"
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
#include "CollectionRec.h"
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

double getTrafficPercent ( long rank ) ;

bool setLangVec ( class Words *words , 
		  class SafeBuf *langBuf , 
		  class Sections *sections ,
		  long niceness ) ;

bool logQueryLogs ( );

// Address.cpp calls this to make a vector from the "place name" for comparing
// to other places in placedb using the computeSimilarity() function. if
// we got a >75% similarity we set the AF_VERIFIED_PLACE_NAME bit in the
// Address::m_flags for that address on the web page.
long makeSimpleWordVector ( char *s, long *vbuf, long vbufSize, long niceness);

// this is used for making the event summary/title vectors as well as in
// Msg40.cpp where it merges events and does not want to repetitively display
// the same summary lines for an event
bool getWordVector ( char *s , 
		     HashTableX *ht , 
		     uint32_t *d ,
		     long *nd ,
		     long ndmax ) ;

bool getDensityRanks ( long long *wids , 
		       long nw,
		       //long wordStart , 
		       //long wordEnd ,
		       long hashGroup ,
		       SafeBuf *densBuf ,
		       Sections *sections ,
		       long niceness );

// diversity vector
bool getDiversityVec ( class Words *words ,
		       class Phrases *phrases ,
		       class HashTableX *countTable ,
		       class SafeBuf *sbWordVec ,
		       //class SafeBuf *sbPhraseVec ,
		       long niceness );

float computeSimilarity ( long   *vec0 , 
			  long   *vec1 ,
			  // corresponding scores vectors
			  long   *s0   , 
			  long   *s1   , 
			  class Query  *q    ,
			  long  niceness ,
			  // only Sections::addDateBasedImpliedSections()
			  // sets this to true right now. if set to true
			  // we essentially dedup each vector, although
			  // the score is compounded into the remaining 
			  // occurence. i'm not sure if that is the right
			  // behavior though.
			  bool dedupVecs = false );

bool isSimilar_sorted ( long   *vec0 , 
			long   *vec1 ,
			long nv0 , // how many longs in vec?
			long nv1 , // how many longs in vec?
			// they must be this similar or more to return true
			long percentSimilar,
			long    niceness ) ;

// this is called by Msg40.cpp to set "top"
long intersectGigabits ( Msg20       **mp          ,   // search results
			 long          nmp         ,
			 uint8_t       langId      ,   // searcher's langId
			 long          maxTop      ,
			 long          docsToScan  ,
			 long          minDocCount , // must be in this # docs
			 class GigabitInfo  *top   ,
			 long          niceness    ) ;

long getDirtyPoints ( char *s , long len , long niceness ) ;

bool storeTerm ( char             *s        ,
                 long              slen     ,
                 long long         termId   ,
                 class HashInfo   *hi       ,
                 long              wordNum  ,
		 long              wordPos  ,
		 char densityRank   ,
		 char diversityRank ,
		 char wordSpamRank  ,
		 char hashGroup ,
		 //bool              isPhrase ,
                 class SafeBuf    *wbuf     ,
                 class HashTableX *wts      ,
		 char              synSrc   ,
		 char              langId   ) ;

// tell zlib to use our malloc/free functions
int gbuncompress ( unsigned char *dest      ,
		   unsigned long *destLen   ,
		   unsigned char *source    ,
		   unsigned long  sourceLen );

int gbcompress   ( unsigned char *dest      ,
		   unsigned long *destLen   ,
		   unsigned char *source    ,
		   unsigned long  sourceLen ,
		   long encoding = ET_DEFLATE);

int gbcompress7  ( unsigned char *dest      ,
		   unsigned long *destLen   ,
		   unsigned char *source    ,
		   unsigned long  sourceLen ,
		   bool compress = true );

int gbuncompress7  ( unsigned char *dest      ,
		     unsigned long *destLen   ,
		     unsigned char *source    ,
		     unsigned long  sourceLen ) ;


uint32_t score8to32 ( uint8_t score8 );

// for Msg13.cpp
char getContentTypeFromContent ( char *p , long niceness ) ;

// . for Msg13.cpp
// . *pend must equal \0
long getContentHash32Fast ( unsigned char *p , 
			    long plen ,
			    long niceness ) ;

uint16_t getCharsetFast ( class HttpMime *mime, 
			  char *url ,
			  char *s , 
			  long slen , 
			  long niceness );

//#define MAX_CONTACT_OUTLINKS 5

#define MAX_CONTACT_ADDRESSES 20
#define EMAILBUFSIZE 512

#define ROOT_TITLE_BUF_MAX 512

// store the subsentences in an array now
class SubSent {
public:
	sentflags_t m_subSentFlags;
	//esflags_t   m_esflags;
	long        m_senta;
	long        m_sentb;
	long        m_subEnding;
	float       m_titleScore;
};

#define MAX_XML_DOCS 4

class XmlDoc {

 public:

	// . variable size rdb records all start with key then dataSize
	// . do not do that here since we compress our record's data!!
	//key_t m_titleRecKey;
	//long  m_dataSize;

	//
	// BEGIN WHAT IS STORED IN THE TITLE REC (Titledb.h)
	//


	// headerSize = this->ptr_firstUrl - this->m_headerSize
	uint16_t  m_headerSize; 
	uint16_t  m_version;
	// these flags are used to indicate which ptr_ members are present:
	uint32_t  m_internalFlags1;
	long      m_ip;
	long      m_crawlDelay;
	// . use this to quickly detect if doc is unchanged
	// . we can avoid setting Xml and Words classes etc...
	long      m_contentHash32;
	// like the above but hash of all tags in TagRec for this url
	long      m_tagHash32;
	long      m_siteNumInlinks;
	long      m_siteNumInlinksUniqueIp; // m_siteNumInlinksFresh
	long      m_siteNumInlinksUniqueCBlock; // m_sitePop;
	time_t    m_spideredTime;
	time_t    m_minPubDate;
	time_t    m_maxPubDate;
	time_t    m_pubDate;    // aka m_datedbDate
	//time_t  m_nextSpiderTime;
	time_t    m_firstIndexedDate;
	time_t    m_outlinksAddedDate;
	uint16_t  m_charset; // the ORIGINAL charset, we are always utf8!
	uint16_t  m_countryId;
	//uint16_t  m_reserved1;//titleWeight;
	//uint16_t  m_reserved2;//headerWeight;
	long      m_siteNumInlinksTotal;
	//uint16_t  m_reserved3;//urlPathWeight;
	uint8_t   m_metaListCheckSum8; // bring it back!!
	char      m_reserved3b;
	uint16_t  m_reserved4;//externalLinkTextWeight;
	uint16_t  m_reserved5;//internalLinkTextWeight;
	uint16_t  m_reserved6;//conceptWeight;

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
	uint16_t  m_wasInjected:1;//eliminateMenus:1;
	uint16_t  m_spiderLinks:1;
	uint16_t  m_isContentTruncated:1;
	uint16_t  m_isLinkSpam:1;
	uint16_t  m_hasAddress:1;
	uint16_t  m_hasTOD:1;
	uint16_t  m_hasSiteVenue:1;
	uint16_t  m_hasContactInfo:1;
	uint16_t  m_isSiteRoot:1;
	uint16_t  m_reserved8;

	char      *ptr_firstUrl;
	char      *ptr_redirUrl;
	//char    *ptr_tagRecData;
	char      *ptr_rootTitleBuf;
	long      *ptr_gigabitHashes;
	long      *ptr_gigabitScores;
	long long *ptr_adVector;
	long long *ptr_wikiDocIds;
	rscore_t  *ptr_wikiScores;
	char      *ptr_imageData;
	long      *ptr_catIds;
	long      *ptr_indCatIds;
	char      *ptr_dmozTitles;
	char      *ptr_dmozSumms;
	char      *ptr_dmozAnchors;
	char      *ptr_utf8Content;
	//char    *ptr_sectionsReply; // votes read from sectiondb - m_osvt
	//char    *ptr_sectionsVotes; // our local votes - m_nsvt
	//char    *ptr_addressReply;
	char      *ptr_clockCandidatesData;
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

	long       size_firstUrl;
	long       size_redirUrl;
	//long     size_tagRecData;
	long       size_rootTitleBuf;
	long       size_gigabitHashes;
	long       size_gigabitScores;
	long       size_adVector;
	long       size_wikiDocIds;
	long       size_wikiScores;
	long       size_imageData;
	long       size_catIds;
	long       size_indCatIds;
	long       size_dmozTitles;
	long       size_dmozSumms;
	long       size_dmozAnchors;
	long       size_utf8Content;
	//long     size_sectionsReply;
	//long     size_sectionsVotes;
	//long     size_addressReply;
	long       size_clockCandidatesData;
	//long     size_sectiondbData;
	//long     size_placedbData;
	long       size_site;
	long       size_linkInfo1;
	long       size_linkdbData;
	long       size_sectiondbData;
	long       size_tagRecData;
	long       size_linkInfo2;

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
	//	   long            niceness     = MAX_NICENESS ,
	//	   bool            justSetLinks = false        );

	// . used by Msg16 to set the Xml to get meta redirect tag's content
	// . used by Msg16 to get <META NAME="ROBOTS" CONTENT="index,follow">
	// . this should be set by Msg16 so it can get meta redirect url


	void print   ( );


	bool set2 ( char *titleRec,
		    long maxSize, 
		    char *coll,
		    class SafeBuf *p,
		    long niceness ,
		    class SpiderRequest *sreq = NULL );

	// . since being set from a docId, we will load the old title rec
	//   and use that!
	// . used by PageGet.cpp
	bool set3 ( long long  docId       , 
		    char      *coll        ,
		    long       niceness    );

	bool set4 ( class SpiderRequest *sreq  , 
		    key_t           *doledbKey ,
		    char            *coll      , 
		    class SafeBuf   *pbuf      , 
		    long             niceness  ,
		    char            *utf8Content = NULL ,
		    bool             deleteFromIndex = false ,
		    long             forcedIp = 0 ,
		    uint8_t          contentType = CT_HTML ,
		    time_t           spideredTime = 0 ,
		    bool             contentHasMime = false ) ;

	// we now call this right away rather than at download time!
	long getSpideredTime();

	// another entry point, like set3() kinda
	bool loadFromOldTitleRec ();

	XmlDoc() ; 
	~XmlDoc() ; 
	void nukeDoc ( class XmlDoc *);
	void reset ( ) ;
	bool setFirstUrl ( char *u , bool addWWW , Url *base = NULL ) ;
	class CollectionRec *getCollRec ( );
	bool setRedirUrl ( char *u , bool addWWW ) ;
	void setStatus ( char *s ) ;
	void setCallback ( void *state, void (*callback) (void *state) ) ;
	void setCallback ( void *state, bool (*callback) (void *state) ) ;
	bool addToSpiderdb ( ) ;
	bool indexDoc ( );
	key_t *getTitleRecKey() ;
	//char *getSkipIndexing ( );
	char *prepareToMakeTitleRec ( ) ;
	char **getTitleRec ( ) ;
	char *getIsAdult ( ) ;
	long **getIndCatIds ( ) ;
	long **getCatIds ( ) ;
	class CatRec *getCatRec ( ) ;
	long long **getWikiDocIds ( ) ;
	void gotWikiResults ( class UdpSlot *slot );
	long *getPubDate ( ) ;
	//class DateParse2 *getDateParse2 ( ) ;
	class Dates *getSimpleDates();
	class Dates *getDates();
	class HashTableX *getClockCandidatesTable();
	long getUrlPubDate ( ) ;
	long getOutlinkAge ( long outlinkNum ) ;
	char *getIsPermalink ( ) ;
	char *getIsUrlPermalinkFormat ( ) ;
	char *getIsRSS ( ) ;
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
	bool gotSectionStats( class Msg3a *msg3a );
	class SectionStats *getSectionStats ( long long secHash64 );
	class SectionVotingTable *getOldSectionVotingTable();
	class SectionVotingTable *getNewSectionVotingTable();
	char **getSectionsReply ( ) ;
	char **getSectionsVotes ( ) ;
	HashTableX *getSectionVotingTable();
	long *getLinkSiteHashes ( );
	class Links *getLinks ( bool doQuickSet = false ) ;
	class HashTableX *getCountTable ( ) ;
	bool hashString_ct ( class HashTableX *ht, char *s , long slen ) ;
	uint8_t *getSummaryLangId ( ) ;
	long     *getTagPairHashVector ( ) ;
	uint32_t *getTagPairHash32 ( ) ;
	long *getSummaryVector ( ) ;
	long *getPageSampleVector ( ) ;
	long *getPostLinkTextVector ( long linkNode ) ;
	long computeVector ( class Sections *sections, class Words *words, 
			     uint32_t *vec , long start = 0 , long end = -1 );
	float *getTagSimilarity ( class XmlDoc *xd2 ) ;
	float *getGigabitSimilarity ( class XmlDoc *xd2 ) ;
	float *getPageSimilarity ( class XmlDoc *xd2 ) ;
	float *getPercentChanged ( );
	uint64_t *getDupHash ( );
	class IndexList *getDupList ( ) ;
	class RdbList *getLikedbListForReq ( );
	class RdbList *getLikedbListForIndexing ( );
	long addLikedbRecords ( bool justGetSize ) ;
	char *getIsDup ( ) ;
	char *isDupOfUs ( long long d ) ;
	uint32_t *getGigabitVectorScorelessHash ( ) ;
	long *getGigabitHashes ( );
	char *getGigabitQuery ( ) ;
	char *getMetaDescription( long *mdlen ) ;
	char *getMetaSummary ( long *mslen ) ;
	char *getMetaKeywords( long *mklen ) ;
	bool addGigabits ( char *s , long long docId , uint8_t langId ) ;
	bool addGigabits2 ( char *s,long slen,long long docId,uint8_t langId);
	bool addGigabits ( class Words *ww , 
			   long long docId,
			   class Sections *sections,
			   //class Weights  *we ,
			   uint8_t langId );

	long *getSiteSpiderQuota ( ) ;
	class Url *getCurrentUrl ( ) ;
	class Url *getFirstUrl() ;
	long long getFirstUrlHash48();
	long long getFirstUrlHash64();
	class Url **getRedirUrl() ;
	class Url **getMetaRedirUrl() ;
	long *getFirstIndexedDate ( ) ;
	long *getOutlinksAddedDate ( ) ;
	//long *getNumBannedOutlinks ( ) ;
	uint16_t *getCountryId ( ) ;
	class XmlDoc **getOldXmlDoc ( ) ;
	bool isRobotsTxtFile ( char *url , long urlLen ) ;
	class XmlDoc **getExtraDoc ( char *url , long maxCacheAge = 0 ) ;
	bool getIsPageParser ( ) ;
	class XmlDoc **getRootXmlDoc ( long maxCacheAge = 0 ) ;
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
	long long *getDocId ( ) ;
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
	long *getNumOfficialEmails ( ) ;
	char *getEmailBuf ( ) ;
	long *getNumContactAddresses ( );
	long addEmailTags ( class Xml *xml , class Words *ww , 
			    class TagRec *gr , long ip ) ;
	//class Url *getContactUsLink ( ) ;
	//class Url *getAboutUsLink ( ) ;
	long *getFirstIp ( ) ;
	bool *updateFirstIp ( ) ;
	long *getSiteNumInlinksUniqueIp ( ) ;
	long *getSiteNumInlinksUniqueCBlock ( ) ;
	long *getSiteNumInlinksTotal ( );
	//long *getSiteNumInlinksFresh ( ) ;
	//long *getSitePop ( ) ;
	uint8_t *getSiteNumInlinks8 () ;
	long *getSiteNumInlinks ( ) ;
	class LinkInfo *getSiteLinkInfo() ;
	long *getIp ( ) ;
	long *gotIp ( bool save ) ;
	bool *getIsAllowed ( ) ;
	//long getTryAgainTimeDelta() { 
	//	if ( ! m_tryAgainTimeDeltaValid ) { char *xx=NULL;*xx=0;}
	//	return m_tryAgainTimeDelta;
	//};
	char *getIsWWWDup ( ) ;
	class LinkInfo *getLinkInfo1 ( ) ;
	class LinkInfo **getLinkInfo2 ( ) ;
	char *getSite ( ) ;
	void  gotSite ( ) ;
	long long *getSiteHash64 ( ) ;
	long *getSiteHash32 ( ) ;
	char **getHttpReply ( ) ;
	char **getHttpReply2 ( ) ;
	char **gotHttpReply ( ) ;
	char *getIsContentTruncated ( );
	long *getDownloadStatus ( ) ;
	long long *getDownloadEndTime ( ) ;
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
	long *getContentHash32 ( ) ;
	long *getTagHash32 ( ) ;
	long getHostHash32a ( ) ;
	long getHostHash32b ( ) ;
	long getDomHash32 ( );
	char **getImageData();
	class Images *getImages ( ) ;
	int8_t *getNextSpiderPriority ( ) ;
	long *getPriorityQueueNum ( ) ;
	class TagRec ***getOutlinkTagRecVector () ;
	long **getOutlinkFirstIpVector () ;
	//char **getOutlinkIsIndexedVector () ;
	long *getRegExpNum ( long outlinkNum ) ;
	long *getRegExpNum2 ( long outlinkNum ) ;
	char *getIsSiteRoot ( ) ;
	bool getIsOutlinkSiteRoot ( char *u , class TagRec *gr ) ;
	int8_t *getHopCount ( ) ;
	//int8_t *getOutlinkHopCountVector ( ) ;
	char *getSpiderLinks ( ) ;
	long *getNextSpiderTime ( ) ;
	//char *getIsSpam() ;
	char *getIsFiltered ();
	bool getIsInjecting();
	long *getSpiderPriority ( ) ;
	long *getIndexCode ( ) ;
	SafeBuf *getNewTagBuf ( ) ;

	char *updateTagdb ( ) ;
	bool logIt ( ) ;
	bool m_doConsistencyTesting;
	bool doConsistencyTest ( bool forceTest ) ;
	long printMetaList ( ) ;
	void printMetaList ( char *metaList , char *metaListEnd ,
			     class SafeBuf *pbuf );
	bool verifyMetaList ( char *p , char *pend , bool forDelete ) ;
	bool hashMetaList ( class HashTableX *ht        ,
			    char       *p         ,
			    char       *pend      ,
			    bool        checkList ) ;

	char *getMetaList ( bool forDelete = false );

	void copyFromOldDoc ( class XmlDoc *od ) ;

	// we add a SpiderReply to spiderdb when done spidering, even if
	// m_indexCode or g_errno was set!
	class SpiderReply *getNewSpiderReply ( );

	void  setSpiderReqForMsg20 ( class SpiderRequest *sreq , 
				     class SpiderReply   *srep );


	char *addOutlinkSpiderRecsToMetaList ( );

	bool addTable96 ( class HashTableX *tt1     , 
			  long       date1   ,
			  bool       nosplit ) ;

	long getSiteRank ();
	bool addTable144 ( class HashTableX *tt1     , 
			   bool       nosplit ) ;
	bool addTable224 ( HashTableX *tt1 ) ;

	bool addTableDate ( class HashTableX *tt1     , //T<key128_t,char> *tt1
                            uint64_t    docId   ,
                            uint8_t     rdbId   ,
                            bool        nosplit ) ;

	bool addTable128 ( class HashTableX *tt1     , // T <key128_t,char>*tt1
                           uint8_t     rdbId   ,
			   bool        forDelete ) ;

	bool hashNoSplit ( class HashTableX *tt ) ;
	char *hashAll ( class HashTableX *table ) ;
	long getBoostFromSiteNumInlinks ( long inlinks ) ;
	bool hashMetaTags ( class HashTableX *table ) ;
	bool hashIsClean ( class HashTableX *table ) ;
	bool hashZipCodes ( class HashTableX *table ) ;
	bool hashMetaZip ( class HashTableX *table ) ;
	bool hashContentType ( class HashTableX *table ) ;
	bool hashLinks ( class HashTableX *table ) ;
	bool hashUrl ( class HashTableX *table ) ;
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
	bool hashCountry ( class HashTableX *table ) ;
	bool hashSiteNumInlinks ( class HashTableX *table ) ;
	bool hashCharset ( class HashTableX *table ) ;
	bool hashTagRec ( class HashTableX *table ) ;
	bool hashPermalink ( class HashTableX *table ) ;
	bool hashVectors(class HashTableX *table ) ;
	bool hashAds(class HashTableX *table ) ;
	class Url *getBaseUrl ( ) ;
	bool hashSubmitUrls ( class HashTableX *table ) ;
	void set20 ( Msg20Request *req ) ;
	class Msg20Reply *getMsg20Reply ( ) ;
	char **getImageUrl() ;
	class MatchOffsets *getMatchOffsets () ;
	Query *getQuery() ;
	Matches *getMatches () ;
	char *getDescriptionBuf ( char *displayMetas , long *dlen ) ;
	class Title *getTitle ();
	class Summary *getSummary () ;
	char *getHighlightedSummary ();
	SafeBuf *getSampleForGigabits ( ) ;
	char *getIsCompromised ( ) ;
	char *getIsNoArchive ( ) ;
	long *getUrlFilterNum();
	long long **getAdVector ( ) ;
	char *getIsLinkSpam ( ) ;
	char *getIsHijacked();
	char *getIsErrorPage ( ) ;
	char* matchErrorMsg(char* p, char* pend );

	bool hashWords  ( //long            wordStart ,
			  //long            wordEnd   ,
			  class HashInfo *hi        ) ;
	bool hashSingleTerm ( long long       termId , 
			      class HashInfo *hi     ) ;
	bool hashSingleTerm ( char            *s    ,
			      long             slen ,
			      class HashInfo  *hi   );
	bool hashString ( class HashTableX *ht   ,
			  //class Weights    *we   ,
			  class Bits       *bits ,
			  char             *s    ,
			  long              slen ) ;
	bool hashString ( char             *s    ,
			  long              slen ,
			  class HashInfo   *hi   ) ;



	bool hashWords3 ( //long              wordStart     ,
			  //long              wordEnd       ,
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
			  long              niceness      );
	
	bool hashString3 ( char             *s              ,
			  long              slen           ,
			  class HashInfo   *hi             ,
			  class HashTableX *countTable     ,
			  class SafeBuf    *pbuf           ,
			  class HashTableX *wts            ,
			  class SafeBuf    *wbuf           ,
			  long              version        ,
			  long              siteNumInlinks ,
			  long              niceness       );


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
	long long m_docId;

	char     *m_ubuf;
	long      m_ubufSize;
	long      m_ubufAlloc;

	// does this page link to gigablast, or has a search form to it?
	//bool linksToGigablast();
	//bool searchboxToGigablast();

	// private:

	// we we started spidering it, in milliseconds since the epoch
	long long    m_startTime;

	// when set() was called by Msg20.cpp so we can time how long it took
	// to generate the summary
	long long    m_setTime;

	// timers
	long long m_beginSEOTime;
	long long m_beginTimeAllMatch;
	long long m_beginTimeMatchUrl;
	long long m_beginTimeFullQueries;
	long long m_beginTimeLinks;
	//long long m_beginMsg98s;
	long long m_beginRelatedQueries;
	long long m_beginMsg95s;

	// . these should all be set using set*() function calls so their
	//   individual validity flags can bet set to true, and successive
	//   calls to their corresponding get*() functions will not core
	// . these particular guys are set immediately on set(char *titleRec)

	Url        m_redirUrl;
	Url       *m_redirUrlPtr;
	Url        m_metaRedirUrl;
	Url       *m_metaRedirUrlPtr;
	long       m_redirError;
	char       m_allowSimplifiedRedirs;
	Url        m_firstUrl;
	long long  m_firstUrlHash48;
	long long  m_firstUrlHash64;
	Url        m_currentUrl;
	char      *m_coll;
	char       m_collBuf[MAX_COLL_LEN+1]; // include \0
	char      *m_content;
	long       m_contentLen;

	char *m_metaList;
	long  m_metaListSize;

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

	Section *m_si;
	//Section *m_nextSection;
	//Section *m_lastSection;
	long m_msg3aRequestsOut;
	long m_msg3aRequestsIn;
	char *m_queryBuf;
	Msg39Request *m_msg39RequestArray;
	SafeBuf m_msg3aBuf;
	Msg3a *m_msg3aArray;
	char  *m_inUse;
	Query *m_queryArray;
	long long *m_secHash64Array;
	bool     m_gotDupStats;
	//long     m_secHash64;
	//Query    m_q4;
	//Msg3a    m_msg3a;
	//Msg39Request m_r39;
	Msg39Request m_mr2;
	HashTableX m_sectionStatsTable;
	//char m_sectionHashQueryBuf[128];

	// also set in getSections()
	long       m_maxVotesForDup;

	// . for rebuild logging of what's changed
	// . Repair.cpp sets these based on titlerec
	char m_logLangId;
	long m_logSiteNumInlinks;

	//SectionVotingTable m_nsvt;

	//SectionVotingTable m_osvt;
	//long m_numSectiondbReads;
	//long m_numSectiondbNeeds;
	//key128_t m_sectiondbStartKey;
	//RdbList m_secdbList;
	//long m_sectiondbRecall;

	//HashTableX m_rvt;
	//Msg17 m_msg17;
	//char *m_cachedRootVoteRec;
	//long  m_cachedRootVoteRecSize;
	//bool  m_triedVoteCache;
	//bool  m_storedVoteCache;
	//SafeBuf m_cacheRecBuf;

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
	uint32_t   m_tagPairHash;
	long       m_firstIp;

	class SafeBuf     *m_savedSb;
	class HttpRequest *m_savedHr;


	// validity flags. on reset() all these are set to false.
	char     m_VALIDSTART;
	// DO NOT add validity flags above this line!
	char     m_metaListValid;
	//char   m_docQualityValid;
	char     m_siteValid;
	char     m_startTimeValid;
	char     m_currentUrlValid;
	char     m_firstUrlValid;
	char     m_firstUrlHash48Valid;
	char     m_firstUrlHash64Valid;
	char     m_lastUrlValid;
	char     m_docIdValid;
	char     m_collValid;
	char     m_tagRecValid;
	char     m_robotsTxtLenValid;
	char     m_tagRecDataValid;
	char     m_newTagBufValid;
	char     m_rootTitleBufValid;
	char     m_filteredRootTitleBufValid;
	char     m_titleBufValid;
	char     m_fragBufValid;
	char     m_wordSpamBufValid;
	char     m_finalSummaryBufValid;

	char     m_matchingQueryBufValid;
	char     m_relatedQueryBufValid;
	char     m_queryLinkBufValid;
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
	char     m_metaListCheckSum8Valid;
	char     m_contentValid;
	char     m_filteredContentValid;
	char     m_charsetValid;
	char     m_langVectorValid;
	char     m_langIdValid;
	char     m_rootLangIdValid;
	char     m_datedbDateValid;
	char     m_isRSSValid;
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
	//char     m_osvtValid;
	//char     m_nsvtValid;
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
	char     m_tagPairHashValid;
	char     m_oldsrValid;
	char     m_newsrValid;
	char     m_titleRecValid;

	bool m_ipValid;
	bool m_firstIpValid;
	bool m_spideredTimeValid;
	//bool m_nextSpiderTimeValid;
	bool m_firstIndexedValid;
	bool m_outlinksAddedDateValid;
	bool m_countryIdValid;
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
	bool m_titleRecKeyValid;
	bool m_adVectorValid;
	bool m_wikiDocIdsValid;
	bool m_catIdsValid;
	bool m_versionValid;
	bool m_indCatIdsValid;
	bool m_dmozTitlesValid;
	bool m_dmozSummsValid;
	bool m_dmozAnchorsValid;
	bool m_rawUtf8ContentValid;
	bool m_expandedUtf8ContentValid;
	bool m_utf8ContentValid;
	bool m_isAllowedValid;
	//bool m_tryAgainTimeDeltaValid;
	bool m_eliminateMenusValid;
	bool m_redirUrlValid;
	bool m_metaRedirUrlValid;
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
	bool m_tagHash32Valid;
	bool m_linkInfo2Valid;
	bool m_spiderLinksValid;
	//bool m_nextSpiderPriorityValid;
	bool m_firstIndexedDateValid;
	bool m_isPermalinkValid;

	bool m_isAdultValid;
	bool m_hasAddressValid;
	bool m_hasTODValid;
	bool m_hasSiteVenueValid;
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
	bool m_siteNumInlinksUniqueIpValid;//FreshValid;
	bool m_siteNumInlinksUniqueCBlockValid;//sitePopValid
	bool m_siteNumInlinksTotalValid;
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
	//bool m_sectiondbDataValid;
	bool m_placedbDataValid;
	bool m_siteHash64Valid;
	bool m_siteHash32Valid;
	bool m_httpReplyValid;
	bool m_contentTypeValid;
	bool m_isBinaryValid;
	bool m_priorityQueueNumValid;
	bool m_outlinkTagRecVectorValid;
	bool m_outlinkIpVectorValid;
	bool m_outlinkIsIndexedVectorValid;
	bool m_isSiteRootValid;
	bool m_wasInjectedValid;
	bool m_outlinkHopCountVectorValid;
	//bool m_isSpamValid;
	bool m_isFilteredValid;
	bool m_urlFilterNumValid;
	bool m_numOutlinksAddedValid;
	bool m_baseUrlValid;
	bool m_replyValid;
	bool m_isPageParserValid;
	bool m_imageUrlValid;
	bool m_matchOffsetsValid;
	bool m_queryValid;
	bool m_matchesValid;
	bool m_dbufValid;
	bool m_titleValid;
	//bool m_twidsValid;
	bool m_termId32BufValid;
	bool m_termInfoBufValid;
	bool m_newTermInfoBufValid;
	bool m_summaryValid;
	bool m_gsbufValid;
	bool m_isCompromisedValid;
	bool m_isNoArchiveValid;
	//bool m_isVisibleValid;
	bool m_clockCandidatesTableValid;
	bool m_clockCandidatesDataValid;
	bool m_isLinkSpamValid;
	bool m_isErrorPageValid;
	bool m_isHijackedValid;
	bool m_dupHashValid;

	// shadows
	char m_isRSS2;
	char m_isPermalink2;
	char m_isAdult2;
        char m_spiderLinks2;
	char m_isContentTruncated2;
	char m_isLinkSpam2;
	bool m_hasAddress2;
	bool m_hasTOD2;
	bool m_hasSiteVenue2;
	char m_hasContactInfo2;
	char m_isSiteRoot2;

	// DO NOT add validity flags below this line!
	char     m_VALIDEND;

	// more stuff
	//char *m_utf8Content;
	//long m_utf8ContentLen;
	CatRec m_catRec;
	// use this stuff for getting wiki docids that match our doc's gigabits
	//Query m_wq; 
	//SearchInput m_si;
	//Msg40 m_msg40;
	//DateParse2 m_dateParse2;
	Dates m_dates;
	HashTableX m_clockCandidatesTable;
	SafeBuf m_cctbuf;
	float m_ageInDays;
	long m_urlPubDate;
	//long m_urlAge;
	char m_isUrlPermalinkFormat;
	uint8_t m_summaryLangId;
	long m_tagPairHashVec[MAX_TAG_PAIR_HASHES];
	long m_tagPairHashVecSize;
	long m_summaryVec [SAMPLE_VECTOR_SIZE/4];
	long m_summaryVecSize;
	long m_titleVec [SAMPLE_VECTOR_SIZE/4];
	long m_titleVecSize;
	long m_pageSampleVec[SAMPLE_VECTOR_SIZE/4];
	long m_pageSampleVecSize;
	long m_postVec[POST_VECTOR_SIZE/4];
	long m_postVecSize;
	float m_tagSimilarity;
	float m_gigabitSimilarity;
	float m_pageSimilarity;
	float m_percentChanged;
	bool  m_unchanged;
	// what docids are similar to us? docids are in this list
	IndexList m_dupList;
	RdbList m_likedbList;
	uint64_t m_dupHash;
	Msg0 m_msg0;
	Msg5 m_msg5;
	char m_isDup;
	long m_ei;
	long m_lastLaunch;
	Msg22Request m_msg22Request;
	Msg22 m_msg22a;
	Msg22 m_msg22b;
	Msg22 m_msg22c;
	Msg22 m_msg22d;
	Msg22 m_msg22e;
	Msg22 m_msg22f;
	long m_collLen;
	uint32_t m_gigabitVectorHash;
	char m_gigabitQuery [XD_GQ_MAX_SIZE];
	long m_gigabitHashes [XD_MAX_GIGABIT_HASHES];
	long m_gigabitScores [XD_MAX_GIGABIT_HASHES];
	char *m_gigabitPtrs  [XD_MAX_GIGABIT_HASHES];
	// for debug printing really
	class GigabitInfo *m_top[100];
	long               m_numTop;
	//char  m_metaDesc[1025];
	//char  m_metaKeywords[1025];
	// these now reference directly into the html src so our 
	// WordPosInfo::m_wordPtr algo works in seo.cpp
	char *m_metaDesc;
	long  m_metaDescLen;
	char *m_metaSummary;
	long  m_metaSummaryLen;
	char *m_metaKeywords;
	long  m_metaKeywordsLen;
	long  m_siteSpiderQuota;
	//long m_numBannedOutlinks;
	class XmlDoc *m_oldDoc;
	class XmlDoc *m_extraDoc;
	class XmlDoc *m_ahrefsDoc;
	//class XmlDoc *m_contactDoc;
	class XmlDoc *m_rootDoc;
	//class XmlDoc *m_gatewayDoc;
	RdbList m_oldMetaList;
	char   *m_oldTitleRec;
	long    m_oldTitleRecSize;
	char   *m_rootTitleRec;
	long    m_rootTitleRecSize;
	//char   *m_contactTitleRec;
	//long    m_contactTitleRecSize;
	char    m_isIndexed;
	Msg8a   m_msg8a;
	char   *m_tagdbColl;
	long    m_tagdbCollLen;
	Addresses m_addresses;

	Address *m_contactAddresses[MAX_CONTACT_ADDRESSES];
	long     m_numContactAddresses;

	char     m_isContacty;

	//Url     m_contactUsLink;
	//Url     m_aboutUsLink;
	/*
	char *m_contactLinks     [MAX_CONTACT_OUTLINKS];
	long  m_contactLens      [MAX_CONTACT_OUTLINKS];
	long  m_contactScores    [MAX_CONTACT_OUTLINKS];
	long  m_contactFlags     [MAX_CONTACT_OUTLINKS];
	char  m_contactProcessed [MAX_CONTACT_OUTLINKS];
	char *m_contactText      [MAX_CONTACT_OUTLINKS];
	char *m_contactTextEnd   [MAX_CONTACT_OUTLINKS];
	long  m_minContactScore;
	long  m_minContactIndex;
	long  m_numContactLinks;
	*/
	Url   m_extraUrl;
	//long m_siteNumInlinksFresh;
	//long m_sitePop;
	uint8_t m_siteNumInlinks8;
	//long m_siteNumInlinks;
	LinkInfo m_siteLinkInfo;
	char m_isInjecting;
	char m_useFakeMime;
	char m_useSiteLinkBuf;
	char m_usePageLinkBuf;
	char m_printInXml;
	Msg25 m_msg25;
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
	Msg13 m_msg13;
	Msg13Request m_msg13Request;
	bool m_isSpiderProxy;
	// for limiting # of iframe tag expansions
	long m_numExpansions;
	char m_newOnly;
	//long m_tryAgainTimeDelta;
	//long m_sameIpWait;
	//long m_sameDomainWait;
	//long m_maxSpidersPerDomain;
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
	long m_minInlinkerHopCount;
	//class LinkInfo *m_linkInfo2Ptr;
	SiteGetter m_siteGetter;
	long long  m_siteHash64;
	//char *m_site;
	//long m_siteLen;
	//Url m_siteUrl;
	long m_siteHash32;
	char *m_httpReply;
	char m_downloadAttempted;
	char m_redirectFlag;
	//char m_isScraping;
	//char m_throttleDownload;
	char m_spamCheckDisabled;
	char m_useRobotsTxt;
	long m_robotsTxtLen;
	long m_httpReplySize;
	long m_httpReplyAllocSize;
	char m_isBinary;
	char *m_filteredContent;
	long m_filteredContentLen;
	char *m_filter;
	long m_filteredContentAllocSize;
	long m_filteredContentMaxSize;
	char m_calledThread;
	long m_errno;
	class CollectionRec *m_cr;
	//long m_utf8ContentAllocSize;
	long m_hostHash32a;
	long m_hostHash32b;
	long m_domHash32;
	long m_priorityQueueNum;

	// this points into m_msge0 i guess
	//class TagRec **m_outlinkTagRecVector;
	Msge0 m_msge0;

	// this points into m_msge1 i guess
	//long *m_outlinkIpVector;
	Msge1 m_msge1;


	//
	// functions and vars for the seo query matching tool
	//
	bool loadTitleRecFromDiskOrSpider();
	//SafeBuf *getSEOQueryInfo ( );
	HashTableX *getTermIdBufDedupTable32();
	//long  *getTopWordsVector( bool includeSynonyms );
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
	bool     addRelatedDocIdInfo ( long long docId ,
				       long queryNum , 
				       float score ,
				       long  rank ,
				       long  siteHash26 ) ;
	bool     setRelatedDocIdWeightAndRank ( class RelatedDocId *rd );
	SafeBuf *getRelatedDocIdsWithTitles();
	bool     setRelatedDocIdInfoFromMsg20Reply ( class RelatedDocId *rd ,
						     class Msg20Reply *reply );

	SafeBuf *getRelatedQueryBuf();
	//SafeBuf *getRelatedQueryLinksModPart ( long modPart );

	bool addTermsFromQuery ( char *queryStr,
				 uint8_t queryLangId,
				 long gigablastTraffic,
				 long googleTraffic,
				 long hackqoff,
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


	//bool     sendBin ( long i );
	//bool     scoreDocIdRestrictedQueries(class Msg99Reply **replyPtrs,
	//				     class QueryLink  *linkPtrs,
	//				     long  numPtrs );

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

	long getNumInsertableTerms ( );
	class SafeBuf *getInsertableTerms ( );
	class SafeBuf *getScoredInsertableTerms ( );
	//class SafeBuf *getInsertableWordFreqInfoBuf ();
	bool processMsg95Replies();
	void setWordPosInfosTrafficGain ( class InsertableTerm *it );
	long getTrafficGain( class QueryChange *qc ) ;
	// print in xml
	bool printScoredInsertableTerms ( SafeBuf *sbuf ) ;


	HashTableX m_tidTable32;
	//long *m_twids;
	//long  m_numTwids;
	SafeBuf m_termId32Buf;
	SafeBuf m_termInfoBuf;
	SafeBuf m_newTermInfoBuf;
	//long  m_maxQueries;
	//long  m_maxRelatedQueries;
	//long  m_maxRelatedUrls;
	//long  m_numMsg99Requests;
	//long  m_numMsg98Requests;
	//long  m_numMsg99Replies;
	//long  m_numMsg98Replies;
	//char *m_msg99ReplyPtrs [MAX_HOSTS];
	//long  m_msg99ReplySizes[MAX_HOSTS];
	//long  m_msg99ReplyAlloc[MAX_HOSTS];
	//long  m_msg99HostIds   [MAX_HOSTS];
	char *m_msg95ReplyPtrs [MAX_HOSTS];
	long  m_msg95ReplySizes[MAX_HOSTS];
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
	long    m_msg8eReplySize[MAX_HOSTS];
	long    m_numMsg8eRequests;
	long    m_numMsg8eReplies;
	//bool    m_launchedAll;
	long long m_tlbufTimer;

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
	//long   m_cacheRecSize;
	//bool   m_triedCache;
	
	//class TopDocIds *m_topDocIdsBuf;
	//long             m_topDocIdsBufSize;
	SafeBuf m_topDocIdsBuf;
	//class TopDocIds *m_nextAvailTopDocIds;
	//long m_nextAvailTopDocIdsOffset;

	//long    m_maxFullQueries;
	//XmlDoc *m_newxd;
	//XmlDoc *m_newxd2;
	//bool    m_newxd2Blocked;
	//HashTableX m_tmpDupTable;
	//class Msg20  *m_newMsg20;
	Msg3a  *m_msg3a;
	Query  *m_query3a;
	long m_numMsg3aRequests;
	long m_numMsg3aReplies;

	long m_numMsg3fRequests;
	long m_numMsg3fReplies;
	long m_numMsg4fRequests;
	long m_numMsg4fReplies;
	bool m_sentMsg4fRequests;
	class UdpSlot *m_savedSlot;
	long m_numMsg95Requests;
	long m_numMsg95Replies;
	long m_qcursor;
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
	long m_numLinkRequestsOut;
	long m_numLinkRequestsIn;
	long m_hadLinkInfoError;
	long m_numMsg20sIn;
	long m_numMsg20sOut;
	long m_numValidMsg20s;
	long m_titleCursor;
	long m_msg20Phase;
	long m_recommendedLinkError;
	SafeBuf *lookupTitles();
	bool gotLinkerTitle ( class Msg20 *msg20 );

	// 1 *current* bin per host!
	//class Bin *m_currentBinPtrs[MAX_HOSTS];
	//long       m_binError;
	//long       m_msg98ReplyError;
	//long       m_binErrorForReplyPtrs;
	//long       m_binErrorForLinkPtrs;
	HashTableX m_qstringTable;

	// flow flags
	bool m_printedQueries;
	bool m_printedRelatedDocIds;
	bool m_printedRelatedQueries;
	bool m_printedScoredInsertableTerms;
	bool m_printedRecommendedLinks;
	bool m_loggedMsg3;
	long long m_lastPrintedDocId;
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
	long m_msg3aErrno ;
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
	long    m_socketWriteBufSent;
	long    m_queryNum;
	long    m_rdCursor;
	long    m_relatedNum;
	long    m_numRelatedAdded;

	// for getRelatedDocIdsWithTitles() launching msg20s
	long m_relatedDocIdError;
	long m_numMsg20Replies;
	long m_numMsg20Requests;
	SafeBuf m_msg20Buf;

	// this points into m_msge2
	//char *m_outlinkIsIndexedVector;
	//Msge2 m_msge2;

	bool m_doneWithAhrefs;
	bool m_useAhrefs;
	bool m_reallyInjectLinks;
	long m_downloadLevel;
	long m_numRegExs;
	//char m_isSiteRoot;
	int8_t *m_outlinkHopCountVector;
	long  m_outlinkHopCountVectorSize;
	//char m_isSpam;
	char m_isUrlBadYear;
	char m_isFiltered;
	long m_urlFilterNum;
	long m_numOutlinksAdded;
	long m_numRedirects;
	bool m_isPageParser;
	Url m_baseUrl;
	Msg20Reply m_reply;
	Msg20Request *m_req;
	//char *m_gsbuf;
	SafeBuf m_gsbuf;
	//long  m_gsbufSize;
	//long  m_gsbufAllocSize;
	char *m_note;
	char *m_imageUrl;
	char  m_imageUrlBuf[100];
	long  m_imageUrlSize;
	MatchOffsets m_matchOffsets;
	Query m_query;
	Matches m_matches;
	// meta description buf
	long m_dbufLen;
	char m_dbuf[1024];
	Title m_title;
	Summary m_summary;
	char m_isCompromised;
	char m_isNoArchive;
	char m_isErrorPage;
	char m_isHijacked;
	//char m_isVisible;
	char m_dmozBuf[12000];

	// stuff
	char *m_statusMsg;
	Msg4  m_msg4;
	Msg8b m_msg8b;
	bool  m_incCount;
	bool  m_decCount;

	bool  m_deleteFromIndex;

	// ptrs to stuff
	char *m_titleRec;
	long  m_titleRecSize;
	bool  m_freeTitleRec;
	long  m_titleRecAllocSize;
	key_t m_titleRecKey;

	// for isDupOfUs()
	char *m_dupTrPtr;
	long  m_dupTrSize;

	// parse these out of spider rec
	/*
	long  m_retryNum                ;
	long  m_spiderRecPriority       ;
	bool  m_spiderRecIsNew          ;
	long  m_spiderRecSiteNumInlinks ;
	long  m_spiderRecRetryCount     ;
	long  m_spiderRecHopCount       ;
	key_t m_spiderRecKey            ;
	bool  m_spiderRecForced         ;
	long  m_spiderRecTime           ;
	long  m_srDataSize ;
	char  m_srData [ MAX_SPIDERREC_SIZE ];
	*/

	key_t     m_doledbKey;
	SpiderRequest m_oldsr;
	SpiderReply   m_newsr;

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
	long long m_calledMsg22d             ;
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

	long    m_langIdScore;
	//long    m_rootLangIdScore;
	//uint8_t m_rootLangId;

	// used for getting contact info
	//bool m_triedRoot                ;
	//long m_winner                   ;

	long m_dist;

	// the tags in this tagRec are just contact info based tags and
	// created in the addContactInfo() function. also, in that same
	// function we add/sub the tags in m_citr to the m_newTagRec tag rec.
	//TagRec m_citr ;

	char m_emailBuf[EMAILBUFSIZE];
	long m_numOfficialEmails;

	// use to store a \0 list of "titles" of the root page so we can
	// see which if any are the venue name, and thus match that to
	// addresses of the venue on the site, and we can use those addresses
	// as default venue addresses when no venues are listed on a page
	// on that site.
	char   m_rootTitleBuf[ROOT_TITLE_BUF_MAX];
	long   m_rootTitleBufSize;

	// . this is filtered
	// . certain punct is replaced with \0
	char   m_filteredRootTitleBuf[ROOT_TITLE_BUF_MAX];
	long   m_filteredRootTitleBufSize;

	// like m_rootTitleBuf but for the current page
	char   m_titleBuf[ROOT_TITLE_BUF_MAX];
	long   m_titleBufSize;


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

	// this is non-zero if we decided not to index the doc
	long m_indexCode;

	// the spider priority
	long m_priority;

	// the download error, like ETIMEDOUT, ENOROUTE, etc.
	long m_downloadStatus;

	// . when the download was completed. will be zero if no download done
	// . used to set SpiderReply::m_downloadEndTime because we need
	//   high resolution for that so we can dole out the next spiderrequest
	//   from that IP quickly if the sameipwait is like 500ms.
	long long m_downloadEndTime;

	char *m_metaListEnd;
	long  m_metaListAllocSize;
	char *m_p;
	char *m_pend;

	long  m_maxCacheAge;

	// a list of 32-bit ints followed by a zero 32-bit int to terminate
	long long m_adIds [ XD_MAX_AD_IDS ];
	//char *m_adVector;// [XMLDOC_MAX_AD_IDS];
	//long  m_adVectorSize;

	char     *m_wikiqbuf;
	long      m_wikiqbufSize;
	long long m_wikiDocIds [ MAX_WIKI_DOCIDS ];
	rscore_t  m_wikiScores [ MAX_WIKI_DOCIDS ];

	bool      m_registeredSleepCallback;
	bool      m_addedNegativeDoledbRec;
	
	bool          m_hashedTitle;
	bool          m_hashedMetas;

	long          m_niceness;

	bool m_usePosdb     ;
	bool m_useDatedb    ;
	bool m_useClusterdb ;
	bool m_useLinkdb    ;
	bool m_useSpiderdb  ;
	bool m_useTitledb   ;
	bool m_useTagdb     ;
	bool m_usePlacedb   ;
	//bool m_useTimedb    ;
	//bool m_useSectiondb ;
	//bool m_useRevdb     ;
	bool m_useSecondaryRdbs ;

	long          m_linkeeQualityBoost;

	SafeBuf *m_pbuf;
	// used by SpiderLoop to set m_pbuf to
	SafeBuf  m_sbuf;
	// store termlist into here if non-null
	bool     m_storeTermListInfo;
	char     m_sortTermListBy;

	//SafeBuf m_sectiondbData;
	//char *m_sectiondbData;
	char *m_placedbData;
	//long  m_sectiondbDataSize;
	long  m_placedbDataSize;

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
	long  m_rawUtf8ContentSize;
	long  m_rawUtf8ContentAllocSize; // we overallocate sometimes
	char *m_expandedUtf8Content;
	long  m_expandedUtf8ContentSize;
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

	// word spam detection
	char *getWordSpamVec ( );
	bool setSpam ( long *profile, long plen , long numWords , 
		       unsigned char *spam );
	long  getProbSpam  ( long *profile, long plen , long step );
	bool m_isRepeatSpammer;
	long m_numRepeatSpam;
	bool m_totallySpammed;

	// frag vector (repeated fragments). 0 means repeated, 1 means not.
	// vector is 1-1 with words in the document body.
	char *getFragVec ( );

	bool injectLinks  ( HashTableX *linkDedupTable ,
			    HashTableX *domDedupTable ,
			    void *finalState , 
			    void (* finalCallback)(void *));
	bool injectAhrefsLinks();
	bool doInjectLoop ( );
	void doneInjecting ( class XmlDoc *xd );
	long  m_i;
	long  m_blocked;
	HashTableX  m_domDedupTable;
	HashTableX *m_linkDedupTablePtr;
	HashTableX *m_domDedupTablePtr;
	bool m_dedupLinkDomains;
	void *m_finalState;
	void (* m_finalCallback) ( void *state );
	char  m_used[MAX_XML_DOCS];
	class XmlDoc *m_xmlDocs[MAX_XML_DOCS];
	long long m_cacheStartTime;
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
	long      m_termOff;
	long      m_termLen;
	//uint32_t  m_score32;
	long      m_descOff;   // the description offset
	long      m_prefixOff; // the prefix offset, like "site" or "gbadid"
	long long m_termId;
	long      m_date;
	bool      m_noSplit;

	//float     m_weight;
	char      m_langId;
	char      m_diversityRank;
	char      m_densityRank;
	char      m_wordSpamRank;
	char      m_hashGroup;
	long      m_wordNum;
	long      m_wordPos;
	//bool      m_isSynonym;
	// 0 = not a syn, 1 = syn from presets,2=wikt,3=generated
	char      m_synSrc;
	long long  m_langBitVec64;
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
		m_noSplit                 = false;
		//m_useWeights              = false;
		m_useSynonyms             = false;
		m_hashGroup = -1;
		m_startDist = 0;
		m_siteHash32 = 0;
	};
	class HashTableX *m_tt;
	char             *m_prefix;
	// "m_desc" should detail the algorithm
	char             *m_desc;
	long              m_date;
	char              m_noSplit;
	char              m_linkerSiteRank;
	//char              m_useWeights;
	char              m_useSynonyms;
	char              m_hashGroup;
	long              m_startDist;
	long              m_siteHash32;
};


// g_tt is used for debugging
//extern class TermTable *g_tt;

extern uint8_t score32to8 ( uint32_t score ) ;

extern pid_t g_pid    ;
extern long  g_ticker ;
extern long  g_filterTimeout ;

// as recommended in the "man system" page we use our own
int my_system_r ( char *cmd , long timeout ) ;

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
inline long XmlDoc::getProbSpam(long *profile, long plen, long step) {

	// you can spam 2 or 1 letter words all you want to
	if ( plen <= 2 ) return 0;

	// if our step is bigger than the profile return 0
	if ( step == plen ) return 0;

	register long avgSpacing, stdDevSpacing;
	long d,dev=0;
	register long i;
	
	for (long j = 0; j < step; j++) {

		// find avg. of gaps between consecutive tokens in subprofile
		// TODO: isn't profile[i] < profile[i+1]??
		long istop = plen-1;
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
	long prob = ( (256*100/7) * plen ) / dev;

	if (prob>100) prob=100;

	return prob;

	//if (prob>=0) {
	//	long i;
	//printf("dev=%i,plen=%i,nseq=%i,prob=%i----\n",dev,plen,step,prob);
	//	for (i=0;i<plen;i++)
	//		printf("%i#",profile[i]);
	//	printf("\n");
	//}
}

#endif

