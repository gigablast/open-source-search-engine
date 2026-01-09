// Gigablast, Inc.  Copyright April 2007

// Linkdb - stores link information

// . Format of a 28-byte key in linkdb
// . used by Msg25::getPageLinkInfo()
// .
// . HHHHHHHH HHHHHHHH HHHHHHHH HHHHHHHH H = sitehash32 of linkEE
// . pppppppp pppppppp pppppppp pppppppp p = linkEEHash, q = ~linkerSiteRank
// . pppppppp pppppppS qqqqqqqq cccccccc c = lower ip byte, S = isLinkSpam?
// . IIIIIIII IIIIIIII IIIIIIII dddddddd I = upper 3 bytes of ip
// . dddddddd dddddddd dddddddd dddddd00 d = linkerdocid,h = half bit,Z =delbit
// . mmmmmmmm mmmmmm0N xxxxxxxx xxxxxxss N = 1 if it was added to existing page
// . ssssssss ssssssss ssssssss sssssshZ s = sitehash32 of linker
//   m = discovery date in days since jan 1
//   x = estimated date it was lost (0 if not yet lost)
//   
// NOTE: the "c" bits were the hopcount of the inlinker, but we changed
// them to the lower ip byte so steve can show the # of unique ips linking
// to your page or site.

#ifndef _LINKDB_H_
#define _LINKDB_H_

#define LDBKS sizeof(key224_t)

#define LDB_MAXURLHASH  0x00007fffffffffffLL

#define LINKDBEPOCH (1325376000-365*86400*4)

#include "Conf.h"
#include "Rdb.h"
#include "Titledb.h"

void  handleRequest25 ( UdpSlot *slot , int32_t netnice ) ;

// . get the inlinkers to this SITE (any page on this site)
// . use that to compute a site quality
// . also get the inlinkers sorted by date and see how many good inlinkers
//   we had since X days ago. (each inlinker needs a pub/birth date)
class Msg25Request {
public:
	// either MODE_PAGELINKINFO or MODE_SITELINKINFO
	char       m_mode;
	int32_t       m_ip                ;
	int64_t  m_docId             ;
	collnum_t  m_collnum           ;
	bool       m_isInjecting       ;
	bool       m_printInXml        ;

	// when we get a reply we call this
	void      *m_state               ;
	void    (* m_callback)(void *state) ;

	int32_t       m_siteNumInlinks      ;
	class LinkInfo  *m_oldLinkInfo         ;
	int32_t       m_niceness            ;
	bool       m_doLinkSpamCheck     ;
	bool       m_oneVotePerIpDom     ;
	bool       m_canBeCancelled      ;
	int32_t       m_lastUpdateTime      ;
	bool       m_onlyNeedGoodInlinks  ;
	bool       m_getLinkerTitles ;
	int32_t       m_ourHostHash32 ;
	int32_t       m_ourDomHash32 ;

	uint8_t m_waitingInLine:1;
	uint8_t m_reserved1:1;
	uint8_t m_reserved2:1;
	uint8_t m_reserved3:1;
	uint8_t m_reserved4:1;
	uint8_t m_reserved5:1;
	uint8_t m_reserved6:1;
	uint8_t m_reserved7:1;

	// new stuff
	int32_t       m_siteHash32;
	int64_t  m_siteHash64;
	int64_t  m_linkHash64;
	// for linked list of these guys in g_lineTable in Linkdb.cpp
	// but only used on the server end, not client end
	class Msg25Request *m_next;
	// the mutlicast we use
	class Multicast *m_mcast;
	UdpSlot *m_udpSlot;
	bool m_printDebugMsgs;
	// store final LinkInfo reply in here
	SafeBuf   *m_linkInfoBuf;


	char      *ptr_site;
	char      *ptr_url;
	char      *ptr_oldLinkInfo;

	int32_t       size_site;
	int32_t       size_url;
	int32_t       size_oldLinkInfo;

	//variable data begins here

	int32_t getStoredSize();
	void serialize();
	void deserialize();
};

// . returns false if blocked, true otherwise
// . sets errno on error
// . your req->m_callback will be called with the Msg25Reply
bool getLinkInfo ( SafeBuf *reqBuf , // store msg25 request in here
		   Multicast *mcast , // use this to send msg 0x25 request
		   char      *site ,
		   char      *url  ,
		   bool       isSiteLinkInfo ,
		   int32_t       ip                  ,
		   int64_t  docId               ,
		   collnum_t collnum ,
		   char      *qbuf                ,
		   int32_t       qbufSize            ,
		   void      *state               ,
		   void (* callback)(void *state) ,
		   bool       isInjecting         ,
		   SafeBuf   *pbuf                ,
		   bool printInXml ,
		   int32_t       siteNumInlinks      ,
		   LinkInfo  *oldLinkInfo         ,
		   int32_t       niceness            ,
		   bool       doLinkSpamCheck     ,
		   bool       oneVotePerIpDom     ,
		   bool       canBeCancelled      ,
		   int32_t       lastUpdateTime      ,
		   bool       onlyNeedGoodInlinks  ,
		   bool       getLinkerTitles , //= false ,
		   // if an inlinking document has an outlink
		   // of one of these hashes then we set
		   // Msg20Reply::m_hadLinkToOurDomOrHost.
		   // it is used to remove an inlinker to a related
		   // docid, which also links to our main seo url
		   // being processed. so we do not recommend
		   // such links since they already link to a page
		   // on your domain or hostname. set BOTH to zero
		   // to not perform this algo in handleRequest20()'s
		   // call to XmlDoc::getMsg20Reply().
		   int32_t       ourHostHash32 ,
		   int32_t       ourDomHash32 ,
		   SafeBuf *myLinkInfoBuf );


