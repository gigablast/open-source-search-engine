// Matt Wells, copyright Nov 2002

//
// . Spider.h/.cpp contains all the code related to spider scheduling
// . Spiderdb holds the SpiderRecs which indicate the time to spider a url
// . there are 2 types of SpiderRecs: SpiderRequest and SpiderReply recs
//

#ifndef _SPIDER_H_
#define _SPIDER_H_

#define MAX_SPIDER_PRIORITIES 128
#define MAX_DAYS 365

#include "Rdb.h"
#include "Conf.h"
#include "Titledb.h"
#include "Hostdb.h"
#include "RdbList.h"
#include "RdbTree.h"
#include "HashTableX.h"
#include <time.h>
#include "Msg5.h"      // local getList()
#include "Msg4.h"
#include "hash.h"

///////////////////////////////////////
//
// QUICK OVERVIEW
//
//
// There are SpiderRequests and SpiderReplies in Spiderdb. they now use
// 16 byte keys (key128_t). when a new spiderdb rec is added to spiderdb
// in Rdb.cpp we call addSpiderRequest() or addSpiderReply(). then
// that rec might be added to the waiting tree. then the waiting tree
// is scanned for IPs that have a SpiderRequest whose spiderTime is
// <= now and we grab ONE from spiderdb and add to doledb. any host
// in our group can spider a request in doledb, but they must lock it
// by calling getLocks() first and all hosts in the group must grant
// them the lock for that url otherwise they remove all the locks and
// try again on another spiderRequest in doledb.
//
// Each group is responsible for spidering a set of IPs in spiderdb.
// and each host in the group has its own subset of those IPs for which
// it is responsible for adding to doledb. but any host in the group
// can spider any request/url in doledb provided they get the lock.
//
///////////////////////////////////////


// Overview of Spider
//
// this new spider algorithm ensures that urls get spidered even if a host
// is dead. and even if the url was being spidered by a host that suddenly went
// dead.


// Spiderdb
//
// the spiderdb holds all the SpiderRequests and SpiderReplies, each of which
// are sorted by their "firstIP" and then by their 48-bit url hash, 
// "uh48". the parentDocId is also kept in the key to prevent collisions.
// Each group of hosts is responsible for spidering a fixed set of IPs. 


// Dividing Workload by IP Address
//
// Each host is responsible for its own set of IP addresses. Each SpiderRequest
// contains an IP address called m_firstIP. It alone is responsible for adding
// SpiderRequests from this set of IPs to doledb.
// And that host is responsible for throttling spider rates of
// a all IP addresses. it does this by doling out SpiderRequests to the 
// host that should spider it only when it should spider it. the doled out 
// SpiderRequest are added to doledb using Msg4. Once in doledb, a 
// SpiderRequest is ready to be spidered by any host in the group, provided 
// that that host gets all the locks.



// "firstIP"
//
// when we lookup the ip address of the subdomain of an outlink for the first
// time we store that ip address into tagdb using the tag named "firstip".
// that way anytime we add outlinks from the same subdomain in the future they
// are guaranteed to get the same "firstip" even if the actual ip changed. this
// allows us to consistently throttle urls from the same subdomain, even if
// the subdomain gets a new ip. this also increaseses performance when looking
// up the "ips" of every outlink on a page because we are often just hitting
// tagdb, which is much faster than doing dns lookups, that might miss the 
// dns cache!


// Adding a SpiderRequest
//
// When a SpiderRequest is added to spiderdb in Rdb.cpp it calls
// SpiderColl::addSpiderRequest(). If our host is responsible for doling
// that firstIP, then we check the wait table to see if that firstIP is
// waiting to get a SpiderRequest into doledb. If it already is in the
// waiting tree, we return. otherwise, we compute the priority and next 
// spider time of the url and we add it to the waiting tree ourselves.
// then when the waiting tree is scanned it will read SpiderRequests from
// spiderdb for just that firstIP and add the best on to doledb when it is
// due to be spidered.


// Adding a SpiderReply
//
// We intercept SpiderReplies being added to Spiderdb in Rdb.cpp as well by
// calling SpiderColl::addSpiderReply().  Then we get the firstIP
// from that and we look in spiderdb to find a replacement SpiderRequest
// to add to doledb. To make this part easy we just add the firstIP to the
// waiting tree with a spiderTime of 0. so when the waiting tree scan happens
// it will pick that up and look in spiderdb for the best SpiderRequest with
// that same firstIP that can be spidered now, and then it adds that to
// doledb. (To prevent from having to scan long spiderdb lists and speed 
// things up we might want to keep a little cache that maps a firstIP to 
// a few SpiderRequests ready to be spidered).


