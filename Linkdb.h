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

#define LDB_MAXSITERANK 0xff
#define LDB_MAXHOPCOUNT 0xff
#define LDB_MAXURLHASH  0x00007fffffffffffLL

#define LINKDBEPOCH (1325376000-365*86400*4)

#include "Conf.h"
#include "Rdb.h"
#include "DiskPageCache.h"
#include "Titledb.h"

long getSiteRank ( long sni ) ;

class Linkdb {
 public:
	void reset();

	bool init    ( );
	bool init2 ( long treeMem );
	bool verify  ( char *coll );
	bool addColl ( char *coll, bool doVerify = true );

	bool setMetaList ( char    *metaList        ,
			   char    *metaListEnd     ,
			   class XmlDoc  *oldDoc          ,
			   class XmlDoc  *newDoc          ,
			   long     niceness        ,
			   long    *numBytesWritten ) ;

	// this makes a "url" key
	key224_t makeKey_uk ( uint32_t  linkeeSiteHash32 ,
			      uint64_t  linkeeUrlHash64  ,
			      bool      isLinkSpam     ,
			      unsigned char linkerSiteRank , // 0-15 i guess
			      unsigned char linkerHopCount ,
			      uint32_t  linkerIp       ,
			      long long linkerDocId    ,
			      unsigned long      discoveryDate  ,
			      unsigned long      lostDate       ,
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

	/*
	long long getUrlHash ( Url *url ) {
		// . now use the probable docid (UNMASKED)
		// . this makes it so we reside on the same host as the
		//   titleRec and spiderRec of this url. that way Msg14
		//   is assured of adding the Linkdb rec before the Spiderdb
		//   rec and therefore of getting its parent as an inlinker.
		long long h = g_titledb.getProbableDocId(url,false);
		// now it is the lower 47 bits since we added the spam bit
		return h & 0x00007fffffffffffLL;	}
	*/

	//
	// accessors for "url" keys in linkdb
	//

	unsigned long getLinkeeSiteHash32_uk ( key224_t *key ) {
		return (key->n3) >> 32; }

	unsigned long long getLinkeeUrlHash64_uk ( key224_t *key ) {
		unsigned long long h = key->n3;
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

	//unsigned char getLinkerHopCount_uk ( key224_t *k ) {
	//	return (k->n2 >> 32) & 0xff; 
	//}
	
	long getLinkerIp_uk ( key224_t *k ) {
		unsigned long ip ;
		// the most significant part of the ip is the lower byte!!!
		ip = (unsigned long)((k->n2>>8)&0x00ffffff);
		ip |= ((k->n2>>8) & 0xff000000);
		return ip;
	}

	void setIp32_uk ( void *k , unsigned long ip ) {
		char *ips = (char *)&ip;
		char *ks = (char *)k;
		ks[16] = ips[3];
		ks[15] = ips[2];
		ks[14] = ips[1];
		ks[13] = ips[0];
	}


	// we are missing the lower byte, it will be zero
	long getLinkerIp24_uk ( key224_t *k ) {
		return (long)((k->n2>>8)&0x00ffffff); 
	}

	long long getLinkerDocId_uk( key224_t *k ) {
		unsigned long long d = k->n2 & 0xff;
		d <<= 30;
		d |= k->n1 >>34;
		return d;
	}

	// . in days since jan 1, 2012 utc
	// . timestamp of jan 1, 2012 utc is 1325376000
	long getDiscoveryDate_uk ( void *k ) {
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
	void setDiscoveryDate_uk ( void *k , long date ) {
		// subtract jan 1 2012
		date -= LINKDBEPOCH;
		// convert into days
		date /= 86400;
		// sanity
		if ( date > 0x3fff || date < 0 ) { char *xx=NULL;*xx=0; }
		// clear old bits
		((key224_t *)k)->n1 &= 0xffffffff03ffffLL;
		// scale us into it
		((key224_t *)k)->n1 |= ((unsigned long long)date) << 18;
	}

	long getLostDate_uk ( void *k ) {
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
	void setLostDate_uk ( void *k , long date ) {
		// subtract jan 1 2012
		date -= LINKDBEPOCH;
		// convert into days
		date /= 86400;
		// sanity
		if ( date > 0x3fff || date < 0 ) { char *xx=NULL;*xx=0; }
		// clear old bits
		((key224_t *)k)->n1 &= 0xffffffffffff0003LL;
		// scale us into it
		((key224_t *)k)->n1 |= ((unsigned long long)date) << 2;
	}

	uint32_t getLinkerSiteHash32_uk( void *k ) {
		uint32_t sh32 = ((key224_t *)k)->n1 & 0x00000003;
		sh32 <<= 30;
		sh32 |= ((key224_t *)k)->n0 >> 2;
		return sh32;
	}

	Rdb           *getRdb()           { return &m_rdb; };

	DiskPageCache *getDiskPageCache () { return &m_pc; };
	DiskPageCache m_pc;

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

//#include "LinkText.h"
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

//#define MSG25_MAX_REPLY_SIZE   1024

void  handleRequest25 ( UdpSlot *slot , long netnice ) ;

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
	bool getLinkInfo ( char      *site ,
			   char      *url  ,
			   bool       isSiteLinkInfo ,
			   long       ip                  ,
			   long long  docId               ,
			   char      *coll                ,
			   char      *qbuf                ,
			   long       qbufSize            ,
			   void      *state               ,
			   void (* callback)(void *state) ,
			   bool       isInjecting         ,
			   SafeBuf   *pbuf                ,
			   class XmlDoc *xd ,
			   long       siteNumInlinks      ,
			   //long       sitePop             ,
			   LinkInfo  *oldLinkInfo         ,
			   long       niceness            ,
			   bool       doLinkSpamCheck     ,
			   bool       oneVotePerIpDom     ,
			   bool       canBeCancelled      ,
			   long       lastUpdateTime      ,
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
			   long       ourHostHash32 , // = 0 ,
			   long       ourDomHash32 ); // = 0 );
	Msg25();
	~Msg25();
	void reset();

	// . based on the linkInfo, are we banned/unbanned/clean/dirty
	// . the linking url may set these bits in it's link: term score
	// . these bits are set based on the linker's siteRec
	//bool isBanned   () {return (m_msg18.isBanned  () || m_isBanned  ); };
	//bool isUnbanned () {return (m_msg18.isUnbanned() || m_isUnbanned); };
	//bool isDirty    () {return (m_msg18.isDirty   () || m_isDirty   ); };
	//bool isClean    () {return (m_msg18.isClean   () || m_isClean   ); };

	// we also set these bits from looking at url's link scores
	//bool m_isBanned;
	//bool m_isUnbanned;
	//bool m_isDirty;
	//bool m_isClean;

	//char getMinInlinkerHopCount () { return m_minInlinkerHopCount; };


	class Msg20Reply *getLoser (class Msg20Reply *r, class Msg20Reply *p);
	char             *isDup    (class Msg20Reply *r, class Msg20Reply *p);

	bool addNote ( char *note , long noteLen , long long docId );

	class LinkInfo *getLinkInfo () { return m_linkInfo; };

	// private:
	// these need to be public for wrappers to call:
	bool gotTermFreq ( bool msg42Called ) ;
	bool getRootTitleRec ( ) ;
	bool gotRootTitleRec ( );
	bool gotDocId ( ) ;
	//bool gotRootQuality2 ( ) ;
	bool gotRootLinkText ( ) ;
	bool gotRootLinkText2 ( ) ;
	bool getLinkingDocIds ( ) ;
	bool gotList     ( ) ;
	bool gotClusterRecs ( ) ;
	bool sendRequests ( );
	bool gotLinkText  ( class Msg20Request *req ) ; //long j );
	bool gotMsg25Reply ( ) ;
	bool doReadLoop ( );

	// input vars
	//Url       *m_url;
	//Url        m_tmpUrl;
	char *m_url;
	char *m_site;

	long m_ourHostHash32;
	long m_ourDomHash32;

	long m_round;
	uint64_t m_linkHash64;
	key224_t m_nextKey;

	bool       m_retried;
	bool       m_prependWWW;
	bool       m_onlyNeedGoodInlinks;
	bool       m_getLinkerTitles;
	long long  m_docId;
	char      *m_coll;
	//long       m_collLen;
	LinkInfo  *m_linkInfo;
	void      *m_state;
	void     (* m_callback) ( void *state );

	long m_siteNumInlinks;
	//long m_sitePop;
	long m_mode;
	bool m_printInXml;
	class XmlDoc  *m_xd;

	// private:

	// url info
	long m_ip;
	long m_top;
	long m_midDomHash;

	bool m_gettingList;

	// hack for seo pipeline in xmldoc.cpp
	long m_hackrd;
	
	// . we use Msg0 to get an indexList for href: terms 
	// . the href: IndexList's docIds are docs that link to us
	// . we now use Msg2 since it has "restrictIndexdb" support to limit
	//   indexdb searches to just the root file to decrease disk seeks
	Msg0  m_msg0;
	RdbList m_list;

	class Inlink *m_k;

	// for getting the root title rec so we can share its pwids
	Msg22 m_msg22;

	long      m_maxNumLinkers;

	// should we free the m_replyPtrs on destruction? default=true
	bool m_ownReplies;

	// Now we just save the replies we get back from Msg20::getSummary()
	// We point to them with a LinkTextReply, which is just a pointer
	// and some access functions. 
 	Msg20Reply    *m_replyPtrs  [ MAX_LINKERS ];
	long           m_replySizes [ MAX_LINKERS ];
	long           m_numReplyPtrs;

	//LinkText *m_linkTexts [ MAX_LINKERS ];
	Msg20        m_msg20s        [ MAX_MSG20_OUTSTANDING ];
	Msg20Request m_msg20Requests [ MAX_MSG20_OUTSTANDING ];
	char         m_inUse         [ MAX_MSG20_OUTSTANDING ];
	// for "fake" replies
	Msg20Reply   m_msg20Replies  [ MAX_MSG20_OUTSTANDING ];

	// make this dynamic to avoid wasting so much space when must pages
	// have *very* few inlinkers. make it point to m_dbuf by default.
	//long long m_docIds    [ MAX_DOCIDS_TO_SAMPLE ];

	//char      m_hasLinkText [ MAX_LINKERS ];

	// make this dynamic as well! (see m_docIds comment above)
	//char      m_scores    [ MAX_DOCIDS_TO_SAMPLE ];

	long      m_numDocIds;
	long      m_cblocks;
	long      m_uniqueIps;

	// new stuff for getting term freqs for really huge links: termlists
	//long long m_termId;
	//Msg42     m_msg42;
	long      m_minRecSizes;
	//long      m_termFreq;

	// Msg20 is for getting the LinkInfo class from this same url's
	// titleRec from another (usually much larger) gigablast cluster/netwrk
	Msg20     m_msg20; 

	// how many msg20s have we sent/recvd?
	long      m_numRequests;
	long      m_numReplies;  

	long      m_linkSpamOut;

	// have we had an error for any transaction?
	long      m_errno;

	// this is used for link ban checks
	//Msg18     m_msg18;

	SafeBuf  *m_pbuf;
	// copied from CollectionRec
	bool  m_oneVotePerIpDom           ;
	bool  m_doLinkSpamCheck           ;
	bool  m_isInjecting               ;
	char  m_canBeCancelled            ;
	long  m_lastUpdateTime            ;

	Multicast m_mcast;

	//char **m_statusPtr;

	long m_good;
	long m_errors;
	long m_noText;
	long m_reciprocal;

	bool m_spideringEnabled;

	//TermTable m_ipTable;
	//long      m_ipdups;
	long      m_dupCount;
	long      m_vectorDups;
	long      m_spamLinks;
	long      m_niceness;
	long      m_numFromSameIp;
	long      m_sameMidDomain;

	// stats for allow some link spam inlinks to vote
	long m_spamCount;
	long m_spamWeight;
	long m_maxSpam;

	char m_siteQuality;
	long m_siteNumFreshInlinks;

	// this is used for the linkdb list
	//HashTableT <long, char> m_ipTable;
	HashTableX m_ipTable;
	HashTableX m_fullIpTable;
	HashTableX m_firstIpTable;

	// this is for deduping docids because we now combine the linkdb
	// list of docids with the old inlinks in the old link info
	//HashTableT <long long, char> m_docIdTable;
	HashTableX m_docIdTable;

	// special counts
	long      m_ipDupsLinkdb;
	long      m_docIdDupsLinkdb;
	long      m_linkSpamLinkdb;
	long      m_lostLinks;
	long      m_ipDups;

	unsigned long  m_groupId;
	long long      m_probDocId;

	LinkInfo *m_oldLinkInfo;

	char      m_buf [ MAX_NOTE_BUF_LEN ];
	char     *m_bufPtr;
	char     *m_bufEnd;
	HashTableX m_table;

	char      m_request [ MSG25_MAX_REQUEST_SIZE ];
	long      m_requestSize;

	//char      m_replyBuf [ MSG25_MAX_REQUEST_SIZE ];

	// hop count got from linkdb
	//char      m_minInlinkerHopCount;

	HashTableX m_adBanTable;

	// for setting <absScore2> or determining if a search results 
	// inlinkers also have the query terms. buzz.
	char *m_qbuf;
	long  m_qbufSize;
};

// used by Msg25::addNote()
#define MAX_ENTRY_DOCIDS 10
class NoteEntry {
public:
	long             m_count;
	char            *m_note;
	long long        m_docIds[MAX_ENTRY_DOCIDS];
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

	long   getStoredSize  ( ) { return m_size; };
	long   getSize        ( ) { return m_size; };
	time_t getLastUpdated ( ) { return m_lastUpdated; };

	//long   getNumTotalInlinks   ( ) { 
	//	if ( this == NULL ) return 0; return m_numTotalInlinks; };
	long   getNumLinkTexts ( ) { 
		if ( this == NULL ) return 0; return m_numStoredInlinks; };

	long   getNumGoodInlinks   ( ) { 
		if ( this == NULL ) return 0; return m_numGoodInlinks; };

	// how many of the inlinks are from the same ip top?
	//long   getNumInternalInlinks( ) { 
	//	if ( this == NULL ) return 0; return m_numInlinksInternal; };

	// how many inlinks are from a different ip top?
	//long   getNumExternalInlinks( ) { 
	//	if ( this == NULL ) return 0; 
	//	return m_numInlinks - m_numInlinksInternal; };

	//long   getNumInlinksExtrapolated ( ){
	//	if ( this == NULL ) return 0;return m_numInlinksExtrapolated;};

	// update them for each Inlink. calls for each Inlink.
	void   updateStringPtrs ( );

	// this returns a ptr to a static Inlink in some cases, so beware
	class Inlink *getNextInlink ( class Inlink *k ) ;

	// do not call this one
	class Inlink *getNextInlink2 ( class Inlink *k ) ;

	bool getItemXml ( Xml *xml , long niceness ) ;

	bool hasLinkText ( );

	/*
	bool hash ( TermTable      *table                  ,
		    long            externalLinkTextWeight ,
		    long            internalLinkTextWeight ,
		    long            ip                     ,
		    long            version                ,
		    long            siteNumInlinks         ,
		    TermTable      *countTable             ,
		    char           *note                   ,
		    long            niceness               ) ;
	*/

	// for PageTitledb
	bool print ( class SafeBuf *sb , char *coll );

	// adds up the page pops of the inlinkers as long as they are from
	// a different site than "u" is
	//long computePagePop ( class Url *u , char *coll ) ;

	bool hasRSSItem();

	// a small header, followed by the buf of "Inlinks", m_buf[]
	char       m_version;
	// we only keep usually no more than 10 or so internal guys, so this
	// can be a single byte
	char       m_numInlinksInternal;
	char       m_reserved1; // was m_siteRootQuality
	char       m_reserved2;
	long       m_size;
	time_t     m_lastUpdated;
	// this is precisely how many inlinks we stored in m_buf[] below
	long       m_numStoredInlinks;//m_numTotalInlinks;
	// . only valid if titleRec version >= 119, otherwise its always 0
	// . this count includes internal as well as external links, i.e. just
	//   the total inlinks we got, counting at most one inlink per page. 
	//   it is not very useful i guess, but steve wants it.
	long       m_totalInlinkingDocIds;//reserved3;
	// . how many inlinks did we have that were "good"?
	// . this is typically less than the # of Inlinks stored in m_buf below
	//   because it does not include internal cblock inlinks
	long       m_numGoodInlinks;
	// . # of c blocks linking to this page/site
	// . only valid if titlerecversion >= 119
	// . includes your own intenral cblock
	long       m_numUniqueCBlocks;//m_pagePop;
	// . # of IPs linking to this page/site
	// . only valid if titlerecversion >= 119
	// . includes your own internal ip
	long       m_numUniqueIps;//numInlinksFresh; // was m_reserved3;
	//long       m_sitePop;
	//long       m_siteNumInlinks;

	// serialize "Inlinks" into this buffer, m_buf[]
	char   m_buf[0];
};


class Inlink { // : public Msg {

 public:

	long  *getFirstSizeParm () { return &size_urlBuf; };
	long  *getLastSizeParm  () { return &size_rssItem; };
	char **getFirstStrPtr   () { return &ptr_urlBuf; };
	long   getBaseSize      () { return sizeof(Inlink);};
	char  *getStringBuf     () { return m_buf; };

	long getBaseNumStrings() { 
		return (char **)&size_urlBuf - (char **)&ptr_urlBuf; };
	
	// zero ourselves out
	void reset() ;

	void set ( class Msg20Reply *reply );

	// set ourselves from a serialized older-versioned Inlink
	void set2 ( class Inlink *old );

	bool setXmlFromRSS      ( Xml *xml , long niceness ) ;
	//bool setXmlFromLinkText ( Xml *xml ) ;

	// . set a Msg20Reply from ourselves
	// . Msg25 uses this to recycle old inlinks that are now gone
	// . allows us to preserve ptr_rssInfo, etc.
	void setMsg20Reply ( class Msg20Reply *r ) ;

	long getStoredSize ( ) ;

	// . return ptr to the buffer we serialize into
	// . return NULL and set g_errno on error
	char *serialize ( long *retSize     ,
			  char *userBuf     ,
			  long  userBufSize ,
			  bool  makePtrsRefNewBuf ) ;

	long updateStringPtrs ( char *buf );

	// returns a ptr into a static buffer
	char *getLinkTextAsUtf8 ( long *len = NULL ) ;

	long       m_ip                  ;
	long long  m_docId               ;
	long       m_firstSpidered       ;
	long       m_lastSpidered        ;
	long	   m_nextSpiderDate	 ;
	// like in the titleRec, the lower 2 bits of the datedbDate have
	// special meaning. 
	// 0x00 --> datedb date extracted from content (pubdate)
	// 0x01 --> datedb date based on estimated "modified" time (moddate)
	// 0x10 --> datedb date is when same-site root was estimated to have
	//          first added that url as an outlink (discoverdate) (TODO)
	long       m_datedbDate          ;
	// this date is used as the discovery date for purposes of computing
	// LinkInfo::m_numInlinksFresh
	long       m_firstIndexedDate    ;
	//long       m_baseScore           ;
	long       m_pageNumInlinks      ;
	long       m_siteNumInlinks      ;
	// record the word position we hashed this link text with
	// so we can match it to the DocIdScoringInfo stuff
	long       m_wordPosStart;//reservedc;//pagePop             ;
	long       m_firstIp;//wordPosEnd;//reservedd;//sitePop             ;

	// . long     m_reserved1           ;
	// . how many strings do we have?
	// . makes it easy to add new strings later
	uint16_t   m_numStrings          ;
	// . and were our first string ptrs starts
	// . allows us to set ourselves from an "old" Inlink 
	uint16_t   m_firstStrPtrOffset   ;

	uint16_t   m_numOutlinks         ;
	// i guess no need to store this stuff if we are storing the url
	// in ptr_urlBuf below. we can call Url::set() then Url::getHostHash()
	// NO, because the site is now only contained in the TagRec now and
	// we compute the site in SiteGetter.cpp, so it is more complicated!!!
	// we get the tag rec of each outlink, and get the site from that
	// and hash that and store it here
	long       m_siteHash            ; // www.hompages.com/~fred/
	//long     m_hostHash            ; // www.ibm.com
	//long     m_midDomHash          ; // the ibm in ibm.com

	// single bit flags
	uint16_t   m_isPermalink      : 1 ;
	uint16_t   m_outlinkInContent : 1 ;
	uint16_t   m_outlinkInComment : 1 ;
	uint16_t   m_isReserved       : 1 ; // was u-n-i-c-o-d-e- bit
	uint16_t   m_isLinkSpam       : 1 ;
	//uint16_t   m_isAnomaly        : 1 ;
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

	uint16_t   m_country             ;
	uint8_t    m_language            ;
	//char     m_docQuality          ;
	char       m_siteRank;
	//char       m_ruleset             ;
	char       m_hopcount            ;
	char       m_linkTextScoreWeight ; // 0-100% (was m_inlinkWeight)

	//
	// add new non-strings right above this line
	//

	// . the url, link text and neighborhoods are stored in here
	// . no need to store vector for voting deduping in here because
	//   that use MsgE's Msg20Replies directly
	// . this is just stuff we want in the title rec
	char      *ptr_urlBuf            ;
	char      *ptr_linkText          ;
	char      *ptr_surroundingText   ; // neighborhoods
	// . this is the rss item that links to us
	// . if calling Msg25::getLinkInfo() with getLinkerTitles set to
	//   true then this is the title!
	char      *ptr_rssItem           ;
	// . zakbot and the turk categorize site roots, and kids inherit
	//   the categories from their parent inlinkers
	// . we can't really use tagdb cuz that operates on subdirectories
	//   which may not be upheld for some sites. (like cnn.com!, the 
	//   stories are not proper subdirectories...)
	// . so inherit the category from our inlinkers. "sports", "world", ...
	// . comma-separated (in ascii)
	char      *ptr_categories        ;
	// . augments our own gigabits vector, used for finding related docs
	// . used along with the template vector for deduping pgs at index time
	// . now we used for finding similar docs AND categorizing
	// . comma-separated
	// . each gigabit has a count in []'s. score in body x1, title x5,
	//   and inlink text x5. i.e. "News[10],blue devils[5],... 
	// . always in UTF-8
	char      *ptr_gigabitQuery      ;
	// . the html tag vector. 
	// . used for deduping voters (anti-spam tech)
	// . used along with the gigabit vector for deduping pgs at index time
	// . now we used for finding similar docs and for categorizing (spam)
	char      *ptr_templateVector    ;

	//
	// add new strings right above this line
	//

	long       size_urlBuf           ;
	long       size_linkText         ;
	long       size_surroundingText  ;
	long       size_rssItem          ;
	long       size_categories       ;
	long       size_gigabitQuery     ;
	long       size_templateVector   ;


	char       m_buf[0]              ;
};

// . this function is normally called like "info = makeLinkInfo()"
//   to create a new LinkInfo based on a bunch of Msg20 replies
// . returns NULL and sets g_errno on error
LinkInfo *makeLinkInfo ( char        *coll                    ,
			 long         ip                      ,
			 //char       siteRootQuality         ,
			 //long         sitePop                 ,
			 long         siteNumInlinks          ,
			 Msg20Reply **replies                 ,
			 long         numReplies              ,
			 //long         extrapolated            ,
			 //long         xfactor                 ,
			 // if link spam give this weight
			 long         spamWeight              ,
			 bool         oneVotePerIpTop         ,
			 long long    linkeeDocId             ,
			 long         lastUpdateTime          ,
			 bool         onlyNeedGoodInlinks      ,
			 long         niceness                ,
			 class Msg25 *msg25 ) ;

// . set from the Msg20 replies in MsgE
// . Msg20 uses this to set the LinkInfo class to the "outlinks"
// . if an outlink has no docid, it is not stored, because it was
//   therefore not in the index.
LinkInfo *makeLinkInfo ( class MsgE *m , long niceness ) ;


////////
//
// LINKS CLASS
//
////////

//typedef short linkflags_t;
typedef long linkflags_t;

// all the links (urls), separated by \0's, are put into a buf of this size
#define LINK_BUF_SIZE (100*1024)

// we allow up to this many links to be put into m_buf
//#define MAX_LINKS      10000

//#define MSR_HAD_REC      0x80
//#define NUM_TYPES_IN_MSR 2

//class MiniSiteRec {
//public:
//	bool hadRec() { return m_flags & MSR_HAD_REC; };
//	short    m_siteOffset;
//	short    m_siteLen;
//	long     m_filenum;
//	uint8_t  m_flags;
//	char     m_siteQuality;
//	SiteType m_types[NUM_TYPES_IN_MSR];
//	SiteType m_lang;
//};

// Link Flags
#define LF_SAMEHOST      0x0001 // same hostname
#define LF_SAMEDOM       0x0002 // same domain
#define LF_SITEROOT      0x0004 // for blogrolls
#define LF_SAMESITE      0x0008 // only get offsite outlink info in Msg20.cpp
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

bool isPermalink ( //char        *coll        ,
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
		   long version, 
		   long niceness ,
		   //bool addSiteRootFlag = false ,
		   //char *coll           = NULL  ,
		   bool  parentIsPermalink , // = false ,
		   Links *oldLinks         , // for LF_OLDLINKS flag
		   // this is used by Msg13.cpp to quickly get ptrs
		   // to the links in the document, no normalization!
		   bool doQuickSet = false );

	// set from a simple text buffer
	bool set ( char *buf , long niceness ) ;

	// Link in ascii text
	bool addLink(char *link,long linkLen,long nodeNum,bool setLinkHashes,
		     long titleRecVersion, long niceness , bool isRSS ,
		     long tagId , linkflags_t flagsArg );

	// . link spam functions. used by linkspam.cpp's setLinkSpam().
	// . also used by Linkdb.cpp to create a linkdb list to add to rdb
	// . we do not add outlinks to linkdb if they are "link spam"
	bool setAllSpamBits ( char *note ) { m_spamNote = note; return true; }
	void setSpamBit  ( char *note , long i ) { m_spamNotes[i] = note; }
	void setSpamBits ( char *note , long i ) { 
		for (long j=i ; j<m_numLinks ; j++) m_spamNotes[j] = note;};
	// . m_spamNote is set if it is ALL link spam... set above
	// . internal outlinks are never considered link spam since we "dedup"
	//   them by ip in Msg25/LinkInfo::merge() anyway
	bool isLinkSpam ( long i ) { 
		if ( isInternalDom(i) ) return false; 
		if ( m_spamNote       ) return true; 
		return m_spamNotes[i]; 
	}
	const char *getSpamNote ( long i ) {
	        if ( isInternalDom(i) ) return "good";
		if ( m_spamNote       ) return m_spamNote;
		if ( m_spamNotes[i]   ) return m_spamNotes[i];
		return "good";
	}

	// for spidering links purposes, we consider "internal" to be same 
	// hostname
	bool isInternal     ( long i ) {return (m_linkFlags[i] & LF_SAMEHOST);}
	bool isInternalHost ( long i ) {return (m_linkFlags[i] & LF_SAMEHOST);}

	// we do not subjugate same domain links to link spam detection in
	// linkspam.cpp::setLinkSpam()
	bool isInternalDom  ( long i ) { return (m_linkFlags[i] & LF_SAMEDOM);}

	bool isOld ( long i ) { return m_linkFlags[i] & LF_OLDLINK; };

	// remove all links from the link buf that do not have the same 
	// hostname as "url". Used in Msg14 to avoid adding such links and 
	// avoid getting link info for such links.
	//void removeExternalLinks ( );

	// . returns false and sets g_errno on error
	// . remove links from our m_linkPtrs[] if they are in "old"
	bool flagOldLinks ( class Links *old ) ;

	// does this page have a subtantial amount of links with naughty words
	// in their hostnames?
	//bool isPageDirty ( );

	// . hash the link: and href: terms
	// . we need SiteRec of the url that supplied these links so we
	//   might set hi bits in the link: terms scores to represent:
	//   banned, unbanned, clean, dirty links if the SiteRec says so
	// . returns false and set errno on error
	/*
	bool hash ( TermTable *table , // SiteRec *sr ,
		    Url *url , 
		    Url *redirUrl , 
		    long version, 
		    long niceness ,
		    bool isRSSFeed );
	*/

	// hash for Linkdb keys
	//bool hash ( class HashTableX *dt , // <key128_t,char> *dt ,
	//	    class XmlDoc     *xd ,
	//	    long        niceness );

	// . does link #n have link text that has at least 1 alnum char in it?
	// . used for scoring link: terms to make link-text adds more efficient
	bool hasLinkText ( long n, long version );

	// . returns false on error and sets errno
	// . get our outgoing link text for this url
	// . store it into "buf"
	long getLinkText ( char  *linkee ,
			   bool   getSiteLinkInfo ,
			   char  *buf       ,
			   long   maxBufLen ,
			   //bool   filter    ,
			   char **itemPtr   ,
			   long  *itemLen   ,
			   long   *retNode1 , // = NULL ,
			   long   *retLinkNum ,
			   long    niceness );

	long getLinkText2 ( long i,
			   char  *buf       ,
			   long   maxBufLen ,
			   //bool   filter    ,
			   char **itemPtr   ,
			   long  *itemLen   ,
			   long   *retNode1 , // = NULL ,
			   long    niceness );

	// quick n dirty check for substrings in linktext
	char *linkTextSubstr(long linkNum, char *string, long niceness);

	// returns list of \0 terminated, normalized links
	char          *getLinkBuf    () { 
		return m_allocBuf; 
	};
	long           getLinkBufLen () { 
		if ( m_allocBuf ) return m_bufPtr - m_allocBuf;
		return 0;
		//return m_allocBuf?m_bufPtr-m_allocBuf:0; 
	};
	//unsigned long *getLinkHashes () { return m_linkHashes; };
	long           getNumLinks   () { return m_numLinks; };

	// was there a link to gigablast.com or www.gigablast.com?
	bool           linksToGigablast() { return m_linksToGigablast; };

	long           getLinkLen    ( long i ) { return m_linkLens  [i]; };
	char          *getLink       ( long i ) { return m_linkPtrs  [i]; };
	char          *getLinkPtr    ( long i ) { return m_linkPtrs  [i]; };
	uint32_t       getLinkHash32 ( long i ) { 
		return (uint32_t)m_linkHashes[i]; };
	uint64_t       getLinkHash64 ( long i ) { return m_linkHashes[i]; };
	uint64_t       getHostHash64 ( long i ) { return m_hostHashes[i]; };
	long           getDomHash32  ( long i ) { return m_domHashes[i]; };
	long           getNodeNum    ( long i ) { return m_linkNodes[i];  };
	bool hasRelNoFollow() { return m_hasRelNoFollow; };

	char *getLinkHost ( long i , long *hostLen ) ;

	long findLinkNum(char* url, long urlLen);

	long getMemUsed () { return m_allocSize; };

	bool hasSelfPermalink ( ) { return m_hasSelfPermalink; };
	bool hasRSSOutlink    ( ) { return m_hasRSSOutlink; };
	bool hasSubdirOutlink ( ) { return m_hasSubdirOutlink; };

	// . make an RdbList for adding to spiderdb
	// . returns -1 and sets g_errno on error
	// . otherwise returns # of outlinks added to "list"
	// . used by Msg14.cpp for adding outlinks to spiderdb
	/*
	char *addToMetaList ( char            *p           , // metalist start
			      char            *pend        , // metalist end
			      class TitleRec  *tr          , 
			      class XmlDoc    *old         ,
			      char           *coll         ,
			      class MsgE     *msge         ,
			      long            niceness     ,
			      Url            *quickLink         = NULL  ,
			      linkflags_t     quickLinkFlags    = 0     ,
			      bool            isAddUrl          = false ,
			      bool            forceAll          = false ,
			      bool            skipExternalLinks = false ,
			      bool            unforceAll        = false ,
			      long            explicitPriority  = -1    );
	*/

	// private:

	Xml   *m_xml;
	Url   *m_baseUrl;
	Url   *m_parentUrl;
	bool   m_parentIsPermalink;

	char  *m_baseSite;
	long   m_baseSiteLen;

	// set <base href>, if any, into m_tmpUrl so m_baseUrl can point to it
	Url    m_tmpUrl;

	// . we store all links in this buf
	// . each link ends in a \0
	// . convenient for passing to Msg10
	// . each link is in complete http:// format with base url, etc.
	char   *m_buf;// [LINK_BUF_SIZE];
	// pointer to the end of the buffer
	char  *m_bufPtr;
	// running count of the bufsize, including static and dynamic
	// long   m_bufSize; 

	// this is non-NULL if all outlinks are considered link spam, 
	// otherwise, individual outlinks will have their m_spamNotes[i] be
	// non-NULL, and point to the string that describes why they are 
	// link spam.
	char  *m_spamNote;

	char          **m_linkPtrs;//   [MAX_LINKS];
	long           *m_linkLens;//   [MAX_LINKS];
	long           *m_linkNodes;//  [MAX_LINKS];
	uint64_t       *m_linkHashes;// [MAX_LINKS];
	uint64_t       *m_hostHashes;// [MAX_LINKS];
	long           *m_domHashes;// [MAX_LINKS];
	linkflags_t    *m_linkFlags;
	char           *m_linkContactyTypes; // for XmlDoc's isContacty() algo
	char          **m_spamNotes;

	bool m_doQuickSet;

	// do we have an rss link? i.e. are we an RSS feed
	bool           m_hasRSS;
	bool           m_isFeedBurner;

	char          *m_linkBuf;
	long           m_allocLinks;
	long           m_numLinks;
	long           m_numNodes;

	// . should we extract redirects from links? (like for yahoo's links)
	// . this is set based on the SiteRec
	//bool m_extractRedirects;

	bool m_linksToGigablast;
	
	bool m_hasRelNoFollow;

	bool m_stripIds;
	
	unsigned long  m_allocSize;
	char          *m_allocBuf;

	// queue the blog roll links into the turk for voting
	bool queueBlogRoll ( class TagRec **tagRecPtrs , long niceness ) ;
	bool  m_addSiteRootFlags;
	char *m_coll;

	char  m_flagged;

	char  m_hasSelfPermalink;
	char  m_hasRSSOutlink;
	char  m_hasSubdirOutlink;
	char *m_rssOutlinkPtr;
	long  m_rssOutlinkLen;

	// . returns  0 if probably not a permalink
	// . returns  1 if probably is a permalink
	// . returns -1 if not enough information to make a decision
	char isPermalink ( char **note ) { return -1; };

	long m_numOutlinksAdded;
};

long getRegExpNumOfOutlink ( Url           *up              ,
			     linkflags_t    linkFlags       ,
			     TagRec        *tagRec          ,
			     long           quality         ,
			     long           ip              ,
			     CollectionRec *cr              ,
			     Url           *parentUrl       ,
			     long           sourceHostHash  ,
			     long           parentHopCount  ,
			     long           parentPriority  ,
			     long           hopCount        , // our hop count
			     long           h               , // hostHash
			     bool           newOutlink      , // are we new?
			     bool           isAddUrl        , // from addUrl?
			     // use -1 if unknown for these 3 values
			     char           isParentRSS     ,
			     char           parentIsNew     ,
			     char           parentIsPermalink ,
			     char           isIndexed       ); // -1--> unknown


#endif