void  handleRequest25 ( UdpSlot *slot , int32_t netnice ) ;

int32_t getSiteRank ( int32_t sni ) ;

class Linkdb {
 public:
	void reset();

	bool init    ( );
	bool init2 ( int32_t treeMem );
	bool verify  ( char *coll );
	bool addColl ( char *coll, bool doVerify = true );

	bool setMetaList ( char    *metaList        ,
			   char    *metaListEnd     ,
			   class XmlDoc  *oldDoc          ,
			   class XmlDoc  *newDoc          ,
			   int32_t     niceness        ,
			   int32_t    *numBytesWritten ) ;

	// this makes a "url" key
	key224_t makeKey_uk ( uint32_t  linkeeSiteHash32 ,
			      uint64_t  linkeeUrlHash64  ,
			      bool      isLinkSpam     ,
			      unsigned char linkerSiteRank , // 0-15 i guess
			      unsigned char linkerHopCount ,
			      uint32_t  linkerIp       ,
			      int64_t linkerDocId    ,
			      uint32_t      discoveryDate  ,
			      uint32_t      lostDate       ,
			      bool      newAddToOldPage   ,
			      uint32_t linkerSiteHash32 ,
			      bool      isDelete       );
	

	key224_t makeStartKey_uk ( uint32_t linkeeSiteHash32 ,
				   uint64_t linkeeUrlHash64  = 0LL ) {
		return makeKey_uk ( linkeeSiteHash32,
				    linkeeUrlHash64,
				    false, // linkspam?
				    255, // 15, // ~siterank
				    0, // hopcount
				    0, // ip
				    0, // docid
				    0, //discovery date
				    0, // lostdate
				    false, // newaddtopage
				    0, // linkersitehash
				    true); // is delete?
	}

	key224_t makeEndKey_uk ( uint32_t linkeeSiteHash32 ,
				 uint64_t linkeeUrlHash64  = 
				 0xffffffffffffffffLL ) {
		return makeKey_uk ( linkeeSiteHash32,
				    linkeeUrlHash64,
				    true, // linkspam?
				    0, // ~siterank
				    0xff, // hopcount
				    0xffffffff, // ip
				    MAX_DOCID, // docid
				    0xffffffff, //discovery date
				    0xffffffff, // lostdate
				    true, // newaddtopage
				    0xffffffff, // linkersitehash
				    false); // is delete?
	}

	//
	// accessors for "url" keys in linkdb
	//

	uint32_t getLinkeeSiteHash32_uk ( key224_t *key ) {
		return (key->n3) >> 32; }

	uint64_t getLinkeeUrlHash64_uk ( key224_t *key ) {
		uint64_t h = key->n3;
		h &= 0x00000000ffffffffLL;
		h <<= 15;
		h |= key->n2 >> 49;
		return h;
	}

	char isLinkSpam_uk (key224_t *key ) {
		if ((key->n2) & 0x1000000000000LL) return true; 
		return false;
	}

	unsigned char getLinkerSiteRank_uk ( key224_t *k ) {
		unsigned char rank = (k->n2 >> 40) & 0xff;
		// complement it back
		rank = (unsigned char)~rank;//LDB_MAXSITERANK - rank;
		return rank;
	}

	int32_t getLinkerIp_uk ( key224_t *k ) {
		uint32_t ip ;
		// the most significant part of the ip is the lower byte!!!
		ip = (uint32_t)((k->n2>>8)&0x00ffffff);
		ip |= ((k->n2>>8) & 0xff000000);
		return ip;
	}

	void setIp32_uk ( void *k , uint32_t ip ) {
		char *ips = (char *)&ip;
		char *ks = (char *)k;
		ks[16] = ips[3];
		ks[15] = ips[2];
		ks[14] = ips[1];
		ks[13] = ips[0];
	}


	// we are missing the lower byte, it will be zero
	int32_t getLinkerIp24_uk ( key224_t *k ) {
		return (int32_t)((k->n2>>8)&0x00ffffff); 
	}

	int64_t getLinkerDocId_uk( key224_t *k ) {
		uint64_t d = k->n2 & 0xff;
		d <<= 30;
		d |= k->n1 >>34;
		return d;
	}

	// . in days since jan 1, 2012 utc
	// . timestamp of jan 1, 2012 utc is 1325376000
	int32_t getDiscoveryDate_uk ( void *k ) {
		uint32_t date = ((key224_t *)k)->n1 >> 18;
		date &= 0x00003fff;
		// if 0 return that
		if ( date == 0 ) return 0;
		// multiply by seconds in days then
		date *= 86400;
		// add OUR epoch
		date += LINKDBEPOCH;
		// and use that
		return date;
	}

	// . in days since jan 1, 2012 utc
	// . timestamp of jan 1, 2012 utc is 1325376000
	void setDiscoveryDate_uk ( void *k , int32_t date ) {
		// subtract jan 1 2012
		date -= LINKDBEPOCH;
		// convert into days
		date /= 86400;
		// sanity
		gbassert_false( date > 0x3fff || date < 0 );
		// clear old bits
		((key224_t *)k)->n1 &= 0xffffffff03ffffLL;
		// scale us into it
		((key224_t *)k)->n1 |= ((uint64_t)date) << 18;
	}