// Deleting Dups
//
// we now remove spiderdb rec duplicates in the spiderdb merge. we also call 
// getUrlFilterNum() on each spiderdb rec during the merge to see if it is
// filtered and not indexed, and if so, we delete it. we also delete all but
// the latest SpiderReply for a given uh48/url. And we remove redundant
// SpiderRequests like we used to do in addSpiderRequest(), which means that
// the merge op needs to keep a small little table to scan in order to
// compare all the SpiderRequests in the list for the same uh48. all of this
// deduping takes place on the final merged list which is then further
// filtered by this by calling Spiderdb.cpp::filterSpiderdbRecs(RdbList *list).
// because the list is just a random piece of spiderdb, boundary issues will
// cause some records to leak through, but with enough file merge operations
// they should eventually be zapped.


// DoleDB
//
// This holds SpiderRequests that are ready to be spidered right now. A host
// in our group will call getLocks() to get the locks for a SPiderRequest
// in doledb that it wants to spider. it must receive grants from every
// alive machine in the group in order to properly get the lock. If it
// receives a rejection from one host it release the lock on all the other
// hosts. It is kind of random to get a lock, similar to ethernet collision
// detection.


// Dole IP Table
//
// m_doleIpTable (HashTableX, 96 bit keys, no data)
// Purpose: let's us know how many SpiderRequests have been doled out for
// a given firstIP
// Key is simply a 4-byte IP.
// Data is the number of doled out SpiderRequests from that IP. 
// we use m_doleIpTable for keeping counts based on ip of what is doled out. 


//
// g_doledb
//
// Purpose: holds the spider request your group is supposed to spider 
// according to getGroupIdToSpider(). 96-bit keys, data is the spider rec with 
// key. ranked according to when things should be spidered.
// <~priority>             8bits
// <spiderTime>           32bits
// <urlHash48>            48bits (to avoid collisions)
// <reserved>              7bits (used 7 bits from urlhash48 to avoid collisio)
// <delBit>                1bit
// DATA:
// <spiderRec>             Xbits (spiderdb record to spider, includes key)
// everyone in group tries to spider this shit in order.
// you call SpiderLoop::getLocks(sr,hostId) to get the lock for it before
// you can spider it. everyone in the group gets the lock request. 
// if you do not get granted lock by all alive hosts in the group then you
// call Msg12::removeAllLocks(sr,hostId). nobody tries to spider
// a doledb spider rec if the lock is granted to someone else, just skip it.
// if a doling host goes dead, then its twins will dole for it after their
// SpiderColl::m_nextReloadTime is reached and they reset their cache and
// re-scan spiderdb. XmlDoc adds the negative key to RDB_DOLEDB so that
// should remove it from doledb when the spidering is complete, and when 
// Rdb.cpp receives that negative doledb key it removes the doledbKey lock
// from m_lockTable. Furthermore, if Rdb.cpp receives a positive doledbKey
// it might update SpiderColl::m_nextDoledbKey so that it gets checked out
// for the next read of doledb when SpiderLoop::spiderDoledUrls() calls
// msg5 to read doledb records from disk.
// TODO: when a host dies, consider speeding up the reload. might be 3 hrs!
// PROBLEM: what if a host dies with outstanding locks???


// SpiderLoop::m_lockTable (HashTableX(6,8))
// Purpose: allows a host to lock a doledb key for spidering. used by Msg12
// and SpiderLoop. a host must get the lock for all its alive twins in its
// group before it can spider the SpiderRequest, otherwise, it will remove
// all the locks from the hosts that did grant it by calling
// Msg12::removeAllLocks(sr,hostId).




// . what groupId should spider/index this spider request?
// . CAUTION: NOT the same group that stores it in spiderdb!!!
// . CAUTION: NOT the same group that doles it out to spider!!!
unsigned long getGroupIdToSpider ( char *spiderRec );

// now used by xmldoc.cpp
bool isAggregator ( long siteHash32 , 
		    long domHash32 , 
		    char *url ,
		    long urlLen ) ;

// The 128-bit Spiderdb record key128_t for a rec in Spiderdb is as follows:
//
// <32 bit firstIp>             (firstIp of the url to spider)
// <48 bit normalized url hash> (of the url to spider)
// <1  bit isRequest>           (a SpiderRequest or SpiderReply record?)
// <38 bit docid of parent>     (to avoid collisions!)
// <8  bit reserved>            (was "spiderLinks"/"forced"/"retryNum")
// <1  bit delbit>              (0 means this is a *negative* key)

// there are two types of SpiderRecs really, a "request" to spider a url
// and a "reply" or report on the attempted spidering of a url. in this way
// Spiderdb is a perfect log of desired and actual spider activity.

// . Spiderdb contains an m_rdb which has SpiderRecs/urls to be spidered
// . we split the SpiderRecs up w/ the hosts in our group by IP of the url
// . once we've spidered a url it gets added with a negative spiderdb key
//   in XmlDoc.cpp
class Spiderdb {

  public:

	// reset rdb
	void reset();
	
	// set up our private rdb for holding SpiderRecs
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( long treeMem );

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	Rdb *getRdb  ( ) { return &m_rdb; };

	DiskPageCache *getDiskPageCache() { return &m_pc; };

	// this rdb holds urls waiting to be spidered or being spidered
	Rdb m_rdb;

