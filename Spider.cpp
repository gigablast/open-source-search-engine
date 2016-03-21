// . TODO: do not cache if less than the 20k thing again.

// . TODO: nuke doledb every couple hours.
//   CollectionRec::m_doledbRefreshRateInSecs. but how would this work
//   for crawlbot jobs where we got 10,000 collections? i'd turn this off.
//   we could selectively update certain firstips in doledb that have
//   been in doledb for a long time.
//   i'd like to see how many collections are actually active
//   for diffbot first though.



// TODO: add m_downloadTimeTable to measure download speed of an IP
// TODO: consider a "latestpubdateage" in url filters for pages that are
//       adding new dates (not clocks) all the time

#include "gb-include.h"
#include "Spider.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "XmlDoc.h"    // score8to32()
#include "Stats.h"
#include "SafeBuf.h"
#include "Repair.h"
#include "CountryCode.h"
#include "DailyMerge.h"
#include "Process.h"
#include "Test.h" // g_test
#include "Threads.h"
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "Rebalance.h"

void testWinnerTreeKey ( ) ;

// . this was 10 but cpu is getting pegged, so i set to 45
// . we consider the collection done spidering when no urls to spider
//   for this many seconds
// . i'd like to set back to 10 for speed... maybe even 5 or less
// . back to 30 from 20 to try to fix crawls thinking they are done
//   maybe because of the empty doledb logic taking too long?
//#define SPIDER_DONE_TIMER 30
// try 45 to prevent false revivals
//#define SPIDER_DONE_TIMER 45
// try 30 again since we have new localcrawlinfo update logic much faster
//#define SPIDER_DONE_TIMER 30
// neo under heavy load go to 60
//#define SPIDER_DONE_TIMER 60
// super overloaded
//#define SPIDER_DONE_TIMER 90
#define SPIDER_DONE_TIMER 20

// seems like timecity.com as gigabytes of spiderdb data so up from 40 to 400
//#define MAX_WINNER_NODES 400

// up it to 2000 because shard #15 has slow disk reads and some collections
// are taking forever to spider because the spiderdb scan is so slow.
// we reduce this below if the spiderdb is smaller.
#define MAX_WINNER_NODES 2000

Doledb g_doledb;

RdbTree *g_tree = NULL;

SpiderRequest *g_sreq = NULL;

int32_t g_corruptCount = 0;

static char s_countsAreValid = 1;

/////////////////////////
/////////////////////////      SPIDEREC
/////////////////////////

void SpiderRequest::setKey (int32_t firstIp,
			    int64_t parentDocId,
			    int64_t uh48,
			    bool isDel) {

	// sanity
	if ( firstIp == 0 || firstIp == -1 ) { char *xx=NULL;*xx=0; }

	m_key = g_spiderdb.makeKey ( firstIp,uh48,true,parentDocId , isDel );
	// set dataSize too!
	setDataSize();
}

void SpiderRequest::setDataSize ( ) {
	m_dataSize = (m_url - (char *)this) + gbstrlen(m_url) + 1 
		// subtract m_key and m_dataSize
		- sizeof(key128_t) - 4 ;
}

int32_t SpiderRequest::print ( SafeBuf *sbarg ) {

	SafeBuf *sb = sbarg;
	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	//sb->safePrintf("k.n1=0x%"XINT64" ",m_key.n1);
	//sb->safePrintf("k.n0=0x%"XINT64" ",m_key.n0);
	sb->safePrintf("k=%s ",KEYSTR(this,
				      getKeySizeFromRdbId(RDB_SPIDERDB)));

	// indicate it's a request not a reply
	sb->safePrintf("REQ ");
	sb->safePrintf("uh48=%"UINT64" ",getUrlHash48());
	// if negtaive bail early now
	if ( (m_key.n0 & 0x01) == 0x00 ) {
		sb->safePrintf("[DELETE]");
		if ( ! sbarg ) printf("%s",sb->getBufStart() );
		return sb->length();
	}

	sb->safePrintf("recsize=%"INT32" ",getRecSize());
	sb->safePrintf("parentDocId=%"UINT64" ",getParentDocId());

	sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );
	sb->safePrintf("hostHash32=0x%"XINT32" ",m_hostHash32 );
	sb->safePrintf("domHash32=0x%"XINT32" ",m_domHash32 );
	sb->safePrintf("siteHash32=0x%"XINT32" ",m_siteHash32 );
	sb->safePrintf("siteNumInlinks=%"INT32" ",m_siteNumInlinks );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf("addedTime=%s(%"UINT32") ",time,(uint32_t)m_addedTime );

	//sb->safePrintf("parentFirstIp=%s ",iptoa(m_parentFirstIp) );
	sb->safePrintf("pageNumInlinks=%i ",(int)m_pageNumInlinks);
	sb->safePrintf("parentHostHash32=0x%"XINT32" ",m_parentHostHash32 );
	sb->safePrintf("parentDomHash32=0x%"XINT32" ",m_parentDomHash32 );
	sb->safePrintf("parentSiteHash32=0x%"XINT32" ",m_parentSiteHash32 );

	sb->safePrintf("hopCount=%"INT32" ",(int32_t)m_hopCount );

	//timeStruct = gmtime ( &m_spiderTime );
	//time[0] = 0;
	//if ( m_spiderTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("spiderTime=%s(%"UINT32") ",time,m_spiderTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("pubDate=%s(%"UINT32") ",time,m_pubDate );

	sb->safePrintf("ufn=%"INT32" ", (int32_t)m_ufn);
	// why was this unsigned?
	sb->safePrintf("priority=%"INT32" ", (int32_t)m_priority);

	//sb->safePrintf("errCode=%s(%"UINT32") ",mstrerror(m_errCode),m_errCode );
	//sb->safePrintf("crawlDelay=%"INT32"ms ",m_crawlDelay );
	//sb->safePrintf("httpStatus=%"INT32" ",(int32_t)m_httpStatus );
	//sb->safePrintf("retryNum=%"INT32" ",(int32_t)m_retryNum );
	//sb->safePrintf("langId=%s(%"INT32") ",
	//	       getLanguageString(m_langId),(int32_t)m_langId );
	//sb->safePrintf("percentChanged=%"INT32"%% ",(int32_t)m_percentChanged );

	if ( m_isNewOutlink ) sb->safePrintf("ISNEWOUTLINK ");
	if ( m_isAddUrl ) sb->safePrintf("ISADDURL ");
	if ( m_isPageReindex ) sb->safePrintf("ISPAGEREINDEX ");
	if ( m_isPageParser ) sb->safePrintf("ISPAGEPARSER ");
	if ( m_urlIsDocId ) sb->safePrintf("URLISDOCID ");
	if ( m_isRSSExt ) sb->safePrintf("ISRSSEXT ");
	if ( m_isUrlPermalinkFormat ) sb->safePrintf("ISURLPERMALINKFORMAT ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	if ( m_fakeFirstIp ) sb->safePrintf("ISFAKEFIRSTIP ");
	if ( m_isInjecting ) sb->safePrintf("ISINJECTING ");
	if ( m_forceDelete ) sb->safePrintf("FORCEDELETE ");
	if ( m_sameDom ) sb->safePrintf("SAMEDOM ");
	if ( m_sameHost ) sb->safePrintf("SAMEHOST ");
	if ( m_sameSite ) sb->safePrintf("SAMESITE ");
	if ( m_wasParentIndexed ) sb->safePrintf("WASPARENTINDEXED ");
	if ( m_parentIsRSS ) sb->safePrintf("PARENTISRSS ");
	if ( m_parentIsPermalink ) sb->safePrintf("PARENTISPERMALINK ");
	if ( m_parentIsPingServer ) sb->safePrintf("PARENTISPINGSERVER ");
	if ( m_parentIsSiteMap ) sb->safePrintf("PARENTISSITEMAP ");
	if ( m_isMenuOutlink ) sb->safePrintf("MENUOUTLINK ");

	if ( m_parentHasAddress ) sb->safePrintf("PARENTHASADDRESS ");
	//if ( m_fromSections ) sb->safePrintf("FROMSECTIONS ");
	if ( m_isScraping ) sb->safePrintf("ISSCRAPING ");
	if ( m_hasContent ) sb->safePrintf("HASCONTENT ");
	if ( m_inGoogle ) sb->safePrintf("INGOOGLE ");
	if ( m_hasAuthorityInlink ) sb->safePrintf("HASAUTHORITYINLINK ");
	if ( m_hasContactInfo ) sb->safePrintf("HASCONTACTINFO ");

	//if ( m_hasSiteVenue  ) sb->safePrintf("HASSITEVENUE ");
	if ( m_isContacty      ) sb->safePrintf("CONTACTY ");
	if ( m_isWWWSubdomain  ) sb->safePrintf("WWWSUBDOMAIN ");
	if ( m_avoidSpiderLinks ) sb->safePrintf("AVOIDSPIDERLINKS ");

	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");

	//uint32_t gid = g_spiderdb.getGroupId(m_firstIp);
	int32_t shardNum = g_hostdb.getShardNum(RDB_SPIDERDB,this);
	sb->safePrintf("shardnum=%"UINT32" ",(uint32_t)shardNum);

	sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) 
		printf("%s",sb->getBufStart() );

	return sb->length();
}

void SpiderReply::setKey (int32_t firstIp,
			  int64_t parentDocId,
			  int64_t uh48,
			  bool isDel) {
	m_key = g_spiderdb.makeKey ( firstIp,uh48,false,parentDocId , isDel );
	// set dataSize too!
	m_dataSize = sizeof(SpiderReply) - sizeof(key128_t) - 4;
}

int32_t SpiderReply::print ( SafeBuf *sbarg ) {

	SafeBuf *sb = sbarg;
	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	//sb->safePrintf("k.n1=0x%llx ",m_key.n1);
	//sb->safePrintf("k.n0=0x%llx ",m_key.n0);
	sb->safePrintf("k=%s ",KEYSTR(this,sizeof(SPIDERDBKEY)));

	// indicate it's a reply
	sb->safePrintf("REP ");

	sb->safePrintf("uh48=%"UINT64" ",getUrlHash48());
	sb->safePrintf("parentDocId=%"UINT64" ",getParentDocId());


	// if negtaive bail early now
	if ( (m_key.n0 & 0x01) == 0x00 ) {
		sb->safePrintf("[DELETE]");
		if ( ! sbarg ) printf("%s",sb->getBufStart() );
		return sb->length();
	}

	sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );
	sb->safePrintf("percentChangedPerDay=%.02f%% ",m_percentChangedPerDay);

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];
	time_t ts = (time_t)m_spideredTime;
	timeStruct = gmtime ( &ts );
	time[0] = 0;
	if ( m_spideredTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("spideredTime=%s(%"UINT32") ",time,
		       (uint32_t)m_spideredTime);

	sb->safePrintf("siteNumInlinks=%"INT32" ",m_siteNumInlinks );

	time_t ts2 = (time_t)m_pubDate;
	timeStruct = gmtime ( &ts2 );
	time[0] = 0;
	if ( m_pubDate != 0 && m_pubDate != -1 ) 
		strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("pubDate=%s(%"INT32") ",time,m_pubDate );

	//sb->safePrintf("newRequests=%"INT32" ",m_newRequests );
	sb->safePrintf("ch32=%"UINT32" ",(uint32_t)m_contentHash32);

	sb->safePrintf("crawldelayms=%"INT32"ms ",m_crawlDelayMS );
	sb->safePrintf("httpStatus=%"INT32" ",(int32_t)m_httpStatus );
	sb->safePrintf("langId=%s(%"INT32") ",
		       getLanguageString(m_langId),(int32_t)m_langId );

	if ( m_errCount )
		sb->safePrintf("errCount=%"INT32" ",(int32_t)m_errCount);

	sb->safePrintf("errCode=%s(%"UINT32") ",mstrerror(m_errCode),
		       (uint32_t)m_errCode );

	//if ( m_isSpam ) sb->safePrintf("ISSPAM ");
	if ( m_isRSS ) sb->safePrintf("ISRSS ");
	if ( m_isPermalink ) sb->safePrintf("ISPERMALINK ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	//if ( m_deleted ) sb->safePrintf("DELETED ");
	if ( m_isIndexed && ! m_isIndexedINValid) sb->safePrintf("ISINDEXED ");

	if ( m_hasAddress    ) sb->safePrintf("HASADDRESS ");
	if ( m_hasTOD        ) sb->safePrintf("HASTOD ");
	//if ( m_hasSiteVenue  ) sb->safePrintf("HASSITEVENUE ");
	if ( m_isContacty    ) sb->safePrintf("CONTACTY ");

	//sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) 
		printf("%s",sb->getBufStart() );

	return sb->length();
}



int32_t SpiderRequest::printToTable ( SafeBuf *sb , char *status ,
				   XmlDoc *xd , int32_t row ) {

	sb->safePrintf("<tr bgcolor=#%s>\n",LIGHT_BLUE);

	// show elapsed time
	if ( xd ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t elapsed = now - xd->m_startTime;
		sb->safePrintf(" <td>%"INT32"</td>\n",row);
		sb->safePrintf(" <td>%"INT64"ms</td>\n",elapsed);
		collnum_t collnum = xd->m_collnum;
		CollectionRec *cr = g_collectiondb.getRec(collnum);
		char *cs = ""; if ( cr ) cs = cr->m_coll;
		// sb->safePrintf(" <td><a href=/crawlbot?c=%s>%"INT32"</a></td>\n",
		// 	       cs,(int32_t)collnum);
		//sb->safePrintf(" <td><a href=/crawlbot?c=%s>%s</a></td>\n",
		//	       cs,cs);
		sb->safePrintf(" <td><a href=/search?c=%s&q=url%%3A%s>%s</a>"
			       "</td>\n",cs,m_url,cs);
	}

	sb->safePrintf(" <td><a href=%s><nobr>",m_url);
	sb->safeTruncateEllipsis ( m_url , 64 );
	sb->safePrintf("</nobr></a></td>\n");
	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",status );

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_priority);
	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_ufn);

	sb->safePrintf(" <td>%s</td>\n",iptoa(m_firstIp) );
	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_errCount );

	sb->safePrintf(" <td>%"UINT64"</td>\n",getUrlHash48());

	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_hostHash32 );
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_domHash32 );
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_siteHash32 );

	sb->safePrintf(" <td>%"INT32"</td>\n",m_siteNumInlinks );
	//sb->safePrintf(" <td>%"INT32"</td>\n",m_pageNumInlinks );
	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_hopCount );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts3 = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts3 );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf(" <td><nobr>%s(%"UINT32")</nobr></td>\n",time,
		       (uint32_t)m_addedTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf(" <td>%s(%"UINT32")</td>\n",time,m_pubDate );

	//sb->safePrintf(" <td>%s(%"UINT32")</td>\n",mstrerror(m_errCode),m_errCode);
	//sb->safePrintf(" <td>%"INT32"ms</td>\n",m_crawlDelay );
	sb->safePrintf(" <td>%i</td>\n",(int)m_pageNumInlinks);
	sb->safePrintf(" <td>%"UINT64"</td>\n",getParentDocId() );

	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_parentHostHash32);
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_parentDomHash32 );
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_parentSiteHash32 );

	//sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_httpStatus );
	//sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_retryNum );
	//sb->safePrintf(" <td>%s(%"INT32")</td>\n",
	//	       getLanguageString(m_langId),(int32_t)m_langId );
	//sb->safePrintf(" <td>%"INT32"%%</td>\n",(int32_t)m_percentChanged );

	sb->safePrintf(" <td><nobr>");

	if ( m_isNewOutlink ) sb->safePrintf("ISNEWOUTLINK ");
	if ( m_isAddUrl ) sb->safePrintf("ISADDURL ");
	if ( m_isPageReindex ) sb->safePrintf("ISPAGEREINDEX ");
	if ( m_isPageParser ) sb->safePrintf("ISPAGEPARSER ");
	if ( m_urlIsDocId ) sb->safePrintf("URLISDOCID ");
	if ( m_isRSSExt ) sb->safePrintf("ISRSSEXT ");
	if ( m_isUrlPermalinkFormat ) sb->safePrintf("ISURLPERMALINKFORMAT ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	if ( m_isInjecting ) sb->safePrintf("ISINJECTING ");
	if ( m_forceDelete ) sb->safePrintf("FORCEDELETE ");
	if ( m_sameDom ) sb->safePrintf("SAMEDOM ");
	if ( m_sameHost ) sb->safePrintf("SAMEHOST ");
	if ( m_sameSite ) sb->safePrintf("SAMESITE ");
	if ( m_wasParentIndexed ) sb->safePrintf("WASPARENTINDEXED ");
	if ( m_parentIsRSS ) sb->safePrintf("PARENTISRSS ");
	if ( m_parentIsPermalink ) sb->safePrintf("PARENTISPERMALINK ");
	if ( m_parentIsPingServer ) sb->safePrintf("PARENTISPINGSERVER ");
	if ( m_parentIsSiteMap ) sb->safePrintf("PARENTISSITEMAP ");
	if ( m_isMenuOutlink ) sb->safePrintf("MENUOUTLINK ");

	if ( m_parentHasAddress ) sb->safePrintf("PARENTHASADDRESS ");
	//if ( m_fromSections ) sb->safePrintf("FROMSECTIONS ");
	if ( m_isScraping ) sb->safePrintf("ISSCRAPING ");
	if ( m_hasContent ) sb->safePrintf("HASCONTENT ");
	if ( m_inGoogle ) sb->safePrintf("INGOOGLE ");
	if ( m_hasAuthorityInlink ) sb->safePrintf("HASAUTHORITYINLINK ");
	if ( m_hasContactInfo ) sb->safePrintf("HASCONTACTINFO ");

	//if ( m_hasSiteVenue  ) sb->safePrintf("HASSITEVENUE ");
	if ( m_isContacty      ) sb->safePrintf("CONTACTY ");

	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");



	sb->safePrintf("</nobr></td>\n");

	sb->safePrintf("</tr>\n");

	return sb->length();
}


int32_t SpiderRequest::printTableHeaderSimple ( SafeBuf *sb , 
					     bool currentlySpidering) {

	sb->safePrintf("<tr bgcolor=#%s>\n",DARK_BLUE);

	// how long its been being spidered
	if ( currentlySpidering ) {
		sb->safePrintf(" <td><b>#</b></td>\n");
		sb->safePrintf(" <td><b>elapsed</b></td>\n");
		sb->safePrintf(" <td><b>coll</b></td>\n");
	}

	sb->safePrintf(" <td><b>url</b></td>\n");
	sb->safePrintf(" <td><b>status</b></td>\n");
	sb->safePrintf(" <td><b>first IP</b></td>\n");
	sb->safePrintf(" <td><b>crawlDelay</b></td>\n");
	sb->safePrintf(" <td><b>pri</b></td>\n");
	sb->safePrintf(" <td><b>errCount</b></td>\n");
	sb->safePrintf(" <td><b>hops</b></td>\n");
	sb->safePrintf(" <td><b>addedTime</b></td>\n");
	//sb->safePrintf(" <td><b>flags</b></td>\n");
	sb->safePrintf("</tr>\n");

	return sb->length();
}

int32_t SpiderRequest::printToTableSimple ( SafeBuf *sb , char *status ,
					 XmlDoc *xd , int32_t row ) {

	sb->safePrintf("<tr bgcolor=#%s>\n",LIGHT_BLUE);

	// show elapsed time
	if ( xd ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t elapsed = now - xd->m_startTime;
		sb->safePrintf(" <td>%"INT32"</td>\n",row);
		sb->safePrintf(" <td>%"INT64"ms</td>\n",elapsed);
		// print collection
		CollectionRec *cr = g_collectiondb.getRec ( xd->m_collnum );
		char *coll = "";
		if ( cr ) coll = cr->m_coll;
		sb->safePrintf("<td>%s</td>",coll);
	}

	sb->safePrintf(" <td><nobr>");
	sb->safeTruncateEllipsis ( m_url , 64 );
	sb->safePrintf("</nobr></td>\n");
	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",status );

	sb->safePrintf(" <td>%s</td>\n",iptoa(m_firstIp));

	if ( xd->m_crawlDelayValid && xd->m_crawlDelay >= 0 )
		sb->safePrintf(" <td>%"INT32" ms</td>\n",xd->m_crawlDelay);
	else
		sb->safePrintf(" <td>--</td>\n");

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_priority);

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_errCount );

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_hopCount );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts4 = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts4 );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf(" <td><nobr>%s(%"UINT32")</nobr></td>\n",time,
		       (uint32_t)m_addedTime);

	/*
	sb->safePrintf(" <td><nobr>");

	if ( m_isNewOutlink ) sb->safePrintf("ISNEWOUTLINK ");
	if ( m_isAddUrl ) sb->safePrintf("ISADDURL ");
	if ( m_isPageReindex ) sb->safePrintf("ISPAGEREINDEX ");
	if ( m_isPageParser ) sb->safePrintf("ISPAGEPARSER ");
	if ( m_urlIsDocId ) sb->safePrintf("URLISDOCID ");
	if ( m_isRSSExt ) sb->safePrintf("ISRSSEXT ");
	if ( m_isUrlPermalinkFormat ) sb->safePrintf("ISURLPERMALINKFORMAT ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	if ( m_isInjecting ) sb->safePrintf("ISINJECTING ");
	if ( m_forceDelete ) sb->safePrintf("FORCEDELETE ");
	if ( m_sameDom ) sb->safePrintf("SAMEDOM ");
	if ( m_sameHost ) sb->safePrintf("SAMEHOST ");
	if ( m_sameSite ) sb->safePrintf("SAMESITE ");
	if ( m_wasParentIndexed ) sb->safePrintf("WASPARENTINDEXED ");
	if ( m_parentIsRSS ) sb->safePrintf("PARENTISRSS ");
	if ( m_parentIsPermalink ) sb->safePrintf("PARENTISPERMALINK ");
	if ( m_parentIsPingServer ) sb->safePrintf("PARENTISPINGSERVER ");
	if ( m_isMenuOutlink ) sb->safePrintf("MENUOUTLINK ");

	if ( m_parentHasAddress ) sb->safePrintf("PARENTHASADDRESS ");
	//if ( m_fromSections ) sb->safePrintf("FROMSECTIONS ");
	if ( m_isScraping ) sb->safePrintf("ISSCRAPING ");
	if ( m_hasContent ) sb->safePrintf("HASCONTENT ");
	if ( m_inGoogle ) sb->safePrintf("INGOOGLE ");
	if ( m_hasAuthorityInlink ) sb->safePrintf("HASAUTHORITYINLINK ");
	if ( m_hasContactInfo ) sb->safePrintf("HASCONTACTINFO ");

	if ( m_hasSiteVenue  ) sb->safePrintf("HASSITEVENUE ");
	if ( m_isContacty      ) sb->safePrintf("CONTACTY ");

	sb->safePrintf("</nobr></td>\n");
	*/

	sb->safePrintf("</tr>\n");

	return sb->length();
}


int32_t SpiderRequest::printTableHeader ( SafeBuf *sb , bool currentlySpidering) {

	sb->safePrintf("<tr bgcolor=#%s>\n",DARK_BLUE);

	// how long its been being spidered
	if ( currentlySpidering ) {
		sb->safePrintf(" <td><b>#</b></td>\n");
		sb->safePrintf(" <td><b>elapsed</b></td>\n");
		sb->safePrintf(" <td><b>coll</b></td>\n");
	}

	sb->safePrintf(" <td><b>url</b></td>\n");
	sb->safePrintf(" <td><b>status</b></td>\n");

	sb->safePrintf(" <td><b>pri</b></td>\n");
	sb->safePrintf(" <td><b>ufn</b></td>\n");

	sb->safePrintf(" <td><b>firstIp</b></td>\n");
	sb->safePrintf(" <td><b>errCount</b></td>\n");
	sb->safePrintf(" <td><b>urlHash48</b></td>\n");
	//sb->safePrintf(" <td><b>hostHash32</b></td>\n");
	//sb->safePrintf(" <td><b>domHash32</b></td>\n");
	//sb->safePrintf(" <td><b>siteHash32</b></td>\n");
	sb->safePrintf(" <td><b>siteInlinks</b></td>\n");
	//sb->safePrintf(" <td><b>pageNumInlinks</b></td>\n");
	sb->safePrintf(" <td><b>hops</b></td>\n");
	sb->safePrintf(" <td><b>addedTime</b></td>\n");
	//sb->safePrintf(" <td><b>lastAttempt</b></td>\n");
	//sb->safePrintf(" <td><b>pubDate</b></td>\n");
	//sb->safePrintf(" <td><b>errCode</b></td>\n");
	//sb->safePrintf(" <td><b>crawlDelay</b></td>\n");
	sb->safePrintf(" <td><b>parentIp</b></td>\n");
	sb->safePrintf(" <td><b>parentDocId</b></td>\n");
	//sb->safePrintf(" <td><b>parentHostHash32</b></td>\n");
	//sb->safePrintf(" <td><b>parentDomHash32</b></td>\n");
	//sb->safePrintf(" <td><b>parentSiteHash32</b></td>\n");
	//sb->safePrintf(" <td><b>httpStatus</b></td>\n");
	//sb->safePrintf(" <td><b>retryNum</b></td>\n");
	//sb->safePrintf(" <td><b>langId</b></td>\n");
	//sb->safePrintf(" <td><b>percentChanged</b></td>\n");
	sb->safePrintf(" <td><b>flags</b></td>\n");
	sb->safePrintf("</tr>\n");

	return sb->length();
}


/////////////////////////
/////////////////////////      SPIDERDB
/////////////////////////


// a global class extern'd in .h file
Spiderdb g_spiderdb;
Spiderdb g_spiderdb2;

// reset rdb
void Spiderdb::reset() { m_rdb.reset(); }

// print the spider rec
int32_t Spiderdb::print( char *srec , SafeBuf *sb ) {
	// get if request or reply and print it
	if ( isSpiderRequest ( (key128_t *)srec ) )
		((SpiderRequest *)srec)->print(sb);
	else
		((SpiderReply *)srec)->print(sb);
	return 0;
}


bool Spiderdb::init ( ) {

	int32_t maxMem = 200000000;
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	int32_t maxTreeNodes  = maxMem  / 78;
	// . really we just cache the first 64k of each priority list
	// . used only by SpiderLoop
	//int32_t maxCacheNodes = 32;
	// we use the same disk page size as indexdb (for rdbmap.cpp)
	//int32_t pageSize = GB_INDEXDB_PAGE_SIZE;
	// disk page cache mem, 100MB on gk0 now
	//int32_t pcmem = 20000000;//g_conf.m_spiderdbMaxDiskPageCacheMem;
	// keep this low if we are the tmp cluster
	//if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// turn off to prevent blocking up cpu
	//pcmem = 0;
	// key parser checks
	//int32_t      ip         = 0x1234;
	char      priority   = 12;
	int32_t      spiderTime = 0x3fe96610;
	int64_t urlHash48  = 0x1234567887654321LL & 0x0000ffffffffffffLL;
	//int64_t pdocid     = 0x567834222LL;
	//key192_t k = makeOrderKey ( ip,priority,spiderTime,urlHash48,pdocid);
	//if (getOrderKeyUrlHash48  (&k)!=urlHash48 ){char*xx=NULL;*xx=0;}
	//if (getOrderKeySpiderTime (&k)!=spiderTime){char*xx=NULL;*xx=0;}
	//if (getOrderKeyPriority   (&k)!=priority  ){char*xx=NULL;*xx=0;}
	//if (getOrderKeyIp         (&k)!=ip        ){char*xx=NULL;*xx=0;}
	//if (getOrderKeyParentDocId(&k)!=pdocid    ){char*xx=NULL;*xx=0;}

	// doledb key test
	key_t dk = g_doledb.makeKey(priority,spiderTime,urlHash48,false);
	if(g_doledb.getPriority(&dk)!=priority){char*xx=NULL;*xx=0;}
	if(g_doledb.getSpiderTime(&dk)!=spiderTime){char*xx=NULL;*xx=0;}
	if(g_doledb.getUrlHash48(&dk)!=urlHash48){char*xx=NULL;*xx=0;}
	if(g_doledb.getIsDel(&dk)!= 0){char*xx=NULL;*xx=0;}

	// spiderdb key test
	int64_t docId = 123456789;
	int32_t firstIp = 0x23991688;
	key128_t sk = g_spiderdb.makeKey ( firstIp,urlHash48,1,docId,false);
	if ( ! g_spiderdb.isSpiderRequest (&sk) ) { char *xx=NULL;*xx=0; }
	if ( g_spiderdb.getUrlHash48(&sk) != urlHash48){char *xx=NULL;*xx=0;}
	if ( g_spiderdb.getFirstIp(&sk) != firstIp) {char *xx=NULL;*xx=0;}

	testWinnerTreeKey();

	// we now use a page cache
	// if ( ! m_pc.init ( "spiderdb", 
	// 		   RDB_SPIDERDB ,
	// 		   pcmem     ,
	// 		   pageSize  ))
	// 	return log(LOG_INIT,"spiderdb: Init failed.");

	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "spiderdb"   ,
			    true    , // dedup
			    -1      , // fixedDataSize
			    // now that we have MAX_WINNER_NODES allowed in doledb
			    // we don't have to keep spiderdb so tightly merged i guess..
			    // MDW: it seems to slow performance when not tightly merged
			    // so put this back to "2"...
			    2,//g_conf.m_spiderdbMinFilesToMerge , mintomerge
			    maxMem,//g_conf.m_spiderdbMaxTreeMem ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0,//g_conf.m_spiderdbMaxCacheMem,
			    0,//maxCacheNodes               ,
			    false                       , // half keys?
			    false                       , // save cache?
			    NULL,//&m_pc                       ,
			    false                       ,
			    false                       ,
			    sizeof(key128_t)            );
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Spiderdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	int32_t maxTreeNodes  = treeMem  / 78;
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "spiderdbRebuild"   ,
			    true          , // dedup
			    -1            , // fixedDataSize
			    200           , // g_conf.m_spiderdbMinFilesToMerge
			    treeMem       , // g_conf.m_spiderdbMaxTreeMem ,
			    maxTreeNodes  ,
			    true          , // balance tree?
			    0             , // m_spiderdbMaxCacheMem,
			    0             , // maxCacheNodes               ,
			    false         , // half keys?
			    false         , // save cache?
			    NULL          , // &m_pc 
			    false         , // isTitledb?
			    false         , // preload diskpagecache
			    sizeof(key128_t));
}

/*
bool Spiderdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}
*/

bool Spiderdb::verify ( char *coll ) {
	//return true;
	log ( LOG_DEBUG, "db: Verifying Spiderdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_SPIDERDB  ,
			      cr->m_collnum  ,
			      &list         ,
			      (char *)&startKey      ,
			      (char *)&endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		char *k = list.getCurrentRec();
		//key_t k = list.getCurrentKey();
		count++;
		// what group's spiderdb should hold this rec
		//uint32_t groupId = g_hostdb.getGroupId ( RDB_SPIDERDB , k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		int32_t shardNum = g_hostdb.getShardNum(RDB_SPIDERDB,k);
		if ( shardNum == g_hostdb.getMyShardNum() ) got++;
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %"INT32" records in spiderdb, "
		     "only %"INT32" belong to our shard.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Spiderdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log (LOG_DEBUG,"db: Spiderdb passed verification successfully for %"INT32" "
	      "recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

key128_t Spiderdb::makeKey ( int32_t      firstIp     ,
			     int64_t urlHash48   , 
			     bool      isRequest   ,
			     // MDW: now we use timestamp instead of parentdocid
			     // for spider replies. so they do not dedup...
			     int64_t parentDocId ,
			     bool      isDel       ) {
	key128_t k;
	k.n1 = (uint32_t)firstIp;
	// push ip to top 32 bits
	k.n1 <<= 32;
	// . top 32 bits of url hash are in the lower 32 bits of k.n1
	// . often the urlhash48 has top bits set that shouldn't be so mask
	//   it to 48 bits
	k.n1 |= (urlHash48 >> 16) & 0xffffffff;
	// remaining 16 bits
	k.n0 = urlHash48 & 0xffff;
	// room for isRequest
	k.n0 <<= 1;
	if ( isRequest ) k.n0 |= 0x01;
	// parent docid
	k.n0 <<= 38;
	// if we are making a spider reply key just leave the parentdocid as 0
	// so we only store one reply per url. the last reply we got.
	// if ( isRequest ) k.n0 |= parentDocId & DOCID_MASK;
	k.n0 |= parentDocId & DOCID_MASK;
	// reserved (padding)
	k.n0 <<= 8;
	// del bit
	k.n0 <<= 1;
	if ( ! isDel ) k.n0 |= 0x01;
	return k;
}

/////////////////////////
/////////////////////////      DOLEDB
/////////////////////////

// reset rdb
void Doledb::reset() { m_rdb.reset(); }

bool Doledb::init ( ) {
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	// . use 5MB for the tree
	int32_t maxTreeMem    = 150000000; // 150MB
	int32_t maxTreeNodes  = maxTreeMem / 78;
	// we use the same disk page size as indexdb (for rdbmap.cpp)
	//int32_t pageSize = GB_INDEXDB_PAGE_SIZE;
	// disk page cache mem, hard code to 5MB
	// int32_t pcmem = 5000000; // g_conf.m_spiderdbMaxDiskPageCacheMem;
	// keep this low if we are the tmp cluster
	// if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// we no longer dump doledb to disk, Rdb::dumpTree() does not allow it
	// pcmem = 0;
	// we now use a page cache
	// if ( ! m_pc.init ( "doledb"  , 
	// 		   RDB_DOLEDB ,
	// 		   pcmem     ,
	// 		   pageSize  ))
	// 	return log(LOG_INIT,"doledb: Init failed.");

	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "doledb"                    ,
			    true                        , // dedup
			    -1                          , // fixedDataSize
			    2                           , // MinFilesToMerge
			    maxTreeMem                  ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0                           , // spiderdbMaxCacheMe
			    0                           , // maxCacheNodes 
			    false                       , // half keys?
			    false                       , // save cache?
			    NULL))//&m_pc                       ))
		return false;
	return true;
}
/*
bool Doledb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	//if ( ! doVerify ) return true;
	// verify
	//if ( verify(coll) ) return true;
	// if not allowing scale, return false
	//if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	//log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}
*/

/////////////////////////
/////////////////////////      SpiderCache
/////////////////////////


// . reload everything this many seconds
// . this was originally done to as a lazy compensation for a bug but
//   now i do not add too many of the same domain if the same domain wait
//   is ample and we know we'll be refreshed in X seconds anyway
//#define DEFAULT_SPIDER_RELOAD_RATE (3*60*60)

// . size of spiderecs to load in one call to readList
// . i increased it to 1MB to speed everything up, seems like cache is 
//   getting loaded up way too slow
#define SR_READ_SIZE (512*1024)

// for caching in s_ufnTree
//#define MAX_NODES (30)

// a global class extern'd in .h file
SpiderCache g_spiderCache;

SpiderCache::SpiderCache ( ) {
	//m_numSpiderColls   = 0;
	//m_isSaving = false;
}

// returns false and set g_errno on error
bool SpiderCache::init ( ) {

	//for ( int32_t i = 0 ; i < MAX_COLL_RECS ; i++ )
	//	m_spiderColls[i] = NULL;

	// success
	return true;
}

/*
static void doneSavingWrapper ( void *state ) {
	SpiderCache *THIS = (SpiderCache *)state;
	log("spcache: done saving something");
	//THIS->doneSaving();
	// . call the callback if any
	// . this let's PageMaster.cpp know when we're closed
	//if (THIS->m_closeCallback) THIS->m_closeCallback(THIS->m_closeState);
}
void SpiderCache::doneSaving ( ) {
	// bail if g_errno was set
	if ( g_errno ) {
		log("spider: Had error saving waitingtree.dat or doleiptable: "
		    "%s.",
		    mstrerror(g_errno));
		g_errno = 0;
	}
	else {
		// display any error, if any, otherwise prints "Success"
		logf(LOG_INFO,"db: Successfully saved waitingtree and "
		     "doleiptable");
	}
	// if still more need to save, not done yet
	if ( needsSave  ( ) ) return;
	// ok, call callback that initiaed the save
	if ( m_callback ) m_callback ( THIS->m_state );
	// ok, we are done!
	//m_isSaving = false;
}
*/


// return false if any tree save blocked
void SpiderCache::save ( bool useThread ) {
	// bail if already saving
	//if ( m_isSaving ) return true;
	// assume saving
	//m_isSaving = true;
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		RdbTree *tree = &sc->m_waitingTree;
		if ( ! tree->m_needsSave ) continue;
		// if already saving from a thread
		if ( tree->m_isSaving ) continue;
		char *filename = "waitingtree";
		char dir[1024];
		sprintf(dir,"%scoll.%s.%"INT32"",g_hostdb.m_dir,
			sc->m_coll,(int32_t)sc->m_collnum);
		// log it for now
		log("spider: saving waiting tree for cn=%"INT32"",(int32_t)i);
		// returns false if it blocked, callback will be called
		tree->fastSave ( dir, // g_hostdb.m_dir ,
				 filename ,
				 useThread ,
				 NULL,//this ,
				 NULL);//doneSavingWrapper );
		// also the doleIpTable
		/*
		filename = "doleiptable.dat";
		sc->m_doleIpTable.fastSave(useThread,
					   dir,
					   filename,
					   NULL,
					   0,
					   NULL,//this,
					   NULL);//doneSavingWrapper );
		*/
		// . crap, this is made at startup from waitintree!
		/*
		// waiting table
		filename = "waitingtable.dat";
		if ( sc->m_waitingTable.m_needsSave )
			logf(LOG_INFO,"db: Saving %s/%s",dir,
			     filename);
		sc->m_waitingTable.fastSave(useThread,
					    dir,
					    filename,
					    NULL,
					    0,
					    NULL,//this,
					    NULL );//doneSavingWrapper );
		*/
	}
	// if still needs save, not done yet, return false to indicate blocked
	//if ( blocked ) return false;
	// all done
	//m_isSaving = false;
	// did not block
	//return true;
}

bool SpiderCache::needsSave ( ) {
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		if ( sc->m_waitingTree.m_needsSave ) return true;
		// also the doleIpTable
		//if ( sc->m_doleIpTable.m_needsSave ) return true;
	}
	return false;
}

void SpiderCache::reset ( ) {
	log(LOG_DEBUG,"spider: resetting spidercache");
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);
		if ( ! sc ) continue;
		sc->reset();
		mdelete ( sc , sizeof(SpiderColl) , "SpiderCache" );
		delete ( sc );
		//m_spiderColls[i] = NULL;
		CollectionRec *cr = g_collectiondb.getRec(i);
		cr->m_spiderColl = NULL;
	}
	//m_numSpiderColls = 0;
}

SpiderColl *SpiderCache::getSpiderCollIffNonNull ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;
	if ( collnum >= g_collectiondb.m_numRecs ) return NULL;
	// int16_tcut
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	// empty?
	if ( ! cr ) return NULL;
	// return it if non-NULL
	return cr->m_spiderColl;
}

bool tryToDeleteSpiderColl ( SpiderColl *sc , char *msg ) {
	// if not being deleted return false
	if ( ! sc->m_deleteMyself ) return false;
	// otherwise always return true
	if ( sc->m_msg5b.m_waitingForList ) {
		log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
		    "waiting1",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// if ( sc->m_msg1.m_mcast.m_inUse ) {
	// 	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
	// 	    "waiting2",
	// 	    (PTRTYPE)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	if ( sc->m_isLoading ) {
		log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
		    "waiting3",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// this means msg5 is out
	if ( sc->m_msg5.m_waitingForList ) {
		log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
		    "waiting4",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// if ( sc->m_gettingList1 ) {
	// 	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" 
	//"waiting5",
	// 	    (int32_t)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	// if ( sc->m_gettingList2 ) {
	// 	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" 
	//"waiting6",
	// 	    (int32_t)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	// there's still a core of someone trying to write to someting
	// in "sc" so we have to try to fix that. somewhere in xmldoc.cpp
	// or spider.cpp. everyone should get sc from cr everytime i'd think
	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" (msg=%s)",
	    (PTRTYPE)sc,(int32_t)sc->m_collnum,msg);
	// . make sure nobody has it
	// . cr might be NULL because Collectiondb.cpp::deleteRec2() might
	//   have nuked it
	//CollectionRec *cr = sc->m_cr;
	// use fake ptrs for easier debugging
	//if ( cr ) cr->m_spiderColl = (SpiderColl *)0x987654;//NULL;
	mdelete ( sc , sizeof(SpiderColl),"postdel1");
	delete ( sc );
	return true;
}

// . get SpiderColl for a collection
// . if it is NULL for that collection then make a new one
SpiderColl *SpiderCache::getSpiderColl ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;
	// return it if non-NULL
	//if ( m_spiderColls [ collnum ] ) return m_spiderColls [ collnum ];
	// if spidering disabled, do not bother creating this!
	//if ( ! g_conf.m_spideringEnabled ) return NULL;
	// int16_tcut
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	// collection might have been reset in which case collnum changes
	if ( ! cr ) return NULL;
	// return it if non-NULL
	SpiderColl *sc = cr->m_spiderColl;
	if ( sc ) return sc;
	// if spidering disabled, do not bother creating this!
	//if ( ! cr->m_spideringEnabled ) return NULL;
	// cast it
	//SpiderColl *sc;
	// make it
	try { sc = new(SpiderColl); }
	catch ( ... ) {
		log("spider: failed to make SpiderColl for collnum=%"INT32"",
		    (int32_t)collnum);
		return NULL;
	}
	// register it
	mnew ( sc , sizeof(SpiderColl), "spcoll" );
	// store it
	//m_spiderColls [ collnum ] = sc;
	cr->m_spiderColl = sc;
	// note it
	logf(LOG_DEBUG,"spider: made spidercoll=%"PTRFMT" for cr=%"PTRFMT"",
	    (PTRTYPE)sc,(PTRTYPE)cr);
	// update this
	//if ( m_numSpiderColls < collnum + 1 )
	//	m_numSpiderColls = collnum + 1;
	// set this
	sc->m_collnum = collnum;
	// save this
	strcpy ( sc->m_coll , cr->m_coll );
	// set this
	if ( ! strcmp ( cr->m_coll,"qatest123" ) ) sc->m_isTestColl = true;
	else                                  sc->m_isTestColl = false;
	
	// set this
	sc->setCollectionRec ( cr ); // sc->m_cr = cr;

	// set first doledb scan key
	sc->m_nextDoledbKey.setMin();

	// turn off quickpolling while loading incase a parm update comes in
	bool saved = g_conf.m_useQuickpoll;
	g_conf.m_useQuickpoll = false;

	// mark it as loading so it can't be deleted while loading
	sc->m_isLoading = true;
	// . load its tables from disk
	// . crap i think this might call quickpoll and we get a parm
	//   update to delete this spider coll!
	sc->load();
	// mark it as loading
	sc->m_isLoading = false;

	// restore
	g_conf.m_useQuickpoll = saved;

	// did crawlbottesting delete it right away?
	if ( tryToDeleteSpiderColl( sc ,"1") ) return NULL;
	// sanity check
	//if ( ! cr ) { char *xx=NULL;*xx=0; }
	// deleted right away?
	//if ( sc->getCollectionRec() == NULL ) { char *xx=NULL;*xx=0; }
	// note it!
	log(LOG_DEBUG,"spider: adding new spider collection for %s",
	    cr->m_coll);
	// that was it
	return sc;
}

/////////////////////////
/////////////////////////      SpiderColl
/////////////////////////

void SpiderColl::setCollectionRec ( CollectionRec *cr ) {
	m_cr = cr;
	// this was useful for debugging a null m_cr bug
	//log("sc: sc 0x%"PTRFMT" setting cr to 0x%"PTRFMT""
	//,(int32_t)this,(int32_t)cr);
}

CollectionRec *SpiderColl::getCollectionRec ( ) {
	//log("sc: sc 0x%"PTRFMT" getting cr of 0x%"PTRFMT""
	//,(int32_t)this,(int32_t)m_cr);
	return m_cr;
}

SpiderColl::SpiderColl () {
	m_overflowList = NULL;
	m_lastOverflowFirstIp = 0;
	m_lastPrinted = 0;
	m_deleteMyself = false;
	m_isLoading = false;
	m_gettingList1 = false;
	m_gettingList2 = false;
	m_lastScanTime = 0;
	m_isPopulatingDoledb = false;
	m_numAdded = 0;
	m_numBytesScanned = 0;
	m_lastPrintCount = 0;
	m_siteListIsEmptyValid = false;
	m_cr = NULL;
	//m_lastSpiderAttempt = 0;
	//m_lastSpiderCouldLaunch = 0;
	//m_numRoundsDone = 0;
	//m_lastDoledbReadEmpty = false; // over all priorities in this coll
	// re-set this to min and set m_needsWaitingTreeRebuild to true
	// when the admin updates the url filters page
	m_waitingTreeNeedsRebuild = false;
	m_nextKey2.setMin();
	m_endKey2.setMax();
	m_spidersOut = 0;
	m_coll[0] = '\0';// = NULL;
	reset();
	// reset this
	memset ( m_outstandingSpiders , 0 , 4 * MAX_SPIDER_PRIORITIES );
	// start off sending all colls local crawl info to all hosts to
	// be sure we are in sync
	memset ( m_sendLocalCrawlInfoToHost , 1 , MAX_HOSTS );
}

int32_t SpiderColl::getTotalOutstandingSpiders ( ) {
	int32_t sum = 0;
	for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ )
		sum += m_outstandingSpiders[i];
	return sum;
}

void nukeDoledb ( collnum_t collnum ) ;

// load the tables that we set when m_doInitialScan is true
bool SpiderColl::load ( ) {

	// error?
	int32_t err = 0;
	// make the dir
	char *coll = g_collectiondb.getColl(m_collnum);
	// sanity check
	if ( ! coll || coll[0]=='\0' ) {
		log("spider: bad collnum of %"INT32"",(int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return false;
		//char *xx=NULL;*xx=0; }
	}

	// reset this once
	//m_msg1Avail    = true;
	m_isPopulatingDoledb = false;

	// keep it kinda low if we got a ton of collections
	int32_t maxMem = 15000;
	int32_t maxNodes = 500;
	if ( g_collectiondb.m_numRecsUsed > 500 ) {
		maxNodes = 100;
		maxMem = maxNodes * 20;
	}

	if ( ! m_lastDownloadCache.init ( maxMem     , // maxcachemem,
					  8          , // fixed data size (MS)
					  false      , // support lists?
					  maxNodes   , // max nodes
					  false      , // use half keys?
					  "downcache", // dbname
					  false      , // load from disk?
					  12         , // key size (firstip)
					  12         , // data key size?
					  -1         ))// numPtrsMax
		return log("spider: dcache init failed");

	// this has a quickpoll in it, so that quickpoll processes
	// a restart request from crawlbottesting for this collnum which
	// calls Collectiondb::resetColl2() which calls deleteSpiderColl()
	// on THIS spidercoll, but our m_loading flag is set
	if (!m_sniTable.set   ( 4,8,0,NULL,0,false,MAX_NICENESS,"snitbl") )
		return false;
	if (!m_cdTable.set    (4,4,0,NULL,0,false,MAX_NICENESS,"cdtbl"))
		return false;
	// doledb seems to have like 32000 entries in it
	int32_t numSlots = 0; // was 128000
	if(!m_doleIpTable.set(4,4,numSlots,NULL,0,false,MAX_NICENESS,"doleip"))
		return false;
	// this should grow dynamically...
	if (!m_waitingTable.set (4,8,16,NULL,0,false,MAX_NICENESS,"waittbl"))
		return false;
	// . a tree of keys, key is earliestSpiderTime|ip (key=12 bytes)
	// . earliestSpiderTime is 0 if unknown
	// . max nodes is 1M but we should grow dynamically! TODO
	// . let's up this to 5M because we are hitting the limit in some
	//   test runs...
	// . try going to 20M now since we hit it again...
	// . start off at just 10 nodes since we grow dynamically now
	if (!m_waitingTree.set(0,10,true,-1,true,"waittree2",
			       false,"waitingtree",sizeof(key_t)))return false;
	m_waitingTreeKeyValid = false;
	m_scanningIp = 0;
	// prevent core with this
	//m_waitingTree.m_rdbId = RDB_NONE;

	// make dir
	char dir[500];
	sprintf(dir,"%scoll.%s.%"INT32"",g_hostdb.m_dir,coll,(int32_t)m_collnum);
	// load up all the tables
	if ( ! m_cdTable .load(dir,"crawldelay.dat"  ) ) err = g_errno;
	if ( ! m_sniTable.load(dir,"siteinlinks.dat" ) ) err = g_errno;
	// and its doledb data
	//if ( ! initializeDoleTables( ) ) err = g_errno;
	// our table that has how many of each firstIP are in doledb
	//if ( ! m_doleIpTable.load(dir,"doleiptable.dat") ) err = g_errno;

	// load in the waiting tree, IPs waiting to get into doledb
	BigFile file;
	file.set ( dir , "waitingtree-saved.dat" , NULL );
	bool treeExists = file.doesExist() > 0;

	// load the table with file named "THISDIR/saved"
	if ( treeExists && ! m_waitingTree.fastLoad(&file,&m_waitingMem) ) 
		err = g_errno;

	// init wait table. scan wait tree and add the ips into table.
	if ( ! makeWaitingTable() ) err = g_errno;
	// save it
	g_errno = err;
	// return false on error
	if ( g_errno ) 
		// note it
		return log("spider: had error loading initial table: %s",
			   mstrerror(g_errno));

	// if we hade doledb0001.dat files on disk then nuke doledb
	// and waiting tree and rebuild now with a tree-only doledb.
	RdbBase *base = getRdbBase ( RDB_DOLEDB , m_collnum );

	// . do this now just to keep everything somewhat in sync
	// . we lost dmoz.org and could not get it back in because it was
	//   in the doleip table but NOT in doledb!!!
	if ( ! makeDoleIPTable() ) return false;

	// delete the files and doledb-saved.dat and the waitingtree
	// and set the waitingtree into rebuild mode.
	if ( base && base->m_numFiles > 0 ) {
		nukeDoledb ( m_collnum );
		return true;
	}

	// otherwise true
	return true;
}

// . scan all spiderRequests in doledb at startup and add them to our tables
// . then, when we scan spiderdb and add to orderTree/urlhashtable it will
//   see that the request is in doledb and set m_doled...
// . initialize the dole table for that then
//   quickly scan doledb and add the doledb records to our trees and
//   tables. that way if we receive a SpiderReply() then addSpiderReply()
//   will be able to find the associated SpiderRequest.
//   MAKE SURE to put each spiderrequest into m_doleTable... and into
//   maybe m_urlHashTable too???
//   this should block since we are at startup...
bool SpiderColl::makeDoleIPTable ( ) {

	log(LOG_DEBUG,"spider: making dole ip table for %s",m_coll);

	key_t startKey ; startKey.setMin();
	key_t endKey   ; endKey.setMax();
	key_t lastKey  ; lastKey.setMin();
	// turn off threads for this so it blocks
	bool enabled = g_threads.areThreadsEnabled();
	// turn off regardless
	g_threads.disableThreads();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_DOLEDB    ,
			      m_collnum     ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      minRecSizes   ,
			      true          , // includeTree?
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0,//MAX_NICENESS  , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        )){
		log(LOG_LOGIC,"spider: getList did not block.");
		return false;
	}
	// int16_tcut
	int32_t minSize=(int32_t)(sizeof(SpiderRequest)+sizeof(key_t)+4-MAX_URL_LEN);
	// all done if empty
	if ( list.isEmpty() ) goto done;
	// loop over entries in list
	for (list.resetListPtr();!list.isExhausted();list.skipCurrentRecord()){
		// get rec
		char *rec = list.getCurrentRec();
		// get key
		key_t k = list.getCurrentKey();		
		// skip deletes -- how did this happen?
		if ( (k.n0 & 0x01) == 0) continue;
		// check this out
		int32_t recSize = list.getCurrentRecSize();
		// zero?
		if ( recSize <= 0 ) { char *xx=NULL;*xx=0; }
		// 16 is bad too... wtf is this?
		if ( recSize <= 16 ) continue;
		// crazy?
		if ( recSize<=minSize) {char *xx=NULL;*xx=0;}
		// . doledb key is 12 bytes, followed by a 4 byte datasize
		// . so skip that key and dataSize to point to spider request
		SpiderRequest *sreq = (SpiderRequest *)(rec+sizeof(key_t)+4);
		// add to dole tables
		if ( ! addToDoleTable ( sreq ) )
			// return false with g_errno set on error
			return false;
	}
	startKey = *(key_t *)list.getLastKey();
	startKey += (uint32_t) 1;
	// watch out for wrap around
	if ( startKey >= *(key_t *)list.getLastKey() ) goto loop;
 done:
	log(LOG_DEBUG,"spider: making dole ip table done.");
	// re-enable threads
	if ( enabled ) g_threads.enableThreads();
	// we wrapped, all done
	return true;
}

key_t makeWaitingTreeKey ( uint64_t spiderTimeMS , int32_t firstIp ) {
	// sanity
	if ( ((int64_t)spiderTimeMS) < 0 ) { char *xx=NULL;*xx=0; }
	// make the wait tree key
	key_t wk;
	wk.n1 = (spiderTimeMS>>32);
	wk.n0 = (spiderTimeMS&0xffffffff);
	wk.n0 <<= 32;
	wk.n0 |= (uint32_t)firstIp;
	// sanity
	if ( wk.n1 & 0x8000000000000000LL ) { char *xx=NULL;*xx=0; }
	return wk;
}

CollectionRec *SpiderColl::getCollRec() {
	CollectionRec *cr = g_collectiondb.m_recs[m_collnum];
	if ( ! cr ) log("spider: lost coll rec");
	return cr;
}

char *SpiderColl::getCollName() {
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return "lostcollection";
	return cr->m_coll;
}

//
// remove all recs from doledb for the given collection
//
static void nukeDoledbWrapper ( int fd , void *state ) {
	g_loop.unregisterSleepCallback ( state , nukeDoledbWrapper );
	collnum_t collnum = *(collnum_t *)state;
	nukeDoledb ( collnum );
}

void nukeDoledb ( collnum_t collnum ) {

	//g_spiderLoop.m_winnerListCache.verify();	
	// in case we changed url filters for this collection #
	g_spiderLoop.m_winnerListCache.clear ( collnum );

	//g_spiderLoop.m_winnerListCache.verify();	

	//WaitEntry *we = (WaitEntry *)state;

	//if ( we->m_registered )
	//	g_loop.unregisterSleepCallback ( we , doDoledbNuke );

	// . nuke doledb for this collnum
	// . it will unlink the files and maps for doledb for this collnum
	// . it will remove all recs of this collnum from its tree too
	if ( g_doledb.getRdb()->isSavingTree () ) {
		g_loop.registerSleepCallback(100,&collnum,nukeDoledbWrapper);
		//we->m_registered = true;
		return;
	}

	// . ok, tree is not saving, it should complete entirely from this call
	g_doledb.getRdb()->deleteAllRecs ( collnum );

	// re-add it back so the RdbBase is new'd
	//g_doledb.getRdb()->addColl2 ( we->m_collnum );

	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull ( collnum );

	if ( sc ) {
		sc->m_lastUrlFiltersUpdate = getTimeLocal();//GlobalNoCore();
		// . make sure to nuke m_doleIpTable as well
		sc->m_doleIpTable.clear();
		// need to recompute this!
		//sc->m_ufnMapValid = false;
		// reset this cache
		//clearUfnTable();
		// log it
		log("spider: rebuilding %s from doledb nuke",
		    sc->getCollName());
		// activate a scan if not already activated
		sc->m_waitingTreeNeedsRebuild = true;
		// if a scan is ongoing, this will re-set it
		sc->m_nextKey2.setMin();
		// clear it?
		sc->m_waitingTree.clear();
		sc->m_waitingTable.clear();
		// kick off the spiderdb scan to rep waiting tree and doledb
		sc->populateWaitingTreeFromSpiderdb(false);
	}

	// nuke this state
	//mfree ( we , sizeof(WaitEntry) , "waitet" );

	// note it
	log("spider: finished nuking doledb for coll (%"INT32")",
	    (int32_t)collnum);
}

// . call this when changing the url filters
// . will make all entries in waiting tree have zero time basically
// . and makes us repopulate doledb from these waiting tree entries
/*
void SpiderColl::urlFiltersChanged ( ) {

	// log it
	log("spider: rebuilding doledb/waitingtree for coll=%s",getCollName());

	WaitEntry *we = (WaitEntry *)mmalloc ( sizeof(WaitEntry) , "waite2" );
	if ( ! we ) {
		log("spider: wait entry alloc: %s",mstrerror(g_errno));
		g_errno = 0;
		return;
	}

	// prepare our state in case the purge operation would block
	we->m_registered = false;
	we->m_cr = m_cr;
	we->m_collnum = m_cr->m_collnum;
	//we->m_callback = doDoledbNuke2;
	//we->m_state = NULL;

	// remove all recs from doledb for the given collection
	doDoledbNuke ( 0 , we );
}
*/

// this one has to scan all of spiderdb
bool SpiderColl::makeWaitingTree ( ) {

	log(LOG_DEBUG,"spider: making waiting tree for %s",m_coll);

	key128_t startKey ; startKey.setMin();
	key128_t endKey   ; endKey.setMax();
	key128_t lastKey  ; lastKey.setMin();
	// turn off threads for this so it blocks
	bool enabled = g_threads.areThreadsEnabled();
	// turn off regardless
	g_threads.disableThreads();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_SPIDERDB  ,
			      m_collnum     ,
			      &list         ,
			      &startKey     ,
			      &endKey       ,
			      minRecSizes   ,
			      true          , // includeTree?
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      MAX_NICENESS  , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        )){
		log(LOG_LOGIC,"spider: getList did not block.");
		return false;
	}
	// all done if empty
	if ( list.isEmpty() ) goto done;
	// loop over entries in list
	for (list.resetListPtr();!list.isExhausted();list.skipCurrentRecord()){
		// get rec
		char *rec = list.getCurrentRec();
		// get key
		key128_t k; list.getCurrentKey(&k);		
		// skip deletes -- how did this happen?
		if ( (k.n0 & 0x01) == 0) continue;
		// check this out
		int32_t recSize = list.getCurrentRecSize();
		// zero?
		if ( recSize <= 0 ) { char *xx=NULL;*xx=0; }
		// 16 is bad too... wtf is this?
		if ( recSize <= 16 ) continue;
		// skip replies
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) continue;
		// get request
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// skip if not assigned to us
		if ( ! isAssignedToUs ( sreq->m_firstIp ) ) continue;
		// get first ip
		int32_t firstIp = sreq->m_firstIp;
		// skip if in dole ip table
		if ( m_doleIpTable.isInTable ( &firstIp ) ) continue;
		// make the key. use 1 for spiderTimeMS. this tells the
		// spider loop that it is temporary and should be updated
		key_t wk = makeWaitingTreeKey ( 1 , firstIp );
		// ok, add to waiting tree
		int32_t wn = m_waitingTree.addKey ( &wk );
		if ( wn < 0 ) {
			log("spider: makeWaitTree: %s",mstrerror(g_errno));
			return false;
		}
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: added time=1 ip=%s to waiting "
			    "tree (node#=%"INT32")", iptoa(firstIp),wn);
		// a tmp var
		int64_t fakeone = 1LL;
		// add to table now since its in the tree
		if ( ! m_waitingTable.addKey ( &firstIp , &fakeone ) ) {
			log("spider: makeWaitTree2: %s",mstrerror(g_errno));
			m_waitingTree.deleteNode3 ( wn , true );
			//log("sper: 6 del node %"INT32" for %s",wn,iptoa(firstIp));
			return false;
		}
	}
	startKey = *(key128_t *)list.getLastKey();
	startKey += (uint32_t) 1;
	// watch out for wrap around
	if ( startKey >= *(key128_t *)list.getLastKey() ) goto loop;
 done:
	log(LOG_DEBUG,"spider: making waiting tree done.");
	// re-enable threads
	if ( enabled ) g_threads.enableThreads();
	// we wrapped, all done
	return true;
}

// for debugging query reindex i guess
int64_t SpiderColl::getEarliestSpiderTimeFromWaitingTree ( int32_t firstIp ) {
	// make the key. use 0 as the time...
	key_t wk = makeWaitingTreeKey ( 0, firstIp );
	// set node from wait tree key. this way we can resume from a prev key
	int32_t node = m_waitingTree.getNextNode ( 0, (char *)&wk );
	// if empty, stop
	if ( node < 0 ) return -1;
	// breathe
	QUICKPOLL(MAX_NICENESS);
	// get the key
	key_t *k = (key_t *)m_waitingTree.getKey ( node );
	// ok, we got one
	int32_t storedFirstIp = (k->n0) & 0xffffffff;
	// match? we call this with a firstIp of 0 below to indicate
	// any IP, we just want to get the next spider time.
	if ( firstIp != 0 && storedFirstIp != firstIp ) return -1;
	// get the time
	uint64_t spiderTimeMS = k->n1;
	// shift upp
	spiderTimeMS <<= 32;
	// or in
	spiderTimeMS |= (k->n0 >> 32);
	// make into seconds
	return spiderTimeMS;
}


bool SpiderColl::makeWaitingTable ( ) {
	log(LOG_DEBUG,"spider: making waiting table for %s.",m_coll);
	int32_t node = m_waitingTree.getFirstNode();
	for ( ; node >= 0 ; node = m_waitingTree.getNextNode(node) ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// get key
		key_t *key = (key_t *)m_waitingTree.getKey(node);
		// get ip from that
		int32_t ip = (key->n0) & 0xffffffff;
		// spider time is up top
		uint64_t spiderTimeMS = (key->n1);
		spiderTimeMS <<= 32;
		spiderTimeMS |= ((key->n0) >> 32);
		// store in waiting table
		if ( ! m_waitingTable.addKey(&ip,&spiderTimeMS) ) return false;
	}
	log(LOG_DEBUG,"spider: making waiting table done.");
	return true;
}

SpiderColl::~SpiderColl () {
	reset();
}

// we call this now instead of reset when Collectiondb::resetColl() is used
void SpiderColl::clearLocks ( ) {

	// remove locks from locktable for all spiders out i guess
	HashTableX *ht = &g_spiderLoop.m_lockTable;
 top:
	// scan the slots
	int32_t ns = ht->m_numSlots;
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		// skip if not our collnum
		if ( lock->m_collnum != m_collnum ) continue;
		// nuke it!
		ht->removeSlot(i);
		// restart since cells may have shifted
		goto top;
	}

	/*
	// reset these for SpiderLoop;
	m_nextDoledbKey.setMin();
	m_didRound = false;
	// set this to -1 here, when we enter spiderDoledUrls() it will
	// see that its -1 and set the m_msg5StartKey
	m_pri2 = -1; // MAX_SPIDER_PRIORITIES - 1;
	m_twinDied = false;
	m_lastUrlFiltersUpdate = 0;

	char *coll = "unknown";
	if ( m_coll[0] ) coll = m_coll;
	logf(LOG_DEBUG,"spider: CLEARING spider cache coll=%s",coll);

	m_ufnMapValid = false;

	m_doleIpTable .clear();
	m_cdTable     .clear();
	m_sniTable    .clear();
	m_waitingTable.clear();
	m_waitingTree .clear();
	m_waitingMem  .clear();

	//m_lastDownloadCache.clear ( m_collnum );

	// copied from reset() below
	for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
		m_nextKeys[i] =	g_doledb.makeFirstKey2 ( i );
		m_isDoledbEmpty[i] = 0;
	}

	// assume the whole thing is not empty
	m_allDoledbPrioritiesEmpty = 0;//false;
	m_lastEmptyCheck = 0;
	*/
}

void SpiderColl::reset ( ) {

	// these don't work because we only store one reply
	// which overwrites any older reply. that's how the 
	// key is. we can change the key to use the timestamp 
	// and not parent docid in makeKey() for spider 
	// replies later.
	// m_numSuccessReplies = 0;
	// m_numFailedReplies  = 0;

	// reset these for SpiderLoop;
	m_nextDoledbKey.setMin();
	//m_didRound = false;
	// set this to -1 here, when we enter spiderDoledUrls() it will
	// see that its -1 and set the m_msg5StartKey
	m_pri2 = -1; // MAX_SPIDER_PRIORITIES - 1;
	m_twinDied = false;
	m_lastUrlFiltersUpdate = 0;

	m_isPopulatingDoledb = false;

	char *coll = "unknown";
	if ( m_coll[0] ) coll = m_coll;
	log(LOG_DEBUG,"spider: resetting spider cache coll=%s",coll);

	m_ufnMapValid = false;

	m_doleIpTable .reset();
	m_cdTable     .reset();
	m_sniTable    .reset();
	m_waitingTable.reset();
	m_waitingTree .reset();
	m_waitingMem  .reset();
	m_winnerTree  .reset();
	m_winnerTable .reset();
	m_dupCache    .reset();

	if ( m_overflowList ) {
		mfree ( m_overflowList , OVERFLOWLISTSIZE * 4 ,"olist" );
		m_overflowList = NULL;
	}

	// each spider priority in the collection has essentially a cursor
	// that references the next spider rec in doledb to spider. it is
	// used as a performance hack to avoid the massive positive/negative
	// key annihilations related to starting at the top of the priority
	// queue every time we scan it, which causes us to do upwards of
	// 300 re-reads!
	for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
		m_nextKeys[i] =	g_doledb.makeFirstKey2 ( i );
		m_isDoledbEmpty[i] = 0;
	}

	// assume the whole thing is not empty
	//m_allDoledbPrioritiesEmpty = 0;//false;
	//m_lastEmptyCheck = 0;

}

bool SpiderColl::updateSiteNumInlinksTable ( int32_t siteHash32, 
					     int32_t sni, 
					     time_t timestamp ) {
	// do not update if invalid
	if ( sni == -1 ) return true;
	// . get entry for siteNumInlinks table
	// . use 32-bit key specialized lookup for speed
	uint64_t *val = (uint64_t *)m_sniTable.getValue32(siteHash32);
	// bail?
	if ( val && ((*val)&0xffffffff) > (uint32_t)timestamp ) return true;
	// . make new data for this key
	// . lower 32 bits is the addedTime
	// . upper 32 bits is the siteNumInlinks
	uint64_t nv = (uint32_t)sni;
	// shift up
	nv <<= 32;
	// or in time
	nv |= (uint32_t)timestamp;//sreq->m_addedTime;
	// just direct update if faster
	if  ( val ) *val = nv;
	// store it anew otherwise
	else if ( ! m_sniTable.addKey(&siteHash32,&nv) )
		// return false with g_errno set on error
		return false;
	// success
	return true;
}

// . we call this when we receive a spider reply in Rdb.cpp
// . returns false and sets g_errno on error
// . xmldoc.cpp adds reply AFTER the negative doledb rec since we decement
//   the count in m_doleIpTable here
bool SpiderColl::addSpiderReply ( SpiderReply *srep ) {

	////
	//
	// skip if not assigned to us for doling
	//
	////
	if ( ! isAssignedToUs ( srep->m_firstIp ) )
		return true;

	/////////
	//
	// remove the lock here
	//
	//////
	int64_t lockKey = makeLockTableKey ( srep );
	
	// int16_tcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	UrlLock *lock = (UrlLock *)ht->getValue ( &lockKey );
	time_t nowGlobal = getTimeGlobal();

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: removing lock uh48=%"INT64" "
		     "lockKey=%"UINT64"",  
		     srep->getUrlHash48(),
		     lockKey );

	// test it
	//if ( m_nowGlobal == 0 && lock )
	//	m_nowGlobal = getTimeGlobal();
	// we do it this way rather than remove it ourselves
	// because a lock request for this guy
	// might be currently outstanding, and it will end up
	// being granted the lock even though we have by now removed
	// it from doledb, because it read doledb before we removed 
	// it! so wait 5 seconds for the doledb negative key to 
	// be absorbed to prevent a url we just spidered from being
	// re-spidered right away because of this sync issue.
	// . if we wait too long then the round end time, SPIDER_DONE_TIMER,
	//   will kick in before us and end the round, then we end up
	//   spidering a previously locked url right after and DOUBLE
	//   increment the round!
	if ( lock ) lock->m_expires = nowGlobal + 2;
	/////
	//
	// but do note that its spider has returned for populating the
	// waiting tree. addToWaitingTree should not add an entry if
	// a spiderReply is still pending according to the lock table,
	// UNLESS, maxSpidersPerIP is more than what the lock table says
	// is currently being spidered.
	//
	/////
	if ( lock ) lock->m_spiderOutstanding = false;
	// bitch if not in there
	// when "rebuilding" (Rebuild.cpp) this msg gets triggered too much...
	// so only show it when in debug mode.
	if ( !lock && g_conf.m_logDebugSpider)//ht->isInTable(&lockKey)) 
		logf(LOG_DEBUG,"spider: rdb: lockKey=%"UINT64" "
		     "was not in lock table",lockKey);

	// now just remove it since we only spider our own urls
	// and doledb is in memory
	g_spiderLoop.m_lockTable.removeKey ( &lockKey );

	// update the latest siteNumInlinks count for this "site" (repeatbelow)
	updateSiteNumInlinksTable ( srep->m_siteHash32, 
				    srep->m_siteNumInlinks,
				    srep->m_spideredTime );

	// . skip the rest if injecting
	// . otherwise it triggers a lookup for this firstip in spiderdb to
	//   get a new spider request to add to doledb
	// . no, because there might be more on disk from the same firstip
	//   so comment this out again
	//if ( srep->m_fromInjectionRequest )
	//	return true;

	// clear error for this
	g_errno = 0;

	// . update the latest crawl delay for this domain
	// . only add to the table if we had a crawl delay
	// . -1 implies an invalid or unknown crawl delay
	// . we have to store crawl delays of -1 now so we at least know we
	//   tried to download the robots.txt (todo: verify that!)
	//   and the webmaster did not have one. then we can 
	//   crawl more vigorously...
	//if ( srep->m_crawlDelayMS >= 0 ) {

	bool update = false;
	// use the domain hash for this guy! since its from robots.txt
	int32_t *cdp = (int32_t *)m_cdTable.getValue32(srep->m_domHash32);
	// update it only if better or empty
	if ( ! cdp ) update = true;

	// no update if injecting or from pagereindex (docid based spider request)
	if ( srep->m_fromInjectionRequest )
		update = false;

	//else if (((*cdp)&0xffffffff)<(uint32_t)srep->m_spideredTime) 
	//	update = true;
	// update m_sniTable if we should
	if ( update ) {
		// . make new data for this key
		// . lower 32 bits is the spideredTime
		// . upper 32 bits is the crawldelay
		int32_t nv = (int32_t)(srep->m_crawlDelayMS);
		// shift up
		//nv <<= 32;
		// or in time
		//nv |= (uint32_t)srep->m_spideredTime;
		// just direct update if faster
		if      ( cdp ) *cdp = nv;
		// store it anew otherwise
		else if ( ! m_cdTable.addKey(&srep->m_domHash32,&nv)){
			// return false with g_errno set on error
			//return false;
			log("spider: failed to add crawl delay for "
			    "firstip=%s",iptoa(srep->m_firstIp));
			// just ignore
			g_errno = 0;
		}
	}

	// . anytime we add a reply then
	//   we must update this downloadTable with the replies 
	//   SpiderReply::m_downloadEndTime so we can obey sameIpWait
	// . that is the earliest that this url can be respidered, but we
	//   also have a sameIpWait constraint we have to consider...
	// . we alone our responsible for adding doledb recs from this ip so
	//   this is easy to throttle...
	// . and make sure to only add to this download time hash table if
	//   SpiderReply::m_downloadEndTime is non-zero, because zero means
	//   no download happened. (TODO: check this)
	// . TODO: consult crawldelay table here too! use that value if is
	//   less than our sameIpWait
	// . make m_lastDownloadTable an rdbcache ...
	// . this is 0 for pagereindex docid-based replies
	if ( srep->m_downloadEndTime )
		m_lastDownloadCache.addLongLong ( m_collnum,
						  srep->m_firstIp ,
						  srep->m_downloadEndTime );
	// log this for now
	if ( g_conf.m_logDebugSpider )
		log("spider: adding spider reply, download end time %"INT64" for "
		    "ip=%s(%"UINT32") uh48=%"UINT64" indexcode=\"%s\" coll=%"INT32" "
		    "k.n1=%"UINT64" k.n0=%"UINT64"",
		    //"to SpiderColl::m_lastDownloadCache",
		    srep->m_downloadEndTime,
		    iptoa(srep->m_firstIp),
		    (uint32_t)srep->m_firstIp,
		    srep->getUrlHash48(),
		    mstrerror(srep->m_errCode),
		    (int32_t)m_collnum,
		    srep->m_key.n1,
		    srep->m_key.n0);
	
	// ignore errors from that, it's just a cache
	g_errno = 0;
	// sanity check - test cache
	//if ( g_conf.m_logDebugSpider && srep->m_downloadEndTime ) {
	//	int64_t last = m_lastDownloadCache.getLongLong ( m_collnum ,
	//						     srep->m_firstIp ,
	//							   -1,// maxAge
	//							   true );//pro
	//	if ( last != srep->m_downloadEndTime ) { char *xx=NULL;*xx=0;}
	//}

	// skip:

	// . add to wait tree and let it populate doledb on its batch run
	// . use a spiderTime of 0 which means unknown and that it needs to
	//   scan spiderdb to get that
	// . returns false if did not add to waiting tree
	// . returns false sets g_errno on error
	bool added = addToWaitingTree ( 0LL, srep->m_firstIp , true );

	// ignore errors i guess
	g_errno = 0;

	// if added to waiting tree, bail now, needs to scan spiderdb
	// in order to add to doledb, because it won't add to waiting tree
	// if we already have spiderrequests in doledb for this firstip
	if ( added ) return true;

	// spider some urls that were doled to us
	g_spiderLoop.spiderDoledUrls( );

	return true;
}


void SpiderColl::removeFromDoledbTable ( int32_t firstIp ) {

	// . decrement doledb table ip count for firstIp
	// . update how many per ip we got doled
	int32_t *score = (int32_t *)m_doleIpTable.getValue32 ( firstIp );

	// wtf! how did this spider without being doled?
	if ( ! score ) {
		//if ( ! srep->m_fromInjectionRequest )
		log("spider: corruption. received spider reply whose "
		    "ip has no entry in dole ip table. firstip=%s",
		    iptoa(firstIp));
		return;
	}

	// reduce it
	*score = *score - 1;

	// now we log it too
	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: removed ip=%s from doleiptable "
		    "(newcount=%"INT32")", iptoa(firstIp),*score);


	// remove if zero
	if ( *score == 0 ) {
		// this can file if writes are disabled on this hashtablex
		// because it is saving
		m_doleIpTable.removeKey ( &firstIp );
		// sanity check
		//if ( ! m_doleIpTable.m_isWritable ) { char *xx=NULL;*xx=0; }
	}
	// wtf!
	if ( *score < 0 ) { char *xx=NULL;*xx=0; }
	// all done?
	if ( g_conf.m_logDebugSpider ) {
		// log that too!
		logf(LOG_DEBUG,"spider: discounting firstip=%s to %"INT32"",
		     iptoa(firstIp),*score);
	}
}


bool SpiderColl::isInDupCache ( SpiderRequest *sreq , bool addToCache ) {

	// init dup cache?
	if ( ! m_dupCache.isInitialized() )
		// use 50k i guess of 64bit numbers and linked list info
		m_dupCache.init ( 90000, 
				  4 , // fixeddatasize (don't really need this)
				  false, // list support?
				  5000, // maxcachenodes
				  false, // usehalfkeys?
				  "urldups", // dbname
				  false, // loadfromdisk
				  12, // cachekeysize
				  0, // datakeysize
				  -1 ); // numptrsmax

	// quit add dups over and over again...
	int64_t dupKey64 = sreq->getUrlHash48();
	// . these flags make big difference in url filters
	// . NOTE: if you see a url that is not getting spidered that should be it might
	//   be because we are not incorporating other flags here...
	if ( sreq->m_fakeFirstIp ) dupKey64 ^= 12345;
	if ( sreq->m_isAddUrl    ) dupKey64 ^= 49387333;
	if ( sreq->m_isInjecting ) dupKey64 ^= 3276404;
	if ( sreq->m_isPageReindex) dupKey64 ^= 32999604;
	if ( sreq->m_forceDelete ) dupKey64 ^= 29386239;
	if ( sreq->m_hadReply    ) dupKey64 ^= 293294099;
	if ( sreq->m_sameDom     ) dupKey64 ^= 963493311;
	if ( sreq->m_sameHost    ) dupKey64 ^= 288844772;
	if ( sreq->m_sameSite    ) dupKey64 ^= 58320355;

	// . maxage=86400,promoteRec=yes. returns -1 if not in there
	// . dupKey64 is for hopcount 0, so if this url is in the dupcache
	//   with a hopcount of zero, do not add it
	if ( m_dupCache.getLong ( 0,dupKey64,86400,true ) != -1 ) {
	dedup:
		if ( g_conf.m_logDebugSpider )
			log("spider: skipping dup request url=%s uh48=%"UINT64"",
			    sreq->m_url,sreq->getUrlHash48());
		return true;
	}
	// if our hopcount is 2 and there is a hopcount 1 in there, do not add
	if ( sreq->m_hopCount >= 2 &&
	     m_dupCache.getLong ( 0,dupKey64 ^ 0x01 ,86400,true ) != -1 ) 
		goto dedup;
	// likewise, if there's a hopcount 2 in there, do not add if we are 3+
	if ( sreq->m_hopCount >= 3 &&
	     m_dupCache.getLong ( 0,dupKey64 ^ 0x02 ,86400,true ) != -1 ) 
		goto dedup;


	if ( ! addToCache ) return false;

	int32_t hc = sreq->m_hopCount;
	// limit hopcount to 3 for making cache key so we don't flood cache
	if ( hc >= 3 ) hc = 3;
	// mangle the key with hopcount before adding it to the cache
	dupKey64 ^= hc;
	// add it
	m_dupCache.addLong(0,dupKey64 ,1);

	return false;
}

// . Rdb.cpp calls SpiderColl::addSpiderRequest/Reply() for every positive
//   spiderdb record it adds to spiderdb. that way our cache is kept 
//   uptodate incrementally
// . returns false and sets g_errno on error
// . if the spiderTime appears to be AFTER m_nextReloadTime then we should
//   not add this spider request to keep the cache trimmed!!! (MDW: TODO)
// . BUT! if we have 150,000 urls that is going to take a int32_t time to
//   spider, so it should have a high reload rate!
bool SpiderColl::addSpiderRequest ( SpiderRequest *sreq , 
				    int64_t nowGlobalMS ) {
	// don't add negative keys or data less thangs
	if ( sreq->m_dataSize <= 0 ) {
		//if ( g_conf.m_logDebugSpider )
		log("spider: add spider request is dataless for "
		    "uh48=%"UINT64"",sreq->getUrlHash48());
		//char *xx=NULL;*xx=0;
		return true;
	}

	// . are we already more or less in spiderdb? true = addToCache
	// . put this above isAssignedToUs() so we *try* to keep twins in sync because
	//   Rdb.cpp won't add the spiderrequest if its in this dup cache, and we add
	//   it to the dupcache here...
	if ( isInDupCache ( sreq , true ) ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: skipping dup request url=%s uh48=%"UINT64"",
			    sreq->m_url,sreq->getUrlHash48());
		return true;
	}

	// skip if not assigned to us for doling
	if ( ! isAssignedToUs ( sreq->m_firstIp ) ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: spider request not assigned to us. "
			    "skipping.");
		return true;
	}

	// . get the url's length contained in this record
	// . it should be NULL terminated
	// . we set the ip here too
	int32_t ulen = sreq->getUrlLen();
	// watch out for corruption
	if ( sreq->m_firstIp ==  0 || sreq->m_firstIp == -1 || ulen <= 0 ) {
		log("spider: Corrupt spider req with url length of "
		    "%"INT32" <= 0 u=%s. dataSize=%"INT32" firstip=%"INT32" uh48=%"UINT64". Skipping.",
		    ulen,sreq->m_url,
		    sreq->m_dataSize,sreq->m_firstIp,sreq->getUrlHash48());
		return true;
	}

	// update crawlinfo stats here and not in xmldoc so that we count
	// seeds and bulk urls added from add url and can use that to
	// determine if the collection is empty of urls or not for printing
	// out the colored bullets in printCollectionNavBar() in Pages.cpp.
	CollectionRec *cr = g_collectiondb.m_recs[m_collnum];
	if ( cr ) {
		cr->m_localCrawlInfo .m_urlsHarvested++;
		cr->m_globalCrawlInfo.m_urlsHarvested++;
		cr->m_needsSave = true;
	}

	// . if already have a request in doledb for this firstIp, forget it!
	// . TODO: make sure we remove from doledb first before adding this
	//   spider request
	// . NOW: allow it in if different priority!!! so maybe hash the
	//   priority in with the firstIp???
	// . we really just need to add it if it beats what is currently
	//   in doledb. so maybe store the best priority doledb in the
	//   data value part of the doleiptable...? therefore we should
	//   probably move this check down below after we get the priority
	//   of the spider request.
	//char *val = (char *)m_doleIpTable.getValue ( &sreq->m_firstIp );
	//if ( val && *val > 0 ) {
	//	if ( g_conf.m_logDebugSpider )
	//		log("spider: request IP already in dole table");
	//	return true;
	//}

	// . skip if already in wait tree
	// . no, no. what if the current url for this firstip is not due to
	//   be spidered until 24 hrs and we are adding a url from this firstip
	//   that should be spidered now...
	//if ( m_waitingTable.isInTable ( &sreq->m_firstIp ) ) {
	//	if ( g_conf.m_logDebugSpider )
	//		log("spider: request already in waiting table");
	//	return true;
	//}


	// . we can't do this because we do not have the spiderReply!!!???
	// . MDW: no, we have to do it because tradesy.com has links to twitter
	//   on every page and twitter is not allowed so we continually
	//   re-scan a big spiderdblist for twitter's firstip. major performace
	//   degradation. so try to get ufn without reply. if we need
	//   a reply to get the ufn then this function should return -1 which
	//   means an unknown ufn and we'll add to waiting tree.
	// get ufn/priority,because if filtered we do not want to add to doledb
	int32_t ufn ;
	// HACK: set isOutlink to true here since we don't know if we have sre
	ufn = ::getUrlFilterNum(sreq,NULL,nowGlobalMS,false,MAX_NICENESS,m_cr,
				true,//isoutlink? HACK!
				NULL,// quota table quotatable
				-1 );  // langid not valid
	// sanity check
	//if ( ufn < 0 ) { 
	//	log("spider: failed to add spider request for %s because "
	//	    "it matched no url filter",
	//	    sreq->m_url);
	//	g_errno = EBADENGINEER;
	//	return false;
	//}

	// spiders disabled for this row in url filters?
	if ( ufn >= 0 && m_cr->m_maxSpidersPerRule[ufn] == 0 ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: request spidersoff ufn=%"INT32" url=%s",ufn,
			    sreq->m_url);
		return true;
	}

	// set the priority (might be the same as old)
	int32_t priority = -1;
	if ( ufn >= 0 ) priority = m_cr->m_spiderPriorities[ufn];

	// sanity checks
	//if ( priority == -1 ) { char *xx=NULL;*xx=0; }
	if ( priority >= MAX_SPIDER_PRIORITIES) {char *xx=NULL;*xx=0;}

	// do not add to doledb if bad
	//if ( priority == SPIDER_PRIORITY_FILTERED ) {
	if ( m_cr->m_forceDelete[ufn] ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: request %s is filtered ufn=%"INT32"",
			    sreq->m_url,ufn);
		return true;
	}

	//if ( priority == SPIDER_PRIORITY_BANNED   ) {
	if ( m_cr->m_forceDelete[ufn] ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: request %s is banned ufn=%"INT32"",
			    sreq->m_url,ufn);
		return true;
	}

	// set it for adding to doledb and computing spidertime
	//sreq->m_ufn      = ufn;
	//sreq->m_priority = priority;


	// get spider time -- i.e. earliest time when we can spider it
	//uint64_t spiderTimeMS = getSpiderTimeMS (sreq,ufn,NULL,nowGlobalMS );
	// sanity
	//if ( (int64_t)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }

	// once in waiting tree, we will scan waiting tree and then lookup
	// each firstIp in waiting tree in spiderdb to get the best
	// SpiderRequest for that firstIp, then we can add it to doledb
	// as int32_t as it can be spidered now
	//bool status = addToWaitingTree ( spiderTimeMS,sreq->m_firstIp,true);
	bool added = addToWaitingTree ( 0 , sreq->m_firstIp , true );

	// if already doled and we beat the priority/spidertime of what
	// was doled then we should probably delete the old doledb key
	// and add the new one. hmm, the waitingtree scan code ...

	// sanity check
	//int64_t ttt=getEarliestSpiderTimeFromWaitingTree(sreq->m_firstIp);
	//logf (LOG_DEBUG,"spider: earliestime=%"INT64" for firstip=%s",
	//      ttt,iptoa(sreq->m_firstIp));
	      
	//if ( ttt != (int64_t)spiderTimeMS ) { char *xx=NULL;*xx=0; }

	// update the latest siteNumInlinks count for this "site"
	if ( sreq->m_siteNumInlinksValid ) {
		// updates m_siteNumInlinksTable
		updateSiteNumInlinksTable ( sreq->m_siteHash32 , 
					    sreq->m_siteNumInlinks ,
					    (time_t)sreq->m_addedTime );
		// clear error for this if there was any
		g_errno = 0;
	}

	if ( ! g_conf.m_logDebugSpider ) return true;//status;

	char *msg = "ADDED";
	if ( ! added ) msg = "DIDNOTADD";
	// log it
	logf(LOG_DEBUG,
	     "spider: %s request to waiting tree %s "
	     "uh48=%"UINT64" "
	     "firstIp=%s "
	     "pageNumInlinks=%"UINT32" "
	     "parentdocid=%"UINT64" "
	     "isinjecting=%"INT32" "
	     "ispagereindex=%"INT32" "
	     "ufn=%"INT32" "
	     "priority=%"INT32" "
	     "addedtime=%"UINT32" "
	     //"spidertime=%"UINT64"",
	     ,
	     msg,
	     sreq->m_url,
	     sreq->getUrlHash48(),
	     iptoa(sreq->m_firstIp),
	     (uint32_t)sreq->m_pageNumInlinks,//(uint32_t)sreq->m_parentFirstIp
	     sreq->getParentDocId(),
	     (int32_t)(bool)sreq->m_isInjecting,
	     (int32_t)(bool)sreq->m_isPageReindex,
	     (int32_t)sreq->m_ufn,
	     (int32_t)sreq->m_priority,
	     (uint32_t)sreq->m_addedTime
	     //spiderTimeMS);
	     );

	return true;//status;
}

bool SpiderColl::printWaitingTree ( ) {
	int32_t node = m_waitingTree.getFirstNode();
	for ( ; node >= 0 ; node = m_waitingTree.getNextNode(node) ) {
		key_t *wk = (key_t *)m_waitingTree.getKey (node);
		// spider time is up top
		uint64_t spiderTimeMS = (wk->n1);
		spiderTimeMS <<= 32;
		spiderTimeMS |= ((wk->n0) >> 32);
		// then ip
		int32_t firstIp = wk->n0 & 0xffffffff;
		// show it
		log("dump: time=%"INT64" firstip=%s",spiderTimeMS,iptoa(firstIp));
	}
	return true;
}

bool SpiderLoop::printLockTable ( ) {
	// count locks
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// scan the slots
	int32_t ns = ht->m_numSlots;
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		// get the key
		int64_t lockKey = *(int64_t *)ht->getKeyFromSlot(i);
		// show it
		log("dump: lock. "
		    "lockkey=%"INT64" "
		    "spiderout=%"INT32" "
		    "confirmed=%"INT32" "
		    "firstip=%s "
		    "expires=%"INT32" "
		    "hostid=%"INT32" "
		    "timestamp=%"INT32" "
		    "sequence=%"INT32" "
		    "collnum=%"INT32" "
		    ,lockKey
		    ,(int32_t)(lock->m_spiderOutstanding)
		    ,(int32_t)(lock->m_confirmed)
		    ,iptoa(lock->m_firstIp)
		    ,lock->m_expires
		    ,lock->m_hostId
		    ,lock->m_timestamp
		    ,lock->m_lockSequence
		    ,(int32_t)lock->m_collnum
		    );
	}
	return true;
}

//////
//
// . 1. called by addSpiderReply(). it should have the sameIpWait available
//      or at least that will be in the crawldelay cache table.
//      SpiderReply::m_crawlDelayMS. Unfortunately, no maxSpidersPerIP!!!
//      we just add a "0" in the waiting tree which means evalIpLoop() will
//      be called and can get the maxSpidersPerIP from the winning candidate
//      and add to the waiting tree based on that.
// . 2. called by addSpiderRequests(). It SHOULD maybe just add a "0" as well
//      to offload the logic. try that.
// . 3. called by populateWaitingTreeFromSpiderdb(). it just adds "0" as well,
//      if not doled
// . 4. UPDATED in evalIpLoop() if the best SpiderRequest for a firstIp is
//      in the future, this is the only time we will add a waiting tree key
//      whose spider time is non-zero. that is where we also take 
//      sameIpWait and maxSpidersPerIP into consideration. evalIpLoop() 
//      will actually REMOVE the entry from the waiting tree if that IP
//      already has the max spiders outstanding per IP. when a spiderReply
//      is received it will populate the waiting tree again with a "0" entry
//      and evalIpLoop() will re-do its check.
//
//////

// . returns true if we added to waiting tree, false if not
// . if one of these add fails consider increasing mem used by tree/table
// . if we lose an ip that sux because it won't be gotten again unless
//   we somehow add another request/reply to spiderdb in the future
bool SpiderColl::addToWaitingTree ( uint64_t spiderTimeMS , int32_t firstIp ,
				    bool callForScan ) {
	// skip if already in wait tree. no - might be an override with
	// a sooner spiderTimeMS
	//if ( m_waitingTable.isInTable ( &firstIp ) ) return true;

	if ( g_conf.m_logDebugSpider )
		log("spider: addtowaitingtree ip=%s",iptoa(firstIp));

	// we are currently reading spiderdb for this ip and trying to find
	// a best SpiderRequest or requests to add to doledb. so if this 
	// happens, let the scan know that more replies or requests came in
	// while we were scanning so that it should not delete the rec from
	// waiting tree and not add to doledb, then we'd lose it forever or
	// until the next waitingtree rebuild was triggered in time.
	//
	// Before i was only setting this in addSpiderRequest() so if a new
	// reply came in it was not setting m_gotNewDataForScanninIp and
	// we ended up losing the IP from the waiting tree forever (or until
	// the next timed rebuild). putting it here seems to fix that.
	if ( firstIp == m_scanningIp ) {
		m_gotNewDataForScanningIp = m_scanningIp;
		//log("spider: got new data for %s",iptoa(firstIp));
		//return true;
	}

	// . this can now be only 0
	// . only evalIpLoop() will add a waiting tree key with a non-zero
	//   value after it figures out the EARLIEST time that a 
	//   SpiderRequest from this firstIp can be spidered.
	if ( spiderTimeMS != 0 ) { char *xx=NULL;*xx=0; }

	// waiting tree might be saving!!!
	if ( ! m_waitingTree.m_isWritable ) {
		log("spider: addtowaitingtree: failed. is not writable. "
		    "saving?");
		return false;
	}

	// only if we are the responsible host in the shard
	if ( ! isAssignedToUs ( firstIp ) ) 
		return false;

	// . do not add to waiting tree if already in doledb
	// . an ip should not exist in both doledb and waiting tree.
	// . waiting tree is meant to be a signal that we need to add
	//   a spiderrequest from that ip into doledb where it can be picked
	//   up for immediate spidering
	if ( m_doleIpTable.isInTable ( &firstIp ) ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: not adding to waiting tree, already in "
			    "doleip table");
		return false;
	}

	// sanity check
	// i think this trigged on gk209 during an auto-save!!! FIX!
	if ( ! m_waitingTree.m_isWritable ) { char *xx=NULL; *xx=0; }

	/*
	///////
	//
	// compute the min time for this entry to satisfy sameIpWait
	//
	///////
	int64_t spiderTimeMS = spiderTimeMSArg;
	int64_t lastDownloadTimeMS = lastDownloadTime ( firstIp );
	// how long to wait between downloads from same ip in milliseconds?
	int32_t sameIpWaitTime = 250; // ms
	if ( ufn >= 0 ) {
		int32_t siwt = m_sc->m_cr->m_spiderIpWaits[ufn];
		if ( siwt >= 0 ) sameIpWaitTime = siwt;
	}
	int64_t minDownloadTime = sameIpWaitTime + siwt;
	// use that if it is more restrictive
	if ( minDownloadTime > now && minDownloadTime > spiderTimeMS )
		spiderTimeMS = minDownloadTime;
	*/


	// see if in tree already, so we can delete it and replace it below
	int32_t ws = m_waitingTable.getSlot ( &firstIp ) ;
	// . this is >= 0 if already in tree
	// . if spiderTimeMS is a sooner time than what this firstIp already
	//   has as its earliest time, then we will override it and have to
	//   update both m_waitingTree and m_waitingTable, however
	//   IF the spiderTimeMS is a later time, then we bail without doing
	//   anything at this point.
	if ( ws >= 0 ) {
		// get timems from waiting table
		int64_t sms = m_waitingTable.getScore64FromSlot(ws);
		// get current time
		//int64_t nowMS = gettimeofdayInMillisecondsGlobal();
		// make the key then
		key_t wk = makeWaitingTreeKey ( sms, firstIp );
		// must be there
		int32_t tn = m_waitingTree.getNode ( (collnum_t)0, (char *)&wk );
		// sanity check. ensure waitingTable and waitingTree in sync
		if ( tn < 0 ) { char *xx=NULL;*xx=0; }
		// not only must we be a sooner time, but we must be 5-seconds
		// sooner than the time currently in there to avoid thrashing
		// when we had a ton of outlinks with this first ip within an
		// 5-second interval.
		//
		// i'm not so sure what i was doing here before, but i don't
		// want to starve the spiders, so make this 100ms not 5000ms
		if ( (int64_t)spiderTimeMS > sms - 100 ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: skip updating waiting tree");
			return false;
		}
		// log the replacement
		if ( g_conf.m_logDebugSpider )
			log("spider: replacing waitingtree key "
			    "oldtime=%"UINT32" newtime=%"UINT32" firstip=%s",
			    (uint32_t)(sms/1000LL),
			    (uint32_t)(spiderTimeMS/1000LL),
			    iptoa(firstIp));
		// remove from tree so we can add it below
		m_waitingTree.deleteNode3 ( tn , false );
		//log("spider: 4 del node %"INT32" for %s",tn,iptoa(firstIp));
	}
	else {
		char *s="";
		// time of 0 means we got the reply for something we spidered
		// in doledb so we will need to recompute the best spider
		// requests for this first ip
		if ( spiderTimeMS==0 ) s = "(replyreset)";
		// log the replacement
		if ( g_conf.m_logDebugSpcache )
			log("spider: adding new key to waitingtree "
			    "newtime=%"UINT32"%s firstip=%s",
			    (uint32_t)(spiderTimeMS/1000LL),s,
			    iptoa(firstIp));
	}

	// make the key
	key_t wk = makeWaitingTreeKey ( spiderTimeMS, firstIp );
	// what is this?
	if ( firstIp == 0 || firstIp == -1 ) {
		log("spider: got ip of %s. cn=%"INT32" "
		    "wtf? failed to add to "
		    "waiting tree, but return true anyway.",
		    iptoa(firstIp) ,
		    (int32_t)m_collnum);
		// don't return true lest m_nextKey2 never gets updated
		// and we end up in an infinite loop doing 
		// populateWaitingTreeFromSpiderdb()
		return true;
		//return false;
		char *xx=NULL; *xx=0;
	}

	// grow the tree if too small!
	int32_t used = m_waitingTree.getNumUsedNodes();
	int32_t max =  m_waitingTree.getNumTotalNodes();
	
	if ( used + 1 > max ) {
		int32_t more = (((int64_t)used) * 15) / 10;
		if ( more < 10 ) more = 10;
		if ( more > 100000 ) more = 100000;
		int32_t newNum = max + more;
		log("spider: growing waiting tree to from %"INT32" to %"INT32" nodes "
		    "for collnum %"INT32"",
		    max , newNum , (int32_t)m_collnum );
		if ( ! m_waitingTree.growTree ( newNum , MAX_NICENESS ) )
			return log("spider: failed to grow waiting tree to "
				   "add firstip %s",iptoa(firstIp) );
		if ( ! m_waitingTable.setTableSize ( newNum , NULL , 0 ) )
			return log("spider: failed to grow waiting table to "
				   "add firstip %s",iptoa(firstIp) );
	}


	// add that
	int32_t wn;
	if ( ( wn = m_waitingTree.addKey ( &wk ) ) < 0 ) {
		log("spider: waitingtree add failed ip=%s. increase max nodes "
		    "lest we lose this IP forever. err=%s",
		    iptoa(firstIp),mstrerror(g_errno));
		//char *xx=NULL; *xx=0;
		return false;
	}

	// note it
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: added time=%"INT64" ip=%s to waiting tree "
		    "scan=%"INT32" node=%"INT32"",
		    spiderTimeMS , iptoa(firstIp),(int32_t)callForScan,wn);

	// add to table now since its in the tree
	if ( ! m_waitingTable.addKey ( &firstIp , &spiderTimeMS ) ) {
		// remove from tree then
		m_waitingTree.deleteNode3 ( wn , false );
		//log("spider: 5 del node %"INT32" for %s",wn,iptoa(firstIp));
		return false;
	}
	// . kick off a scan, i don't care if this blocks or not!
	// . the populatedoledb loop might already have a scan in progress
	//   but usually it won't, so rather than wait for its sleepwrapper
	//   to be called we force it here for speed.
	// . re-entry is false because we are entering for the first time
	// . calling this everytime msg4 adds a spider request is super slow!!!
	//   SO TAKE THIS OUT FOR NOW
	// . no that was not it. mdw. put it back.
	if ( callForScan ) populateDoledbFromWaitingTree ( );
	// tell caller there was no error
	return true;
}

// . this scan is started anytime we call addSpiderRequest() or addSpiderReply
// . if nothing is in tree it quickly exits
// . otherwise it scan the entries in the tree
// . each entry is a key with spiderTime/firstIp
// . if spiderTime > now it stops the scan
// . if the firstIp is already in doledb (m_doleIpTable) then it removes
//   it from the waitingtree and waitingtable. how did that happen?
// . otherwise, it looks up that firstIp in spiderdb to get a list of all
//   the spiderdb recs from that firstIp
// . then it selects the "best" one and adds it to doledb. once added to
//   doledb it adds it to doleIpTable, and remove from waitingtree and 
//   waitingtable
// . returns false if blocked, true otherwise
int32_t SpiderColl::getNextIpFromWaitingTree ( ) {

	// if nothing to scan, bail
	if ( m_waitingTree.isEmpty() ) return 0;
	// reset first key to get first rec in waiting tree
	m_waitingTreeKey.setMin();
	// current time on host #0
	uint64_t nowMS = gettimeofdayInMillisecondsGlobal();
 top:

	// we might have deleted the only node below...
	if ( m_waitingTree.isEmpty() ) return 0;

	// advance to next
	//m_waitingTreeKey += 1LL;
	// assume none
	int32_t firstIp = 0;
	// set node from wait tree key. this way we can resume from a prev key
	int32_t node = m_waitingTree.getNextNode ( 0, (char *)&m_waitingTreeKey );
	// if empty, stop
	if ( node < 0 ) return 0;
	// breathe. take this out fix core because cr could be deleted...
	//QUICKPOLL(MAX_NICENESS);
	// get the key
	key_t *k = (key_t *)m_waitingTree.getKey ( node );

	// ok, we got one
	firstIp = (k->n0) & 0xffffffff;

	// sometimes we take over for a dead host, but if he's no longer
	// dead then we can remove his keys. but first make sure we have had
	// at least one ping from him so we do not remove at startup.
	// if it is in doledb or in the middle of being added to doledb 
	// via msg4, nuke it as well!
	if ( ! isAssignedToUs (firstIp) || m_doleIpTable.isInTable(&firstIp)) {
		// only delete if this host is alive and has sent us a ping
		// before so we know he was up at one time. this way we do not
		// remove all his keys just because we restarted and think he
		// is alive even though we have gotten no ping from him.
		//if ( hp->m_numPingRequests > 0 )
	removeFromTree:
		// these operations should fail if writes have been disabled
		// and becase the trees/tables for spidercache are saving
		// in Process.cpp's g_spiderCache::save() call
		m_waitingTree.deleteNode3 ( node , true );
		//log("spdr: 8 del node node %"INT32" for %s",node,iptoa(firstIp));
		// note it
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: removed1 ip=%s from waiting "
			    "tree. nn=%"INT32"",
			    iptoa(firstIp),m_waitingTree.m_numUsedNodes);

		// log it
		if ( g_conf.m_logDebugSpcache )
			log("spider: erasing waitingtree key firstip=%s",
			    iptoa(firstIp) );
		// remove from table too!
		m_waitingTable.removeKey  ( &firstIp );
		goto top;
	}

	// spider time is up top
	uint64_t spiderTimeMS = (k->n1);
	spiderTimeMS <<= 32;
	spiderTimeMS |= ((k->n0) >> 32);
	// stop if need to wait for this one
	if ( spiderTimeMS > nowMS ) return 0;
	// sanity
	if ( (int64_t)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }
	// save key for deleting when done
	m_waitingTreeKey.n1 = k->n1;
	m_waitingTreeKey.n0 = k->n0;
	m_waitingTreeKeyValid = true;
	m_scanningIp = firstIp;
	// sanity
	if ( firstIp == 0 || firstIp == -1 ) { 
		//char *xx=NULL;*xx=0; }
		log("spider: removing corrupt spiderreq firstip of %"INT32
		    " from waiting tree collnum=%i",
		    firstIp,(int)m_collnum);
		goto removeFromTree;
	}
	// avoid corruption
	
	// we set this to true when done
	//m_isReadDone = false;
	// compute the best request from spiderdb list, not valid yet
	//m_bestRequestValid = false;
	m_lastReplyValid   = false;

	// start reading spiderdb here
	m_nextKey = g_spiderdb.makeFirstKey(firstIp);
	m_endKey  = g_spiderdb.makeLastKey (firstIp);
	// all done
	return firstIp;
}

uint64_t SpiderColl::getNextSpiderTimeFromWaitingTree ( ) {
	// if nothing to scan, bail
	if ( m_waitingTree.isEmpty() ) return 0LL;
	// the key
	key_t mink; mink.setMin();
	// set node from wait tree key. this way we can resume from a prev key
	int32_t node = m_waitingTree.getNextNode (0,(char *)&mink );
	// if empty, stop
	if ( node < 0 ) return 0LL;
	// get the key
	key_t *wk = (key_t *)m_waitingTree.getKey ( node );
	// time from that
	uint64_t spiderTimeMS = (wk->n1);
	spiderTimeMS <<= 32;
	spiderTimeMS |= ((wk->n0) >> 32);
	// stop if need to wait for this one
	return spiderTimeMS;
}
	


static void gotSpiderdbListWrapper2( void *state , RdbList *list,Msg5 *msg5) {

	SpiderColl *THIS = (SpiderColl *)state;

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( THIS ,"2") ) return;

	THIS->m_gettingList2 = false;

	THIS->populateWaitingTreeFromSpiderdb ( true );
}

// lower from 1300 to 300
#define MAXUDPSLOTS 300

//////////////////
//////////////////
//
// THE BACKGROUND FUNCTION
//
// when the user changes the ufn table the waiting tree is flushed
// and repopulated from spiderdb with this. also used for repairs.
//
//////////////////
//////////////////

// . this stores an ip into the waiting tree with a spidertime of "0" so
//   it will be evaluate properly by populateDoledbFromWaitingTree()
//
// . scan spiderdb to make sure each firstip represented in spiderdb is
//   in the waiting tree. it seems they fall out over time. we need to fix
//   that but in the meantime this should do a bg repair. and is nice to have
// . the waiting tree key is reall just a spidertime and a firstip. so we will
//   still need populatedoledbfromwaitingtree to periodically scan firstips
//   that are already in doledb to see if it has a higher-priority request
//   for that firstip. in which case it can add that to doledb too, but then
//   we have to be sure to only grant one lock for a firstip to avoid hammering
//   that firstip
// . this should be called from a sleepwrapper, the same sleep wrapper we
//   call populateDoledbFromWaitingTree() from should be fine
void SpiderColl::populateWaitingTreeFromSpiderdb ( bool reentry ) {
	// skip if in repair mode
	if ( g_repairMode ) return;
	// sanity
	if ( m_deleteMyself ) { char *xx=NULL;*xx=0; }
	// skip if spiders off
	if ( ! m_cr->m_spideringEnabled ) return;
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) return;
	// skip if udp table is full
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) return;
	// if entering for the first time, we need to read list from spiderdb
	if ( ! reentry ) {
		// just return if we should not be doing this yet
		if ( ! m_waitingTreeNeedsRebuild ) return;
		// a double call? can happen if list read is slow...
		if ( m_gettingList2 ) return;

		// . borrow a msg5
		// . if none available just return, we will be called again
		//   by the sleep/timer function

		// . read in a replacement SpiderRequest to add to doledb from
		//   this ip
		// . get the list of spiderdb records
		// . do not include cache, those results are old and will mess
		//   us up
		log(LOG_DEBUG,"spider: populateWaitingTree: "
		    "calling msg5: startKey=0x%"XINT64",0x%"XINT64" "
		    "firstip=%s",
		    m_nextKey2.n1,m_nextKey2.n0,
		    iptoa(g_spiderdb.getFirstIp(&m_nextKey2)));
		// flag it
		m_gettingList2 = true;
		// make state
		//int32_t state2 = (int32_t)m_cr->m_collnum;
		// read the list from local disk
		if ( ! m_msg5b.getList ( RDB_SPIDERDB   ,
					 m_cr->m_collnum,
					 &m_list2       ,
					 &m_nextKey2    ,
					 &m_endKey2     ,
					 SR_READ_SIZE   , // minRecSizes (512k)
					 true           , // includeTree
					 false          , // addToCache
					 0              , // max cache age
					 0              , // startFileNum
					 -1             , // numFiles (all)
					 this,//(void *)state2,//this//state
					 gotSpiderdbListWrapper2 ,
					 MAX_NICENESS   , // niceness
					 true          )) // do error correct?
			// return if blocked
			return;
	}

	// show list stats
	if ( g_conf.m_logDebugSpider )
		log("spider: populateWaitingTree: got list of size %"INT32"",
		    m_list2.m_listSize);

	// unflag it
	m_gettingList2 = false;
	// stop if we are done
	//if ( m_isReadDone2 ) return;

	// if waitingtree is locked for writing because it is saving or
	// writes were disabled then just bail and let the scan be re-called
	// later
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable ) return;

	// int16_tcut
	RdbList *list = &m_list2;
	// ensure we point to the top of the list
	list->resetListPtr();
	// bail on error
	if ( g_errno ) {
		log("spider: Had error getting list of urls "
		    "from spiderdb2: %s.",mstrerror(g_errno));
		//m_isReadDone2 = true;
		return;
	}

	int32_t lastOne = 0;
	// loop over all serialized spiderdb records in the list
	for ( ; ! list->isExhausted() ; ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get spiderdb rec in its serialized form
		char *rec = list->getCurrentRec();
		// skip to next guy
		list->skipCurrentRecord();
		// negative? wtf?
		if ( (rec[0] & 0x01) == 0x00 ) {
			//logf(LOG_DEBUG,"spider: got negative spider rec");
			continue;
		}
		// if its a SpiderReply skip it
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec)) continue;
		// cast it
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// get first ip
		int32_t firstIp = sreq->m_firstIp;
		// corruption?
		// if ( firstIp == 0 || firstIp == -1 )
		// 	gotCorruption = true;
		// if same as last, skip it
		if ( firstIp == lastOne ) continue;
		// set this lastOne for speed
		lastOne = firstIp;
		// check for dmoz. set up gdb on gk157/gk221 to break here
		// so we can see what's going on
		//if ( firstIp == -815809331 )
		//	log("got dmoz");
		// if firstip already in waiting tree, skip it
		if ( m_waitingTable.isInTable ( &firstIp ) ) continue;
		// skip if only our twin should add it to waitingtree/doledb
		if ( ! isAssignedToUs ( firstIp ) ) continue;
		// skip if ip already represented in doledb i guess otehrwise
		// the populatedoledb scan will nuke it!!
		if ( m_doleIpTable.isInTable ( &firstIp ) ) continue;
		// not currently spidering either. when they got their
		// lock they called confirmLockAcquisition() which will
		// have added an entry to the waiting table. sometimes the
		// lock still exists but the spider is done. because the
		// lock persists for 5 seconds afterwards in case there was
		// a lock request for that url in progress, so it will be
		// denied.

		// . this is starving other collections , should be
		//   added to waiting tree anyway! otherwise it won't get
		//   added!!!
		// . so now i made this collection specific, not global
		if ( g_spiderLoop.getNumSpidersOutPerIp (firstIp,m_collnum)>0)
			continue;

		// otherwise, we want to add it with 0 time so the doledb
		// scan will evaluate it properly
		// this will return false if we are saving the tree i guess
		if ( ! addToWaitingTree ( 0 , firstIp , false ) ) {
			log("spider: failed to add ip %s to waiting tree. "
			    "ip will not get spidered then and our "
			    "population of waiting tree will repeat until "
			    "this add happens."
			    , iptoa(firstIp) );
			return;
		}

		// count it
		m_numAdded++;
		// ignore errors for this
		g_errno = 0;
	}

	// are we the final list in the scan?
	bool shortRead = ( list->getListSize() <= 0);//(int32_t)SR_READ_SIZE) ;

	m_numBytesScanned += list->getListSize();

	// reset? still left over from our first scan?
	if ( m_lastPrintCount > m_numBytesScanned )
		m_lastPrintCount = 0;

	// announce every 10MB
	if ( m_numBytesScanned - m_lastPrintCount > 10000000 ) {
		log("spider: %"UINT64" spiderdb bytes scanned for waiting tree "
		    "re-population for cn=%"INT32"",m_numBytesScanned,
		    (int32_t)m_collnum);
		m_lastPrintCount = m_numBytesScanned;
	}

	// debug info
	log(LOG_DEBUG,"spider: Read2 %"INT32" spiderdb bytes.",list->getListSize());
	// reset any errno cuz we're just a cache
	g_errno = 0;

	// if not done, keep going
	if ( ! shortRead ) {
		// . inc it here
		// . it can also be reset on a collection rec update
		key128_t lastKey  = *(key128_t *)list->getLastKey();

		if ( lastKey < m_nextKey2 ) {
			log("spider: got corruption 9. spiderdb "
			    "keys out of order for "
			    "collnum=%"INT32, (int32_t)m_collnum);
			g_corruptCount++;
			// this should result in an empty list read for
			// our next scan of spiderdb. unfortunately we could
			// miss a lot of spider requests then
			m_nextKey2  = m_endKey2;
		}
		else {
			m_nextKey2  = lastKey;
			m_nextKey2 += (uint32_t) 1;
		}

		// watch out for wrap around
		if ( m_nextKey2 < lastKey ) shortRead = true;
		// nah, advance the firstip, should be a lot faster when
		// we are only a few firstips...
		if ( lastOne && lastOne != -1 ) { // && ! gotCorruption ) {
			key128_t cand = g_spiderdb.makeFirstKey(lastOne+1);
			// corruption still seems to happen, so only
			// do this part if it increases the key to avoid
			// putting us into an infinite loop.
			if ( cand > m_nextKey2 ) m_nextKey2 = cand;
		}
	}

	if ( shortRead ) {
		// mark when the scan completed so we can do another one
		// like 24 hrs from that...
		m_lastScanTime = getTimeLocal();
		// log it
		// if ( m_numAdded )
		// 	log("spider: added %"INT32" recs to waiting tree from "
		// 	    "scan of %"INT64" bytes coll=%s",
		// 	    m_numAdded,m_numBytesScanned,
		// 	    m_cr->m_coll);
		// note it
		log("spider: rebuild complete for %s. Added %"INT32" "
		    "recs to waiting tree, scanned %"INT64" bytes of spiderdb.",
		    m_coll,m_numAdded,m_numBytesScanned);
		// reset the count for next scan
		m_numAdded = 0 ;
		m_numBytesScanned = 0;
		// reset for next scan
		m_nextKey2.setMin();
		// no longer need rebuild
		m_waitingTreeNeedsRebuild = false;
		// and re-send the crawlinfo in handerequestc1 to each host
		// so they no if we have urls ready to spider or not. because
		// if we told them no before we completed this rebuild we might
		// have found some urls.
		// MDW: let's not do this unless we find it is a problem
		//m_cr->localCrawlInfoUpdate();
	}

	// free list to save memory
	list->freeList();
	// wait for sleepwrapper to call us again with our updated m_nextKey2
	return;
}

//static bool    s_ufnTreeSet = false;
//static RdbTree s_ufnTree;
//static time_t  s_lastUfnTreeFlushTime = 0;

//////////////////////////
//////////////////////////
//
// The first KEYSTONE function.
//
// CALL THIS ANYTIME to load up doledb from waiting tree entries
//
// This is a key function.
//
// It is called from two places:
//
// 1) sleep callback
//
// 2) addToWaitingTree()
//    is called from addSpiderRequest() anytime a SpiderRequest
//    is added to spiderdb (or from addSpiderReply())
//
// It can only be entered once so will just return if already scanning 
// spiderdb.
//
//////////////////////////
//////////////////////////

// . for each IP in the waiting tree, scan all its SpiderRequests and determine
//   which one should be the next to be spidered. and put that one in doledb.
// . we call this a lot, like if the admin changes the url filters table
//   we have to re-scan all of spiderdb basically and re-do doledb
void SpiderColl::populateDoledbFromWaitingTree ( ) { // bool reentry ) {
	// only one loop can run at a time!
	//if ( ! reentry && m_isPopulating ) return;
	if ( m_isPopulatingDoledb ) return;
	// skip if in repair mode
	if ( g_repairMode ) return;
	// if rebuilding the waiting tree, do that first
	// MDW. re-allow us to populate doledb while waiting tree is being
	// build so spiders can go right away. i had this in there to debug.
	//if ( m_waitingTreeNeedsRebuild ) return;

	// let's skip if spiders off so we can inject/popoulate the index quick
	// since addSpiderRequest() calls addToWaitingTree() which then calls
	// this. 
	if ( ! g_conf.m_spideringEnabled ) return;
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) return;


	// skip if udp table is full
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) return;

	// try skipping!!!!!!!!!!!
	// yeah, this makes us scream. in addition to calling
	// Doledb::m_rdb::addRecord() below
	// WE NEED THIS TO REPOPULATE DOLEDB THOUGH!!!
	//return;
	//if ( g_conf.m_logDebugSpider )
	//	log("spider: in populatedoledbfromwaitingtree "
	//	    "numUsedNodes=%"INT32"",
	// 	    m_waitingTree.m_numUsedNodes);

	// set this flag so we are not re-entered
	m_isPopulatingDoledb = true;
 loop:

	// if waiting tree is being saved, we can't write to it
	// so in that case, bail and wait to be called another time
	RdbTree *wt = &m_waitingTree;
	if( wt->m_isSaving || ! wt->m_isWritable ) {
		m_isPopulatingDoledb = false;
		return;
	}

	// are we trying to exit? some firstip lists can be quite long, so
	// terminate here so all threads can return and we can exit properly
	if ( g_process.m_mode == EXIT_MODE ) {
		m_isPopulatingDoledb = false; 
		return;
	}

	// . get next IP that is due to be spidered from
	// . also sets m_waitingTreeKey so we can delete it easily!
	int32_t ip = getNextIpFromWaitingTree();
	
	// . return if none. all done. unset populating flag.
	// . it returns 0 if the next firstip has a spidertime in the future
	if ( ip == 0 ) { m_isPopulatingDoledb = false; return; }

	// set read range for scanning spiderdb
	m_nextKey = g_spiderdb.makeFirstKey(ip);
	m_endKey  = g_spiderdb.makeLastKey (ip);

	if ( g_conf.m_logDebugSpider )
		log("spider: for cn=%i nextip=%s nextkey=%s",
		    (int)m_collnum,
		    iptoa(ip),
		    KEYSTR(&m_nextKey,sizeof(key128_t)));

	//////
	//
	// do TWO PASSES, one to count pages, the other to get the best url!!
	//
	//////
	// assume we don't have to do two passes
	m_countingPagesIndexed = false;
	// get the collectionrec
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	// but if we have quota based url filters we do have to count
	if ( cr && cr->m_urlFiltersHavePageCounts ) {
		// tell evalIpLoop() to count first
		m_countingPagesIndexed = true;
		// reset this stuff used for counting UNIQUE votes
		m_lastReqUh48a = 0LL;
		m_lastReqUh48b = 0LL;
		m_lastRepUh48  = 0LL;
		// and setup the LOCAL counting table if not initialized
		if ( m_localTable.m_ks == 0 ) 
			m_localTable.set (4,4,0,NULL,0,false,0,"ltpct" );
		// otherwise, just reset it so we can repopulate it
		else m_localTable.reset();
	}


	// debug output
	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: evalIpLoop: waitingtree nextip=%s "
		    "numUsedNodes=%"INT32"",iptoa(ip),m_waitingTree.m_numUsedNodes);

	/*
	// assume using tree
	m_useTree = true;

	// . flush the tree every 12 hours
	// . i guess we could add incoming requests to the ufntree if
	//   they strictly beat the ufn tree tail node, HOWEVER, we 
	//   still have the problem of that if a url we spidered is due
	//   to be respidered very soon we will miss it, as only the reply
	//   is added back into spiderdb, not a new request.
	int32_t nowLocal = getTimeLocal();
	// make it one hour so we don't cock-block a new high priority 
	// request that just got added... crap, what if its an addurl
	// or something like that????
	if ( nowLocal - s_lastUfnTreeFlushTime > 3600 ) {
		s_ufnTree.clear();
		s_lastUfnTreeFlushTime = nowLocal;
	}

	int64_t uh48;

	//
	// s_ufnTree tries to cache the top X spiderrequests for an IP
	// that should be spidered next so we do not have to scan like
	// a million spiderrequests in spiderdb to find the best one.
	//

	// if we have a specific uh48 targetted in s_ufnTree then that
	// saves a ton of time!
	// key format for s_ufnTree:
	// iiiiiiii iiiiiiii iiiiiii iiiiiii  i = firstip
	// PPPPPPPP tttttttt ttttttt ttttttt  P = priority
	// tttttttt tttttttt hhhhhhh hhhhhhh  t = spiderTimeMS (40 bits)
	// hhhhhhhh hhhhhhhh hhhhhhh hhhhhhh  h = urlhash48
	key128_t key;
	key.n1 = ip;
	key.n1 <<= 32;
	key.n0 = 0LL;
	int32_t node = s_ufnTree.getNextNode(0,(char *)&key);
	// cancel node if not from our ip
	if ( node >= 0 ) {
		key128_t *rk = (key128_t *)s_ufnTree.getKey ( node );
		if ( (rk->n1 >> 32) != (uint32_t)ip ) node = -1;
	}
	if ( node >= 0 ) {
		// get the key
		key128_t *nk = (key128_t *)s_ufnTree.getKey ( node );
		// parse out uh48
		uh48 = nk->n0;
		// mask out spidertimems
		uh48 &= 0x0000ffffffffffffLL;
		// use that to refine the key range immensley!
		m_nextKey = g_spiderdb.makeFirstKey2 (ip, uh48);
		m_endKey  = g_spiderdb.makeLastKey2  (ip, uh48);
		// do not add the recs to the tree!
		m_useTree = false;
	}
	*/
	// turn this off until we figure out why it sux
	m_useTree = false;

	// so we know if we are the first read or not...
	m_firstKey = m_nextKey;

	// . initialize this before scanning the spiderdb recs of an ip
	// . it lets us know if we recvd new spider requests for m_scanningIp
	//   while we were doing the scan
	m_gotNewDataForScanningIp = 0;

	m_lastListSize = -1;

	// let evalIpLoop() know it has not yet tried to read from spiderdb
	m_didRead = false;

	// reset this
	int32_t maxWinners = (int32_t)MAX_WINNER_NODES;
	//if ( ! m_cr->m_isCustomCrawl ) maxWinners = 1;

	if ( m_winnerTree.m_numNodes == 0 &&
	     ! m_winnerTree.set ( -1 , // fixeddatasize
				  maxWinners , // maxnumnodes
				  true , // balance?
				  maxWinners * MAX_BEST_REQUEST_SIZE, // memmax
				  true, // owndata?
				  "wintree", // allocname
				  false, // datainptrs?
				  NULL, // dbname
				  sizeof(key192_t), // keysize
				  false, // useprotection?
				  false, // allowdups?
				  -1 ) ) { // rdbid
		m_isPopulatingDoledb = false;
		log("spider: winntree set: %s",mstrerror(g_errno));
		return;
	}

	if ( ! m_winnerTable.isInitialized() &&
	     ! m_winnerTable.set ( 8 , // uh48 is key
				   sizeof(key192_t) , // winnertree key is data
				   64 , // 64 slots initially
				   NULL ,
				   0 ,
				   false , // allow dups?
				   MAX_NICENESS ,
				   "wtdedup" ) ) {
		m_isPopulatingDoledb = false;
		log("spider: wintable set: %s",mstrerror(g_errno));
		return;
	}

	// clear it before evaluating this ip so it is empty
	m_winnerTree.clear();

	// and table as well now
	m_winnerTable.clear();

	// reset this as well
	m_minFutureTimeMS = 0LL;

	m_totalBytesScanned = 0LL;

	m_totalNewSpiderRequests = 0LL;

	m_lastOverflowFirstIp = 0;
	
	// . look up in spiderdb otherwise and add best req to doledb from ip
	// . if it blocks ultimately it calls gotSpiderdbListWrapper() which
	//   calls this function again with re-entry set to true
	if ( ! evalIpLoop ( ) ) return ;

	// oom error? i've seen this happen and we end up locking up!
	if ( g_errno ) { 
		log("spider: evalIpLoop: %s",mstrerror(g_errno));
		m_isPopulatingDoledb = false; 
		return; 
	}
	// try more
	goto loop;
}

/*
// replace this func with the one above...
static void doledWrapper ( void *state ) {
	SpiderColl *THIS = (SpiderColl *)state;
	// msg4 is available again
	THIS->m_msg1Avail = true;

	if ( g_errno )
		log("spider: msg1 addlist had error: %s. need to "
		    "somehow reduce doleiptable score now...",
		    mstrerror(g_errno));

	// no longer populating doledb. we also set to false in 
	// gotSpiderListWrapper
	//THIS->m_isPopulating = false;

	int64_t now = gettimeofdayInMilliseconds();
	int64_t diff = now - THIS->m_msg4Start;
	// we add recs to doledb using msg1 to keep things fast because
	// msg4 has a delay of 500ms in it. but even then, msg1 can take
	// 6ms or more just because of load issues.
	if ( diff > 10 ) 
		log("spider: adding to doledb took %"INT64"ms",diff);

	// we are done!! that was the final step...
	THIS->m_isPopulating = false;

	// did collection get nuked while we were waiting for msg1 reply?
	if ( tryToDeleteSpiderColl ( THIS ,"3") ) return;

	// . we added a rec to doledb for the firstIp in m_waitingTreeKey, so
	//   now go to the next node in the wait tree.
	// . it will get the next key after m_waitingTreeKey
	// . re-entry is true because we just got the msg4 reply
	THIS->populateDoledbFromWaitingTree ( );
}
*/

key128_t makeUfnTreeKey ( int32_t      firstIp      ,
			  int32_t      priority     ,
			  int64_t spiderTimeMS ,
			  int64_t uh48         ) {
	// sanity check, do not allow negative priorities for now
	if ( priority < 0 ) { char *xx=NULL;*xx=0; }
	if ( priority > 255 ) { char *xx=NULL;*xx=0; }
	key128_t key;
	key.n1 = (uint32_t)firstIp;
	// all of priority (COMPLEMENTED!)
	key.n1 <<= 8;
	key.n1 |= (unsigned char)(255-priority);
	// top 3 bytes of spiderTimeMS (5 bytes total)
	key.n1 <<= 24;
	key.n1 |= ((spiderTimeMS >> 16) & 0x00ffffff);
	// remaining 2 bytes of spiderTimeMS goes in key.n0
	key.n0 = (spiderTimeMS & 0xffff);
	// 6 bytes uh48
	key.n0 <<= 48;
	key.n0 |= uh48;
	return key;
}

////////
//
// winner tree key. holds the top/best spider requests for a firstIp
// for spidering purposes.
//
////////

// key bitmap (192 bits):
//
// ffffffff ffffffff ffffffff ffffffff  f=firstIp
// pppppppp pppppppp HHHHHHHH HHHHHHHH  p=255-priority  H=hopcount
// tttttttt tttttttt tttttttt tttttttt  t=spiderTimeMS
// tttttttt tttttttt tttttttt tttttttt  h=urlHash48
// hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh 
// hhhhhhhh hhhhhhhh 00000000 00000000

key192_t makeWinnerTreeKey ( int32_t firstIp ,
			     int32_t priority ,
			     int32_t hopCount,
			     int64_t spiderTimeMS ,
			     int64_t uh48 ) {
	key192_t k;
	k.n2 = firstIp;
	k.n2 <<= 16;
	k.n2 |= (255-priority);
	k.n2 <<= 16;
	// query reindex is still using hopcount -1...
	if ( hopCount == -1 ) hopCount = 0;
	if ( hopCount < 0 ) { char *xx=NULL;*xx=0; }
	if ( hopCount > 0xffff ) hopCount = 0xffff;
	k.n2 |= hopCount;

	k.n1 = spiderTimeMS;

	k.n0 = uh48;
	k.n0 <<= 16;

	return k;
}

void parseWinnerTreeKey ( key192_t  *k ,
			  int32_t      *firstIp ,
			  int32_t      *priority ,
			  int32_t *hopCount,
			  int64_t  *spiderTimeMS ,
			  int64_t *uh48 ) {
	*firstIp = (k->n2) >> 32;
	*priority = 255 - ((k->n2 >> 16) & 0xffff);
	*hopCount = (k->n2 & 0xffff);

	*spiderTimeMS = k->n1;

	*uh48 = (k->n0 >> 16);
}

void testWinnerTreeKey ( ) {
	int32_t firstIp = 1234567;
	int32_t priority = 123;
	int64_t spiderTimeMS = 456789123LL;
	int64_t uh48 = 987654321888LL;
	int32_t hc = 4321;
	key192_t k = makeWinnerTreeKey (firstIp,priority,hc,spiderTimeMS,uh48);
	int32_t firstIp2;
	int32_t priority2;
	int64_t spiderTimeMS2;
	int64_t uh482;
	int32_t hc2;
	parseWinnerTreeKey(&k,&firstIp2,&priority2,&hc2,&spiderTimeMS2,&uh482);
	if ( firstIp != firstIp2 ) { char *xx=NULL;*xx=0; }
	if ( priority != priority2 ) { char *xx=NULL;*xx=0; }
	if ( spiderTimeMS != spiderTimeMS2 ) { char *xx=NULL;*xx=0; }
	if ( uh48 != uh482 ) { char *xx=NULL;*xx=0; }
	if ( hc != hc2 ) { char *xx=NULL;*xx=0; }
}

void removeExpiredLocks ( int32_t hostId );


static void gotSpiderdbListWrapper ( void *state , RdbList *list , Msg5 *msg5){
	SpiderColl *THIS = (SpiderColl *)state;
	// prevent a core
	THIS->m_gettingList1 = false;
	// are we trying to exit? some firstip lists can be quite long, so
	// terminate here so all threads can return and we can exit properly
	if ( g_process.m_mode == EXIT_MODE ) return;
	// return if that blocked
	if ( ! THIS->evalIpLoop() ) return;
	// we are done, re-entry popuatedoledb
	THIS->m_isPopulatingDoledb = false;
	// gotta set m_isPopulatingDoledb to false lest it won't work
	THIS->populateDoledbFromWaitingTree ( );
}


///////////////////
//
// KEYSTONE FUNCTION
//
// . READ ALL spiderdb recs for IP of m_scanningIp
// . add winner to doledb
// . called ONLY by populateDoledbFromWaitingTree()
//
// . continually scan spiderdb requests for a particular ip, m_scanningIp
// . compute the best spider request to spider next
// . add it to doledb
// . getNextIpFromWaitingTree() must have been called to set m_scanningIp
//   otherwise m_bestRequestValid might not have been reset to false
//
///////////////////

bool SpiderColl::evalIpLoop ( ) {

	//testWinnerTreeKey ( );

	// sanity
	if ( m_scanningIp == 0 || m_scanningIp == -1 ) { char *xx=NULL;*xx=0;}

	// are we trying to exit? some firstip lists can be quite long, so
	// terminate here so all threads can return and we can exit properly
	if ( g_process.m_mode == EXIT_MODE ) return true;

	// if this ip is in the winnerlistcache use that. it saves
	// us a lot of time.
	key_t cacheKey;
	cacheKey.n0 = m_scanningIp;
	cacheKey.n1 = 0;
	char *doleBuf = NULL;
	int32_t doleBufSize;
	RdbCache *wc = &g_spiderLoop.m_winnerListCache;
	time_t cachedTimestamp = 0;
	bool inCache = false;
	bool useCache = true;
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( this ,"6") ) return false;

	// if doing site or page quotes for the sitepages or domainpages
	// url filter expressions, we can't muck with the cache because
	// we end up skipping the counting part.
	if ( ! cr )
		useCache = false;
	if ( cr && cr->m_urlFiltersHavePageCounts )
		useCache = false;
	if ( m_countingPagesIndexed )
		useCache = false;
	// assume not from cache
	if ( useCache ) {
		//wc->verify();
		inCache = wc->getRecord ( m_collnum     ,
					  (char *)&cacheKey ,
					  &doleBuf,
					  &doleBufSize  ,
					  false, // doCopy?
					  // we raised MAX_WINNER_NODES so
					  // grow from 600 to 1200
					  // (10 mins to 20 mins) to make
					  // some crawls faster
					  1200, // maxAge, 600 seconds
					  true ,// incCounts
					  &cachedTimestamp , // rec timestamp
					  true );  // promote rec?
		//wc->verify();
	}


	// if ( m_collnum == 18752 ) {
	// 	int32_t coff = 0;
	// 	if ( inCache && doleBufSize >= 4 ) coff = *(int32_t *)doleBuf;
	// 	log("spider: usecache=%i incache=%i dbufsize=%i currentoff=%i "
	// 	    "ctime=%i ip=%s"
	// 	    ,(int)useCache
	// 	    ,(int)inCache
	// 	    ,(int)doleBufSize
	// 	    ,(int)coff
	// 	    ,(int)cachedTimestamp
	// 	    ,iptoa(m_scanningIp));
	// }

	// doleBuf could be NULL i guess...
	if ( inCache ) { // && doleBufSize > 0 ) {
		int32_t crc = hash32 ( doleBuf + 4 , doleBufSize - 4 );
		if ( g_conf.m_logDebugSpider ) // || m_collnum == 18752 )
			log("spider: GOT %"INT32" bytes of SpiderRequests "
			    "from winnerlistcache for ip %s ptr=0x%"PTRFMT
			    " crc=%"UINT32
			    ,doleBufSize,
			    iptoa(m_scanningIp),
			    (PTRTYPE)doleBuf,
			    crc);
		// set own to false so it doesn't get freed
		// m_doleBuf.setBuf ( doleBuf , 
		// 		   doleBufSize ,
		// 		   doleBufSize , 
		// 		   false , // ownData?
		// 		   0 ); // encoding. doesn't matter.
		//m_doleBuf.reset();
		// gotta copy it because we end up re-adding part of it
		// to rdbcache below
		//m_doleBuf.safeMemcpy ( doleBuf , doleBufSize );
		// we no longer re-add to avoid churn. but do not free it
		// so do not 'own' it.
		SafeBuf sb;
		sb.setBuf ( doleBuf, doleBufSize, doleBufSize, false );
		// now add the first rec m_doleBuf into doledb's tree
		// and re-add the rest back to the cache with the same key.
		return addDoleBufIntoDoledb(&sb,true);//,cachedTimestamp)
	}

 top:

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( this ,"4") ) return false;

	// if first time here, let's do a read first
	if ( ! m_didRead ) {
		// reset list size to 0
		m_list.reset();
		// assume we did a read now
		m_didRead = true;
		// reset some stuff
		m_lastScanningIp = 0;

		// reset these that need to keep track of requests for
		// the same url that might span two spiderdb lists or more
		m_lastSreqUh48 = 0LL;

		// do a read. if it blocks it will recall this loop
		if ( ! readListFromSpiderdb () ) return false;
	}

 loop:

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( this ,"5") )
		// pretend to block since we got deleted!!!
		return false;

	// . did reading the list from spiderdb have an error?
	// . i guess we don't add to doledb then
	if ( g_errno ) {
		log("spider: Had error getting list of urls "
		    "from spiderdb: %s.",mstrerror(g_errno));
		// save mem
		m_list.freeList();
		//m_isReadDone = true;
		return true;
	}


	// if we started reading, then assume we got a fresh list here
	if ( g_conf.m_logDebugSpider )
		log("spider: back from msg5 spiderdb read2 of %"INT32" bytes "
		    "(cn=%"INT32")",m_list.m_listSize,(int32_t)m_collnum);


	// . set the winning request for all lists we read so far
	// . if m_countingPagesIndexed is true this will just fill in
	//   quota info into m_localTable...
	scanListForWinners();

	// if list not empty, keep reading!
	if ( ! m_list.isEmpty() ) {
		// update m_nextKey for successive reads of spiderdb by
		// calling readListFromSpiderdb()
		key128_t lastKey  = *(key128_t *)m_list.getLastKey();
		// sanity
		//if ( endKey != finalKey ) { char *xx=NULL;*xx=0; }
		// crazy corruption?
		if ( lastKey < m_nextKey ) {
			log("spider: got corruption. spiderdb "
			    "keys out of order for "
			    "collnum=%"INT32" for evaluation of "
			    "firstip=%s so terminating evaluation of that "
			    "firstip." ,
			    (int32_t)m_collnum,
			    iptoa(m_scanningIp));
			g_corruptCount++;
			// this should result in an empty list read for
			// m_scanningIp in spiderdb
			m_nextKey  = m_endKey;
		}
		else {
			m_nextKey  = lastKey;
			m_nextKey += (uint32_t) 1;
		}
		// . watch out for wrap around
		// . normally i would go by this to indicate that we are
		//   done reading, but there's some bugs... so we go
		//   by whether our list is empty or not for now
		if ( m_nextKey < lastKey ) m_nextKey = lastKey;
		// reset list to save mem
		m_list.reset();
		// read more! return if it blocked
		if ( ! readListFromSpiderdb() ) return false;
		// we got a list without blocking
		goto loop;
	}


	// . we are all done if last list read was empty
	// . if we were just counting pages for quota, do a 2nd pass!
	if ( m_countingPagesIndexed ) {
		// do not do again. 
		m_countingPagesIndexed = false;
		// start at the top again
		m_nextKey = g_spiderdb.makeFirstKey(m_scanningIp);
		// this time m_localTable should have the quota info in it so 
		// getUrlFilterNum() can use that
		m_didRead = false;
		// do the 2nd pass. read list from the very top.
		goto top;
	}

	// free list to save memory
	m_list.freeList();

	// . add all winners if we can in m_winnerTree into doledb
	// . if list was empty, then reading is all done so take the winner we 
	//   got from all the lists we did read for this IP and add him 
	//   to doledb
	// . if no winner exists, then remove m_scanningIp from m_waitingTree
	//   so we do not waste our time again. if url filters change then
	//   waiting tree will be rebuilt and we'll try again... or if
	//   a new spider request or reply for this ip comes in we'll try
	//   again as well...
	// . this returns false if blocked adding to doledb using msg1
	if ( ! addWinnersIntoDoledb() ) return false;

	// . do more from tree
	// . re-entry is true because we just got the  msg5 reply
	// . don't do this because populateDoledb calls us in a loop
	//   and we call it from all our callbacks if we blocked...
	//populateDoledbFromWaitingTree ( true );

	// we are done...
	return true;
}

// . this is ONLY CALLED from evalIpLoop() above
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool SpiderColl::readListFromSpiderdb ( ) {

	if ( ! m_waitingTreeKeyValid ) { char *xx=NULL;*xx=0; }
	if ( ! m_scanningIp ) { char *xx=NULL;*xx=0; }

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) {
		log("spider: lost collnum %"INT32"",(int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return true;
	}

	// i guess we are always restricted to an ip, because
	// populateWaitingTreeFromSpiderdb calls its own msg5.
	int32_t firstIp0 = g_spiderdb.getFirstIp(&m_nextKey);
	// sanity
	if ( m_scanningIp != firstIp0 ) { char *xx=NULL;*xx=0; }
	// sometimes we already have this ip in doledb/doleiptable
	// already and somehow we try to scan spiderdb for it anyway
	if ( m_doleIpTable.isInTable ( &firstIp0 ) ) { char *xx=NULL;*xx=0;}
	// if it got zapped from the waiting tree by the time we read the list
	if ( ! m_waitingTable.isInTable ( &m_scanningIp ) ) return true;
	// sanity check
	int32_t wn = m_waitingTree.getNode(0,(char *)&m_waitingTreeKey);
	// it gets removed because addSpiderReply() calls addToWaitingTree
	// and replaces the node we are scanning with one that has a better
	// time, an earlier time, even though that time may have come and
	// we are scanning it now. perhaps addToWaitingTree() should ignore
	// the ip if it equals m_scanningIp?
	if ( wn < 0 ) { 
		log("spider: waiting tree key removed while reading list "
		    "for %s (%"INT32")",
		    cr->m_coll,(int32_t)m_collnum);
		return true;
	}
	// sanity. if first time, this must be invalid
	//if ( needList && m_nextKey == m_firstKey && m_bestRequestValid ) {
	//	char *xx=NULL; *xx=0 ; }

	// . if the scanning ip has too many outstanding spiders
	// . looks a UrlLock::m_firstIp and UrlLock::m_isSpiderOutstanding
	//   since the lock lives for 5 seconds after the spider reply
	//   comes back.
	// . when the spiderReply comes back that will re-add a "0" entry
	//   to the waiting tree. 
	// . PROBLEM: some spiders don't seem to add a spiderReply!! wtf???
	//   they end up having their locks timeout after like 3 hrs?
	// . maybe just do not add to waiting tree in confirmLockAcquisition()
	//   handler in such cases? YEAH.. try that
	//int32_t numOutPerIp = getOustandingSpidersPerIp ( firstIp );
	//if ( numOutPerIp > maxSpidersPerIp ) {
	//	// remove from the tree and table
	//	removeFromWaitingTree ( firstIp );
	//	return true;
	//}

	// readLoop:

	// if we re-entered from the read wrapper, jump down
	//if ( needList ) {

	// sanity check
	if ( m_gettingList1 ) { char *xx=NULL;*xx=0; }
	// . read in a replacement SpiderRequest to add to doledb from
	//   this ip
	// . get the list of spiderdb records
	// . do not include cache, those results are old and will mess
	//   us up
	if (g_conf.m_logDebugSpider ) {
		// got print each out individually because KEYSTR
		// uses a static buffer to store the string
		SafeBuf tmp;
		tmp.safePrintf("spider: readListFromSpiderdb: "
			       "calling msg5: ");
		tmp.safePrintf("firstKey=%s "
			       ,KEYSTR(&m_firstKey,sizeof(key128_t)));
		tmp.safePrintf("endKey=%s "
			       ,KEYSTR(&m_endKey,sizeof(key128_t)));
		tmp.safePrintf("nextKey=%s "
			       ,KEYSTR(&m_nextKey,sizeof(key128_t)));
		tmp.safePrintf("firstip=%s "
			       ,iptoa(m_scanningIp));
		tmp.safePrintf("(cn=%"INT32")",(int32_t)m_collnum);
		log(LOG_DEBUG,"%s",tmp.getBufStart());
	}
	// log this better
	if ( g_conf.m_logDebugSpider )
		log("spider: readListFromSpiderdb: firstip=%s key=%s"
		    ,iptoa(m_scanningIp)
		    ,KEYSTR(&m_nextKey,sizeof(key128_t) ) );
	// flag it
	m_gettingList1 = true;
	// make state
	//int32_t state2 = (int32_t)m_cr->m_collnum;
	// . read the list from local disk
	// . if a niceness 0 intersect thread is taking a LONG time
	//   then this will not complete in a int32_t time and we
	//   end up timing out the round. so try checking for
	//   m_gettingList in spiderDoledUrls() and setting
	//   m_lastSpiderCouldLaunch
	if ( ! m_msg5.getList ( RDB_SPIDERDB   ,
				m_cr->m_collnum   ,
				&m_list        ,
				&m_nextKey      ,
				&m_endKey       ,
				SR_READ_SIZE   , // minRecSizes (512k)
				true           , // includeTree
				false          , // addToCache
				0              , // max cache age
				0              , // startFileNum
				-1             , // numFiles (all)
				this,//(void *)state2,//this,//state 
				gotSpiderdbListWrapper ,
				MAX_NICENESS   , // niceness
				true          )) // do error correct?
		// return false if blocked
		return false ;
	// note its return
	if ( g_conf.m_logDebugSpider )
		log("spider: back from msg5 spiderdb read of %"INT32" bytes",
		    m_list.m_listSize);
	// no longer getting list
	m_gettingList1 = false;

	// got it without blocking. maybe all in tree or in cache
	return true;

	// unflag it
	//m_gettingList = false;
	// stop if we are done
	//if ( m_isReadDone ) return true;
}

static int32_t s_lastIn  = 0;
static int32_t s_lastOut = 0;

bool SpiderColl::isFirstIpInOverflowList ( int32_t firstIp ) {
	if ( ! m_overflowList ) return false;
	if ( firstIp == 0 || firstIp == -1 ) return false;
	if ( firstIp == s_lastIn ) return true;
	if ( firstIp == s_lastOut ) return false;
	for ( int32_t oi = 0 ; ; oi++ ) {
		// stop at end
		if ( ! m_overflowList[oi] ) break;
		// an ip of zero is end of the list
		if ( m_overflowList[oi] == firstIp ) {
			s_lastIn = firstIp;
			return true;
		}
	}
	s_lastOut = firstIp;
	return false;
}

// . ADDS top X winners to m_winnerTree
// . this is ONLY CALLED from evalIpLoop() above
// . scan m_list that we read from spiderdb for m_scanningIp IP
// . set m_bestRequest if an request in m_list is better than what is
//   in m_bestRequest from previous lists for this IP
bool SpiderColl::scanListForWinners ( ) {

	

	// if list is empty why are we here?
	if ( m_list.isEmpty() ) return true;

	// if waitingtree is locked for writing because it is saving or
	// writes were disabled then just bail and let the scan be re-called
	// later
	//
	// MDW: move this up in evalIpLoop() i think
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable )
		return true;

	// int16_tcut
	RdbList *list = &m_list;
	// ensure we point to the top of the list
	list->resetListPtr();

	// get this
	int64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();//Local();
	uint32_t nowGlobal   = nowGlobalMS / 1000;

	//SpiderRequest *winReq      = NULL;
	//int32_t           winPriority = -10;
	//uint64_t       winTimeMS   = 0xffffffffffffffffLL;
	//int32_t           winMaxSpidersPerIp = 9999;
	SpiderReply   *srep        = NULL;
	int64_t      srepUh48;

	// for getting the top MAX_NODES nodes
	//int32_t           tailPriority = -10;
	//uint64_t       tailTimeMS   = 0xffffffffffffffffLL;

	// if we are continuing from another list...
	if ( m_lastReplyValid ) {
		srep     = (SpiderReply *)m_lastReplyBuf;
		srepUh48 = srep->getUrlHash48();
	}

	// sanity, if it was in ufntree it should be on disk then...
	/*
	if ( list->isEmpty() && m_nextKey == m_firstKey && ! m_useTree ) {
		SafeBuf sb;
		sb.safePrintf("startkey=%s,",
			      KEYSTR(&m_nextKey,sizeof(key128_t) ));
		sb.safePrintf("endkey=%s",
			      KEYSTR(&m_endKey,sizeof(key128_t) ));
		// get waiting key info
		int32_t firstIp = m_waitingTreeKey.n0 & 0xffffffff;
		log("spider: strange corruption #1. there was an entry "
		    "in the waiting tree, but spiderdb read was empty. "
		    "%s. deleting waitingtree key firstip=%s",
		    sb.getBufStart(),
		    iptoa(firstIp));
		// delete the exact node #
		m_waitingTree.deleteNode3 ( wn , false );
	}
	*/
	//char *xx=NULL;*xx=0; }

	/*
	// use the ufntree?
	bool useTree = m_useTree;
	// if we are the first read and list is not full do not bother
	// using the tree because its just as fast to scan the little list
	// we got
	if ( m_nextKey == m_firstKey && list->getListSize() < SR_READ_SIZE )
		useTree = false;
	// init ufn tree
	if ( useTree && ! s_ufnTreeSet ) {
		s_ufnTreeSet = true;
		s_ufnTree.set ( 0 , // fixed data size (uh48)
				1000000 , // max num nodes
				true, // balance?
				-1 , // maxmem, none
				false , // own data?
				"ufntree",
				false, // data is ptr! (true?)
				"ufntreedb",
				sizeof(key128_t),
				false,
				false );
	}
	*/

	// show list stats
	if ( g_conf.m_logDebugSpider )
		log("spider: readListFromSpiderdb: got list of size %"INT32" "
		    "for firstip=%s",
		    m_list.m_listSize,iptoa(m_scanningIp));


	// if we don't read minRecSizes worth of data that MUST indicate
	// there is no more data to read. put this theory to the test
	// before we use it to indcate an end of list condition.
	if ( list->getListSize() > 0 && 
	     m_lastScanningIp == m_scanningIp &&
	     m_lastListSize < (int32_t)SR_READ_SIZE &&
	     m_lastListSize >= 0 ) {
		//char *xx=NULL;*xx=0; }
		log("spider: shucks. spiderdb reads not full.");
	}

	m_lastListSize = list->getListSize();
	m_lastScanningIp = m_scanningIp;

	m_totalBytesScanned += list->getListSize();

	if ( list->isEmpty() && g_conf.m_logDebugSpider )
		log("spider: failed to get rec for ip=%s",iptoa(m_scanningIp));

	int32_t firstIp = m_waitingTreeKey.n0 & 0xffffffff;

	//int32_t numNodes = 0;
	//int32_t tailNode = -1;

	key128_t finalKey;
	int32_t recCount = 0;

	// how many spiders currently out for this ip?
	//int32_toutNow=g_spiderLoop.getNumSpidersOutPerIp(m_scanningIp,m_collnum)

	// loop over all serialized spiderdb records in the list
	for ( ; ! list->isExhausted() ; ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// stop coring on empty lists
		if ( list->isEmpty() ) break;
		// get spiderdb rec in its serialized form
		char *rec = list->getCurrentRec();
		// count it
		recCount++;
		// sanity
		gbmemcpy ( (char *)&finalKey , rec , sizeof(key128_t) );
		// skip to next guy
		list->skipCurrentRecord();
		// negative? wtf?
		if ( (rec[0] & 0x01) == 0x00 ) {
			logf(LOG_DEBUG,"spider: got negative spider rec");
			continue;
		}
		// if its a SpiderReply set it for an upcoming requests
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec ) ) {

			// see if this is the most recent one
			SpiderReply *tmp = (SpiderReply *)rec;

			// . MDW: we have to detect corrupt replies up here so
			//   they do not become the winning reply because
			//   their date is in the future!!

			if ( tmp->m_spideredTime > nowGlobal + 1 ) {
				if ( m_cr->m_spiderCorruptCount == 0 ) {
					log("spider: got corrupt time "
					    "spiderReply in "
					    "scan "
					    "uh48=%"INT64" "
					    "httpstatus=%"INT32" "
					    "datasize=%"INT32" "
					    "(cn=%"INT32")",
					    tmp->getUrlHash48(),
					    (int32_t)tmp->m_httpStatus,
					    tmp->m_dataSize,
					    (int32_t)m_collnum);
				}
				m_cr->m_spiderCorruptCount++;
				// don't nuke it just for that...
				//srep = NULL;
				continue;
			}

			// . this is -1 on corruption
			// . i've seen -31757, 21... etc for bad http replies
			//   in the qatest123 doc cache... so turn off for that
			if ( tmp->m_httpStatus >= 1000 ) {
				if ( m_cr->m_spiderCorruptCount == 0 ) {
					log("spider: got corrupt 3 "
					    "spiderReply in "
					    "scan "
					    "uh48=%"INT64" "
					    "httpstatus=%"INT32" "
					    "datasize=%"INT32" "
					    "(cn=%"INT32")",
					    tmp->getUrlHash48(),
					    (int32_t)tmp->m_httpStatus,
					    tmp->m_dataSize,
					    (int32_t)m_collnum);
				}
				m_cr->m_spiderCorruptCount++;
				// don't nuke it just for that...
				//srep = NULL;
				continue;
			}
			// ignore these to fix diffbot's malformed url bug
			if ( tmp->m_errCode == 32880 &&
			     // and is before about dec 18th 2015
			     tmp->m_spideredTime < 1450488447 )
				continue;
			// ignore these to fix diffbot's ebadtitlerec error
			// 'bad cached document'.
			// ignore them so we can respider the urls and
			// the new logic in xmldoc.cpp can ignore them.
			// i fixed xmldoc.cpp to index these status docs.
			if ( tmp->m_errCode == 32792 &&
			     // and is before about dec 22nd 2015
			     tmp->m_spideredTime < 1450897197 )
				continue;
			// bad langid?
			if ( ! getLanguageAbbr (tmp->m_langId) ) {
				log("spider: got corrupt 4 spiderReply in "
				    "scan uh48=%"INT64" "
				    "langid=%"INT32" (cn=%"INT32")",
				    tmp->getUrlHash48(),
				    (int32_t)tmp->m_langId,
				    (int32_t)m_collnum);
				m_cr->m_spiderCorruptCount++;
				//srep = NULL;
				// if ( tmp->getUrlHash48() == 
				//      271713196158770LL )
				// 	log("hey");
				continue;
			}

			// reset reply stats if beginning a new url
			// these don't work because we only store one reply
			// which overwrites any older reply. that's how the 
			// key is. we can change the key to use the timestamp 
			// and not parent docid in makeKey() for spider 
			// replies later.
			// if ( srepUh48 != tmp->getUrlHash48() ) {
			// 	m_numSuccessReplies = 0;
			// 	m_numFailedReplies  = 0;
			// }

			// inc stats
			// these don't work because we only store one reply
			// which overwrites any older reply. that's how the 
			// key is. we can change the key to use the timestamp 
			// and not parent docid in makeKey() for spider 
			// replies later.
			// if ( tmp->m_errCode == 0 ) m_numSuccessReplies++;
			// else                       m_numFailedReplies ++;

			// if we are corrupt, skip us
			if ( tmp->getRecSize() > (int32_t)MAX_SP_REPLY_SIZE )
				continue;
			// if we have a more recent reply already, skip this 
			if ( srep && 
			     srep->getUrlHash48() == tmp->getUrlHash48() &&
			     srep->m_spideredTime >= tmp->m_spideredTime )
				continue;
			// otherwise, assign it
			srep     = tmp;
			srepUh48 = srep->getUrlHash48();
			continue;
		}

		// MDW: this is handled in url filters now just fine.
		// regardless of the spider request, if it has a spider
		// reply for THIS ROUND, and we are doing crawl rounds,
		// then skip it
		// if ( m_cr->m_isCustomCrawl &&
		//      srep &&
		//      srep->m_spideredTime >= m_cr->m_spiderRoundStartTime )
		// 	continue;

		// cast it
		SpiderRequest *sreq = (SpiderRequest *)rec;

		// skip if our twin or another shard should handle it
		if ( ! isAssignedToUs ( sreq->m_firstIp ) )
			continue;

		int64_t uh48 = sreq->getUrlHash48();

		// reset reply stats if beginning a new url
		// these don't work because we only store one reply
		// which overwrites any older reply. that's how the key is.
		// we can change the key to use the timestamp and not
		// parent docid in makeKey() for spider replies later.
		// if ( ! srep ) {
		// 	m_numSuccessReplies = 0;
		// 	m_numFailedReplies  = 0;
		// }

		// . skip if our twin should add it to doledb
		// . waiting tree only has firstIps assigned to us so
		//   this should not be necessary
		//if ( ! isAssignedToUs ( sreq->m_firstIp ) ) continue;
		// null out srep if no match
		if ( srep && srepUh48 != uh48 ) srep = NULL;
		// if we are doing parser test, ignore all but initially
		// injected requests. NEVER DOLE OUT non-injected urls
		// when doing parser test
		if ( g_conf.m_testParserEnabled ) {
			// skip if already did it
			if ( srep ) continue;
			// skip if not injected
			if ( ! sreq->m_isInjecting ) {
				if ( g_conf.m_logDebugSpider )
					log("spider: skipping8 %s", 
					    sreq->m_url);
				continue;
			}
		}

		// . ignore docid-based requests if spidered the url afterwards
		// . these are one-hit wonders
		// . once done they can be deleted
		if ( sreq->m_isPageReindex && // urlIsDocId &&
		     srep && 
		     srep->m_spideredTime > sreq->m_addedTime ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: skipping9 %s", sreq->m_url);
			continue;
		}

		// if a replie-less new url spiderrequest count it
		if ( ! srep && m_lastSreqUh48 != uh48 &&
		     // avoid counting query reindex requests
		     ! sreq->m_fakeFirstIp )
			m_totalNewSpiderRequests++;

		//int32_t  ipdom ( int32_t ip ) { return ip & 0x00ffffff; };
		int32_t cblock = ipdom ( sreq->m_firstIp );

		bool countIt = true;

		// reset page inlink count on url request change
		if ( m_lastSreqUh48 != uh48 ) {
			m_pageNumInlinks = 0;
			m_lastCBlockIp = 0;
		}

		//if ( uh48 != m_lastSreqUh48 )
		//	countIt = false;

		if ( cblock == m_lastCBlockIp )
			countIt = false;

		// do not count manually added spider requests
		if ( (sreq->m_isAddUrl || sreq->m_isInjecting) )
			countIt = false;

		// 20 is good enough
		if ( m_pageNumInlinks >= 20 )
			countIt = false;

		if ( countIt ) {
			int32_t ca;
			for ( ca = 0 ; ca < m_pageNumInlinks ; ca++ ) 
				if ( m_cblocks[ca] == cblock ) break;
			// if found in our list, do not count it, already did
			if ( ca < m_pageNumInlinks )
				countIt = false;
		}

		if ( countIt ) {
			m_cblocks[m_pageNumInlinks] = cblock;
			m_pageNumInlinks++;
			if ( m_pageNumInlinks > 20 ) { char *xx=NULL;*xx=0;}
		}

		// set this now. it does increase with each request. so 
		// initial requests will not see the full # of inlinks.
		sreq->m_pageNumInlinks = (uint8_t)m_pageNumInlinks;

		// put these in the spiderequest in doledb so we can
		// show in the json spider status docs in 
		// XmlDoc::getSpiderStatusDocMetaList2()
		// these don't work because we only store one reply
		// which overwrites any older reply. that's how the 
		// key is. we can change the key to use the timestamp 
		// and not parent docid in makeKey() for spider 
		// replies later.
		// sreq->m_reservedc1 = m_numSuccessReplies;
		// sreq->m_reservedc2 = m_numFailedReplies;
		
		m_lastSreqUh48 = uh48;
		m_lastCBlockIp = cblock;

		// only add firstip if manually added and not fake
 		// if ( uh48 == 272628060426254 )
 		// 	log("spider: got special seed");

// #undef sleep
// 		if ( uh48 == 272628060426254 ) {
// 			log("spider: got special seed");
// 			bool flag = true;
// 		sleepLoop:
// 			sleep(1);
// 			if ( flag ) goto sleepLoop;
// 		}
// #define sleep(a) { char *xx=NULL;*xx=0; }

		//
		// just calculating page counts? if the url filters are based
		// on the # of pages indexed per ip or subdomain/site then
		// we have to maintain a page count table. sitepages.
		//
		if ( m_countingPagesIndexed ) { //&& sreq->m_fakeFirstIp ) {
			// get request url hash48 (jez= 220459274533043 )
			//int64_t uh48 = sreq->getUrlHash48();
			// do not repeatedly page count if we just have
			// a single fake firstip request. this just adds
			// an entry to the table that will end up in
			// m_pageCountTable so we avoid doing this count
			// again over and over. also gives url filters
			// table a zero-entry...
			//m_localTable.addScore(&sreq->m_firstIp,0);
			//m_localTable.addScore(&sreq->m_siteHash32,0);
			//m_localTable.addScore(&sreq->m_domHash32,0);
			// only add dom/site hash seeds if it is
			// a fake firstIp to avoid double counting seeds
			if ( sreq->m_fakeFirstIp ) continue;
			// count the manual additions separately. mangle their
			// hash with 0x123456 so they are separate.
			if ( (sreq->m_isAddUrl || sreq->m_isInjecting) &&
			     // unique votes per seed
			     uh48 != m_lastReqUh48a ) {
				// do not repeat count the same url
				m_lastReqUh48a = uh48;
				// sanity
				if ( ! sreq->m_siteHash32){char*xx=NULL;*xx=0;}
				if ( ! sreq->m_domHash32){char*xx=NULL;*xx=0;}
				// do a little magic because we count
				// seeds as "manual adds" as well as normal pg
				int32_t h32;
				h32 = sreq->m_siteHash32 ^ 0x123456;
				m_localTable.addScore(&h32);
				h32 = sreq->m_domHash32 ^ 0x123456;
				m_localTable.addScore(&h32);
			}
			// unique votes per other for quota
			if ( uh48 == m_lastReqUh48b ) continue;
			// update this to ensure unique voting
			m_lastReqUh48b = uh48;
			// now count pages indexed below here
			if ( ! srep ) continue;
			if ( srepUh48 == m_lastRepUh48 ) continue;
			m_lastRepUh48 = srepUh48;
			//if ( ! srep ) continue;
			// TODO: what is srep->m_isIndexedINValid is set????
			if ( ! srep->m_isIndexed ) continue;
			// keep count per site and firstip
			m_localTable.addScore(&sreq->m_firstIp,1);
			m_localTable.addScore(&sreq->m_siteHash32,1);
			m_localTable.addScore(&sreq->m_domHash32,1);
			// debug
			if ( ! g_conf.m_logDebugSpider ) continue;
			int32_t *valPtr ;
			int32_t fip = sreq->m_firstIp;
			int32_t sh32 = sreq->m_siteHash32;
			valPtr = (int32_t *)m_localTable.getValue(&sh32);
			log("spider: sitequota: "
			    "got %"INT32" indexed docs for site from "
			    "firstip of %s from url %s",
			    *valPtr,iptoa(fip),sreq->m_url);
			continue;
		}


		// if the spiderrequest has a fake firstip that means it
		// was injected without doing a proper ip lookup for speed.
		// xmldoc.cpp will check for m_fakeFirstIp and it that is
		// set in the spiderrequest it will simply add a new request
		// with the correct firstip. it will be a completely different
		// spiderrequest key then. so no need to keep the "fakes".
		// it will log the EFAKEFIRSTIP error msg.
		if ( sreq->m_fakeFirstIp &&
		     srep && 
		     srep->m_spideredTime > sreq->m_addedTime ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: skipping6 %s", sreq->m_url);
			continue;
		}

		// once we have a spiderreply, even i guess if its an error,
		// for a url, then bail if respidering is disabled
		if ( m_cr->m_isCustomCrawl && 
		     srep && 

		     // no! for bulk jobs and crawl jobs we ALWAYS retry
		     // on errors... tmp errors, but this is just a int16_tcut
		     // so only take this int16_tcut if there is no error
		     // and repeat is 0.0.
		     // ... CRAP we do not want error'ed urls to resuscitate
		     // and job that is not already in progress if it is not
		     // supposed to be a repeat crawl.
		     ( srep->m_errCode && 
		       // BUT skip this url if the job is not in progress
		       // even if the errCode is NON-zero, THUS we prevent
		       // a job from flip flopping from in progress to
		       // not in progress and sending out alerts. so once
		       // it goes to NOT in progress, that's it...
		       m_cr->m_spiderStatus != SP_INPROGRESS ) &&
		     // this means "repeat" is set to 0, to not repeat but
		     // if we get a &roundStart=1 request we do a round anyway.
		     m_cr->m_collectiveRespiderFrequency <= 0.0 ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: skipping0 %s",sreq->m_url);
			continue;
		}

		// sanity check. check for http(s)://
		if ( ( sreq->m_url[0] != 'h' ||
		       sreq->m_url[1] != 't' ||
		       sreq->m_url[2] != 't' ||
		       sreq->m_url[3] != 'p' ) &&
		     // might be a docid from a pagereindex.cpp
		     ! is_digit(sreq->m_url[0]) ) { 
			if ( m_cr->m_spiderCorruptCount == 0 )
				log("spider: got corrupt 1 spiderRequest in "
				    "scan because url is %s (cn=%"INT32")"
				    ,sreq->m_url,(int32_t)m_collnum);
			m_cr->m_spiderCorruptCount++;
			continue;
		}
		if ( sreq->m_dataSize > (int32_t)sizeof(SpiderRequest) ||
		     sreq->m_dataSize < 0 ) {
			if ( m_cr->m_spiderCorruptCount == 0 )
				log("spider: got corrupt 11 spiderRequest in "
				    "scan because size=%i u=%s (cn=%"INT32")"
				    ,(int)sreq->m_dataSize
				    ,sreq->m_url,(int32_t)m_collnum);
			m_cr->m_spiderCorruptCount++;
			continue;
		}
		int32_t delta = sreq->m_addedTime - nowGlobal;
		if ( delta > 86400 ) {
			static bool s_first = true;
			if ( m_cr->m_spiderCorruptCount == 0 || s_first ) {
				s_first = false;
				log("spider: got corrupt 6 spiderRequest in "
				    "scan because added time is %"INT32" "
				    "(delta=%"INT32" "
				    "which is well into the future. url=%s "
				    "(cn=%i)"
				    ,(int32_t)sreq->m_addedTime
				    ,delta
				    ,sreq->m_url
				    ,(int)m_collnum);
			}
			m_cr->m_spiderCorruptCount++;
			continue;
		}


		// update SpiderRequest::m_siteNumInlinks to most recent value
		int32_t sni = sreq->m_siteNumInlinks;
		// get the # of inlinks to the site from our table
		uint64_t *val;
		val = (uint64_t *)m_sniTable.getValue32(sreq->m_siteHash32);
		// use the most recent sni from this table
		if ( val ) 
			sni = (int32_t)((*val)>>32);
		// if SpiderRequest is forced then m_siteHash32 is 0!
		else if ( srep && srep->m_spideredTime >= sreq->m_addedTime ) 
			sni = srep->m_siteNumInlinks;
		// assign
		sreq->m_siteNumInlinks = sni;
		// store rror count in request so xmldoc knows what it is
		// and can increment it and re-add it to its spiderreply if
		// it gets another error
		if ( srep ) {
			sreq->m_errCount = srep->m_errCount;
			// . assign this too from latest reply - smart compress
			// . this WAS SpiderReply::m_pubdate so it might be
			//   set to a non-zero value that is wrong now... but
			//   not a big deal!
			sreq->m_contentHash32 = srep->m_contentHash32;
			// if we tried it before
			sreq->m_hadReply = true;
		}

		// . get the url filter we match
		// . if this is slow see the TODO below in dedupSpiderdbList()
		//   which can pre-store these values assuming url filters do
		//   not change and siteNumInlinks is about the same.
		int32_t ufn = ::getUrlFilterNum(sreq,
					     srep,
					     nowGlobal,
					     false,
					     MAX_NICENESS,
					     m_cr,
					     false, // isOutlink?
					     // provide the page quota table
						&m_localTable,
						-1);
		// sanity check
		if ( ufn == -1 ) { 
			log("spider: failed to match url filter for "
			    "url = %s coll=%s", sreq->m_url,m_cr->m_coll);
			g_errno = EBADENGINEER;
			return true;
		}
		// set the priority (might be the same as old)
		int32_t priority = m_cr->m_spiderPriorities[ufn];
		// now get rid of negative priorities since we added a
		// separate force delete checkbox in the url filters
		if ( priority < 0 ) priority = 0;
		// sanity checks
		//if ( priority == -1 ) { char *xx=NULL;*xx=0; }
		if ( priority >= MAX_SPIDER_PRIORITIES) {char *xx=NULL;*xx=0;}

		if ( g_conf.m_logDebugSpider )
			log("spider: got ufn=%"INT32" for %s (%"INT64"",
			    ufn,sreq->m_url,sreq->getUrlHash48());

		if ( g_conf.m_logDebugSpider && srep )
			log("spider: lastspidered=%"UINT32"",
			    srep->m_spideredTime);

		
		// spiders disabled for this row in url filteres?
		//if ( ! m_cr->m_spidersEnabled[ufn] ) continue;
		if ( m_cr->m_maxSpidersPerRule[ufn] <= 0 ) continue;

		// skip if banned (unless need to delete from index)
		bool skip = false;
		// if ( priority == SPIDER_PRIORITY_FILTERED ) skip = true;
		// if ( priority == SPIDER_PRIORITY_BANNED   ) skip = true;
		if ( m_cr->m_forceDelete[ufn] ) skip = true;
		// but if it is currently indexed we have to delete it
		if ( skip && srep && srep->m_isIndexed ) skip = false;
		if ( skip ) continue;

		// temp debug
		//char *xx=NULL;*xx=0;

		if ( m_cr->m_forceDelete[ufn] )
			// force it to a delete
			sreq->m_forceDelete = true;

		int64_t spiderTimeMS;
		spiderTimeMS = getSpiderTimeMS ( sreq,ufn,srep,nowGlobalMS );
		// how many outstanding spiders on a single IP?
		//int32_t maxSpidersPerIp = m_cr->m_spiderIpMaxSpiders[ufn];
		// sanity
		if ( (int64_t)spiderTimeMS < 0 ) { 
			log("spider: got corrupt 2 spiderRequest in "
			    "scan (cn=%"INT32")",
			    (int32_t)m_collnum);
			continue;
		}
		// more corruption detection
		if ( sreq->m_hopCount < -1 ) {
			log("spider: got corrupt 5 spiderRequest in "
			    "scan (cn=%"INT32")",
			    (int32_t)m_collnum);
			continue;
		}
		// 100 days out is corruption. ppl sometimes put
		// 3000 days to re-spider... so take this out
		/*
		int64_t delta2 = spiderTimeMS - nowGlobalMS;
		if ( delta2 > 86400LL*1000LL*100LL ) {
			log("spider: got corrupt 7 spiderRequest in "
			    "scan (cn=%"INT32") (delta=%"INT64") url=%s",
			    (int32_t)m_collnum,
			    delta2,
			    sreq->m_url);
			SafeBuf sb; g_spiderdb.print ( (char *)sreq , &sb );
			if ( srep ) g_spiderdb.print ( (char *)srep , &sb );
			log("spider: %s",sb.getBufStart());
			continue;
		}
		*/

		// save this shit for storing in doledb
		sreq->m_ufn = ufn;
		sreq->m_priority = priority;

		// if it is in future, skip it and just set m_futureTime and
		// and we will update the waiting tree
		// with an entry based on that future time if the winnerTree 
		// turns out to be empty after we've completed our scan
		if ( spiderTimeMS > nowGlobalMS ) {
			// if futuretime is zero set it to this time
			if ( ! m_minFutureTimeMS ) 
				m_minFutureTimeMS = spiderTimeMS;
			// otherwise we get the MIN of all future times
			else if ( spiderTimeMS < m_minFutureTimeMS )
				m_minFutureTimeMS = spiderTimeMS;
			if ( g_conf.m_logDebugSpider )
				log("spider: skippingx %s",sreq->m_url);
			continue;
		}

		// debug point
		// if ( ((long long)srep->m_spideredTime)*1000LL > 
		//      nowGlobalMS - 86400LL*1000LL*30LL )
		// 	log("spider: should not be spidering this!");

		//////
		//
		// MDW: no, take this out now that we allow multiple urls
		// in doledb from this firstip for speeding up the spiders.
		// the crawldelay will be used by Msg13.cpp when it tries
		// to download the url.
		//
		//////
		/*

		// how many "ready" urls for this IP? urls in doledb
		// can be spidered right now
		int32_t *score ;
		score = (int32_t *)m_doleIpTable.getValue32 ( sreq->m_firstIp );
		// how many spiders are current outstanding
		int32_t out2 = outNow;
		// add in any requests in doledb
		if ( score ) out2 += *score;

		// . do not add any more to doledb if we could violate ourquota
		// . shit we have to add it our it never gets in
		// . try regulating in msg13.cpp download code. just queue
		//   up requests to avoid hammering there.
		if ( out2 >= maxSpidersPerIp ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: skipping1 %s",sreq->m_url);
			continue;
		}

		// by ensuring only one spider out at a time when there
		// is a positive crawl-delay, we ensure that m_lastDownloadTime
		// is the last time we downloaded from this ip so that we
		// can accurately set the time in getSpiderTimeMS() for
		// when the next url from this firstip should be spidered.
		if ( out2 >= 1 ) {
			// get the crawldelay for this domain
			int32_t *cdp ;
			cdp = (int32_t *)m_cdTable.getValue (&sreq->m_domHash32);
			// if crawl delay is NULL, we need to download
			// robots.txt. most of the time it will be -1
			// which indicates not specified in robots.txt
			if ( ! cdp ) {
				if ( g_conf.m_logDebugSpider )
					log("spider: skipping2 %s",
					    sreq->m_url);
				continue;
			}
			// if we had a positive crawldelay and there is
			// already >= 1 outstanding spider on this ip, 
			// then skip this url
			if ( cdp && *cdp > 0 ) {
				if ( g_conf.m_logDebugSpider )
					log("spider: skipping3 %s",
					    sreq->m_url);
				continue;
			}
		}
		*/

		// debug. show candidates due to be spidered now.
		//if(g_conf.m_logDebugSpider ) //&& spiderTimeMS< nowGlobalMS )
		//	log("spider: considering ip=%s sreq spiderTimeMS=%"INT64" "
		//	    "pri=%"INT32" uh48=%"INT64"",
		//	    iptoa(sreq->m_firstIp),
		//	    spiderTimeMS,
		//	    priority,
		//	    sreq->getUrlHash48());


		// we can't have negative priorities at this point because
		// the s_ufnTree uses priority as part of the key so it
		// can get the top 100 or so urls for a firstip to avoid
		// having to hit spiderdb for every one!
		if ( priority < 0 ) { char *xx=NULL;*xx=0; }

		//
		// NO! then just a single root url can prevent all his
		// kids from getting spidered. because this logic was
		// priority based over time. so while the high priority url
		// would be sitting in the waiting tree, the kids whose
		// time it was to be spidered would be starving for attention.
		// only use priority if the high priority url can be spidered
		// now, so he doesn't lock the others out of the waiting tree.
		//
		// now pick the SpiderRequest with the best priority, then
		// break ties with the "spiderTime".
		//if ( priority <  winPriority ) 
		//	continue;
		// if tied, use times
		//if ( priority == winPriority && spiderTimeMS > winTimeMS ) 
		//	continue;

		// bail if it is locked! we now call 
		// msg12::confirmLockAcquisition() after we get the lock,
		// which deletes the doledb record from doledb and doleiptable
		// rightaway and adds a "0" entry into the waiting tree so
		// that evalIpLoop() repopulates doledb again with that
		// "firstIp". this way we can spider multiple urls from the
		// same ip at the same time.
		int64_t key = makeLockTableKey ( sreq );

		if ( g_conf.m_logDebugSpider )
			log("spider: checking uh48=%"INT64" lockkey=%"INT64" "
			    "used=%"INT32"",
			    uh48,key,
			    g_spiderLoop.m_lockTable.getNumUsedSlots());

		// MDW
		if ( g_spiderLoop.m_lockTable.isInTable ( &key ) ) {
			// get it
			//CrawlInfo *ci = &m_cr->m_localCrawlInfo;
			// do not think the round is over!
			//ci->m_lastSpiderCouldLaunch = nowGlobal;
			// there are urls ready to spider, just locked up
			//ci->m_hasUrlsReadyToSpider = true;
			// debug note
			if ( g_conf.m_logDebugSpider )
				log("spider: skipping url lockkey=%"INT64" in "
				    "lock table sreq.url=%s",key,
				    sreq->m_url);
			continue;
		}

		//int64_t uh48 = sreq->getUrlHash48();

		// make key
		key192_t wk = makeWinnerTreeKey( firstIp ,
						 priority ,
						 sreq->m_hopCount,
						 spiderTimeMS ,
						 uh48 );

		// assume our added time is the first time this url was added
		sreq->m_discoveryTime = sreq->m_addedTime;

		// if ( uh48 == 110582802025376LL )
		// 	log("hey");

		// if this url is already in the winnerTree then either we 
		// replace it or we skip ourselves. 
		//
		// watch out for dups in winner tree, the same url can have 
		// multiple spiderTimeMses somehow... i guess it could have 
		// different hop counts
		// as well, resulting in different priorities...
		// actually the dedup table could map to a priority and a node
		// so we can kick out a lower priority version of the same url.
		int32_t winSlot = m_winnerTable.getSlot ( &uh48 );
		if ( winSlot >= 0 ) {
			key192_t *oldwk = (key192_t *)m_winnerTable.
				getDataFromSlot ( winSlot );

			// get the min hopcount  
			SpiderRequest *wsreq ;
			wsreq =(SpiderRequest *)m_winnerTree.
				getData(0,(char *)oldwk);
			if ( wsreq ) {
				if ( sreq->m_hopCount < wsreq->m_hopCount )
					wsreq->m_hopCount = sreq->m_hopCount;
				if ( wsreq->m_hopCount < sreq->m_hopCount )
					sreq->m_hopCount = wsreq->m_hopCount;
				// and the min added time as well!
				// get the oldest timestamp so
				// gbssDiscoveryTime will be accurate.
				if ( sreq->m_discoveryTime < wsreq->m_discoveryTime )
					wsreq->m_discoveryTime = 
						sreq->m_discoveryTime;
				if ( wsreq->m_discoveryTime < sreq->m_discoveryTime )
					sreq->m_discoveryTime = 
						wsreq->m_discoveryTime;
			}

			

			// are we lower priority? (or equal)
			// smaller keys are HIGHER priority.
			if(KEYCMP((char *)&wk,(char *)oldwk,
				  sizeof(key192_t))>=0) 
				continue;
			// from table too. no it's a dup uh48!
			//m_winnerTable.deleteKey ( &uh48 );
			// otherwise we supplant it. remove old key from tree.
			m_winnerTree.deleteNode ( 0 , (char *)oldwk , false);
			// supplant in table and tree... just add below...
		}

		// get the top 100 spider requests by priority/time/etc.
		int32_t maxWinners = (int32_t)MAX_WINNER_NODES; // 40
		//if ( ! m_cr->m_isCustomCrawl ) maxWinners = 1;

		// if less than 10MB of spiderdb requests limit to 400
		if ( m_totalBytesScanned < 10000000 ) maxWinners = 400;

		// only put one doledb record into winner tree if
		// the list is pretty short. otherwise, we end up caching
		// too much. granted, we only cache for about 2 mins.
		// mdw: for testing take this out!
		if ( m_totalBytesScanned < 25000 ) maxWinners = 1;

		// sanity. make sure read is somewhat hefty for our 
		// maxWinners=1 thing
		if ( (int32_t)SR_READ_SIZE < 500000 ) { char *xx=NULL;*xx=0; }

		// only compare to min winner in tree if tree is full
		if ( m_winnerTree.getNumUsedNodes() >= maxWinners ) {
			// get that key
			int64_t tm1 = spiderTimeMS;
			// get the spider time of lowest scoring req in tree
			int64_t tm2 = m_tailTimeMS;
			// if they are both overdue, make them the same
			if ( tm1 < nowGlobalMS ) tm1 = 1;
			if ( tm2 < nowGlobalMS ) tm2 = 1;
			// skip spider request if its time is past winner's
			if ( tm1 > tm2 )
				continue;
			if ( tm1 < tm2 )
				goto gotNewWinner;
			// if tied, use priority
			if ( priority < m_tailPriority )
				continue;
			if ( priority > m_tailPriority )
				goto gotNewWinner;
			// if tied use hop counts so we are breadth first
			if ( sreq->m_hopCount > m_tailHopCount )
				continue;
			if ( sreq->m_hopCount < m_tailHopCount )
				goto gotNewWinner;
			// if hopcounts tied prefer the unindexed doc
			// i don't think we need this b/c spidertimems
			// for new docs should be less than old docs...
			// TODO: verify that
			//if ( sreq->m_isIndexed && ! m_tailIsIndexed )
			//	continue;
			//if ( ! sreq->m_isIndexed && m_tailIsIndexed )
			//	goto gotNewWinner;
			// if tied, use actual times. assuming both<nowGlobalMS
			if ( spiderTimeMS > m_tailTimeMS )
				continue;
			if ( spiderTimeMS < m_tailTimeMS )
				goto gotNewWinner;
			// all tied, keep it the same i guess
			continue;
			// otherwise, add the new winner in and remove the old
		gotNewWinner:
			// get lowest scoring node in tree
			int32_t tailNode = m_winnerTree.getLastNode();
			// from table too
			m_winnerTable.removeKey ( &m_tailUh48 );
			// delete the tail so new spiderrequest can enter
			m_winnerTree.deleteNode3 ( tailNode , true );

		}

		// somestimes the firstip in its key does not match the
		// firstip in the record!
		if ( sreq->m_firstIp != firstIp ) {
			log("spider: request %s firstip does not match "
			    "firstip in key collnum=%i",sreq->m_url,
			    (int)m_collnum);
			log("spider: ip1=%s",iptoa(sreq->m_firstIp));
			log("spider: ip2=%s",iptoa(firstIp));
			continue;
		}

		////
		//
		// add spider request to winner tree
		//
		////

		/*
		// make the key
		//if ( useTree ) {
		int64_t uh48 = sreq->getUrlHash48();
		key128_t k = makeUfnTreeKey ( firstIp ,priority,
					      spiderTimeMS , uh48 );
		//int32_t nn =;
		m_winnerTree.addNode(0,(char *)&k,NULL,8);
		//log("adding node #%"INT32" firstip=%s uh48=%"UINT64" "
		//    "ufntree.k.n1=0x%"XINT64" "
		//    "spiderdb.k.n1=0x%"XINT64" "
		//    "spiderdb.k.n0=0x%"XINT64" "
		//    ,
		//    nn,iptoa(firstIp),uh48,k.n1,
		//    *(int64_t *)rec,
		//    *(int64_t *)(rec+8)
		//   );
		//numNodes++;
		//}
		*/

		// . add to table which allows us to ensure same url not 
		//   repeated in tree
		// . just skip if fail to add...
		if ( m_winnerTable.addKey ( &uh48 , &wk ) < 0 ) continue;

		// use an individually allocated buffer for each spiderrequest
		// so if it gets removed from tree the memory can be freed by 
		// the tree which "owns" the data because m_winnerTree.set() 
		// above set ownsData
		// to true above.
		int32_t need = sreq->getRecSize();
		char *newMem = (char *)mdup ( sreq , need , "sreqbuf" );
		if ( ! newMem ) continue;

		// add it to the tree of the top urls to spider
		m_winnerTree.addNode( 0, 
				      (char *)&wk ,
				      (char *)newMem ,
				      need );

		// log("adding wk uh48=%llu #usednodes=%i",
		//     uh48,m_winnerTree.m_numUsedNodes);

		// sanity
		//SpiderRequest *sreq2 = (SpiderRequest *)m_winnerTree.
		//getData ( nn );

		// //////////////////////
		// // MDW dedup test
		// HashTableX dedup;
		// int32_t ntn = m_winnerTree.getNumNodes();
		// char dbuf[3*MAX_WINNER_NODES*(8+1)];
		// dedup.set ( 8,
		// 	    0,
		// 	    (int32_t)2*ntn, // # slots to initialize to
		// 	    dbuf,
		// 	    (int32_t)(3*MAX_WINNER_NODES*(8+1)),
		// 	    false,
		// 	    MAX_NICENESS,
		// 	    "windt");
		// for ( int32_t node = m_winnerTree.getFirstNode() ; 
		//       node >= 0 ; 
		//       node = m_winnerTree.getNextNode ( node ) ) {
		// // get data for that
		// SpiderRequest *sreq2;
		// sreq2 = (SpiderRequest *)m_winnerTree.getData ( node );
		// // parse it up
		// int32_t winIp;
		// int32_t winPriority;
		// int32_t winHopCount;
		// int64_t winSpiderTimeMS;
		// int64_t winUh48;
		// key192_t *winKey = (key192_t *)m_winnerTree.getKey ( node );
		// parseWinnerTreeKey ( winKey ,
		// 		     &winIp ,
		// 		     &winPriority,
		// 		     &winHopCount,
		// 		     &winSpiderTimeMS ,
		// 		     &winUh48 );
		// // sanity
		//if(winUh48 != sreq2->getUrlHash48() ) { char *xx=NULL;*xx=0;}
		// // make the doledb key
		// key_t doleKey = g_doledb.makeKey ( winPriority,
		// 				   // convert to secs from ms
		// 				   winSpiderTimeMS / 1000     ,
		// 				   winUh48 ,
		// 				   false                    );
		// // dedup. if we add dups the problem is is that they
		// // overwrite the key in doledb yet the doleiptable count
		// // remains undecremented and doledb is empty and never
		// // replenished because the firstip can not be added to
		// // waitingTree because doleiptable count is > 0. this was
		// // causing spiders to hang for collections. i am not sure
		// // why we should be getting dups in winnertree because they
		// // have the same uh48 and that is the key in the tree.
		// if ( dedup.isInTable ( &winUh48 ) ) {
		// 	log("spider: got dup uh48=%"UINT64" dammit", winUh48);
		// 	char *xx=NULL;*xx=0;
		// 	continue;
		// }
		// // do not allow dups
		// dedup.addKey ( &winUh48 );
		// }
		// // end dedup test
		//////////////////////////

		// set new tail priority and time for next compare
		if ( m_winnerTree.getNumUsedNodes() >= maxWinners ) {
			// for the worst node in the tree...
			int32_t tailNode = m_winnerTree.getLastNode();
			if ( tailNode < 0 ) { char *xx=NULL;*xx=0; }
			// set new tail parms
			key192_t *tailKey;
			tailKey = (key192_t *)m_winnerTree.getKey ( tailNode );
			// convert to char first then to signed int32_t
			parseWinnerTreeKey ( tailKey ,
					     &m_tailIp ,
					     &m_tailPriority,
					     &m_tailHopCount,
					     &m_tailTimeMS ,
					     &m_tailUh48 );
			// sanity
			if ( m_tailIp != firstIp ) { char *xx=NULL;*xx=0;}
		}

		/*
		// ok, we got a new winner
		winPriority = priority;
		winTimeMS   = spiderTimeMS;
		winMaxSpidersPerIp = maxSpidersPerIp;
		winReq      = sreq;
		// set these for doledb
		winReq->m_priority   = priority;
		winReq->m_ufn        = ufn;
		//winReq->m_spiderTime = spiderTime;
		*/
	}

	/*

	  MDW: now that we have winnerTree obsolete this stuff:

	// if its ready to spider now, that trumps one in the future always!
	if ( winReq &&
	     m_bestRequestValid &&
	     m_bestSpiderTimeMS <= nowGlobalMS &&
	     winTimeMS > nowGlobal )
		winReq = NULL;

	// if this is a successive call we have to beat the global because
	// the firstIp has a *ton* of spider requests and we can't read them
	// all in one list, then see if we beat our global winner!
	if ( winReq && 
	     m_bestRequestValid &&
	     m_bestSpiderTimeMS <= nowGlobalMS &&
	     m_bestRequest->m_priority > winPriority )
		winReq = NULL;

	// or if both in future. use time.
	if ( winReq && 
	     m_bestRequestValid &&
	     m_bestSpiderTimeMS > nowGlobalMS &&
	     winTimeMS > nowGlobal &&
	     m_bestSpiderTimeMS < winTimeMS )
		winReq = NULL;

	// if both recs are overdue for spidering and priorities tied, use
	// the hopcount. should make us breadth-first, all else being equal.
	if ( winReq && 
	     m_bestRequestValid &&
	     m_bestRequest->m_priority == winPriority &&
	     m_bestSpiderTimeMS <= nowGlobalMS &&
	     winTimeMS <= nowGlobal &&
	     m_bestRequest->m_hopCount < winReq->m_hopCount )
		winReq = NULL;
		
	// use times if hops are equal and both are overdue from same priority.
	if ( winReq && 
	     m_bestRequestValid &&
	     m_bestRequest->m_priority == winPriority &&
	     m_bestSpiderTimeMS <= nowGlobalMS &&
	     winTimeMS <= nowGlobal &&
	     m_bestRequest->m_hopCount == winReq->m_hopCount &&
	     m_bestSpiderTimeMS <= winTimeMS )
		winReq = NULL;

	// if nothing, we are done!
	if ( winReq ) {
		// store this
		int32_t rsize = winReq->getRecSize();
		// sanity check
		if ( rsize > (int32_t)MAX_BEST_REQUEST_SIZE){char *xx=NULL;*xx=0;}
		// now store this SpiderRequest for adding to doledb
		gbmemcpy ( m_bestRequestBuf , winReq, rsize );
		// point to that
		m_bestRequest = (SpiderRequest *)m_bestRequestBuf;
		// set this
		m_bestRequestValid = true;
		// this too
		m_bestSpiderTimeMS = winTimeMS;
		m_bestMaxSpidersPerIp = winMaxSpidersPerIp;
		// sanity
		if ( (int64_t)winTimeMS < 0 ) { char *xx=NULL;*xx=0; }
		// note it
		if ( g_conf.m_logDebugSpider ) {
			log("spider: found winning request IN THIS LIST ip=%s "
			    "spiderTimeMS=%"INT64" "
			    "ufn=%"INT32" "
			    "hadreply=%"INT32" "
			    "pri=%"INT32" uh48=%"INT64" url=%s",
			    iptoa(m_bestRequest->m_firstIp),
			    m_bestSpiderTimeMS,
			    (int32_t)m_bestRequest->m_ufn,
			    (int32_t)m_bestRequest->m_hadReply,
			    (int32_t)m_bestRequest->m_priority,
			    m_bestRequest->getUrlHash48(),
			    m_bestRequest->m_url);
			// debug why not sticking to our site!
			//if ( strstr(m_bestRequest->m_url,"//www.jezebelgallery.com") == NULL ) { char *xx=NULL;*xx=0; }
		}
	}
	else if ( g_conf.m_logDebugSpider ) {
		log("spider: did not find winning request for %s but "
		    "bestReqValid=%"INT32"",
		    iptoa(m_scanningIp),(int32_t)m_bestRequestValid);
	}
	*/

	// are we the final list in the scan?
	//m_isReadDone = ( list->getListSize() < (int32_t)SR_READ_SIZE ) ;

	//
	// . try to fix the bug of reading like only 150k when we asked for 512
	// . that bug was because of dedupList() function
	//
	//if ( list->isEmpty() )
	//	m_isReadDone = true;

	// if no spiderreply for the current url, invalidate this
	m_lastReplyValid = false;
	// if read is not yet done, save the reply in case next list needs it
	if ( srep ) { // && ! m_isReadDone ) {
		int32_t rsize = srep->getRecSize();
		if ( rsize > (int32_t)MAX_SP_REPLY_SIZE){char *xx=NULL;*xx=0; }
		gbmemcpy ( m_lastReplyBuf, srep, rsize );
		m_lastReplyValid = true;
	}

	// debug info
	if ( g_conf.m_logDebugSpider )
		log("spider: Checked list of %"INT32" spiderdb "
		    "bytes (%"INT32" recs) "
		    "for winners "
		    "for firstip=%s. winnerTreeUsedNodes=%"INT32" #newreqs=%"
		    INT64
		    ,list->getListSize(),recCount,iptoa(m_scanningIp),
		    m_winnerTree.getNumUsedNodes(),
		    m_totalNewSpiderRequests);
	// reset any errno cuz we're just a cache
	g_errno = 0;


	/////
	//
	// BEGIN maintain firstip overflow list
	//
	/////
	bool overflow = false;
	// don't add any more outlinks to this firstip after we
	// have 10M spider requests for it.
	// lower for testing
	//if ( m_totalNewSpiderRequests > 1 )
	if ( m_totalNewSpiderRequests > 10000000 )
		overflow = true;

	// need space
	if ( overflow && ! m_overflowList ) {
		int32_t need = OVERFLOWLISTSIZE*4;
		m_overflowList = (int32_t *)mmalloc(need,"list");
		m_overflowList[0] = 0;
	}
	//
	// ensure firstip is in the overflow list if we overflowed
	int32_t emptySlot = -1;
	bool found = false;
	int32_t oi;
	// if we dealt with this last round, we're done
	if ( m_lastOverflowFirstIp == firstIp )
		return true;
	m_lastOverflowFirstIp = firstIp;
	if ( overflow && g_conf.m_logDebugSpider )
		log("spider: firstip %s overflowing with %"INT32" new reqs",
		    iptoa(firstIp),(int32_t)m_totalNewSpiderRequests);
	for ( oi = 0 ; ; oi++ ) {
		// sanity
		if ( ! m_overflowList ) break;
		// an ip of zero is end of the list
		if ( ! m_overflowList[oi] ) break;
		// if already in there, we are done
		if ( m_overflowList[oi] == firstIp ) {
			found = true;
			break;
		}
		// -1 means empty slot
		if ( m_overflowList[oi] == -1 ) emptySlot = oi;
	}
	// if we need to add it...
	if ( overflow && ! found && m_overflowList ) {
		log("spider: adding %s to overflow list",iptoa(firstIp));
		// reset this little cache thingy
		s_lastOut = 0;
		// take the empty slot if there is one
		if ( emptySlot >= 0 )
			m_overflowList[emptySlot] = firstIp;
		// or add to new slot. this is #defined to 200 last check
		else if ( oi+1 < OVERFLOWLISTSIZE ) {
			m_overflowList[oi] = firstIp;
			m_overflowList[oi+1] = 0;
		}
		else 
			log("spider: could not add firstip %s to "
			    "overflow list, full.", iptoa(firstIp));
	}
	// ensure firstip is NOT in the overflow list if we are ok
	for ( int32_t oi2 = 0 ; ! overflow ; oi2++ ) {
		// sanity
		if ( ! m_overflowList ) break;
		// an ip of zero is end of the list
		if ( ! m_overflowList[oi2] ) break;
		// skip if not a match
		if ( m_overflowList[oi2] != firstIp ) continue;
		// take it out of list
		m_overflowList[oi2] = -1;
		log("spider: removing %s from overflow list",iptoa(firstIp));
		// reset this little cache thingy
		s_lastIn = 0;
		break;
	}
	/////
	//
	// END maintain firstip overflow list
	//
	/////
		



	// ok we've updated m_bestRequest!!!
	return true;
}


// . this is ONLY CALLED from evalIpLoop() above
// . add another 0 entry into waiting tree, unless we had no winner
// . add winner in here into doledb
// . returns false if blocked and doledWrapper() will be called
// . returns true and sets g_errno on error
bool SpiderColl::addWinnersIntoDoledb ( ) {

	if ( g_errno ) {
		log("spider: got error when trying to add winner to doledb: "
		    "%s",mstrerror(g_errno));
		return true;
	}

	// gotta check this again since we might have done a QUICKPOLL() above
	// to call g_process.shutdown() so now tree might be unwritable
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable )
		return true;

	/*
	  MDW: let's print out recs we add to doledb
	//if ( g_conf.m_logDebugSpider && m_bestRequestValid ) {
	if ( g_conf.m_logDebugSpider && m_bestRequestValid ) {
		log("spider: got best ip=%s sreq spiderTimeMS=%"INT64" "
		    "pri=%"INT32" uh48=%"INT64"",
		    iptoa(m_bestRequest->m_firstIp),
		    m_bestSpiderTimeMS,
		    (int32_t)m_bestRequest->m_priority,
		    m_bestRequest->getUrlHash48());
	}
	else if ( g_conf.m_logDebugSpider ) {
		log("spider: no best request for ip=%s",iptoa(m_scanningIp));
	}
	*/

	// ok, all done if nothing to add to doledb. i guess we were misled
	// that firstIp had something ready for us. maybe the url filters
	// table changed to filter/ban them all. if a new request/reply comes 
	// in for this firstIp then it will re-add an entry to waitingtree and
	// we will re-scan spiderdb. if we had something to spider but it was 
	// in the future the m_minFutureTimeMS will be non-zero, and we deal
	// with that below...
	if ( m_winnerTree.isEmpty() && ! m_minFutureTimeMS ) {
		// if we received new incoming requests while we were
		// scanning, which is happening for some crawls, then do
		// not nuke! just repeat later in populateDoledbFromWaitingTree
		if ( m_gotNewDataForScanningIp ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: received new requests, not "
				    "nuking misleading key");
			return true;
		}
		// note it - this can happen if no more to spider right now!
		if ( g_conf.m_logDebugSpider )
			log("spider: nuking misleading waitingtree key "
			    "firstIp=%s", iptoa(m_scanningIp));
		m_waitingTree.deleteNode ( 0,(char *)&m_waitingTreeKey,true);
		//log("spider: 7 del node for %s",iptoa(m_scanningIp));
		m_waitingTreeKeyValid = false;
		// note it
		uint64_t timestamp64 = m_waitingTreeKey.n1;
		timestamp64 <<= 32;
		timestamp64 |= m_waitingTreeKey.n0 >> 32;
		int32_t firstIp = m_waitingTreeKey.n0 &= 0xffffffff;
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: removed2 time=%"INT64" ip=%s from "
			    "waiting tree. nn=%"INT32".",
			    timestamp64, iptoa(firstIp),
			    m_waitingTree.m_numUsedNodes);

		m_waitingTable.removeKey  ( &firstIp  );
		// sanity check
		if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}
		return true;
	}


	// i've seen this happen, wtf?
	if ( m_winnerTree.isEmpty() && m_minFutureTimeMS ) { 
		// this will update the waiting tree key with minFutureTimeMS
		addDoleBufIntoDoledb ( NULL , false );
		return true;
	}

	// i am seeing dup uh48's in the m_winnerTree
	int32_t firstIp = m_waitingTreeKey.n0 & 0xffffffff;
	//char dbuf[147456];//3*MAX_WINNER_NODES*(8+1)];
	HashTableX dedup;
	int32_t ntn = m_winnerTree.getNumNodes();
	dedup.set ( 8,
		    0,
		    (int32_t)2*ntn, // # slots to initialize to
		    NULL,//dbuf,
		    0,//147456,//(int32_t)(3*MAX_WINNER_NODES*(8+1)),
		    false,
		    MAX_NICENESS,
		    "windt");

	///////////
	//
	// make winner tree into doledb list to add
	//
	///////////
	//m_doleBuf.reset();
	//m_doleBuf.setLabel("dolbuf");
	// first 4 bytes is offset of next doledb record to add to doledb
	// so we do not have to re-add the dolebuf to the cache and make it
	// churn. it is really inefficient.
	SafeBuf doleBuf;
	doleBuf.pushLong(4);
	int32_t added = 0;
	for ( int32_t node = m_winnerTree.getFirstNode() ; 
	      node >= 0 ; 
	      node = m_winnerTree.getNextNode ( node ) ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get data for that
		SpiderRequest *sreq2;
		sreq2 = (SpiderRequest *)m_winnerTree.getData ( node );
		// sanity
		if ( sreq2->m_firstIp != firstIp ) { char *xx=NULL;*xx=0; }
		//if ( sreq2->m_spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }
		if ( sreq2->m_ufn          < 0 ) { char *xx=NULL;*xx=0; }
		if ( sreq2->m_priority ==   -1 ) { char *xx=NULL;*xx=0; }
		// check for errors
		bool hadError = false;
		// parse it up
		int32_t winIp;
		int32_t winPriority;
		int32_t winHopCount;
		int64_t winSpiderTimeMS;
		int64_t winUh48;
		key192_t *winKey = (key192_t *)m_winnerTree.getKey ( node );
		parseWinnerTreeKey ( winKey ,
				     &winIp ,
				     &winPriority,
				     &winHopCount,
				     &winSpiderTimeMS ,
				     &winUh48 );
		// sanity
		if ( winIp != firstIp ) { char *xx=NULL;*xx=0;}
		if ( winUh48 != sreq2->getUrlHash48() ) { char *xx=NULL;*xx=0;}
		// make the doledb key
		key_t doleKey = g_doledb.makeKey ( winPriority,
						   // convert to secs from ms
						   winSpiderTimeMS / 1000     ,
						   winUh48 ,
						   false                    );
		// dedup. if we add dups the problem is is that they
		// overwrite the key in doledb yet the doleiptable count
		// remains undecremented and doledb is empty and never
		// replenished because the firstip can not be added to
		// waitingTree because doleiptable count is > 0. this was
		// causing spiders to hang for collections. i am not sure
		// why we should be getting dups in winnertree because they
		// have the same uh48 and that is the key in the tree.
		if ( dedup.isInTable ( &winUh48 ) ) {
			log("spider: got dup uh48=%"UINT64" dammit", winUh48);
			continue;
		}
		// count it
		added++;
		// do not allow dups
		dedup.addKey ( &winUh48 );
		// store doledb key first
		if ( ! doleBuf.safeMemcpy ( &doleKey, sizeof(key_t) ) ) 
			hadError = true;
		// then size of spiderrequest
		if ( ! doleBuf.pushLong ( sreq2->getRecSize() ) ) 
			hadError = true;
		// then the spiderrequest encapsulated
		if ( ! doleBuf.safeMemcpy ( sreq2 , sreq2->getRecSize() )) 
			hadError=true;
		// note and error
		if ( hadError ) {
			log("spider: error making doledb list: %s",
			    mstrerror(g_errno));
			    return true;
		}
	}

	// log("spider: added %"INT32" doledb recs to cache for cn=%i "
	//     "dolebufsize=%i",
	//     added,
	//     (int)m_collnum,
	//     (int)doleBuf.length());

	return addDoleBufIntoDoledb ( &doleBuf , false );//, 0 );
}

bool SpiderColl::validateDoleBuf ( SafeBuf *doleBuf ) {
	char *doleBufEnd = doleBuf->getBuf();
	// get offset
	char *pstart = doleBuf->getBufStart();
	char *p = pstart;
	int32_t jump = *(int32_t *)p;
	p += 4;
	// sanity
	if ( jump < 4 || jump > doleBuf->getLength() ) {
		char *xx=NULL;*xx=0; }
	bool gotIt = false;
	for ( ; p < doleBuf->getBuf() ; ) {
		if ( p == pstart + jump )
			gotIt = true;
		// first is doledbkey
		p += sizeof(key_t);
		// then size of spider request
		int32_t recSize = *(int32_t *)p;
		p += 4;
		// the spider request encapsulated
		SpiderRequest *sreq3;
		sreq3 = (SpiderRequest *)p;
		// point "p" to next spiderrequest
		if ( recSize != sreq3->getRecSize() ) { char *xx=NULL;*xx=0;}
		p += recSize;//sreq3->getRecSize();
		// sanity
		if ( p > doleBufEnd ) { char *xx=NULL;*xx=0; }
		if ( p < pstart     ) { char *xx=NULL;*xx=0; }
	}
	if ( ! gotIt ) { char *xx=NULL;*xx=0; }
	return true;
}

bool SpiderColl::addDoleBufIntoDoledb ( SafeBuf *doleBuf, bool isFromCache ) {
					// uint32_t cachedTimestamp ) {

	//validateDoleBuf ( doleBuf );

	////////////////////
	//
	// UPDATE WAITING TREE ENTRY
	//
	// Normally the "spidertime" is 0 for a firstIp. This will make it
	// a future time if it is not yet due for spidering.
	//
	////////////////////

	int32_t firstIp = m_waitingTreeKey.n0 & 0xffffffff;

	// sanity check. how did this happen? it messes up our crawl!
	// maybe a doledb add went through? so we should add again?
	int32_t wn = m_waitingTree.getNode(0,(char *)&m_waitingTreeKey);
	if ( wn < 0 ) { 
		log("spider: waiting tree key removed while reading list for "
		    "%s (%"INT32")",m_coll,(int32_t)m_collnum);
		// play it safe and add it back for now...
		// when i try to break here in gdb it never happens because
		// of timing issues. heisenbug...
		// false = callForScan
		//if ( ! addToWaitingTree ( 0 , m_scanningIp , false ) )
		//	log("spider: failed to add wk2 to waiting tree: %s"
		//	    ,mstrerror(g_errno));
		return true;
	}

	/*

	  MDW: now we store multiple winners in doledb, so this logic
	  won't be used. maybe just rely on the crawldelay type logic
	  in Msg13.cpp to space things out. we could also deny locks if
	  too many urls being spidered for this ip.

	// how many spiders currently out for this ip?
	int32_t outNow=g_spiderLoop.getNumSpidersOutPerIp(m_scanningIp,m_collnum);

	// even if hadn't gotten list we can bail early if too many
	// spiders from this ip are out! 
	//int32_t out = g_spiderLoop.getNumSpidersOutPerIp ( m_scanningIp );
	if ( outNow >= m_bestMaxSpidersPerIp ) {
		// note it
		if ( g_conf.m_logDebugSpider )
			log("spider: already got %"INT32" from this ip out. ip=%s",
			    m_bestMaxSpidersPerIp,
			    iptoa(m_scanningIp)
			    );
		// when his SpiderReply comes back it will call 
		// addWaitingTree with a "0" time so he'll get back in there
		//if ( wn < 0 ) { char *xx=NULL; *xx=0; }
		if ( wn >= 0 ) {
			m_waitingTree.deleteNode (wn,false );
			// note that
			//log("spdr: 1 del node %"INT32" for %s",wn,iptoa(firstIp));
		}
		// keep the table in sync now with the time
		m_waitingTable.removeKey( &m_bestRequest->m_firstIp );
		return true;
	}		
	*/

	//int64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();//Local();

	// if best request has a future spiderTime, at least update
	// the wait tree with that since we will not be doling this request
	// right now.
	if ( m_winnerTree.isEmpty() && m_minFutureTimeMS && ! isFromCache ) {

		// save memory
		m_winnerTree.reset();
		m_winnerTable.reset();

		// if in the process of being added to doledb or in doledb...
		if ( m_doleIpTable.isInTable ( &firstIp ) ) {
			// sanity i guess. remove this line if it hits this!
			log("spider: wtf????");
			//char *xx=NULL;*xx=0;
			return true;
		}

		// before you set a time too far into the future, if we
		// did receive new spider requests, entertain those
		if ( m_gotNewDataForScanningIp &&
		     // we had twitter.com with a future spider date
		     // on the pe2 cluster but we kept hitting this, so
		     // don't do this anymore if we scanned a ton of bytes
		     // like we did for twitter.com because it uses all the
		     // resources when we can like 150MB of spider requests
		     // for a single firstip
		     m_totalBytesScanned < 30000 ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: received new requests, not "
				    "updating waiting tree with future time");
			return true;
		}

		// get old time
		uint64_t oldSpiderTimeMS = m_waitingTreeKey.n1;
		oldSpiderTimeMS <<= 32;
		oldSpiderTimeMS |= (m_waitingTreeKey.n0 >> 32);
		// delete old node
		//int32_t wn = m_waitingTree.getNode(0,(char *)&m_waitingTreeKey);
		//if ( wn < 0 ) { char *xx=NULL;*xx=0; }
		if ( wn >= 0 ) {
			m_waitingTree.deleteNode3 (wn,false );
			//log("spdr: 2 del node %"INT32" for %s",wn,iptoa(firstIp));
		}

		// invalidate
		m_waitingTreeKeyValid = false;
		//int32_t  fip = m_bestRequest->m_firstIp;
		key_t wk2 = makeWaitingTreeKey ( m_minFutureTimeMS , firstIp );
		// log the replacement
		if ( g_conf.m_logDebugSpider )
			log("spider: scan replacing waitingtree key "
			    "oldtime=%"UINT32" newtime=%"UINT32" firstip=%s",
			    // bestpri=%"INT32" "
			    //"besturl=%s",
			    (uint32_t)(oldSpiderTimeMS/1000LL),
			    (uint32_t)(m_minFutureTimeMS/1000LL),
			    iptoa(firstIp)
			    //(int32_t)m_bestRequest->m_priority,
			    //	    m_bestRequest->m_url);
			    );
		// this should never fail since we deleted one above
		int32_t dn = m_waitingTree.addKey ( &wk2 );
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: RE-added time=%"INT64" ip=%s to "
			    "waiting tree node %"INT32"",
			    m_minFutureTimeMS , iptoa(firstIp),dn);

		// keep the table in sync now with the time
		m_waitingTable.addKey( &firstIp, &m_minFutureTimeMS );
		// sanity check
		if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}
		return true;
	}
	// we are coring here. i guess the best request or a copy of it
	// somehow started spidering since our last spider read, so i would
	// say we should bail on this spider scan! really i'm not exactly
	// sure what happened...
	// MDW: now we add a bunch of urls to doledb, so i guess we can't
	// check locks...
	/*
	int64_t key = makeLockTableKey ( m_bestRequest );
	if ( g_spiderLoop.m_lockTable.isInTable ( &key ) ) {
		log("spider: best request got doled out from under us");
		return true;
		char *xx=NULL;*xx=0; 
	}

	// make the doledb key first for this so we can add it
	key_t doleKey = g_doledb.makeKey ( m_bestRequest->m_priority     ,
					   // convert to seconds from ms
					   m_bestSpiderTimeMS / 1000     ,
					   m_bestRequest->getUrlHash48() ,
					   false                         );
	

	if ( g_conf.m_logDebugSpider )
		log("spider: got winner pdocid=%"INT64" url=%s",
		    m_bestRequest->m_probDocId,
		    m_bestRequest->m_url);


	// make it into a doledb record
	char *p = m_doleBuf;
	*(key_t *)p = doleKey;
	p += sizeof(key_t);
	int32_t recSize = m_bestRequest->getRecSize();
	*(int32_t *)p = recSize;
	p += 4;
	gbmemcpy ( p , m_bestRequest , recSize );
	p += recSize;
	// sanity check
	if ( p - m_doleBuf > (int32_t)MAX_DOLEREC_SIZE ) { char *xx=NULL;*xx=0; }
	*/

	// how did this happen?
	//if ( ! m_msg1Avail ) { char *xx=NULL;*xx=0; }

	char *doleBufEnd = doleBuf->getBuf();

	// add it to doledb ip table now so that waiting tree does not
	// immediately get another spider request from this same ip added
	// to it while the msg4 is out. but if add failes we totally bail
	// with g_errno set
	//
	// crap, i think this could be slowing us down when spidering
	// a single ip address. maybe use msg1 here not msg4?
	//if ( ! addToDoleTable ( m_bestRequest ) ) return true;
	// . MDW: now we have a list of doledb records in a SafeBuf:
	// . scan the requests in safebuf

	// get offset
	char *p = doleBuf->getBufStart();
	int32_t jump = *(int32_t *)p;
	// sanity
	if ( jump < 4 || jump > doleBuf->getLength() ) {
		char *xx=NULL;*xx=0; }
	// the jump includes itself
	p += jump;
	//for ( ; p < m_doleBuf.getBuf() ; ) {
	// save it
	char *doledbRec = p;
	// first is doledbkey
	p += sizeof(key_t);
	// then size of spider request
	p += 4;
	// the spider request encapsulated
	SpiderRequest *sreq3;
	sreq3 = (SpiderRequest *)p;
	// point "p" to next spiderrequest
	p += sreq3->getRecSize();

	// sanity
	if ( p > doleBufEnd ) { char *xx=NULL;*xx=0; }

	// for caching logic below, set this
	int32_t doledbRecSize = sizeof(key_t) + 4 + sreq3->getRecSize();
	// process sreq3 my incrementing the firstip count in 
	// m_doleIpTable
	if ( ! addToDoleTable ( sreq3 ) ) return true;	

	// only add the top key for now!
	//break;

	// 	// this logic is now in addToDoleTable()
	// 	// . if it was empty it is no longer
	// 	// . we have this flag here to avoid scanning empty doledb 
	// 	//   priorities because it saves us a msg5 call to doledb in 
	// 	//   the scanning loop
	// 	//int32_t bp = sreq3->m_priority;//m_bestRequest->m_priority;
	// 	//if ( bp <  0                     ) { char *xx=NULL;*xx=0; }
	// 	//if ( bp >= MAX_SPIDER_PRIORITIES ) { char *xx=NULL;*xx=0; }
	// 	//m_isDoledbEmpty [ bp ] = 0;
	// }

	// now cache the REST of the spider requests to speed up scanning.
	// better than adding 400 recs per firstip to doledb because
	// msg5's call to RdbTree::getList() is way faster.
	// even if m_doleBuf is from the cache, re-add it to lose the
	// top rec.
	// allow this to add a 0 length record otherwise we keep the same
	// old url in here and keep spidering it over and over again!

	//bool addToCache = false;
	//if( skipSize && m_doleBuf.length() - skipSize > 0 ) addToCache =true;
	// if winnertree was empty, then we might have scanned like 10M
	// twitter.com urls and not wanted any of them, so we don't want to
	// have to keep redoing that!
	//if ( m_doleBuf.length() == 0 && ! isFromCache ) addToCache = true;

	RdbCache *wc = &g_spiderLoop.m_winnerListCache;

	// remove from cache? if we added the last spider request in the
	// cached dolebuf to doledb then remove it from cache so it's not
	// a cached empty dolebuf and we recompute it not using the cache.
	if ( isFromCache && p >= doleBufEnd ) {
		//if ( addToCache ) { char *xx=NULL;*xx=0; }
		// debug note
		// if ( m_collnum == 18752 )
		// 	log("spider: rdbcache: adding single byte. skipsize=%i"
		// 	    ,doledbRecSize);
		// let's get this working right...
		//wc->removeKey ( collnum , k , start );
		//wc->markDeletedRecord(start);
		// i don't think we can remove keys from cache so add
		// a rec with a byte size of 1 to indicate for us to ignore.
		// set the timestamp to 12345 so the getRecord above will
		// not get it and promote it in the linked list.
		char byte = 0;
		key_t cacheKey;
		cacheKey.n0 = firstIp;
		cacheKey.n1 = 0;
		//wc->verify();
		wc->addRecord ( m_collnum,
				(char *)&cacheKey,
				&byte ,
				1 ,
		 		12345 );//cachedTimestamp );
		//wc->verify();
	}

	// if it wasn't in the cache and it was only one record we
	// obviously do not want to add it to the cache.
	else if ( p < doleBufEnd ) { // if ( addToCache ) {
		key_t cacheKey;
		cacheKey.n0 = firstIp;
		cacheKey.n1 = 0;
		char *x = doleBuf->getBufStart();
		// the new offset is the next record after the one we
		// just added to doledb
		int32_t newJump = (int32_t)(p - x);
		int32_t oldJump = *(int32_t *)x;
		// NO! we do a copy in rdbcache and copy the thing over
		// since we promote it. so this won't work...
		*(int32_t *)x = newJump;
		if ( newJump >= doleBuf->getLength() ) { char *xx=NULL;*xx=0;}
		if ( newJump < 4 ) { char *xx=NULL;*xx=0;}
		if ( g_conf.m_logDebugSpider ) // || m_collnum == 18752 )
			log("spider: rdbcache: updating "
			    "%"INT32" bytes of SpiderRequests "
			    "to winnerlistcache for ip %s oldjump=%"INT32
			    " newJump=%"INT32" ptr=0x%"PTRFMT,
			    doleBuf->length(),iptoa(firstIp),oldJump,
			    newJump,
			    (PTRTYPE)x);
		//validateDoleBuf ( doleBuf );
		//wc->verify();
		// inherit timestamp. if 0, RdbCache will set to current time
		// don't re-add just use the same modified buffer so we
		// don't churn the cache.
		// but do add it to cache if not already in there yet.
		if ( ! isFromCache ) {
			// if ( m_collnum == 18752 )
			// 	log("spider: rdbcache: adding record a new "
			// 	    "dbufsize=%i",(int)doleBuf->length());
			wc->addRecord ( m_collnum,
					(char *)&cacheKey,
					doleBuf->getBufStart(),//+ skipSize ,
					doleBuf->length() ,//- skipSize ,
					0);//cachedTimestamp );
		}
		//validateDoleBuf( doleBuf );
		/*
		// test it
		char *testPtr;
		int32_t testLen;
		bool inCache2 = wc->getRecord ( m_collnum     ,
						(char *)&cacheKey ,
						&testPtr,
						&testLen,
						false, // doCopy?
						600, // maxAge,600 secs
						true ,// incCounts
						NULL , // rec timestamp
						true );  // promote?
		if ( ! inCache2 ) { char *xx=NULL;*xx=0; }
		if ( testLen != m_doleBuf.length() ) {char *xx=NULL;*xx=0; }
		if ( *(int32_t *)testPtr != newJump ){char *xx=NULL;*xx=0; }
		SafeBuf tmp;
		tmp.setBuf ( testPtr , testLen , testLen , false );
		validateDoleBuf ( &tmp );
		*/
		//wc->verify();
	}

	// and the whole thing is no longer empty
	//m_allDoledbPrioritiesEmpty = 0;//false;
	//m_lastEmptyCheck = 0;

	//
	// delete the winner from ufntree as well
	//
	/*
	int64_t buh48 = m_bestRequest->getUrlHash48();
	key128_t bkey = makeUfnTreeKey ( m_bestRequest->m_firstIp ,
					 m_bestRequest->m_priority ,
					 m_bestSpiderTimeMS ,
					 buh48 );
	// must be in tree!
	int32_t node = s_ufnTree.getNextNode ( 0, (char *)&bkey );
	// if this firstip had too few requests to make it into the
	// tree then node will be < 0!
	//if ( node < 0 ) { char *xx=NULL;*xx=0; }
	if ( node >= 0 ) {
		//log("deleting node #%"INT32" firstip=%s uh48=%"UINT64"",
		//    node,iptoa(firstIp),uh48);
		s_ufnTree.deleteNode ( node , true );
	}
	*/

	m_msg4Start = gettimeofdayInMilliseconds();

	//RdbList *tmpList = &m_msg1.m_tmpList;

	// keep it on stack now that doledb is tree-only
	RdbList tmpList;

	// only add one doledb record at a time now since we
	// have the winnerListCache
	//m_doleBuf.setLength ( skipSize );

	//tmpList.setFromSafeBuf ( &m_doleBuf , RDB_DOLEDB );
	tmpList.setFromPtr ( doledbRec , doledbRecSize , RDB_DOLEDB );

	// now that doledb is tree-only and never dumps to disk, just
	// add it directly
	g_doledb.m_rdb.addList ( m_collnum , &tmpList , MAX_NICENESS );

	if ( g_conf.m_logDebugSpider )
		log("spider: adding doledb tree node size=%"INT32"",
		    doledbRecSize);


	// and it happens right away. just add it locally.
	bool status = true;

	// . use msg4 to transmit our guys into the rdb, RDB_DOLEDB
	// . no, use msg1 for speed, so we get it right away!!
	// . we' already incremented doleiptable counts... so we need to
	//   make sure this happens!!!
	/*
	bool status = m_msg1.addList ( tmpList ,
				       RDB_DOLEDB    ,
				       m_collnum    ,
				       this          ,
				       doledWrapper  ,
				       false , // forcelocal?
				       // Rdb.cpp can't call dumpTree()
				       // if niceness is 0!
				       MAX_NICENESS);
	// if it blocked set this to true so we do not reuse it
	if ( ! status ) m_msg1Avail = false;
	*/

	int32_t storedFirstIp = (m_waitingTreeKey.n0) & 0xffffffff;

	// log it
	if ( g_conf.m_logDebugSpcache ) {
		uint64_t spiderTimeMS = m_waitingTreeKey.n1;
		spiderTimeMS <<= 32;
		spiderTimeMS |= (m_waitingTreeKey.n0 >> 32);
		logf(LOG_DEBUG,"spider: removing doled waitingtree key"
		     " spidertime=%"UINT64" firstIp=%s "
		     //"pri=%"INT32" "
		     //"url=%s"
		     ,spiderTimeMS,
		     iptoa(storedFirstIp)
		     //(int32_t)m_bestRequest->m_priority,
		     //m_bestRequest->m_url);
		     );
	}

	// before adding to doledb remove from waiting tree so we do not try
	// to readd to doledb...
	m_waitingTree.deleteNode ( 0, (char *)&m_waitingTreeKey , true);
	m_waitingTable.removeKey  ( &storedFirstIp );
	//log("spider: 3 del node for %s",iptoa(storedFirstIp));
	
	// invalidate
	m_waitingTreeKeyValid = false;

	// sanity check
	if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}

	// note that ip as being in dole table
	if ( g_conf.m_logDebugSpider )
		log("spider: added best sreq for ip=%s to doletable AND "
		    "removed from waiting table",
		    iptoa(firstIp));

	// save memory
	m_winnerTree.reset();
	m_winnerTable.reset();

	//validateDoleBuf( doleBuf );

	// add did not block
	return status;
}



uint64_t SpiderColl::getSpiderTimeMS ( SpiderRequest *sreq,
				       int32_t ufn,
				       SpiderReply *srep,
				       uint64_t nowGlobalMS ) {
	// . get the scheduled spiderTime for it
	// . assume this SpiderRequest never been successfully spidered
	int64_t spiderTimeMS = ((uint64_t)sreq->m_addedTime) * 1000LL;
	// how can added time be in the future? did admin set clock back?
	//if ( spiderTimeMS > nowGlobalMS ) spiderTimeMS = nowGlobalMS;
	// if injecting for first time, use that!
	if ( ! srep && sreq->m_isInjecting ) return spiderTimeMS;
	if ( ! srep && sreq->m_isPageReindex ) return spiderTimeMS;


	//log("spider: getting spider time %"INT64, spiderTimeMS);
	// to avoid hammering an ip, get last time we spidered it...
	int64_t lastMS ;
	lastMS = m_lastDownloadCache.getLongLong ( m_collnum       ,
						   sreq->m_firstIp ,
						   -1              , // maxAge
						   true            );// promote
	// -1 means not found
	if ( (int64_t)lastMS == -1 ) lastMS = 0;
	// sanity
	if ( (int64_t)lastMS < -1 ) { 
		log("spider: corrupt last time in download cache. nuking.");
		lastMS = 0;
	}
	// min time we can spider it
	int64_t minSpiderTimeMS1 = lastMS + m_cr->m_spiderIpWaits[ufn];
	// if not found in cache
	if ( lastMS == -1 ) minSpiderTimeMS1 = 0LL;

	/////////////////////////////////////////////////
	/////////////////////////////////////////////////
	// crawldelay table check!!!!
	/////////////////////////////////////////////////
	/////////////////////////////////////////////////
	int32_t *cdp = (int32_t *)m_cdTable.getValue ( &sreq->m_domHash32 );
	int64_t minSpiderTimeMS2 = 0;
	// limit to 60 seconds crawl delay. 
	// help fight SpiderReply corruption too
	if ( cdp && *cdp > 60000 ) *cdp = 60000;
	if ( cdp && *cdp >= 0 ) minSpiderTimeMS2 = lastMS + *cdp;

	// wait 5 seconds for all outlinks in order for them to have a
	// chance to get any link info that might have been added
	// from the page that supplied this outlink
	// CRAP! this slows down same ip spidering i think... yeah, without
	// this it seems the spiders are always at 10 (sometimes 8 or 9) 
	// when i spider techcrunch.com.
	//spiderTimeMS += 5000;

	//  ensure min
	if ( spiderTimeMS < minSpiderTimeMS1 ) spiderTimeMS = minSpiderTimeMS1;
	if ( spiderTimeMS < minSpiderTimeMS2 ) spiderTimeMS = minSpiderTimeMS2;
	// if no reply, use that
	if ( ! srep ) return spiderTimeMS;
	// if this is not the first try, then re-compute the spiderTime
	// based on that last time
	// sanity check
	if ( srep->m_spideredTime <= 0 ) {
		// a lot of times these are corrupt! wtf???
		//spiderTimeMS = minSpiderTimeMS;
		return spiderTimeMS;
		//{ char*xx=NULL;*xx=0;}
	}
	// compute new spiderTime for this guy, in seconds
	int64_t waitInSecs = (uint64_t)(m_cr->m_spiderFreqs[ufn]*3600*24.0);
	// do not spider more than once per 15 seconds ever!
	// no! might be a query reindex!!
	/*
	if ( waitInSecs < 1 && ! sreq->m_isPageReindex ) { //urlIsDocId ) { 
		static bool s_printed = false;
		if ( ! s_printed ) {
			s_printed = true;
			log("spider: min spider wait is 15 seconds, "
			    "not %"UINT64" (ufn=%"INT32")",waitInSecs,ufn);
		}
		waitInSecs = 15;//900; this was 15 minutes
	}
	*/
	// in fact, force docid based guys to be zero!
	//if ( sreq->m_urlIsDocId ) waitInSecs = 0;
	if ( sreq->m_isPageReindex ) waitInSecs = 0;
	// when it was spidered
	int64_t lastSpideredMS = ((uint64_t)srep->m_spideredTime) * 1000;
	// . when we last attempted to spider it... (base time)
	// . use a lastAttempt of 0 to indicate never! 
	// (first time)
	int64_t minSpiderTimeMS3 = lastSpideredMS + (waitInSecs * 1000LL);
	//  ensure min
	if ( spiderTimeMS < minSpiderTimeMS3 ) spiderTimeMS = minSpiderTimeMS3;
	// sanity
	if ( (int64_t)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }

	return spiderTimeMS;
}

// . returns false with g_errno set on error
// . Rdb.cpp should call this when it receives a doledb key
// . when trying to add a SpiderRequest to the waiting tree we first check
//   the doledb table to see if doledb already has an sreq from this firstIp
// . therefore, we should add the ip to the dole table before we launch the
//   Msg4 request to add it to doledb, that way we don't add a bunch from the
//   same firstIP to doledb
bool SpiderColl::addToDoleTable ( SpiderRequest *sreq ) {
	// update how many per ip we got doled
	int32_t *score = (int32_t *)m_doleIpTable.getValue32 ( sreq->m_firstIp );
	// debug point
	if ( g_conf.m_logDebugSpider ){//&&1==2 ) { // disable for now, spammy
		int64_t  uh48 = sreq->getUrlHash48();
		int64_t pdocid = sreq->getParentDocId();
		int32_t ss = 1;
		if ( score ) ss = *score + 1;
		// if for some reason this collides with another key
		// already in doledb then our counts are off
		log("spider: added to doletbl uh48=%"UINT64" parentdocid=%"UINT64" "
		    "ipdolecount=%"INT32" ufn=%"INT32" priority=%"INT32" firstip=%s",
		    uh48,pdocid,ss,(int32_t)sreq->m_ufn,(int32_t)sreq->m_priority,
		    iptoa(sreq->m_firstIp));
	}
	// we had a score there already, so inc it
	if ( score ) {
		// inc it
		*score = *score + 1;
		// sanity check
		if ( *score <= 0 ) { char *xx=NULL;*xx=0; }
		// only one per ip!
		// not any more! we allow MAX_WINNER_NODES per ip!
		if ( *score > MAX_WINNER_NODES )
			log("spider: crap. had %"INT32" recs in doledb for %s "
			    "from %s."
			    "how did this happen?",
			    (int32_t)*score,m_coll,iptoa(sreq->m_firstIp));
		// now we log it too
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: added ip=%s to doleiptable "
			    "(score=%"INT32")",
			    iptoa(sreq->m_firstIp),*score);
	}
	else {
		// ok, add new slot
		int32_t val = 1;
		if ( ! m_doleIpTable.addKey ( &sreq->m_firstIp , &val ) ) {
			// log it, this is bad
			log("spider: failed to add ip %s to dole ip tbl",
			    iptoa(sreq->m_firstIp));
			// return true with g_errno set on error
			return false;
		}
		// now we log it too
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: added ip=%s to doleiptable "
			    "(score=1)",iptoa(sreq->m_firstIp));
		// sanity check
		//if ( ! m_doleIpTable.m_isWritable ) { char *xx=NULL;*xx=0;}
	}

	// . these priority slots in doledb are not empty
	// . unmark individual priority buckets
	// . do not skip them when scanning for urls to spiderd
	int32_t pri = sreq->m_priority;
	m_isDoledbEmpty[pri] = 0;		
	// reset scan for this priority in doledb
	m_nextKeys     [pri] =g_doledb.makeFirstKey2 ( pri );


	return true;
}


/////////////////////////
/////////////////////////      UTILITY FUNCTIONS
/////////////////////////

// . map a spiderdb rec to the shard # that should spider it
// . "sr" can be a SpiderRequest or SpiderReply
// . shouldn't this use Hostdb::getShardNum()?
/*
uint32_t getShardToSpider ( char *sr ) {
	// use the url hash
	int64_t uh48 = g_spiderdb.getUrlHash48 ( (key128_t *)sr );
	// host to dole it based on ip
	int32_t hostId = uh48 % g_hostdb.m_numHosts ;
	// get it
	Host *h = g_hostdb.getHost ( hostId ) ;
	// and return groupid
	return h->m_groupId;
}
*/

// does this belong in our spider cache?
bool isAssignedToUs ( int32_t firstIp ) {
	// sanity check... must be in our group.. we assume this much
	//if ( g_spiderdb.getGroupId(firstIp) != g_hostdb.m_myHost->m_groupId){
	//	char *xx=NULL;*xx=0; }
	// . host to dole it based on ip
	// . ignore lower 8 bits of ip since one guy often owns a whole block!
	//int32_t hostId=(((uint32_t)firstIp) >> 8) % g_hostdb.getNumHosts();

	if( !g_hostdb.getMyHost()->m_spiderEnabled ) return false;
	
	// get our group
	//Host *group = g_hostdb.getMyGroup();
	Host *shard = g_hostdb.getMyShard();
	// pick a host in our group

	// if not dead return it
	//if ( ! g_hostdb.isDead(hostId) ) return hostId;
	// get that host
	//Host *h = g_hostdb.getHost(hostId);
	// get the group
	//Host *group = g_hostdb.getGroup ( h->m_groupId );
	// and number of hosts in the group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// let's mix it up since spider shard was selected using this
	// same mod on the firstIp method!!
	uint64_t h64 = firstIp;
	unsigned char c = firstIp & 0xff;
	h64 ^= g_hashtab[c][0];
	// select the next host number to try
	//int32_t next = (((uint32_t)firstIp) >> 16) % hpg ;
	// hash to a host
	int32_t i = ((uint32_t)h64) % hpg;
	Host *h = &shard[i];
	// return that if alive
	if ( ! g_hostdb.isDead(h) && h->m_spiderEnabled) {
		return (h->m_hostId == g_hostdb.m_hostId);
	}
	// . select another otherwise
	// . put all alive in an array now
	Host *alive[64];
	int32_t upc = 0;

	for ( int32_t j = 0 ; j < hpg ; j++ ) {
		Host *h = &shard[j];
		if ( g_hostdb.isDead(h) ) continue;
		if( ! h->m_spiderEnabled ) continue;
		alive[upc++] = h;
	}
	// if none, that is bad! return the first one that we wanted to
	if ( upc == 0 ) {
		log("spider: no hosts can handle spider request for ip=%s", iptoa(firstIp));
		return false;
		//return (h->m_hostId == g_hostdb.m_hostId);
	}
	// select from the good ones now
	i  = ((uint32_t)firstIp) % upc;
	// get that
	h = alive[i]; //&shard[i];
	// guaranteed to be alive... kinda
	return (h->m_hostId == g_hostdb.m_hostId);
}

/////////////////////////
/////////////////////////      SPIDERLOOP
/////////////////////////

static void indexedDocWrapper ( void *state ) ;
static void doneSleepingWrapperSL ( int fd , void *state ) ;

// a global class extern'd in .h file
SpiderLoop g_spiderLoop;

SpiderLoop::SpiderLoop ( ) {
	m_crx = NULL;
	// clear array of ptrs to Doc's
	memset ( m_docs , 0 , sizeof(XmlDoc *) * MAX_SPIDERS );
}

SpiderLoop::~SpiderLoop ( ) { reset(); }

// free all doc's
void SpiderLoop::reset() {
	// delete all doc's in use
	for ( int32_t i = 0 ; i < MAX_SPIDERS ; i++ ) {
		if ( m_docs[i] ) {
			mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
			delete (m_docs[i]);
		}
		m_docs[i] = NULL;
		//m_lists[i].freeList();
	}
	m_list.freeList();
	m_lockTable.reset();
	m_lockCache.reset();
	m_winnerListCache.reset();
}

void updateAllCrawlInfosSleepWrapper ( int fd , void *state ) ;

void SpiderLoop::startLoop ( ) {
	//m_cri     = 0;
	m_crx = NULL;
	m_activeListValid = false;
	m_activeListModified = false;
	m_activeList = NULL;
	m_recalcTime = 0;
	m_recalcTimeValid = false;
	// falsify this flag
	m_outstanding1 = false;
	// not flushing
	m_msg12.m_gettingLocks = false;
	// we aren't in the middle of waiting to get a list of SpiderRequests
	m_gettingDoledbList = false;
	// we haven't registered for sleeping yet
	m_isRegistered = false;
	// clear array of ptrs to Doc's
	memset ( m_docs , 0 , sizeof(XmlDoc *) * MAX_SPIDERS );
	// . m_maxUsed is the largest i such that m_docs[i] is in use
	// . -1 means there are no used m_docs's
	m_maxUsed = -1;
	m_numSpidersOut = 0;
	m_processed = 0;
	// for locking. key size is 8 for easier debugging
	m_lockTable.set ( 8,sizeof(UrlLock),0,NULL,0,false,MAX_NICENESS,
			  "splocks", true ); // useKeyMagic? yes.

	if ( ! m_lockCache.init ( 20000 , // maxcachemem
				  4     , // fixedatasize
				  false , // supportlists?
				  1000  , // maxcachenodes
				  false , // use half keys
				  "lockcache", // dbname
				  false  ) )
		log("spider: failed to init lock cache. performance hit." );


	if ( ! m_winnerListCache.init ( 20000000 , // maxcachemem, 20MB
					-1     , // fixedatasize
					false , // supportlists?
					10000  , // maxcachenodes
					false , // use half keys
					"winnerspidercache", // dbname
					false  ) )
		log("spider: failed to init winnerlist cache. slows down.");

	// dole some out
	//g_spiderLoop.doleUrls1();
	// spider some urls that were doled to us
	//g_spiderLoop.spiderDoledUrls( );
	// sleep for .1 seconds = 100ms
	if (!g_loop.registerSleepCallback(50,this,doneSleepingWrapperSL))
		log("build: Failed to register timer callback. Spidering "
		    "is permanently disabled. Restart to fix.");

	// crawlinfo updating
	// save bandwidth for now make this every 4 seconds not 1 second
	// then try not to send crawlinfo the host should already have.
	// each collrec can have a checksum for each host of the last
	// info we sent it. but we should resend all every 100 secs anyway
	// in case host when dead.
	// now that we only send the info on startup and if changed,
	// let's move back down to 1 second
	// . make it 20 seconds because handlerequestc1 is always on
	//   profiler when we have thousands of collections
	// . let's try 10 seconds so as not to think a job is done when
	//   it is not
	if ( !g_loop.registerSleepCallback(10000,
					   this,
					   updateAllCrawlInfosSleepWrapper))
		log("build: failed to register updatecrawlinfowrapper");
}

// call this every 50ms it seems to try to spider urls and populate doledb
// from the waiting tree
void doneSleepingWrapperSL ( int fd , void *state ) {
	//SpiderLoop *THIS = (SpiderLoop *)state;
	// dole some out
	//g_spiderLoop.doleUrls1();

	// if spidering disabled then do not do this crap
	if ( ! g_conf.m_spideringEnabled )  return;
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) return;
	
	//if ( ! g_conf.m_webSpideringEnabled )  return;
	// or if trying to exit
	if ( g_process.m_mode == EXIT_MODE ) return;	
	// skip if udp table is full
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) return;

	// wait for clock to sync with host #0
	if ( ! isClockInSync() ) { 
		// let admin know why we are not spidering
		static char s_printed = false;
		if ( ! s_printed ) {
			logf(LOG_DEBUG,"spider: NOT SPIDERING until clock "
			     "is in sync with host #0.");
			s_printed = true;
		}
		return;
	}

	//if ( g_hostdb.hasDeadHost() ) return;

	static int32_t s_count = -1;
	// count these calls
	s_count++;

	int32_t now = getTimeLocal();


	// reset SpiderColl::m_didRound and m_nextDoledbKey if it is maxed
	// because we might have had a lock collision
	//int32_t nc = g_collectiondb.m_numRecs;

 redo:

	// point to head of active linked list of collection recs
	CollectionRec *nextActive = g_spiderLoop.getActiveList(); 
	collnum_t nextActiveCollnum ;
	if ( nextActive ) nextActiveCollnum = nextActive->m_collnum;

	//for ( int32_t i = 0 ; i < nc ; i++ ) {
	for ( ; nextActive ;  ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// before we assign crp to nextActive, ensure that it did
		// not get deleted on us.
		// if the next collrec got deleted, tr will be NULL
		CollectionRec *tr = g_collectiondb.getRec( nextActiveCollnum );
		// if it got deleted or restarted then it will not
		// match most likely
		if ( tr != nextActive ) {
			// this shouldn't happen much so log it
			log("spider: collnum %"INT32" got deleted. "
			    "rebuilding active list",
			    (int32_t)nextActiveCollnum);
			// rebuild the active list now
			goto redo;
		}
		// now we become him
		CollectionRec *crp = nextActive;
		// update these two vars for next iteration
		nextActive        = crp->m_nextActive;
		nextActiveCollnum = -1;
		if ( nextActive ) nextActiveCollnum = nextActive->m_collnum;
		// if list was modified a collection was deleted/added
		//if ( g_spiderLoop.m_activeListModified ) goto top;
		// // get collectionrec
		// CollectionRec *cr = g_collectiondb.getRec(i);
		// if ( ! cr ) continue;
		// skip if not enabled
		if ( ! crp->m_spideringEnabled ) continue;

		// get it
		//SpiderColl *sc = cr->m_spiderColl;
		SpiderColl *sc = g_spiderCache.getSpiderColl(crp->m_collnum);
		// skip if none
		if ( ! sc ) continue;
		// also scan spiderdb to populate waiting tree now but
		// only one read per 100ms!!
		// MDW: try taking this out
		//if ( (s_count % 10) == 0 ) {
		// always do a scan at startup & every 24 hrs
		// AND at process startup!!!
		if ( ! sc->m_waitingTreeNeedsRebuild &&
		     now - sc->m_lastScanTime > 24*3600) {
			// if a scan is ongoing, this will re-set it
			sc->m_nextKey2.setMin();
			sc->m_waitingTreeNeedsRebuild = true;
			log(LOG_INFO,
			    "spider: hit spider queue "
			    "rebuild timeout for %s (%"INT32")",
			    crp->m_coll,(int32_t)crp->m_collnum);
			// flush the ufn table
			//clearUfnTable();
		}
		// try this then. it just returns if
		// sc->m_waitingTreeNeedsRebuild is false so it
		// should be fast in those cases
		sc->populateWaitingTreeFromSpiderdb ( false );
		//}
		// if list was modified a collection was deleted/added
		//if ( g_spiderLoop.m_activeListModified ) goto top;

		// re-entry is false because we are entering for the first time
		sc->populateDoledbFromWaitingTree ( );
		// . skip if still loading doledb lists from disk this round
		// . we use m_didRound to share spiders across all collections
		//if ( ! sc->m_didRound ) continue;
		// ensure at the top!
		//if( sc->m_pri2!=MAX_SPIDER_PRIORITIES-1){char*xx=NULL;*xx=0;}
		// ok, reset it so it can start a new doledb scan
		//sc->m_didRound = false;
		// reset this as well. if there are no spiderRequests
		// available on any priority level for this collection,
		// then it will remain true. but if we attempt to spider
		// a url, or can't spider a url b/c of a max oustanding
		// constraint, we set this to false. this is used to
		// send notifications when a crawl is basically in hiatus.
		//sc->m_encounteredDoledbRecs = false;
		//sc->m_nextDoledbKey.setMin();
		// if list was modified a collection was deleted/added
		//if ( g_spiderLoop.m_activeListModified ) goto top;
	}

	// set initial priority to the highest to start spidering there
	//g_spiderLoop.m_pri = MAX_SPIDER_PRIORITIES - 1;

	// if recently called, do not call again from the sleep wrapper
	int64_t nowms = gettimeofdayInMillisecondsLocal();
	if ( nowms - g_spiderLoop.m_lastCallTime < 50 )
		return;

	// if we have a ton of collections, reduce cpu load from calling
	// spiderDoledUrls()
	static uint64_t s_skipCount = 0;
	s_skipCount++;
	// so instead of every 50ms make it every 200ms if we got
	// 100+ collections in use.
	//CollectionRec *tmp = g_spiderLoop.getActiveList(); 
	g_spiderLoop.getActiveList(); 
	int32_t activeListCount = g_spiderLoop.m_activeListCount;
	if ( ! g_spiderLoop.m_activeListValid )
		activeListCount = 0;
	int32_t skip = 1;
	if ( activeListCount >= 50 ) 
		skip = 2;
	if ( activeListCount >= 100 ) 
		skip = 4;
	if ( activeListCount >= 200 ) 
		skip = 8;
	if ( ( s_skipCount % skip ) != 0 ) 
		return;


	// spider some urls that were doled to us
	g_spiderLoop.spiderDoledUrls( );
}

void doneSendingNotification ( void *state ) {
	EmailInfo *ei = (EmailInfo *)state;
	collnum_t collnum = ei->m_collnum;
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	if ( cr != ei->m_collRec ) cr = NULL;
	char *coll = "lostcoll";
	if ( cr ) coll = cr->m_coll;
	log(LOG_INFO,"spider: done sending notifications for coll=%s (%i)", 
	    coll,(int)ei->m_collnum);

	// all done if collection was deleted from under us
	if ( ! cr ) return;

	// do not re-call this stuff
	cr->m_sendingAlertInProgress = false;

	// we can re-use the EmailInfo class now
	// pingserver.cpp sets this
	//ei->m_inUse = false;

	// so we do not send for this status again, mark it as sent for
	// but we reset sentCrawlDoneAlert to 0 on round increment below
	//log("spider: setting sentCrawlDoneAlert status to %"INT32"",
	//    (int32_t)cr->m_spiderStatus);

	// mark it as sent. anytime a new url is spidered will mark this
	// as false again! use LOCAL crawlInfo, since global is reset often.
	cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 1;//cr->m_spiderStatus;//1;

	// be sure to save state so we do not re-send emails
	cr->m_needsSave = 1;

	// sanity
	if ( cr->m_spiderStatus == 0 ) { char *xx=NULL;*xx=0; }

	// i guess each host advances its own round... so take this out
	// sanity check
	//if ( g_hostdb.m_myHost->m_hostId != 0 ) { char *xx=NULL;*xx=0; }

	//float respiderFreq = -1.0;
	float respiderFreq = cr->m_collectiveRespiderFrequency;

	// if not REcrawling, set this to 0 so we at least update our
	// round # and round start time...
	if ( respiderFreq < 0.0 ) //== -1.0 ) 
		respiderFreq = 0.0;

	//if ( respiderFreq < 0.0 ) {
	//	log("spider: bad respiderFreq of %f. making 0.",
	//	    respiderFreq);
	//	respiderFreq = 0.0;
	//}

	// advance round if that round has completed, or there are no
	// more urls to spider. if we hit maxToProcess/maxToCrawl then 
	// do not increment the round #. otherwise we should increment it.
	// do allow maxtocrawl guys through if they repeat, however!
	//if(cr->m_spiderStatus == SP_MAXTOCRAWL && respiderFreq <= 0.0)return;
	//if(cr->m_spiderStatus == SP_MAXTOPROCESS && respiderFreq<=0.0)return;

	
	////////
	//
	// . we are here because hasUrlsReadyToSpider is false
	// . we just got done sending an email alert
	// . now increment the round only if doing rounds!
	//
	///////


	// if not doing rounds, keep the round 0. they might want to up
	// their maxToCrawl limit or something.
	if ( respiderFreq <= 0.0 ) return;


	// if we hit the max to crawl rounds, then stop!!! do not
	// increment the round...
	if ( cr->m_spiderRoundNum >= cr->m_maxCrawlRounds &&
	     // there was a bug when maxCrawlRounds was 0, which should
	     // mean NO max, so fix that here:
	     cr->m_maxCrawlRounds > 0 ) return;

	// this should have been set below
	//if ( cr->m_spiderRoundStartTime == 0 ) { char *xx=NULL;*xx=0; }

	// find the "respider frequency" from the first line in the url
	// filters table whose expressions contains "{roundstart}" i guess
	//for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
	//	// get it
	//	char *ex = cr->m_regExs[i].getBufStart();
	//	// compare
	//	if ( ! strstr ( ex , "roundstart" ) ) continue;
	//	// that's good enough
	//	respiderFreq = cr->m_spiderFreqs[i];
	//	break;
	//}

	int32_t seconds = (int32_t)(respiderFreq * 24*3600);
	// add 1 for lastspidertime round off errors so we can be assured
	// all spiders have a lastspidertime LESS than the new
	// m_spiderRoundStartTime we set below.
	if ( seconds <= 0 ) seconds = 1;

	// now update this round start time. all the other hosts should
	// sync with us using the parm sync code, msg3e, every 13.5 seconds.
	//cr->m_spiderRoundStartTime += respiderFreq;
	char roundTime[128];
	sprintf(roundTime,"%"UINT32"", (uint32_t)(getTimeGlobal() + seconds));
	// roundNum++ round++
	char roundStr[128];
	sprintf(roundStr,"%"INT32"", cr->m_spiderRoundNum + 1);

	// waiting tree will usually be empty for this coll since no
	// spider requests had a valid spider priority, so let's rebuild!
	// this is not necessary because PF_REBUILD is set for the
	// "spiderRoundStart" parm in Parms.cpp so it will rebuild if that parm
	// changes already.
	//if ( cr->m_spiderColl )
	//	cr->m_spiderColl->m_waitingTreeNeedsRebuild = true;

	// we have to send these two parms to all in cluster now INCLUDING
	// ourselves, when we get it in Parms.cpp there's special
	// code to set this *ThisRound counts to 0!!!
	SafeBuf parmList;
	g_parms.addNewParmToList1 ( &parmList,cr->m_collnum,roundStr,-1 ,
				    "spiderRoundNum");
	g_parms.addNewParmToList1 ( &parmList,cr->m_collnum,roundTime, -1 ,
				    "spiderRoundStart");

	//g_parms.addParmToList1 ( &parmList , cr , "spiderRoundNum" ); 
	//g_parms.addParmToList1 ( &parmList , cr , "spiderRoundStart" ); 
	// this uses msg4 so parm ordering is guaranteed
	g_parms.broadcastParmList ( &parmList , NULL , NULL );

	// log it
	log("spider: new round #%"INT32" starttime = %"UINT32" for %s"
	    , cr->m_spiderRoundNum
	    , (uint32_t)cr->m_spiderRoundStartTime
	    , cr->m_coll
	    );
}

bool sendNotificationForCollRec ( CollectionRec *cr )  {

	// do not send email for maxrounds hit, it will send a round done
	// email for that. otherwise we end up calling doneSendingEmail()
	// twice and increment the round twice
	//if ( cr->m_spiderStatus == SP_MAXROUNDS ) {
	//	log("spider: not sending email for max rounds limit "
	//	    "since already sent for round done.");
	//	return true;
	//}

	// wtf? caller must set this
	if ( ! cr->m_spiderStatus ) { char *xx=NULL; *xx=0; }

	log(LOG_INFO,
	    "spider: sending notification for crawl status %"INT32" in "
	    "coll %s. "
	    //"current sent state is %"INT32""
	    ,(int32_t)cr->m_spiderStatus
	    ,cr->m_coll
	    //cr->m_spiderStatusMsg,
	    //,(int32_t)cr->m_localCrawlInfo.m_sentCrawlDoneAlert);
	    );

	// if we already sent it return now. we set this to false everytime
	// we spider a url, which resets it. use local crawlinfo for this
	// since we reset global.
	//if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) return true;

	if ( cr->m_sendingAlertInProgress ) return true;

	// ok, send it
	//EmailInfo *ei = &cr->m_emailInfo;
	EmailInfo *ei = (EmailInfo *)mcalloc ( sizeof(EmailInfo),"eialrt");
	if ( ! ei ) {
		log("spider: could not send email alert: %s",
		    mstrerror(g_errno));
		return true;
	}

	// in use already?
	//if ( ei->m_inUse ) return true;

	// pingserver.cpp sets this
	//ei->m_inUse = true;

	// set it up
	ei->m_finalCallback = doneSendingNotification;
	ei->m_finalState    = ei;
	ei->m_collnum       = cr->m_collnum;
	// collnums can be recycled, so ensure collection with the ptr
	ei->m_collRec       = cr;

	SafeBuf *buf = &ei->m_spiderStatusMsg;
	// stop it from accumulating status msgs
	buf->reset();
	int32_t status = -1;
	getSpiderStatusMsg ( cr , buf , &status );
					 
	// if no email address or webhook provided this will not block!
	// DISABLE THIS UNTIL FIXED

	//log("spider: SENDING EMAIL NOT");

	// do not re-call this stuff
	cr->m_sendingAlertInProgress = true;

	// ok, put it back...
	if ( ! sendNotification ( ei ) ) return false;

	// so handle this ourselves in that case:
	doneSendingNotification ( ei );
	return true;
}

// we need to update crawl info for collections that
// have urls ready to spider



SpiderColl *getNextSpiderColl ( int32_t *cri ) ;


void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) ;

//////////////////////////
//////////////////////////
//
// The second KEYSTONE function.
//
// Scans doledb and spiders the doledb records.
//
// Doledb records contain SpiderRequests ready for spidering NOW.
//
// 1. gets all locks from all hosts in the shard
// 2. sends confirm msg to all hosts if lock acquired:
//    - each host will remove from doledb then
//    - assigned host will also add new "0" entry to waiting tree if need be
//    - calling addToWaitingTree() will trigger populateDoledbFromWaitingTree()
//      to add a new entry into waiting tree, not the one just locked.
// 3. makes a new xmldoc class for that url and calls indexDoc() on it
//
//////////////////////////
//////////////////////////

// now check our RDB_DOLEDB for SpiderRequests to spider!
void SpiderLoop::spiderDoledUrls ( ) {

	//char *reb = g_rebalance.getNeedsRebalance();
	//if ( ! reb || *reb ) {return;

	//if ( g_conf.m_logDebugSpider )
	//	log("spider: trying to get a doledb rec to spider. "
	//	    "currentnumout=%"INT32"",m_numSpidersOut);

	m_lastCallTime = gettimeofdayInMillisecondsLocal();

	// when getting a lock we keep a ptr to the SpiderRequest in the
	// doledb list, so do not try to read more just yet until we know
	// if we got the lock or not
	if ( m_msg12.m_gettingLocks ) {
		// this should no longer happen
		char *xx=NULL; *xx=0;
		// make a note, maybe this is why spiders are deficient?
		if ( g_conf.m_logDebugSpider )
			log("spider: failed to get doledb rec to spider: "
			    "msg12 is getting locks");
		return;
	}

	// turn on for now
	//g_conf.m_logDebugSpider = 1;

 collLoop:

	// start again at head if this is NULL
	if ( ! m_crx ) m_crx = getActiveList();

	bool firstTime = true;

	// detect overlap
	//CollectionRec *start = m_crx;
	m_bookmark = m_crx;

	// log this now
	//logf(LOG_DEBUG,"spider: getting collnum to dole from");
	// get this
	m_sc = NULL;
	// avoid infinite loops
	//int32_t count = g_collectiondb.m_numRecs;
	// set this in the loop
	CollectionRec *cr = NULL;
	uint32_t nowGlobal = 0;
	// debug
	//log("spider: m_cri=%"INT32"",(int32_t)m_cri);
	// . get the next collection to spider
	// . just alternate them
	//for ( ; count > 0 ; m_cri++ , count-- ) {

	m_launches = 0;

 subloop:

	QUICKPOLL(MAX_NICENESS);

	// must be spidering to dole out
	if ( ! g_conf.m_spideringEnabled ) return;
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) return;

	// or if trying to exit
	if ( g_process.m_mode == EXIT_MODE ) return;	
	// if we don't have all the url counts from all hosts, then wait.
	// one host is probably down and was never up to begin with
	if ( ! s_countsAreValid ) return;
	//if ( ! g_conf.m_webSpideringEnabled )  return;
	// if we do not overlap ourselves
	if ( m_gettingDoledbList ) return;
	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) return;
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) return;
	// skip if too many udp slots being used
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) return;
	// stop if too many out. this is now 50 down from 500.
	if ( m_numSpidersOut >= MAX_SPIDERS ) return;
	// a new global conf rule
	if ( m_numSpidersOut >= g_conf.m_maxTotalSpiders ) return;
	// bail if no collections
	if ( g_collectiondb.m_numRecs <= 0 ) return;
	// not while repairing
	if ( g_repairMode ) return;
	// do not spider until collections/parms in sync with host #0
	if ( ! g_parms.m_inSyncWithHost0 ) return;
	// don't spider if not all hosts are up, or they do not all
	// have the same hosts.conf.
	if ( ! g_pingServer.m_hostsConfInAgreement ) return;
	// if nothin in the active list then return as well
	if ( ! m_activeList ) return;

	// if we hit the end of the list, wrap it around
	if ( ! m_crx ) m_crx = m_activeList;

	// we use m_bookmark to determine when we've done a round over all
	// the collections. but it will be set to null sometimes when we
	// are in this loop because the active list gets recomputed. so
	// if we lost it because our bookmarked collection is no longer 
	// 'active' then just set it to the list head i guess
	if ( ! m_bookmark || ! m_bookmark->m_isActive )
		m_bookmark = m_activeList;

	// i guess return at the end of the linked list if no collection
	// launched a spider... otherwise do another cycle to launch another
	// spider. i could see a single collection dominating all the spider
	// slots in some scenarios with this approach unfortunately.
	if ( m_crx == m_bookmark && ! firstTime && m_launches == 0 )
		return;

	// reset # launches after doing a round and having launched > 0
	if ( m_crx == m_bookmark && ! firstTime )
		m_launches = 0;

	firstTime = false;

	// if a collection got deleted re-calc the active list so
	// we don't core trying to access a delete collectionrec.
	// i'm not sure if this can happen here but i put this in as a 
	// precaution.
	if ( ! m_activeListValid ) { m_crx = NULL; goto collLoop; }

	// return now if list is just empty
	if ( ! m_activeList ) return;

	cr = m_crx;

	// advance for next time we call goto subloop;
	m_crx = m_crx->m_nextActive;


	// get the spider collection for this collnum
	m_sc = g_spiderCache.getSpiderColl(cr->m_collnum);//m_cri);
	// skip if none
	if ( ! m_sc ) goto subloop;
	// always reset priority to max at start
	m_sc->setPriority ( MAX_SPIDER_PRIORITIES - 1 );

 subloopNextPriority:

	QUICKPOLL(MAX_NICENESS);

		// wrap it if we should
		//if ( m_cri >= g_collectiondb.m_numRecs ) m_cri = 0;
		// get rec
		//cr = g_collectiondb.m_recs[m_cri];
		// skip if gone
	        if ( ! cr ) goto subloop;
		// stop if not enabled
		if ( ! cr->m_spideringEnabled ) goto subloop;

		// hit crawl round max?
		if ( cr->m_maxCrawlRounds > 0 &&
		     cr->m_isCustomCrawl &&
		     cr->m_spiderRoundNum >= cr->m_maxCrawlRounds ) {
			// should we resend our local crawl info to all hosts?
			if ( cr->m_localCrawlInfo.m_hasUrlsReadyToSpider )
				cr->localCrawlInfoUpdate();
			// mark it has false
			cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = false;
			// prevent having to save all the time
			if ( cr->m_spiderStatus != SP_MAXROUNDS ) {
				cr->m_needsSave = true;
				cr->m_spiderStatus = SP_MAXROUNDS;
			}
			goto subloop;
		}

		// hit pages to crawl max?
		if ( cr->m_maxToCrawl > 0 &&
		     cr->m_isCustomCrawl == 1 && // bulk jobs exempt!
		     cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound >=
		     cr->m_maxToCrawl ) {
			// should we resend our local crawl info to all hosts?
			if ( cr->m_localCrawlInfo.m_hasUrlsReadyToSpider )
				cr->localCrawlInfoUpdate();
			// now once all hosts have no urls ready to spider
			// then the send email code will be called.
			// do it this way for code simplicity.
			cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = false;
			// prevent having to save all the time
			if ( cr->m_spiderStatus != SP_MAXTOCRAWL ) {
				cr->m_needsSave = true;
				cr->m_spiderStatus = SP_MAXTOCRAWL;
			}
			goto subloop;
		}

		// hit pages to process max?
		if ( cr->m_maxToProcess > 0 &&
		     cr->m_isCustomCrawl == 1 && // bulk jobs exempt!
		     cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound >=
		     cr->m_maxToProcess ) {
			// should we resend our local crawl info to all hosts?
			if ( cr->m_localCrawlInfo.m_hasUrlsReadyToSpider )
				cr->localCrawlInfoUpdate();
			// now we can update it
			cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = false;
			// prevent having to save all the time
			if ( cr->m_spiderStatus != SP_MAXTOPROCESS ) {
				cr->m_needsSave = true;
				cr->m_spiderStatus = SP_MAXTOPROCESS;
			}
			goto subloop;
		}

		// int16_tcut
		CrawlInfo *ci = &cr->m_localCrawlInfo;

		// . if nothing left to spider...
		// . this is what makes us fast again! but the problem
		//   is is that if they change the url filters so that 
		//   something becomes ready to spider again we won't know
		//   because we do not set this flag back to true in
		//   Parms.cpp "doRebuild" because we don't want to get
		//   another email alert if there is nothing ready to spider
		//   after they change the parms. perhaps we should set
		//   the # of urls spidered to the "sentEmail" flag so we
		//   know if that changes to send another...
		// . bug in this...
		//if ( cr->m_isCustomCrawl && ! ci->m_hasUrlsReadyToSpider ) 
		//	continue;

		// get the spider collection for this collnum
		//m_sc = g_spiderCache.getSpiderColl(cr->m_collnum);//m_cri);
		// skip if none
		//if ( ! m_sc ) goto subloop;
		// skip if we completed the doledb scan for every spider
		// priority in this collection
		// MDW: this was the only thing based on the value of
		// m_didRound, but it was preventing addSpiderReply() from
		// calling g_spiderLoop.spiderDoledUrls( ) to launch another
		// spider. but now since we use m_crx logic and not
		// m_didRound logic to spread spiders equally over all 
		// collections, let's try without this
		//if ( m_sc->m_didRound ) goto subloop;

		// set current time, synced with host #0
		nowGlobal = (uint32_t)getTimeGlobal();

		// the last time we attempted to spider a url for this coll
		//m_sc->m_lastSpiderAttempt = nowGlobal;
		// now we save this so when we restart these two times
		// are from where we left off so we do not end up setting
		// hasUrlsReadyToSpider to true which in turn sets
		// the sentEmailAlert flag to false, which makes us
		// send ANOTHER email alert!!
		ci->m_lastSpiderAttempt = nowGlobal;

		// sometimes our read of spiderdb to populate the waiting
		// tree using evalIpLoop() takes a LONG time because
		// a niceness 0 thread is taking a LONG time! so do not
		// set hasUrlsReadyToSpider to false because of that!!
		if ( m_sc->m_gettingList1 )
			ci->m_lastSpiderCouldLaunch = nowGlobal;

		// update this for the first time in case it is never updated.
		// then after 60 seconds we assume the crawl is done and
		// we send out notifications. see below.
		if ( ci->m_lastSpiderCouldLaunch == 0 )
			ci->m_lastSpiderCouldLaunch = nowGlobal;

		//
		// . if doing respider with roundstarttime....
		// . roundstarttime is > 0 if m_collectiveRespiderFrequency
		//   is > 0, unless it has not been set to current time yet
		// . if m_collectiveRespiderFrequency was set to 0.0 then
		//   PageCrawlBot.cpp also sets m_roundStartTime to 0.
		//
		if ( nowGlobal < cr->m_spiderRoundStartTime ) goto subloop;

		// if populating this collection's waitingtree assume
		// we would have found something to launch as well. it might
		// mean the waitingtree-saved.dat file was deleted from disk
		// so we need to rebuild it at startup.
		if ( m_sc->m_waitingTreeNeedsRebuild ) 
			ci->m_lastSpiderCouldLaunch = nowGlobal;

		// get max spiders
		int32_t maxSpiders = cr->m_maxNumSpiders;
		if ( m_sc->m_isTestColl ) {
			// parser does one at a time for consistency
			if ( g_conf.m_testParserEnabled ) maxSpiders = 1;
			// need to make it 6 since some priorities essentially
			// lock the ips up that have urls in higher 
			// priorities. i.e. once we dole a url out for ip X, 
			// then if later we add a high priority url for IP X it
			// can't get spidered until the one that is doled does.
			if ( g_conf.m_testSpiderEnabled ) maxSpiders = 6;
		}

		// if some spiders are currently outstanding
		if ( m_sc->m_spidersOut )
			// do not end the crawl until empty of urls because
			// that url might end up adding more links to spider
			// when it finally completes
			ci->m_lastSpiderCouldLaunch = nowGlobal;

		// debug log
		//if ( g_conf.m_logDebugSpider )
		//	log("spider: has %"INT32" spiders out",m_sc->m_spidersOut);
		// obey max spiders per collection too
		if ( m_sc->m_spidersOut >= maxSpiders )
			goto subloop;

		// int16_tcut
		SpiderColl *sc = cr->m_spiderColl;

		if ( sc && sc->m_doleIpTable.isEmpty() )
			goto subloop;

		/*
		// . HACK. 
		// . TODO: we set spidercoll->m_gotDoledbRec to false above,
		//   then make Rdb.cpp set spidercoll->m_gotDoledbRec to
		//   true if it receives a rec. then if we see that it
		//   got set to true do not update m_nextKeys[i] or
		//   m_isDoledbEmpty[i] etc. because we might have missed
		//   the new doledb rec coming in.
		// . reset our doledb empty timer every 3 minutes and also
		// . reset our doledb empty status
		int32_t wait = SPIDER_DONE_TIMER - 10;
		if ( wait < 10 ) { char *xx=NULL;*xx=0; }
		if ( sc && nowGlobal - sc->m_lastEmptyCheck >= wait ) {
			// assume doledb not empty
			sc->m_allDoledbPrioritiesEmpty = 0;
			// reset the timer
			sc->m_lastEmptyCheck = nowGlobal;
			// reset all empty flags for each priority
			for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
				sc->m_nextKeys[i] = g_doledb.makeFirstKey2(i);
				sc->m_isDoledbEmpty[i] = 0;
			}
		}

		// . if all doledb priorities are empty, skip it quickly
		// . do this only after we update lastSpiderAttempt above
		// . this is broken!! why??
		if ( sc && sc->m_allDoledbPrioritiesEmpty >= 3 )
			continue;
		*/
		// ok, we are good to launch a spider for coll m_cri
		//break;
		//}
	
	// if none, bail, wait for sleep wrapper to re-call us later on
	//if ( count == 0 ) return;

	// sanity check
	if ( nowGlobal == 0 ) { char *xx=NULL;*xx=0; }

	// sanity check
	//if ( m_cri >= g_collectiondb.m_numRecs ) { char *xx=NULL;*xx=0; }

	// grab this
	//collnum_t collnum = m_cri;
	//CollectionRec *cr = g_collectiondb.m_recs[collnum];

	// update the crawlinfo for this collection if it has been a while.
	// should never block since callback is NULL.
	//if ( ! updateCrawlInfo(cr,NULL,NULL,true) ) { char *xx=NULL;*xx=0; }

	// get this
	//char *coll = cr->m_coll;

	// need this for msg5 call
	key_t endKey; endKey.setMax();

	// start at the top each time now
	//m_sc->m_nextDoledbKey.setMin();

	// set the key to this at start (break Spider.cpp:4683
	//m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_sc->m_pri );

	// init the m_priorityToUfn map array?
	if ( ! m_sc->m_ufnMapValid ) {
		// reset all priorities to map to a ufn of -1
		for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ )
			m_sc->m_priorityToUfn[i] = -1;
		// initialize the map that maps priority to first ufn that uses
		// that priority. map to -1 if no ufn uses it.
		for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
			// breathe
			QUICKPOLL ( MAX_NICENESS );
			// get the ith rule priority
			int32_t sp = cr->m_spiderPriorities[i];
			// must not be filtered or banned
			if ( sp < 0 ) continue;
			// sanity
			if ( sp >= MAX_SPIDER_PRIORITIES){char *xx=NULL;*xx=0;}
			// skip if already mapped
			if ( m_sc->m_priorityToUfn[sp] != -1 ) continue;
			// map that
			m_sc->m_priorityToUfn[sp] = i;
		}
		// all done
		m_sc->m_ufnMapValid = true;
	}

 loop:

	QUICKPOLL(MAX_NICENESS);

	// shortcut
	//CrawlInfo *ci = &cr->m_localCrawlInfo;
	ci = &cr->m_localCrawlInfo;

	// bail if waiting for lock reply, no point in reading more
	if ( m_msg12.m_gettingLocks ) {
		// assume we would have launched a spider for this coll
		ci->m_lastSpiderCouldLaunch = nowGlobal;
		// wait for sleep callback to re-call us in 10ms
		return;
	}

	// reset priority when it goes bogus
	if ( m_sc->m_pri2 < 0 ) {
		// i guess the scan is complete for this guy
		//m_sc->m_didRound = true;
		// count # of priority scan rounds done
		//m_sc->m_numRoundsDone++;
		// reset for next coll
		m_sc->setPriority ( MAX_SPIDER_PRIORITIES - 1 );
		//m_sc->m_pri2 = MAX_SPIDER_PRIORITIES - 1;
		// reset key now too since this coll was exhausted
		//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
		// we can't keep starting over because there are often tons
		// of annihilations between positive and negative keys
		// and causes massive disk slow down because we have to do
		// like 300 re-reads or more of about 2k each on coeus
		//m_sc->m_nextDoledbKey = m_sc->m_nextKeys [ m_sc->m_pri2 ];
		// and this
		//m_sc->m_msg5StartKey = m_sc->m_nextDoledbKey;
		// was it all empty? if did not encounter ANY doledb recs
		// after scanning all priorities, set empty to true.
		//if ( ! m_sc->m_encounteredDoledbRecs &&
		//     // if waiting tree is rebuilding... could be empty...
		//     ! m_sc->m_waitingTreeNeedsRebuild )
		//	m_sc->m_lastDoledbReadEmpty = true;
		// and go up top
		//goto collLoop;
		goto subloop;
	}

	// . skip priority if we knows its empty in doledb
	// . this will save us a call to msg5 below
	//if ( m_sc->m_isDoledbEmpty [ m_sc->m_pri2 ] ) {
	//	// decrease the priority
	//	m_sc->devancePriority();
	//	// and try the one below
	//	goto loop;
	//}

	// int16_tcut
	//CollectionRec *cr = m_sc->m_cr;
	// sanity
	if ( cr != m_sc->getCollectionRec() ) { char *xx=NULL;*xx=0; }
	// skip the priority if we already have enough spiders on it
	int32_t out = m_sc->m_outstandingSpiders[m_sc->m_pri2];
	// how many spiders can we have out?
	int32_t max = 0;
	for ( int32_t i =0 ; i < cr->m_numRegExs ; i++ ) {
		if ( cr->m_spiderPriorities[i] != m_sc->m_pri2 ) continue;
		//if ( ! cr->m_spidersEnabled[i] ) continue;
		if ( cr->m_maxSpidersPerRule[i] > max )
			max = cr->m_maxSpidersPerRule[i];
	}
	// get the max # of spiders over all ufns that use this priority!
	//int32_t max = getMaxAllowableSpidersOut ( m_sc->m_pri2 );
	//int32_t ufn = m_sc->m_priorityToUfn[m_sc->m_pri2];
	// how many can we have? crap, this is based on ufn, not priority
	// so we need to map the priority to a ufn that uses that priority
	//int32_t max = 0;
	// see if it has a maxSpiders, if no ufn uses this priority then
	// "max" will remain set to 0
	//if ( ufn >= 0 ) max = m_sc->m_cr->m_maxSpidersPerRule[ufn];
	// turned off?
	//if ( ufn >= 0 && ! m_sc->m_cr->m_spidersEnabled[ufn] ) max = 0;

	// if we have one out, do not end the round!
	if ( out > 0 ) {
		// assume we could have launched a spider
		ci->m_lastSpiderCouldLaunch = nowGlobal;
	}

	// always allow at least 1, they can disable spidering otherwise
	// no, we use this to disabled spiders... if ( max <= 0 ) max = 1;
	// skip?
	if ( out >= max ) {
		// count as non-empty then!
		//m_sc->m_encounteredDoledbRecs = true;
		// try the priority below us
		m_sc->devancePriority();
		//m_sc->m_pri--;
		// set the new key for this priority if valid
		//if ( m_sc->m_pri >= 0 )
		//	//m_sc->m_nextDoledbKey = 
		//	//	g_doledb.makeFirstKey2(m_sc->m_pri);
		//	m_sc->m_nextDoledbKey = m_sc->m_nextKeys[m_sc->m_pri2];
		// and try again
		goto loop;
	}

	// we only launch one spider at a time... so lock it up
	m_gettingDoledbList = true;

	// log this now
	if ( g_conf.m_logDebugSpider )
		m_doleStart = gettimeofdayInMillisecondsLocal();

	// debug
	if ( g_conf.m_logDebugSpider &&
	     m_sc->m_msg5StartKey != m_sc->m_nextDoledbKey )
		log("spider: msg5startKey differs from nextdoledbkey");

	// seems like we need this reset here... strange
	m_list.reset();

	// get a spider rec for us to spider from doledb (mdw)
	if ( ! m_msg5.getList ( RDB_DOLEDB      ,
				cr->m_collnum, // coll            ,
				&m_list         ,
				m_sc->m_msg5StartKey,//m_sc->m_nextDoledbKey,
				endKey          ,
				// need to make this big because we don't
				// want to end up getting just a negative key
				//1             , // minRecSizes (~ 7000)
				// we need to read in a lot because we call
				// "goto listLoop" below if the url we want
				// to dole is locked.
				// seems like a ton of negative recs
				// MDW: let's now read in 50k, not 2k,of doledb
				// spiderrequests because often the first one
				// has an ip already in use and then we'd
				// just give up on the whole PRIORITY! which
				// really freezes the spiders up.
				// Also, if a spider request is corrupt in
				// doledb it would cork us up too!
				50000            , // minRecSizes
				true            , // includeTree
				false           , // addToCache
				0               , // max cache age
				0               , // startFileNum
				-1              , // numFiles (all)
				this            , // state 
				gotDoledbListWrapper2 ,
				MAX_NICENESS    , // niceness
				true            ))// do error correction?
		// return if it blocked
		return ;
	// debug
	//log(LOG_DEBUG,"spider: read list of %"INT32" bytes from spiderdb for "
	//    "pri=%"INT32"+",m_list.m_listSize,(int32_t)m_sc->m_pri);
	// breathe
	QUICKPOLL ( MAX_NICENESS );

	int32_t saved = m_launches;

	// . add urls in list to cache
	// . returns true if we should read another list
	// . will set startKey to next key to start at
	bool status = gotDoledbList2 ( );

	// if we did not launch anything, then decrement priority and
	// try again. but if priority hits -1 then subloop2 will just go to 
	// the next collection.
	if ( saved == m_launches ) {
		m_sc->devancePriority();
		goto subloopNextPriority;
	}


	if ( status ) {
		// . if priority is -1 that means try next priority
		// . DO NOT reset the whole scan. that was what was happening
		//   when we just had "goto loop;" here
		// . this means a reset above!!!
		//if ( m_sc->m_pri2 == -1 ) return;
		// bail if waiting for lock reply, no point in reading more
		// mdw- i moved this check up to loop: jump point.
		//if ( m_msg12.m_gettingLocks ) return;
		// gotDoledbList2() always advances m_nextDoledbKey so
		// try another read
		// now advance to next coll, launch one spider per coll
		goto subloop;//loop;
	}
	// wait for the msg12 get lock request to return... 
	// or maybe spiders are off
	//return;
	// now advance to next coll, launch one spider per coll
	goto subloop;//loop;
}

// . decrement priority
// . will also set m_sc->m_nextDoledbKey
// . will also set m_sc->m_msg5StartKey
void SpiderColl::devancePriority() {
	// try next
	m_pri2 = m_pri2 - 1;
	// how can this happen?
	if ( m_pri2 < -1 ) m_pri2 = -1;
	// bogus?
	if ( m_pri2 < 0 ) return;
	// set to next priority otherwise
	//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
	m_nextDoledbKey = m_nextKeys [m_pri2];
	// and the read key
	m_msg5StartKey = m_nextDoledbKey;
}

void SpiderColl::setPriority(int32_t pri) {
	m_pri2 = pri;
	m_nextDoledbKey = m_nextKeys [ m_pri2 ];
	m_msg5StartKey = m_nextDoledbKey;
}

void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// process the doledb list and try to launch a spider
	g_spiderLoop.gotDoledbList2();
	// regardless of whether that blocked or not try to launch another 
	// and try to get the next SpiderRequest from doledb
	g_spiderLoop.spiderDoledUrls();
}

// . this is in seconds
// . had to make this 4 hours since one url was taking more than an hour
//   to lookup over 80,000 places in placedb. after an hour it had only
//   reached about 30,000
//   http://pitchfork.com/news/tours/833-julianna-barwick-announces-european-and-north-american-dates/
// . this problem with this now is that it will lock an entire IP until it
//   expires if we have maxSpidersPerIp set to 1. so now we try to add
//   a SpiderReply for local errors like when XmlDoc::indexDoc() sets g_errno,
//   we try to add a SpiderReply at least.
#define MAX_LOCK_AGE (3600*4)

// spider the spider rec in this list from doledb
// returns false if would block indexing a doc, returns true if would not,
// and returns true and sets g_errno on error
bool SpiderLoop::gotDoledbList2 ( ) {
	// unlock
	m_gettingDoledbList = false;

	// int16_tcuts
	CollectionRec *cr = m_sc->getCollectionRec();
	CrawlInfo *ci = &cr->m_localCrawlInfo;

	// update m_msg5StartKey for next read
	if ( m_list.getListSize() > 0 ) {
		// what is m_list.m_ks ?
		m_list.getLastKey((char *)&m_sc->m_msg5StartKey);
		m_sc->m_msg5StartKey += 1;
		// i guess we had something? wait for nothing to be there
		//m_sc->m_encounteredDoledbRecs = true;
	}

	// log this now
	if ( g_conf.m_logDebugSpider ) {
		int64_t now = gettimeofdayInMillisecondsLocal();
		int64_t took = now - m_doleStart;
		if ( took > 2 )
			logf(LOG_DEBUG,"spider: GOT list from doledb in "
			     "%"INT64"ms "
			     "size=%"INT32" bytes",
			     took,m_list.getListSize());
	}

	bool bail = false;
	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) bail = true;
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) bail = true;
	// skip if too many udp slots being used
	if(g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) bail =true;
	// stop if too many out
	if ( m_numSpidersOut >= MAX_SPIDERS ) bail = true;

	if ( bail ) {
		// assume we could have launched a spider
		ci->m_lastSpiderCouldLaunch = getTimeGlobal();
		// return false to indicate to try another
		return false;
	}


	// bail if list is empty
	if ( m_list.getListSize() <= 0 ) {
		// don't bother with this priority again until a key is
		// added to it! addToDoleIpTable() will be called
		// when that happens and it might unset this then.
		m_sc->m_isDoledbEmpty [ m_sc->m_pri2 ] = 1;

		/*
		// if all priorities now empty set another flag
		m_sc->m_allDoledbPrioritiesEmpty++;
		for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
			if ( m_sc->m_isDoledbEmpty[m_sc->m_pri2] ) continue;
			// must get empties 3 times in a row to ignore it
			// in case something was added to doledb while
			// we were reading from doledb.
			m_sc->m_allDoledbPrioritiesEmpty--;
			break;
		}
		*/
	
		// if no spiders...
		//if ( g_conf.m_logDebugSpider ) {
		//	log("spider: empty doledblist collnum=%"INT32" "
		//	    "inwaitingtree=%"INT32"",
		//	    (int32_t)cr->m_collnum,
		//	    m_sc->m_waitingTree.m_numUsedNodes);
		//}
		//if ( g_conf.m_logDebugSpider )
		//	log("spider: resetting doledb priority pri=%"INT32"",
		//	    m_sc->m_pri);
		// trigger a reset
		//m_sc->m_pri = -1;
		// . let the sleep timer init the loop again!
		// . no, just continue the loop
		//return true;
		// . this priority is EMPTY, try next
		// . will also set m_sc->m_nextDoledbKey
		// . will also set m_sc->m_msg5StartKey
		//m_sc->devancePriority();
		// this priority is EMPTY, try next
		//m_sc->m_pri = m_sc->m_pri - 1;
		// how can this happen?
		//if ( m_sc->m_pri < -1 ) m_sc->m_pri = -1;
		// all done if priority is negative, it will start over
		// at the top most priority, we've completed a round
		//if ( m_sc->m_pri < 0 ) return true;
		// set to next priority otherwise
		//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
		//m_sc->m_nextDoledbKey = m_sc->m_nextKeys [m_sc->m_pri];
		// and load that list from doledb for that priority
		return true;
	}

	// if debugging the spider flow show the start key if list non-empty
	/*if ( g_conf.m_logDebugSpider ) {
		// 12 byte doledb keys
		int32_t pri = g_doledb.getPriority(&m_sc->m_nextDoledbKey);
		int32_t stm = g_doledb.getSpiderTime(&m_sc->m_nextDoledbKey);
		int64_t uh48 = g_doledb.getUrlHash48(&m_sc->m_nextDoledbKey);
		logf(LOG_DEBUG,"spider: loading list from doledb startkey=%s"
		     " pri=%"INT32" time=%"UINT32" uh48=%"UINT64"",
		     KEYSTR(&m_sc->m_nextDoledbKey,12),
		     pri,
		     stm,
		     uh48);
		     }*/
	

	time_t nowGlobal = getTimeGlobal();

	// double check
	//if ( ! m_list.checkList_r( true , false, RDB_DOLEDB) ) { 
	//	char *xx=NULL;*xx=0; }

	// debug parm
	//int32_t lastpri = -2;
	//int32_t lockCount = 0;

	// reset ptr to point to first rec in list
	m_list.resetListPtr();

 listLoop:

	// all done if empty
	//if ( m_list.isExhausted() ) {
	//	// copied from above
	//	m_sc->m_didRound = true;
	//	// and try next colleciton immediately
	//	return true;
	//}

	// breathe
	QUICKPOLL(MAX_NICENESS);
	// get the current rec from list ptr
	char *rec = (char *)m_list.getListPtr();
	// the doledbkey
	key_t *doledbKey = (key_t *)rec;

	// get record after it next time
	m_sc->m_nextDoledbKey = *doledbKey ;

	// sanity check -- wrap watch -- how can this really happen?
	if ( m_sc->m_nextDoledbKey.n1 == 0xffffffff           &&
	     m_sc->m_nextDoledbKey.n0 == 0xffffffffffffffffLL ) {
		char *xx=NULL;*xx=0; }

	// only inc it if its positive! because we do have negative
	// doledb keys in here now
	//if ( (m_sc->m_nextDoledbKey & 0x01) == 0x01 )
	//	m_sc->m_nextDoledbKey += 1;
	// if its negative inc by two then! this fixes the bug where the
	// list consisted only of one negative key and was spinning forever
	//else
	//	m_sc->m_nextDoledbKey += 2;

	// if its negative inc by two then! this fixes the bug where the
	// list consisted only of one negative key and was spinning forever
	if ( (m_sc->m_nextDoledbKey & 0x01) == 0x00 )
		m_sc->m_nextDoledbKey += 2;

	// did it hit zero? that means it wrapped around!
	if ( m_sc->m_nextDoledbKey.n1 == 0x0 &&
	     m_sc->m_nextDoledbKey.n0 == 0x0 ) {
		// TODO: work this out
		char *xx=NULL;*xx=0; }

	// get priority from doledb key
	int32_t pri = g_doledb.getPriority ( doledbKey );

	// if the key went out of its priority because its priority had no
	// spider requests then it will bleed over into another priority so
	// in that case reset it to the top of its priority for next time
	int32_t pri3 = g_doledb.getPriority ( &m_sc->m_nextDoledbKey );
	if ( pri3 != m_sc->m_pri2 ) {
		m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_sc->m_pri2);
		// the key must match the priority queue its in as nextKey
		//if ( pri3 != m_sc->m_pri2 ) { char *xx=NULL;*xx=0; }
	}

	if ( g_conf.m_logDebugSpider ) {
		int32_t pri4 = g_doledb.getPriority ( &m_sc->m_nextDoledbKey );
		log("spider: setting pri2=%"INT32" queue doledb nextkey to "
		    "%s (pri=%"INT32")",
		    m_sc->m_pri2,KEYSTR(&m_sc->m_nextDoledbKey,12),pri4);
		//if ( pri4 != m_sc->m_pri2 ) { char *xx=NULL;*xx=0; }
	}

	// update next doledbkey for this priority to avoid having to
	// process excessive positive/negative key annihilations (mdw)
	m_sc->m_nextKeys [ m_sc->m_pri2 ] = m_sc->m_nextDoledbKey;

	// sanity
	if ( pri < 0 || pri >= MAX_SPIDER_PRIORITIES ) { char *xx=NULL;*xx=0; }
	// skip the priority if we already have enough spiders on it
	int32_t out = m_sc->m_outstandingSpiders[pri];
	// get the first ufn that uses this priority
	//int32_t max = getMaxAllowableSpidersOut ( pri );
	// how many spiders can we have out?
	int32_t max = 0;
	// in milliseconds. ho wint32_t to wait between downloads from same IP.
	// only for parnent urls, not including child docs like robots.txt
	// iframe contents, etc.
	int32_t sameIpWaitTime = 5000; // 250; // ms
	int32_t maxSpidersOutPerIp = 1;
	for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
		if ( cr->m_spiderPriorities[i] != pri ) continue;
		//if ( ! cr->m_spidersEnabled[i] ) continue;
		if ( cr->m_maxSpidersPerRule[i] > max )
			max = cr->m_maxSpidersPerRule[i];
		if ( cr->m_spiderIpWaits[i] < sameIpWaitTime )
			sameIpWaitTime = cr->m_spiderIpWaits[i];
		if ( cr->m_spiderIpMaxSpiders[i] > maxSpidersOutPerIp )
			maxSpidersOutPerIp = cr->m_spiderIpMaxSpiders[i];
	}

	//int32_t ufn = m_sc->m_priorityToUfn[pri];
	// how many can we have? crap, this is based on ufn, not priority
	// so we need to map the priority to a ufn that uses that priority
	//int32_t max = 0;
	// see if it has a maxSpiders, if no ufn uses this priority then
	// "max" will remain set to 0
	//if ( ufn >= 0 ) max = m_sc->m_cr->m_maxSpidersPerRule[ufn];
	// turned off?
	//if ( ufn >= 0 && ! m_sc->m_cr->m_spidersEnabled[ufn] ) max = 0;
	// if we skipped over the priority we wanted, update that
	//m_pri = pri;
	// then do the next one after that for next round
	//m_pri--;
	// always allow at least 1, they can disable spidering otherwise
	//if ( max <= 0 ) max = 1;
	// skip? and re-get another doledb list from next priority...
	if ( out >= max ) {
		// come here if we hit our ip limit too
		//	hitMax:
		// assume we could have launched a spider
		if ( max > 0 ) ci->m_lastSpiderCouldLaunch = nowGlobal;
		// this priority is maxed out, try next
		//m_sc->devancePriority();
		// assume not an empty read
		//m_sc->m_encounteredDoledbRecs = true;
		//m_sc->m_pri = pri - 1;
		// all done if priority is negative
		//if ( m_sc->m_pri < 0 ) return true;
		// set to next priority otherwise
		//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
		//m_sc->m_nextDoledbKey = m_sc->m_nextKeys [m_sc->m_pri];
		// and load that list
		return true;
	}

	// no negatives - wtf?
	// if only the tree has doledb recs, Msg5.cpp does not remove
	// the negative recs... it doesn't bother to merge.
	if ( (doledbKey->n0 & 0x01) == 0 ) { 
		// just increment then i guess
		m_list.skipCurrentRecord();
		// if exhausted -- try another load with m_nextKey set
		if ( m_list.isExhausted() ) return true;
		// otherwise, try the next doledb rec in this list
		goto listLoop;
	}

	// what is this? a dataless positive key?
	if ( m_list.getCurrentRecSize() <= 16 ) { char *xx=NULL;*xx=0; }

	int32_t ipOut = 0;
	int32_t globalOut = 0;

	// get the "spider rec" (SpiderRequest) (embedded in the doledb rec)
	SpiderRequest *sreq = (SpiderRequest *)(rec + sizeof(key_t)+4);
	// sanity check. check for http(s)://
	if ( sreq->m_url[0] != 'h' &&
	     // might be a docid from a pagereindex.cpp
	     ! is_digit(sreq->m_url[0]) ) { 
		// note it
		if ( (g_corruptCount % 1000) == 0 )
			log("spider: got corrupt doledb record. ignoring. "
			    "pls fix!!! cn=%i",(int)m_collnum);
		g_corruptCount++;
		goto skipDoledbRec;
		// skip for now....!! what is causing this???
		//m_list.skipCurrentRecord();
		// if exhausted -- try another load with m_nextKey set
		//if ( m_list.isExhausted() ) return true;
		// otherwise, try the next doledb rec in this list
		//goto listLoop;
	}		


	// . how many spiders out for this ip now?
	// . TODO: count locks in case twin is spidering... but it did not seem
	//   to work right for some reason
	for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
		// get it
		XmlDoc *xd = m_docs[i];
		if ( ! xd ) continue;
		if ( ! xd->m_sreqValid ) continue;
		// to prevent one collection from hogging all the urls for
		// particular IP and starving other collections, let's make 
		// this a per collection count.
		// then allow msg13.cpp to handle the throttling on its end.
		// also do a global count over all collections now
		if ( xd->m_sreq.m_firstIp == sreq->m_firstIp ) globalOut++;
		// only count for our same collection otherwise another
		// collection can starve us out
		if ( xd->m_collnum != cr->m_collnum ) continue;
		if ( xd->m_sreq.m_firstIp == sreq->m_firstIp ) ipOut++;
	}
	// don't give up on this priority, just try next in the list.
	// we now read 50k instead of 2k from doledb in order to fix
	// one ip from bottle corking the whole priority!!
	if ( ipOut >= maxSpidersOutPerIp ) {
		// assume we could have launched a spider
		if ( maxSpidersOutPerIp > 0 ) 
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		//goto hitMax;
	skipDoledbRec:
		// skip
		m_list.skipCurrentRecord();
		// if not exhausted try the next doledb rec in this list
		if ( ! m_list.isExhausted() ) goto listLoop;
		// print a log msg if we corked things up even
		// though we read 50k from doledb
		static bool s_flag = true;
		if ( m_list.m_listSize > 50000 && s_flag ) {
			s_flag = true;
			log("spider: 50k not big enough");
		}
		// list is exhausted...
		return true;
	}

	// but if the global is high, only allow one out per coll so at 
	// least we dont starve and at least we don't make a huge wait in
	// line of queued results just sitting there taking up mem and
	// spider slots so the crawlbot hourly can't pass.
	if ( globalOut >= maxSpidersOutPerIp && ipOut >= 1 ) {
		// assume we could have launched a spider
		if ( maxSpidersOutPerIp > 0 ) 
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		goto skipDoledbRec;
	}

	if ( g_conf.m_logDebugSpider )
		log("spider: %"INT32" spiders out for %s for %s",
		    ipOut,iptoa(sreq->m_firstIp),
		    sreq->m_url);

	// sometimes we have it locked, but is still in doledb i guess.
	// seems like we might have give the lock to someone else and
	// there confirmation has not come through yet, so it's still
	// in doledb.
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// int16_tcut
	int64_t lockKey = makeLockTableKey ( sreq );
	// get the lock... only avoid if confirmed!
	int32_t slot = ht->getSlot ( &lockKey );
	UrlLock *lock = NULL;
	if ( slot >= 0 ) 
		// get the corresponding lock then if there
		lock = (UrlLock *)ht->getValueFromSlot ( slot );
	// if there and confirmed, why still in doledb?
	if ( lock && lock->m_confirmed ) {
		// fight log spam
		static int32_t s_lastTime = 0;
		if ( nowGlobal - s_lastTime >= 2 ) {
			// why is it not getting unlocked!?!?!
			log("spider: spider request locked but still in "
			    "doledb. uh48=%"INT64" firstip=%s %s",
			    sreq->getUrlHash48(), 
			    iptoa(sreq->m_firstIp),sreq->m_url );
			s_lastTime = nowGlobal;
		}
		// just increment then i guess
		m_list.skipCurrentRecord();
		// let's return false here to avoid an infinite loop
		// since we are not advancing nextkey and m_pri is not
		// being changed, that is what happens!
		if ( m_list.isExhausted() ) {
			// crap. but then we never make it to lower priorities.
			// since we are returning false. so let's try the
			// next priority in line.
			//m_sc->m_pri--;
			//m_sc->devancePriority();
			// try returning true now that we skipped to
			// the next priority level to avoid the infinite
			// loop as described above.
			return true;
			//return false;//true;
		}
		// try the next record in this list
		goto listLoop;
	}

	// . no no! the SpiderRequests in doledb are in our group because
	//   doledb is split based on ... firstIp i guess...
	//   BUT now lock is done based on probable docid since we do not
	//   know the firstIp if injected spider requests but we do know
	//   their probable docids since that is basically a function of
	//   the url itself. THUS we now must realize this by trying to
	//   get the lock for it and failing!
	/*
	// . likewise, if this request is already being spidered, if it
	//   is in the lock table, skip it...
	// . if this is currently locked for spidering by us or another
	//   host (or by us) then return true here
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// int16_tcut
	//int64_t uh48 = sreq->getUrlHash48();
	// get the lock key
	uint64_t lockKey ;
	lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);
	// check tree
	int32_t slot = ht->getSlot ( &lockKey );
	// if more than an hour old nuke it and clear it
	if ( slot >= 0 ) {
		// get the corresponding lock then if there
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot ( slot );
		// if 1hr+ old, nuke it and disregard
		if ( nowGlobal - lock->m_timestamp > MAX_LOCK_AGE ) {
			// unlock it
			ht->removeSlot ( slot );
			// it is gone
			slot = -1;
		}
	}
	// if there say no no -- will try next spiderrequest in doledb then
	if ( slot >= 0 ) {
		// just increment then i guess
		m_list.skipCurrentRecord();
		// count locks
		//if ( pri == lastpri ) lockCount++;
		//else                  lockCount = 1;
		//lastpri = pri;
		// how is it we can have 2 locked but only 1 outstanding
		// for the same priority?
		// basically this url is done being spidered, but we have
		// not yet processed the negative doledb key in Rdb.cpp which
		// will remove the lock from the lock table... so this 
		// situation is perfectly fine i guess.. assuming that is
		// what is going on
		//if ( lockCount >= max ) { char *xx=NULL;*xx=0; }
		// this is not good
		static bool s_flag = false;
		if ( ! s_flag ) {
			s_flag = true;
			log("spider: got url %s that is locked but in dole "
			    "table... skipping",sreq->m_url);
		}
		// if exhausted -- try another load with m_nextKey set
		if ( m_list.isExhausted() ) return true;
		// otherwise, try the next doledb rec in this list
		goto listLoop;
	}
	*/

	// force this set i guess... why isn't it set already? i guess when
	// we added the spider request to doledb it was not set at that time
	//sreq->m_doled = 1;

	//
	// sanity check. verify the spiderrequest also exists in our
	// spidercache. we no longer store doled out spider requests in our
	// cache!! they are separate now.
	//
	//if ( g_conf.m_logDebugSpider ) {
	//	// scan for it since we may have dup requests
	//	int64_t uh48   = sreq->getUrlHash48();
	//	int64_t pdocid = sreq->getParentDocId();
	//	// get any request from our urlhash table
	//	SpiderRequest *sreq2 = m_sc->getSpiderRequest2 (&uh48,pdocid);
	//	// must be there. i guess it could be missing if there is
	//	// corruption and we lost it in spiderdb but not in doledb...
	//	if ( ! sreq2 ) { char *xx=NULL;*xx=0; }
	//}

	// log this now
	if ( g_conf.m_logDebugSpider ) 
		logf(LOG_DEBUG,"spider: trying to spider url %s",sreq->m_url);

	/*
	if ( ufn >= 0 ) {
		int32_t siwt = m_sc->m_cr->m_spiderIpWaits[ufn];
		if ( siwt >= 0 ) sameIpWaitTime = siwt;
	}

	if ( ufn >= 0 ) {
		maxSpidersOutPerIp = m_sc->m_cr->m_spiderIpMaxSpiders[ufn];
		if ( maxSpidersOutPerIp < 0 ) maxSpidersOutPerIp = 999;
	}
	*/

	// assume we launch the spider below. really this timestamp indicates
	// the last time we COULD HAVE LAUNCHED *OR* did actually launch
	// a spider
	ci->m_lastSpiderCouldLaunch = nowGlobal;

	// set crawl done email sent flag so another email can be sent again
	// in case the user upped the maxToCrawl limit, for instance,
	// so that the crawl could continue.
	//ci->m_sentCrawlDoneAlert = 0;

	// if we thought we were done, note it if something comes back up
	if ( ! ci->m_hasUrlsReadyToSpider ) 
		log("spider: got a reviving url for coll %s (%"INT32") to crawl %s",
		    cr->m_coll,(int32_t)cr->m_collnum,sreq->m_url);

	// if changing status, resend our local crawl info to all hosts?
	if ( ! ci->m_hasUrlsReadyToSpider )
		cr->localCrawlInfoUpdate();

	// there are urls ready to spider
	ci->m_hasUrlsReadyToSpider = true;

	// newly created crawls usually have this set to false so set it
	// to true so getSpiderStatus() does not return that "the job
	// is completed and no repeat is scheduled"...
	if ( cr->m_spiderStatus == SP_INITIALIZING ) {
		// this is the GLOBAL crawl info, not the LOCAL, which
		// is what "ci" represents...
		// MDW: is this causing the bug?
		// the other have already reported that there are no urls
		// to spider, so they do not re-report. we already
		// had 'hasurlsreadytospider' set to true so we didn't get
		// the reviving log msg.
		cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;
		// set this right i guess...?
		ci->m_lastSpiderAttempt = nowGlobal;
	}

	// reset reason why crawl is not running, because we basically are now
	cr->m_spiderStatus = SP_INPROGRESS; // this is 7
	//cr->m_spiderStatusMsg = NULL;

	// be sure to save state so we do not re-send emails
	cr->m_needsSave = 1;

	// assume not an empty read
	//m_sc->m_encounteredDoledbRecs = true;

	// int16_tcut
	//char *coll = m_sc->m_cr->m_coll;
	// sometimes the spider coll is reset/deleted while we are
	// trying to get the lock in spiderUrl9() so let's use collnum
	collnum_t collnum = m_sc->getCollectionRec()->m_collnum;

	// . spider that. we don't care wheter it blocks or not
	// . crap, it will need to block to get the locks!
	// . so at least wait for that!!!
	// . but if we end up launching the spider then this should NOT
	//   return false! only return false if we should hold up the doledb
	//   scan
	// . this returns true right away if it failed to get the lock...
	//   which means the url is already locked by someone else...
	// . it might also return true if we are already spidering the url
	bool status = spiderUrl9 ( sreq , doledbKey , collnum, sameIpWaitTime ,
				   maxSpidersOutPerIp ) ;

	// just increment then i guess
	m_list.skipCurrentRecord();

	// if it blocked, wait for it to return to resume the doledb list 
	// processing because the msg12 is out and we gotta wait for it to 
	// come back. when lock reply comes back it tries to spider the url
	// then it tries to call spiderDoledUrls() to keep the spider queue
	// spidering fully.
	if ( ! status ) return false;

	// if exhausted -- try another load with m_nextKey set
	if ( m_list.isExhausted() ) {
		// if no more in list, fix the next doledbkey,
		// m_sc->m_nextDoledbKey
		log ( LOG_DEBUG,"spider: list exhausted.");
		return true;
	}
	// otherwise, it might have been in the lock cache and quickly
	// rejected, or rejected for some other reason, so try the next 
	// doledb rec in this list
	goto listLoop;

	//
	// otherwise, it blocked, trying to get the lock across the network.
	// so reset the doledb scan assuming it will go through. if it does
	// NOT get the lock, then it will be in the lock cache for quick
	// "return true" from spiderUrl() above next time we try it.
	//

	// once we get a url from doledb to spider, reset our doledb scan.
	// that way if a new url gets added to doledb that is high priority
	// then we get it right away.  
	//
	// NO! because the lock request can block then fail!! and we end
	// up resetting and in an infinite loop!
	//
	//m_sc->m_pri = -1;

	//return false;
}

// . spider the next url that needs it the most
// . returns false if blocked on a spider launch, otherwise true.
// . returns false if your callback will be called
// . returns true and sets g_errno on error
bool SpiderLoop::spiderUrl9 ( SpiderRequest *sreq ,
			      key_t *doledbKey ,
			      //char *coll ,
			      collnum_t collnum ,
			      int32_t sameIpWaitTime ,
			      int32_t maxSpidersOutPerIp ) {
	// sanity check
	//if ( ! sreq->m_doled ) { char *xx=NULL;*xx=0; }
	// if waiting on a lock, wait
	if ( m_msg12.m_gettingLocks ) { char *xx=NULL;*xx=0; }
	// sanity
	if ( ! m_sc ) { char *xx=NULL;*xx=0; }

	// sanity check
	// core dump? just re-run gb and restart the parser test...
	if ( //g_test.m_isRunning && 
	     //! g_test.m_spiderLinks &&
	     g_conf.m_testParserEnabled &&
	     ! sreq->m_isInjecting ) { 
		char *xx=NULL;*xx=0; }

	// wait until our clock is synced with host #0 before spidering since
	// we store time stamps in the domain and ip wait tables in 
	// SpiderCache.cpp. We don't want to freeze domain for a int32_t time
	// because we think we have to wait until tomorrow before we can
	// spider it.
	if ( ! isClockInSync() ) { 
		// let admin know why we are not spidering
		static char s_printed = false;
		if ( ! s_printed ) {
			logf(LOG_DEBUG,"spider: NOT SPIDERING until clock "
			     "is in sync with host #0.");
			s_printed = true;
		}
		return true;
	}
	// turned off?
	if ( ( (! g_conf.m_spideringEnabled ||
		// or if trying to exit
		g_process.m_mode == EXIT_MODE
		) && // ! g_conf.m_webSpideringEnabled ) &&
	       ! sreq->m_isInjecting ) || 
	     // repairing the collection's rdbs?
	     g_repairMode ||
	     // power went off?
	     ! g_process.m_powerIsOn ) {
		// try to cancel outstanding spiders, ignore injects
		for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
			// get it
			XmlDoc *xd = m_docs[i];
			if ( ! xd                      ) continue;
			//if ( xd->m_sreq.m_isInjecting ) continue;
			// let everyone know, TcpServer::cancel() uses this in
			// destroySocket()
			g_errno = ECANCELLED;
			// cancel the socket trans who has "xd" as its state. 
			// this will cause XmlDoc::gotDocWrapper() to be called
			// now, on this call stack with g_errno set to 
			// ECANCELLED. But if Msg16 was not in the middle of 
			// HttpServer::getDoc() then this will have no effect.
			g_httpServer.cancel ( xd );//, g_msg13RobotsWrapper );
			// cancel any Msg13 that xd might have been waiting for
			g_udpServer.cancel ( &xd->m_msg13 , 0x13 );
		}
		return true;
	}
	// do not launch any new spiders if in repair mode
	if ( g_repairMode ) { 
		g_conf.m_spideringEnabled = false; 
		//g_conf.m_injectionEnabled = false; 
		return true; 
	}
	// do not launch another spider if less than 25MB of memory available.
	// this causes us to dead lock when spiders use up all the mem, and
	// file merge operation can not get any, and spiders need to add to 
	// titledb but can not until the merge completes!!
	if ( g_conf.m_maxMem - g_mem.m_used < 25*1024*1024 ) {
		static int32_t s_lastTime = 0;
		static int32_t s_missed   = 0;
		s_missed++;
		int32_t now = getTime();
		// don't spam the log, bug let people know about it
		if ( now - s_lastTime > 10 ) {
			log("spider: Need 25MB of free mem to launch spider, "
			    "only have %"INT64". Failed to launch %"INT32" times so "
			    "far.", g_conf.m_maxMem - g_mem.m_used , s_missed );
			s_lastTime = now;
		}
	}

	// we store this in msg12 for making a fakedb key
	//collnum_t collnum = g_collectiondb.getCollnum ( coll );

	// int16_tcut
	int64_t lockKeyUh48 = makeLockTableKey ( sreq );

	//uint64_t lockKey ;
	//lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);
	//lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);

	// . now that we have to use msg12 to see if the thing is locked
	//   to avoid spidering it.. (see comment in above function)
	//   we often try to spider something we are already spidering. that
	//   is why we have an rdbcache, m_lockCache, to make these lock
	//   lookups quick, now that the locking group is usually different
	//   than our own!
	// . we have to check this now because removeAllLocks() below will
	//   remove a lock that one of our spiders might have. it is only
	//   sensitive to our hostid, not "spider id"
	// sometimes we exhaust the doledb and m_nextDoledbKey gets reset
	// to zero, we do a re-scan and get a doledbkey that is currently
	// being spidered or is waiting for its negative doledb key to
	// get into our doledb tree
	for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
		// get it
		XmlDoc *xd = m_docs[i];
		if ( ! xd ) continue;

		// jenkins was coring spidering the same url in different
		// collections at the same time
		if ( ! xd->m_collnumValid ) continue;
		if ( xd->m_collnum != collnum ) continue;

		// . problem if it has our doledb key!
		// . this happens if we removed the lock above before the
		//   spider returned!! that's why you need to set
		//   MAX_LOCK_AGE to like an hour or so
		// . i've also seen this happen because we got stuck looking
		//   up like 80,000 places and it was taking more than an
		//   hour. it had only reach about 30,000 after an hour.
		//   so at this point just set the lock timeout to
		//   4 hours i guess.
		// . i am seeing this again and we are trying over and over
		//   again to spider the same url and hogging the cpu so

		//   we need to keep this sanity check in here for times
		//   like this
		if ( xd->m_doledbKey == *doledbKey ) { 
			// just note it for now
			log("spider: spidering same url %s twice. "
			    "different firstips?",
			    xd->m_firstUrl.m_url);
			//char *xx=NULL;*xx=0; }
		}
		// keep chugging
		continue;
		//if ( xd->m_doledbKey != *doledbKey ) continue;
		// count it as processed
		m_processed++;
		// log it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: we are already spidering %s "
			     "lockkey=%"UINT64"",sreq->m_url,lockKeyUh48);
		// all done, no lock granted...
		return true;
	}

	// reset g_errno
	g_errno = 0;
	// breathe
	QUICKPOLL(MAX_NICENESS);

	// sanity. ensure m_sreq doesn't change from under us i guess
	if ( m_msg12.m_gettingLocks ) { char *xx=NULL;*xx=0; }

	// get rid of this crap for now
	//g_spiderCache.meterBandwidth();

	// save these in case getLocks() blocks
	m_sreq      = sreq;
	m_doledbKey = doledbKey;
	//m_coll      = coll;
	m_collnum = collnum;



	// if we already have the lock then forget it. this can happen
	// if spidering was turned off then back on.
	// MDW: TODO: we can't do this anymore since we no longer have
	// the lockTable check above because we do not control our own
	// lock now necessarily. it often is in another group's lockTable.
	//if ( g_spiderLoop.m_lockTable.isInTable(&lockKey) ) {
	//	log("spider: already have lock for lockKey=%"UINT64"",lockKey);
	//	// proceed
	//	return spiderUrl2();
	//}
	
	// count it
	m_processed++;

	// now we just take it out of doledb instantly
	int32_t node = g_doledb.m_rdb.m_tree.deleteNode(m_collnum,
							(char *)m_doledbKey,
							true);

	if ( g_conf.m_logDebugSpider )
		log("spider: deleting doledb tree node %"INT32,node);

	// if url filters rebuilt then doledb gets reset and i've seen us hit
	// this node == -1 condition here... so maybe ignore it... just log
	// what happened? i think we did a quickpoll somewhere between here
	// and the call to spiderDoledUrls() and it the url filters changed
	// so it reset doledb's tree. so in that case we should bail on this
	// url.
	if ( node == -1 ) { 
		g_errno = EADMININTERFERENCE;
		log("spider: lost url about to spider from url filters "
		    "and doledb tree reset. %s",mstrerror(g_errno));
		return true;
	}


	// now remove from doleiptable since we removed from doledb
	m_sc->removeFromDoledbTable ( sreq->m_firstIp );

	// DO NOT add back to waiting tree if max spiders
	// out per ip was 1 OR there was a crawldelay. but better
	// yet, take care of that in the winReq code above.

	// . now add to waiting tree so we add another spiderdb
	//   record for this firstip to doledb
	// . true = callForScan
	// . do not add to waiting tree if we have enough outstanding
	//   spiders for this ip. we will add to waiting tree when
	//   we receive a SpiderReply in addSpiderReply()
	if ( //sc && //out < cq->m_maxSpidersOutPerIp &&
	     // this will just return true if we are not the 
	     // responsible host for this firstip
	     // DO NOT populate from this!!! say "false" here...
	     ! m_sc->addToWaitingTree ( 0 , sreq->m_firstIp, false ) &&
	     // must be an error...
	     g_errno ) {
		char *msg = "FAILED TO ADD TO WAITING TREE";
		log("spider: %s %s",msg,mstrerror(g_errno));
		//us->sendErrorReply ( udpSlot , g_errno );
		//return;
	}


	// . add it to lock table to avoid respider, removing from doledb
	//   is not enough because we re-add to doledb right away
	// . return true on error here
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	UrlLock tmp;
	tmp.m_hostId = g_hostdb.m_myHost->m_hostId;
	tmp.m_timestamp = 0;
	tmp.m_expires = 0;
	tmp.m_firstIp = m_sreq->m_firstIp;
	tmp.m_spiderOutstanding = 0;
	tmp.m_confirmed = 1;
	tmp.m_collnum = m_collnum;
	if ( g_conf.m_logDebugSpider )
		log("spider: adding lock uh48=%"INT64" lockkey=%"INT64"",
		    m_sreq->getUrlHash48(),lockKeyUh48);
	if ( ! ht->addKey ( &lockKeyUh48 , &tmp ) )
		return true;

	// now do it. this returns false if it would block, returns true if it
	// would not block. sets g_errno on error. it spiders m_sreq.
	return spiderUrl2 ( );

	// flag it so m_sreq does not "disappear"
	m_msg12.m_gettingLocks = true;

	// count it
	m_processed++;

	//if ( g_conf.m_logDebugSpider )
	//	logf(LOG_DEBUG,"spider: getting lock for %s",m_sreq->m_url);

	//
	// . try to get the lock. assume it always blocks
	// . it will call spiderUrl2 with sr when it gets a reply
	// . if injecting, no need for lock! will return true for that!
	//
	if ( ! m_msg12.getLocks ( m_sreq->getUrlHash48() ,
				  //m_sreq->m_probDocId,//UrlHash48(),
				  m_sreq->m_url ,
				  m_doledbKey ,
				  collnum,
				  sameIpWaitTime,
				  maxSpidersOutPerIp,
				  m_sreq->m_firstIp,
				  NULL , // state
				  NULL ) )  // callback
		return false;
	// no go
	m_msg12.m_gettingLocks = false;
	// it will not block if the lock was found in our m_lockCache!
	return true;
	// should always block now!
	//char *xx=NULL;*xx=0;
	// i guess we got it
	//return spiderUrl2 ( );
	//return true;
}

bool SpiderLoop::spiderUrl2 ( ) {

	// sanity check
	//if ( ! m_sreq->m_doled ) { char *xx=NULL;*xx=0; }

	// . find an available doc slot
	// . we can have up to MAX_SPIDERS spiders (300)
	int32_t i;
	for ( i=0 ; i<MAX_SPIDERS ; i++ ) if (! m_docs[i]) break;

	// breathe
	QUICKPOLL(MAX_NICENESS);

	// come back later if we're full
	if ( i >= MAX_SPIDERS ) {
		log(LOG_DEBUG,"build: Already have %"INT32" outstanding spiders.",
		    (int32_t)MAX_SPIDERS);
		char *xx = NULL; *xx = 0;
	}

	// breathe
	QUICKPOLL(MAX_NICENESS);

	XmlDoc *xd;
	// otherwise, make a new one if we have to
	try { xd = new (XmlDoc); }
	// bail on failure, sleep and try again
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("build: Could not allocate %"INT32" bytes to spider "
		    "the url %s. Will retry later.",
		    (int32_t)sizeof(XmlDoc),  m_sreq->m_url );
		return true;
	}
	// register it's mem usage with Mem.cpp class
	mnew ( xd , sizeof(XmlDoc) , "XmlDoc" );
	// add to the array
	m_docs [ i ] = xd;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	char *coll = "collnumwasinvalid";
	if ( cr ) coll = cr->m_coll;

	// . pass in a pbuf if this is the "qatest123" collection
	// . we will dump the SafeBuf output into a file in the
	//   test subdir for comparison with previous versions of gb
	//   in order to see what changed
	SafeBuf *pbuf = NULL;
	if ( !strcmp( coll,"qatest123") && g_conf.m_testParserEnabled ) 
		pbuf = &xd->m_sbuf;

	//
	// sanity checks
	//
	//int64_t uh48;
	//int64_t pdocid;
	//if ( g_conf.m_logDebugSpider ) {
	//	// scan for it since we may have dup requests
	//	uh48   = m_sreq->getUrlHash48();
	//	pdocid = m_sreq->getParentDocId();
	//	// get any request from our urlhash table
	//	SpiderRequest *sreq2 = m_sc->getSpiderRequest2 (&uh48,pdocid);
	//	// must be valid parent
	//	if ( ! sreq2 && pdocid == 0LL ) { char *xx=NULL;*xx=0; }
	//	// for now core on this
	//	if ( ! sreq2 ) {  char *xx=NULL;*xx=0; }
	//	// log it
	//	logf(LOG_DEBUG,"spider: spidering uh48=%"UINT64" pdocid=%"UINT64"",
	//	     uh48,pdocid);
	//}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: spidering firstip9=%s(%"UINT32") "
		     "uh48=%"UINT64" prntdocid=%"UINT64" k.n1=%"UINT64" k.n0=%"UINT64"",
		     iptoa(m_sreq->m_firstIp),
		     (uint32_t)m_sreq->m_firstIp,
		     m_sreq->getUrlHash48(),
		     m_sreq->getParentDocId() ,
		     m_sreq->m_key.n1,
		     m_sreq->m_key.n0);


	// this returns false and sets g_errno on error
	if ( ! xd->set4 ( m_sreq       ,
			  m_doledbKey  ,
			  coll       ,
			  pbuf         ,
			  MAX_NICENESS ) ) {
		// i guess m_coll is no longer valid?
		mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
		delete (m_docs[i]);
		m_docs[i] = NULL;
		// error, g_errno should be set!
		return true;
	}

	// . launch count increment
	// . this is only used locally on this host to set its
	//   m_hasUrlsReadyToSpider to false.
	//cr->m_localCrawlInfo.m_numUrlsLaunched++;

	// call this after doc gets indexed
	xd->setCallback ( xd  , indexedDocWrapper );

	/*
	// set it from provided parms if we are injecting via Msg7
	if ( m_sreq->m_isInjecting ) {
		// now fill these in if provided too!
		if ( m_content ) {
			if ( m_sreq->m_firstIp ) {
				xd->m_ip      = m_sreq->m_firstIp;
				xd->m_ipValid = true;
			}
			xd->m_isContentTruncated      = false;
			xd->m_isContentTruncatedValid = true;
			xd->m_httpReplyValid          = true;
			xd->m_httpReply               = m_content;
			xd->m_httpReplySize           = m_contentLen + 1;
			if ( ! m_contentHasMime ) xd->m_useFakeMime = true;
		}
		// a special callback for injected docs
		//xd->m_injectionCallback = m_callback;
		//xd->m_injectionState    = m_state;
	}
	*/

	// increase m_maxUsed if we have to
	if ( i > m_maxUsed ) m_maxUsed = i;
	// count it
	m_numSpidersOut++;
	// count this
	m_sc->m_spidersOut++;

	g_spiderLoop.m_launches++;

	// count it as a hit
	//g_stats.m_spiderUrlsHit++;
	// sanity check
	if (m_sreq->m_priority <= -1 ) { 
		log("spider: fixing bogus spider req priority of %i for "
		    "url %s",
		    (int)m_sreq->m_priority,m_sreq->m_url);
		m_sreq->m_priority = 0;
		//char *xx=NULL;*xx=0; 
	}
	//if(m_sreq->m_priority >= MAX_SPIDER_PRIORITIES){char *xx=NULL;*xx=0;}
	// update this
	m_sc->m_outstandingSpiders[(unsigned char)m_sreq->m_priority]++;

	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: sc_out=%"INT32" waiting=%"INT32" url=%s",
		    m_sc->m_spidersOut,
		    m_sc->m_waitingTree.m_numUsedNodes,
		    m_sreq->m_url);


	// debug log
	//log("XXX: incremented count to %"INT32" for %s",
	//    m_sc->m_spidersOut,m_sreq->m_url);
	//if ( m_sc->m_spidersOut != m_numSpidersOut ) { char *xx=NULL;*xx=0; }

	// . return if this blocked
	// . no, launch another spider!
	bool status = xd->indexDoc();

	// . reset the next doledbkey to start over!
	// . when spiderDoledUrls() see this negative priority it will
	//   reset the doledb scan to the top priority.
	// . MDW, no, 2/25/2015. then the 'goto loop;' statement
	//   basially thinks a round was completed and exits
	//m_sc->m_pri2 = -1;	
	// maybe then try setting to 127
	//m_sc->setPriority ( MAX_SPIDER_PRIORITIES - 1 );

	// if we were injecting and it blocked... return false
	if ( ! status ) return false;

	// deal with this error
	indexedDoc ( xd );

	// "callback" will not be called cuz it should be NULL
	return true;
}

// . the one that was just indexed
// . Msg7.cpp uses this to see what docid the injected doc got so it
//   can forward it to external program
//static int64_t s_lastDocId = -1;
//int64_t SpiderLoop::getLastDocId ( ) { return s_lastDocId; }

void indexedDocWrapper ( void *state ) {
	// . process the results
	// . return if this blocks
	if ( ! g_spiderLoop.indexedDoc ( (XmlDoc *)state ) ) return;
	//a hack to fix injecting urls, because they can
	//run at niceness 0 but most of the spider pipeline
	//cannot.  we should really just make injection run at
	//MAX_NICENESS. OK, done! mdw
	//if ( g_loop.m_inQuickPoll ) return;
	// . continue gettings Spider recs to spider
	// . if it's already waiting for a list it'll just return
	// . mdw: keep your eye on this, it was commented out
	// . this won't execute if we're already getting a list now
	//g_spiderLoop.spiderUrl ( );
	// spider some urls that were doled to us
	g_spiderLoop.spiderDoledUrls( );
}

// . this will delete m_docs[i]
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool SpiderLoop::indexedDoc ( XmlDoc *xd ) {

	// save the error in case a call changes it below
	//int32_t saved = g_errno;

	// get our doc #, i
	//int32_t i = doc - m_docs[0];
	int32_t i = 0;
	for ( ; i < MAX_SPIDERS ; i++ ) if ( m_docs[i] == xd) break;
	// sanity check
	if ( i >= MAX_SPIDERS ) { char *xx=NULL;*xx=0; }
	// set to -1 to indicate inject
	//if ( i < 0 || i >= MAX_SPIDERS ) i = -1;

	//char injecting = false;
	//if ( xd->m_sreq.m_isInjecting ) injecting = true;

	// save it for Msg7.cpp to pass docid of injected doc back 
	//s_lastDocId = xd->m_docId;


	// . decrease m_maxUsed if we need to
	// . we can decrease all the way to -1, which means no spiders going on
	if ( m_maxUsed == i ) {
		m_maxUsed--;
		while ( m_maxUsed >= 0 && ! m_docs[m_maxUsed] ) m_maxUsed--;
	}
	// count it
	m_numSpidersOut--;

	// get coll
	collnum_t collnum = xd->m_collnum;//tiondb.getCollnum ( xd->m_coll );
	// if coll was deleted while spidering, sc will be NULL
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);
	// decrement this
	if ( sc ) sc->m_spidersOut--;
	// get the original request from xmldoc
	SpiderRequest *sreq = &xd->m_sreq;
	// update this. 
	if ( sc ) sc->m_outstandingSpiders[(unsigned char)sreq->m_priority]--;

	// debug log
	//log("XXX: decremented count to %"INT32" for %s",
	//    sc->m_spidersOut,sreq->m_url);
	//if ( sc->m_spidersOut != m_numSpidersOut ) { char *xx=NULL;*xx=0; }

	// breathe
	QUICKPOLL ( xd->m_niceness );

	// are we a re-spider?
	bool respider = false;
	if ( xd->m_oldDocValid && xd->m_oldDoc ) respider = true;

	// . dump it out to a file in the "qatest123" subdir
	// . but only the first time we spider it...
	/*
	if ( ! strcmp(xd->m_coll,"qatest123") && ! respider &&
	     // no longer need this when qa testing spider, not parser
	     g_conf.m_testParserEnabled ) {
		// save the buffers
		//saveTestBuf();
		// get it
		//SafeBuf *pbuf = xd->m_pbuf;
		SafeBuf sb;
		// get it
		xd->printDoc ( &sb );
		// get the first url
		Url *u = xd->getFirstUrl();
		// . get its hash
		// . should be same hash we use to store doc.%"UINT64".html in
		//   XmlDoc.cpp/Msg13.cpp stuff (getTestDoc())
		int64_t h = hash64 ( u->getUrl() , u->getUrlLen() );
		char *testDir = g_test.getTestDir();
		// make filename to dump out to
		char fn[1024]; 
		sprintf(fn,"%s/%s/parse.%"UINT64".%"UINT32".html",
			g_hostdb.m_dir,testDir,h,g_test.m_runId);
		// . dump it out to a file
		// . WATCH OUT. g_errno is set on internal errors, like OOM
		//   or whatever, so don't save in those cases...???????
		sb.dumpToFile ( fn );
		// just dump the <div class=shotdisplay> tags into this file
		sprintf(fn,"%s/%s/parse-int16_tdisplay.%"UINT64".%"UINT32".html",
			g_hostdb.m_dir,testDir,h,g_test.m_runId);
		// output to a special file
		SafeBuf tmp;
		// insert this
		tmp.safeStrcpy("<meta http-equiv=\"Content-Type\" "
			       "content=\"text/html; "
			       "charset=utf-8\">\n");
		// header stuff
		tmp.safePrintf("<html><body>\n");
		// put the onclick script in there
		tmp.safeStrcpy ( xd->getCheckboxScript() );
		// concatenate just these sections in "sb" to "tmp"
		tmp.cat2 ( sb , 
			   "<div class=int16_tdisplay>" ,
			   "</div class=int16_tdisplay>" );
		// header stuff
		tmp.safePrintf("\n</body></html>\n");
		// then dump
		tmp.dumpToFile ( fn );
		// if it had critical errors from XmlDoc::validateOutput()
		// then create that file!
		//if ( xd->m_validateMisses > 0 || xd->m_validateFlagged ) {
		// make the critical file filename
		char cf[1024];
		sprintf (cf,"%s/%s/critical.%"UINT64".%"UINT32".txt",
			 g_hostdb.m_dir,testDir,h,g_test.m_runId);
		// save to that
		ttt.dumpToFile ( cf );
		//char cmd[256];
		//sprintf(cmd,"touch %s/test/critical.%"UINT64".%"UINT32".txt",
		//	g_hostdb.m_dir,h,g_test.m_runId);
		//system(cmd);

		// note it
		//log("crazyin: %s",u->m_url );
		// note it
		//g_test.m_urlsAdded--;
		g_test.m_urlsIndexed++;

		// now in PingServer.cpp for hostid 0 it checks
		// the urlsindexed from each host if g_conf.m_testParserEnabled
		// is true to see if we should call g_test.stopIt()

		// if that is zero we are done
		//if ( g_test.m_urlsAdded == 0 && ! g_test.m_isAdding &&
		//     // only stop if not spidering links
		//     //! g_test.m_spiderLinks )
		//     g_conf.m_testParserEnabled )
		//	// wrap things up
		//	g_test.stopIt();
	}
	*/

	// note it
	// this should not happen any more since indexDoc() will take
	// care of g_errno now by clearing it and adding an error spider
	// reply to release the lock!!
	if ( g_errno ) {
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		log("spider: spidering %s has error: %s. uh48=%"INT64". "
		    //"Respidering "
		    //"in %"INT32" seconds. MAX_LOCK_AGE when lock expires. "
		    "cn=%"INT32"",
		    xd->m_firstUrl.m_url,
		    mstrerror(g_errno),
		    xd->getFirstUrlHash48(),
		    //(int32_t)MAX_LOCK_AGE,
		    (int32_t)collnum);
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// don't release the lock on it right now. just let the
		// lock expire on it after MAX_LOCK_AGE seconds. then it will
		// be retried. we need to debug gb so these things never
		// hapeen...
	}
		
	// breathe
	QUICKPOLL ( xd->m_niceness );

	// . call the final callback used for injecting urls
	// . this may send a reply back so the caller knows the url
	//   was fully injected into the index
	// . Msg7.cpp uses a callback that returns a void, so use m_callback1!
	//if ( xd->m_injectionCallback && injecting ) {
	//	g_errno = saved;
	//	// use the index code as the error for PageInject.cpp
	//	if ( ! g_errno && xd->m_indexCode ) g_errno = xd->m_indexCode;
	//	xd->m_injectionCallback ( xd->m_injectionState );
	//}

	// we don't need this g_errno passed this point
	g_errno = 0;

	// breathe
	QUICKPOLL ( xd->m_niceness );

	// did this doc get a chance to add its meta list to msg4 bufs?
	//bool addedMetaList = m_docs[i]->m_listAdded;

	// set this in case we need to call removeAllLocks
	//m_uh48 = 0LL;
	//if ( xd->m_sreqValid ) m_uh48 = xd->m_sreq.getUrlHash48();

	// we are responsible for deleting doc now
	mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
	delete (m_docs[i]);
	m_docs[i] = NULL;

	// we remove the spider lock from g_spiderLoop.m_lockTable in Rdb.cpp
	// when it receives the negative doledb key. but if the this does not
	// happen, we have a problem then!
	//if ( addedMetaList ) return true;

	// sanity
	//if ( ! m_uh48 ) { char *xx=NULL; *xx=0; }

	// the lock we had in g_spiderLoop.m_lockTable for the doleKey
	// is now remove in Rdb.cpp when it receives a negative dole key to
	// add to doledb... assuming we added that meta list!!
	// m_uh48 should be set from above
	//if ( ! removeAllLocks () ) return false;

	// we did not block, so return true
	return true;
}


void gotLockReplyWrapper ( void *state , UdpSlot *slot ) {
	// cast it
	Msg12 *msg12 = (Msg12 *)state;
	// . call handler
	// . returns false if waiting for more replies to come in
	if ( ! msg12->gotLockReply ( slot ) ) return;
	// if had callback, maybe from PageReindex.cpp
	if ( msg12->m_callback ) msg12->m_callback ( msg12->m_state );
	// ok, try to get another url to spider
	else                     g_spiderLoop.spiderDoledUrls();
}

Msg12::Msg12 () {
	m_numRequests = 0;
	m_numReplies  = 0;
}

// . returns false if blocked, true otherwise.
// . returns true and sets g_errno on error
// . before we can spider for a SpiderRequest we must be granted the lock
// . each group shares the same doledb and each host in the group competes
//   for spidering all those urls. 
// . that way if a host goes down is load is taken over
bool Msg12::getLocks ( int64_t uh48, // probDocId , 
		       char *url ,
		       DOLEDBKEY *doledbKey,
		       collnum_t collnum,
		       int32_t sameIpWaitTime,
		       int32_t maxSpidersOutPerIp,
		       int32_t firstIp,
		       void *state ,
		       void (* callback)(void *state) ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// do not use locks for injections
	//if ( m_sreq->m_isInjecting ) return true;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// reset
	m_numRequests = 0;
	m_numReplies  = 0;
	m_grants   = 0;
	m_removing = false;
	m_confirming = false;
	// make sure is really docid
	//if ( probDocId & ~DOCID_MASK ) { char *xx=NULL;*xx=0; }
	// . mask out the lower bits that may change if there is a collision
	// . in this way a url has the same m_probDocId as the same url
	//   in the index. i.e. if we add a new spider request for url X and
	//   url X is already indexed, then they will share the same lock 
	//   even though the indexed url X may have a different actual docid
	//   than its probable docid.
	// . we now use probable docids instead of uh48 because query reindex
	//   in PageReindex.cpp adds docid based spider requests and we
	//   only know the docid, not the uh48 because it is creating
	//   SpiderRequests from docid-only search results. having to look
	//   up the msg20 summary for like 1M search results is too painful!
	//m_lockKey = g_titledb.getFirstProbableDocId(probDocId);
	// . use this for locking now, and let the docid-only requests just use
	//   the docid
	m_lockKeyUh48 = makeLockTableKey ( uh48 , firstIp );
	m_url = url;
	m_callback = callback;
	m_state = state;
	m_hasLock = false;
	m_origUh48 = uh48;
	// support ability to spider multiple urls from same ip
	m_doledbKey = *doledbKey;
	m_collnum = collnum;
	m_sameIpWaitTime = sameIpWaitTime;
	m_maxSpidersOutPerIp = maxSpidersOutPerIp;
	m_firstIp = firstIp;

	// sanity check, just 6 bytes! (48 bits)
	if ( uh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }

	if ( m_lockKeyUh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }

	// cache time
	int32_t ct = 120;

	// if docid based assume it was a query reindex and keep it int16_t!
	// otherwise we end up waiting 120 seconds for a query reindex to
	// go through on a docid we just spidered. TODO: use m_urlIsDocId
	// MDW: check this out
	if ( url && is_digit(url[0]) ) ct = 2;

	// . this seems to be messing us up and preventing us from adding new
	//   requests into doledb when only spidering a few IPs.
	// . make it random in the case of twin contention
	ct = rand() % 10;

	// . check our cache to avoid repetitive asking
	// . use -1 for maxAge to indicate no max age
	// . returns -1 if not in cache
	// . use maxage of two minutes, 120 seconds
	int32_t lockTime ;
	lockTime = g_spiderLoop.m_lockCache.getLong(0,m_lockKeyUh48,ct,true);
	// if it was in the cache and less than 2 minutes old then return
	// true now with m_hasLock set to false.
	if ( lockTime >= 0 ) {
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: cached missed lock for %s "
			     "lockkey=%"UINT64"", m_url,m_lockKeyUh48);
		return true;
	}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: sending lock request for %s "
		     "lockkey=%"UINT64"",  m_url,m_lockKeyUh48);

	// now the locking group is based on the probable docid
	//m_lockGroupId = g_hostdb.getGroupIdFromDocId(m_lockKey);
	// ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( m_lockGroupId );
	// the same group (shard) that has the spiderRequest/Reply is
	// the one responsible for locking.
	Host *hosts = g_hostdb.getMyShard();

	// int16_t cut
	UdpServer *us = &g_udpServer;


	static int32_t s_lockSequence = 0;
	// remember the lock sequence # in case we have to call remove locks
	m_lockSequence = s_lockSequence++;

	LockRequest *lr = &m_lockRequest;
	lr->m_lockKeyUh48 = m_lockKeyUh48;
	lr->m_firstIp = m_firstIp;
	lr->m_removeLock = 0;
	lr->m_lockSequence = m_lockSequence;
	lr->m_collnum = collnum;

	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;

	// point to start of the 12 byte request buffer
	char *request = (char *)lr;//m_lockKey;
	int32_t  requestSize = sizeof(LockRequest);//12;

	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead (h) ) continue;
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: sent lock "
			     "request #%"INT32" for lockkey=%"UINT64" %s to "
			     "hid=%"INT32"",m_numRequests,m_lockKeyUh48,
			     m_url,h->m_hostId);
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 60*60*24*365    ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// i guess nothing... hmmm... all dead?
	//char *xx=NULL; *xx=0; 
	// m_hasLock should be false... all lock hosts seem dead... wait
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: all lock hosts seem dead for %s "
		     "lockkey=%"UINT64"", m_url,m_lockKeyUh48);
	return true;
}

// after adding the negative doledb recs to remove the url we are spidering
// from doledb, and adding the fake titledb rec to add a new entry into
// waiting tree so that our ip can have more than one outstanding spider,
// call the callback. usually msg4::addMetaList() will not block i'd guess.
void rejuvenateIPWrapper ( void *state ) {
	Msg12 *THIS = (Msg12 *)state;
	THIS->m_callback ( THIS->m_state );
}

// returns true if all done, false if waiting for more replies
bool Msg12::gotLockReply ( UdpSlot *slot ) {

	// no longer use this
	char *xx=NULL;*xx=0;

	// got reply
	m_numReplies++;
	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;
	// check for a hammer reply
	char *reply     = slot->m_readBuf;
	int32_t  replySize = slot->m_readBufSize;
	// if error, treat as a not grant
	if ( g_errno ) {
		bool logIt = true;
		// note it
		if ( g_conf.m_logDebugSpider )
			log("spider: got msg12 reply error = %s",
			    mstrerror(g_errno));
		// if we got an ETRYAGAIN when trying to confirm our lock
		// that means doledb was saving/dumping to disk and we 
		// could not remove the record from doledb and add an
		// entry to the waiting tree, so we need to keep trying
		if ( g_errno == ETRYAGAIN && m_confirming ) {
			// c ount it again
			m_numRequests++;
			// use what we were using
			char *request     = (char *)&m_confirmRequest;
			int32_t  requestSize = sizeof(ConfirmRequest);
			Host *h = g_hostdb.getHost(slot->m_hostId);
			// send request to him
			UdpServer *us = &g_udpServer;
			if ( ! us->sendRequest ( request      ,
						 requestSize  ,
						 0x12         , // msgType
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL         , // retSlotPtrPt
						 this         , // state data
						 gotLockReplyWrapper ,
						 60*60*24*365    ) ) 
				return false;
			// error?
			// don't spam the log!
			static int32_t s_last = 0;
			int32_t now = getTimeLocal();
			if ( now - s_last >= 1 ) {
				s_last = now;
				log("spider: error re-sending confirm "
				    "request: %s",  mstrerror(g_errno));
			}
		}
		// only log every 10 seconds for ETRYAGAIN
		if ( g_errno == ETRYAGAIN ) {
			static time_t s_lastTime = 0;
			time_t now = getTimeLocal();
			logIt = false;
			if ( now - s_lastTime >= 3 ) {
				logIt = true;
				s_lastTime = now;
			}
		}
		if ( logIt )
			log ( "sploop: host had error getting lock url=%s"
			      ": %s" ,
			      m_url,mstrerror(g_errno) );
	}
	// grant or not
	if ( replySize == 1 && ! g_errno && *reply == 1 ) m_grants++;
	// wait for all to get back
	if ( m_numReplies < m_numRequests ) return false;
	// all done if we were removing
	if ( m_removing ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: done removing all locks "
			   "(replies=%"INT32") for %s",
			   m_numReplies,m_url);//m_sreq->m_url);
		// we are done
		m_gettingLocks = false;
		return true;
	}
	// all done if we were confirming
	if ( m_confirming ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: done confirming all locks "
			   "for %s uh48=%"INT64"",m_url,m_origUh48);//m_sreq->m_url);
		// we are done
		m_gettingLocks = false;
		// . keep processing
		// . if the collection was nuked from under us the spiderUrl2
		//   will return true and set g_errno
		if ( ! m_callback ) return g_spiderLoop.spiderUrl2();
		// if we had a callback let our parent call it
		return true;
	}

	// if got ALL locks, spider it
	if ( m_grants == m_numReplies ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: got lock for docid=lockkey=%"UINT64"",
			   m_lockKeyUh48);
		// flag this
		m_hasLock = true;
		// we are done
		//m_gettingLocks = false;


		///////
		//
		// now tell our group (shard) to remove from doledb
		// and re-add to waiting tree. the evalIpLoop() function
		// should skip this probable docid because it is in the 
		// LOCK TABLE!
		//
		// This logic should allow us to spider multiple urls
		// from the same IP at the same time.
		//
		///////

		// returns false if would block
		if ( ! confirmLockAcquisition ( ) ) return false;
		// . we did it without blocking, maybe cuz we are a single node
		// . ok, they are all back, resume loop
		// . if the collection was nuked from under us the spiderUrl2
		//   will return true and set g_errno
		if ( ! m_callback ) g_spiderLoop.spiderUrl2 ( );
		// all done
		return true;

	}
	// note it
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: missed lock for %s lockkey=%"UINT64" "
		     "(grants=%"INT32")",   m_url,m_lockKeyUh48,m_grants);

	// . if it was locked by another then add to our lock cache so we do
	//   not try to lock it again
	// . if grants is not 0 then one host granted us the lock, but not
	//   all hosts, so we should probably keep trying on it until it is
	//   locked up by one host
	if ( m_grants == 0 ) {
		int32_t now = getTimeGlobal();
		g_spiderLoop.m_lockCache.addLong(0,m_lockKeyUh48,now,NULL);
	}

	// reset again
	m_numRequests = 0;
	m_numReplies  = 0;
	// no need to remove them if none were granted because another
	// host in our group might have it 100% locked. 
	if ( m_grants == 0 ) {
		// no longer in locks operation mode
		m_gettingLocks = false;
		// ok, they are all back, resume loop
		//if ( ! m_callback ) g_spiderLoop.spiderUrl2 ( );
		// all done
		return true;
	}
	// note that
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: sending request to all in shard to "
		     "remove lock uh48=%"UINT64". grants=%"INT32"",
		     m_lockKeyUh48,(int32_t)m_grants);
	// remove all locks we tried to get, BUT only if from our hostid!
	// no no! that doesn't quite work right... we might be the ones
	// locking it! i.e. another one of our spiders has it locked...
	if ( ! removeAllLocks ( ) ) return false; // true;
	// if did not block, how'd that happen?
	log("sploop: did not block in removeAllLocks: %s",mstrerror(g_errno));
	return true;
}

bool Msg12::removeAllLocks ( ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// skip if injecting
	//if ( m_sreq->m_isInjecting ) return true;
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: removing all locks for %s %"UINT64"",
		     m_url,m_lockKeyUh48);
	// we are now removing 
	m_removing = true;

	LockRequest *lr = &m_lockRequest;
	lr->m_lockKeyUh48 = m_lockKeyUh48;
	lr->m_lockSequence = m_lockSequence;
	lr->m_firstIp = m_firstIp;
	lr->m_removeLock = 1;

	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;

	// make that the request
	// . point to start of the 12 byte request buffer
	// . m_lockSequence should still be valid
	char *request     = (char *)lr;//m_lockKey;
	int32_t  requestSize = sizeof(LockRequest);//12;

	// now the locking group is based on the probable docid
	//uint32_t groupId = g_hostdb.getGroupIdFromDocId(m_lockKeyUh48);
	// ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( groupId );
	Host *hosts = g_hostdb.getMyShard();
	// this must select the same group that is going to spider it!
	// i.e. our group! because we check our local lock table to see
	// if a doled url is locked before spidering it ourselves.
	//Host *hosts = g_hostdb.getMyGroup();
	// int16_t cut
	UdpServer *us = &g_udpServer;
	// set the hi bit though for this one
	//m_lockKey |= 0x8000000000000000LL;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead ( h ) ) continue;
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 60*60*24*365    ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// did not block
	return true;
}

bool Msg12::confirmLockAcquisition ( ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// we are now removing 
	m_confirming = true;

	// make that the request
	// . point to start of the 12 byte request buffer
	// . m_lockSequence should still be valid
	ConfirmRequest *cq = &m_confirmRequest;
	char *request     = (char *)cq;
	int32_t  requestSize = sizeof(ConfirmRequest);
	// sanity
	if ( requestSize == sizeof(LockRequest)){ char *xx=NULL;*xx=0; }
	// set it
	cq->m_collnum   = m_collnum;
	cq->m_doledbKey = m_doledbKey;
	cq->m_firstIp   = m_firstIp;
	cq->m_lockKeyUh48 = m_lockKeyUh48;
	cq->m_maxSpidersOutPerIp = m_maxSpidersOutPerIp;
	// . use the locking group from when we sent the lock request
	// . get ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( m_lockGroupId );
	// the same group (shard) that has the spiderRequest/Reply is
	// the one responsible for locking.
	Host *hosts = g_hostdb.getMyShard();
	// this must select the same shard that is going to spider it!
	// i.e. our shard! because we check our local lock table to see
	// if a doled url is locked before spidering it ourselves.
	//Host *hosts = g_hostdb.getMyShard();
	// int16_t cut
	UdpServer *us = &g_udpServer;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;
	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: confirming lock for uh48=%"UINT64" firstip=%s",
		    m_lockKeyUh48,iptoa(m_firstIp));
	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead ( h ) ) continue;
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 // a size of 2 should mean confirm
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 60*60*24*365    ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// did not block
	return true;
}

// use -1 for any collnum
int32_t SpiderLoop::getNumSpidersOutPerIp ( int32_t firstIp , collnum_t collnum ) {
	int32_t count = 0;
	// count locks
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// scan the slots
	int32_t ns = ht->m_numSlots;
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// breathe
		//QUICKPOLL(niceness);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		// skip if not outstanding, just a 5-second expiration wait
		// when the spiderReply returns, so that in case a lock
		// request for the same url was in progress, it will be denied.
		if ( ! lock->m_spiderOutstanding ) continue;
		// must be confirmed too
		if ( ! lock->m_confirmed ) continue;
		// correct collnum?
		if ( lock->m_collnum != collnum && collnum != -1 ) continue;
		// skip if not yet expired
		if ( lock->m_firstIp == firstIp ) count++;
	}
	/*
	for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
		// get it
		XmlDoc *xd = m_docs[i];
		// skip if empty
		if ( ! xd ) continue;
		// check it
		if ( xd->m_firstIp == firstIp ) count++;
	}
	*/
	return count;
}

void handleRequest12 ( UdpSlot *udpSlot , int32_t niceness ) {
	// get request
	char *request = udpSlot->m_readBuf;
	int32_t  reqSize = udpSlot->m_readBufSize;
	// int16_t cut
	UdpServer *us = &g_udpServer;
	// breathe
	QUICKPOLL ( niceness );

	// int16_tcut
	char *reply = udpSlot->m_tmpBuf;

	//
	// . is it confirming that he got all the locks?
	// . if so, remove the doledb record and dock the doleiptable count
	//   before adding a waiting tree entry to re-pop the doledb record
	//
	if ( reqSize == sizeof(ConfirmRequest) ) {
		char *msg = NULL;
		ConfirmRequest *cq = (ConfirmRequest *)request;

		// confirm the lock
		HashTableX *ht = &g_spiderLoop.m_lockTable;
		int32_t slot = ht->getSlot ( &cq->m_lockKeyUh48 );
		if ( slot < 0 ) { 
			log("spider: got a confirm request for a key not "
			    "in the table! coll must have been deleted "
			    " or reset "
			    "while lock request was outstanding.");
			g_errno = EBADENGINEER;
			us->sendErrorReply ( udpSlot , g_errno );
			return;
			//char *xx=NULL;*xx=0; }
		}
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot ( slot );
		lock->m_confirmed = true;

		// note that
		if ( g_conf.m_logDebugSpider ) // Wait )
			log("spider: got confirm lock request for ip=%s",
			    iptoa(lock->m_firstIp));

		// get it
		SpiderColl *sc = g_spiderCache.getSpiderColl(cq->m_collnum);
		// make it negative
		cq->m_doledbKey.n0 &= 0xfffffffffffffffeLL;
		// and add the negative rec to doledb (deletion operation)
		Rdb *rdb = &g_doledb.m_rdb;
		if ( ! rdb->addRecord ( cq->m_collnum,
					(char *)&cq->m_doledbKey,
					NULL , // data
					0    , //dataSize
					1 )){ // niceness
			// tree is dumping or something, probably ETRYAGAIN
			if ( g_errno != ETRYAGAIN ) {msg = "error adding neg rec to doledb";	log("spider: %s %s",msg,mstrerror(g_errno));
			}
			//char *xx=NULL;*xx=0;
			us->sendErrorReply ( udpSlot , g_errno );
			return;
		}
		// now remove from doleiptable since we removed from doledb
		if ( sc ) sc->removeFromDoledbTable ( cq->m_firstIp );

		// how many spiders outstanding for this coll and IP?
		//int32_t out=g_spiderLoop.getNumSpidersOutPerIp ( cq->m_firstIp);

		// DO NOT add back to waiting tree if max spiders
		// out per ip was 1 OR there was a crawldelay. but better
		// yet, take care of that in the winReq code above.

		// . now add to waiting tree so we add another spiderdb
		//   record for this firstip to doledb
		// . true = callForScan
		// . do not add to waiting tree if we have enough outstanding
		//   spiders for this ip. we will add to waiting tree when
		//   we receive a SpiderReply in addSpiderReply()
		if ( sc && //out < cq->m_maxSpidersOutPerIp &&
		     // this will just return true if we are not the 
		     // responsible host for this firstip
		    // DO NOT populate from this!!! say "false" here...
		     ! sc->addToWaitingTree ( 0 , cq->m_firstIp, false ) &&
		     // must be an error...
		     g_errno ) {
			msg = "FAILED TO ADD TO WAITING TREE";
			log("spider: %s %s",msg,mstrerror(g_errno));
			us->sendErrorReply ( udpSlot , g_errno );
			return;
		}
		// success!!
		reply[0] = 1;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}



	// sanity check
	if ( reqSize != sizeof(LockRequest) ) {
		log("spider: bad msg12 request size of %"INT32"",reqSize);
		us->sendErrorReply ( udpSlot , EBADREQUEST );
		return;
	}
	// deny it if we are not synced yet! otherwise we core in 
	// getTimeGlobal() below
	if ( ! isClockInSync() ) { 
		// log it so we can debug it
		//log("spider: clock not in sync with host #0. so "
		//    "returning etryagain for lock reply");
		// let admin know why we are not spidering
		us->sendErrorReply ( udpSlot , ETRYAGAIN );
		return;
	}

	LockRequest *lr = (LockRequest *)request;
	//uint64_t lockKey = *(int64_t *)request;
	//int32_t lockSequence = *(int32_t *)(request+8);
	// is this a remove operation? assume not
	//bool remove = false;
	// get top bit
	//if ( lockKey & 0x8000000000000000LL ) remove = true;

	// mask it out
	//lockKey &= 0x7fffffffffffffffLL;
	// sanity check, just 6 bytes! (48 bits)
	if ( lr->m_lockKeyUh48 &0xffff000000000000LL ) { char *xx=NULL;*xx=0; }
	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: got msg12 request uh48=%"INT64" remove=%"INT32"",
		    lr->m_lockKeyUh48, (int32_t)lr->m_removeLock);
	// get time
	int32_t nowGlobal = getTimeGlobal();
	// int16_tcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

	int32_t hostId = g_hostdb.getHostId ( udpSlot->m_ip , udpSlot->m_port );
	// this must be legit - sanity check
	if ( hostId < 0 ) { char *xx=NULL;*xx=0; }

	// remove expired locks from locktable
	removeExpiredLocks ( hostId );

	int64_t lockKey = lr->m_lockKeyUh48;

	// check tree
	int32_t slot = ht->getSlot ( &lockKey ); // lr->m_lockKeyUh48 );
	// put it here
	UrlLock *lock = NULL;
	// if there say no no
	if ( slot >= 0 ) lock = (UrlLock *)ht->getValueFromSlot ( slot );

	// if doing a remove operation and that was our hostid then unlock it
	if ( lr->m_removeLock && 
	     lock && 
	     lock->m_hostId == hostId &&
	     lock->m_lockSequence == lr->m_lockSequence ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock for lockkey=%"UINT64" hid=%"INT32"",
			    lr->m_lockKeyUh48,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// ok, at this point all remove ops return
	if ( lr->m_removeLock ) {
		reply[0] = 1;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}

	/////////
	//
	// add new lock
	//
	/////////


	// if lock > 1 hour old then remove it automatically!!
	if ( lock && nowGlobal - lock->m_timestamp > MAX_LOCK_AGE ) {
		// note it for now
		log("spider: removing lock after %"INT32" seconds "
		    "for lockKey=%"UINT64" hid=%"INT32"",
		    (nowGlobal - lock->m_timestamp),
		    lr->m_lockKeyUh48,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// if lock still there, do not grant another lock
	if ( lock ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: refusing lock for lockkey=%"UINT64" hid=%"INT32"",
			    lr->m_lockKeyUh48,hostId);
		reply[0] = 0;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}
	// make the new lock
	UrlLock tmp;
	tmp.m_hostId       = hostId;
	tmp.m_lockSequence = lr->m_lockSequence;
	tmp.m_timestamp    = nowGlobal;
	tmp.m_expires      = 0;
	tmp.m_firstIp      = lr->m_firstIp;
	tmp.m_collnum      = lr->m_collnum;

	// when the spider returns we remove its lock on reception of the
	// spiderReply, however, we actually just set the m_expires time
	// to 5 seconds into the future in case there is a current request
	// to get a lock for that url in progress. but, we do need to
	// indicate that the spider has indeed completed by setting
	// m_spiderOutstanding to true. this way, addToWaitingTree() will
	// not count it towards a "max spiders per IP" quota when deciding
	// on if it should add a new entry for this IP.
	tmp.m_spiderOutstanding = true;
	// this is set when all hosts in the group (shard) have granted the
	// lock and the host sends out a confirmLockAcquisition() request.
	// until then we do not know if the lock will be granted by all hosts
	// in the group (shard)
	tmp.m_confirmed    = false;

	// put it into the table
	if ( ! ht->addKey ( &lockKey , &tmp ) ) {
		// return error if that failed!
		us->sendErrorReply ( udpSlot , g_errno );
		return;
	}
	// note it for now
	if ( g_conf.m_logDebugSpider )
		log("spider: granting lock for lockKey=%"UINT64" hid=%"INT32"",
		    lr->m_lockKeyUh48,hostId);
	// grant the lock
	reply[0] = 1;
	us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
	return;
}

// hostId is the remote hostid sending us the lock request
void removeExpiredLocks ( int32_t hostId ) {
	// when we last cleaned them out
	static time_t s_lastTime = 0;

	int32_t nowGlobal = getTimeGlobalNoCore();
	int32_t niceness = MAX_NICENESS;

	// only do this once per second at the most
	if ( nowGlobal <= s_lastTime ) return;

	// int16_tcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

 restart:

	// scan the slots
	int32_t ns = ht->m_numSlots;
	// . clean out expired locks...
	// . if lock was there and m_expired is up, then nuke it!
	// . when Rdb.cpp receives the "fake" title rec it removes the
	//   lock, only it just sets the m_expired to a few seconds in the
	//   future to give the negative doledb key time to be absorbed.
	//   that way we don't repeat the same url we just got done spidering.
	// . this happens when we launch our lock request on a url that we
	//   or a twin is spidering or has just finished spidering, and
	//   we get the lock, but we avoided the negative doledb key.
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		int64_t lockKey = *(int64_t *)ht->getKeyFromSlot(i);
		// if collnum got deleted or reset
		collnum_t collnum = lock->m_collnum;
		if ( collnum >= g_collectiondb.m_numRecs ||
		     ! g_collectiondb.m_recs[collnum] ) {
			log("spider: removing lock from missing collnum "
			    "%"INT32"",(int32_t)collnum);
			goto nuke;
		}
		// skip if not yet expired
		if ( lock->m_expires == 0 ) continue;
		if ( lock->m_expires >= nowGlobal ) continue;
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock after waiting. elapsed=%"INT32"."
			    " lockKey=%"UINT64" hid=%"INT32" expires=%"UINT32" "
			    "nowGlobal=%"UINT32"",
			    (nowGlobal - lock->m_timestamp),
			    lockKey,hostId,
			    (uint32_t)lock->m_expires,
			    (uint32_t)nowGlobal);
	nuke:
		// nuke the slot and possibly re-chain
		ht->removeSlot ( i );
		// gotta restart from the top since table may have shrunk
		goto restart;
	}
	// store it
	s_lastTime = nowGlobal;
}		

/////////////////////////
/////////////////////////      PAGESPIDER
/////////////////////////

// don't change name to "State" cuz that might conflict with another
class State11 {
public:
	int32_t          m_numRecs;
	Msg5          m_msg5;
	RdbList       m_list;
	TcpSocket    *m_socket;
	HttpRequest   m_r;
	collnum_t     m_collnum;
	char         *m_coll;
	int32_t          m_count;
	key_t         m_startKey;
	key_t         m_endKey;
	int32_t          m_minRecSizes;
	bool          m_done;
	SafeBuf       m_safeBuf;
	int32_t          m_priority;
};

static bool loadLoop ( class State11 *st ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the urls we got in doledb
// . doledb is sorted by priority complement then spider time
// . do not show urls in doledb whose spider time has not yet been reached,
//   so only show the urls spiderable now
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSpiderdb ( TcpSocket *s , HttpRequest *r ) {
	// set up a msg5 and RdbLists to get the urls from spider queue
	State11 *st ;
	try { st = new (State11); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageSpiderdb: new(%i): %s", 
		    (int)sizeof(State11),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State11) , "PageSpiderdb" );
	// get the priority/#ofRecs from the cgi vars
	st->m_numRecs  = r->getLong ("n", 20  );
	st->m_r.copy ( r );
	// get collection name
	char *coll = st->m_r.getString ( "c" , NULL , NULL );
	// get the collection record to see if they have permission
	//CollectionRec *cr = g_collectiondb.getRec ( coll );

	// the socket read buffer will remain until the socket is destroyed
	// and "coll" points into that
	st->m_coll = coll;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;
	// set socket for replying in case we block
	st->m_socket = s;
	st->m_count = 0;
	st->m_priority = MAX_SPIDER_PRIORITIES - 1;
	// get startKeys/endKeys/minRecSizes
	st->m_startKey    = g_doledb.makeFirstKey2 (st->m_priority);
	st->m_endKey      = g_doledb.makeLastKey2  (st->m_priority);
	st->m_minRecSizes = 20000;
	st->m_done        = false;
	// returns false if blocked, true otherwise
	return loadLoop ( st ) ;
}

static void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) ;
static bool sendPage        ( State11 *st );
static bool printList       ( State11 *st );

bool loadLoop ( State11 *st ) {
 loop:
	// let's get the local list for THIS machine (use msg5)
	if ( ! st->m_msg5.getList  ( RDB_DOLEDB          ,
				     st->m_collnum       ,
				     &st->m_list         ,
				     st->m_startKey      ,
				     st->m_endKey        ,
				     st->m_minRecSizes   ,
				     true                , // include tree
				     false               , // add to cache
				     0                   , // max age
				     0                   , // start file #
				     -1                  , // # files
				     st                  , // callback state
				     gotListWrapper3     ,
				     0                   , // niceness
				     true               )) // do err correction
		return false;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return true;
	}
	// otherwise, load more
	goto loop;
}

void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// cast it
	State11 *st = (State11 *)state;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return;
	}
	// otherwise, load more
	loadLoop( (State11 *)state );
}


// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool printList ( State11 *st ) {
	// useful
	time_t nowGlobal ;
	if ( isClockInSync() ) nowGlobal = getTimeGlobal();
	else                   nowGlobal = getTimeLocal();
	// print the spider recs we got
	SafeBuf *sbTable = &st->m_safeBuf;
	// shorcuts
	RdbList *list = &st->m_list;
	// row count
	int32_t j = 0;
	// put it in there
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		// stop if we got enough
		if ( st->m_count >= st->m_numRecs )  break;
		// get the doledb key
		key_t dk = list->getCurrentKey();
		// update to that
		st->m_startKey = dk;
		// inc by one
		st->m_startKey += 1;
		// get spider time from that
		int32_t spiderTime = g_doledb.getSpiderTime ( &dk );
		// skip if in future
		if ( spiderTime > nowGlobal ) continue;
		// point to the spider request *RECORD*
		char *rec = list->getCurrentData();
		// skip negatives
		if ( (dk.n0 & 0x01) == 0 ) continue;
		// count it
		st->m_count++;
		// what is this?
		if ( list->getCurrentRecSize() <= 16 ) { char *xx=NULL;*xx=0;}
		// sanity check. requests ONLY in doledb
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec )) {
			log("spider: not printing spiderreply");
			continue;
			//char*xx=NULL;*xx=0;
		}
		// get the spider rec, encapsed in the data of the doledb rec
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// print it into sbTable
		if ( ! sreq->printToTable ( sbTable,"ready",NULL,j))
			return false;
		// count row
		j++;
	}
	// need to load more?
	if ( st->m_count >= st->m_numRecs ||
	     // if list was a partial, this priority is int16_t then
	     list->getListSize() < st->m_minRecSizes ) {
		// . try next priority
		// . if below 0 we are done
		if ( --st->m_priority < 0 ) st->m_done = true;
		// get startKeys/endKeys/minRecSizes
		st->m_startKey    = g_doledb.makeFirstKey2 (st->m_priority);
		st->m_endKey      = g_doledb.makeLastKey2  (st->m_priority);
		// if we printed something, print a blank line after it
		if ( st->m_count > 0 )
			sbTable->safePrintf("<tr><td colspan=30>..."
					    "</td></tr>\n");
		// reset for each priority
		st->m_count = 0;
	}


	return true;
}

bool sendPage ( State11 *st ) {
	// sanity check
	//if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	//SafeBuf sb; sb.safePrintf("Error = %s",mstrerror(g_errno));

	// int16_tcut
	SafeBuf *sbTable = &st->m_safeBuf;

	// generate a query string to pass to host bar
	char qs[64]; sprintf ( qs , "&n=%"INT32"", st->m_numRecs );

	// store the page in here!
	SafeBuf sb;
	sb.reserve ( 64*1024 );

	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r , qs );


	// get spider coll
	collnum_t collnum = g_collectiondb.getCollnum ( st->m_coll );
	// and coll rec
	CollectionRec *cr = g_collectiondb.getRec ( collnum );

	if ( ! cr ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
						     sb.length() );
	}

	// print reason why spiders are not active for this collection
	int32_t tmp2;
	SafeBuf mb;
	if ( cr ) getSpiderStatusMsg ( cr , &mb , &tmp2 );
	if ( mb.length() && tmp2 != SP_INITIALIZING )
		sb.safePrintf(//"<center>"
			      "<table cellpadding=5 "
			      //"style=\""
			      //"border:2px solid black;"
			      "max-width:600px\" "
			      "border=0"
			      ">"
			      "<tr>"
			      //"<td bgcolor=#ff6666>"
			      "<td>"
			      "For collection <i>%s</i>: "
			      "<b><font color=red>%s</font></b>"
			      "</td>"
			      "</tr>"
			      "</table>\n"
			      , cr->m_coll
			      , mb.getBufStart() );


	// begin the table
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			//"<center>"
			"<b>Currently Spidering on This Host</b>"
			" (%"INT32" spiders)"
			//" (%"INT32" locks)"
			//"</center>"
			"</td></tr>\n"
			, TABLE_STYLE
			, (int32_t)g_spiderLoop.m_numSpidersOut
			//, g_spiderLoop.m_lockTable.m_numSlotsUsed
			);
	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb , true ) ) return false;
	// int16_tcut
	XmlDoc **docs = g_spiderLoop.m_docs;
	// count # of spiders out
	int32_t j = 0;
	// first print the spider recs we are spidering
	for ( int32_t i = 0 ; i < (int32_t)MAX_SPIDERS ; i++ ) {
		// get it
		XmlDoc *xd = docs[i];
		// skip if empty
		if ( ! xd ) continue;
		// sanity check
		if ( ! xd->m_sreqValid ) { char *xx=NULL;*xx=0; }
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		char *status = xd->m_statusMsg;
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}
	// now print the injections as well!
	XmlDoc *xd = getInjectHead ( ) ;
	for ( ; xd ; xd = xd->m_nextInject ) {
		// how does this happen?
		if ( ! xd->m_sreqValid ) continue;
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		SafeBuf xb;
		xb.safePrintf("[<font color=red><b>injecting</b></font>] %s",
			      xd->m_statusMsg);
		char *status = xb.getBufStart();
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}

	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );


	/*
	if ( g_spiderCache.m_numMsgSamples > 0 ) {
		sb.safePrintf ( 
			       "<table width=100%% bgcolor=#%s "
			       "cellpadding=4 border=1 >"
			       "<tr>"
			       "<td colspan=3 bgcolor=#%s>"
			       "<b>Proportion of Spider Time Spent in "
			       "Section.</b>"
			       "</td>"
			       "</tr>\n",
			       LIGHT_BLUE ,
			       DARK_BLUE  );
		HashTableT<uint32_t,float>* m = &g_spiderCache.m_spiderMsgs;
		for(int32_t i = 0; i < m->getNumSlots();i++) {
			if(m->getKey(i) == 0) continue;
			sb.safePrintf ( 
				       "<tr>"
				       "<td>%.2f%%</td>"
				       "<td>%.0f</td>"
				       "<td>%s</td>"
				       "</tr>\n",
				       100*m->getValueFromSlot(i)/
				       g_spiderCache.m_numMsgSamples,
				       m->getValueFromSlot(i),
				       (char*)m->getKey(i));
		}
		sb.safePrintf ("</table>\n");
	}
	*/

	/*

	  // try to put these in tool tips

	// describe the various parms
	sb.safePrintf ( 
		       "<table width=100%% bgcolor=#%s "
		       "cellpadding=4 border=1>"
		       "<tr>"
		       "<td colspan=2 bgcolor=#%s>"
		       "<b>Status descriptions</b>"
		       "</td>"
		       "</tr>\n"

		       "<tr>"
		       //"<td>getting link info</td><td>performing "
		       "<td>getting site title buf</td><td>getting "
		       "the title and all inlinker text of the root page."
		       "</td></tr>"

		       "<tr>"
		       "<td>getting outlink ip vector</td><td>getting "
		       "ips of the outlinks. Gets from tagdb firstip "
		       "tag if it exists."
		       "</td></tr>"

		       "<tr>"
		       "<td>getting robots.txt</td><td>downloading the "
		       "robots.txt file for this url."
		       "</td></tr>"


		       "<tr>"
		       "<td>checking for dup</td><td>looking up the url's "
		       "docid in checksumdb to see if its content checksum "
		       "is in use by another indexed document from the same "
		       "site. Will index even if it is a dup if it has a "
		       "higher quality."
		       "</td></tr>"

		       "<tr>"
		       "<td>getting web page</td><td>downloading the web "
		       "page."
		       "</td></tr>"

		       "<tr>"
		       "<td><nobr>getting cached web page</nobr></td><td>"
		       "looking up the "
		       "old record for this url in titledb to see how the "
		       "content changed."
		       "</td></tr>"

		       "<tr>"
		       "<td>adding links</td><td>adding links from the page "
		       "to spiderdb. Links are distributed to the host that "
		       "stores them based on the hash of the link. Make sure "
		       "&lt;tfndbMaxPageCacheMem&gt; is high enough to keep "
		       "tfndb disk seeks down. A tfndb access is done for "
		       "every link added."
		       "</td></tr>"

		       "</table><br><br>\n\n",

		       LIGHT_BLUE ,
		       DARK_BLUE  );
	*/


	// then spider collection
	//SpiderColl *sc = g_spiderCache.m_spiderColls[collnum];
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);


	//
	// spiderdb rec stats, from scanning spiderdb
	//

	// if not there, forget about it
	if ( sc ) sc->printStats ( sb );

	//
	// Spiders Table
	//
	int64_t totalPoints = g_stats.m_totalSpiderSuccessNew +
				g_stats.m_totalSpiderErrorsNew +
				g_stats.m_totalSpiderSuccessOld +
				g_stats.m_totalSpiderErrorsOld;
	int64_t totalNew = g_stats.m_totalSpiderSuccessNew +
			     g_stats.m_totalSpiderErrorsNew;
	int64_t totalOld = g_stats.m_totalSpiderSuccessOld +
			     g_stats.m_totalSpiderErrorsOld;
	double tsr = 100.00;
	double nsr = 100.00;
	double osr = 100.00;
	if ( totalPoints > 0 ) {
		tsr = 100.00*
			(double)(g_stats.m_totalSpiderSuccessNew +
				 g_stats.m_totalSpiderSuccessOld) /
			(double)totalPoints;
		if ( totalNew > 0 )
			nsr= 100.00*(double)(g_stats.m_totalSpiderSuccessNew) /
				     (double)(totalNew);
		if ( totalOld > 0 )
			osr= 100.00*(double)(g_stats.m_totalSpiderSuccessOld) /
				     (double)(totalOld);
	}
	int32_t points = g_stats.m_spiderSample;
	if ( points > 1000 ) points = 1000;
	int32_t sampleNew = g_stats.m_spiderNew;
	int32_t sampleOld = points - g_stats.m_spiderNew;
	double tssr = 100.00;
	double nssr = 100.00;
	double ossr = 100.00;
	if ( points > 0 ) {
		tssr = 100.00*
			(double)(points -
				 g_stats.m_spiderErrors) / (double)points ;
		if ( sampleNew > 0 )
			nssr = 100.00*(double)(sampleNew -
					       g_stats.m_spiderErrorsNew) /
				      (double)(sampleNew);
		if ( sampleOld > 0 )
			ossr = 100.00*(double)(sampleOld -
					       (g_stats.m_spiderErrors -
						g_stats.m_spiderErrorsNew)) /
				      (double)(sampleOld);
	}

	sb.safePrintf(
		      "<style>"
		      ".poo { background-color:#%s;}\n"
		      "</style>\n" ,
		      LIGHT_BLUE );

	sb.safePrintf (

		       "<table %s>"
		       "<tr>"
		       "<td colspan=7>"
		       "<center><b>Spider Stats</b></td></tr>\n"
		       "<tr bgcolor=#%s><td>"
		       "</td><td><b>Total</b></td>"
		       "<td><b>Total New</b></td>"
		       "<td><b>Total Old</b></td>"
		       "<td><b>Sample</b></td>"
		       "<td><b>Sample New</b></td>"
		       "<td><b>Sample Old</b></b>"
		       "</td></tr>"

		       "<tr class=poo><td><b>Total Spiders</n>"
		       "</td><td>%"INT64"</td><td>%"INT64"</td><td>%"INT64"</td>\n"
		       "</td><td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td></tr>\n"
		       //"<tr class=poo><td><b>Successful Spiders</n>"
		       //"</td><td>%"INT64"</td><td>%"INT64"</td><td>%"INT64"</td>\n"
		       //"</td><td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td></tr>\n"
		       //"<tr class=poo><td><b>Failed Spiders</n>"
		       //"</td><td>%"INT64"</td><td>%"INT64"</td><td>%"INT64"</td>\n"
		       //"</td><td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td></tr>\n"
		       "<tr class=poo><td><b>Success Rate</b>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td></tr>",
		       TABLE_STYLE,  
		       DARK_BLUE,
		       totalPoints,
		       totalNew,
		       totalOld,
		       points,
		       sampleNew,
		       sampleOld,

		       //g_stats.m_totalSpiderSuccessNew +
		       //g_stats.m_totalSpiderSuccessOld,
		       //g_stats.m_totalSpiderSuccessNew,
		       //g_stats.m_totalSpiderSuccessOld,
		       //g_stats.m_spiderSuccessNew +
		       //g_stats.m_spiderSuccessOld,
		       //g_stats.m_spiderSuccessNew,
		       //g_stats.m_spiderSuccessOld,

		       //g_stats.m_totalSpiderErrorsNew +
		       //g_stats.m_totalSpiderErrorsOld,
		       //g_stats.m_totalSpiderErrorsNew,
		       //g_stats.m_totalSpiderErrorsOld,
		       //g_stats.m_spiderErrorsNew +
		       //g_stats.m_spiderErrorsOld,
		       //g_stats.m_spiderErrorsNew,
		       //g_stats.m_spiderErrorsOld,

		       tsr, nsr, osr, tssr, nssr, ossr );

	int32_t bucketsNew[65536];
	int32_t bucketsOld[65536];
	memset ( bucketsNew , 0 , 65536*4 );
	memset ( bucketsOld , 0 , 65536*4 );
	for ( int32_t i = 0 ; i < points; i++ ) {
		int32_t n = g_stats.m_errCodes[i];
		if ( n < 0 || n > 65535 ) {
			log("admin: Bad spider error code.");
			continue;
		}
		if ( g_stats.m_isSampleNew[i] )
			bucketsNew[n]++;
		else
			bucketsOld[n]++;
	}
	for ( int32_t i = 0 ; i < 65536 ; i++ ) {
		if ( g_stats.m_allErrorsNew[i] == 0 &&
		     g_stats.m_allErrorsOld[i] == 0 &&
		     bucketsNew[i] == 0 && bucketsOld[i] == 0 ) continue;
		sb.safePrintf (
			       "<tr bgcolor=#%s>"
			       "<td><b><a href=/search?c=%s&q=gbstatusmsg%%3A"
			       "%%22"
			       ,
			       LIGHT_BLUE , cr->m_coll );
		sb.urlEncode(mstrerror(i));
		sb.safePrintf ("%%22>"
			       "%s"
			       "</a>"
			       "</b></td>"
			       "<td>%"INT64"</td>"
			       "<td>%"INT64"</td>"
			       "<td>%"INT64"</td>"
			       "<td>%"INT32"</td>"
			       "<td>%"INT32"</td>"
			       "<td>%"INT32"</td>"
			       "</tr>\n" ,
			       mstrerror(i),
			       g_stats.m_allErrorsNew[i] +
			       g_stats.m_allErrorsOld[i],
			       g_stats.m_allErrorsNew[i],
			       g_stats.m_allErrorsOld[i],
			       bucketsNew[i] + bucketsOld[i] ,
			       bucketsNew[i] ,
			       bucketsOld[i] );
	}

	sb.safePrintf ( "</table><br>\n" );



	// describe the various parms
	/*
	sb.safePrintf ( 
		       "<table width=100%% bgcolor=#%s "
		       "cellpadding=4 border=1>"
		       "<tr class=poo>"
		       "<td colspan=2 bgcolor=#%s>"
		       "<b>Field descriptions</b>"
		       "</td>"
		       "</tr>\n"
		       "<tr class=poo>"
		       "<td>hits</td><td>The number of  attempts that were "
		       "made by the spider to read a url from the spider "
		       "queue cache.</td>"
		       "</tr>\n"


		       "<tr class=poo>"
		       "<td>misses</td><td>The number of those attempts that "
		       "failed to get a url to spider.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>cached</td><td>The number of urls that are "
		       "currently in the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>water</td><td>The number of urls that were in the "
		       "spider queue cache at any one time, since the start "
		       "of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>kicked</td><td>The number of urls that were "
		       "replaced in the spider queue cache with urls loaded "
		       "from disk, since the start of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>added</td><td>The number of urls that were added "
		       "to the spider queue cache since the start of the last "
		       "disk scan. After a document is spidered its url "
		       "if often added again to the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>attempted</td><td>The number of urls that "
		       "Gigablast attempted to add to the spider queue cache "
		       "since the start of the last disk scan. In "
		       "a distributed environment, urls are distributed "
		       "between twins so not all urls read will "
		       "make it into the spider queue cache. Also includes "
		       "spider recs attempted to be re-added to spiderdb "
		       "after being spidering, but usually with a different "
		       "spider time.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>nl</td><td>This is 1 iff Gigablast currently "
		       "needs to reload the spider queue cache from disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>rnl</td><td>This is 1 iff Gigablast currently "
		       "really needs to reload the spider queue cache from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>more</td><td>This is 1 iff there are urls on "
		       "the disk that are not in the spider queue cache.</td>"
		       "</tr>\n"


		       "<tr class=poo>"
		       "<td>loading</td><td>This is 1 iff Gigablast is "
		       "currently loading this spider cache queue from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>scanned</td><td>The number of bytes that were "
		       "read from disk since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>reads</td><td>The number of disk read "
		       "operations since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>elapsed</td><td>The time in seconds that has "
		       "elapsed since the start or end of the last disk "
		       "scan, depending on if a scan is currently in "
		       "progress.</td>"
		       "</tr>\n"

		       "</table>\n",

		       LIGHT_BLUE ,
		       DARK_BLUE  );
	*/

	// done if no sc
	if ( ! sc ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
						     sb.length() );
	}

	/////
	//
	// READY TO SPIDER table
	//
	/////

	int32_t ns = 0;
	if ( sc ) ns = sc->m_doleIpTable.getNumSlotsUsed();

	// begin the table
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			"<b>URLs Ready to Spider for collection "
			"<font color=red><b>%s</b>"
			"</font>"
			" (%"INT32" ips in doleiptable)"
			,
			TABLE_STYLE,
			st->m_coll ,
			ns );

	// print time format: 7/23/1971 10:45:32
	time_t nowUTC = getTimeGlobal();
	struct tm *timeStruct ;
	char time[256];
	timeStruct = gmtime ( &nowUTC );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb.safePrintf("</b>" //  (current time = %s = %"UINT32") "
		      "</td></tr>\n" 
		      //,time,nowUTC
		      );

	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb ,false ) ) return false;
	// the the doledb spider recs
	char *bs = sbTable->getBufStart();
	if ( bs && ! sb.safePrintf("%s",bs) ) return false;
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );



	/////////////////
	//
	// PRINT WAITING TREE
	//
	// each row is an ip. print the next url to spider for that ip.
	//
	/////////////////
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			"<b>IPs Waiting for Selection Scan for collection "
			"<font color=red><b>%s</b>"
			"</font>"
			,
			TABLE_STYLE,
			st->m_coll );
	// print time format: 7/23/1971 10:45:32
	int64_t timems = gettimeofdayInMillisecondsGlobal();
	sb.safePrintf("</b> (current time = %"UINT64")(totalcount=%"INT32")"
		      "(waittablecount=%"INT32")",
		      timems,
		      sc->m_waitingTree.getNumUsedNodes(),
		      sc->m_waitingTable.getNumUsedSlots());

	double a = (double)g_spiderdb.getUrlHash48 ( &sc->m_firstKey );
	double b = (double)g_spiderdb.getUrlHash48 ( &sc->m_endKey );
	double c = (double)g_spiderdb.getUrlHash48 ( &sc->m_nextKey );
	double percent = (100.0 * (c-a)) ;
	if ( b-a > 0 ) percent /= (b-a);
	if ( percent > 100.0 ) percent = 100.0;
	if ( percent < 0.0 ) percent = 0.0;
	sb.safePrintf("(spiderdb scan for ip %s is %.2f%% complete)",
		      iptoa(sc->m_scanningIp),
		      (float)percent );

	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("<tr bgcolor=#%s>",DARK_BLUE);
	sb.safePrintf("<td><b>spidertime (MS)</b></td>\n");
	sb.safePrintf("<td><b>firstip</b></td>\n");
	sb.safePrintf("</tr>\n");
	// the the waiting tree
	int32_t node = sc->m_waitingTree.getFirstNode();
	int32_t count = 0;
	//uint64_t nowMS = gettimeofdayInMillisecondsGlobal();
	for ( ; node >= 0 ; node = sc->m_waitingTree.getNextNode(node) ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// get key
		key_t *key = (key_t *)sc->m_waitingTree.getKey(node);
		// get ip from that
		int32_t firstIp = (key->n0) & 0xffffffff;
		// get the time
		uint64_t spiderTimeMS = key->n1;
		// shift upp
		spiderTimeMS <<= 32;
		// or in
		spiderTimeMS |= (key->n0 >> 32);
		char *note = "";
		// if a day more in the future -- complain
		// no! we set the repeat crawl to 3000 days for crawl jobs that
		// do not repeat...
		// if ( spiderTimeMS > nowMS + 1000 * 86400 )
		// 	note = " (<b><font color=red>This should not be "
		// 		"this far into the future. Probably a corrupt "
		// 		"SpiderRequest?</font></b>)";
		// get the rest of the data
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td>%"INT64"%s</td>"
			      "<td>%s</td>"
			      "</tr>\n",
			      LIGHT_BLUE,
			      (int64_t)spiderTimeMS,
			      note,
			      iptoa(firstIp));
		// stop after 20
		if ( ++count == 20 ) break;
	}
	// ...
	if ( count ) 
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td colspan=10>...</td></tr>\n",
			      LIGHT_BLUE);
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );

	// get the socket
	TcpSocket *s = st->m_socket;
	// then we can nuke the state
	mdelete ( st , sizeof(State11) , "PageSpiderdb" );
	delete (st);
	// erase g_errno for sending
	g_errno = 0;
	// now encapsulate it in html head/tail and send it off
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(),sb.length() );
}

///////////////////////////////////
//
// URLFILTERS
//
///////////////////////////////////

/*
// assign these a value of 1 in s_table hashtable
static char *s_ypSites[] = {
	"www.yellow.com",
	"www.yellowpages.com",
	"www.dexknows.com",
	"yellowpages.aol.com",
	"www.superpages.com",
	"citysearch.com",
	"www.yellowbook.com",
	"www.magicyellow.com",
	"home.digitalcity.com",
	"www.switchboard.com",
	"cityguide.aol.com",
	"www.bizrate.com",
	"www.restaurantica.com",
	"www.insiderpages.com",
	"local.yahoo.com"
};

// . assign these a value of 2 in s_table hashtable
// . mwells@g0:/y$ cat gobyout  | awk '{print $4}' | grep -v goby.com | grep -vi goby | grep -v google.com | grep -v mappoint | urlinfo | grep "host: " | awk '{print $2}' | sort | uniq > foo
// . then take the top linked to sites on goby and print out for direct
//   insertion into this file:

// then get the popular domains from THAT list:
// mwells@g0:/y$ cat foo | awk '{print $2}' | urlinfo | grep "dom: " | awk '{print $2}' | sort | uniq -c | sort > foodom

static char *s_aggSites[] = {
	"isuwmsrugby.tripod.com",
	"meyerlemon.eventbrite.com",
	"miami.tourcorp.com",
	"valentinesdaydatenightcoupleschi.eventbrite.com",
	"volcano.si.edu",
	"webpages.csus.edu",
	"weddingextravaganza.eventbrite.com",
	"www.alliancerugby.org",
	"www.asuwrfc.com",
	"www.btpd.org",
	"www.chicagodragons.org",
	"www.chsgeorgia.org",
	"www.derugbyfoundation.org",
	"www.foxborosportscenter.com",
	"www.lynn.edu",
	"www.owensboroparks.org",
	"www.scitrek.org",
	"www.southcarolinaparks.com",
	"www.usbr.gov",
	"dummil.eventbrite.com",
	"jacksonvilleantiqueshow.eventbrite.com",
	"kidsfest.eventbrite.com",
	"piuvalentine.eventbrite.com",
	"www.anytimefitness.com",
	"www.dumbartonhouse.org",
	"www.lsurugby.com",
	"www.maliburugby.com",
	"www.pitsrugby.com",
	"www.renegaderugby.org",
	"www.rotor.com",
	"www.rugbyrats.com",
	"www.sanjoserugby.com",
	"www.seattleartists.com",
	"www.sixflags.com",
	"www.vacavillesports.com",
	"atlcomedyfest.eventbrite.com",
	"easyweekdaycooking.eventbrite.com",
	"hartford.citysearch.com",
	"healthythaicooking.eventbrite.com",
	"hicaregiversconference.eventbrite.com",
	"skiing.alpinezone.com",
	"spirit.lib.uconn.edu",
	"springfield.ettractions.com",
	"tomatofest2011.eventbrite.com",
	"www.abc-of-meditation.com",
	"www.amf.com",
	"www.atlantaharlequins.com",
	"www.chicagoparkdistrict.com",
	"www.denverwildfirerfc.org",
	"www.gowaterfalling.com",
	"www.harlequins.org",
	"www.ignatius.org",
	"www.masmacon.com",
	"www.palmbeachrugby.org",
	"www.riversiderugby.com",
	"www.rmne.org",
	"www.thehilliard.org",
	"www.woodsmenrugby.com",
	"devildoll.eventbrite.com",
	"iexpectcrabfeedfundraiser.eventbrite.com",
	"sports.groups.yahoo.com",
	"valentinesdaycookingwithlove.eventbrite.com",
	"www.agisamazing.com",
	"www.ascendinglotus.com",
	"www.auduboninstitute.org",
	"www.azrugbyref.com",
	"www.blackicerugby.com",
	"www.bluegrassmuseum.org",
	"www.krewerugby.com",
	"www.lamorugby.com",
	"www.lsue.edu",
	"www.norwichrink.com",
	"www.ombac.org",
	"www.sdarmada.org",
	"www.sirensrugby.com",
	"www.tampabarbarians.org",
	"www.travellanecounty.org",
	"www.visit-newhampshire.com",
	"hawaii.tourcorp.com",
	"tasteofkorea.eventbrite.com",
	"www.ballyfitness.com",
	"www.calpolyrugby.com",
	"www.destateparks.com",
	"www.eaa.org",
	"www.goldsgym.com",
	"www.gonzagarugby.com",
	"www.greatexplorations.org",
	"www.heparks.org",
	"www.imagisphere.org",
	"www.jeffdavis.org",
	"www.park.granitecity.com",
	"www.poets.org",
	"www.regis.edu",
	"www.verizoncenter.com",
	"mybridalsale.eventbrite.com",
	"pigandsausagetoo.eventbrite.com",
	"www.gaelrugby.com",
	"www.independent.com",
	"www.kohlchildrensmuseum.org",
	"www.operaamerica.org",
	"www.recration.du.edu",
	"www.symmetricalskatingschool.org",
	"www.telcomhistory.org",
	"www.texasoutside.com",
	"reagan.eureka.edu",
	"stampede2011.eventbrite.com",
	"synergy2011.eventbrite.com",
	"theexperience2011.eventbrite.com",
	"www.24hourfitness.com",
	"www.dematha.org",
	"www.facebook.com",
	"www.iaapa.org",
	"www.icelandrestoration.com",
	"www.louisvillewomensrugby.com",
	"www.manchesterrunningcompany.com",
	"www.moaonline.org",
	"www.pvicechalet.com",
	"www.rendlake.com",
	"attinuptown.eventbrite.com",
	"chocolateanddessertfantasy.eventbrite.com",
	"colorado.ettractions.com",
	"longbeachstaterugby.webs.com",
	"volcano.oregonstate.edu",
	"www.columbiaspacescience.org",
	"www.eventful.com",
	"eventful.com",
	"www.newmexico.org",
	"www.rmparks.org",
	"www.sbyouthrugby.org",
	"www.venturacountyrugbyclub.com",
	"www.wheatonicearena.com",
	"faithorigins.eventbrite.com",
	"jerseyshore.metromix.com",
	"stlouis.citysearch.com",
	"valentinesdaydatenightcooking.eventbrite.com",
	"www.floridarugbyunion.com",
	"www.rugbyatucf.com",
	"www.stingrayrugby.com",
	"www.usfbullsrugby.com",
	"atlanta.going.com",
	"klsnzwineday.eventbrite.com",
	"losangeles.citysearch.com",
	"sourdough.eventbrite.com",
	"valentinesdaygourmetdating.eventbrite.com",
	"web.mit.edu",
	"www.airmuseum.org",
	"www.eparugby.org",
	"www.navicache.com",
	"www.siliconvalleyrugby.org",
	"www.yale.edu",
	"rhodeisland.ettractions.com",
	"studentorgs.vanderbilt.edu",
	"www.jaxrugby.org",
	"www.orlandomagazine.com",
	"www.plnurugby.com",
	"www.recreation.du.edu",
	"www.riversideraptors.com",
	"www.usarchery.org",
	"cacspringfling.eventbrite.com",
	"dallas.going.com",
	"groups.northwestern.edu",
	"hpualumniiphonelaunchparty.eventbrite.com",
	"juliachild.eventbrite.com",
	"southbaysciencesymposium2011.eventbrite.com",
	"www.curugby.com",
	"www.everyoneruns.net",
	"www.glendalerugby.com",
	"www.phantomsyouthrugby.org",
	"www.usdrugby.com",
	"10000expo-sponsoship-nec.eventbrite.com",
	"greenville.metromix.com",
	"spssan.eventbrite.com",
	"www.cmaathletics.org",
	"www.csulb.edu",
	"www.doralrugby.com",
	"www.neworleansrugbyclub.com",
	"www.sos.louisiana.gov",
	"www.southbayrugby.org",
	"www.travelnevada.com",
	"www.uicrugbyclub.org",
	"www.atlantabucksrugby.org",
	"www.dinodatabase.com",
	"www.fest21.com",
	"www.georgiatechrugby.com",
	"www.gsuwomensrugby.com",
	"www.siuwomensrugby.com",
	"www.snowtracks.com",
	"www.trainweb.com",
	"www.visitnebraska.gov",
	"www.visitsanantonio.com",
	"hometown.aol.com",
	"next2normal.eventbrite.com",
	"sixmonthpassatlanta2011.eventbrite.com",
	"winejazz2.eventbrite.com",
	"www.amityrugby.org",
	"www.meetandplay.com",
	"www.miami.edu",
	"www.miamirugby.com",
	"www.phillipscollection.org",
	"www.tridentsrugby.com",
	"wwwbloggybootcampsandiego.eventbrite.com",
	"whale-watching.gordonsguide.com",
	"www.culturemob.com",
	"www.denver-rugby.com",
	"www.hillwoodmuseum.org",
	"www.peabody.yale.edu",
	"www.yoursciencecenter.com",
	"newyorkcity.ettractions.com",
	"rawfoodcert.eventbrite.com",
	"www.discoverydepot.org",
	"www.dukecityrugbyclub.com",
	"www.jazztimes.com",
	"www.kissimmeeairmuseum.com",
	"www.southstreetseaportmuseum.org",
	"www.wsbarbariansrugby.com",
	"beerunch2011.eventbrite.com",
	"milwaukee.ettractions.com",
	"seminoletampa.casinocity.com",
	"silveroak.eventbrite.com",
	"tsunamifitclub.eventbrite.com",
	"walking-tours.gordonsguide.com",
	"www.alamedarugby.com",
	"www.atshelicopters.com",
	"www.camelbackrugby.com",
	"www.dlshs.org",
	"www.eteamz.com",
	"newyork.ettractions.com",
	"www.allaboutrivers.com",
	"www.childrensmuseumatl.org",
	"www.hartfordroses.org",
	"www.nationalparks.org",
	"www.seahawkyouthrugby.com",
	"www.skiingthebackcountry.com",
	"epcontinental.eventbrite.com",
	"healthandwellnessshow.eventbrite.com",
	"www.apopkamuseum.org",
	"www.condorsrugby.com",
	"www.dcr.virginia.gov",
	"www.diabloyouthrugby.org",
	"www.rockandice.com",
	"honolulu.metromix.com",
	"mowcrabfeed2011.eventbrite.com",
	"ptt-superbowl.eventbrite.com",
	"whitewater-rafting.gordonsguide.com",
	"winearomatraining.eventbrite.com",
	"www.broadway.com",
	"www.usc.edu",
	"www.gatorrugby.com",
	"www.iumudsharks.net",
	"www.scrrs.net",
	"www.sfggrugby.com",
	"www.unco.edu",
	"hctmspring2011conference.eventbrite.com",
	"sandiego.going.com",
	"www.crt.state.la.us",
	"www.foodhistorynews.com",
	"www.lancerrugbyclub.org",
	"www.littlerockrugby.com",
	"www.sharksrugbyclub.com",
	"www.channelislandsice.com",
	"www.idealist.org",
	"www.mbtykesrugby.com",
	"katahdicon.eventbrite.com",
	"foodwineloversfestival.eventbrite.com",
	"maristeveningseries2011.eventbrite.com",
	"philadelphia.ettractions.com",
	"sugarrushla.eventbrite.com",
	"www.chicagolions.com",
	"www.skatingsafe.com",
	"www.themeparkinsider.com",
	"fremdcraftfairspring2011.eventbrite.com",
	"gorptravel.away.com",
	"minnesota.ettractions.com",
	"www.chicagohopeacademy.org",
	"www.fmcicesports.com",
	"www.kitebeaches.com",
	"www.mixedmartialarts.com",
	"www.slatermill.org",
	"www.sunnysideoflouisville.org",
	"www.visitrochester.com",
	"careshow.eventbrite.com",
	"massachusetts.ettractions.com",
	"edwardianla2011.eventbrite.com",
	"indianapolis.metromix.com",
	"www.pasadenamarathon.org",
	"washington.going.com",
	"www.sjquiltmuseum.org",
	"www.wannakitesurf.com",
	"fauwomensrugby.sports.officelive.com",
	"newhampshire.ettractions.com",
	"www.vcmha.org",
	"milwaukee.going.com",
	"phoenix.going.com",
	"www.anrdoezrs.net",
	"www.temperugby.com",
	"pampermefabulous2011.eventbrite.com",
	"www.napavalleyvineyards.org",
	"r4k11.eventbrite.com",
	"ramonamusicfest.eventbrite.com",
	"www.abc-of-rockclimbing.com",
	"www.geocities.com",
	"jackson.metromix.com",
	"www.santamonicarugby.com",
	"cleveland.metromix.com",
	"lancaster.ettractions.com",
	"www.fortnet.org",
	"www.horseandtravel.com",
	"www.pubcrawler.com",
	"kdwp.state.ks.us",
	"www.berkeleyallblues.com",
	"www.liferugby.com",
	"www.socalmedicalmuseum.org",
	"www.dcsm.org",
	"www.sutler.net",
	"desmoines.metromix.com",
	"www.cavern.com",
	"www.dotoledo.org",
	"www.fws.gov",
	"www.ghosttowngallery.com",
	"www.museumamericas.org",
	"www.museumsofboston.org",
	"www.northshorerugby.com",
	"geocaching.gpsgames.org",
	"www.americaeast.com",
	"www.cwrfc.org",
	"www.jewelryshowguide.com",
	"www.livelytimes.com",
	"www.pascorugbyclub.com",
	"www.westminsterice.com",
	"www.claremontrugby.org",
	"www.jugglingdb.com",
	"www.metalblade.com",
	"www.preservationnation.org",
	"sofla2011.eventbrite.com",
	"www.belmonticeland.com",
	"www.dropzone.com",
	"www.smecc.org",
	"www.studentgroups.ucla.edu",
	"www.visitdetroit.com",
	"honolulu.going.com",
	"sippingandsaving5.eventbrite.com",
	"www.connecticutsar.org",
	"www.guestranches.com",
	"www.nvtrailmaps.com",
	"www.visitnh.gov",
	"illinois.ettractions.com",
	"www.spymuseum.org",
	"www.ci.riverside.ca.us",
	"www.hbnews.us",
	"www.santaclarayouthrugby.com",
	"www.thestranger.com",
	"www.freewebs.com",
	"www.miamirugbykids.com",
	"www.mtwashingtonvalley.org",
	"www.ocbucksrugby.com",
	"bridalpaloozala.eventbrite.com",
	"maps.yahoo.com",
	"www.azstateparks.com",
	"www.paywindowpro.com",
	"www.rowadventures.com",
	"parksandrecreation.idaho.gov",
	"www.artsmemphis.org",
	"www.lasvegasweekly.com",
	"www.redmountainrugby.org",
	"san-francisco.tourcorp.com",
	"www.khsice.com",
	"www.vansenusauto.com",
	"quinceanerasmagazineoc.eventbrite.com",
	"www.mvc-sports.com",
	"www.tbsa.com",
	"www.travelportland.com",
	"rtnpilgrim.eventbrite.com",
	"www.bigfishtackle.com",
	"www.centralmass.org",
	"cpca2011.eventbrite.com",
	"www.matadorrecords.com",
	"www.sebabluegrass.org",
	"prescott.showup.com",
	"vintagevoltage2011.eventbrite.com",
	"www.seattleperforms.com",
	"www.valleyskating.com",
	"resetbootcamp.eventbrite.com",
	"www.abc-of-mountaineering.com",
	"www.snocountry.com",
	"events.nytimes.com",
	"www.icecenter.net",
	"www.livefrommemphis.com",
	"www.pasadenarfc.com",
	"www.ucsdrugby.com",
	"uclaccim.eventbrite.com",
	"www.visitchesapeake.com",
	"www.natureali.org",
	"www.nordicskiracer.com",
	"www.nowplayingva.org",
	"www.sbcounty.gov",
	"www.seedesmoines.com",
	"www.world-waterfalls.com",
	"denver.going.com",
	"hearstmuseum.berkeley.edu",
	"www.lmurugby.com",
	"www.ftlrugby.com",
	"www.pelicanrugby.com",
	"rtnharthighschool.eventbrite.com",
	"www.visitri.com",
	"www.aba.org",
	"www.americaonice.us",
	"www.thecontemporary.org",
	"www.wherigo.com",
	"www.drtopo.com",
	"www.visitseattle.org",
	"calendar.dancemedia.com",
	"trips.outdoors.org",
	"www.chs.org",
	"www.myneworleans.com",
	"www.oaklandice.com",
	"nashville.metromix.com",
	"www.americangolf.com",
	"www.fossilmuseum.net",
	"www.oakparkparks.com",
	"www.visit-maine.com",
	"www.oregonlive.com",
	"www.allwashingtondctours.com",
	"www.wannadive.net",
	"www.sportsheritage.org",
	"hudsonvalley.metromix.com",
	"www.scificonventions.com",
	"www.wildernessvolunteers.org",
	"essencemusicfestival.eventbrite.com",
	"www.kitesurfatlas.com",
	"www.ndtourism.com",
	"valentinesgourmetdatingchicago.eventbrite.com",
	"www.fingerlakeswinecountry.com",
	"www.dmnh.org",
	"www.ticketnetwork.com",
	"partystroll.eventbrite.com",
	"www.bedandbreakfastnetwork.com",
	"www.sternmass.org",
	"www.visitnh.com",
	"www.places2ride.com",
	"www.hawaiieventsonline.com",
	"www.ucirugby.com",
	"www.gohawaii.com",
	"www.writersforum.org",
	"www.roadracingworld.com",
	"www.bigisland.org",
	"www.boatbookings.com",
	"www.lhs.berkeley.edu",
	"www.dnr.state.mn.us",
	"www.mostateparks.com",
	"www.historicnewengland.org",
	"www.waza.org",
	"www.backbayrfc.com",
	"newyork.metromix.com",
	"www.larebellion.org",
	"teetimes.golfhub.com",
	"10000expo-sponsoship-ceg.eventbrite.com",
	"10000expo-sponsor-bjm.eventbrite.com",
	"parks.ky.gov",
	"www.bostonusa.com",
	"www.visitbuffaloniagara.com",
	"www.sharksice.com",
	"2011burbankapprentice.eventbrite.com",
	"kansascity.ettractions.com",
	"www.bicycling.com",
	"www.cityofchino.org",
	"www.ridingworld.com",
	"www.whittierrugby.com",
	"10000bestjobsam.eventbrite.com",
	"www.adventurecentral.com",
	"www.earlymusic.org",
	"www.upcomingevents.com",
	"www.sleddogcentral.com",
	"www.capecodkidz.com",
	"www.collectorsguide.com",
	"www.cougarrugby.org",
	"www.sfvrugby.com",
	"strivetothrivepabcconf.eventbrite.com",
	"www.visithoustontexas.com",
	"www.authorstrack.com",
	"www.aboutgolfschools.org",
	"www.huntingspotz.com",
	"www.lib.az.us",
	"members.aol.com",
	"www.fs.fed.us",
	"www.ncarts.org",
	"www.vermonttravelplanner.org",
	//"www.scubadiving.com",
	"www.waterfallsnorthwest.com",
	"www.philadelphiausa.travel",
	"www.usgolfschoolguide.com",
	"njgin.state.nj.us",
	"www.artcards.cc",
	"www.rimonthly.com",
	"www.atlanta.net",
	"www.glacialgardens.com",
	"2011superbowlcruise.eventbrite.com",
	"swimming-with-dolphins.gordonsguide.com",
	"www.trackpedia.com",
	// why was this in there?
	//"www.dailyherald.com",
	"www.nhm.org",
	"boston.ettractions.com",
	"www.geneseefun.com",
	"www.travelsd.com",
	"www.golfbuzz.com",
	"www.in.gov",
	"cincinnati.metromix.com",
	"www.sanjose.com",
	"brevard.metromix.com",
	"www.dogsledrides.com",
	"www.orvis.com",
	"philadelphia.going.com",
	"twincities.metromix.com",
	"www.orlandorugby.com",
	"www.csufrugby.com",
	"www.larugby.com",
	"www.washingtonwine.org",
	"calendar.gardenweb.com",
	"gulfcoast.metromix.com",
	"florida.ettractions.com",
	"www.northeastwaterfalls.com",
	"www.computerhistory.org",
	"www.ct.gov",
	"www.hosteltraveler.com",
	"www.thinkrentals.com",
	"www.4x4trailhunters.com",
	"www.cityweekly.net",
	"www.yourrunning.com",
	"www.spasofamerica.com",
	"www.indoorclimbing.com",
	"www.utah.com",
	"boston.going.com",
	"minneapolisstpaul.ettractions.com",
	"www.coolrunning.com",
	"www.greensboronc.org",
	"www.michigan.org",
	"www.artfestival.com",
	"www.divespots.com",
	"www.oregonstateparks.org",
	"www.virginiawine.org",
	"www.morebeach.com",
	"www.minnesotamonthly.com",
	"www.texasescapes.com",
	"www.usatf.org",
	"www.findrentals.com",
	"www.hachettebookgroup.com",
	"www.racesonline.com",
	"www.usace.army.mil",
	"web.georgia.org",
	"detroit.metromix.com",
	"www.homebrewersassociation.org",
	"www.baltimore.org",
	"www.gastateparks.org",
	"www.arkansasstateparks.com",
	"www.visitlasvegas.com",
	"www.whenwerv.com",
	"www.chilicookoff.com",
	"www.bikeride.com",
	"www.eaglerockrugby.com",
	"www.pickwickgardens.com",
	"flagstaff.showup.com",
	"miami.going.com",
	"www.anchorage.net",
	"www.wlra.us",
	"www.thetrustees.org",
	"www.artnet.com",
	"www.mthoodterritory.com",
	"www.hihostels.com",
	"www.bfa.net",
	"167.102.232.26",
	"www.flyins.com",
	"www.stepintohistory.com",
	"www.festing.com",
	"www.pursuetheoutdoors.com",
	"newyork.going.com",
	"www.fishingguidenetwork.com",
	"www.visit-massachusetts.com",
	"www.visitindy.com",
	"www.washingtonpost.com",
	"www.greatamericandays.com",
	"www.washingtonian.com",
	"national.citysearch.com",
	"www.infohub.com",
	"www.productionhub.com",
	"www.events.org",
	"www.traveliowa.com",
	"www.findmyadventure.com",
	"delaware.metromix.com",
	"www.marinmagazine.com",
	"us.penguingroup.com",
	"www.bicycletour.com",
	"www.travelok.com",
	"www.scububble.com",
	"www.childrensmuseums.org",
	"www.conventionscene.com",
	"www.scubaspots.com",
	"www.tnvacation.com",
	"stlouis.ettractions.com",
	"www.mxparks.com",
	"florida.greatestdivesites.com",
	"www.nowplayingaustin.com",
	"www.skinnyski.com",
	"www.sportoften.com",
	"www.zvents.com",
	"www.visitphoenix.com",
	"palmsprings.metromix.com",
	"upcoming.yahoo.com",
	"www.washington.org",
	"www.balloonridesacrossamerica.com",
	"www.playbill.com",
	"palmbeach.ettractions.com",
	"louisville.metromix.com",
	"www.animecons.com",
	"www.findanartshow.com",
	"www.usef.org",
	"www.villagevoice.com",
	"www.discovergold.org",
	"www.georgiaoffroad.com",
	"www.memphistravel.com",
	"dc.metromix.com",
	"www.aplf-planetariums.info",
	"www.skateisi.com",
	"www.usacycling.org",
	"www.wine-compass.com",
	"www.visitdelaware.com",
	"tucson.metromix.com",
	"www.happycow.net",
	"www.indiecraftshows.com",
	"www.gethep.net",
	"www.agritourismworld.com",
	"stlouis.metromix.com",
	"phoenix.metromix.com",
	"stream-flow.allaboutrivers.com",
	"www.festivalsandevents.com",
	"www.winemcgee.com",
	"www.aurcade.com",
	"www.visitjacksonville.com",
	"www.nashvillescene.com",
	"www.4x4trails.net",
	"www.americancraftmag.org",
	"blog.danceruniverse.com",
	"www.vacationrealty.com",
	"www.californiasciencecenter.org",
	"www.rollerhome.com",
	"www.atvsource.com",
	"www.hotairballooning.com",
	"www.freeskateparks.com",
	"www.ruralbounty.com",
	"connecticut.ettractions.com",
	"www.localattractions.com",
	"www.skategroove.com",
	"www.hawaiitours.com",
	"www.visitrhodeisland.com",
	"www.swac.org",
	"www.swimmingholes.org",
	"www.roadfood.com",
	"www.gotriadscene.com",
	"www.runnersworld.com",
	"www.outerquest.com",
	"www.seattleweekly.com",
	"www.onlyinsanfrancisco.com",
	"www.bikereg.com",
	"www.artslant.com",
	"www.louisianatravel.com",
	"www.operabase.com",
	"www.stepintoplaces.com",
	"www.vinarium-usa.com",
	"www.visitconnecticut.com",
	"www.abc-of-mountainbiking.com",
	"www.wannask8.com",
	"www.xcski.org",
	"www.active-days.org",
	"www.hawaiiactivities.com",
	"www.massvacation.com",
	"www.uspa.org",
	"miami.ettractions.com",
	"www.abc-of-hiking.com",
	"www.bestofneworleans.com",
	"www.phillyfunguide.com",
	"www.beermonthclub.com",
	"www.newenglandwaterfalls.com",
	"www.lake-link.com",
	"www.festivalfinder.com",
	"www.visitmississippi.org",
	"www.lanierbb.com",
	"www.thepmga.com",
	"www.skitown.com",
	"www.fairsandfestivals.net",
	"sanfrancisco.going.com",
	"www.koa.com",
	"www.wildlifeviewingareas.com",
	"www.boatrenting.com",
	"www.nowplayingutah.com",
	"www.ultimaterollercoaster.com",
	"www.findacraftfair.com",
	"www.ababmx.com",
	"www.abc-of-skiing.com",
	"www.pw.org",
	"tampabay.metromix.com",
	"www.onthesnow.com",
	"www.sunny.org",
	"www.visitnewengland.com",
	"atlanta.metromix.com",
	"www.allaboutapples.com",
	"www.monsterjam.com",
	"www.bnbfinder.com",
	"www.sandiego.org",
	"www.worldcasinodirectory.com",
	"www.yoga.com",
	"www.1-800-volunteer.org",
	"www.visitkc.com",
	"www.theskichannel.com",
	"www.thephoenix.com",
	"www.virginia.org",
	"www.avclub.com",
	"www.orlandoinfo.com",
	"www.trustedtours.com",
	"www.peakradar.com",
	"web.minorleaguebaseball.com",
	"www.artshound.com",
	"www.daytonabeach.com",
	"chicago.going.com",
	"www.cetaceanwatching.com",
	"www.citypages.com",
	"www.nowplayingnashville.com",
	"www.discoverlosangeles.com",
	"www.ratebeer.com",
	"www.harpercollins.com",
	"www.seenewengland.com",
	"www.visitmt.com",
	"www.goldstar.com",
	"www.caverbob.com",
	"www.sanjose.org",
	"www.backcountrysecrets.com",
	"authors.simonandschuster.com",
	"rafting.allaboutrivers.com",
	"chicago.ettractions.com",
	"iweb.aam-us.org",
	"www.theputtingpenguin.com",
	"www.festivals.com",
	"www.artsboston.org",
	"www.aboutskischools.com",
	"tucson.showup.com",
	"www.thiswaytothe.net",
	"www.rei.com",
	"www.magicseaweed.com",
	"www.waterfallswest.com",
	"fortlauderdale.ettractions.com",
	"www.foodreference.com",
	"www.californiawineryadvisor.com",
	"www.teamap.com",
	"www.neworleanscvb.com",
	"www.skatetheory.com",
	"www.visitmaine.com",
	"www.rollerskating.org",
	"www.culturecapital.com",
	"www.delawarescene.com",
	"www.nyc-arts.org",
	"www.huntingoutfitters.net",
	"www.showcaves.com",
	"www.soccerbars.com",
	"www.visitnewportbeach.com",
	"www.beerme.com",
	"www.pitch.com",
	"www.museum.com",
	"www.hauntworld.com",
	"www.forestcamping.com",
	"www.dogpark.com",
	"www.critterplaces.com",
	"www.visitnj.org",
	"www.findagrave.com",
	"www.arcadefly.com",
	"www.winerybound.com",
	"www.usms.org",
	"www.zipscene.com",
	"www.horsetraildirectory.com",
	"www.coaster-net.com",
	"www.anaheimoc.org",
	"www.visitpa.com",
	"www.antiquetrader.com",
	"www.dallasobserver.com",
	"www.eventsetter.com",
	"www.goingoutside.com",
	"www.sightseeingworld.com",
	"www.artlog.com",
	"www.bnbstar.com",
	"www.hostels.com",
	"www.theartnewspaper.com",
	"consumer.discoverohio.com",
	"www.nssio.org",
	"www.wingshootingusa.org",
	"www.shootata.com",
	"www.randomhouse.com",
	"www.artforum.com",
	"www.bachtrack.com",
	"www.wayspa.com",
	"www.visitidaho.org",
	"www.exploreminnesota.com",
	"chicago.metromix.com",
	"www.worldgolf.com",
	"nysparks.state.ny.us",
	"www.meetup.com",
	"www.skateboardparks.com",
	"www.downtownjacksonville.org",
	"www.lighthousefriends.com",
	"www.strikespots.com",
	"ww2.americancanoe.org",
	"www.inlandarts.com",
	"www.horseshowcentral.com",
	"www.ridingresource.com",
	"www.experiencewa.com",
	"database.thrillnetwork.com",
	"denver.metromix.com",
	"www.bostoncentral.com",
	"www.segwayguidedtours.com",
	"www.colorado.com",
	"www.artandseek.org",
	"www.floridastateparks.org",
	"www.sparkoc.com",
	"losangeles.going.com",
	"www.motorcycleevents.com",
	"www.destination-store.com",
	"www.scubadviser.com",
	"www.booktour.com",
	"www.cloud9living.com",
	"www.allaboutjazz.com",
	"www.sacramento365.com",
	"www.discoversouthcarolina.com",
	"www.riverfronttimes.com",
	"www.hauntedhouses.com",
	"www.arenamaps.com",
	"www.artsnwct.org",
	"www.eventbrite.com",
	"animal.discovery.com",
	"www.eatfeats.com",
	"www.1001seafoods.com",
	"www.malletin.com",
	"www.yelp.com",
	"www.wannasurf.com",
	"www.clubplanet.com",
	"www.dupagecvb.com",
	"www.smartdestinations.com",
	"www.artfaircalendar.com",
	"www.excitations.com",
	"www.balloonrideus.com",
	"www.extravagift.com",
	"www.skisite.com",
	"www.orlandoweekly.com",
	"www.iloveny.com",
	"www.sandiegoreader.com",
	"web.usarugby.org",
	"www.artscalendar.com",
	"www.sfweekly.com",
	"store-locator.barnesandnoble.com",
	"www.realhaunts.com",
	"trails.mtbr.com",
	"www.bbonline.com",
	"www.pickyourownchristmastree.org",
	"events.myspace.com",
	"www.alabama.travel",
	"www.ctvisit.com",
	"freepages.history.rootsweb.com",
	"www.waterparks.com",
	"www.flavorpill.com",
	"www.marinasdirectory.org",
	"www.publicgardens.org",
	"www.alwaysonvacation.com",
	"www.infosports.com",
	"www.summitpost.org",
	"www.exploregeorgia.org",
	"www.brewerysearch.com",
	"www.phoenixnewtimes.com",
	"www.marinas.com",
	"www.arestravel.com",
	"www.gamebirdhunts.com",
	"www.cbssports.com",
	"tutsan.forest.net",
	"www.azcentral.com",
	"www.tennispulse.org",
	"www.westword.com",
	"www.factorytoursusa.com",
	"www.americanwhitewater.org",
	"www.spamagazine.com",
	"www.dogparkusa.com",
	"tps.cr.nps.gov",
	"www.sfstation.com",
	"www.abc-of-yoga.com",
	"www.worldeventsguide.com",
	"www.active.com",
	"www.beerexpedition.com",
	"www.iloveinns.com",
	"www.warpig.com",
	"www.artsopolis.com",
	"www.skatepark.com",
	"www.offroadnorthamerica.com",
	"www.visitflorida.com",
	"www.last.fm",
	"www.pbplanet.com",
	"www.traveltex.com",
	"phoenix.showup.com",
	"www.travelandleisure.com",
	"www.kentuckytourism.com",
	"www.gospelgigs.com",
	"www.whenwegetthere.com",
	"www.surfline.com",
	"www.stubhub.com",
	"www.centerstagechicago.com",
	"www.sunshineartist.com",
	"www.reserveamerica.com",
	"www.clubzone.com",
	"www.paddling.net",
	"www.xperiencedays.com",
	"www.razorgator.com",
	"www.dalejtravis.com",
	"www.pickyourown.org",
	"www.localhikes.com",
	"www.parks.ca.gov",
	"www.casinocity.com",
	"www.nofouls.com",
	"www.laweekly.com",
	"www.denver.org",
	"www.enjoyillinois.com",
	"www.livenation.com",
	"www.viator.com",
	"members.bikeleague.org",
	"www.skatespotter.com",
	"family.go.com",
	"www.myspace.com",
	"www.takemefishing.org",
	"www.localwineevents.com",
	"www.rinkdirectory.com",
	"www.walkjogrun.net",
	"www.nps.gov",
	"www.ghosttowns.com",
	"www.theatermania.com",
	"www.skateboardpark.com",
	"www.miaminewtimes.com",
	"www.explorechicago.org",
	"www.ocweekly.com",
	"www.ustasearch.com",
	"www.rateclubs.com",
	"www.tennismetro.com",
	"www.motorcyclemonster.com",
	"www.hauntedhouse.com",
	"www.pumpkinpatchesandmore.org",
	"www.courtsoftheworld.com",
	"www.ecoanimal.com",
	"www.yogafinder.com",
	"www.traillink.com",
	"www.equinenow.com",
	"www.jambase.com",
	"www.spaemergency.com",
	//"www.vacationhomerentals.com",
	"www.ava.org",
	"affiliate.isango.com",
	"www.museumland.net",
	"www.dirtworld.com",
	"www.rockclimbing.com",
	"www.kijubi.com",
	"www.outdoortrips.info",
	"www.visitcalifornia.com",
	"www.heritagesites.com",
	"www.bedandbreakfast.com",
	"www.discoveramerica.com",
	"www.singletracks.com",
	"www.museumstuff.com",
	"www.opentable.com",
	"www.homeaway.com",
	"www.thegolfcourses.net",
	"www.golflink.com",
	"www.trekaroo.com",
	"gocitykids.parentsconnect.com",
	"www.wildernet.com",
	"www.10best.com",
	"swim.isport.com",
	"www.wheretoshoot.org",
	"www.hostelworld.com",
	"www.landbigfish.com",
	"www.recreation.gov",
	"www.healthclubdirectory.com",
	"www.spafinder.com",
	"www.nationalregisterofhistoricplaces.com",
	"www.americantowns.com",
	"www.hmdb.org",
	"www.golfnow.com",
	"www.grandparents.com",
	"www.swimmersguide.com",
	"www.luxergy.com",
	"activities.wildernet.com",
	"events.mapchannels.com",
	"www.museumsusa.org",
	"www.rinktime.com",
	"www.rentandorbuy.com",
	"www.mytravelguide.com",
	"playspacefinder.kaboom.org",
	"www.famplosion.com",
	"www.eviesays.com",
	"www.anglerweb.com",
	"www.trails.com",
	"www.waymarking.com",
	"www.priceline.com",
	"local.yahoo.com",

	"ticketmaster.com",

	// rss feeds
	"trumba.com",

	// movie times:
	"cinemark.com",

	// domains (hand selected from above list filtered with urlinfo)
	"patch.com",
	"gordonsguide.com",
	"tourcorp.com",
	"americangolf.com",
	"casinocity.com",
	"going.com",
	"metromix.com",
	"ettractions.com",
	"citysearch.com",
	"eventbrite.com"
};
*/

/*
static HashTableX s_table;
static bool s_init = false;
static char s_buf[25000];
static int32_t s_craigsList;

bool initAggregatorTable ( ) {
	// this hashtable is used for "isyellowpages" and "iseventaggregator"
	if ( s_init ) return true;
	// use niceness 0
	s_table.set(4,1,4096,s_buf,25000,false,0,"spsitetbl");
	// now stock it with yellow pages sites
	int32_t n = (int32_t)sizeof(s_ypSites)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char *s    = s_ypSites[i];
		int32_t  slen = gbstrlen ( s );
		int32_t h32 = hash32 ( s , slen );
		char val = 1;
		if ( ! s_table.addKey(&h32,&val)) {char*xx=NULL;*xx=0;}
	}
	// then stock with event aggregator sites
	n = (int32_t)sizeof(s_aggSites)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char *s    = s_aggSites[i];
		int32_t  slen = gbstrlen ( s );
		int32_t h32 = hash32 ( s , slen );
		char val = 2;
		if ( ! s_table.addKey(&h32,&val)) {char*xx=NULL;*xx=0;}
	}
	// do not repeat this
	s_init = true;
	s_craigsList = hash32n("craigslist.org");
	return true;
}

bool isAggregator ( int32_t siteHash32,int32_t domHash32,char *url,int32_t urlLen ) {
	// make sure its stocked
	initAggregatorTable();
	// is site a hit?
	char *v = (char *)s_table.getValue ( &siteHash32 );
	// hit?
	if ( v && *v ) return true;
	// try domain?
	v = (char *)s_table.getValue ( &domHash32 );
	// hit?
	if ( v && *v ) return true;
	// these guys mirror eventful.com's db so let's grab it...
	// abcd.com
	if ( urlLen>30 &&
	     url[11]=='t' &&
	     url[18]=='o' &&
	     strncmp(url,"http://www.thingstodoin",23) == 0 ) 
		return true;
	// craigslist
	if ( domHash32 == s_craigsList && strstr(url,".com/cal/") )
		return true;
	// otherwise, no
	return false;
}
*/

#define SIGN_EQ 1
#define SIGN_NE 2
#define SIGN_GT 3
#define SIGN_LT 4
#define SIGN_GE 5
#define SIGN_LE 6

// from PageBasic.cpp
char *getMatchingUrlPattern ( SpiderColl *sc , SpiderRequest *sreq, char *tag);

// . this is called by SpiderCache.cpp for every url it scans in spiderdb
// . we must skip certain rules in getUrlFilterNum() when doing to for Msg20
//   because things like "parentIsRSS" can be both true or false since a url
//   can have multiple spider recs associated with it!
int32_t getUrlFilterNum2 ( SpiderRequest *sreq       ,
		       SpiderReply   *srep       ,
		       int32_t           nowGlobal  ,
		       bool           isForMsg20 ,
		       int32_t           niceness   ,
		       CollectionRec *cr         ,
			bool           isOutlink  ,
			   HashTableX   *quotaTable ,
			   int32_t langIdArg ) {

	if ( ! sreq ) {
		log("spider: sreq is NULL!");
	}

	int32_t langId = langIdArg;
	if ( srep ) langId = srep->m_langId;

	// convert lang to string
	char *lang    = NULL;
	int32_t  langLen = 0;
	if ( langId >= 0 ) { // if ( srep ) {
		// this is NULL on corruption
		lang = getLanguageAbbr ( langId );//srep->m_langId );	
		if (lang) langLen = gbstrlen(lang);
	}

	// . get parent language in the request
	// . primarpy language of the parent page that linked to this url
	char *plang = NULL;
	int32_t  plangLen = 0;
	plang = getLanguageAbbr(sreq->m_parentLangId);
	if ( plang ) plangLen = gbstrlen(plang);

	char *tld = (char *)-1;
	int32_t  tldLen;

	int32_t  urlLen = sreq->getUrlLen();
	char *url    = sreq->m_url;

	char *row;
	bool checkedRow = false;
	//SpiderColl *sc = cr->m_spiderColl;
	SpiderColl *sc = g_spiderCache.getSpiderColl(cr->m_collnum);

	if ( ! quotaTable ) quotaTable = &sc->m_localTable;

	//if ( strstr(url,"http://www.vault.com/rankings-reviews/company-rankings/law/vault-law-100/.aspx?pg=2" ))
	//	log("hey");

	//initAggregatorTable();

	//int32_t tldlen2;
	//char *tld2 = getTLDFast ( sreq->m_url , &tldlen2);
	//bool bad = true;
	//if ( tld2[0] == 'c' && tld2[1] == 'o' && tld2[2]=='m' ) bad = false;
	//if ( tld2[0] == 'o' && tld2[1] == 'r' && tld2[2]=='g' ) bad = false;
	//if ( tld2[0] == 'u' && tld2[1] == 's' ) bad = false;
	//if ( tld2[0] == 'g' && tld2[1] == 'o' && tld2[2]=='v' ) bad = false;
	//if ( tld2[0] == 'e' && tld2[1] == 'd' && tld2[2]=='u' ) bad = false;
	//if ( tld2[0] == 'i' && tld2[1] == 'n' && tld2[2]=='f' ) bad = false;
	//if ( bad ) 
	//	log("hey");

	// int16_tcut
	char *ucp = cr->m_diffbotUrlCrawlPattern.getBufStart();
	char *upp = cr->m_diffbotUrlProcessPattern.getBufStart();

	if ( upp && ! upp[0] ) upp = NULL;
	if ( ucp && ! ucp[0] ) ucp = NULL;

	// get the compiled regular expressions
	regex_t *ucr = &cr->m_ucr;
	regex_t *upr = &cr->m_upr;
	if ( ! cr->m_hasucr ) ucr = NULL;
	if ( ! cr->m_hasupr ) upr = NULL;


	char *ext;
	//char *special;

	// CONSIDER COMPILING FOR SPEED:
	// 1) each command can be combined into a bitmask on the spiderRequest
	//    bits, or an access to m_siteNumInlinks, or a substring match
	// 2) put all the strings we got into the list of Needles
	// 3) then generate the list of needles the SpiderRequest/url matches
	// 4) then reduce each line to a list of needles to have, a
	//    min/max/equal siteNumInlinks, min/max/equal hopCount,
	//    and a bitMask to match the bit flags in the SpiderRequest

	// stop at first regular expression it matches
	for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
		// breathe
		QUICKPOLL ( niceness );
		// get the ith rule
		SafeBuf *sb = &cr->m_regExs[i];
		//char *p = cr->m_regExs[i];
		char *p = sb->getBufStart();

	checkNextRule:

		// skip leading whitespace
		while ( *p && isspace(*p) ) p++;

		// do we have a leading '!'
		bool val = 0;
		if ( *p == '!' ) { val = 1; p++; }
		// skip whitespace after the '!'
		while ( *p && isspace(*p) ) p++;

		// new rules for when to download (diffbot) page
		if ( *p ==  'm' && 
		     p[1]== 'a' &&
		     p[2]== 't' &&
		     p[3]== 'c' &&
		     p[4]== 'h' &&
		     p[5]== 'e' &&
		     p[6]== 's' &&
		     p[7]== 'u' &&
		     p[8]== 'c' &&
		     p[9]== 'p' ) {
			// . skip this expression row if does not match
			// . url must match one of the patterns in there. 
			// . inline this for speed
			// . "ucp" is a ||-separated list of substrings
			// . "ucr" is a regex
			// . regexec returns 0 for a match
			if ( ucr && regexec(ucr,url,0,NULL,0) &&
			     // seed or other manual addition always matches
			     ! sreq->m_isAddUrl &&
			     ! sreq->m_isPageReindex &&
			     ! sreq->m_isInjecting )
				continue;
			// do not require a match on ucp if ucr is given
			if ( ucp && ! ucr &&
			     ! doesStringContainPattern(url,ucp) &&
			     // seed or other manual addition always matches
			     ! sreq->m_isAddUrl &&
			     ! sreq->m_isPageReindex &&
			     ! sreq->m_isInjecting )
				continue;
			p += 10;
			p = strstr(p,"&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// new rules for when to "process" (diffbot) page
		if ( *p ==  'm' && 
		     p[1]== 'a' &&
		     p[2]== 't' &&
		     p[3]== 'c' &&
		     p[4]== 'h' &&
		     p[5]== 'e' &&
		     p[6]== 's' &&
		     p[7]== 'u' &&
		     p[8]== 'p' &&
		     p[9]== 'p' ) {
			// . skip this expression row if does not match
			// . url must match one of the patterns in there. 
			// . inline this for speed
			// . "upp" is a ||-separated list of substrings
			// . "upr" is a regex
			// . regexec returns 0 for a match
			if ( upr && regexec(upr,url,0,NULL,0) ) 
				continue;
			if ( upp && !upr &&!doesStringContainPattern(url,upp))
				continue;
			p += 10;
			p = strstr(p,"&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}


		if ( *p=='h' && strncmp(p,"hasauthorityinlink",18) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! sreq->m_hasAuthorityInlinkValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_hasAuthorityInlink==val)continue;
			// skip
			p += 18;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( *p=='h' && strncmp(p,"hascontactinfo",14) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! sreq->m_hasContactInfoValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_hasContactInfo==val ) continue;
			// skip
			p += 14;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( *p=='h' && strncmp(p,"hasaddress",10) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! srep->m_hasAddressValid ) continue;
			// if no match continue
			if ( (bool)srep->m_hasAddress==val ) continue;
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( *p=='h' && strncmp(p,"hastod",6) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! srep->m_hasTODValid ) continue;
			// if no match continue
			if ( (bool)srep->m_hasTOD==val ) continue;
			// skip
			p += 6;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( *p=='h' && strncmp(p,"hasreply",8) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we got a reply, we are not new!!
			//if ( (bool)srep == (bool)val ) continue;
			if ( (bool)(sreq->m_hadReply) == (bool)val ) continue;
			// skip it for speed
			p += 8;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// hastmperror, if while spidering, the last reply was
		// like EDNSTIMEDOUT or ETCPTIMEDOUT or some kind of
		// usually temporary condition that warrants a retry
		if ( *p=='h' && strncmp(p,"hastmperror",11) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// get our error code
			int32_t errCode = srep->m_errCode;
			// . make it zero if not tmp error
			// . now have EDOCUNCHANGED and EDOCNOGOODDATE from
			//   Msg13.cpp, so don't count those here...
			if ( errCode != EDNSTIMEDOUT &&
			     errCode != ETCPTIMEDOUT &&
			     errCode != EDNSDEAD &&
			     // add this here too now because we had some
			     // seeds that failed one time and the crawl
			     // never repeated after that!
			     errCode != EBADIP &&
			     // assume diffbot is temporarily experiencing errs
			     // but the crawl, if recurring, should retry these
			     // at a later point
			     errCode != EDIFFBOTUNABLETOAPPLYRULES &&
			     errCode != EDIFFBOTCOULDNOTPARSE &&
			     errCode != EDIFFBOTCOULDNOTDOWNLOAD &&
			     errCode != EDIFFBOTINVALIDAPI &&
			     errCode != EDIFFBOTVERSIONREQ &&
			     errCode != EDIFFBOTURLPROCESSERROR &&
			     errCode != EDIFFBOTTOKENEXPIRED &&
			     errCode != EDIFFBOTUNKNOWNERROR &&
			     errCode != EDIFFBOTINTERNALERROR &&
			     // if diffbot received empty content when d'lding
			     errCode != EDIFFBOTEMPTYCONTENT &&
			     // or diffbot tcp timed out when d'lding the url
			     errCode != EDIFFBOTREQUESTTIMEDOUT &&
			     // if diffbot closed the socket on us...
			     errCode != EDIFFBOTMIMEERROR &&
			     // of the diffbot reply itself was not 200 (OK)
			     errCode != EDIFFBOTBADHTTPSTATUS &&
			     // out of memory while crawling?
			     errCode != ENOMEM &&
			     errCode != ENETUNREACH &&
			     errCode != EHOSTUNREACH )
				errCode = 0;
			// if no match continue
			if ( (bool)errCode == val ) continue;
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		/*
		if ( *p=='h' && strncmp(p,"hassitevenue",12) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! sreq->m_hasSiteVenueValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_hasSiteVenue==val ) continue;
			// allow "!isindexed" if no SpiderReply at all
			//if ( ! srep && val == 0 ) continue;
			// skip
			p += 12;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}
		*/

		if ( *p != 'i' ) goto skipi;

		if ( strncmp(p,"isinjected",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_isInjecting==val ) continue;
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isdocidbased",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			//if ( (bool)sreq->m_urlIsDocId==val ) continue;
			if ( (bool)sreq->m_isPageReindex==val ) continue;
			// skip
			p += 12;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isreindex",9) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			//if ( (bool)sreq->m_urlIsDocId==val ) continue;
			if ( (bool)sreq->m_isPageReindex==val ) continue;
			// skip
			p += 9;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"iscontacty",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid
			if ( ! sreq->m_isContactyValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_isContacty==val ) continue;
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// is it in the big list of sites?
		if ( strncmp(p,"insitelist",10) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// if only seeds in the sitelist and no

			// if there is no domain or url explicitly listed
			// then assume user is spidering the whole internet
			// and we basically ignore "insitelist"
			if ( sc->m_siteListIsEmpty && 
			     sc->m_siteListIsEmptyValid ) {
				// use a dummy row match
				row = (char *)1;
			}
			else if ( ! checkedRow ) {
				// only do once for speed
				checkedRow = true;
				// this function is in PageBasic.cpp
				row = getMatchingUrlPattern ( sc, sreq ,NULL);
			}
			// if we are not submitted from the add url api, skip
			if ( (bool)row == val ) continue;
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// . was it submitted from PageAddUrl.cpp?
		// . replaces the "add url priority" parm
		if ( strncmp(p,"isaddurl",8) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we are not submitted from the add url api, skip
			if ( (bool)sreq->m_isAddUrl == val ) continue;
			// skip
			p += 8;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( p[0]=='i' && strncmp(p,"ismanualadd",11) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// . if we are not submitted from the add url api, skip
			// . if we have '!' then val is 1
			if ( sreq->m_isAddUrl    || 
			     sreq->m_isInjecting ||
			     sreq->m_isPageReindex ||
			     sreq->m_isPageParser ) {
				if ( val ) continue;
			}
			else {
				if ( ! val ) continue;
			}
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// does it have an rss inlink? we want to expedite indexing
		// of such pages. i.e. that we gather from an rss feed that
		// we got from a pingserver...
		if ( strncmp(p,"isparentrss",11) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we have no such inlink
			if ( (bool)sreq->m_parentIsRSS == val ) continue;
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isparentsitemap",15) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_parentIsSiteMap == val) continue;
			// skip
			p += 15;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// does it have an rss inlink? we want to expedite indexing
		// of such pages. i.e. that we gather from an rss feed that
		// we got from a pingserver...
		if ( strncmp(p,"isroot",6) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// this is a docid only url, no actual url, so skip
			if ( sreq->m_isPageReindex ) continue;
			// a fast check
			char *u = sreq->m_url;
			// skip http
			u += 4;
			// then optional s for https
			if ( *u == 's' ) u++;
			// then ://
			u += 3;
			// scan until \0 or /
			for ( ; *u && *u !='/' ; u++ );
			// if \0 we are root
			bool isRoot = true;
			if ( *u == '/' ) {
				u++;
				if ( *u ) isRoot = false;
			}
			// if we are not root
			if ( isRoot == val ) continue;
			// skip
			p += 6;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		/*
		if ( strncmp(p,"isparentindexed",16) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we have no such inlink
			if ( (bool)sreq->m_wasParentIndexed == val ) continue;
			// skip
			p += 16;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}
		*/

		// we can now handle this guy since we have the latest
		// SpiderReply, pretty much guaranteed
		if ( strncmp(p,"isindexed",9) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if reply does not KNOW because of an error
			// since XmDoc::indexDoc() called
			// XmlDoc::getNewSpiderReply() and did not have this
			// info...
			if ( srep && (bool)srep->m_isIndexedINValid ) continue;
			// if no match continue
			if ( srep && (bool)srep->m_isIndexed==val ) continue;
			// allow "!isindexed" if no SpiderReply at all
			if ( ! srep && val == 0 ) continue;
			// skip
			p += 9;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"ingoogle",8) == 0 ) {
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! sreq->m_inGoogleValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_inGoogle == val ) continue;
			// allow "!isindexed" if no SpiderReply at all
			if ( ! srep && val == 0 ) continue;
			// skip
			p += 8;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}


		// . check to see if a page is linked to by
		//   www.weblogs.com/int16_tChanges.xml and if it is we put
		//   it into a queue that has a respider rate no faster than
		//   30 days, because we don't need to spider it quick since
		//   it is in the ping server!
		if ( strncmp(p,"isparentpingserver",18) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_parentIsPingServer == val) continue;
			// skip
			p += 18;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"ispingserver",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_isPingServer == val ) continue;
			// skip
			p += 12;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isonsamesubdomain",17 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			if ( val == 0 &&
			     sreq->m_parentHostHash32 != sreq->m_hostHash32 ) 
				continue;
			if ( val == 1 &&
			     sreq->m_parentHostHash32 == sreq->m_hostHash32 ) 
				continue;
			p += 6;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isfakeip",8 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_fakeFirstIp == val ) continue;
			p += 8;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isonsamedomain",14 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			if ( val == 0 &&
			     sreq->m_parentDomHash32 != sreq->m_domHash32 ) 
				continue;
			if ( val == 1 &&
			     sreq->m_parentDomHash32 == sreq->m_domHash32 ) 
				continue;
			p += 6;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// jpg JPG gif GIF wmv mpg css etc.
		if ( strncmp ( p , "ismedia",7 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;

			// the new way is much faster, but support the
			// old way below for a while since this bit is new
			if ( sreq->m_hasMediaExtension )
				goto gotOne;
			// if that bit is valid, and zero, then we do not match
			if ( sreq->m_hasMediaExtensionValid )
				continue;

			// check the extension
			if ( urlLen<=5 ) continue;
			ext = url + urlLen - 4;
			if ( ext[0] == '.' ) {
				if ( to_lower_a(ext[1]) == 'c' &&
				     to_lower_a(ext[2]) == 's' &&
				     to_lower_a(ext[3]) == 's' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'n' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'w' &&
				     to_lower_a(ext[2]) == 'm' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'w' &&
				     to_lower_a(ext[2]) == 'a' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'j' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'g' &&
				     to_lower_a(ext[2]) == 'i' &&
				     to_lower_a(ext[3]) == 'f' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'i' &&
				     to_lower_a(ext[2]) == 'c' &&
				     to_lower_a(ext[3]) == 'o' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == '3' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == '4' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'o' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'a' &&
				     to_lower_a(ext[2]) == 'v' &&
				     to_lower_a(ext[3]) == 'i' )
					goto gotOne;
			}
			else if ( ext[-1] == '.' ) {
				if ( to_lower_a(ext[0]) == 'm' &&
				     to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'e' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[0]) == 'j' &&
				     to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'e' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
			}
			// two letter extensions
			// .warc.gz and .arc.gz is ok
			// take this out for now
			// else if ( ext[1] == '.' ) {
			// 	if ( to_lower_a(ext[2]) == 'g' &&
			// 	     to_lower_a(ext[3]) == 'z' )
			// 		goto gotOne;
			// }

			// check for ".css?" substring
			// these two suck up a lot of time:
			// take them out for now. MDW 2/21/2015
			//special = strstr(url,".css?");
			//if ( special ) goto gotOne;
			//special = strstr(url,"/print/");
			// try to make detecting .css? super fast
			if ( ext[0] != '.' &&
			     ext[1] != '.' &&
			     urlLen > 10 ) {
				for(register int32_t k=urlLen-10;k<urlLen;k++){
					if ( url[k] != '.' ) continue;
					if ( url[k+1] == 'c' &&
					     url[k+2] == 's' &&
					     url[k+3] == 's' &&
					     url[k+4] == '?' )
						goto gotOne;
				}
			}
			//if ( special ) goto gotOne;
			// no match, try the next rule
			continue;
		gotOne:
			p += 7;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}


		// check for "isrss" aka "rss"
		if ( strncmp(p,"isrss",5) == 0 ) {
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// if we are not rss, we do not match this rule
			if ( (bool)srep->m_isRSS == val ) continue; 
			// skip it
			p += 5;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for "isrss" aka "rss"
		if ( strncmp(p,"isrssext",8) == 0 ) {
			// if we are not rss, we do not match this rule
			if ( (bool)sreq->m_isRSSExt == val ) continue; 
			// skip it
			p += 8;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for permalinks. for new outlinks we *guess* if its
		// a permalink by calling isPermalink() function.
		if (!strncmp(p,"ispermalink",11) ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// if we are not rss, we do not match this rule
			if ( (bool)srep->m_isPermalink == val ) continue; 
			// skip it
			p += 11;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// supports LF_ISPERMALINK bit for outlinks that *seem* to
		// be permalinks but might not
		if (!strncmp(p,"ispermalinkformat",17) ) {
			// if we are not rss, we do not match this rule
			if ( (bool)sreq->m_isUrlPermalinkFormat ==val)continue;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for this
		if ( strncmp(p,"isnewoutlink",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if we do not match this rule
			if ( (bool)sreq->m_isNewOutlink == val ) continue;
			// skip it
			p += 10;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for this
		if ( strncmp(p,"isnewrequest",12) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if we are a new request and val is 1 (has '!')
			if ( ! srep && val ) continue;
			// skip if we are a new request and val is 1 (has '!')
			if(srep&&sreq->m_addedTime>srep->m_spideredTime &&val)
				continue;
			// skip if we are old and val is 0 (does not have '!')
			if(srep&&sreq->m_addedTime<=srep->m_spideredTime&&!val)
				continue;
			// skip it for speed
			p += 12;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// kinda like isnewrequest, but has no reply. use hasreply?
		if ( strncmp(p,"isnew",5) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we got a reply, we are not new!!
			if ( (bool)sreq->m_hadReply != (bool)val ) continue;
			// skip it for speed
			p += 5;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}
		// iswww, means url is like www.xyz.com/...
		if ( strncmp(p,"iswww", 5) == 0 ) {
			// now this is a bit - doesn't seem to be working yet
			//if ( (bool)sreq->m_isWWWSubdomain == (bool)val ) 
			//	continue;
			// skip "iswww"
			p += 5;
			// skip over http:// or https://
			char *u = sreq->m_url;
			if ( u[4] == ':' ) u += 7;
			if ( u[5] == ':' ) u += 8;
			// url MUST be a www url
			char isWWW = 0;
			if( u[0] == 'w' &&
			    u[1] == 'w' &&
			    u[2] == 'w' ) isWWW = 1;
			// skip if no match
			if ( isWWW == val ) continue;
			// TODO: fix www.knightstown.skepter.com
			// maybe just have a bit in the spider request
			// another rule?
			p = strstr(p,"&&");
			if ( ! p ) return i;
			// skip the '&&'
			p += 2;
			goto checkNextRule;
		}

		// non-boolen junk
 skipi:

		// . we always match the "default" reg ex
		// . this line must ALWAYS exist!
		if ( *p=='d' && ! strcmp(p,"default" ) )
			return i;

		// is it in the big list of sites?
		if ( *p == 't' && strncmp(p,"tag:",4) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// if only seeds in the sitelist and no

			// if there is no domain or url explicitly listed
			// then assume user is spidering the whole internet
			// and we basically ignore "insitelist"
			if ( sc->m_siteListIsEmpty &&
			     sc->m_siteListIsEmptyValid ) {
				row = NULL;// no row
			}
			else if ( ! checkedRow ) {
				// only do once for speed
				checkedRow = true;
				// this function is in PageBasic.cpp
				// . it also has to match "tag" at (p+4)
				row = getMatchingUrlPattern ( sc, sreq ,p+4);
			}
			// if we are not submitted from the add url api, skip
			if ( (bool)row == val ) continue;
			// skip tag:
			p += 4;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}
		



		// set the sign
		char *s = p;
		// skip s to after
		while ( *s && is_alpha_a(*s) ) s++;

		// skip white space before the operator
		//char *saved = s;
		while ( *s && is_wspace_a(*s) ) s++;

		char sign = 0;
		if ( *s == '=' ) {
			s++;
			if ( *s == '=' ) s++;
			sign = SIGN_EQ;
		}
		else if ( *s == '!' && s[1] == '=' ) {
			s += 2;
			sign = SIGN_NE;
		}
		else if ( *s == '<' ) {
			s++;
			if ( *s == '=' ) { sign = SIGN_LE; s++; }
			else               sign = SIGN_LT; 
		} 
		else if ( *s == '>' ) {
			s++;
			if ( *s == '=' ) { sign = SIGN_GE; s++; }
			else               sign = SIGN_GT; 
		} 

		// skip whitespace after the operator
		while ( *s && is_wspace_a(*s) ) s++;


		// seed counts. how many seeds this subdomain has. 'siteadds'
		if ( *p == 's' &&
		     p[1] == 'i' &&
		     p[2] == 't' &&
		     p[3] == 'e' &&
		     p[4] == 'a' &&
		     p[5] == 'd' &&
		     p[6] == 'd' &&
		     p[7] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			// a special hack so it is seeds so we can use same tbl
			int32_t h32 = sreq->m_siteHash32 ^ 0x123456;
			int32_t *valPtr =(int32_t *)quotaTable->getValue(&h32);
			int32_t a;
			// if no count in table, that is strange, i guess
			// skip for now???
			// this happens if INJECTING a url from the
			// "add url" function on homepage
			if ( ! valPtr ) a=0;//continue;//{char *xx=NULL;*xx=0;}
			// int16_tcut
			else a = *valPtr;
			//log("siteadds=%"INT32" for %s",a,sreq->m_url);
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// domain seeds. 'domainadds'
		if ( *p == 'd' &&
		     p[1] == 'o' &&
		     p[2] == 'm' &&
		     p[3] == 'a' &&
		     p[4] == 'i' &&
		     p[5] == 'n' &&
		     p[6] == 'a' &&
		     p[7] == 'd' &&
		     p[8] == 'd' &&
		     p[9] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			// a special hack so it is seeds so we can use same tbl
			int32_t h32 = sreq->m_domHash32 ^ 0x123456;
			int32_t *valPtr ;
			valPtr = (int32_t *)quotaTable->getValue(&h32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}



		// new quotas. 'sitepages' = pages from site.
		// 'sitepages > 20 && seedcount <= 1 --> FILTERED'
		if ( *p == 's' &&
		     p[1] == 'i' &&
		     p[2] == 't' &&
		     p[3] == 'e' &&
		     p[4] == 'p' &&
		     p[5] == 'a' &&
		     p[6] == 'g' &&
		     p[7] == 'e' &&
		     p[8] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			int32_t *valPtr ;
		       valPtr=(int32_t*)quotaTable->getValue(&sreq->m_siteHash32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// int16_tcut
			//log("sitepgs=%"INT32" for %s",a,sreq->m_url);
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// domain quotas. 'domainpages > 10 && hopcount >= 1 --> FILTERED'
		if ( *p == 'd' &&
		     p[1] == 'o' &&
		     p[2] == 'm' &&
		     p[3] == 'a' &&
		     p[4] == 'i' &&
		     p[5] == 'n' &&
		     p[6] == 'p' &&
		     p[7] == 'a' &&
		     p[8] == 'g' &&
		     p[9] == 'e' &&
		     p[10] == 's' ) {
			// need a quota table for this. this only happens
			// when trying to int16_tcut things to avoid adding
			// urls to spiderdb... like XmlDoc.cpp calls
			// getUrlFtilerNum() to see if doc is banned or
			// if it should harvest links.
			if ( ! quotaTable )
				return -1;
			int32_t *valPtr;
			valPtr=(int32_t*)quotaTable->getValue(&sreq->m_domHash32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// tld:cn 
		if ( *p=='t' && strncmp(p,"tld",3)==0){
			// set it on demand
			if ( tld == (char *)-1 )
				tld = getTLDFast ( sreq->m_url , &tldLen );
			// no match if we have no tld. might be an IP only url,
			// or not in our list in Domains.cpp::isTLD()
			if ( ! tld || tldLen == 0 ) continue;
			// set these up
			//char *a    = tld;
			//int32_t  alen = tldLen;
			char *b    = s;
			// loop for the comma-separated list of tlds
			// like tld:us,uk,fr,it,de
		subloop1:
			// get length of it in the regular expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had tld==com,org,...
			if ( sign == SIGN_EQ &&
			     blen == tldLen && 
			     strncasecmp(start,tld,tldLen)==0 ) 
				// if we matched any, that's great
				goto matched1;
			// if its tld!=com,org,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == tldLen && 
			     strncasecmp(start,tld,tldLen)==0 ) 
				// we do not match this rule if we matched
				// and of the tlds in the != list
				continue;
			// might have another tld in a comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched1;
				// otherwise, bad sign?
				continue;
			}
			// advance to next tld if there was a comma after us
			b++;
			// and try again
			goto subloop1;
			// otherwise
			// do we match, if not, try next regex
			//sm = strncasecmp(a,b,blen);
			//if ( sm != 0 && sign == SIGN_EQ ) goto miss1;
			//if ( sm == 0 && sign == SIGN_NE ) goto miss1;
			// come here on a match
		matched1:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// lang:en,zh_cn
		if ( *p=='l' && strncmp(p,"lang",4)==0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( langId == -1 ) continue;
			// skip if unknown? no, we support "xx" as unknown now
			//if ( srep->m_langId == 0 ) continue;
			// set these up
			char *b = s;
			// loop for the comma-separated list of langids
			// like lang==en,es,...
		subloop2:
			// get length of it in the regular expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had lang==en,es,...
			if ( sign == SIGN_EQ &&
			     blen == langLen && 
			     strncasecmp(start,lang,langLen)==0 ) 
				// if we matched any, that's great
				goto matched2;
			// if its lang!=en,es,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == langLen && 
			     strncasecmp(start,lang,langLen)==0 ) 
				// we do not match this rule if we matched
				// and of the langs in the != list
				continue;
			// might have another in the comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched2;
				// otherwise, bad sign?
				continue;
			}
			// advance to next list item if was a comma after us
			b++;
			// and try again
			goto subloop2;
			// come here on a match
		matched2:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// parentlang=en,zh_cn
		if ( *p=='p' && strncmp(p,"parentlang",10)==0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			//if ( ! srep ) continue;
			// skip if unknown? no, we support "xx" as unknown now
			//if ( srep->m_langId == 0 ) continue;
			// set these up
			char *b = s;
			// loop for the comma-separated list of langids
			// like parentlang==en,es,...
		subloop2b:
			// get length of it in the expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had parentlang==en,es,...
			if ( sign == SIGN_EQ &&
			     blen == plangLen && 
			     strncasecmp(start,plang,plangLen)==0 ) 
				// if we matched any, that's great
				goto matched2b;
			// if its parentlang!=en,es,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == plangLen && 
			     strncasecmp(start,plang,plangLen)==0 ) 
				// we do not match this rule if we matched
				// and of the langs in the != list
				continue;
			// might have another in the comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched2b;
				// otherwise, bad sign?
				continue;
			}
			// advance to next list item if was a comma after us
			b++;
			// and try again
			goto subloop2b;
			// come here on a match
		matched2b:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// hopcount == 20 [&&]
		if ( *p=='h' && strncmp(p, "hopcount", 8) == 0){
			// skip if not valid
			if ( ! sreq->m_hopCountValid ) continue;
			// int16_tcut
			int32_t a = sreq->m_hopCount;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// the last time it was spidered
		if ( *p=='l' && strncmp(p,"lastspidertime",14) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			int32_t a = 0;
			// if no spider reply we can't match this rule!
			if ( ! srep ) continue;
			// int16_tcut
			if ( srep ) a = srep->m_spideredTime;
			// make it point to the retry count
			int32_t b ;
			// now "s" can be "{roundstart}"
			if ( s[0]=='{' && strncmp(s,"{roundstart}",12)==0)
				b = cr->m_spiderRoundStartTime;//Num;
			else
				b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// selector using the first time it was added to the Spiderdb
		// added by Sam, May 5th 2015
		if ( *p=='u' && strncmp(p,"urlage",6) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) {
				//log("was for message 20");
				continue;

			}
			// get the age of the spider_request. 
			// (substraction of uint with int, hope
			// every thing goes well there)
			int32_t sreq_age = 0;

			// if m_discoveryTime is available, we use it. Otherwise we use m_addedTime
			if ( sreq && sreq->m_discoveryTime!=0) sreq_age = nowGlobal-sreq->m_discoveryTime;
			if ( sreq && sreq->m_discoveryTime==0) sreq_age = nowGlobal-sreq->m_addedTime;
			//log("spiderage=%d",sreq_age);
			// the argument entered by user
			int32_t argument_age=atoi(s) ;
			if ( sign == SIGN_EQ && sreq_age != argument_age ) continue;
			if ( sign == SIGN_NE && sreq_age == argument_age ) continue;
			if ( sign == SIGN_GT && sreq_age <= argument_age ) continue;
			if ( sign == SIGN_LT && sreq_age >= argument_age ) continue;
			if ( sign == SIGN_GE && sreq_age <  argument_age ) continue;
			if ( sign == SIGN_LE && sreq_age >  argument_age ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}


		if ( *p=='e' && strncmp(p,"errorcount",10) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// int16_tcut
			int32_t a = srep->m_errCount;
			// make it point to the retry count
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 10;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// EBADURL malformed url is ... 32880
		if ( *p=='e' && strncmp(p,"errorcode",9) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// int16_tcut
			int32_t a = srep->m_errCode;
			// make it point to the retry count
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 9;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		if ( *p == 'n' && strncmp(p,"numinlinks",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// these are -1 if they are NOT valid
			int32_t a = sreq->m_pageNumInlinks;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 10;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// siteNumInlinks >= 300 [&&]
		if ( *p=='s' && strncmp(p, "sitenuminlinks", 14) == 0){
			// these are -1 if they are NOT valid
			int32_t a1 = sreq->m_siteNumInlinks;
			// only assign if valid
			int32_t a2 = -1; 
			if ( srep ) a2 = srep->m_siteNumInlinks;
			// assume a1 is the best
			int32_t a ;
			// assign to the first valid one
			if      ( a1 != -1 ) a = a1;
			else if ( a2 != -1 ) a = a2;
			// swap if both are valid, but srep is more recent
			if ( a1 != -1 && a2 != -1 &&
			     srep->m_spideredTime > sreq->m_addedTime )
				a = a2;
			// skip if nothing valid
			if ( a == -1 ) continue;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 14;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		/*
		// retryNum >= 2 [&&] ...
		if ( *p=='r' && strncmp(p, "retrynum", 8) == 0){
			// int16_tcut
			int32_t a = sr->m_retryNum;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}
		*/

		// how many days have passed since it was last attempted
		// to be spidered? used in conjunction with percentchanged
		// to assign when to re-spider it next
		if ( *p=='s' && strncmp(p, "spiderwaited", 12) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// do not match rule if never attempted
			// if ( srep->m_spideredTime ==  0 ) {
			// 	char*xx=NULL;*xx=0;}
			// if ( srep->m_spideredTime == (uint32_t)-1){
			// 	char*xx=NULL;*xx=0;}
			// shortcut
			int32_t a = nowGlobal - srep->m_spideredTime;
			// make into days
			//af /= (3600.0*24.0);
			// back to a int32_t, round it
			//int32_t a = (int32_t)(af + 0.5);
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// percentchanged >= 50 [&&] ...
		if ( *p=='p' && strncmp(p, "percentchangedperday", 20) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// int16_tcut
			float a = srep->m_percentChangedPerDay;
			// make it point to the priority
			float b = atof(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// httpStatus == 400
		if ( *p=='h' && strncmp(p, "httpstatus", 10) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// int16_tcut (errCode doubles as g_errno)
			int32_t a = srep->m_errCode;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// how old is the doc in seconds? age is the pubDate age
		if ( *p =='a' && strncmp(p, "age", 3) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// int16_tcut
			int32_t age;
			if ( srep->m_pubDate <= 0 ) age = -1;
			else age = nowGlobal - srep->m_pubDate;
			// we can not match if invalid
			if ( age <= 0 ) continue;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && age != b ) continue;
			if ( sign == SIGN_NE && age == b ) continue;
			if ( sign == SIGN_GT && age <= b ) continue;
			if ( sign == SIGN_LT && age >= b ) continue;
			if ( sign == SIGN_GE && age <  b ) continue;
			if ( sign == SIGN_LE && age >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		/*
		  MDW: i replaced this with 
		  m_contentHash32 to make spiders faster/smarter so let's
		  take this out for now

		// how many new inlinkers we got since last spidered time?
		if ( *p =='n' && strncmp(p, "newinlinks", 10) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// . make it point to the newinlinks.
			// . # of new SpiderRequests added since 
			//   srep->m_spideredTime
			// . m_dupCache insures that the same ip/hostHash
			//   does not add more than 1 SpiderRequest for the
			//   same url/outlink
			int32_t a = srep->m_newRequests;
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// quick
			p += 10;
			// look for more
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}
		*/

		// our own regex thing (match front of url)
		if ( *p=='^' ) {
			// advance over caret
			p++;
			// now pstart pts to the string we will match
			char *pstart = p;
			// make "p" point to one past the last char in string
			while ( *p && ! is_wspace_a(*p) ) p++;
			// how long is the string to match?
			int32_t plen = p - pstart;
			// empty? that's kinda an error
			if ( plen == 0 ) 
				continue;
			int32_t m = 1;
			// check to see if we matched if url was int32_t enough
			if ( urlLen >= plen )
				m = strncmp(pstart,url,plen);
			if ( ( m == 0 && val == 0 ) ||
			     // if they used the '!' operator and we
			     // did not match the string, that's a 
			     // row match
			     ( m && val == 1 ) ) {
				// another expression follows?
				p = strstr(s, "&&");
				//if nothing, else then it is a match
				if ( ! p ) return i;
				//skip the '&&' and go to next rule
				p += 2;
				goto checkNextRule;
			}
			// no match
			continue;
		}

		// our own regex thing (match end of url)
		if ( *p=='$' ) {
			// advance over dollar sign
			p++;
			// a hack for $\.css, skip over the backslash too
			if ( *p=='\\' && *(p+1)=='.' ) p++;
			// now pstart pts to the string we will match
			char *pstart = p;
			// make "p" point to one past the last char in string
			while ( *p && ! is_wspace_a(*p) ) p++;
			// how long is the string to match?
			int32_t plen = p - pstart;
			// empty? that's kinda an error
			if ( plen == 0 ) 
				continue;
			// . do we match it?
			// . url has to be at least as big
			// . match our tail
			int32_t m = 1;
			// check to see if we matched if url was int32_t enough
			if ( urlLen >= plen )
				m = strncmp(pstart,url+urlLen-plen,plen);
			if ( ( m == 0 && val == 0 ) ||
			     // if they used the '!' operator and we
			     // did not match the string, that's a 
			     // row match
			     ( m && val == 1 ) ) {
				// another expression follows?
				p = strstr(s, "&&");
				//if nothing, else then it is a match
				if ( ! p ) return i;
				//skip the '&&' and go to next rule
				p += 2;
				goto checkNextRule;
			}
			// no match
			continue;
		}

		// . by default a substring match
		// . action=edit
		// . action=history

		// now pstart pts to the string we will match
		char *pstart = p;
		// make "p" point to one past the last char in string
		while ( *p && ! is_wspace_a(*p) ) p++;
		// how long is the string to match?
		int32_t plen = p - pstart;
		// need something...
		if ( plen <= 0 ) continue;
		// must be at least as big
		//if ( urlLen < plen ) continue;
		// nullilfy it temporarily
		char c = *p;
		*p     = '\0';
		// does url contain it? haystack=u needle=p
		char *found = strstr ( url , pstart );
		// put char back
		*p     = c;

		// kinda of a hack fix. if they inject a filtered url
		// into test coll, do not filter it! fixes the fact that
		// we filtered facebook, but still add it in our test
		// collection injection in urls.txt
		if ( found && 
		     sreq->m_isInjecting &&
		     cr->m_coll[0]=='t' &&
		     cr->m_coll[1]=='e' &&
		     cr->m_coll[2]=='s' &&
		     cr->m_coll[3]=='t' &&
		     cr->m_coll[4]=='\0' &&
		     cr->m_spiderPriorities[i] < 0 )
			continue;

		// support "!company" meaning if it does NOT match
		// then do this ...
		if ( ( found && val == 0 ) ||
		     // if they used the '!' operator and we
		     // did not match the string, that's a 
		     // row match
		     ( ! found && val == 1 ) ) {
			// another expression follows?
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

	}
	// sanity check ... must be a default rule!
	//char *xx=NULL;*xx=0;
	// return -1 if no match, caller should use a default
	return -1;
}

//static bool s_ufnInit = false;
//static HashTableX s_ufnTable;

//void clearUfnTable ( ) { 
//	s_ufnTable.clear(); 
//	s_ufnTree.clear();
//}

int32_t getUrlFilterNum ( SpiderRequest *sreq       ,
		       SpiderReply   *srep       ,
		       int32_t           nowGlobal  ,
		       bool           isForMsg20 ,
		       int32_t           niceness   ,
		       CollectionRec *cr         ,
		       bool           isOutlink  ,
			  HashTableX    *quotaTable ,
			  int32_t langId ) {

	/*
	  turn this off for now to save memory on the g0 cluster.
	  we should nuke this anyway with rankdb

	// init table?
	if ( ! s_ufnInit ) {
		s_ufnInit = true;
		if ( ! s_ufnTable.set(8,
				      1,
				      1024*1024*5,
				      NULL,0,
				      false,
				      MAX_NICENESS,
				      "ufntab") ) { char *xx=NULL;*xx=0; } 
	}

	// check in cache using date of request and reply and uh48 as the key
	int64_t key64 = sreq->getUrlHash48();
	key64 ^= (int64_t)sreq->m_addedTime;
	if ( srep ) key64 ^= ((int64_t)srep->m_spideredTime)<<32;
	char *uv = (char *)s_ufnTable.getValue(&key64);
	if ( uv ) 
		return *uv;
	*/
	char ufn = getUrlFilterNum2 ( sreq,
				      srep,
				      nowGlobal,
				      isForMsg20,
				      niceness,
				      cr,
				      isOutlink,
				      quotaTable ,
				      langId );

	/*
	// is table full? clear it if so
	if ( s_ufnTable.getNumSlotsUsed() > 2000000 ) {
		log("spider: resetting ufn table");
		s_ufnTable.clear();
	}
	// cache it
	s_ufnTable.addKey ( &key64 , &ufn );
	*/

	return (int32_t)ufn;
}


bool SpiderColl::printStats ( SafeBuf &sb ) {
	return true;
}

// . dedup for spiderdb
// . TODO: we can still have spider request dups in this if they are
//   sandwiched together just right because we only compare to the previous
//   SpiderRequest we added when looking for dups. just need to hash the
//   relevant input bits and use that for deduping.
// . TODO: we can store ufn/priority/spiderTime in the SpiderRequest along
//   with the date now, so if url filters do not change then 
//   gotSpiderdbList() can assume those to be valid and save time. BUT it does
//   have siteNumInlinks...
void dedupSpiderdbList ( RdbList *list , int32_t niceness , bool removeNegRecs ) {

	//int32_t  need = list->m_listSize;
	char *newList = list->m_list;//(char *)mmalloc (need,"dslist");
	//if ( ! newList ) {
	//	log("spider: could not dedup spiderdb list: %s",
	//	    mstrerror(g_errno));
	//	return;
	//}
	char *dst          = newList;
	char *restorePoint = newList;
	int64_t reqUh48  = 0LL;
	int64_t repUh48  = 0LL;
	SpiderReply   *oldRep = NULL;
	SpiderRequest *oldReq = NULL;
	char *lastKey     = NULL;
	char *prevLastKey = NULL;

	// save list ptr in case of re-read?
	//char *saved = list->m_listPtr;
	// reset it
	list->resetListPtr();

	for ( ; ! list->isExhausted() ; ) {
		// breathe. NO! assume in thread!!
		//QUICKPOLL(niceness);
		// get rec
		char *rec = list->getCurrentRec();

		// pre skip it
		list->skipCurrentRec();

		// skip if negative, just copy over
		if ( ( rec[0] & 0x01 ) == 0x00 ) {
			// should not be in here if this was true...
			if ( removeNegRecs ) {
				log("spider: filter got negative key");
				char *xx=NULL;*xx=0;
			}
			// save this
			prevLastKey = lastKey;
			lastKey     = dst;
			// otherwise, keep it
			memmove ( dst , rec , sizeof(key128_t) );
			dst += sizeof(key128_t);
			continue;
		}

		// is it a reply?
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) {
			// cast it
			SpiderReply *srep = (SpiderReply *)rec;
			// int16_tcut
			int64_t uh48 = srep->getUrlHash48();
			// crazy?
			if ( ! uh48 ) { 
				//uh48 = hash64b ( srep->m_url );
				uh48 = 12345678;
				log("spider: got uh48 of zero for spider req. "
				    "computing now.");
			}
			// does match last reply?
			if ( repUh48 == uh48 ) {
				// if he's a later date than us, skip us!
				if ( oldRep->m_spideredTime >=
				     srep->m_spideredTime )
					// skip us!
					continue;
				// otherwise, erase him
				dst     = restorePoint;
				lastKey = prevLastKey;
			}
			// save in case we get erased
			restorePoint = dst;
			prevLastKey  = lastKey;
			lastKey      = dst;
			// get our size
			int32_t recSize = srep->getRecSize();
			// and add us
			memmove ( dst , rec , recSize );
			// advance
			dst += recSize;
			// update this crap for comparing to next reply
			repUh48 = uh48;
			oldRep  = srep;
			// get next spiderdb record
			continue;
		}

		// int16_tcut
		SpiderRequest *sreq = (SpiderRequest *)rec;

		// int16_tcut
		int64_t uh48 = sreq->getUrlHash48();

		// crazy?
		if ( ! uh48 ) { 
			//uh48 = hash64b ( sreq->m_url );
			uh48 = 12345678;
			log("spider: got uh48 of zero for spider req. "
			    "computing now.");
		}

		// update request with SpiderReply if newer, because ultimately
		// ::getUrlFilterNum() will just look at SpiderRequest's 
		// version of these bits!
		if ( oldRep && repUh48 == uh48 &&
		     oldRep->m_spideredTime > sreq->m_addedTime ) {

			// if request was a page reindex docid based request 
			// and url has since been spidered, nuke it!
			//if ( sreq->m_urlIsDocId ) continue;
			if ( sreq->m_isPageReindex ) continue;

			// same if indexcode was EFAKEFIRSTIP which XmlDoc.cpp
			// re-adds to spiderdb with the right firstip. once
			// those guys have a reply we can ignore them.
			// TODO: what about diffbotxyz spider requests? those
			// have a fakefirstip... they should not have requests
			// though, since their parent url has that.
			if ( sreq->m_fakeFirstIp ) continue;

			SpiderReply *old = oldRep;
			sreq->m_inGoogle           = old->m_inGoogle;
			sreq->m_hasAuthorityInlink = old->m_hasAuthorityInlink;
			sreq->m_hasContactInfo     = old->m_hasContactInfo;
			//sreq->m_hasSiteVenue       = old->m_hasSiteVenue;
		}

		// if we are not the same url as last request, add it
		if ( uh48 != reqUh48 ) {
			// a nice hook in
		addIt:
			// save in case we get erased
			restorePoint = dst;
			prevLastKey  = lastKey;
			// get our size
			int32_t recSize = sreq->getRecSize();
			// save this
			lastKey = dst;
			// and add us
			memmove ( dst , rec , recSize );
			// advance
			dst += recSize;
			// update this crap for comparing to next reply
			reqUh48  = uh48;
			oldReq   = sreq;
			// get next spiderdb record
			continue;
		}

		// try to kinda grab the min hop count as well
		// do not alter spiderdb!
		// if ( sreq->m_hopCountValid && oldReq->m_hopCountValid ) {
		// 	if ( oldReq->m_hopCount < sreq->m_hopCount )
		// 		sreq->m_hopCount = oldReq->m_hopCount;
		// 	else
		// 		oldReq->m_hopCount = sreq->m_hopCount;
		// }

		// if he's essentially different input parms but for the
		// same url, we want to keep him because he might map the
		// url to a different url priority!
		if ( oldReq->m_siteHash32    != sreq->m_siteHash32    ||
		     oldReq->m_isNewOutlink  != sreq->m_isNewOutlink  ||
		     //  use hopcount now too!
		     oldReq->m_hopCount      != sreq->m_hopCount      ||
		     // makes a difference as far a m_minPubDate goes, because
		     // we want to make sure not to delete that request that
		     // has m_parentPrevSpiderTime
		     // no no, we prefer the most recent spider request
		     // from thsi site in the logic above, so this is not
		     // necessary. mdw commented out.
		     //oldReq->m_wasParentIndexed != sreq->m_wasParentIndexed||
		     oldReq->m_isInjecting   != sreq->m_isInjecting   ||
		     oldReq->m_hasContent    != sreq->m_hasContent    ||
		     oldReq->m_isAddUrl      != sreq->m_isAddUrl      ||
		     oldReq->m_isPageReindex != sreq->m_isPageReindex ||
		     oldReq->m_forceDelete   != sreq->m_forceDelete    )
			// we are different enough to coexist
			goto addIt;
		// . if the same check who has the most recent added time
		// . if we are not the most recent, just do not add us
		// . no, now i want the oldest so we can do gbssDiscoveryTime
		//   and set sreq->m_discoveryTime accurately, above
		if ( sreq->m_addedTime >= oldReq->m_addedTime ) continue;
		// otherwise, erase over him
		dst     = restorePoint;
		lastKey = prevLastKey;
		// and add us over top of him
		goto addIt;
	}

	// free the old list
	//char *oldbuf  = list->m_alloc;
	//int32_t  oldSize = list->m_allocSize;

	// sanity check
	if ( dst < list->m_list || dst > list->m_list + list->m_listSize ) {
		char *xx=NULL;*xx=0; }

	// and stick our newly filtered list in there
	//list->m_list      = newList;
	list->m_listSize  = dst - newList;
	// set to end i guess
	list->m_listPtr   = dst;
	//list->m_allocSize = need;
	//list->m_alloc     = newList;
	list->m_listEnd   = list->m_list + list->m_listSize;
	list->m_listPtrHi = NULL;
	//KEYSET(list->m_lastKey,lastKey,list->m_ks);

	if ( lastKey ) KEYSET(list->m_lastKey,lastKey,list->m_ks);

	//mfree ( oldbuf , oldSize, "oldspbuf");
}

///////
//
// diffbot uses these for limiting crawls in a collection
//
///////

void gotCrawlInfoReply ( void *state , UdpSlot *slot);

static int32_t s_requests = 0;
static int32_t s_replies  = 0;
static int32_t s_validReplies  = 0;
static bool s_inUse = false;
// we initialize CollectionRec::m_updateRoundNum to 0 so make this 1
static int32_t s_updateRoundNum = 1;

// . just call this once per second for all collections
// . figure out how to backoff on collections that don't need it so much
// . ask every host for their crawl infos for each collection rec
void updateAllCrawlInfosSleepWrapper ( int fd , void *state ) {

	// debug test
	//int32_t mr = g_collectiondb.m_recs[0]->m_maxCrawlRounds;
	//log("mcr: %"INT32"",mr);

	// i don't know why we have locks in the lock table that are not
	// getting removed... so log when we remove an expired locks and see.
	// piggyback on this sleep wrapper call i guess...
	// perhaps the collection was deleted or reset before the spider
	// reply could be generated. in that case we'd have a dangling lock.
	removeExpiredLocks ( -1 );

	if ( s_inUse ) return;

	// "i" means to get incremental updates since last round
	// "f" means to get all stats
	char *request = "i";
	int32_t requestSize = 1;

	static bool s_firstCall = true;
	if ( s_firstCall ) {
		s_firstCall = false;
		request = "f";
	}

	s_inUse = true;

	// reset tmp crawlinfo classes to hold the ones returned to us
	//for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
	//	CollectionRec *cr = g_collectiondb.m_recs[i];
	//	if ( ! cr ) continue;
	//	cr->m_tmpCrawlInfo.reset();
	//}

	// send out the msg request
	for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		// skip if dead. no! we need replies from all hosts
		// otherwise our counts could be int16_t and we might end up
		// re-spidering stuff even though we've really hit maxToCrawl
		//if ( g_hostdb.isDead(i) ) {
		//	if ( g_conf.m_logDebugSpider )
		//		log("spider: skipping dead host #%"INT32" "
		//		    "when getting "
		//		    "crawl info",i);
		//	continue;
		//}
		// count it as launched
		s_requests++;
		// launch it
		if ( ! g_udpServer.sendRequest ( request,
						 requestSize,
						 0xc1 , // msgtype
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL, // retslot
						 NULL, // state
						 gotCrawlInfoReply ) ) {
			log("spider: error sending c1 request: %s",
			    mstrerror(g_errno));
			s_replies++;
		}
	}

	// return false if we blocked awaiting replies
	if ( s_replies < s_requests )
		return;

	// how did this happen?
	log("spider: got bogus crawl info replies!");
	s_inUse = false;
	return;

	// somehow we did not block... hmmmm...
	//char *xx=NULL;*xx=0;
	//gotCrawlInfoReply( cr , NULL );

	// we did not block...
	//return true;
}

// . Parms.cpp calls this when it receives our "spiderRoundNum" increment above
// . all hosts should get it at *about* the same time
void spiderRoundIncremented ( CollectionRec *cr ) {

	log("spider: incrementing spider round for coll %s to %"INT32" (%"UINT32")",
	    cr->m_coll,cr->m_spiderRoundNum,
	    (uint32_t)cr->m_spiderRoundStartTime);

	// . need to send a notification for this round
	// . we are only here because the round was incremented and
	//   Parms.cpp just called us... and that only happens in 
	//   doneSending... so do not send again!!!
	//cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 0;

	// . if we set sentCrawlDoneALert to 0 it will immediately
	//   trigger another round increment !! so we have to set these
	//   to true to prevent that.
	// . if we learnt that there really are no more urls ready to spider
	//   then we'll go to the next round. but that can take like
	//   SPIDER_DONE_TIMER seconds of getting nothing.
	cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = true;
	cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;

	cr->m_localCrawlInfo.m_pageDownloadSuccessesThisRound = 0;
	cr->m_localCrawlInfo.m_pageProcessSuccessesThisRound  = 0;
	cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound = 0;
	cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound  = 0;

	cr->localCrawlInfoUpdate();

	cr->m_needsSave = true;
}

void gotCrawlInfoReply ( void *state , UdpSlot *slot ) {

	// loop over each LOCAL crawlinfo we received from this host
	CrawlInfo *ptr   = (CrawlInfo *)(slot->m_readBuf);
	CrawlInfo *end   = (CrawlInfo *)(slot->m_readBuf+ slot->m_readBufSize);
	//int32_t       allocSize           = slot->m_readBufMaxSize;

	// host sending us this reply
	Host *h = slot->m_host;

	// assume it is a valid reply, not an error, like a udptimedout
	s_validReplies++;

	// reply is error? then use the last known good reply we had from him
	// assuming udp reply timed out. empty buf just means no update now!
	if ( ! slot->m_readBuf && g_errno ) {
		log("spider: got crawlinfo reply error from host %"INT32": %s. "
		    "spidering will be paused.",
		    h->m_hostId,mstrerror(g_errno));
		// just clear it
		g_errno = 0;
		// if never had any reply... can't be valid then
		if ( ! ptr ) s_validReplies--;
		/*
		// just use his last known good reply
		ptr = (CrawlInfo *)h->m_lastKnownGoodCrawlInfoReply;
		end = (CrawlInfo *)h->m_lastKnownGoodCrawlInfoReplyEnd;
		*/
	}
	// otherwise, if reply was good it is the last known good now!
	/*
	else {
		
		// free the old good one and replace it with the new one
		if ( h->m_lastKnownGoodCrawlInfoReply ) {
			//log("spider: skiipping possible bad free!!!! until we fix");
			mfree ( h->m_lastKnownGoodCrawlInfoReply , 
				h->m_replyAllocSize , 
				"lknown" );
		}
		// add in the new good in case he goes down in the future
		h->m_lastKnownGoodCrawlInfoReply    = (char *)ptr;
		h->m_lastKnownGoodCrawlInfoReplyEnd = (char *)end;
		// set new alloc size
		h->m_replyAllocSize = allocSize;
		// if valid, don't let him free it now!
		slot->m_readBuf = NULL;
	}
	*/

	// inc it
	s_replies++;

	if ( s_replies > s_requests ) { char *xx=NULL;*xx=0; }


	// crap, if any host is dead and not reporting it's number then
	// that seriously fucks us up because our global count will drop
	// and something that had hit a max limit, like maxToCrawl, will
	// now be under the limit and the crawl will resume.
	// what's the best way to fix this?
	//
	// perhaps, let's just keep the dead host's counts the same
	// as the last time we got them. or maybe the simplest way is to
	// just not allow spidering if a host is dead 

	// the sendbuf should never be freed! it points into collrec
	// it is 'i' or 'f' right now
	slot->m_sendBufAlloc = NULL;

	/////
	//  SCAN the list of CrawlInfos we received from this host, 
	//  one for each non-null collection
	/////

	// . add the LOCAL stats we got from the remote into the GLOBAL stats
	// . readBuf is null on an error, so check for that...
	// . TODO: do not update on error???
	for ( ; ptr < end ; ptr++ ) {

		QUICKPOLL ( slot->m_niceness );

		// get collnum
		collnum_t collnum = (collnum_t)(ptr->m_collnum);

		CollectionRec *cr = g_collectiondb.getRec ( collnum );
		if ( ! cr ) {
			log("spider: updatecrawlinfo collnum %"INT32" "
			    "not found",(int32_t)collnum);
			continue;
		}
		
		//CrawlInfo *stats = ptr;

		// just copy into the stats buf
		if ( ! cr->m_crawlInfoBuf.getBufStart() ) {
			int32_t need = sizeof(CrawlInfo) * g_hostdb.m_numHosts;
			cr->m_crawlInfoBuf.setLabel("cibuf");
			cr->m_crawlInfoBuf.reserve(need);
			// in case one was udp server timed out or something
			cr->m_crawlInfoBuf.zeroOut();
		}

		CrawlInfo *cia = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();

		if ( cia )
			gbmemcpy ( &cia[h->m_hostId] , ptr , sizeof(CrawlInfo));
		
		// debug
		// log("spd: got ci from host %"INT32" downloads=%"INT64", replies=%"INT32"",
		//     h->m_hostId,
		//     ptr->m_pageDownloadSuccessesThisRound,
		//     s_replies
		//     );

		// mark it for computation once we got all replies
		cr->m_updateRoundNum = s_updateRoundNum;
	}

	// keep going until we get all replies
	if ( s_replies < s_requests ) return;
	
	// if it's the last reply we are to receive, and 1 or more 
	// hosts did not have a valid reply, and not even a
	// "last known good reply" then then we can't do
	// much, so do not spider then because our counts could be
	// way off and cause us to start spidering again even though
	// we hit a maxtocrawl limit!!!!!
	if ( s_validReplies < s_replies ) {
		// this will tell us to halt all spidering
		// because a host is essentially down!
		s_countsAreValid = false;
		// might as well stop the loop here since we are
		// not updating our crawlinfo states.
		//break;
	}
	else {
		if ( ! s_countsAreValid )
			log("spider: got all crawlinfo replies. all shards "
			    "up. spidering back on.");
		s_countsAreValid = true;
	}


	// loop over 
	for ( int32_t x = 0 ; x < g_collectiondb.m_numRecs ; x++ ) {

		QUICKPOLL ( slot->m_niceness );

		// a niceness 0 routine could have nuked it?
		if ( x >= g_collectiondb.m_numRecs )
			break;

		CollectionRec *cr = g_collectiondb.m_recs[x];
		if ( ! cr ) continue;

		// must be in need of computation
		if ( cr->m_updateRoundNum != s_updateRoundNum ) continue;

		//log("spider: processing c=%s",cr->m_coll);

		CrawlInfo *gi = &cr->m_globalCrawlInfo;

		int32_t hadUrlsReady = gi->m_hasUrlsReadyToSpider;

		// clear it out
		gi->reset();

		// retrieve stats for this collection and scan all hosts
		CrawlInfo *cia = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();

		// if empty for all hosts, i guess no stats...
		if ( ! cia ) continue;

		for ( int32_t k = 0 ; k < g_hostdb.m_numHosts; k++ ) {
			QUICKPOLL ( slot->m_niceness );
			// get the CrawlInfo for the ith host
			CrawlInfo *stats = &cia[k];
			// point to the stats for that host
			int64_t *ss = (int64_t *)stats;
			int64_t *gs = (int64_t *)gi;
			// are stats crazy?
			bool crazy = false;
			for ( int32_t j = 0 ; j < NUMCRAWLSTATS ; j++ ) {
				// crazy stat?
				if ( *ss > 1000000000LL ||
				     *ss < -1000000000LL ) {
					log("spider: crazy stats %"INT64" "
					    "from host #%"INT32" coll=%s. "
					    "ignoring.",
					    *ss,k,cr->m_coll);
					crazy = true;
					break;
				}
				ss++;
			}
			// reset ptr to accumulate
			ss = (int64_t *)stats;
			for ( int32_t j = 0 ; j < NUMCRAWLSTATS ; j++ ) {
				// do not accumulate if corrupted.
				// probably mem got corrupted and it saved
				// to disk.
				if ( crazy ) break;
				*gs = *gs + *ss;
				gs++;
				ss++;
			}
			// . special counts
			// . assume round #'s match!
			//if ( ss->m_spiderRoundNum == 
			//     cr->m_localCrawlInfo.m_spiderRoundNum ) {
			gi->m_pageDownloadSuccessesThisRound +=
				stats->m_pageDownloadSuccessesThisRound;
			gi->m_pageProcessSuccessesThisRound +=
				stats->m_pageProcessSuccessesThisRound;
			//}

			if ( ! stats->m_hasUrlsReadyToSpider ) continue;
			// inc the count otherwise
			gi->m_hasUrlsReadyToSpider++;
			// . no longer initializing?
			// . sometimes other shards get the spider 
			//  requests and not us!!!
			if ( cr->m_spiderStatus == SP_INITIALIZING )
				cr->m_spiderStatus = SP_INPROGRESS;
			// i guess we are back in business even if
			// m_spiderStatus was SP_MAXTOCRAWL or 
			// SP_ROUNDDONE...
			cr->m_spiderStatus = SP_INPROGRESS;
			// unflag the sent flag if we had sent an alert
			// but only if it was a crawl round done alert,
			// not a maxToCrawl or maxToProcess or 
			// maxRounds alert.
			// we can't do this because on startup we end 
			// up setting hasUrlsReadyToSpider to true and
			// we may have already sent an email, and it 
			// gets RESET here when it shouldn't be
			//if(cr->m_localCrawlInfo.m_sentCrawlDoneAlert
			//== SP_ROUNDDONE )
			//cr->m_localCrawlInfo.m_sentCrawlDoneAlert=0;
			// revival?
			if ( ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider )
				log("spider: reviving crawl %s "
				    "from host %"INT32"", cr->m_coll,k);
		} // end loop over hosts


		// log("spider: %"INT64" (%"INT64") total downloads for c=%s",
		//     gi->m_pageDownloadSuccessesThisRound,
		//     gi->m_pageDownloadSuccesses,
		//     cr->m_coll);

		// revival?
		//if ( cr->m_tmpCrawlInfo.m_hasUrlsReadyToSpider &&
		//     ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		//	log("spider: reviving crawl %s (%"INT32")",cr->m_coll,
		//	    cr->m_tmpCrawlInfo.m_hasUrlsReadyToSpider);
		//}

		//bool has = cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider;
		if ( hadUrlsReady &&
		     // and it no longer does now...
		     ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
			log(LOG_INFO,
			    "spider: all %"INT32" hosts report "
			    "%s (%"INT32") has no "
			    "more urls ready to spider",
			    s_replies,cr->m_coll,(int32_t)cr->m_collnum);
			// set crawl end time
			cr->m_diffbotCrawlEndTime = getTimeGlobalNoCore();
		}


		// now copy over to global crawl info so things are not
		// half ass should we try to read globalcrawlinfo
		// in between packets received.
		//gbmemcpy ( &cr->m_globalCrawlInfo , 
		//	 &cr->m_tmpCrawlInfo ,
		//	 sizeof(CrawlInfo) );

		// turn not assume we are out of urls just yet if a host
		// in the network has not reported...
		//if ( g_hostdb.hasDeadHost() && has )
		//	cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;
		     

		// should we reset our "sent email" flag?
		bool reset = false;

		// can't reset if we've never sent an email out yet
		if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) reset = true;
		    
		// must have some urls ready to spider now so we can send
		// another email after another round of spidering
		if (!cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider) reset=false;

		// . if we have urls ready to be spidered then prepare to send
		//   another email/webhook notification.
		// . do not reset this flag if SP_MAXTOCRAWL etc otherwise we 
		//   end up sending multiple notifications, so this logic here
		//   is only for when we are done spidering a round, which 
		//   happens when hasUrlsReadyToSpider goes false for all 
		//   shards.
		if ( reset ) {
			log("spider: resetting sent crawl done alert to 0 "
			    "for coll %s",cr->m_coll);
			cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 0;
		}



		// update cache time
		cr->m_globalCrawlInfo.m_lastUpdateTime = getTime();
		
		// make it save to disk i guess
		cr->m_needsSave = true;

		// if spidering disabled in master controls then send no
		// notifications
		// crap, but then we can not update the round start time
		// because that is done in doneSendingNotification().
		// but why does it say all 32 report done, but then
		// it has urls ready to spider?
		if ( ! g_conf.m_spideringEnabled )
			continue;

		// and we've examined at least one url. to prevent us from
		// sending a notification if we haven't spidered anything
		// because no seed urls have been added/injected.
		//if ( cr->m_globalCrawlInfo.m_urlsConsidered == 0 ) return;
		if ( cr->m_globalCrawlInfo.m_pageDownloadAttempts == 0 &&
		     // if we don't have this here we may not get the
		     // pageDownloadAttempts in time from the host that
		     // did the spidering.
		     ! hadUrlsReady ) 
			continue;

		// if urls were considered and roundstarttime is still 0 then
		// set it to the current time...
		//if ( cr->m_spiderRoundStartTime == 0 )
		//	// all hosts in the network should sync with host #0 
		//      // on this
		//	cr->m_spiderRoundStartTime = getTimeGlobal();

		// but of course if it has urls ready to spider, do not send 
		// alert... or if this is -1, indicating "unknown".
		if ( cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) 
			continue;

		// update status if nto already SP_MAXTOCRAWL, etc. we might
		// just be flat out of urls
		if ( ! cr->m_spiderStatus || 
		     cr->m_spiderStatus == SP_INPROGRESS ||
		     cr->m_spiderStatus == SP_INITIALIZING )
			cr->m_spiderStatus = SP_ROUNDDONE;

		//
		// TODO: set the spiderstatus outright here...
		// maxtocrawl, maxtoprocess, etc. based on the counts.
		//


		// only host #0 sends emails
		if ( g_hostdb.m_myHost->m_hostId != 0 )
			continue;

		// . if already sent email for this, skip
		// . localCrawlInfo stores this value on disk so persistent
		// . we do it this way so SP_ROUNDDONE can be emailed and then
		//   we'd email SP_MAXROUNDS to indicate we've hit the maximum
		//   round count. 
		if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) {
			// debug
			logf(LOG_DEBUG,"spider: already sent alert for %s"
			     , cr->m_coll);
			continue;
		}

		
		// do email and web hook...
		sendNotificationForCollRec ( cr );

		// deal with next collection rec
	}

	// wait for more replies to come in
	//if ( s_replies < s_requests ) return;

	// initialize
	s_replies  = 0;
	s_requests = 0;
	s_validReplies = 0;
	s_inUse    = false;
	s_updateRoundNum++;
}

void handleRequestc1 ( UdpSlot *slot , int32_t niceness ) {
	//char *request = slot->m_readBuf;
	// just a single collnum
	if ( slot->m_readBufSize != 1 ) { char *xx=NULL;*xx=0;}

	char *req = slot->m_readBuf;

	if ( ! slot->m_host ) {
		log("handc1: no slot->m_host from ip=%s udpport=%i",
		    iptoa(slot->m_ip),(int)slot->m_port);
		g_errno = ENOHOSTS;
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	//if ( ! isClockSynced() ) {
	//}

	//collnum_t collnum = *(collnum_t *)request;
	//CollectionRec *cr = g_collectiondb.getRec(collnum);

	// deleted from under us? i've seen this happen
	//if ( ! cr ) {
	//	log("spider: c1: coll deleted returning empty reply");
	//	g_udpServer.sendReply_ass ( "", // reply
	//				    0, 
	//				    0 , // alloc
	//				    0 , //alloc size
	//				    slot );
	//	return;
	//}


	// while we are here update CrawlInfo::m_nextSpiderTime
	// to the time of the next spider request to spider.
	// if doledb is empty and the next rec in the waiting tree
	// does not have a time of zero, but rather, in the future, then
	// return that future time. so if a crawl is enabled we should
	// actively call updateCrawlInfo a collection every minute or
	// so.
	//cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = 1;

	//int64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();
	//int64_t nextSpiderTimeMS;
	// this will be 0 for ip's which have not had their SpiderRequests
	// in spiderdb scanned yet to get the best SpiderRequest, so we
	// just have to wait for that.
	/*
	nextSpiderTimeMS = sc->getEarliestSpiderTimeFromWaitingTree(0); 
	if ( ! sc->m_waitingTreeNeedsRebuild &&
	     sc->m_lastDoledbReadEmpty && 
	     cr->m_spideringEnabled &&
	     g_conf.m_spideringEnabled &&
	     nextSpiderTimeMS > nowGlobalMS +10*60*1000 ) 
		// turn off this flag, "ready queue" is empty
		cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = 0;

	// but send back a -1 if we do not know yet because we haven't
	// read the doledblists from disk from all priorities for this coll
	if ( sc->m_numRoundsDone == 0 )
		cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = -1;
	*/

	//int32_t now = getTimeGlobal();
	SafeBuf replyBuf;

	uint32_t now = (uint32_t)getTimeGlobalNoCore();

	uint64_t nowMS = gettimeofdayInMillisecondsGlobalNoCore();

	//SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {

		QUICKPOLL(slot->m_niceness);

		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;

		// int16_tcut
		CrawlInfo *ci = &cr->m_localCrawlInfo;

		// this is now needed for alignment by the receiver
		ci->m_collnum = i;

		SpiderColl *sc = cr->m_spiderColl;

		/////////
		//
		// ARE WE DONE SPIDERING?????
		//
		/////////

		// speed up qa pipeline
		uint32_t spiderDoneTimer = (uint32_t)SPIDER_DONE_TIMER;
		if ( cr->m_coll[0] == 'q' &&
		     cr->m_coll[1] == 'a' &&
		     strcmp(cr->m_coll,"qatest123")==0)
			spiderDoneTimer = 10;

		// if we haven't spidered anything in 1 min assume the
		// queue is basically empty...
		if ( ci->m_lastSpiderAttempt &&
		     ci->m_lastSpiderCouldLaunch &&
		     ci->m_hasUrlsReadyToSpider &&
		     // the next round we are waiting for, if any, must
		     // have had some time to get urls! otherwise we
		     // will increment the round # and wait just
		     // SPIDER_DONE_TIMER seconds and end up setting
		     // hasUrlsReadyToSpider to false!
		     now > cr->m_spiderRoundStartTime + spiderDoneTimer &&
		     // no spiders currently out. i've seen a couple out
		     // waiting for a diffbot reply. wait for them to
		     // return before ending the round...
		     sc && sc->m_spidersOut == 0 &&
		     // it must have launched at least one url! this should
		     // prevent us from incrementing the round # at the gb
		     // process startup
		     //ci->m_numUrlsLaunched > 0 &&
		     //cr->m_spideringEnabled &&
		     //g_conf.m_spideringEnabled &&
		     ci->m_lastSpiderAttempt - ci->m_lastSpiderCouldLaunch > 
		     spiderDoneTimer ) {

			// break it here for our collnum to see if
			// doledb was just lagging or not.
			bool printIt = true;
			if ( now < sc->m_lastPrinted ) printIt = false;
			if ( printIt ) sc->m_lastPrinted = now + 5;

			// doledb must be empty
			if ( ! sc->m_doleIpTable.isEmpty() ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "doledb not empty for coll=%s",cr->m_coll);
				goto doNotEnd;
			}

			uint64_t nextTimeMS ;
			nextTimeMS = sc->getNextSpiderTimeFromWaitingTree ( );

			// and no ips awaiting scans to get into doledb
			// except for ips needing scans 60+ seconds from now
			if ( nextTimeMS &&  nextTimeMS < nowMS + 60000 ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "waiting tree key is ready for scan "
				    "%"INT64" ms from now for coll=%s",
				    nextTimeMS - nowMS,cr->m_coll );
				goto doNotEnd;
			}

			// maybe wait for waiting tree population to finish
			if ( sc->m_waitingTreeNeedsRebuild ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "waiting tree is building for coll=%s",
				    cr->m_coll );
				goto doNotEnd;
			}

			// this is the MOST IMPORTANT variable so note it
			log(LOG_INFO,
			    "spider: coll %s has no more urls to spider",
			    cr->m_coll);
			// assume our crawl on this host is completed i guess
			ci->m_hasUrlsReadyToSpider = 0;
			// if changing status, resend local crawl info to all
			cr->localCrawlInfoUpdate();
			// save that!
			cr->m_needsSave = true;
		}

	doNotEnd:

		int32_t hostId = slot->m_host->m_hostId;

		bool sendIt = false;

		// . if not sent to host yet, send
		// . this will be true when WE startup, not them...
		// . but once we send it we set flag to false
		// . and if we update anything we send we set flag to true
		//   again for all hosts
		if ( cr->shouldSendLocalCrawlInfoToHost(hostId) ) 
			sendIt = true;

		// they can override. if host crashed and came back up
		// it might not have saved the global crawl info for a coll
		// perhaps, at the very least it does not have
		// the correct CollectionRec::m_crawlInfoBuf because we do
		// not save the array of crawlinfos for each host for
		// all collections.
		if ( req && req[0] == 'f' )
			sendIt = true;

		if ( ! sendIt ) continue;

		// note it
		// log("spider: sending ci for coll %s to host %"INT32"",
		//     cr->m_coll,hostId);
		
		// save it
		replyBuf.safeMemcpy ( ci , sizeof(CrawlInfo) );

		// do not re-do it unless it gets update here or in XmlDoc.cpp
		cr->sentLocalCrawlInfoToHost ( hostId );
	}

	g_udpServer.sendReply_ass ( replyBuf.getBufStart() , 
				    replyBuf.length() ,
				    replyBuf.getBufStart() , // alloc
				    replyBuf.getCapacity() , //alloc size
				    slot );

	// udp server will free this
	replyBuf.detachBuf();
}

bool getSpiderStatusMsg ( CollectionRec *cx , SafeBuf *msg , int32_t *status ) {

	if ( ! g_conf.m_spideringEnabled && ! cx->m_isCustomCrawl ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Spidering disabled in "
				       "master controls. You can turn it "
				       "back on there.");
	}

	if ( g_conf.m_readOnlyMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("In read-only mode. Spidering off.");
	}

	if ( g_dailyMerge.m_mergeMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Daily merge engaged, spidering "
				       "paused.");
	}

	// if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) {
	// 	*status = SP_ADMIN_PAUSED;
	// 	return msg->safePrintf("Too many UDP slots in use, "
	// 			       "spidering paused.");
	// }

	if ( g_repairMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("In repair mode, spidering paused.");
	}

	// do not spider until collections/parms in sync with host #0
	if ( ! g_parms.m_inSyncWithHost0 ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Parms not in sync with host #0, "
				       "spidering paused");
	}

	// don't spider if not all hosts are up, or they do not all
	// have the same hosts.conf.
	if ( g_pingServer.m_hostsConfInDisagreement ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Hosts.conf discrepancy, "
				       "spidering paused.");
	}

	uint32_t now = (uint32_t)getTimeGlobal();


	// hit crawl round max? this could be SP_ROUNDDONE and it doesn't
	// get converted to SP_MAXROUNDS until we call spiderDoledUrls()
	// so fix the crawlbot nightly smoke test by setting this here
	// to SP_MAXROUNDS.
	// smoketest msg = FAIL: testCrawlRounds (__main__.TestRepeatCrawl)
	// self.assertEqual(j['jobs'][0]['jobStatus']['status'],1,msg=self.name
	// AssertionError: 4 != 1 : 1227151934RepeatCrawlself.
	// assertEqual(j['jobs'][0]['jobStatus']['status'],1,msg=self.name)
	int32_t spiderStatus = cx->m_spiderStatus;
	if ( spiderStatus == SP_ROUNDDONE &&
	     cx->m_maxCrawlRounds > 0 &&
	     cx->m_isCustomCrawl &&
	     cx->m_spiderRoundNum >= cx->m_maxCrawlRounds )
		spiderStatus = SP_MAXROUNDS;


	// try to fix crawlbot nightly test complaining about job status
	// for TestRepeatCrawlWithMaxToCrawl
	if ( //(spiderStatus == SP_MAXTOCRAWL ||
	     // spiderStatus == SP_MAXTOPROCESS ) &&
	     spiderStatus == SP_INPROGRESS &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime &&
	     cx->m_maxCrawlRounds > 0 &&
	     cx->m_isCustomCrawl &&
	     cx->m_spiderRoundNum >= cx->m_maxCrawlRounds ) {
		*status = SP_MAXROUNDS;
		return msg->safePrintf ( "Job has reached maxRounds "
					 "limit." );
	}		

	// . 0 means not to RE-crawl
	// . indicate if we are WAITING for next round...
	if ( spiderStatus == SP_MAXTOCRAWL &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Jobs has reached maxToCrawl limit. "
				       "Next crawl round to start "
				       "in %"INT32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now));
	}

	if ( spiderStatus == SP_MAXTOPROCESS &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Jobs has reached maxToProcess limit. "
				       "Next crawl round to start "
				       "in %"INT32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now));
	}


	if ( spiderStatus == SP_MAXTOCRAWL ) {
		*status = SP_MAXTOCRAWL;
		return msg->safePrintf ( "Job has reached maxToCrawl "
					 "limit." );
	}

	if ( spiderStatus == SP_MAXTOPROCESS ) {
		*status = SP_MAXTOPROCESS;
		return msg->safePrintf ( "Job has reached maxToProcess "
					 "limit." );
	}

	if ( spiderStatus == SP_MAXROUNDS ) {
		*status = SP_MAXROUNDS;
		return msg->safePrintf ( "Job has reached maxRounds "
					 "limit." );
	}

	if ( ! cx->m_spideringEnabled ) {
		*status = SP_PAUSED;
		if ( cx->m_isCustomCrawl )
			return msg->safePrintf("Job paused.");
		else
			return msg->safePrintf("Spidering disabled "
					       "in spider controls.");
	}

	// . 0 means not to RE-crawl
	// . indicate if we are WAITING for next round...
	if ( cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Next crawl round to start "
				       "in %"INT32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now) );
	}

	// if spiderdb is empty for this coll, then no url
	// has been added to spiderdb yet.. either seed or spot
	//CrawlInfo *cg = &cx->m_globalCrawlInfo;
	//if ( cg->m_pageDownloadAttempts == 0 ) {
	//	*status = SP_NOURLS;
	//	return msg->safePrintf("Crawl is waiting for urls.");
	//}

	if ( spiderStatus == SP_INITIALIZING ) {
		*status = SP_INITIALIZING;
		return msg->safePrintf("Job is initializing.");
	}

	// if we had seeds and none were successfully crawled, do not just
	// print that the crawl completed.
	if ( cx->m_collectiveRespiderFrequency <= 0.0 &&
	     cx->m_isCustomCrawl &&
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider &&
	     cx->m_globalCrawlInfo.m_pageDownloadAttempts > 0 &&
	     cx->m_globalCrawlInfo.m_pageDownloadSuccesses == 0 ) {
		*status = SP_SEEDSERROR;
		return msg->safePrintf("Failed to crawl any seed.");
	}

	// if we sent an email simply because no urls
	// were left and we are not recrawling!
	if ( cx->m_collectiveRespiderFrequency <= 0.0 &&
	     cx->m_isCustomCrawl &&
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		*status = SP_COMPLETED;
		return msg->safePrintf("Job has completed and no "
			"repeat is scheduled.");
	}

	if ( spiderStatus == SP_ROUNDDONE && ! cx->m_isCustomCrawl ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf ( "Nothing currently "
					 "available to spider. "
					 "Change your url filters, try "
					 "adding new urls, or wait for "
					 "existing urls to be respidered.");
	}

	// let's pass the qareindex() test in qa.cpp... it wasn't updating
	// the status to done. it kept saying in progress.
	if ( ! cx->m_isCustomCrawl && 
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		//*status = SP_COMPLETED;
		*status = SP_INPROGRESS;
		return msg->safePrintf ( "Nothing currently "
					 "available to spider. "
					 "Change your url filters, try "
					 "adding new urls, or wait for "
					 "existing urls to be respidered.");
	}
		

	if ( spiderStatus == SP_ROUNDDONE ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf ( "Job round completed.");
	}


	if ( ! g_conf.m_spideringEnabled ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("All crawling temporarily paused "
				       "by root administrator for "
				       "maintenance.");
	}

	// out CollectionRec::m_globalCrawlInfo counts do not have a dead
	// host's counts tallied into it, which could make a difference on
	// whether we have exceed a maxtocrawl limit or some such, so wait...
	if ( ! s_countsAreValid && g_hostdb.hasDeadHost() ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("All crawling temporarily paused "
				       "because a shard is down.");
	}



	// otherwise in progress?
	*status = SP_INPROGRESS;
	if ( cx->m_isCustomCrawl )
		return msg->safePrintf("Job is in progress.");
	else
		return msg->safePrintf("Spider is in progress.");
}

bool hasPositivePattern ( char *pattern ) {
	char *p = pattern;
	// scan the " || " separated substrings
	for ( ; *p ; ) {
		// get beginning of this string
		char *start = p;
		// skip white space
		while ( *start && is_wspace_a(*start) ) start++;
		// done?
		if ( ! *start ) break;
		// find end of it
		char *end = start;
		while ( *end && end[0] != '|' )
			end++;
		// advance p for next guy
		p = end;
		// should be two |'s
		if ( *p ) p++;
		if ( *p ) p++;
		// skip if negative pattern
		if ( start[0] == '!' && start[1] && start[1]!='|' )
			continue;
		// otherwise it's a positive pattern
		return true;
	}
	return false;
}


// pattern is a ||-separted list of substrings
bool doesStringContainPattern ( char *content , char *pattern ) {
				//bool checkForNegatives ) {

	char *p = pattern;

	int32_t matchedOne = 0;
	bool hadPositive = false;

	int32_t count = 0;
	// scan the " || " separated substrings
	for ( ; *p ; ) {
		// get beginning of this string
		char *start = p;
		// skip white space
		while ( *start && is_wspace_a(*start) ) start++;
		// done?
		if ( ! *start ) break;
		// find end of it
		char *end = start;
		while ( *end && end[0] != '|' )
			end++;
		// advance p for next guy
		p = end;
		// should be two |'s
		if ( *p ) p++;
		if ( *p ) p++;
		// temp null this
		char c = *end;
		*end = '\0';
		// count it as an attempt
		count++;

		bool matchFront = false;
		if ( start[0] == '^' ) { start++; matchFront = true; }

		// if pattern is NOT/NEGATIVE...
		bool negative = false;
		if ( start[0] == '!' && start[1] && start[1]!='|' ) {
			start++;
			negative = true;
		}
		else
			hadPositive = true;

		// . is this substring anywhere in the document
		// . check the rawest content before converting to utf8 i guess
		// . suuport the ^ operator
		char *foundPtr = NULL;
		if ( matchFront ) {
			// if we match the front, set to bogus 0x01
			if ( strncmp(content,start,end-start)==0 ) 
				foundPtr =(char *)0x01;
		}
		else {
			foundPtr = strstr ( content , start ) ;
		}

		// debug log statement
		//if ( foundPtr )
		//	log("build: page %s matches ppp of \"%s\"",
		//	    m_firstUrl.m_url,start);
		// revert \0
		*end = c;

		// negative mean we should NOT match it
		if ( negative ) {
			// so if its matched, that is bad
			if ( foundPtr ) return false;
			continue;
		}

		// skip if not found
		if ( ! foundPtr ) continue;
		// did we find it?
		matchedOne++;
		// if no negatives, done
		//if ( ! checkForNegatives )
		//return true;
	}
	// if we had no attempts, it is ok
	if ( count == 0 ) return true;
	// must have matched one at least
	if ( matchedOne ) return true;
	// if all negative? i.e. !category||!author
	if ( ! hadPositive ) return true;
	// if we had an unfound substring...
	return false;
}

int32_t getFakeIpForUrl1 ( char *url1 ) {
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url1 );
	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	return firstIp;
}

int32_t getFakeIpForUrl2 ( Url *url2 ) {
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url2 );
	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	return firstIp;
}

// returns false and sets g_errno on error
bool SpiderRequest::setFromAddUrl ( char *url ) {
	// reset it
	reset();
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url );

	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	//int32_t firstIp = getFakeIpForUrl1 ( url );

	// ensure not crazy
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;

	// . now fill it up
	// . TODO: calculate the other values... lazy!!! (m_isRSSExt, 
	//         m_siteNumInlinks,...)
	m_isNewOutlink = 1;
	m_isAddUrl     = 1;
	m_addedTime    = (uint32_t)getTimeGlobal();//now;
	m_fakeFirstIp   = 1;
	//m_probDocId     = probDocId;
	m_firstIp       = firstIp;
	m_hopCount      = 0;

	// new: validate it?
	m_hopCountValid = 1;

	// its valid if root
	Url uu; uu.set ( url );
	if ( uu.isRoot() ) m_hopCountValid = true;
	// too big?
	if ( gbstrlen(url) > MAX_URL_LEN ) {
		g_errno = EURLTOOLONG;
		return false;
	}
	// the url! includes \0
	strcpy ( m_url , url );
	// call this to set m_dataSize now
	setDataSize();
	// make the key dude -- after setting url
	setKey ( firstIp , 0LL, false );
	// need a fake first ip lest we core!
	//m_firstIp = (pdocId & 0xffffffff);
	// how to set m_firstIp? i guess addurl can be throttled independently
	// of the other urls???  use the hash of the domain for it!
	int32_t  dlen;
	char *dom = getDomFast ( url , &dlen );
	// fake it for this...
	//m_firstIp = hash32 ( dom , dlen );
	// sanity
	if ( ! dom ) {
		g_errno = EBADURL;
		return false;
		//return sendReply ( st1 , true );
	}

	m_domHash32 = hash32 ( dom , dlen );
	// and "site"
	int32_t hlen = 0;
	char *host = getHostFast ( url , &hlen );
	m_siteHash32 = hash32 ( host , hlen );
	m_hostHash32 = m_siteHash32;

	return true;
}

bool SpiderRequest::setFromInject ( char *url ) {
	// just like add url
	if ( ! setFromAddUrl ( url ) ) return false;
	// but fix this
	m_isAddUrl = 0;
	m_isInjecting = 1;
	return true;
}


bool SpiderRequest::isCorrupt ( ) {

	// more corruption detection
	if ( m_hopCount < -1 ) {
		log("spider: got corrupt 5 spiderRequest");
		return true;
	}

	if ( m_dataSize > (int32_t)sizeof(SpiderRequest) ) {
		log("spider: got corrupt oversize spiderrequest %i",
		    (int)m_dataSize);
		return true;
	}

	if ( m_dataSize <= 0 ) {
		log("spider: got corrupt undersize spiderrequest %i",
		    (int)m_dataSize);
		return true;
	}

	// sanity check. check for http(s)://
	if ( m_url[0] == 'h' && m_url[1]=='t' && m_url[2]=='t' &&
	     m_url[3] == 'p' ) 
		return false;
	// might be a docid from a pagereindex.cpp
	if ( ! is_digit(m_url[0]) ) { 
		log("spider: got corrupt 1 spiderRequest");
		return true;
	}
	// if it is a digit\0 it is ok, not corrupt
	if ( ! m_url[1] )
		return false;
	// if it is not a digit after the first digit, that is bad
	if ( ! is_digit(m_url[1]) ) { 
		log("spider: got corrupt 2 spiderRequest");
		return true;
	}

	return false;
}

CollectionRec *SpiderLoop::getActiveList() {

	uint32_t nowGlobal = (uint32_t)getTimeGlobal();

	if ( nowGlobal >= m_recalcTime && m_recalcTimeValid )
		m_activeListValid = false;

	// we set m_activeListValid to false when enabling/disabling spiders,
	// when rebuilding url filters in Collectiondb.cpp rebuildUrlFilters()
	// and when updating the site list in updateSiteList(). all of these
	// could possible make an inactive collection active again, or vice
	// versa. also when deleting a collection in Collectiondb.cpp. this
	// keeps the below loop fast when we have thousands of collections
	// and most are inactive or empty/deleted.
	if ( ! m_activeListValid || m_activeListModified ) {
		buildActiveList();
		//m_crx = m_activeList;
		// recompute every 3 seconds, it seems kinda buggy!!
		m_recalcTime = nowGlobal + 3;
		m_recalcTimeValid = true;
		m_activeListModified = false;
	}

	return m_activeList;
}


void SpiderLoop::buildActiveList ( ) {

	//log("spider: rebuilding active list");

	//m_bookmark = NULL;

	// set current time, synced with host #0
	uint32_t nowGlobal = (uint32_t)getTimeGlobal();

	// when do we need to rebuild the active list again?
	m_recalcTimeValid = false;

	m_activeListValid = true;

	m_activeListCount = 0;

	// reset the linked list of active collections
	m_activeList = NULL;
	bool found = false;

	CollectionRec *tail = NULL;

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get rec
		CollectionRec *cr = g_collectiondb.m_recs[i];
		// skip if gone
		if ( ! cr ) continue;
		// stop if not enabled
		bool active = true;
		if ( ! cr->m_spideringEnabled ) active = false;
		if ( cr->m_spiderStatus == SP_MAXROUNDS ) active = false;
		if ( cr->m_spiderStatus == SP_MAXTOCRAWL ) active = false;
		if ( cr->m_spiderStatus == SP_MAXTOPROCESS ) active = false;
		//
		// . if doing respider with roundstarttime....
		// . roundstarttime is > 0 if m_collectiveRespiderFrequency
		//   is > 0, unless it has not been set to current time yet
		// . if m_collectiveRespiderFrequency was set to 0.0 then
		//   PageCrawlBot.cpp also sets m_roundStartTime to 0.
		//
		if ( nowGlobal < cr->m_spiderRoundStartTime ) {
			active = false;
			// no need to do this now since we recalc every
			// 3 seconds anyway...
			// if ( cr->m_spiderRoundStartTime < m_recalcTime ) {
			// 	m_recalcTime = cr->m_spiderRoundStartTime;
			// 	m_recalcTimeValid = true;
			// }
		}

		// MDW: let's not do this either unless it proves to be a prob
		//if ( sc->m_needsRebuild ) active = true;

		// we are at the tail of the linked list OR not in the list
		cr->m_nextActive = NULL;

		cr->m_isActive = false;

		if ( ! active ) continue;

		cr->m_isActive = true;

		m_activeListCount++;

		if ( cr == m_crx ) found = true;


		// if first one, set it to head
		if ( ! tail ) {
			m_activeList = cr;
			tail = cr;
			continue;
		}

		// if not first one, add it to end of tail
		tail->m_nextActive = cr;
		tail = cr;
	}

	// we use m_bookmark so we do not get into an infinite loop
	// in spider urls logic above
	if ( ! found ) {
		m_bookmark = NULL;
		m_crx = NULL;
	}
}