	int32_t getLostDate_uk ( void *k ) {
		uint32_t date = ((key224_t *)k)->n1 >> 2;
		date &= 0x00003fff;
		// if 0 return that
		if ( date == 0 ) return 0;
		// multiply by seconds in days then
		date *= 86400;
		// add OUR epoch
		date += LINKDBEPOCH;
		// and use that
		return date;
	}

	// . in days since jan 1, 2012 utc
	// . timestamp of jan 1, 2012 utc is 1325376000
	void setLostDate_uk ( void *k , int32_t date ) {
		// subtract jan 1 2012
		date -= LINKDBEPOCH;
		// convert into days
		date /= 86400;
		// sanity
		gbassert_false( date > 0x3fff || date < 0 );
		// clear old bits
		((key224_t *)k)->n1 &= 0xffffffffffff0003LL;
		// scale us into it
		((key224_t *)k)->n1 |= ((uint64_t)date) << 2;
	}

	uint32_t getLinkerSiteHash32_uk( void *k ) {
		uint32_t sh32 = ((key224_t *)k)->n1 & 0x00000003;
		sh32 <<= 30;
		sh32 |= ((key224_t *)k)->n0 >> 2;
		return sh32;
	}

	Rdb           *getRdb()           { return &m_rdb; };

 private:
	Rdb           m_rdb;

};

extern class Linkdb g_linkdb;
extern class Linkdb g_linkdb2;


// . get ALL the linkText classes for a url and merge 'em into a LinkInfo class
// . also gets the link-adjusted quality of our site's url (root url)
// . first gets all docIds of docs that link to that url via an link: search
// . gets the LinkText, customized for our url, from each docId in that list
// . merge them into a final LinkInfo class for your url

#include "Msg2.h"      // for getting IndexLists from Indexdb
#include "Msg20.h"     // for getting this url's LinkInfo from another cluster
#include "SafeBuf.h"
#include "HashTableX.h"
#include "Msg22.h"
#include "CatRec.h"

#define MAX_LINKERS 3000

// if a linker is a "title rec not found" or log spam, then we get another
// linker's titleRec. churn through up to these many titleRecs in an attempt
// to get MAX_LINKERS good titlerecs before giving up.
//#define MAX_DOCIDS_TO_SAMPLE 25000
// on news.google.com, 22393 of the 25000 are link spam, and we only end
// up getting 508 good inlinks, so rais from 25000 to 50000
//#define MAX_DOCIDS_TO_SAMPLE 50000
// try a ton of lookups so we can ditch xfactor and keep posdb key as
// simple as possible. just make sure we recycle link info a lot!
#define MAX_DOCIDS_TO_SAMPLE 1000000

// go down from 300 to 100 so XmlDoc::getRecommendLinksBuf() can launch
// like 5 msg25s and have no fear of having >500 msg20 requests outstanding
// which clogs things up
// crap, no, on gk144 we got 128 hosts now, so put back to 300...
// if we have less hosts then limit this proportionately in Linkdb.cpp
#define	MAX_MSG20_OUTSTANDING 300

#define MAX_NOTE_BUF_LEN 20000

#define MSG25_MAX_REQUEST_SIZE (MAX_URL_LEN+MAX_COLL_LEN+64)

void  handleRequest25 ( UdpSlot *slot , int32_t netnice ) ;

class Msg25 {

 public:

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . this sets Msg25::m_siteRootQuality and Msg25::m_linkInfo
	// . "url/coll" should NOT be on stack in case weBlock
	// . if "reallyGetLinkInfo" is false we don't actually try to fetch 
	//   any link text and return true right away, really saves a bunch 
	//   of disk seeks when spidering small collections that don't need 
	//   link text/info indexing/analysis
	bool getLinkInfo2 (char      *site ,
			   char      *url  ,
			   bool       isSiteLinkInfo ,
			   int32_t       ip                  ,
			   int64_t  docId               ,
			   collnum_t collnum,
			   char      *qbuf                ,
			   int32_t       qbufSize            ,
			   void      *state               ,
			   void (* callback)(void *state) ,
			   bool       isInjecting         ,
			   bool       printDebugMsgs ,
			   bool       printInXml ,
			   int32_t       siteNumInlinks      ,
			   LinkInfo  *oldLinkInfo         ,
			   int32_t       niceness            ,
			   bool       doLinkSpamCheck     ,
			   bool       oneVotePerIpDom     ,
			   bool       canBeCancelled      ,
			   int32_t       lastUpdateTime      ,
			   bool       onlyNeedGoodInlinks  ,
			   bool       getLinkerTitles , //= false ,
			   // if an inlinking document has an outlink
			   // of one of these hashes then we set
			   // Msg20Reply::m_hadLinkToOurDomOrHost.
			   // it is used to remove an inlinker to a related
			   // docid, which also links to our main seo url
			   // being processed. so we do not recommend
			   // such links since they already link to a page
			   // on your domain or hostname. set BOTH to zero
			   // to not perform this algo in handleRequest20()'s
			   // call to XmlDoc::getMsg20Reply().
			   int32_t       ourHostHash32 ,
			   int32_t       ourDomHash32 ,
			   SafeBuf *myLinkInfoBuf );
	Msg25();
	~Msg25();
	void reset();

	// a new parm referencing the request we got over the network
	class Msg25Request * m_req25;