	long long getUrlHash48 ( key128_t *k ) {
		return (((k->n1)<<16) | k->n0>>(64-16)) & 0xffffffffffffLL; };
	
	bool isSpiderRequest ( key128_t *k ) {
		return (k->n0>>(64-17))&0x01; };
	bool isSpiderReply   ( key128_t *k ) {
		return ((k->n0>>(64-17))&0x01)==0x00; };

	long long getParentDocId ( key128_t *k ) {return (k->n0>>9)&DOCID_MASK;};
	// same as above
	long long getDocId ( key128_t *k ) {return (k->n0>>9)&DOCID_MASK;};

	long getFirstIp ( key128_t *k ) { return (k->n1>>32); }
	
	key128_t makeKey ( long      firstIp     ,
			   long long urlHash48   ,
			   bool      isRequest   ,
			   long long parentDocId ,
			   bool      isDel       ) ;

	key128_t makeFirstKey ( long firstIp ) {
		return makeKey ( firstIp,0LL,false,0LL,true); };
	key128_t makeLastKey ( long firstIp ) {
		return makeKey ( firstIp,0xffffffffffffLL,true,
				 MAX_DOCID,false); };

	key128_t makeFirstKey2 ( long firstIp , long long uh48 ) {
		return makeKey ( firstIp,uh48,false,0LL,true); };
	key128_t makeLastKey2 ( long firstIp  , long long uh48 ) {
		return makeKey ( firstIp,uh48,true,MAX_DOCID,false); };

	// what group id spiders this url?
	inline unsigned long getGroupId ( long firstIp ) {
		// must be valid
		if ( firstIp == 0 || firstIp == -1 ) {char *xx=NULL;*xx=0; }
		// mix it up
		unsigned long h = (unsigned long)hash32h ( firstIp, 0x123456 );
		// get it
		return g_hostdb.getGroupId ( h % g_hostdb.m_numGroups );
	};

	// print the spider rec
	long print( char *srec );

  private:

	DiskPageCache m_pc;
};

void dedupSpiderdbList ( RdbList *list , long niceness , bool removeNegRecs );

extern class Spiderdb g_spiderdb;
extern class Spiderdb g_spiderdb2;

class SpiderRequest {

 public:

	// we now define the data so we can use this class to cast
	// a SpiderRec outright
	key128_t   m_key;
	
	long    m_dataSize;

	// this ip is taken from the TagRec for the domain of the m_url,
	// but we do a dns lookup initially if not in tagdb and we put it in
	// tagdb then. that way, even if the domain gets a new ip, we still
	// use the original ip for purposes of deciding which groupId is
	// responsible for storing, doling/throttling this domain. if the
	// ip lookup results in NXDOMAIN or another error then we generally
	// do not add it to tagdb in msge*.cpp. this ensures that any given
	// domain will always be spidered by the same group of hosts even if
	// the ip changes later on. this also increases performance since
	// we do a lot fewer dns lookups on the outlinks.
	long    m_firstIp;
	//long getFirstIp ( ) { return g_spiderdb.getFirstIp(&m_key); };

	long    m_hostHash32;
	long    m_domHash32;
	long    m_siteHash32;

	// this is computed from every outlink's tagdb record but i guess
	// we can update it when adding a spider rec reply
	long    m_siteNumInlinks;

	// . when this request was first was added to spiderdb
	// . Spider.cpp dedups the oldest SpiderRequests that have the
	//   same bit flags as this one. that way, we get the most uptodate
	//   date in the request... UNFORTUNATELY we lose m_addedTime then!!!
	time_t  m_addedTime;

	// if m_isNewOutlink is true, then this SpiderRequest is being added 
	// for a link that did not exist on this page the last time it was
	// spidered. XmlDoc.cpp needs to set XmlDoc::m_min/maxPubDate for
	// m_url. if m_url's content does not contain a pub date explicitly
	// then we can estimate it based on when m_url's parent was last
	// spidered (when m_url was not an outlink on its parent page)
	time_t  m_parentPrevSpiderTime;

	// info on the page we were harvest from
	long    m_parentFirstIp;
	long    m_parentHostHash32;
	long    m_parentDomHash32;
	long    m_parentSiteHash32;

	// the PROBABLE DOCID. if there is a collision with another docid
	// then we increment the last 8 bits or so. see Msg22.cpp.
	long long m_probDocId;

	//long  m_parentPubDate;

	// . pub date taken from url directly, not content
	// . ie. http://mysite.com/blog/nov-06-2009/food.html
	// . ie. http://mysite.com/blog/11062009/food.html
	//long    m_urlPubDate;
	// . replace this with something we need for smart compression
	// . this is zero if none or invalid
	long    m_contentHash32;

	// . each request can have a different hop count
	// . this is only valid if m_hopCountValid is true!
	long    m_hopCount;

	// . this is now computed dynamically often based on the latest
	//   m_addedTime and m_percentChanged of all the SpideRec *replies*.
	//   we may decide to change some url filters
	//   that affect this computation. so SpiderCache always changes
	//   this value before adding any SpiderRec *request* to the 
	//   m_orderTree, etc.
	//time_t  m_spiderTime;

	//
	// our bit flags
	//

	long    m_hopCountValid:1;
	// are we a request/reply from the add url page?
	long    m_isAddUrl:1; 
	// are we a request/reply from PageReindex.cpp
	long    m_isPageReindex:1; 
	// are we a request/reply from PageInject.cpp
	long    m_isPageInject:1; 
	// or from PageParser.cpp directly
	long    m_isPageParser:1; 
	// should we use the test-spider-dir for caching test coll requests?
	long    m_useTestSpiderDir:1;
	// . is the url a docid (not an actual url)
	// . could be a "query reindex"
	long    m_urlIsDocId:1;
	// does m_url end in .rss? or a related rss file extension?
	long    m_isRSSExt:1;
	// is url in a format known to be a permalink format?
	long    m_isUrlPermalinkFormat:1;
	// is url "rpc.weblogs.com/shortChanges.xml"?
	long    m_isPingServer:1; 
	// . are we a delete instruction? (from Msg7.cpp as well)
	// . if you want it to be permanently banned you should ban or filter
	//   it in the urlfilters/tagdb. so this is kinda useless...
	long    m_forceDelete:1; 
	// are we a fake spider rec? called from Test.cpp now!
	long    m_isInjecting:1; 
	// are we a respider request from Sections.cpp
	//long    m_fromSections:1;
	// a new flag. replaced above. did we have a corresponding SpiderReply?
	long    m_hadReply:1;
	// are we scraping from google, etc.?
	long    m_isScraping:1; 
	// page add url can be updated to store content for the web page
	// into titledb or something to simulate injections of yore. we can
	// use that content as the content of the web page. the add url can
	// accept it from a form and we store it right away into titledb i
	// guess using msg4, then we look it up when we spider the url.
	long    m_hasContent:1;
	// is first ip a hash of url or docid or whatever?
	long    m_fakeFirstIp:1;
	// www.xxx.com/*? or xxx.com/*?
	long    m_isWWWSubdomain:1;

	//
	// these "parent" bits are invalid if m_parentHostHash32 is 0!
	// that includes m_isMenuOutlink
	//

	// if the parent was respidered and the outlink was there last time
	// and is there now, then this is "0", otherwise, this is "1"
	long    m_isNewOutlink      :1;
	long    m_sameDom           :1;
	long    m_sameHost          :1;
	long    m_sameSite          :1;
	long    m_wasParentIndexed  :1;
	long    m_parentIsRSS       :1;
	long    m_parentIsPermalink :1;
	long    m_parentIsPingServer:1;
	long    m_parentHasAddress  :1;
	// is this outlink from content or menu?
	long    m_isMenuOutlink     :1; 


	// 
	// these bits also in SpiderReply
	//

	// was it in google's index?
	long    m_inGoogle:1;
	// expires after a certain time or if ownership changed
	// did it have an inlink from a really nice site?
	long    m_hasAuthorityInlink :1;
	long    m_hasContactInfo     :1;
	long    m_isContacty         :1;
	long    m_hasSiteVenue       :1;


	// are the 3 bits above valid? 
	// if site ownership changes might be invalidated. 
	// "inGoogle" also may expire after so long too
	long    m_inGoogleValid           :1;
	long    m_hasAuthorityInlinkValid :1;
	long    m_hasContactInfoValid     :1;
	long    m_isContactyValid         :1;
	long    m_hasAddressValid         :1;
	long    m_hasTODValid             :1;
	long    m_hasSiteVenueValid       :1;
	long    m_siteNumInlinksValid     :1;

	// . only support certain tags in url filters now i guess
	// . use the tag value from most recent SpiderRequest only
	// . the "deep" tag is popular for hitting certain sites hard
	long    m_tagDeep:1;
	// for identifying address heavy sites...
	long    m_tagYellowPages:1;
	// called XmlDoc::set4() from PageSubmit.cpp?
	//long    m_isPageSubmit:1;

	//
	// INTERNAL USE ONLY
	//

	// are we in the m_orderTree/m_doleTables/m_ipTree
	//long    m_inOrderTree:1;
	// are we doled out?
	//long    m_doled:1;
	// are we a re-add of a spiderrequest already in spiderdb added
	// from xmldoc.cpp when done spidering so that the spider request
	// gets back in the cache quickly?
	//long    m_readd:1;

	// . what url filter num do we match in the url filters table?
	// . determines our spider priority and wait time
	short   m_ufn;

	// . m_priority is dynamically computed like m_spiderTime
	// . can be negative to indicate filtered, banned, skipped, etc.
	// . for the spiderrec request, this is invalid until it is set
	//   by the SpiderCache logic, but for the spiderrec reply this is
	//   the priority we used!
	char    m_priority;

	// . this is copied from the most recent SpiderReply into here
	// . its so XMlDoc.cpp can increment it and add it to the new
	//   SpiderReply it adds in case there is another download error ,
	//   like ETCPTIMEDOUT or EDNSTIMEDOUT
	char    m_errCount;