	class Msg20Reply *getLoser (class Msg20Reply *r, class Msg20Reply *p);
	char             *isDup    (class Msg20Reply *r, class Msg20Reply *p);

	bool addNote ( char *note , int32_t noteLen , int64_t docId );

	// m_linkInfo ptr references into here. provided by caller.
	SafeBuf *m_linkInfoBuf;

	SafeBuf m_realBuf;

	bool gotList     ( ) ;
	bool sendRequests ( );
	bool gotLinkText  ( class Msg20Request *req ) ;
	bool doReadLoop ( );

	char *m_url;
	char *m_site;

	int32_t m_ourHostHash32;
	int32_t m_ourDomHash32;

	int32_t m_round;
	uint64_t m_linkHash64;
	key224_t m_nextKey;

	bool       m_retried;
	bool       m_prependWWW;
	bool       m_onlyNeedGoodInlinks;
	bool       m_getLinkerTitles;
	int64_t  m_docId;
	collnum_t m_collnum;
	void      *m_state;
	void     (* m_callback) ( void *state );

	int32_t m_siteNumInlinks;
	int32_t m_mode;
	bool m_printInXml;

	// url info
	int32_t m_ip;
	int32_t m_top;
	int32_t m_midDomHash;

	bool m_gettingList;

	// hack for seo pipeline in xmldoc.cpp
	int32_t m_hackrd;
	
	// . we use Msg0 to get an indexList for href: terms 
	// . the href: IndexList's docIds are docs that link to us
	// . we now use Msg2 since it has "restrictIndexdb" support to limit
	//   indexdb searches to just the root file to decrease disk seeks
	Msg5 m_msg5;
	RdbList m_list;

	class Inlink *m_k;

	// for getting the root title rec so we can share its pwids
	Msg22 m_msg22;

	int32_t      m_maxNumLinkers;

	// should we free the m_replyPtrs on destruction? default=true
	bool m_ownReplies;

	// Now we just save the replies we get back from Msg20::getSummary()
	// We point to them with a LinkTextReply, which is just a pointer
	// and some access functions. 
 	Msg20Reply    *m_replyPtrs  [ MAX_LINKERS ];
	int32_t           m_replySizes [ MAX_LINKERS ];
	int32_t           m_numReplyPtrs;

	Msg20        m_msg20s        [ MAX_MSG20_OUTSTANDING ];
	Msg20Request m_msg20Requests [ MAX_MSG20_OUTSTANDING ];
	char         m_inUse         [ MAX_MSG20_OUTSTANDING ];
	// for "fake" replies
	Msg20Reply   m_msg20Replies  [ MAX_MSG20_OUTSTANDING ];

	int32_t      m_numDocIds;
	int32_t      m_cblocks;
	int32_t      m_uniqueIps;

	int32_t      m_minRecSizes;

	// Msg20 is for getting the LinkInfo class from this same url's
	// titleRec from another (usually much larger) gigablast cluster/netwrk
	Msg20     m_msg20; 

	// how many msg20s have we sent/recvd?
	int32_t      m_numRequests;
	int32_t      m_numReplies;  

	int32_t      m_linkSpamOut;

	// have we had an error for any transaction?
	int32_t      m_errno;

	SafeBuf   m_tmp;
	SafeBuf  *m_pbuf; // will point to m_tmp if m_printDebugMsgs

	// copied from CollectionRec
	bool  m_oneVotePerIpDom           ;
	bool  m_doLinkSpamCheck           ;
	bool  m_isInjecting               ;
	char  m_canBeCancelled            ;
	int32_t  m_lastUpdateTime            ;

	Multicast m_mcast;

	int32_t m_good;
	int32_t m_errors;
	int32_t m_noText;
	int32_t m_reciprocal;

	bool m_spideringEnabled;

	int32_t      m_dupCount;
	int32_t      m_vectorDups;
	int32_t      m_spamLinks;
	int32_t      m_niceness;
	int32_t      m_numFromSameIp;
	int32_t      m_sameMidDomain;

	// stats for allow some link spam inlinks to vote
	int32_t m_spamCount;
	int32_t m_spamWeight;
	int32_t m_maxSpam;

	char m_siteQuality;
	int32_t m_siteNumFreshInlinks;

	// this is used for the linkdb list
	//HashTableT <int32_t, char> m_ipTable;
	HashTableX m_ipTable;
	HashTableX m_fullIpTable;
	HashTableX m_firstIpTable;

	// this is for deduping docids because we now combine the linkdb
	// list of docids with the old inlinks in the old link info
	//HashTableT <int64_t, char> m_docIdTable;
	HashTableX m_docIdTable;

	// special counts
	int32_t      m_ipDupsLinkdb;
	int32_t      m_docIdDupsLinkdb;
	int32_t      m_linkSpamLinkdb;
	int32_t      m_lostLinks;
	int32_t      m_ipDups;

	uint32_t  m_groupId;
	int64_t      m_probDocId;

	LinkInfo *m_oldLinkInfo;

	char      m_buf [ MAX_NOTE_BUF_LEN ];
	char     *m_bufPtr;
	char     *m_bufEnd;
	HashTableX m_table;

	char      m_request [ MSG25_MAX_REQUEST_SIZE ];
	int32_t      m_requestSize;

	HashTableX m_adBanTable;

	// for setting <absScore2> or determining if a search results 
	// inlinkers also have the query terms. buzz.
	char *m_qbuf;
	int32_t  m_qbufSize;
};