	// we really only need store the url for *requests* and not replies
	char    m_url[MAX_URL_LEN+1];

	// . basic functions
	// . clear all
	void reset() { 
		memset ( this , 0 , (long)m_url - (long)&m_key ); 
		// -1 means uninitialized, this is required now
		m_ufn = -1;
		// this too
		m_priority = -1;
	};

	static long getNeededSize ( long urlLen ) {
		return sizeof(SpiderRequest) - (long)MAX_URL_LEN + urlLen; };

	long getRecSize () { return m_dataSize + 4 + sizeof(key128_t); }

	// how much buf will we need to serialize ourselves?
	//long getRecSize () { 
	//	//return m_dataSize + 4 + sizeof(key128_t); }
	//	return (m_url - (char *)this) + gbstrlen(m_url) + 1
	//		// subtract m_key and m_dataSize
	//		- sizeof(key_t) - 4 ;
	//};

	long getUrlLen() { return m_dataSize -
				   // subtract the \0
				   ((char *)m_url-(char *)&m_firstIp) - 1;};

	//long getUrlLen() { return gbstrlen(m_url); };

	void setKey ( long firstIp ,
		      long long parentDocId , 
		      long long uh48 , 
		      bool isDel ) ;

	void setKey ( long firstIp, long long parentDocId , bool isDel ) { 
		long long uh48 = hash64b ( m_url );
		setKey ( firstIp , parentDocId, uh48, isDel );
	}

	void setDataSize ( );

	long long  getUrlHash48  () {return g_spiderdb.getUrlHash48(&m_key); };
	long long getParentDocId (){return g_spiderdb.getParentDocId(&m_key);};

	long print( class SafeBuf *sb );

	long printToTable     ( SafeBuf *sb , char *status ,
				class XmlDoc *xd ) ;
	static long printTableHeader ( SafeBuf *sb , bool currentlSpidering ) ;
};

// . XmlDoc adds this record to spiderdb after attempting to spider a url
//   supplied to it by a SpiderRequest
// . before adding a SpiderRequest to the spider cache, we scan through
//   all of its SpiderRecReply records and just grab the last one. then
//   we pass that to ::getUrlFilterNum()
// . if it was not a successful reply, then we try to populate it with
//   the member variables from the last *successful* reply before passing
//   it to ::getUrlFilterNum()
// . getUrlFilterNum() also takes the SpiderRequest record as well now
// . we only keep the last X successful SpiderRecReply records, and the 
//   last unsucessful Y records (only if more recent), and we nuke all the 
//   other SpiderRecReply records
class SpiderReply {

 public:

	// we now define the data so we can use this class to cast
	// a SpiderRec outright
	key128_t   m_key;

	long    m_dataSize;

	// for calling getHostIdToDole()
	long    m_firstIp;
	//long getFirstIp ( ) { return g_spiderdb.getFirstIp(&m_key); };

	// we need this too in case it changes!
	long    m_siteHash32;
	// and this for updating crawl delay in m_cdTable
	long    m_domHash32;
	// since the last successful SpiderRecReply
	float   m_percentChangedPerDay;
	// when we attempted to spider it
	time_t  m_spideredTime;
	// . value of g_errno/m_indexCode. 0 means successfully indexed.
	// . might be EDOCBANNED or EDOCFILTERED
	long    m_errCode;
	// this is fresher usually so we can use it to override 
	// SpiderRequest's m_siteNumLinks
	long    m_siteNumInlinks;
	// how many inlinks does this particular page have?
	//long    m_pageNumInlinks;
	// the actual pub date we extracted (0 means none, -1 unknown)
	long    m_pubDate;
	// . SpiderRequests added to spiderdb since m_spideredTime
	// . XmlDoc.cpp's ::getUrlFilterNum() uses this as "newinlinks" arg
	//long    m_newRequests;
	// . replaced m_newRequests
	// . this is zero if none or invalid
	long    m_contentHash32;
	// in milliseconds, from robots.txt (-1 means none)
	// TODO: store in tagdb, lookup when we lookup tagdb recs for all
	// out outlinks
	long    m_crawlDelayMS; 
	// . when we basically finished DOWNLOADING it
	// . use 0 if we did not download at all
	// . used by Spider.cpp to space out urls using sameIpWait
	long long  m_downloadEndTime;
	// how many errors have we had in a row?
	//long    m_retryNum;
	// . like "404" etc. "200" means successfully downloaded
	// . we can still successfully index pages that are 404 or permission
	//   denied, because we might have link text for them.
	short   m_httpStatus;

	// . only non-zero if errCode is set!
	// . 1 means it is the first time we tried to download and got an error
	// . 2 means second, etc.
	char    m_errCount;

	// what language was the page in?
	char    m_langId;

	//
	// our bit flags
	//

	// XmlDoc::isSpam() returned true for it!
	//char    m_isSpam:1; 
	// was the page in rss format?
	long    m_isRSS:1;
	// was the page a permalink?
	long    m_isPermalink:1;
	// are we a pingserver page?
	long    m_isPingServer:1; 
	// did we delete the doc from the index?
	//long    m_deleted:1;  
	// was it in the index when we were done?
	long    m_isIndexed:1;