// used by Msg25::addNote()
#define MAX_ENTRY_DOCIDS 10
class NoteEntry {
public:
	int32_t             m_count;
	char            *m_note;
	int64_t        m_docIds[MAX_ENTRY_DOCIDS];
};

// . takes a bunch of Msg20Replies and makes a serialized buffer, LinkInfo
// . LinkInfo's buffer consists of a bunch of serialized "Inlinks" as defined
//   below
// . THINK OF THIS CLASS as a Msg25 reply ("Msg25Reply") class

#include "Xml.h"

// how big can the rss item we store in the Inlink::ptr_rssItem be?
#define MAX_RSSITEM_SIZE 30000

class LinkInfo {

 public:

	int32_t   getStoredSize  ( ) { return m_lisize; };
	int32_t   getSize        ( ) { return m_lisize; };
	time_t getLastUpdated ( ) { return (time_t)m_lastUpdated; };

	int32_t   getNumLinkTexts ( ) { return m_numStoredInlinks; };

	int32_t   getNumGoodInlinks   ( ) { return m_numGoodInlinks; };

	class Inlink *getNextInlink ( class Inlink *k ) ;

	bool getItemXml ( Xml *xml , int32_t niceness ) ;

	bool hasLinkText ( );

	// for PageTitledb
	bool print ( class SafeBuf *sb , char *coll );

	bool hasRSSItem();

	// a small header, followed by the buf of "Inlinks", m_buf[]
	char       m_version;
	// we only keep usually no more than 10 or so internal guys, so this
	// can be a single byte
	char       m_numInlinksInternal;
	char       m_reserved1; // was m_siteRootQuality
	char       m_reserved2;
	// includes Inlinks in m_buf[] below
	int32_t       m_lisize;
	// this is really a time_t but that changes and this can't change!
	int32_t       m_lastUpdated;
	// this is precisely how many inlinks we stored in m_buf[] below
	int32_t       m_numStoredInlinks;//m_numTotalInlinks;
	// . only valid if titleRec version >= 119, otherwise its always 0
	// . this count includes internal as well as external links, i.e. just
	//   the total inlinks we got, counting at most one inlink per page. 
	//   it is not very useful i guess, but steve wants it.
	int32_t       m_totalInlinkingDocIds;//reserved3;
	// . how many inlinks did we have that were "good"?
	// . this is typically less than the # of Inlinks stored in m_buf below
	//   because it does not include internal cblock inlinks
	int32_t       m_numGoodInlinks;
	// . # of c blocks linking to this page/site
	// . only valid if titlerecversion >= 119
	// . includes your own intenral cblock
	int32_t       m_numUniqueCBlocks;//m_pagePop;
	// . # of IPs linking to this page/site
	// . only valid if titlerecversion >= 119
	// . includes your own internal ip
	int32_t       m_numUniqueIps;

	// serialize "Inlinks" into this buffer, m_buf[]
	char   m_buf[0];
} __attribute__((packed, aligned(4)));


#define MAXINLINKSTRINGBUFSIZE 2048

class Inlink {

 public:

	// zero ourselves out
	void reset() ;

	void set ( class Msg20Reply *reply );

	// set ourselves from a serialized older-versioned Inlink
	void set2 ( class Inlink *old );

	bool setXmlFromRSS      ( Xml *xml , int32_t niceness ) ;

	// . set a Msg20Reply from ourselves
	// . Msg25 uses this to recycle old inlinks that are now gone
	// . allows us to preserve ptr_rssInfo, etc.
	void setMsg20Reply ( class Msg20Reply *r ) ;

	int32_t getStoredSize ( ) ;

	// . return ptr to the buffer we serialize into
	// . return NULL and set g_errno on error
	char *serialize ( int32_t *retSize     ,
			  char *userBuf     ,
			  int32_t  userBufSize ,
			  bool  makePtrsRefNewBuf ) ;

	int32_t       m_ip                  ; //0
	int64_t  m_docId               ; // 4
	int32_t       m_firstSpidered       ; // 12
	int32_t       m_lastSpidered        ; // 16
	int32_t	   m_nextSpiderDate	 ; // 20
	// like in the titleRec, the lower 2 bits of the datedbDate have
	// special meaning. 
	// 0x00 --> datedb date extracted from content (pubdate)
	// 0x01 --> datedb date based on estimated "modified" time (moddate)
	// 0x10 --> datedb date is when same-site root was estimated to have
	//          first added that url as an outlink (discoverdate) (TODO)
	int32_t       m_datedbDate          ; // 24
	// this date is used as the discovery date for purposes of computing
	// LinkInfo::m_numInlinksFresh
	int32_t       m_firstIndexedDate    ; // 28
	//int32_t       m_baseScore           ;
	int32_t       m_pageNumInlinks      ; // 32
	int32_t       m_siteNumInlinks      ; // 36
	// record the word position we hashed this link text with
	// so we can match it to the DocIdScoringInfo stuff
	int32_t       m_wordPosStart;
	int32_t       m_firstIp;

	// . how many strings do we have?
	// . makes it easy to add new strings later
	uint16_t   m_reserved_NumStrings          ; // 48
	// . and were our first string ptrs starts
	// . allows us to set ourselves from an "old" Inlink 
	uint16_t   m_reserved_FirstStrPtrOffset   ; // 50

	uint16_t   m_numOutlinks         ; // 52
	// i guess no need to store this stuff if we are storing the url
	// in ptr_urlBuf below. we can call Url::set() then Url::getHostHash()
	// NO, because the site is now only contained in the TagRec now and
	// we compute the site in SiteGetter.cpp, so it is more complicated!!!
	// we get the tag rec of each outlink, and get the site from that
	// and hash that and store it here