	// 
	// these bits also in SpiderRequest
	//

	// was it in google's index?
	long    m_inGoogle:1;
	// did it have an inlink from a really nice site?
	long    m_hasAuthorityInlink:1;
	// does it have contact info
	long    m_hasContactInfo:1;
	long    m_isContacty    :1;
	long    m_hasAddress    :1;
	long    m_hasTOD        :1;
	long    m_hasSiteVenue  :1;

	// expires after a certain time or if ownership changed
	long    m_inGoogleValid           :1;
	long    m_hasContactInfoValid     :1;
	long    m_hasAuthorityInlinkValid :1;
	long    m_isContactyValid         :1;
	long    m_hasAddressValid         :1;
	long    m_hasTODValid             :1;
	long    m_hasSiteVenueValid       :1;
	long    m_siteNumInlinksValid     :1;
	// was the request an injection request
	long    m_fromInjectionRequest    :1; 
	long    m_reserved1 :1;
	long    m_reserved2 :1;
	long    m_reserved3 :1;
	long    m_reserved4 :1;

	// how much buf will we need to serialize ourselves?
	long getRecSize () { return m_dataSize + 4 + sizeof(key128_t); }

	// clear all
	void reset() { memset ( this , 0 , sizeof(SpiderReply) ); };

	void setKey ( long firstIp,
		      long long parentDocId , 
		      long long uh48 , 
		      bool isDel ) ;

	long print ( class SafeBuf *sbarg );

	long long  getUrlHash48  () {return g_spiderdb.getUrlHash48(&m_key); };
	long long getParentDocId (){return g_spiderdb.getParentDocId(&m_key);};
};

// are we responsible for this ip?
bool isAssignedToUs ( long firstIp ) ;

// . store urls that can be spidered right NOW in doledb
// . SpiderLoop.cpp doles out urls from its local spiderdb into 
//   the doledb rdb of remote hosts (and itself as well sometimes!)
// . then each host calls SpiderLoop::spiderDoledUrls() to spider the
//   urls doled to their group in doledb
class Doledb {

  public:

	void reset();
	
	bool init ( );

	bool addColl ( char *coll, bool doVerify = true );

	DiskPageCache *getDiskPageCache() { return &m_pc; };

	// . see "overview of spidercache" below for key definition
	// . these keys when hashed are clogging up the hash table
	//   so i am making the 7 reserved bits part of the urlhash48...
	key_t makeKey ( long      priority   ,
			time_t    spiderTime ,
			long long urlHash48  ,
			bool      isDelete   ) {
		// sanity checks
		if ( priority  & 0xffffff00           ) { char *xx=NULL;*xx=0;}
		if ( urlHash48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0;}
		key_t k;
		k.n1 = (255 - priority);
		k.n1 <<= 24;
		k.n1 |= (spiderTime >>8);
		k.n0 = spiderTime & 0xff;
		k.n0 <<= 48;
		k.n0 |= urlHash48;
		// 7 bits reserved
		k.n0 <<= 7;
		// still reserved but when adding to m_doleReqTable it needs
		// to be more random!! otherwise the hash table is way slow!
		k.n0 |= (urlHash48 & 0x7f);
		// 1 bit for negative bit
		k.n0 <<= 1;
		// we are positive or not? setting this means we are positive
		if ( ! isDelete ) k.n0 |= 0x01;
		return k;
	};

	// . use this for a query reindex
	// . a docid-based spider request
	// . crap, might we have collisions between a uh48 and docid????
	key_t makeReindexKey ( long priority ,
			       time_t spiderTime ,
			       long long docId ,
			       bool isDelete ) {
		return makeKey ( priority,spiderTime,docId,isDelete); };


	key_t makeFirstKey2 ( long priority ) { 
		key_t k; 
		k.setMin(); 
		// set priority
		k.n1 = (255 - priority);
		k.n1 <<= 24;
		return k;
	};


	key_t makeLastKey2 ( long priority ) { 
		key_t k; 
		k.setMax(); 
		// set priority
		k.n1 = (255 - priority);
		k.n1 <<= 24;
		k.n1 |= 0x00ffffff;
		return k;
	};

	long getPriority  ( key_t *k ) {
		return 255 - ((k->n1 >> 24) & 0xff); };
	long getSpiderTime ( key_t *k ) {
		unsigned long spiderTime = (k->n1) & 0xffffff;
		spiderTime <<= 8;
		// upper 8 bits of k.n0 are lower 8 bits of spiderTime
		spiderTime |= (unsigned long)((k->n0) >> (64-8));
		return (long)spiderTime;
	};
	long getIsDel     ( key_t *k ) {
		if ( (k->n0 & 0x01) ) return 0;
		return 1; };
	long long getUrlHash48 ( key_t *k ) {
		return (k->n0>>8)&0x0000ffffffffffffLL; }

	key_t makeFirstKey ( ) { key_t k; k.setMin(); return k;};
	key_t makeLastKey  ( ) { key_t k; k.setMax(); return k;};

	Rdb *getRdb() { return &m_rdb;};

	Rdb m_rdb;

	DiskPageCache m_pc;
};



extern class Doledb g_doledb;

// was 1000 but breached, now equals SR_READ_SIZE/sizeof(SpiderReply)
#define MAX_BEST_REQUEST_SIZE (MAX_URL_LEN+1+sizeof(SpiderRequest))
#define MAX_DOLEREC_SIZE      (MAX_BEST_REQUEST_SIZE+sizeof(key_t)+4)
#define MAX_SP_REPLY_SIZE     (sizeof(SpiderReply))

// we have one SpiderColl for each collection record
class SpiderColl {
 public:

	~SpiderColl ( );
	SpiderColl  ( ) ;

	// called by main.cpp on exit to free memory
	void      reset();

	bool      load();

	key128_t m_firstKey;
	// spiderdb is now 128bit keys
	key128_t m_nextKey;
	key128_t m_endKey;

	bool m_useTree;


	bool           m_bestRequestValid;
	char           m_bestRequestBuf[MAX_BEST_REQUEST_SIZE];
	SpiderRequest *m_bestRequest;
	uint64_t       m_bestSpiderTimeMS;

	bool           m_lastReplyValid;
	char           m_lastReplyBuf[MAX_SP_REPLY_SIZE];

	// doledbkey + dataSize + bestRequestRec
	char m_doleBuf[MAX_DOLEREC_SIZE];

	// for scanning the wait tree...
	bool m_isPopulating;
	// for reading from spiderdb
	bool m_isReadDone;

	Msg4 m_msg4;
	bool m_msg4Avail;

	// Rdb.cpp calls this
	bool  addSpiderReply   ( SpiderReply   *srep );
	bool  addSpiderRequest ( SpiderRequest *sreq , long long nowGlobalMS );

	bool  addToDoleTable   ( SpiderRequest *sreq ) ;


	bool updateSiteNumInlinksTable ( long siteHash32,long sni,long tstamp);

	uint64_t getSpiderTimeMS ( SpiderRequest *sreq,
				   long ufn,
				   SpiderReply *srep,
				   uint64_t nowGlobalMS);

	// maps priority to first ufn that uses that
	// priority. map to -1 if no ufn uses it. that way when we scan
	// priorities for spiderrequests to dole out we can start with
	// priority 63 and see what the max spiders or same ip wait are
	// because we need the ufn to get the maxSpiders from the url filters
	// table.
	long m_priorityToUfn[MAX_SPIDER_PRIORITIES];
	// init this to false, and also set to false on reset, then when
	// it is false we re-stock m_ufns. re-stock if user changes the
	// url filters table...
	bool m_ufnMapValid;

	// list for loading spiderdb recs during the spiderdb scan
	RdbList        m_list;

	// spiderdb scan for populating waiting tree
	RdbList  m_list2;
	Msg5     m_msg5b;
	bool     m_gettingList2;
	key128_t m_nextKey2;
	key128_t m_endKey2;
	time_t   m_lastScanTime;
	bool     m_waitingTreeNeedsRebuild;
	long     m_numAdded;
	long long m_numBytesScanned;
	long long m_lastPrintCount;

	// used by SpiderLoop.cpp
	long m_spidersOut;

	// . hash of collection name this arena represents
	// . 0 for main collection
	collnum_t m_collnum;
	char  m_coll [ MAX_COLL_LEN + 1 ] ;
	class CollectionRec *m_cr;
	bool     m_isTestColl;

	HashTableX m_doleIpTable;

	// freshest m_siteNumInlinks per site stored in here
	HashTableX m_sniTable;

	// maps a domainHash32 to a crawl delay in milliseconds
	HashTableX m_cdTable;

	RdbCache m_lastDownloadCache;

	bool makeDoleIPTable     ( );
	bool makeWaitingTable    ( );
	bool makeWaitingTree     ( );

	long long getEarliestSpiderTimeFromWaitingTree ( long firstIp ) ;

	bool addToWaitingTree    ( uint64_t spiderTime , long firstIp ,
				   bool callForScan );
	long getNextIpFromWaitingTree ( );
	void populateDoledbFromWaitingTree ( bool reentry );
	bool scanSpiderdb        ( bool needList );

	void populateWaitingTreeFromSpiderdb ( bool reentry ) ;

	HashTableX m_waitingTable;
	RdbTree    m_waitingTree;
	RdbMem     m_waitingMem; // used by m_waitingTree
	key_t      m_waitingTreeKey;

	key_t m_nextDoledbKey;
	bool  m_didRound;
	long  m_pri;
	bool  m_twinDied;
	long  m_lastUrlFiltersUpdate;

	// for reading lists from spiderdb
	Msg5 m_msg5;
	bool m_gettingList;

	// how many outstanding spiders a priority has
	long m_outstandingSpiders[MAX_SPIDER_PRIORITIES];