	// we got a 2 byte padding before this PADPADPADPADP
	int16_t    m_pad0;

	int32_t       m_siteHash            ;

	// single bit flags
	uint16_t   m_isPermalink      : 1 ; // 60
	uint16_t   m_outlinkInContent : 1 ;
	uint16_t   m_outlinkInComment : 1 ;
	uint16_t   m_isReserved       : 1 ; // was u-n-i-c-o-d-e- bit
	uint16_t   m_isLinkSpam       : 1 ;
	// when Msg20Request::ptr_qbuf is set and
	// Msg20Request::m_computeLinkInfo is true, Msg20 calls Msg25, which 
	// in turn calls one Msg20 for each inlinker the doc has, thereby 
	// passing the ptr_qbuf into each of those Msg20s. if the inlinker 
	// matches the query then it sets m_hasAllQueryTerms to true and
	// returns the Msg20Reply to Msg25. When Msg25 is done it calls
	// makeLinkInfo() to make a LinkInfo out of all those Msg20Replies.
	// We use m_hasAllQueryTerms to display the absScore2 of each inlinker
	// in the raw xml search results feed for buzz.
	uint16_t   m_hasAllQueryTerms : 1 ;
	// if we imported it from the old LinkInfo. helps us preserve rssInfo,
	// hopcounts, etc.
	uint16_t   m_recycled         : 1 ;
	uint16_t   m_reserved4        : 1 ;
	uint16_t   m_reserved5        : 1 ;
	uint16_t   m_reserved6        : 1 ;
	uint16_t   m_reserved7        : 1 ;
	uint16_t   m_reserved8        : 1 ;
	uint16_t   m_reserved9        : 1 ;
	uint16_t   m_reserveda        : 1 ;
	uint16_t   m_reservedb        : 1 ;

	uint16_t   m_country             ; // 62
	uint8_t    m_language            ; // 64
	char       m_siteRank; // 65
	char       m_hopcount            ;  // 66
	char       m_linkTextScoreWeight ; // 0-100% (was m_inlinkWeight) //67

	char *getUrl ( ) { 
		if ( size_urlBuf == 0 ) return NULL;
		return m_buf ;
	};
	char *getLinkText ( ) { 
		if ( size_linkText == 0 ) return NULL;
		return m_buf +
			size_urlBuf;
	};
	char *getSurroundingText ( ) { 
		if ( size_surroundingText == 0 ) return NULL;
		return m_buf +
			size_urlBuf +
			size_linkText;
	};
	char *getRSSItem ( ) { 
		if ( size_rssItem == 0 ) return NULL;
		return m_buf +
			size_urlBuf +
			size_linkText + 
			size_surroundingText;
	};
	char *getCategories ( ) { 
		if ( size_categories == 0 ) return NULL;
		return m_buf +
			size_urlBuf +
			size_linkText + 
			size_surroundingText +
			size_rssItem;
	};
	char *getGigabitQuery ( ) { 
		if ( size_gigabitQuery == 0 ) return NULL;
		return m_buf +
			size_urlBuf +
			size_linkText + 
			size_surroundingText +
			size_rssItem +
			size_categories;
	};
	char *getTemplateVector ( ) { 
		if ( size_templateVector == 0 ) return NULL;
		return m_buf +
			size_urlBuf +
			size_linkText + 
			size_surroundingText +
			size_rssItem +
			size_categories +
			size_gigabitQuery;
	};
		

	//
	// add new non-strings right above this line
	//

	// . the url, link text and neighborhoods are stored in here
	// . no need to store vector for voting deduping in here because
	//   that use MsgE's Msg20Replies directly
	// . this is just stuff we want in the title rec
	int32_t    off_urlBuf            ; // 68
	int32_t    off_linkText          ;
	int32_t    off_surroundingText   ; // neighborhoods
	// . this is the rss item that links to us
	// . if calling Msg25::getLinkInfo() with getLinkerTitles set to
	//   true then this is the title!
	int32_t    off_rssItem           ;
	// . zakbot and the turk categorize site roots, and kids inherit
	//   the categories from their parent inlinkers
	// . we can't really use tagdb cuz that operates on subdirectories
	//   which may not be upheld for some sites. (like cnn.com!, the 
	//   stories are not proper subdirectories...)
	// . so inherit the category from our inlinkers. "sports", "world", ...
	// . comma-separated (in ascii)
	int32_t    off_categories        ;
	// . augments our own gigabits vector, used for finding related docs
	// . used along with the template vector for deduping pgs at index time
	// . now we used for finding similar docs AND categorizing
	// . comma-separated
	// . each gigabit has a count in []'s. score in body x1, title x5,
	//   and inlink text x5. i.e. "News[10],blue devils[5],... 
	// . always in UTF-8
	int32_t    off_gigabitQuery      ;
	// . the html tag vector. 
	// . used for deduping voters (anti-spam tech)
	// . used along with the gigabit vector for deduping pgs at index time
	// . now we used for finding similar docs and for categorizing (spam)
	int32_t    off_templateVector    ;

	//
	// add new strings right above this line
	//

	int32_t       size_urlBuf           ;
	int32_t       size_linkText         ;
	int32_t       size_surroundingText  ;
	int32_t       size_rssItem          ;
	int32_t       size_categories       ;
	int32_t       size_gigabitQuery     ;
	int32_t       size_templateVector   ;


	char       m_buf[MAXINLINKSTRINGBUFSIZE] ;
} __attribute__((packed, aligned(4)));

// . this function is normally called like "info = makeLinkInfo()"
//   to create a new LinkInfo based on a bunch of Msg20 replies
// . returns NULL and sets g_errno on error
LinkInfo *makeLinkInfo ( char        *coll                    ,
			 int32_t         ip                      ,
			 int32_t         siteNumInlinks          ,
			 Msg20Reply **replies                 ,
			 int32_t         numReplies              ,
			 // if link spam give this weight
			 int32_t         spamWeight              ,
			 bool         oneVotePerIpTop         ,
			 int64_t    linkeeDocId             ,
			 int32_t         lastUpdateTime          ,
			 bool         onlyNeedGoodInlinks      ,
			 int32_t         niceness                ,
			 class Msg25 *msg25 ,
			 SafeBuf *linkInfoBuf ) ;

////////
//
// LINKS CLASS
//
////////

typedef int32_t linkflags_t;

// all the links (urls), separated by \0's, are put into a buf of this size
#define LINK_BUF_SIZE (100*1024)

// Link Flags
#define LF_SAMEHOST      0x0001 // same hostname
#define LF_SAMEDOM       0x0002 // same domain
#define LF_SITEROOT      0x0004 // for blogrolls
#define LF_OLDLINK       0x0010 // set this if it was on the pg last spider tim
#define LF_RSS           0x0020 // is it from an rss <link href=> tag?
#define LF_PERMALINK     0x0040 // a probable permalink? of permalink format?
#define LF_SUBDIR        0x0080 // is the outlink in a subdir of parent?
#define LF_AHREFTAG      0x0100 // an <a href=> outlink
#define LF_LINKTAG       0x0200 // a <link> outlink
#define LF_FBTAG         0x0400 // a feed burner original outlink
#define LF_SELFLINK      0x0800 // links to self
#define LF_SELFPERMALINK 0x1000 // has "permalink" "link text" or attribute
#define LF_STRONGPERM    0x2000 // is permalink of /yyyy/mm/dd/ format
#define LF_EDUTLD        0x4000
#define LF_GOVTLD        0x8000

#define LF_NOFOLLOW     0x10000

bool isPermalink (
		   class Links *links       ,
		   class Url   *u           ,
		   char         contentType ,
		   class LinkInfo    *linkInfo    ,
		   bool         isRSS       ,
		   char       **note        = NULL  ,
		   char        *pathOverride= NULL  ,
		   bool         ignoreCgi   = false ,
		   linkflags_t  *extraFlags = NULL  ) ;

class Links {

public:
	Links();
	~Links();
	void reset();

	// call this before calling hash() and write()
	bool set ( bool useRelNoFollow ,
		   Xml *xml, 
		   Url *parentUrl , 
		   bool setLinkHashes ,
		   // use NULL for this if you do not have a baseUrl
		   Url *baseUrl , 
		   int32_t version, 
		   int32_t niceness ,
		   bool  parentIsPermalink ,
		   Links *oldLinks         , // for LF_OLDLINKS flag
		   // this is used by Msg13.cpp to quickly get ptrs
		   // to the links in the document, no normalization!
		   bool doQuickSet = false ,
		   class SafeBuf *diffbotReply = NULL );

	// set from a simple text buffer
	bool set ( char *buf , int32_t niceness ) ;

	bool print ( SafeBuf *sb ) ;

	// Link in ascii text
	bool addLink(char *link,int32_t linkLen,int32_t nodeNum,bool setLinkHashes,
		     int32_t titleRecVersion, int32_t niceness , bool isRSS ,
		     int32_t tagId , linkflags_t flagsArg );

	// . link spam functions. used by linkspam.cpp's setLinkSpam().
	// . also used by Linkdb.cpp to create a linkdb list to add to rdb
	// . we do not add outlinks to linkdb if they are "link spam"
	bool setAllSpamBits ( char *note ) { m_spamNote = note; return true; }
	void setSpamBit  ( char *note , int32_t i ) { m_spamNotes[i] = note; }
	void setSpamBits ( char *note , int32_t i ) { 
		for (int32_t j=i ; j<m_numLinks ; j++) m_spamNotes[j] = note;};
	// . m_spamNote is set if it is ALL link spam... set above
	// . internal outlinks are never considered link spam since we "dedup"
	//   them by ip in Msg25/LinkInfo::merge() anyway
	bool isLinkSpam ( int32_t i ) { 
		if ( isInternalDom(i) ) return false; 
		if ( m_spamNote       ) return true; 
		return m_spamNotes[i]; 
	}
	const char *getSpamNote ( int32_t i ) {
	        if ( isInternalDom(i) ) return "good";
		if ( m_spamNote       ) return m_spamNote;
		if ( m_spamNotes[i]   ) return m_spamNotes[i];
		return "good";
	}

	// we do not subjugate same domain links to link spam detection in
	// linkspam.cpp::setLinkSpam()
	bool isInternalDom  ( int32_t i ) { return (m_linkFlags[i] & LF_SAMEDOM);}

	// . returns false and sets g_errno on error
	// . remove links from our m_linkPtrs[] if they are in "old"
	bool flagOldLinks ( class Links *old ) ;

	// . does link #n have link text that has at least 1 alnum char in it?
	// . used for scoring link: terms to make link-text adds more efficient
	bool hasLinkText ( int32_t n, int32_t version );