	bool printStats ( SafeBuf &sb ) ;
};

class SpiderCache {

 public:

	// returns false and set g_errno on error
	bool init ( ) ;

	SpiderCache ( ) ;

	// what SpiderColl does a SpiderRec with this key belong?
	SpiderColl *getSpiderColl ( collnum_t collNum ) ;

	// called by main.cpp on exit to free memory
	void reset();

	void save ( bool useThread );

	bool needsSave ( ) ;
	void doneSaving ( ) ;

	bool m_isSaving;

	// . we allocate one SpiderColl per collection
	// . each one stores the collNum of the collection name it represents,
	//   and has a ptr to it, m_cr, that is updated by sync()
	//   when the Collectiondb is updated
	SpiderColl *m_spiderColls [ MAX_COLL_RECS ];
	long        m_numSpiderColls;
};

extern class SpiderCache g_spiderCache;



class UrlLock {
public:
	long m_hostId;
	long m_lockSequence;
	long m_timestamp;
	long m_expires;
};

class Msg12 {
 public:
	// stuff for getting the msg12 lock for spidering a url
	bool getLocks       ( long long probDocId,
			      char *url ,
			      void *state,
			      void (* callback)(void *state) );
	bool gotLockReply   ( class UdpSlot *slot );
	bool removeAllLocks ( );

	// these two things comprise the lock request buffer
	unsigned long long  m_lockKey;
	long                m_lockSequence;

	long       m_numReplies;
	long       m_numRequests;
	long       m_grants;
	bool       m_removing;
	char      *m_url; // for debugging
	void      *m_state;
	void      (*m_callback)(void *state);
	bool       m_gettingLocks;
	bool       m_hasLock;
};

void handleRequest12 ( UdpSlot *udpSlot , long niceness ) ;


// . the spider loop
// . it gets urls to spider from the SpiderCache global class, g_spiderCache
// . supports robots.txt
// . supports <META NAME="ROBOTS" CONTENT="NOINDEX">  (no indexing)
// . supports <META NAME="ROBOTS" CONTENT="NOFOLLOW"> (no links)
// . supports limiting spiders per domain

// max spiders we can have going at once for this process
#define MAX_SPIDERS 500

class SpiderLoop {

 public:

	~SpiderLoop();
	SpiderLoop();

	// free all XmlDocs and m_list
	void reset();

	// . call this no matter what
	// . if spidering is disabled this will sleep about 10 seconds or so
	//   before checking to see if it's been enabled
	void startLoop();

	void doLoop();

	void doleUrls1();
	void doleUrls2();

	void spiderDoledUrls ( ) ;
	bool gotDoledbList2  ( ) ;

	// . returns false if blocked and "callback" will be called, 
	//   true otherwise
	// . returns true and sets g_errno on error
	bool spiderUrl9 ( class SpiderRequest *sreq ,
			 key_t *doledbKey       ,
			 char  *coll            );

	bool spiderUrl2 ( );

	Msg12 m_msg12;

	// state memory for calling SpiderUrl2() (maybe also getLocks()!)
	SpiderRequest *m_sreq;

	char      *m_coll;
	char      *m_content;
	long       m_contentLen;
	char       m_contentHasMime;
	key_t     *m_doledbKey;
	void      *m_state;
	void     (*m_callback)(void *state);

	// . the one that was just indexed
	// . Msg7.cpp uses this to see what docid the injected doc got so it
	//   can forward it to external program
	long long getLastDocId ( );

	// delete m_msg14[i], decrement m_numSpiders, m_maxUsed
	void cleanUp ( long i );

	// registers sleep callback iff not already registered
	void doSleep ( ) ;

	bool indexedDoc ( class XmlDoc *doc );

	// are we registered for sleep callbacks
	bool m_isRegistered;

	long m_numSpidersOut;

	// for spidering/parsing/indexing a url(s)
	class XmlDoc *m_docs [ MAX_SPIDERS ];

	// . this is "i" where m_msg14[i] is the highest m_msg14 in use
	// . we use it to limit our scanning to the first "i" m_msg14's
	long m_maxUsed;

	// . list for getting next url(s) to spider
	RdbList m_list;

	// for getting RdbLists
	Msg5 m_msg5;

	class SpiderColl *m_sc;

	// used to avoid calling getRec() twice!
	bool m_gettingList;

	long m_outstanding1;
	bool m_gettingDoledbList;
	HashTableX m_lockTable;
	// save on msg12 lookups! keep somewhat local...
	RdbCache   m_lockCache;

	//bool m_gettingLocks;

	// for round robining in SpiderLoop::doleUrls(), etc.
	long m_cri;

	long m_processed;
};

extern class SpiderLoop g_spiderLoop;

void clearUfnTable ( ) ;

long getUrlFilterNum ( class SpiderRequest *sreq , 
		       class SpiderReply   *srep , 
		       long nowGlobal , 
		       bool isForMsg20 ,
		       long niceness , 
		       class CollectionRec *cr ,
		       bool isOutlink = false ) ;

#endif