	// . returns false on error and sets errno
	// . get our outgoing link text for this url
	// . store it into "buf"
	int32_t getLinkText ( char  *linkee ,
			   bool   getSiteLinkInfo ,
			   char  *buf       ,
			   int32_t   maxBufLen ,
			   //bool   filter    ,
			   char **itemPtr   ,
			   int32_t  *itemLen   ,
			   int32_t   *retNode1 , // = NULL ,
			   int32_t   *retLinkNum ,
			   int32_t    niceness );

	int32_t getLinkText2 ( int32_t i,
			   char  *buf       ,
			   int32_t   maxBufLen ,
			   //bool   filter    ,
			   char **itemPtr   ,
			   int32_t  *itemLen   ,
			   int32_t   *retNode1 , // = NULL ,
			   int32_t    niceness );

	// quick n dirty check for substrings in linktext
	char *linkTextSubstr(int32_t linkNum, char *string, int32_t niceness);

	// returns list of \0 terminated, normalized links
	char          *getLinkBuf    () { 
		return m_allocBuf; 
	};
	int32_t           getLinkBufLen () { 
		if ( m_allocBuf ) return m_bufPtr - m_allocBuf;
		return 0;
	};
	int32_t           getNumLinks   () { return m_numLinks; };

	// was there a link to gigablast.com or www.gigablast.com?
	bool           linksToGigablast() { return m_linksToGigablast; };

	int32_t           getLinkLen    ( int32_t i ) { return m_linkLens  [i]; };
	char          *getLink       ( int32_t i ) { return m_linkPtrs  [i]; };
	char          *getLinkPtr    ( int32_t i ) { return m_linkPtrs  [i]; };
	uint32_t       getLinkHash32 ( int32_t i ) { 
		return (uint32_t)m_linkHashes[i]; };
	uint64_t       getLinkHash64 ( int32_t i ) { return m_linkHashes[i]; };
	uint64_t       getHostHash64 ( int32_t i ) { return m_hostHashes[i]; };
	int32_t           getDomHash32  ( int32_t i ) { return m_domHashes[i]; };
	int32_t           getNodeNum    ( int32_t i ) { return m_linkNodes[i];  };
	bool hasRelNoFollow() { return m_hasRelNoFollow; };

	char *getLinkHost ( int32_t i , int32_t *hostLen ) ;

	int32_t findLinkNum(char* url, int32_t urlLen);

	int32_t getMemUsed () { return m_allocSize; };

	bool hasSelfPermalink ( ) { return m_hasSelfPermalink; };
	bool hasRSSOutlink    ( ) { return m_hasRSSOutlink; };
	bool hasSubdirOutlink ( ) { return m_hasSubdirOutlink; };

	Xml   *m_xml;
	Url   *m_baseUrl;
	Url   *m_parentUrl;
	bool   m_parentIsPermalink;

	char  *m_baseSite;
	int32_t   m_baseSiteLen;

	// set <base href>, if any, into m_tmpUrl so m_baseUrl can point to it
	Url    m_tmpUrl;

	// . we store all links in this buf
	// . each link ends in a \0
	// . convenient for passing to Msg10
	// . each link is in complete http:// format with base url, etc.
	char   *m_buf;// [LINK_BUF_SIZE];
	// pointer to the end of the buffer
	char  *m_bufPtr;

	// this is non-NULL if all outlinks are considered link spam, 
	// otherwise, individual outlinks will have their m_spamNotes[i] be
	// non-NULL, and point to the string that describes why they are 
	// link spam.
	char  *m_spamNote;

	char          **m_linkPtrs;//   [MAX_LINKS];
	int32_t           *m_linkLens;//   [MAX_LINKS];
	int32_t           *m_linkNodes;//  [MAX_LINKS];
	uint64_t       *m_linkHashes;// [MAX_LINKS];
	uint64_t       *m_hostHashes;// [MAX_LINKS];
	int32_t           *m_domHashes;// [MAX_LINKS];
	linkflags_t    *m_linkFlags;
	char           *m_linkContactyTypes; // for XmlDoc's isContacty() algo
	char          **m_spamNotes;

	bool m_doQuickSet;

	// do we have an rss link? i.e. are we an RSS feed
	bool           m_hasRSS;
	bool           m_isFeedBurner;

	char          *m_linkBuf;
	int32_t           m_allocLinks;
	int32_t           m_numLinks;
	int32_t           m_numNodes;

	// . should we extract redirects from links? (like for yahoo's links)
	// . this is set based on the SiteRec
	//bool m_extractRedirects;

	bool m_linksToGigablast;
	
	bool m_hasRelNoFollow;

	bool m_stripIds;
	
	uint32_t  m_allocSize;
	char          *m_allocBuf;

	// queue the blog roll links into the turk for voting
	bool queueBlogRoll ( class TagRec **tagRecPtrs , int32_t niceness ) ;
	bool  m_addSiteRootFlags;
	char *m_coll;

	char  m_flagged;

	char  m_hasSelfPermalink;
	char  m_hasRSSOutlink;
	char  m_hasSubdirOutlink;
	char *m_rssOutlinkPtr;
	int32_t  m_rssOutlinkLen;

	// . returns  0 if probably not a permalink
	// . returns  1 if probably is a permalink
	// . returns -1 if not enough information to make a decision
	char isPermalink ( char **note ) { return -1; };

	int32_t m_numOutlinksAdded;
};

#endif
