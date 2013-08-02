// TODO: add m_downloadTimeTable to measure download speed of an IP
// TODO: consider adding m_titleWeight/m_bodyWeight/ etc. to url filters table.
//       like maybe make the wikipedia page titles really heavy..
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

Doledb g_doledb;

RdbTree *g_tree = NULL;

SpiderRequest *g_sreq = NULL;

long g_corruptCount = 0;

/////////////////////////
/////////////////////////      SPIDEREC
/////////////////////////

void SpiderRequest::setKey (long firstIp,
			    long long parentDocId,
			    long long uh48,
			    bool isDel) {
	m_key = g_spiderdb.makeKey ( firstIp,uh48,true,parentDocId , isDel );
	// set dataSize too!
	setDataSize();
}

void SpiderRequest::setDataSize ( ) {
	m_dataSize = (m_url - (char *)this) + gbstrlen(m_url) + 1 
		// subtract m_key and m_dataSize
		- sizeof(key128_t) - 4 ;
}

long SpiderRequest::print ( SafeBuf *sbarg ) {

	SafeBuf *sb = sbarg;
	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	sb->safePrintf("k.n1=0x%llx ",m_key.n1);
	sb->safePrintf("k.n0=0x%llx ",m_key.n0);

	sb->safePrintf("uh48=%llu ",getUrlHash48());
	sb->safePrintf("parentDocId=%llu ",getParentDocId());
	// if negtaive bail early now
	if ( (m_key.n0 & 0x01) == 0x00 ) {
		sb->safePrintf("[DELETE]");
		if ( ! sbarg ) printf("%s",sb->getBufStart() );
		return sb->length();
	}

	sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );
	sb->safePrintf("hostHash32=0x%lx ",m_hostHash32 );
	sb->safePrintf("domHash32=0x%lx ",m_domHash32 );
	sb->safePrintf("siteHash32=0x%lx ",m_siteHash32 );
	sb->safePrintf("siteNumInlinks=%li ",m_siteNumInlinks );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	timeStruct = gmtime ( &m_addedTime );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf("addedTime=%s(%lu) ",time,m_addedTime );

	sb->safePrintf("parentFirstIp=%s ",iptoa(m_parentFirstIp) );
	sb->safePrintf("parentHostHash32=0x%lx ",m_parentHostHash32 );
	sb->safePrintf("parentDomHash32=0x%lx ",m_parentDomHash32 );
	sb->safePrintf("parentSiteHash32=0x%lx ",m_parentSiteHash32 );

	sb->safePrintf("hopCount=%li ",m_hopCount );

	//timeStruct = gmtime ( &m_spiderTime );
	//time[0] = 0;
	//if ( m_spiderTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("spiderTime=%s(%lu) ",time,m_spiderTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("pubDate=%s(%lu) ",time,m_pubDate );

	sb->safePrintf("ufn=%li ", (long)m_ufn);
	// why was this unsigned?
	sb->safePrintf("priority=%li ", (long)m_priority);

	//sb->safePrintf("errCode=%s(%lu) ",mstrerror(m_errCode),m_errCode );
	//sb->safePrintf("crawlDelay=%lims ",m_crawlDelay );
	//sb->safePrintf("httpStatus=%li ",(long)m_httpStatus );
	//sb->safePrintf("retryNum=%li ",(long)m_retryNum );
	//sb->safePrintf("langId=%s(%li) ",
	//	       getLanguageString(m_langId),(long)m_langId );
	//sb->safePrintf("percentChanged=%li%% ",(long)m_percentChanged );

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
	if ( m_isWWWSubdomain  ) sb->safePrintf("WWWSUBDOMAIN ");

	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");

	unsigned long gid = g_spiderdb.getGroupId(m_firstIp);
	sb->safePrintf("gid=%lu ",gid);

	sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) 
		printf("%s",sb->getBufStart() );

	return sb->length();
}

void SpiderReply::setKey (long firstIp,
			  long long parentDocId,
			  long long uh48,
			  bool isDel) {
	m_key = g_spiderdb.makeKey ( firstIp,uh48,false,parentDocId , isDel );
	// set dataSize too!
	m_dataSize = sizeof(SpiderReply) - sizeof(key128_t) - 4;
}

long SpiderReply::print ( SafeBuf *sbarg ) {

	SafeBuf *sb = sbarg;
	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	sb->safePrintf("k.n1=0x%llx ",m_key.n1);
	sb->safePrintf("k.n0=0x%llx ",m_key.n0);

	sb->safePrintf("uh48=%llu ",getUrlHash48());
	sb->safePrintf("parentDocId=%llu ",getParentDocId());

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
	timeStruct = gmtime ( &m_spideredTime );
	time[0] = 0;
	if ( m_spideredTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("spideredTime=%s(%lu) ",time,m_spideredTime);

	sb->safePrintf("siteNumInlinks=%li ",m_siteNumInlinks );

	timeStruct = gmtime ( &m_pubDate );
	time[0] = 0;
	if ( m_pubDate != 0 && m_pubDate != -1 ) 
		strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("pubDate=%s(%li) ",time,m_pubDate );

	//sb->safePrintf("newRequests=%li ",m_newRequests );
	sb->safePrintf("ch32=%lu ",(long)m_contentHash32);

	sb->safePrintf("crawldelayms=%lims ",m_crawlDelayMS );
	sb->safePrintf("httpStatus=%li ",(long)m_httpStatus );
	sb->safePrintf("langId=%s(%li) ",
		       getLanguageString(m_langId),(long)m_langId );

	if ( m_errCount )
		sb->safePrintf("errCount=%li ",(long)m_errCount);

	sb->safePrintf("errCode=%s(%lu) ",mstrerror(m_errCode),m_errCode );

	//if ( m_isSpam ) sb->safePrintf("ISSPAM ");
	if ( m_isRSS ) sb->safePrintf("ISRSS ");
	if ( m_isPermalink ) sb->safePrintf("ISPERMALINK ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	//if ( m_deleted ) sb->safePrintf("DELETED ");
	if ( m_isIndexed ) sb->safePrintf("ISINDEXED ");

	if ( m_hasAddress    ) sb->safePrintf("HASADDRESS ");
	if ( m_hasTOD        ) sb->safePrintf("HASTOD ");
	if ( m_hasSiteVenue  ) sb->safePrintf("HASSITEVENUE ");
	if ( m_isContacty    ) sb->safePrintf("CONTACTY ");

	//sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) 
		printf("%s",sb->getBufStart() );

	return sb->length();
}



long SpiderRequest::printToTable ( SafeBuf *sb , char *status ,
				   XmlDoc *xd ) {

	sb->safePrintf("<tr>\n");

	// show elapsed time
	if ( xd ) {
		long long now = gettimeofdayInMilliseconds();
		long long elapsed = now - xd->m_startTime;
		sb->safePrintf(" <td>%llims</td>\n",elapsed);
	}

	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",m_url);
	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",status );

	sb->safePrintf(" <td>%li</td>\n",(long)m_priority);
	sb->safePrintf(" <td>%li</td>\n",(long)m_ufn);

	sb->safePrintf(" <td>%s</td>\n",iptoa(m_firstIp) );
	sb->safePrintf(" <td>%li</td>\n",(long)m_errCount );

	sb->safePrintf(" <td>%llu</td>\n",getUrlHash48());

	sb->safePrintf(" <td>0x%lx</td>\n",m_hostHash32 );
	sb->safePrintf(" <td>0x%lx</td>\n",m_domHash32 );
	sb->safePrintf(" <td>0x%lx</td>\n",m_siteHash32 );
	sb->safePrintf(" <td>%li</td>\n",m_siteNumInlinks );
	//sb->safePrintf(" <td>%li</td>\n",m_pageNumInlinks );
	sb->safePrintf(" <td>%li</td>\n",m_hopCount );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	timeStruct = gmtime ( &m_addedTime );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf(" <td><nobr>%s(%lu)</nobr></td>\n",time,m_addedTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf(" <td>%s(%lu)</td>\n",time,m_pubDate );

	//sb->safePrintf(" <td>%s(%lu)</td>\n",mstrerror(m_errCode),m_errCode);
	//sb->safePrintf(" <td>%lims</td>\n",m_crawlDelay );
	sb->safePrintf(" <td>%s</td>\n",iptoa(m_parentFirstIp) );
	sb->safePrintf(" <td>%llu</td>\n",getParentDocId() );
	sb->safePrintf(" <td>0x%lx</td>\n",m_parentHostHash32);
	sb->safePrintf(" <td>0x%lx</td>\n",m_parentDomHash32 );
	sb->safePrintf(" <td>0x%lx</td>\n",m_parentSiteHash32 );
	//sb->safePrintf(" <td>%li</td>\n",(long)m_httpStatus );
	//sb->safePrintf(" <td>%li</td>\n",(long)m_retryNum );
	//sb->safePrintf(" <td>%s(%li)</td>\n",
	//	       getLanguageString(m_langId),(long)m_langId );
	//sb->safePrintf(" <td>%li%%</td>\n",(long)m_percentChanged );

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

	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");



	sb->safePrintf("</nobr></td>\n");

	sb->safePrintf("</tr>\n");

	return sb->length();
}


long SpiderRequest::printTableHeader ( SafeBuf *sb , bool currentlySpidering) {

	sb->safePrintf("<tr>\n");

	// how long its been being spidered
	if ( currentlySpidering )
		sb->safePrintf(" <td><b>elapsed</b></td>\n");

	sb->safePrintf(" <td><b>url</b></td>\n");
	sb->safePrintf(" <td><b>status</b></td>\n");

	sb->safePrintf(" <td><b>pri</b></td>\n");
	sb->safePrintf(" <td><b>ufn</b></td>\n");

	sb->safePrintf(" <td><b>firstIp</b></td>\n");
	sb->safePrintf(" <td><b>errCount</b></td>\n");
	sb->safePrintf(" <td><b>urlHash48</b></td>\n");
	sb->safePrintf(" <td><b>hostHash32</b></td>\n");
	sb->safePrintf(" <td><b>domHash32</b></td>\n");
	sb->safePrintf(" <td><b>siteHash32</b></td>\n");
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
	sb->safePrintf(" <td><b>parentHostHash32</b></td>\n");
	sb->safePrintf(" <td><b>parentDomHash32</b></td>\n");
	sb->safePrintf(" <td><b>parentSiteHash32</b></td>\n");
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
long Spiderdb::print( char *srec ) {
	// get if request or reply and print it
	if ( isSpiderRequest ( (key128_t *)srec ) )
		((SpiderRequest *)srec)->print(NULL);
	else
		((SpiderReply *)srec)->print(NULL);
	return 0;
}


bool Spiderdb::init ( ) {

	long maxMem = 200000000;
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	long maxTreeNodes  = maxMem  / 78;
	// . really we just cache the first 64k of each priority list
	// . used only by SpiderLoop
	//long maxCacheNodes = 32;
	// we use the same disk page size as indexdb (for rdbmap.cpp)
	long pageSize = GB_INDEXDB_PAGE_SIZE;
	// disk page cache mem, 100MB on gk0 now
	long pcmem = 20000000;//g_conf.m_spiderdbMaxDiskPageCacheMem;
	// keep this low if we are the tmp cluster
	if ( g_hostdb.m_useTmpCluster ) pcmem = 0;

	// key parser checks
	//long      ip         = 0x1234;
	char      priority   = 12;
	long      spiderTime = 0x3fe96610;
	long long urlHash48  = 0x1234567887654321LL & 0x0000ffffffffffffLL;
	//long long pdocid     = 0x567834222LL;
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
	long long docId = 123456789;
	long firstIp = 0x23991688;
	key128_t sk = g_spiderdb.makeKey ( firstIp,urlHash48,1,docId,false);
	if ( ! g_spiderdb.isSpiderRequest (&sk) ) { char *xx=NULL;*xx=0; }
	if ( g_spiderdb.getUrlHash48(&sk) != urlHash48){char *xx=NULL;*xx=0;}
	if ( g_spiderdb.getFirstIp(&sk) != firstIp) {char *xx=NULL;*xx=0;}

	// we now use a page cache
	if ( ! m_pc.init ( "spiderdb", 
			   RDB_SPIDERDB ,
			   pcmem     ,
			   pageSize  ,
			   true      ,  // use shared mem?
			   false     )) // minimizeDiskSeeks?
		return log(LOG_INIT,"spiderdb: Init failed.");

	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "spiderdb"   ,
			    true    , // dedup
			    -1      , // fixedDataSize
			    -1,//g_conf.m_spiderdbMinFilesToMerge , mintomerge
			    maxMem,//g_conf.m_spiderdbMaxTreeMem ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0,//g_conf.m_spiderdbMaxCacheMem,
			    0,//maxCacheNodes               ,
			    false                       , // half keys?
			    false                       , // save cache?
			    &m_pc                       ,
			    false                       ,
			    false                       ,
			    sizeof(key128_t)            );
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Spiderdb::init2 ( long treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	long maxTreeNodes  = treeMem  / 78;
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
			    NULL          );// &m_pc 
}


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

bool Spiderdb::verify ( char *coll ) {
	//return true;
	log ( LOG_INFO, "db: Verifying Spiderdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	//long minRecSizes = 64000;
	
	if ( ! msg5.getList ( RDB_SPIDERDB  ,
			      coll          ,
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

	long count = 0;
	long got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		char *k = list.getCurrentRec();
		//key_t k = list.getCurrentKey();
		count++;
		// what group's spiderdb should hold this rec
		uint32_t groupId = g_hostdb.getGroupId ( RDB_SPIDERDB , k );
		if ( groupId == g_hostdb.m_groupId ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %li records in spiderdb, "
		     "only %li belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Spiderdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_INFO,"db: Spiderdb passed verification successfully for %li "
	      "recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

key128_t Spiderdb::makeKey ( long      firstIp     ,
			     long long urlHash48   , 
			     bool      isRequest   ,
			     long long parentDocId ,
			     bool      isDel       ) {
	key128_t k;
	k.n1 = (unsigned long)firstIp;
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
	long maxTreeMem    = 150000000; // 150MB
	long maxTreeNodes  = maxTreeMem / 78;
	// we use the same disk page size as indexdb (for rdbmap.cpp)
	long pageSize = GB_INDEXDB_PAGE_SIZE;
	// disk page cache mem, hard code to 5MB
	long pcmem = 5000000; // g_conf.m_spiderdbMaxDiskPageCacheMem;
	// keep this low if we are the tmp cluster
	if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// we now use a page cache
	if ( ! m_pc.init ( "doledb"  , 
			   RDB_DOLEDB ,
			   pcmem     ,
			   pageSize  ,
			   true      ,  // use shared mem?
			   false     )) // minimizeDiskSeeks?
		return log(LOG_INIT,"doledb: Init failed.");

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
			    &m_pc                       ))
		return false;
	return true;
}

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
#define MAX_NODES (30)

// a global class extern'd in .h file
SpiderCache g_spiderCache;

SpiderCache::SpiderCache ( ) {
	m_numSpiderColls   = 0;
	//m_isSaving = false;
}

// returns false and set g_errno on error
bool SpiderCache::init ( ) {

	for ( long i = 0 ; i < MAX_COLL_RECS ; i++ )
		m_spiderColls[i] = NULL;

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
	for ( long i = 0 ; i < m_numSpiderColls ; i++ ) {
		SpiderColl *sc = m_spiderColls[i];
		if ( ! sc ) continue;
		RdbTree *tree = &sc->m_waitingTree;
		char *filename = "waitingtree";
		char dir[1024];
		sprintf(dir,"%scoll.%s.%li",g_hostdb.m_dir,
			sc->m_coll,(long)sc->m_collnum);
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
	for ( long i = 0 ; i < m_numSpiderColls ; i++ ) {
		SpiderColl *sc = m_spiderColls[i];
		if ( ! sc ) continue;
		if ( sc->m_waitingTree.m_needsSave ) return true;
		// also the doleIpTable
		//if ( sc->m_doleIpTable.m_needsSave ) return true;
	}
	return false;
}

void SpiderCache::reset ( ) {
	// loop over all SpiderColls and get the best
	for ( long i = 0 ; i < m_numSpiderColls ; i++ ) {
		SpiderColl *sc = m_spiderColls[i];
		if ( ! sc ) continue;
		sc->reset();
		mdelete ( sc , sizeof(SpiderColl) , "SpiderCache" );
		delete ( sc );
		m_spiderColls[i] = NULL;
	}
	m_numSpiderColls = 0;
}

// get SpiderColl for a collection
SpiderColl *SpiderCache::getSpiderColl ( collnum_t collnum ) {
	// return it if non-NULL
	if ( m_spiderColls [ collnum ] ) return m_spiderColls [ collnum ];
	// if spidering disabled, do not bother creating this!
	//if ( ! g_conf.m_spideringEnabled ) return NULL;
	// shortcut
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	// if spidering disabled, do not bother creating this!
	//if ( ! cr->m_spideringEnabled ) return NULL;
	// cast it
	SpiderColl *sc;
	// make it
	try { sc = new(SpiderColl); }
	catch ( ... ) {
		log("spider: failed to make SpiderColl for collnum=%li",
		    (long)collnum);
		return NULL;
	}
	// register it
	mnew ( sc , sizeof(SpiderColl), "spcoll" );
	// store it
	m_spiderColls [ collnum ] = sc;
	// update this
	if ( m_numSpiderColls < collnum + 1 )
		m_numSpiderColls = collnum + 1;
	// set this
	sc->m_collnum = collnum;
	// save this
	strcpy ( sc->m_coll , cr->m_coll );
	// set this
	if ( ! strcmp ( cr->m_coll,"test" ) ) sc->m_isTestColl = true;
	else                                  sc->m_isTestColl = false;
	
	// set first doledb scan key
	sc->m_nextDoledbKey.setMin();
	// load its tables from disk
	sc->load();
	// set this
	sc->m_cr = cr;
	// sanity check
	if ( ! cr ) { char *xx=NULL;*xx=0; }
	// that was it
	return sc;
}

/////////////////////////
/////////////////////////      SpiderColl
/////////////////////////

SpiderColl::SpiderColl () {
	m_gettingList = false;
	m_gettingList2 = false;
	m_lastScanTime = 0;
	m_numAdded = 0;
	m_numBytesScanned = 0;
	m_lastPrintCount = 0;
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
}

// load the tables that we set when m_doInitialScan is true
bool SpiderColl::load ( ) {
	// error?
	long err = 0;
	// make the dir
	char *coll = g_collectiondb.getColl(m_collnum);
	// sanity check
	if ( ! coll || coll[0]=='\0' ) { char *xx=NULL;*xx=0; }

	// reset this once
	m_msg4Avail    = true;
	m_isPopulating = false;

	if ( ! m_lastDownloadCache.init ( 35000      , // maxcachemem,
					  8          , // fixed data size (MS)
					  false      , // support lists?
					  500        , // max nodes
					  false      , // use half keys?
					  "downcache", // dbname
					  false      , // load from disk?
					  12         , // key size (firstip)
					  12         , // data key size?
					  -1         ))// numPtrsMax
		return log("spider: dcache init failed");

	if (!m_sniTable.set   ( 4,8,5000,NULL,0,false,MAX_NICENESS,"snitbl") )
		return false;
	if (!m_cdTable.set    (4,8,3000,NULL,0,false,MAX_NICENESS,"cdtbl"))
		return false;
	// doledb seems to have like 32000 entries in it
	if (!m_doleIpTable.set(4,4,128000,NULL,0,false,MAX_NICENESS,"doleip"))
		return false;
	// this should grow dynamically...
	if (!m_waitingTable.set (4,8,3000,NULL,0,false,MAX_NICENESS,"waittbl"))
		return false;
	// . a tree of keys, key is earliestSpiderTime|ip (key=12 bytes)
	// . earliestSpiderTime is 0 if unknown
	// . max nodes is 1M but we should grow dynamically! TODO
	// . let's up this to 5M because we are hitting the limit in some
	//   test runs...
	// . try going to 20M now since we hit it again...
	if (!m_waitingTree.set(0,-1,true,20000000,true,"waittree2",
			       false,"waitingtree",sizeof(key_t)))return false;

	// make dir
	char dir[500];
	sprintf(dir,"%scoll.%s.%li",g_hostdb.m_dir,coll,(long)m_collnum);
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

	// . do this now just to keep everything somewhat in sync
	// . we lost dmoz.org and could not get it back in because it was
	//   in the doleip table but NOT in doledb!!!
	if ( ! makeDoleIPTable() ) return false;

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

	log("spider: making dole ip table for %s",m_coll);

	key_t startKey ; startKey.setMin();
	key_t endKey   ; endKey.setMax();
	key_t lastKey  ; lastKey.setMin();
	// turn off threads for this so it blocks
	bool enabled = g_threads.areThreadsEnabled();
	// turn off regardless
	g_threads.disableThreads();
	// get a meg at a time
	long minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_DOLEDB    ,
			      m_coll        ,
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
	// shortcut
	long minSize=(long)(sizeof(SpiderRequest)+sizeof(key_t)+4-MAX_URL_LEN);
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
		long recSize = list.getCurrentRecSize();
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
	startKey += (unsigned long) 1;
	// watch out for wrap around
	if ( startKey >= *(key_t *)list.getLastKey() ) goto loop;
 done:
	log("spider: making dole ip table done.");
	// re-enable threads
	if ( enabled ) g_threads.enableThreads();
	// we wrapped, all done
	return true;
}

key_t makeWaitingTreeKey ( uint64_t spiderTimeMS , long firstIp ) {
	// sanity
	if ( ((long long)spiderTimeMS) < 0 ) { char *xx=NULL;*xx=0; }
	// make the wait tree key
	key_t wk;
	wk.n1 = (spiderTimeMS>>32);
	wk.n0 = (spiderTimeMS&0xffffffff);
	wk.n0 <<= 32;
	wk |= (unsigned long)firstIp;
	// sanity
	if ( wk.n1 & 0x8000000000000000LL ) { char *xx=NULL;*xx=0; }
	return wk;
}

// this one has to scan all of spiderdb
bool SpiderColl::makeWaitingTree ( ) {
	log("spider: making waiting tree for %s",m_coll);

	key128_t startKey ; startKey.setMin();
	key128_t endKey   ; endKey.setMax();
	key128_t lastKey  ; lastKey.setMin();
	// turn off threads for this so it blocks
	bool enabled = g_threads.areThreadsEnabled();
	// turn off regardless
	g_threads.disableThreads();
	// get a meg at a time
	long minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_SPIDERDB  ,
			      m_coll        ,
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
		long recSize = list.getCurrentRecSize();
		// zero?
		if ( recSize <= 0 ) { char *xx=NULL;*xx=0; }
		// 16 is bad too... wtf is this?
		if ( recSize <= 16 ) continue;
		// skip replies
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) continue;
		// get request
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// get first ip
		long firstIp = sreq->m_firstIp;
		// skip if in dole ip table
		if ( m_doleIpTable.isInTable ( &firstIp ) ) continue;
		// make the key. use 1 for spiderTimeMS. this tells the
		// spider loop that it is temporary and should be updated
		key_t wk = makeWaitingTreeKey ( 1 , firstIp );
		// ok, add to waiting tree
		long wn = m_waitingTree.addKey ( &wk );
		if ( wn < 0 ) {
			log("spider: makeWaitTree: %s",mstrerror(g_errno));
			return false;
		}
		// a tmp var
		long long fakeone = 1LL;
		// add to table now since its in the tree
		if ( ! m_waitingTable.addKey ( &firstIp , &fakeone ) ) {
			log("spider: makeWaitTree2: %s",mstrerror(g_errno));
			m_waitingTree.deleteNode ( wn , true );
			return false;
		}
	}
	startKey = *(key128_t *)list.getLastKey();
	startKey += (unsigned long) 1;
	// watch out for wrap around
	if ( startKey >= *(key128_t *)list.getLastKey() ) goto loop;
 done:
	log("spider: making waiting tree done.");
	// re-enable threads
	if ( enabled ) g_threads.enableThreads();
	// we wrapped, all done
	return true;
}

// for debugging query reindex i guess
long long SpiderColl::getEarliestSpiderTimeFromWaitingTree ( long firstIp ) {
	// make the key. use 0 as the time...
	key_t wk = makeWaitingTreeKey ( 0, firstIp );
	// set node from wait tree key. this way we can resume from a prev key
	long node = m_waitingTree.getNextNode ( 0, (char *)&wk );
	// if empty, stop
	if ( node < 0 ) return -1;
	// breathe
	QUICKPOLL(MAX_NICENESS);
	// get the key
	key_t *k = (key_t *)m_waitingTree.getKey ( node );
	// ok, we got one
	long storedFirstIp = (k->n0) & 0xffffffff;
	// match?
	if ( storedFirstIp != firstIp ) return -1;
	// get the time
	unsigned long long spiderTimeMS = k->n1;
	// shift upp
	spiderTimeMS <<= 32;
	// or in
	spiderTimeMS |= (k->n0 >> 32);
	// make into seconds
	return spiderTimeMS;
}


bool SpiderColl::makeWaitingTable ( ) {
	logf(LOG_INFO,"spider: making waiting table for %s.",m_coll);
	long node = m_waitingTree.getFirstNode();
	for ( ; node >= 0 ; node = m_waitingTree.getNextNode(node) ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// get key
		key_t *key = (key_t *)m_waitingTree.getKey(node);
		// get ip from that
		long ip = (key->n0) & 0xffffffff;
		// spider time is up top
		uint64_t spiderTimeMS = (key->n1);
		spiderTimeMS <<= 32;
		spiderTimeMS |= ((key->n0) >> 32);
		// store in waiting table
		if ( ! m_waitingTable.addKey(&ip,&spiderTimeMS) ) return false;
	}
	logf(LOG_INFO,"spider: making waiting table done.");
	return true;
}

SpiderColl::~SpiderColl () {
	reset();
}

void SpiderColl::reset ( ) {

	// reset these for SpiderLoop;
	m_nextDoledbKey.setMin();
	m_didRound = false;
	m_pri = MAX_SPIDER_PRIORITIES - 1;
	m_twinDied = false;
	m_lastUrlFiltersUpdate = 0;

	char *coll = "unknown";
	if ( m_coll[0] ) coll = m_coll;
	logf(LOG_DEBUG,"spider: resetting spider cache coll=%s",coll);

	m_ufnMapValid = false;

	m_doleIpTable .reset();
	m_cdTable     .reset();
	m_sniTable    .reset();
	m_waitingTable.reset();
	m_waitingTree .reset();
	m_waitingMem  .reset();
}

bool SpiderColl::updateSiteNumInlinksTable ( long siteHash32, 
					     long sni, 
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

	// skip if not assigned to us for doling
	if ( ! isAssignedToUs ( srep->m_firstIp ) )
		return true;

	// update the latest siteNumInlinks count for this "site" (repeatbelow)
	updateSiteNumInlinksTable ( srep->m_siteHash32, 
				    srep->m_siteNumInlinks,
				    srep->m_spideredTime );

	// . skip the rest if injecting
	// . otherwise it triggers a lookup for this firstip in spiderdb to
	//   get a new spider request to add to doledb
	if ( srep->m_fromInjectionRequest )
		return true;

	// clear error for this
	g_errno = 0;

	// . update the latest crawl delay for this domain
	// . only add to the table if we had a crawl delay
	// . -1 implies an invalid or unknown crawl delay
	if ( srep->m_crawlDelayMS >= 0 ) {
		// use the domain hash for this guy! since its from robots.txt
		uint64_t *cdp ;
		cdp = (uint64_t *)m_cdTable.getValue32(srep->m_domHash32);
		// update it only if better or empty
		bool update = false;
		if      ( ! cdp )
			update = true;
		else if (((*cdp)&0xffffffff)<(uint32_t)srep->m_spideredTime) 
			update = true;
		// update m_sniTable if we should
		if ( update ) {
			// . make new data for this key
			// . lower 32 bits is the addedTime
			// . upper 32 bits is the siteNumInlinks
			uint64_t nv = (uint32_t)(srep->m_crawlDelayMS);
			// shift up
			nv <<= 32;
			// or in time
			nv |= (uint32_t)srep->m_spideredTime;
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
	if ( srep->m_downloadEndTime )
		m_lastDownloadCache.addLongLong ( m_collnum,
						  srep->m_firstIp ,
						  srep->m_downloadEndTime );
	// log this for now
	if ( g_conf.m_logDebugSpider )
		log("spider: adding last download end time %lli for "
		    "ip=%s uh48=%llu indexcode=\"%s\" coll=%li "
		    "to SpiderColl::m_lastDownloadCache",
		    srep->m_downloadEndTime,
		    iptoa(srep->m_firstIp),srep->getUrlHash48(),
		    mstrerror(srep->m_errCode),
		    (long)m_collnum);
	// ignore errors from that, it's just a cache
	g_errno = 0;
	// sanity check - test cache
	//if ( g_conf.m_logDebugSpider && srep->m_downloadEndTime ) {
	//	long long last = m_lastDownloadCache.getLongLong ( m_collnum ,
	//						     srep->m_firstIp ,
	//							   -1,// maxAge
	//							   true );//pro
	//	if ( last != srep->m_downloadEndTime ) { char *xx=NULL;*xx=0;}
	//}

	// . decrement doledb table ip count for firstIp
	// . update how many per ip we got doled
	long *score = (long *)m_doleIpTable.getValue32 ( srep->m_firstIp );

	// wtf! how did this spider without being doled?
	if ( ! score ) {
		if ( ! srep->m_fromInjectionRequest )
			log("spider: corruption. received spider reply whose "
			    "ip has no entry in dole ip table. firstip=%s",
			    iptoa(srep->m_firstIp));
		goto skip;
	}

	// reduce it
	*score = *score - 1;
	// remove if zero
	if ( *score == 0 ) {
		// this can file if writes are disabled on this hashtablex
		// because it is saving
		m_doleIpTable.removeKey ( &srep->m_firstIp );
		// sanity check
		//if ( ! m_doleIpTable.m_isWritable ) { char *xx=NULL;*xx=0; }
	}
	// wtf!
	if ( *score < 0 ) { char *xx=NULL;*xx=0; }
	// all done?
	if ( g_conf.m_logDebugSpider ) {
		// log that too!
		logf(LOG_DEBUG,"spider: undoling srep uh48=%llu "
		     "parentdocid=%llu ipdolecount=%li",
		     srep->getUrlHash48(),srep->getParentDocId(),*score);
	}

 skip:

	// . add to wait tree and let it populate doledb on its batch run
	// . use a spiderTime of 0 which means unknown and that it needs to
	//   scan spiderdb to get that
	// . returns false and sets g_errno on error
	return addToWaitingTree ( 0LL, srep->m_firstIp , true );
}

// . Rdb.cpp calls SpiderColl::addSpiderRequest/Reply() for every positive
//   spiderdb record it adds to spiderdb. that way our cache is kept 
//   uptodate incrementally
// . returns false and sets g_errno on error
// . if the spiderTime appears to be AFTER m_nextReloadTime then we should
//   not add this spider request to keep the cache trimmed!!! (MDW: TODO)
// . BUT! if we have 150,000 urls that is going to take a long time to
//   spider, so it should have a high reload rate!
bool SpiderColl::addSpiderRequest ( SpiderRequest *sreq , 
				    long long nowGlobalMS ) {
	// don't add negative keys or data less thangs
	if ( sreq->m_dataSize <= 0 ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: add spider request is dataless for "
			    "uh48=%llu",sreq->getUrlHash48());
		char *xx=NULL;*xx=0;
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
	long ulen = sreq->getUrlLen();
	// watch out for corruption
	if ( sreq->m_firstIp ==  0 || sreq->m_firstIp == -1 || ulen <= 0 ) {
		log("spider: Corrupt spider req with url length of "
		    "%li <= 0. dataSize=%li uh48=%llu. Skipping.",
		    ulen,sreq->m_dataSize,sreq->getUrlHash48());
		return true;
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
	char *val = (char *)m_doleIpTable.getValue ( &sreq->m_firstIp );
	if ( val && *val > 0 ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: request already in dole table");
		return true;
	}

	// . skip if already in wait tree
	// . no, no. what if the current url for this firstip is not due to
	//   be spidered until 24 hrs and we are adding a url from this firstip
	//   that should be spidered now...
	//if ( m_waitingTable.isInTable ( &sreq->m_firstIp ) ) {
	//	if ( g_conf.m_logDebugSpider )
	//		log("spider: request already in waiting table");
	//	return true;
	//}

	// get ufn/priority,because if filtered we do not want to add to doledb
	long ufn ;
	ufn = ::getUrlFilterNum(sreq,NULL,nowGlobalMS,false,MAX_NICENESS,m_cr);
	// sanity check
	if ( ufn < 0 ) { char *xx=NULL;*xx=0; }
	// set the priority (might be the same as old)
	long priority = m_cr->m_spiderPriorities[ufn];

	// sanity checks
	if ( priority == -1 ) { char *xx=NULL;*xx=0; }
	if ( priority >= MAX_SPIDER_PRIORITIES) {char *xx=NULL;*xx=0;}

	// do not add to doledb if bad
	if ( priority == SPIDER_PRIORITY_FILTERED ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: request is filtered ufn=%li",ufn);
		return true;
	}

	if ( priority == SPIDER_PRIORITY_BANNED   ) {
		if ( g_conf.m_logDebugSpider )
			log("spider: request is banned ufn=%li",ufn);
		return true;
	}

	// set it for adding to doledb and computing spidertime
	sreq->m_ufn      = ufn;
	sreq->m_priority = priority;

	// get spider time -- i.e. earliest time when we can spider it
	uint64_t spiderTimeMS = getSpiderTimeMS (sreq,ufn,NULL,nowGlobalMS );
	// sanity
	if ( (long long)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }

	// once in waiting tree, we will scan waiting tree and then lookup
	// each firstIp in waiting tree in spiderdb to get the best
	// SpiderRequest for that firstIp, then we can add it to doledb
	// as long as it can be spidered now
	bool status = addToWaitingTree ( spiderTimeMS , sreq->m_firstIp, true);

	// if already doled and we beat the priority/spidertime of what
	// was doled then we should probably delete the old doledb key
	// and add the new one. hmm, the waitingtree scan code ...



	// sanity check
	//long long ttt=getEarliestSpiderTimeFromWaitingTree(sreq->m_firstIp);
	//logf (LOG_DEBUG,"spider: earliestime=%lli for firstip=%s",
	//      ttt,iptoa(sreq->m_firstIp));
	      
	//if ( ttt != (long long)spiderTimeMS ) { char *xx=NULL;*xx=0; }

	// update the latest siteNumInlinks count for this "site"
	if ( sreq->m_siteNumInlinksValid ) {
		// updates m_siteNumInlinksTable
		updateSiteNumInlinksTable ( sreq->m_siteHash32 , 
					    sreq->m_siteNumInlinks ,
					    sreq->m_addedTime );
		// clear error for this if there was any
		g_errno = 0;
	}


	if ( ! g_conf.m_logDebugSpider ) return status;

	// log it
	logf(LOG_DEBUG,
	     "spider: added %s request to wait tree "
	     "uh48=%llu "
	     "firstIp=%s "
	     "parentFirstIp=%lu "
	     "parentdocid=%llu "
	     "isinjecting=%li "
	     "ispagereindex=%li "
	     "ufn=%li "
	     "priority=%li "
	     "addedtime=%lu "
	     "spidertime=%llu",
	     sreq->m_url,
	     sreq->getUrlHash48(),
	     iptoa(sreq->m_firstIp),
	     sreq->m_parentFirstIp,
	     sreq->getParentDocId(),
	     (long)(bool)sreq->m_isInjecting,
	     (long)(bool)sreq->m_isPageReindex,
	     (long)sreq->m_ufn,
	     (long)sreq->m_priority,
	     sreq->m_addedTime,
	     spiderTimeMS);

	return status;
}

// . if one of these add fails consider increasing mem used by tree/table
// . if we lose an ip that sux because it won't be gotten again unless
//   we somehow add another request/reply to spiderdb in the future
bool SpiderColl::addToWaitingTree ( uint64_t spiderTimeMS , long firstIp ,
				    bool callForScan ) {
	// skip if already in wait tree. no - might be an override with
	// a sooner spiderTimeMS
	//if ( m_waitingTable.isInTable ( &firstIp ) ) return true;

	// waiting tree might be saving!!!
	if ( ! m_waitingTree.m_isWritable ) {
		log("spider: addtowaitingtree: failed. is not writable. "
		    "saving?");
		return false;
	}

	// sanity check
	// i think this trigged on gk209 during an auto-save!!! FIX!
	if ( ! m_waitingTree.m_isWritable ) { char *xx=NULL; *xx=0; }

	// see if in tree already, so we can delete it and replace it below
	long ws = m_waitingTable.getSlot ( &firstIp ) ;
	// . this is >= 0 if already in tree
	// . if spiderTimeMS is a sooner time than what this firstIp already
	//   has as its earliest time, then we will override it and have to
	//   update both m_waitingTree and m_waitingTable, however
	//   IF the spiderTimeMS is a later time, then we bail without doing
	//   anything at this point.
	if ( ws >= 0 ) {
		// get timems from waiting table
		long long sms = m_waitingTable.getScore64FromSlot(ws);
		// get current time
		//long long nowMS = gettimeofdayInMillisecondsGlobal();
		// make the key then
		key_t wk = makeWaitingTreeKey ( sms, firstIp );
		// must be there
		long tn = m_waitingTree.getNode ( (collnum_t)0, (char *)&wk );
		// sanity check. ensure waitingTable and waitingTree in sync
		if ( tn < 0 ) { char *xx=NULL;*xx=0; }
		// not only must we be a sooner time, but we must be 5-seconds
		// sooner than the time currently in there to avoid thrashing
		// when we had a ton of outlinks with this first ip within an
		// 5-second interval
		if ( (long long)spiderTimeMS > sms - 5000 ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: skip updating waiting tree");
			return true;
		}
		// log the replacement
		if ( g_conf.m_logDebugSpcache )
			log("spider: replacing waitingtree key "
			    "oldtime=%lu newtime=%lu firstip=%s",
			    (unsigned long)(m_bestSpiderTimeMS/1000LL),
			    (unsigned long)(spiderTimeMS/1000LL),
			    iptoa(firstIp));
		// remove from tree so we can add it below
		m_waitingTree.deleteNode ( tn , false );
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
			    "newtime=%lu%s firstip=%s",
			    (unsigned long)(spiderTimeMS/1000LL),s,
			    iptoa(firstIp));
	}

	// make the key
	key_t wk = makeWaitingTreeKey ( spiderTimeMS, firstIp );
	// what is this?
	if ( firstIp == 0 || firstIp == -1 ) {
		log("spider: got ip of %s. wtf?",iptoa(firstIp) );
		char *xx=NULL; *xx=0;
	}
	// add that
	long wn;
	if ( ( wn = m_waitingTree.addKey ( &wk ) ) < 0 ) {
		log("spider: waitingtree add failed ip=%s. increase max nodes "
		    "lest we lose this IP forever. err=%s",
		    iptoa(firstIp),mstrerror(g_errno));
		//char *xx=NULL; *xx=0;
		return false;
	}
	// add to table now since its in the tree
	if ( ! m_waitingTable.addKey ( &firstIp , &spiderTimeMS ) ) {
		// remove from tree then
		m_waitingTree.deleteNode ( wn , false );
		log("spider: wait table add failed ip=%s",iptoa(firstIp));
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
	if ( callForScan ) populateDoledbFromWaitingTree ( false );
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
long SpiderColl::getNextIpFromWaitingTree ( ) {

	// if nothing to scan, bail
	if ( m_waitingTree.isEmpty() ) return 0;
	// reset first key to get first rec in waiting tree
	m_waitingTreeKey.setMin();
	// current time on host #0
	uint64_t nowMS = gettimeofdayInMillisecondsGlobal();
 top:
	// advance to next
	//m_waitingTreeKey += 1LL;
	// assume none
	long firstIp = 0;
	// set node from wait tree key. this way we can resume from a prev key
	long node = m_waitingTree.getNextNode ( 0, (char *)&m_waitingTreeKey );
	// if empty, stop
	if ( node < 0 ) return 0;
	// breathe
	QUICKPOLL(MAX_NICENESS);
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
		// these operations should fail if writes have been disabled
		// and becase the trees/tables for spidercache are saving
		// in Process.cpp's g_spiderCache::save() call
		m_waitingTree.deleteNode ( node , true );
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
	if ( (long long)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }
	// save key for deleting when done
	m_waitingTreeKey.n1 = k->n1;
	m_waitingTreeKey.n0 = k->n0;
	// sanity
	if ( firstIp == 0 || firstIp == -1 ) { char *xx=NULL;*xx=0; }
	// we set this to true when done
	m_isReadDone = false;
	// compute the best request from spiderdb list, not valid yet
	m_bestRequestValid = false;
	m_lastReplyValid   = false;

	// start reading spiderdb here
	m_nextKey = g_spiderdb.makeFirstKey(firstIp);
	m_endKey  = g_spiderdb.makeLastKey (firstIp);
	// all done
	return firstIp;
}

static void gotSpiderdbListWrapper2( void *state , RdbList *list , Msg5 *msg5);

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
	// skip if spiders off
	if ( ! m_cr->m_spideringEnabled ) return;
	// if entering for the first time, we need to read list from spiderdb
	if ( ! reentry ) {
		// just return if we should not be doing this yet
		if ( ! m_waitingTreeNeedsRebuild ) return;
		// a double call? can happen if list read is slow...
		if ( m_gettingList2 ) return;
		// . read in a replacement SpiderRequest to add to doledb from
		//   this ip
		// . get the list of spiderdb records
		// . do not include cache, those results are old and will mess
		//   us up
		log(LOG_DEBUG,"spider: populateWaitingTree: "
		    "calling msg5: startKey=0x%llx,0x%llx "
		    "firstip=%s",
		    m_nextKey2.n1,m_nextKey2.n0,
		    iptoa(g_spiderdb.getFirstIp(&m_nextKey2)));
		// flag it
		m_gettingList2 = true;
		// read the list from local disk
		if ( ! m_msg5b.getList ( RDB_SPIDERDB   ,
					 m_cr->m_coll   ,
					 &m_list2       ,
					 &m_nextKey2    ,
					 &m_endKey2     ,
					 SR_READ_SIZE   , // minRecSizes (512k)
					 true           , // includeTree
					 false          , // addToCache
					 0              , // max cache age
					 0              , // startFileNum
					 -1             , // numFiles (all)
					 this           , // state 
					 gotSpiderdbListWrapper2 ,
					 MAX_NICENESS   , // niceness
					 true          )) // do error correct?
			// return if blocked
			return;
	}

	// show list stats
	if ( g_conf.m_logDebugSpider )
		log("spider: populateWaitingTree: got list of size %li",
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

	// shortcut
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

	long lastOne = 0;
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
		long firstIp = sreq->m_firstIp;
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
		// otherwise, we want to add it with 0 time so the doledb
		// scan will evaluate it properly
		// this will return false if we are saving the tree i guess
		if ( ! addToWaitingTree ( 0 , firstIp , false ) ) return;
		// count it
		m_numAdded++;
		// ignore errors for this
		g_errno = 0;
	}

	// are we the final list in the scan?
	bool shortRead = ( list->getListSize() < (long)SR_READ_SIZE ) ;

	m_numBytesScanned += list->getListSize();

	// reset? still left over from our first scan?
	if ( m_lastPrintCount > m_numBytesScanned )
		m_lastPrintCount = 0;

	// announce every 100MB maybe
	if ( m_numBytesScanned - m_lastPrintCount > 100000000 ) {
		log("spider: %llu spiderdb bytes scanned for waiting tree "
		    "re-population",m_numBytesScanned);
		m_lastPrintCount = m_numBytesScanned;
	}

	// debug info
	log(LOG_DEBUG,"spider: Read2 %li spiderdb bytes.",list->getListSize());
	// reset any errno cuz we're just a cache
	g_errno = 0;

	// if not done, keep going
	if ( ! shortRead ) {
		// . inc it here
		// . it can also be reset on a collection rec update
		key128_t endKey  = *(key128_t *)list->getLastKey();
		m_nextKey2       = endKey;
		m_nextKey2      += (unsigned long) 1;
		// watch out for wrap around
		if ( m_nextKey2 < endKey ) shortRead = true;
	}

	if ( shortRead ) {
		// mark when the scan completed so we can do another one
		// like 24 hrs from that...
		m_lastScanTime = getTimeLocal();
		// log it
		if ( m_numAdded )
			log("spider: added %li recs to waiting tree from "
			    "scan of %lli bytes",m_numAdded,m_numBytesScanned);
		// reset the count for next scan
		m_numAdded = 0 ;
		m_numBytesScanned = 0;
		// reset for next scan
		m_nextKey2.setMin();
		// no longer need rebuild
		m_waitingTreeNeedsRebuild = false;
	}

	// free list to save memory
	list->freeList();
	// wait for sleepwrapper to call us again with our updated m_nextKey2
	return;
}

static bool    s_ufnTreeSet = false;
static RdbTree s_ufnTree;
static time_t  s_lastUfnTreeFlushTime = 0;

// . for each IP in the waiting tree, scan all its SpiderRequests and determine
//   which one should be the next to be spidered. and put that one in doledb.
// . we call this a lot, like if the admin changes the url filters table
//   we have to re-scan all of spiderdb basically and re-do doledb
// . "rentry" if true means we are re-entering from a callback because the
//   call to scanSpiderdb() blocked
void SpiderColl::populateDoledbFromWaitingTree ( bool reentry ) {
	// only one loop can run at a time!
	if ( ! reentry && m_isPopulating ) return;
	// skip if in repair mode
	if ( g_repairMode ) return;
	// try skipping!!!!!!!!!!!
	// yeah, this makes us scream. in addition to calling
	// Doledb::m_rdb::addRecord() below
	// WE NEED THIS TO REPOPULATE DOLEDB THOUGH!!!
	//return;
	// set this flag so we are not re-entered
	m_isPopulating = true;
 loop:

	// if waiting tree is being saved, we can't write to it
	// so in that case, bail and wait to be called another time
	RdbTree *wt = &m_waitingTree;
	if( wt->m_isSaving || ! wt->m_isWritable ) {
		m_isPopulating = false;
		return;
	}

	// get next IP that is due to be spidered from
	long ip = getNextIpFromWaitingTree();
	// . return if none. all done. unset populating flag.
	// . it returns 0 if the next firstip has a spidertime in the future
	if ( ip == 0 ) { m_isPopulating = false; return; }

	// set read range for scanning spiderdb
	m_nextKey = g_spiderdb.makeFirstKey(ip);
	m_endKey  = g_spiderdb.makeLastKey (ip);

	// debug output
	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: scanSpiderdb: waitingtree nextip=%s "
		    "numUsedNodes=%li",iptoa(ip),m_waitingTree.m_numUsedNodes);

	// assume using tree
	m_useTree = true;

	// . flush the tree every 12 hours
	// . i guess we could add incoming requests to the ufntree if
	//   they strictly beat the ufn tree tail node, HOWEVER, we 
	//   still have the problem of that if a url we spidered is due
	//   to be respidered very soon we will miss it, as only the reply
	//   is added back into spiderdb, not a new request.
	long nowLocal = getTimeLocal();
	// make it one hour so we don't cock-block a new high priority 
	// request that just got added... crap, what if its an addurl
	// or something like that????
	if ( nowLocal - s_lastUfnTreeFlushTime > 3600 ) {
		s_ufnTree.clear();
		s_lastUfnTreeFlushTime = nowLocal;
	}

	long long uh48;
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
	long node = s_ufnTree.getNextNode(0,(char *)&key);
	// cancel node if not from our ip
	if ( node >= 0 ) {
		key128_t *rk = (key128_t *)s_ufnTree.getKey ( node );
		if ( (rk->n1 >> 32) != (unsigned long)ip ) node = -1;
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
	// turn this off until we figure out why it sux
	m_useTree = false;

	// so we know if we are the first read or not...
	m_firstKey = m_nextKey;

	// look up in spiderdb otherwise and add best req to doledb from ip
	if ( ! scanSpiderdb ( true ) ) return;
	// oom error? i've seen this happen and we end up locking up!
	if ( g_errno ) return;
	// try more
	goto loop;
}

static void gotSpiderdbListWrapper ( void *state , RdbList *list , Msg5 *msg5){
	SpiderColl *THIS = (SpiderColl *)state;
	// . finish processing the list we read now
	// . if that blocks, it will call doledWrapper
	if ( ! THIS->scanSpiderdb ( false ) ) return;
	// . otherwise, do more from tree
	// . re-entry is true because we just got the msg5 reply
	THIS->populateDoledbFromWaitingTree ( true );
}

static void gotSpiderdbListWrapper2( void *state , RdbList *list , Msg5 *msg5){
	SpiderColl *THIS = (SpiderColl *)state;
	// re-entry is true because we just got the msg5 reply
	THIS->populateWaitingTreeFromSpiderdb ( true );
}


// replace this func with the one above...
static void doledWrapper ( void *state ) {
	SpiderColl *THIS = (SpiderColl *)state;
	// msg4 is available again
	THIS->m_msg4Avail = true;
	// . we added a rec to doledb for the firstIp in m_waitingTreeKey, so
	//   now go to the next node in the wait tree.
	// . it will get the next key after m_waitingTreeKey
	// . re-entry is true because we just got the msg4 reply
	THIS->populateDoledbFromWaitingTree ( true );
}

key128_t makeUfnTreeKey ( long      firstIp      ,
			  long      priority     ,
			  long long spiderTimeMS ,
			  long long uh48         ) {
	// sanity check, do not allow negative priorities for now
	if ( priority < 0 ) { char *xx=NULL;*xx=0; }
	if ( priority > 255 ) { char *xx=NULL;*xx=0; }
	key128_t key;
	key.n1 = (unsigned long)firstIp;
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

void parseUfnTreeKey ( key128_t  *k ,
		       long      *firstIp ,
		       long      *priority ,
		       uint64_t  *spiderTimeMS ,
		       long long *uh48 ) {
	*firstIp = (k->n1) >> 32;
	*priority = (long)(char)((k->n1 >> 16)&0xff);
	*priority = 255 - *priority; // uncomplement
	*spiderTimeMS = k->n1 & 0xffffff;
	*spiderTimeMS <<= 16;
	*spiderTimeMS |= k->n0 >> (32+24);
}

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool SpiderColl::scanSpiderdb ( bool needList ) {

 readLoop:

	// if we re-entered from the read wrapper, jump down
	if ( needList ) {
		// sanity check
		if ( m_gettingList ) { char *xx=NULL;*xx=0; }
		// . read in a replacement SpiderRequest to add to doledb from
		//   this ip
		// . get the list of spiderdb records
		// . do not include cache, those results are old and will mess
		//   us up
		log(LOG_DEBUG,"spider: scanSpiderdb: "
		    "calling msg5: startKey=0x%llx,0x%llx "
		    "firstip=%s",
		    m_nextKey.n1,m_nextKey.n0,
		    iptoa(g_spiderdb.getFirstIp(&m_nextKey)));
		// flag it
		m_gettingList = true;
		// read the list from local disk
		if ( ! m_msg5.getList ( RDB_SPIDERDB   ,
					m_cr->m_coll   ,
					&m_list        ,
					&m_nextKey      ,
					&m_endKey       ,
					SR_READ_SIZE   , // minRecSizes (512k)
					true           , // includeTree
					false          , // addToCache
					0              , // max cache age
					0              , // startFileNum
					-1             , // numFiles (all)
					this           , // state 
					gotSpiderdbListWrapper ,
					MAX_NICENESS   , // niceness
					true          )) // do error correct?
			// return false if blocked
			return false ;
	}

	// show list stats
	if ( g_conf.m_logDebugSpider )
		log("spider: scanSpiderdb: got list of size %li",
		    m_list.m_listSize);

	// unflag it
	m_gettingList = false;
	// stop if we are done
	if ( m_isReadDone ) return true;

	// if waitingtree is locked for writing because it is saving or
	// writes were disabled then just bail and let the scan be re-called
	// later
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable )
		return true;

	// shortcut
	RdbList *list = &m_list;
	// ensure we point to the top of the list
	list->resetListPtr();
	// bail on error
	if ( g_errno ) {
		log("spider: Had error getting list of urls "
		    "from spiderdb: %s.",mstrerror(g_errno));
		m_isReadDone = true;
		return true;
	}
	// get this
	uint64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();//Local();
	uint32_t nowGlobal   = nowGlobalMS / 1000;

	SpiderRequest *winReq      = NULL;
	long           winPriority = -10;
	uint64_t       winTimeMS   = 0xffffffffffffffffLL;
	SpiderReply   *srep        = NULL;
	long long      srepUh48;

	// for getting the top MAX_NODES nodes
	long           tailPriority = -10;
	uint64_t       tailTimeMS   = 0xffffffffffffffffLL;

	// if we are continuing from another list...
	if ( m_lastReplyValid ) {
		srep     = (SpiderReply *)m_lastReplyBuf;
		srepUh48 = srep->getUrlHash48();
	}

	// sanity, if it was in ufntree it should be on disk then...
	if ( list->isEmpty() && m_nextKey == m_firstKey && ! m_useTree )
		log("spider: strange corruption #1");
	//char *xx=NULL;*xx=0; }

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


	long firstIp = m_waitingTreeKey.n0 & 0xffffffff;

	long numNodes = 0;
	long tailNode = -1;

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
			logf(LOG_DEBUG,"spider: got negative spider rec");
			continue;
		}
		// if its a SpiderReply set it for an upcoming requests
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec ) ) {
			// see if this is the most recent one
			SpiderReply *tmp = (SpiderReply *)rec;
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
		// cast it
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// . skip if our twin should add it to doledb
		// . waiting tree only has firstIps assigned to us so
		//   this should not be necessary
		//if ( ! isAssignedToUs ( sreq->m_firstIp ) ) continue;
		// null out srep if no match
		if ( srep && srepUh48 != sreq->getUrlHash48() ) srep = NULL;
		// if we are doing parser test, ignore all but initially
		// injected requests. NEVER DOLE OUT non-injected urls
		// when doing parser test
		if ( g_conf.m_testParserEnabled ) {
			// skip if already did it
			if ( srep ) continue;
			// skip if not injected
			if ( ! sreq->m_isInjecting ) continue;
		}

		// . ignore docid-based requests if spidered the url afterwards
		// . these are one-hit wonders
		// . once done they can be deleted
		if ( sreq->m_urlIsDocId &&
		     srep && 
		     srep->m_spideredTime > sreq->m_addedTime )
			continue;

		// sanity check. check for http(s)://
		if ( sreq->m_url[0] != 'h' &&
		     // might be a docid from a pagereindex.cpp
		     ! is_digit(sreq->m_url[0]) ) { 
			log("spider: got corrupt 1 spiderRequest in scan");
			continue;
		}

		// update SpiderRequest::m_siteNumInlinks to most recent value
		long sni = sreq->m_siteNumInlinks;
		// get the # of inlinks to the site from our table
		uint64_t *val;
		val = (uint64_t *)m_sniTable.getValue32(sreq->m_siteHash32);
		// use the most recent sni from this table
		if ( val ) 
			sni = (long)((*val)>>32);
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
		// this is -1 on corruption
		if ( srep && srep->m_httpStatus >= 1000 ) {
			log("spider: got corrupt 3 spiderReply in scan");
			srep = NULL;
		}
		// bad langid?
		if ( srep && ! getLanguageAbbr (srep->m_langId) ) {
			log("spider: got corrupt 4 spiderReply in scan");
			srep = NULL;
		}
		// . get the url filter we match
		// . if this is slow see the TODO below in dedupSpiderdbList()
		//   which can pre-store these values assuming url filters do
		//   not change and siteNumInlinks is about the same.
		long ufn = ::getUrlFilterNum(sreq,srep,nowGlobal,false,
					     MAX_NICENESS,m_cr);
		// sanity check
		if ( ufn == -1 ) { char *xx=NULL;*xx=0; }
		// set the priority (might be the same as old)
		long priority = m_cr->m_spiderPriorities[ufn];
		// sanity checks
		if ( priority == -1 ) { char *xx=NULL;*xx=0; }
		if ( priority >= MAX_SPIDER_PRIORITIES) {char *xx=NULL;*xx=0;}

		uint64_t spiderTimeMS;
		spiderTimeMS = getSpiderTimeMS ( sreq,ufn,srep,nowGlobalMS );
		// sanity
		if ( (long long)spiderTimeMS < 0 ) { 
			log("spider: got corrupt 2 spiderRequest in scan");
			continue;
		}
		// skip if banned
		if ( priority == SPIDER_PRIORITY_FILTERED ) continue;
		if ( priority == SPIDER_PRIORITY_BANNED   ) continue;

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

		// only compare to min winner in tree if we got 100 in
		// tree from this firstip already
		if ( numNodes >= MAX_NODES && useTree ) {
			uint64_t tm1 = spiderTimeMS;
			uint64_t tm2 = tailTimeMS;
			// if they are both overdue, make them the same
			if ( tm1 < nowGlobalMS ) tm1 = 1;
			if ( tm2 < nowGlobalMS ) tm2 = 1;
			// skip spider request if its time is past winner's
			if ( tm1 > tm2 )
				continue;
			// if tied, use priority
			if ( tm1 == tm2 && priority < tailPriority )
				continue;
			// if tied, use actual times. assuming both 
			// are < nowGlobalMS
			if ( tm1 == tm2 && priority == tailPriority &&
			     spiderTimeMS > tailTimeMS )
				continue;
			// cut tail
			s_ufnTree.deleteNode ( tailNode , true );
		}

		// somestimes the firstip in its key does not match the
		// firstip in the record!
		if ( sreq->m_firstIp != firstIp ) {
			log("spider: request %s firstip does not match "
			    "firstip in key",sreq->m_url);
			log("spider: ip1=%s",iptoa(sreq->m_firstIp));
			log("spider: ip2=%s",iptoa(firstIp));
			continue;
		}

		// make the key
		if ( useTree ) {
			long long uh48 = sreq->getUrlHash48();
			key128_t k = makeUfnTreeKey ( firstIp ,priority,
						      spiderTimeMS , uh48 );
			//long nn =;
			s_ufnTree.addNode(0,(char *)&k,NULL,8);
			//log("adding node #%li firstip=%s uh48=%llu "
			//    "ufntree.k.n1=0x%llx "
			//    "spiderdb.k.n1=0x%llx "
			//    "spiderdb.k.n0=0x%llx "
			//    ,
			//    nn,iptoa(firstIp),uh48,k.n1,
			//    *(long long *)rec,
			//    *(long long *)(rec+8)
			//   );
			numNodes++;
		}

		// compute new tail node
		if ( numNodes >= MAX_NODES && useTree ) {
			key128_t nk = makeUfnTreeKey (firstIp+1,255,0,0 );
			tailNode = s_ufnTree.getPrevNode ( 0,(char *)&nk );
			if ( tailNode < 0 ) { char *xx=NULL;*xx=0; }
			// set new tail parms
			key128_t *tailKey;
			tailKey = (key128_t *)s_ufnTree.getKey ( tailNode );
			// convert to char first then to signed long
			long      tailIp;
			long long tailUh48;
			parseUfnTreeKey ( tailKey ,
					  &tailIp ,
					  &tailPriority,
					  &tailTimeMS ,
					  &tailUh48 );
			// sanity
			if ( tailIp != firstIp ) { char *xx=NULL;*xx=0;}
		}

		// skip if not the best
		uint64_t tm1 = spiderTimeMS;
		uint64_t tm2 = winTimeMS;
		// if they are both overdue, make them the same
		if ( tm1 < nowGlobalMS ) tm1 = 1;
		if ( tm2 < nowGlobalMS ) tm2 = 1;
		// skip spider request if its time is past winner's
		if ( tm1 > tm2 )
			continue;
		// if tied, use priority
		if ( tm1 == tm2 && priority < winPriority )
			continue;
		// if tied, use actual times. assuming both 
		// are < nowGlobalMS
		if ( tm1 == tm2 && priority == winPriority &&
		     spiderTimeMS > winTimeMS )
			continue;
		     
		// ok, we got a new winner
		winPriority = priority;
		winTimeMS   = spiderTimeMS;
		winReq      = sreq;
		// set these for doledb
		winReq->m_priority   = priority;
		winReq->m_ufn        = ufn;
		//winReq->m_spiderTime = spiderTime;
	}

	// if this is a successive call we have to beat the global because
	// the firstIp has a *ton* of spider requests and we can't read them
	// all in one list, then see if we beat our global winner!
	if ( winReq && m_bestRequestValid ) {
		if ( m_bestRequest->m_priority > winPriority )
			winReq = NULL;
		else if ( m_bestRequest->m_priority == winPriority &&
			  (long)m_bestSpiderTimeMS < winPriority )
			winReq = NULL;
	}

	// if nothing, we are done!
	if ( winReq ) {
		// store this
		long rsize = winReq->getRecSize();
		// sanity check
		if ( rsize > (long)MAX_BEST_REQUEST_SIZE){char *xx=NULL;*xx=0;}
		// now store this SpiderRequest for adding to doledb
		memcpy ( m_bestRequestBuf , winReq, rsize );
		// point to that
		m_bestRequest = (SpiderRequest *)m_bestRequestBuf;
		// set this
		m_bestRequestValid = true;
		// this too
		m_bestSpiderTimeMS = winTimeMS;
		// sanity
		if ( (long long)winTimeMS < 0 ) { char *xx=NULL;*xx=0; }
	}

	// are we the final list in the scan?
	m_isReadDone = ( list->getListSize() < (long)SR_READ_SIZE ) ;

	// if no spiderreply for the current url, invalidate this
	m_lastReplyValid = false;
	// if read is not yet done, save the reply in case next list needs it
	if ( srep && ! m_isReadDone ) {
		long rsize = srep->getRecSize();
		if ( rsize > (long)MAX_SP_REPLY_SIZE ) { char *xx=NULL;*xx=0; }
		memcpy ( m_lastReplyBuf, srep, rsize );
		m_lastReplyValid = true;
	}

	// debug info
	log(LOG_DEBUG,"spider: Read %li spiderdb bytes.",list->getListSize());
	// reset any errno cuz we're just a cache
	g_errno = 0;

	//
	// end list processing
	//

	// if not done, keep going
	if ( ! m_isReadDone ) { 
		// . inc it here
		// . it can also be reset on a collection rec update
		key128_t endKey  = *(key128_t *)list->getLastKey();
		m_nextKey        = endKey;
		m_nextKey       += (unsigned long) 1;
		// watch out for wrap around
		if ( m_nextKey < endKey ) {
			m_nextKey = endKey;
			m_isReadDone = true;
		}
	}

	// free list to save memory
	list->freeList();

	if ( ! m_isReadDone ) {
		// read more now!
		needList = true; 
		goto readLoop; 
	}

	// print out here
	//log("spider: got best req=%s ip=%s uh48=%llu",m_bestRequest->m_url,
	//    iptoa(m_bestRequest->m_firstIp),m_bestRequest->getUrlHash48());

	// gotta check this again since we might have done a QUICKPOLL() above
	// to call g_process.shutdown() so now tree might be unwritable
	if ( wt->m_isSaving || ! wt->m_isWritable )
		return true;

	// ok, all done if nothing to add to doledb. i guess we were misled
	// that firstIp had something ready for us. maybe the url filters
	// table changed to filter/ban them all.
	if ( ! g_errno && ! m_bestRequestValid ) {
		// note it
		if ( g_conf.m_logDebugSpcache )
			log("spider: nuking misleading waitingtree key "
			    "firstIp=%s", iptoa(firstIp));
		m_waitingTree.deleteNode ( 0,(char *)&m_waitingTreeKey,true);
		m_waitingTable.removeKey  ( &firstIp  );
		// sanity check
		if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}
		return true;
	}

	if ( g_errno ) {
		log("spider: scanSpiderdb: %s",mstrerror(g_errno));
		return true;
	}

	if ( m_bestRequest->m_firstIp != firstIp ) { char *xx=NULL;*xx=0; }

	//uint64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();

	// sanity checks
	if ( (long long)m_bestSpiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }
	if ( m_bestRequest->m_ufn          < 0 ) { char *xx=NULL;*xx=0; }
	if ( m_bestRequest->m_priority ==   -1 ) { char *xx=NULL;*xx=0; }

	// if best request has a future spiderTime, at least update
	// the wait tree with that since we will not be doling this request
	// right now.
	if ( m_bestSpiderTimeMS > nowGlobalMS ) {

		// if in the process of being added to doledb or in doledb...
		if ( m_doleIpTable.isInTable ( &firstIp ) ) {
			// sanity i guess. remove this line if it hits this!
			log("spider: wtf????");
			//char *xx=NULL;*xx=0;
			return true;
		}

		// get old time
		unsigned long long oldSpiderTimeMS = m_waitingTreeKey.n1;
		oldSpiderTimeMS <<= 32;
		oldSpiderTimeMS |= (m_waitingTreeKey.n0 >> 32);
		// delete old node
		m_waitingTree.deleteNode ( 0,(char *)&m_waitingTreeKey ,true);
		long  fip = m_bestRequest->m_firstIp;
		key_t wk2 = makeWaitingTreeKey ( m_bestSpiderTimeMS , fip );
		// log the replacement
		if ( g_conf.m_logDebugSpcache )
			log("spider: scan replacing waitingtree key "
			    "oldtime=%lu newtime=%lu firstip=%s bestpri=%li "
			    "besturl=%s",
			    (unsigned long)(oldSpiderTimeMS/1000LL),
			    (unsigned long)(m_bestSpiderTimeMS/1000LL),
			    iptoa(fip),
			    (long)m_bestRequest->m_priority,
			    m_bestRequest->m_url);
		// this should never fail since we deleted one above
		m_waitingTree.addKey ( &wk2 );
		// keep the table in sync now with the time
		m_waitingTable.addKey( &fip, &m_bestSpiderTimeMS );
		// sanity check
		if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}
		return true;
	}

	// make the doledb key first for this so we can add it
	key_t doleKey = g_doledb.makeKey ( m_bestRequest->m_priority     ,
					   // convert to seconds from ms
					   m_bestSpiderTimeMS / 1000     ,
					   m_bestRequest->getUrlHash48() ,
					   false                         );
	
	// make it into a doledb record
	char *p = m_doleBuf;
	*(key_t *)p = doleKey;
	p += sizeof(key_t);
	long recSize = m_bestRequest->getRecSize();
	*(long *)p = recSize;
	p += 4;
	memcpy ( p , m_bestRequest , recSize );
	p += recSize;
	// sanity check
	if ( p - m_doleBuf > (long)MAX_DOLEREC_SIZE ) { char *xx=NULL;*xx=0; }
	// how did this happen?
	if ( ! m_msg4Avail ) { char *xx=NULL;*xx=0; }

	// add it to doledb ip table now so that waiting tree does not
	// immediately get another spider request from this same ip added
	// to it while the msg4 is out. but if add failes we totally bail
	// with g_errno set
	if ( ! addToDoleTable ( m_bestRequest ) ) return true;

	//
	// delete the winner from ufntree as well
	//
	long long uh48 = m_bestRequest->getUrlHash48();
	key128_t bkey = makeUfnTreeKey ( m_bestRequest->m_firstIp ,
					 m_bestRequest->m_priority ,
					 m_bestSpiderTimeMS ,
					 uh48 );
	// must be in tree!
	long node = s_ufnTree.getNextNode ( 0, (char *)&bkey );
	// if this firstip had too few requests to make it into the
	// tree then node will be < 0!
	//if ( node < 0 ) { char *xx=NULL;*xx=0; }
	if ( node >= 0 ) {
		//log("deleting node #%li firstip=%s uh48=%llu",
		//    node,iptoa(firstIp),uh48);
		s_ufnTree.deleteNode ( node , true );
	}
	

	// use msg4 to transmit our guys into the rdb, RDB_DOLEDB
	bool status = m_msg4.addMetaList ( m_doleBuf     ,
					   p - m_doleBuf ,
					   m_collnum     ,
					   this          ,
					   doledWrapper  ,
					   MAX_NICENESS  ,
					   RDB_DOLEDB    ) ;
	// if it blocked set this to true so we do not reuse it
	if ( ! status ) m_msg4Avail = false;

	long storedFirstIp = (m_waitingTreeKey.n0) & 0xffffffff;

	// log it
	if ( g_conf.m_logDebugSpcache ) {
		unsigned long long spiderTimeMS = m_waitingTreeKey.n1;
		spiderTimeMS <<= 32;
		spiderTimeMS |= (m_waitingTreeKey.n0 >> 32);
		logf(LOG_DEBUG,"spider: removing doled waitingtree key"
		     " spidertime=%llu firstIp=%s "
		     "pri=%li "
		     "url=%s"
		     ,spiderTimeMS,
		     iptoa(storedFirstIp),
		     (long)m_bestRequest->m_priority,
		     m_bestRequest->m_url);
	}

	// before adding to doledb remove from waiting tree so we do not try
	// to readd to doledb...
	m_waitingTree.deleteNode ( 0, (char *)&m_waitingTreeKey , true);
	m_waitingTable.removeKey  ( &storedFirstIp );
	
	// sanity check
	if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}

	// add did not block
	return status;
}



uint64_t SpiderColl::getSpiderTimeMS ( SpiderRequest *sreq,
				       long ufn,
				       SpiderReply *srep,
				       uint64_t nowGlobalMS ) {
	// . get the scheduled spiderTime for it
	// . assume this SpiderRequest never been successfully spidered
	uint64_t spiderTimeMS = ((uint64_t)sreq->m_addedTime) * 1000LL;
	// if injecting for first time, use that!
	if ( ! srep && sreq->m_isInjecting ) return spiderTimeMS;

	// to avoid hammering an ip, get last time we spidered it...
	uint64_t lastMS ;
	lastMS = m_lastDownloadCache.getLongLong ( m_collnum       ,
						   sreq->m_firstIp ,
						   -1              , // maxAge
						   true            );// promote
	// -1 means not found
	if ( (long long)lastMS == -1 ) lastMS = 0;
	// sanity
	if ( (long long)lastMS < -1 ) { 
		log("spider: corrupt last time in download cache. nuking.");
		lastMS = 0;
	}
	// min time we can spider it
	uint64_t minSpiderTimeMS = lastMS + m_cr->m_spiderIpWaits[ufn];
	// if not found in cache
	if ( lastMS == (uint64_t)-1 ) minSpiderTimeMS = 0LL;

	// wait 5 seconds for all outlinks in order for them to have a
	// chance to get any link info that might have been added
	// from the page that supplied this outlink
	spiderTimeMS += 5000;
	//  ensure min
	if ( spiderTimeMS < minSpiderTimeMS ) spiderTimeMS = minSpiderTimeMS;
	// if no reply, use that
	if ( ! srep ) return spiderTimeMS;
	// if this is not the first try, then re-compute the spiderTime
	// based on that last time
	// sanity check
	if ( srep->m_spideredTime <= 0 ) {
		// a lot of times these are corrupt! wtf???
		spiderTimeMS = minSpiderTimeMS;
		return spiderTimeMS;
		//{ char*xx=NULL;*xx=0;}
	}
	// compute new spiderTime for this guy, in seconds
	uint64_t waitInSecs = (uint64_t)(m_cr->m_spiderFreqs[ufn]*3600*24.0);
	// do not spider more than once per 15 minutes ever!
	// no! might be a query reindex!!
	if ( waitInSecs < 900 && ! sreq->m_urlIsDocId ) { 
		static bool s_printed = false;
		if ( ! s_printed ) {
			s_printed = true;
			log("spider: min spider wait is 900, "
			    "not %llu (ufn=%li)",waitInSecs,ufn);
		}
		waitInSecs = 900;
	}
	// in fact, force docid based guys to be zero!
	if ( sreq->m_urlIsDocId ) waitInSecs = 0;
	// when it was spidered
	uint64_t lastSpideredMS = ((uint64_t)srep->m_spideredTime) * 1000;
	// . when we last attempted to spider it... (base time)
	// . use a lastAttempt of 0 to indicate never! 
	// (first time)
	spiderTimeMS = lastSpideredMS + (waitInSecs * 1000LL);
	//  ensure min
	if ( spiderTimeMS < minSpiderTimeMS ) spiderTimeMS = minSpiderTimeMS;
	// sanity
	if ( (long long)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }

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
	long *score = (long *)m_doleIpTable.getValue32 ( sreq->m_firstIp );
	// debug point
	if ( g_conf.m_logDebugSpider ) {
		long long  uh48 = sreq->getUrlHash48();
		long long pdocid = sreq->getParentDocId();
		long ss = 1;
		if ( score ) ss = *score + 1;
		log("spider: added to doletbl uh48=%llu parentdocid=%llu "
		    "ipdolecount=%li ufn=%li priority=%li firstip=%s",
		    uh48,pdocid,ss,(long)sreq->m_ufn,(long)sreq->m_priority,
		    iptoa(sreq->m_firstIp));
	}
	// we had a score there already, so inc it
	if ( score ) {
		// inc it
		*score = *score + 1;
		// sanity check
		if ( *score <= 0 ) { char *xx=NULL;*xx=0; }
	}
	else {
		// ok, add new slot
		long val = 1;
		if ( ! m_doleIpTable.addKey ( &sreq->m_firstIp , &val ) ) {
			// log it, this is bad
			log("spider: failed to add ip %s to dole ip tbl",
			    iptoa(sreq->m_firstIp));
			// return true with g_errno set on error
			return false;
		}
		// sanity check
		//if ( ! m_doleIpTable.m_isWritable ) { char *xx=NULL;*xx=0;}
	}
	return true;
}


/////////////////////////
/////////////////////////      UTILITY FUNCTIONS
/////////////////////////

// . map a spiderdb rec to the group id that should spider it
// . "sr" can be a SpiderRequest or SpiderReply
unsigned long getGroupIdToSpider ( char *sr ) {
	// use the url hash
	long long uh48 = g_spiderdb.getUrlHash48 ( (key128_t *)sr );
	// host to dole it based on ip
	long hostId = uh48 % g_hostdb.m_numHosts ;
	// get it
	Host *h = g_hostdb.getHost ( hostId ) ;
	// and return groupid
	return h->m_groupId;
}


// does this belong in our spider cache?
bool isAssignedToUs ( long firstIp ) {
	// sanity check... must be in our group.. we assume this much
	//if ( g_spiderdb.getGroupId(firstIp) != g_hostdb.m_myHost->m_groupId){
	//	char *xx=NULL;*xx=0; }
	// . host to dole it based on ip
	// . ignore lower 8 bits of ip since one guy often owns a whole block!
	//long hostId=(((unsigned long)firstIp) >> 8) % g_hostdb.getNumHosts();

	// get our group
	Host *group = g_hostdb.getMyGroup();
	// pick a host in our group

	// if not dead return it
	//if ( ! g_hostdb.isDead(hostId) ) return hostId;
	// get that host
	//Host *h = g_hostdb.getHost(hostId);
	// get the group
	//Host *group = g_hostdb.getGroup ( h->m_groupId );
	// and number of hosts in the group
	long hpg = g_hostdb.getNumHostsPerGroup();
	// select the next host number to try
	//long next = (((unsigned long)firstIp) >> 16) % hpg ;
	// hash to a host
	long i = ((uint32_t)firstIp) % hpg;
	Host *h = &group[i];
	// return that if alive
	if ( ! g_hostdb.isDead(h) ) return (h->m_hostId == g_hostdb.m_hostId);
	// . select another otherwise
	// . put all alive in an array now
	Host *alive[64];
	long upc = 0;
	for ( long j = 0 ; j < hpg ; j++ ) {
		Host *h = &group[i];
		if ( g_hostdb.isDead(h) ) continue;
		alive[upc++] = h;
	}
	// if none, that is bad! return the first one that we wanted to
	if ( upc == 0 ) return (h->m_hostId == g_hostdb.m_hostId);
	// select from the good ones now
	i  = ((uint32_t)firstIp) % hpg;
	// get that
	h = &group[i];
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
	// clear array of ptrs to Doc's
	memset ( m_docs , 0 , sizeof(XmlDoc *) * MAX_SPIDERS );
}

SpiderLoop::~SpiderLoop ( ) { reset(); }

// free all doc's
void SpiderLoop::reset() {
	// delete all doc's in use
	for ( long i = 0 ; i < MAX_SPIDERS ; i++ ) {
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
}

void SpiderLoop::startLoop ( ) {
	m_cri     = 0;
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
	// for locking. key size is 6 bytes!
	m_lockTable.set ( 6,sizeof(UrlLock),0,NULL,0,false,MAX_NICENESS,
			  "splocks");

	if ( ! m_lockCache.init ( 10000 , // maxcachemem
				  4     , // fixedatasize
				  false , // supportlists?
				  1000  , // maxcachenodes
				  false , // use half keys
				  "lockcache", // dbname
				  false  ) )
		log("spider: failed to init lock cache. performance hit." );

	// dole some out
	//g_spiderLoop.doleUrls1();
	// spider some urls that were doled to us
	//g_spiderLoop.spiderDoledUrls( );
	// sleep for .1 seconds = 100ms
	if (!g_loop.registerSleepCallback(10,this,doneSleepingWrapperSL))
		log("build: Failed to register timer callback. Spidering "
		    "is permanently disabled. Restart to fix.");
}

void doneSleepingWrapperSL ( int fd , void *state ) {
	//SpiderLoop *THIS = (SpiderLoop *)state;
	// dole some out
	//g_spiderLoop.doleUrls1();

	// if spidering disabled then do not do this crap
	if ( ! g_conf.m_spideringEnabled )  return;
	//if ( ! g_conf.m_webSpideringEnabled )  return;

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

	static long s_count = -1;
	// count these calls
	s_count++;

	// reset SpiderColl::m_didRound and m_nextDoledbKey if it is maxed
	// because we might have had a lock collision
	long nc = g_collectiondb.m_numRecs;
	for ( long i = 0 ; i < nc ; i++ ) {
		// get it
		SpiderColl *sc = g_spiderCache.m_spiderColls[i];
		// skip if none
		if ( ! sc ) continue;
		// also scan spiderdb to populate waiting tree now but
		// only one read per 100ms!!
		if ( (s_count % 10) == 0 ) {
			// always do a scan at startup & every 24 hrs
			if ( ! sc->m_waitingTreeNeedsRebuild &&
			     getTimeLocal() - sc->m_lastScanTime > 24*3600) {
				// if a scan is ongoing, this will re-set it
				sc->m_nextKey2.setMin();
				sc->m_waitingTreeNeedsRebuild = true;
				// flush the ufn table
				clearUfnTable();
			}
			// try this then. it just returns if
			// sc->m_waitingTreeNeedsRebuild is false
			sc->populateWaitingTreeFromSpiderdb ( false );
		}
		// re-entry is false because we are entering for the first time
		sc->populateDoledbFromWaitingTree ( false );
		// skip if still loading doledb lists from disk this round
		if ( ! sc->m_didRound ) continue;
		// ok, reset it so it can start a new doledb scan
		sc->m_didRound = false;
		//sc->m_nextDoledbKey.setMin();
	}

	// set initial priority to the highest to start spidering there
	//g_spiderLoop.m_pri = MAX_SPIDER_PRIORITIES - 1;

	// spider some urls that were doled to us
	g_spiderLoop.spiderDoledUrls( );
}


SpiderColl *getNextSpiderColl ( long *cri ) ;


void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) ;

// now check our RDB_DOLEDB for SpiderRequests to spider!
void SpiderLoop::spiderDoledUrls ( ) {

	// must be spidering to dole out
	if ( ! g_conf.m_spideringEnabled ) return;
	//if ( ! g_conf.m_webSpideringEnabled )  return;
	// if we do not overlap ourselves
	if ( m_gettingDoledbList ) return;
	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) return;
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) return;
	// skip if too many udp slots being used
	if ( g_udpServer.getNumUsedSlots() >= 1300 ) return;
	// stop if too many out
	if ( m_numSpidersOut >= MAX_SPIDERS ) return;
	// bail if no collections
	if ( g_collectiondb.m_numRecs <= 0 ) return;
	// not while repairing
	if ( g_repairMode ) return;

	// when getting a lock we keep a ptr to the SpiderRequest in the
	// doledb list, so do not try to read more just yet until we know
	// if we got the lock or not
	if ( m_msg12.m_gettingLocks ) return;

	// turn on for now
	//g_conf.m_logDebugSpider = 1;

 collLoop:

	// log this now
	//logf(LOG_DEBUG,"spider: getting collnum to dole from");
	// get this
	m_sc = NULL;
	// avoid infinite loops
	long count = g_collectiondb.m_numRecs;
	// set this in the loop
	CollectionRec *cr = NULL;
	// . get the next collection to spider
	// . just alternate them
	for ( ; count > 0 ; m_cri++ , count-- ) {
		// wrap it if we should
		if ( m_cri >= g_collectiondb.m_numRecs ) m_cri = 0;
		// get rec
		cr = g_collectiondb.m_recs[m_cri];
		// stop if not enabled
		if ( ! cr->m_spideringEnabled ) continue;
		// get the spider collection for this collnum
		//m_sc = g_spiderCache.m_spiderColls[m_cri];
		m_sc = g_spiderCache.getSpiderColl(m_cri);
		// skip if none
		if ( ! m_sc ) continue;
		// skip if we completed the doledb scan for this collection
		if ( m_sc->m_didRound ) continue;
		// get max spiders
		long maxSpiders = cr->m_maxNumSpiders;
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
		// obey max spiders per collection too
		if ( m_sc->m_spidersOut >= maxSpiders ) continue;
		// ok, we are good to launch a spider for coll m_cri
		break;
	}
	
	// if none, bail, wait for sleep wrapper to re-call us later on
	if ( count == 0 ) return;

	// sanity check
	if ( m_cri >= g_collectiondb.m_numRecs ) { char *xx=NULL;*xx=0; }

	// grab this
	collnum_t collnum = m_cri;
	// get this
	char *coll = g_collectiondb.m_recs[collnum]->m_coll;

	// need this for msg5 call
	key_t endKey; endKey.setMax();

	// start at the top each time now
	//m_sc->m_nextDoledbKey.setMin();

	// set the key to this at start
	//m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_pri );

	// init the m_priorityToUfn map array?
	if ( ! m_sc->m_ufnMapValid ) {
		// reset all priorities to map to a ufn of -1
		for ( long i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ )
			m_sc->m_priorityToUfn[i] = -1;
		// initialize the map that maps priority to first ufn that uses
		// that priority. map to -1 if no ufn uses it.
		for ( long i = 0 ; i < cr->m_numRegExs ; i++ ) {
			// breathe
			QUICKPOLL ( MAX_NICENESS );
			// get the ith rule priority
			long sp = cr->m_spiderPriorities[i];
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

	// reset priority when it goes bogus
	if ( m_sc->m_pri < 0 ) {
		// i guess the scan is complete for this guy
		m_sc->m_didRound = true;
		// reset for next coll
		m_sc->m_pri = MAX_SPIDER_PRIORITIES - 1;
		// reset key now too since this coll was exhausted  WE HIT HERE!!!
		m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_sc->m_pri );
		// and go up top
		goto collLoop;
	}

	// skip the priority if we already have enough spiders on it
	long out = m_sc->m_outstandingSpiders[m_sc->m_pri];
	// get the first ufn that uses this priority
	long ufn = m_sc->m_priorityToUfn[m_sc->m_pri];
	// how many can we have? crap, this is based on ufn, not priority
	// so we need to map the priority to a ufn that uses that priority
	long max = 0;
	// see if it has a maxSpiders, if no ufn uses this priority then
	// "max" will remain set to 0
	if ( ufn >= 0 ) max = m_sc->m_cr->m_maxSpidersPerRule[ufn];
	// turned off?
	if ( ufn >= 0 && ! m_sc->m_cr->m_spidersEnabled[ufn] ) max = 0;
	// always allow at least 1, they can disable spidering otherwise
	// no, we use this to disabled spiders... if ( max <= 0 ) max = 1;
	// skip?
	if ( out >= max ) {
		// try the priority below us
		m_sc->m_pri--;
		// set the new key for this priority if valid
		if ( m_sc->m_pri >= 0 )
			m_sc->m_nextDoledbKey = 
				g_doledb.makeFirstKey2(m_sc->m_pri);
		// and try again
		goto loop;
	}

	// we only launch one spider at a time... so lock it up
	m_gettingDoledbList = true;

	// log this now
	//if ( g_conf.m_logDebugSpider ) 
	//	logf(LOG_DEBUG,"spider: loading list from doledb");

	// get a spider rec for us to spider from doledb
	if ( ! m_msg5.getList ( RDB_DOLEDB      ,
				coll            ,
				&m_list         ,
				m_sc->m_nextDoledbKey   ,
				endKey          ,
				// need to make this big because we don't
				// want to end up getting just a negative key
				//1             , // minRecSizes (~ 7000)
				// we need to read in a lot because we call
				// "goto listLoop" below if the url we want
				// to dole is locked.
				2000            , // minRecSizes
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
	log(LOG_DEBUG,"spider: read list of %li bytes from spiderdb for "
	    "pri=%li+",m_list.m_listSize,(long)m_sc->m_pri);
	// breathe
	QUICKPOLL ( MAX_NICENESS );
	// . add urls in list to cache
	// . returns true if we should read another list
	// . will set startKey to next key to start at
	if ( gotDoledbList2 ( ) ) {
		// . if priority is -1 that means try next priority
		// . DO NOT reset the whole scan. that was what was happening
		//   when we just had "goto loop;" here
		if ( m_sc->m_pri == -1 ) return;
		// bail if waiting for lock reply, no point in reading more
		if ( m_msg12.m_gettingLocks ) return;
		// gotDoledbList2() always advances m_nextDoledbKey so
		// try another read
		goto loop;
	}
	// wait for the msg12 get lock request to return... or maybe spiders are off
	return;
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
#define MAX_LOCK_AGE (3600*4)

// spider the spider rec in this list from doledb
bool SpiderLoop::gotDoledbList2 ( ) {
	// unlock
	m_gettingDoledbList = false;

	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) return false;
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) return false;
	// skip if too many udp slots being used
	if ( g_udpServer.getNumUsedSlots() >= 1300 ) return false;
	// stop if too many out
	if ( m_numSpidersOut >= MAX_SPIDERS ) return false;



	// bail if list is empty
	if ( m_list.getListSize() <= 0 ) {
		// trigger a reset
		m_sc->m_pri = -1;
		// . let the sleep timer init the loop again!
		// . no, just continue the loop
		return true;
	}

	//time_t nowGlobal = getTimeGlobal();

	// double check
	//if ( ! m_list.checkList_r( true , false, RDB_DOLEDB) ) { 
	//	char *xx=NULL;*xx=0; }

	// debug parm
	//long lastpri = -2;
	//long lockCount = 0;

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
	if ( (m_sc->m_nextDoledbKey & 0x01) == 0x01 )
		m_sc->m_nextDoledbKey += 1;
	// if its negative inc by two then! this fixes the bug where the
	// list consisted only of one negative key and was spinning forever
	else
		m_sc->m_nextDoledbKey += 2;

	// did it hit zero? that means it wrapped around!
	if ( m_sc->m_nextDoledbKey.n1 == 0x0 &&
	     m_sc->m_nextDoledbKey.n0 == 0x0 ) {
		// TODO: work this out
		char *xx=NULL;*xx=0; }

	// get priority from doledb key
	long pri = g_doledb.getPriority ( doledbKey );
	// sanity
	if ( pri < 0 || pri >= MAX_SPIDER_PRIORITIES ) { char *xx=NULL;*xx=0; }
	// skip the priority if we already have enough spiders on it
	long out = m_sc->m_outstandingSpiders[pri];
	// get the first ufn that uses this priority
	long ufn = m_sc->m_priorityToUfn[pri];
	// how many can we have? crap, this is based on ufn, not priority
	// so we need to map the priority to a ufn that uses that priority
	long max = 0;
	// see if it has a maxSpiders, if no ufn uses this priority then
	// "max" will remain set to 0
	if ( ufn >= 0 ) max = m_sc->m_cr->m_maxSpidersPerRule[ufn];
	// turned off?
	if ( ufn >= 0 && ! m_sc->m_cr->m_spidersEnabled[ufn] ) max = 0;
	// if we skipped over the priority we wanted, update that
	//m_pri = pri;
	// then do the next one after that for next round
	//m_pri--;
	// always allow at least 1, they can disable spidering otherwise
	//if ( max <= 0 ) max = 1;
	// skip? and re-get another doledb list from next priority...
	if ( out >= max ) {
		// this priority is maxed out, try next
		m_sc->m_pri = pri - 1;
		// all done if priority is negative
		if ( m_sc->m_pri < 0 ) return true;
		// set to next priority otherwise
		m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_sc->m_pri );
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

	// get the "spider rec" (SpiderRequest) (embedded in the doledb rec)
	SpiderRequest *sreq = (SpiderRequest *)(rec + sizeof(key_t)+4);
	// sanity check. check for http(s)://
	if ( sreq->m_url[0] != 'h' &&
	     // might be a docid from a pagereindex.cpp
	     ! is_digit(sreq->m_url[0]) ) { 
		// note it
		if ( (g_corruptCount % 1000) == 0 )
			log("spider: got corrupt doledb record. ignoring. "
			    "pls fix!!!");
		g_corruptCount++;
		// skip for now....!! what is causing this???
		m_list.skipCurrentRecord();
		// if exhausted -- try another load with m_nextKey set
		if ( m_list.isExhausted() ) return true;
		// otherwise, try the next doledb rec in this list
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
	// shortcut
	//long long uh48 = sreq->getUrlHash48();
	// get the lock key
	unsigned long long lockKey ;
	lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);
	// check tree
	long slot = ht->getSlot ( &lockKey );
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
	//	long long uh48   = sreq->getUrlHash48();
	//	long long pdocid = sreq->getParentDocId();
	//	// get any request from our urlhash table
	//	SpiderRequest *sreq2 = m_sc->getSpiderRequest2 (&uh48,pdocid);
	//	// must be there. i guess it could be missing if there is
	//	// corruption and we lost it in spiderdb but not in doledb...
	//	if ( ! sreq2 ) { char *xx=NULL;*xx=0; }
	//}

	// log this now
	if ( g_conf.m_logDebugSpider ) 
		logf(LOG_DEBUG,"spider: trying to spider url %s",sreq->m_url);

	// shortcut
	char *coll = m_sc->m_cr->m_coll;
	// . spider that. we don't care wheter it blocks or not
	// . crap, it will need to block to get the locks!
	// . so at least wait for that!!!
	// . but if we end up launching the spider then this should NOT
	//   return false! only return false if we should hold up the doledb
	//   scan
	// . this returns true right away if it failed to get the lock...
	//   which means the url is already locked by someone else...
	// . it might also return true if we are already spidering the url
	bool status = spiderUrl9 ( sreq , doledbKey , coll ) ;

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
bool SpiderLoop::spiderUrl9 ( SpiderRequest *sreq,key_t *doledbKey,char *coll){
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
	// SpiderCache.cpp. We don't want to freeze domain for a long time
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
	if ( ( (! g_conf.m_spideringEnabled 
		) && // ! g_conf.m_webSpideringEnabled ) &&
	       ! sreq->m_isInjecting ) || 
	     // repairing the collection's rdbs?
	     g_repairMode ||
	     // power went off?
	     ! g_process.m_powerIsOn ) {
		// try to cancel outstanding spiders, ignore injects
		for ( long i = 0 ; i <= m_maxUsed ; i++ ) {
			// get it
			XmlDoc *xd = m_docs[i];
			if ( ! xd                      ) continue;
			//if ( xd->m_oldsr.m_isInjecting ) continue;
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
	if ( g_mem.m_maxMem - g_mem.m_used < 25*1024*1024 ) {
		static long s_lastTime = 0;
		static long s_missed   = 0;
		s_missed++;
		long now = getTime();
		// don't spam the log, bug let people know about it
		if ( now - s_lastTime > 10 ) {
			log("spider: Need 25MB of free mem to launch spider, "
			    "only have %lli. Failed to launch %li times so "
			    "far.", g_mem.m_maxMem - g_mem.m_used , s_missed );
			s_lastTime = now;
		}
	}

	// shortcut
	//long long uh48 = sreq->getUrlHash48();
	unsigned long long lockKey ;
	lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);

	// . now that we have to use msg12 to see if the thing is locked
	//   to avoid spidering it.. (see comment in above function)
	//   we often try to spider something we are already spidering. that
	//   is why we have an rdbcache, m_lockCache, to make these lock
	//   lookups quick, now that the locking group is usually different
	//   than our own!
	// . we have ot check this now because removeAllLocks() below will
	//   remove a lock that one of our spiders might have. it is only
	//   sensitive to our hostid, not "spider id"
	// sometimes we exhaust the doledb and m_nextDoledbKey gets reset
	// to zero, we do a re-scan and get a doledbkey that is currently
	// being spidered or is waiting for its negative doledb key to
	// get into our doledb tree
	for ( long i = 0 ; i <= m_maxUsed ; i++ ) {
		// get it
		XmlDoc *xd = m_docs[i];
		if ( ! xd ) continue;
		// . problem if it has our doledb key!
		// . this happens if we removed the lock above before the
		//   spider returned!! that's why you need to set
		//   MAX_LOCK_AGE to like an hour or so
		// . i've also seen this happen because we got stuck looking
		//   up like 80,000 places and it was taking more than an
		//   hour. it had only reach about 30,000 after an hour.
		//   so at this point just set the lock timeout to
		//   4 hours i guess.
		//if ( xd->m_doledbKey == *doledbKey ) { char *xx=NULL;*xx=0; }
		if ( xd->m_doledbKey != *doledbKey ) continue;
		// count it as processed
		m_processed++;
		// log it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: we are already spidering %s "
			     "lockkey=%llu",sreq->m_url,lockKey);
		// all done, no lock granted...
		return true;
	}

	// reset g_errno
	g_errno = 0;
	// breathe
	QUICKPOLL(MAX_NICENESS);
	// get rid of this crap for now
	//g_spiderCache.meterBandwidth();

	// save these in case getLocks() blocks
	m_sreq      = sreq;
	m_doledbKey = doledbKey;
	m_coll      = coll;

	// if we already have the lock then forget it. this can happen
	// if spidering was turned off then back on.
	// MDW: TODO: we can't do this anymore since we no longer have
	// the lockTable check above because we do not control our own
	// lock now necessarily. it often is in another group's lockTable.
	//if ( g_spiderLoop.m_lockTable.isInTable(&lockKey) ) {
	//	log("spider: already have lock for lockKey=%llu",lockKey);
	//	// proceed
	//	return spiderUrl2();
	//}
	
	// sanity
	if ( m_msg12.m_gettingLocks ) { char *xx=NULL;*xx=0; }

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
	if ( ! m_msg12.getLocks ( m_sreq->m_probDocId,//UrlHash48(),
				  m_sreq->m_url ,
				  NULL ,
				  NULL ) ) 
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
	long i;
	for ( i=0 ; i<MAX_SPIDERS ; i++ ) if (! m_docs[i]) break;

	// breathe
	QUICKPOLL(MAX_NICENESS);

	// come back later if we're full
	if ( i >= MAX_SPIDERS ) {
		log(LOG_DEBUG,"build: Already have %li outstanding spiders.",
		    (long)MAX_SPIDERS);
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
		log("build: Could not allocate %li bytes to spider "
		    "the url %s. Will retry later.",
		    (long)sizeof(XmlDoc),  m_sreq->m_url );
		return true;
	}
	// register it's mem usage with Mem.cpp class
	mnew ( xd , sizeof(XmlDoc) , "XmlDoc" );
	// add to the array
	m_docs [ i ] = xd;

	// . pass in a pbuf if this is the "test" collection
	// . we will dump the SafeBuf output into a file in the
	//   test subdir for comparison with previous versions of gb
	//   in order to see what changed
	SafeBuf *pbuf = NULL;
	if ( !strcmp( m_coll,"test") && g_conf.m_testParserEnabled ) 
		pbuf = &xd->m_sbuf;

	//
	// sanity checks
	//
	//long long uh48;
	//long long pdocid;
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
	//	logf(LOG_DEBUG,"spider: spidering uh48=%llu pdocid=%llu",
	//	     uh48,pdocid);
	//}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: spidering uh48=%llu pdocid=%llu",
		     m_sreq->getUrlHash48(),m_sreq->getParentDocId() );
	
	if ( ! xd->set4 ( m_sreq       ,
			  m_doledbKey  ,
			  m_coll       ,
			  pbuf         ,
			  MAX_NICENESS ) )
		// error, g_errno should be set!
		return true;

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
	// count it as a hit
	//g_stats.m_spiderUrlsHit++;
	// sanity check
	if (m_sreq->m_priority <= -1 ) { char *xx=NULL;*xx=0; }
	//if(m_sreq->m_priority >= MAX_SPIDER_PRIORITIES){char *xx=NULL;*xx=0;}
	// update this
	m_sc->m_outstandingSpiders[(unsigned char)m_sreq->m_priority]++;

	// . return if this blocked
	// . no, launch another spider!
	bool status = xd->indexDoc();

	// reset the next doledbkey to start over!
	m_sc->m_pri = -1;	

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
//static long long s_lastDocId = -1;
//long long SpiderLoop::getLastDocId ( ) { return s_lastDocId; }

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
	//long saved = g_errno;

	// get our doc #, i
	//long i = doc - m_docs[0];
	long i = 0;
	for ( ; i < MAX_SPIDERS ; i++ ) if ( m_docs[i] == xd) break;
	// sanity check
	if ( i >= MAX_SPIDERS ) { char *xx=NULL;*xx=0; }
	// set to -1 to indicate inject
	//if ( i < 0 || i >= MAX_SPIDERS ) i = -1;

	//char injecting = false;
	//if ( xd->m_oldsr.m_isInjecting ) injecting = true;

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
	collnum_t collnum = g_collectiondb.getCollnum ( xd->m_coll );
	// get it
	SpiderColl *sc = g_spiderCache.m_spiderColls[collnum];
	// decrement this
	sc->m_spidersOut--;
	// get the original request from xmldoc
	SpiderRequest *sreq = &xd->m_oldsr;
	// update this
	sc->m_outstandingSpiders[(unsigned char)sreq->m_priority]--;


	// breathe
	QUICKPOLL ( xd->m_niceness );

	// are we a re-spider?
	bool respider = false;
	if ( xd->m_oldDocValid && xd->m_oldDoc ) respider = true;

	// . dump it out to a file in the "test" subdir
	// . but only the first time we spider it...
	/*
	if ( ! strcmp(xd->m_coll,"test") && ! respider &&
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
		// . should be same hash we use to store doc.%llu.html in
		//   XmlDoc.cpp/Msg13.cpp stuff (getTestDoc())
		long long h = hash64 ( u->getUrl() , u->getUrlLen() );
		char *testDir = g_test.getTestDir();
		// make filename to dump out to
		char fn[1024]; 
		sprintf(fn,"%s/%s/parse.%llu.%lu.html",
			g_hostdb.m_dir,testDir,h,g_test.m_runId);
		// . dump it out to a file
		// . WATCH OUT. g_errno is set on internal errors, like OOM
		//   or whatever, so don't save in those cases...???????
		sb.dumpToFile ( fn );
		// just dump the <div class=shotdisplay> tags into this file
		sprintf(fn,"%s/%s/parse-shortdisplay.%llu.%lu.html",
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
			   "<div class=shortdisplay>" ,
			   "</div class=shortdisplay>" );
		// header stuff
		tmp.safePrintf("\n</body></html>\n");
		// then dump
		tmp.dumpToFile ( fn );
		// if it had critical errors from XmlDoc::validateOutput()
		// then create that file!
		//if ( xd->m_validateMisses > 0 || xd->m_validateFlagged ) {
		// make the critical file filename
		char cf[1024];
		sprintf (cf,"%s/%s/critical.%llu.%lu.txt",
			 g_hostdb.m_dir,testDir,h,g_test.m_runId);
		// save to that
		ttt.dumpToFile ( cf );
		//char cmd[256];
		//sprintf(cmd,"touch %s/test/critical.%llu.%lu.txt",
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
	if ( g_errno ) {
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
		log("spider: spidering %s has error: %s. Respidering "
		    "in %li seconds. MAX_LOCK_AGE when lock expires.",
		    xd->m_firstUrl.m_url,mstrerror(g_errno),
		    (long)MAX_LOCK_AGE);
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
		log("spider: ----CRITICAL CRITICAL CRITICAL----");
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
	//if ( xd->m_oldsrValid ) m_uh48 = xd->m_oldsr.getUrlHash48();

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

// . returns false if blocked, true otherwise.
// . returns true and sets g_errno on error
// . before we can spider for a SpiderRequest we must be granted the lock
// . each group shares the same doledb and each host in the group competes
//   for spidering all those urls. 
// . that way if a host goes down is load is taken over
bool Msg12::getLocks ( long long probDocId , 
		       char *url ,
		       void *state ,
		       void (* callback)(void *state) ) {
	// do not use locks for injections
	//if ( m_sreq->m_isInjecting ) return true;
	// get # of hosts in each mirror group
	long hpg = g_hostdb.getNumHostsPerGroup();
	// reset
	m_numRequests = 0;
	m_numReplies  = 0;
	m_grants   = 0;
	m_removing = false;
	// make sure is really docid
	if ( probDocId & ~DOCID_MASK ) { char *xx=NULL;*xx=0; }
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
	m_lockKey = g_titledb.getFirstProbableDocId(probDocId);
	m_url = url;
	m_callback = callback;
	m_state = state;
	m_hasLock = false;

	// sanity check, just 6 bytes! (48 bits)
	if ( probDocId & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }

	// cache time
	long ct = 120;
	// if docid based assume it was a query reindex and keep it short!
	// otherwise we end up waiting 120 seconds for a query reindex to
	// go through on a docid we just spidered. TODO: use m_urlIsDocId
	if ( url && is_digit(url[0]) ) ct = 2;
	// . check our cache to avoid repetitive asking
	// . use -1 for maxAge to indicate no max age
	// . returns -1 if not in cache
	// . use maxage of two minutes, 120 seconds
	long lockTime = g_spiderLoop.m_lockCache.getLong(0,m_lockKey,ct,true);
	// if it was in the cache and less than 2 minutes old then return
	// true now with m_hasLock set to false
	if ( lockTime >= 0 ) {
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: cached missed lock for %s "
			     "lockkey=%llu", m_url,m_lockKey);
		return true;
	}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: sending lock request for %s "
		     "lockkey=%llu",  m_url,m_lockKey);

	// now the locking group is based on the probable docid
	unsigned long groupId = g_hostdb.getGroupIdFromDocId(m_lockKey);
	// ptr to list of hosts in the group
	Host *hosts = g_hostdb.getGroup ( groupId );

	// short cut
	UdpServer *us = &g_udpServer;

	static long s_lockSequence = 0;
	// point to start of the 12 byte request buffer
	char *request = (char *)&m_lockKey;
	// remember the lock sequence # in case we have to call remove locks
	m_lockSequence = s_lockSequence++;
	long  requestSize = 12;

	// loop over hosts in that group
	for ( long i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead (h) ) continue;
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: sent lock "
			     "request #%li for lockkey=%llu %s to "
			     "hid=%li",m_numRequests,m_lockKey,
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
		     "lockkey=%llu", m_url,m_lockKey);
	return true;
}

// returns true if all done, false if waiting for more replies
bool Msg12::gotLockReply ( UdpSlot *slot ) {
	// got reply
	m_numReplies++;
	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;
	// check for a hammer reply
	char *reply     = slot->m_readBuf;
	long  replySize = slot->m_readBufSize;
	// if error, treat as a not grant
	if ( g_errno ) {
		bool logIt = true;
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
		      logf(LOG_DEBUG,"spider: done removing all locks for %s",
			   m_url);//m_sreq->m_url);
		// we are done
		m_gettingLocks = false;
		return true;
	}
	// if got ALL locks, spider it
	if ( m_grants == m_numReplies ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: got lock for %s lockkey=%llu",
			   m_url,m_lockKey);//m_sreq->getUrlHash48());
		// flag this
		m_hasLock = true;
		// we are done
		m_gettingLocks = false;
		// ok, they are all back, resume loop
		if ( ! m_callback ) g_spiderLoop.spiderUrl2 ( );
		// all done
		return true;
	}
	// note it
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: missed lock for %s lockkey=%llu "
		     "(grants=%li)",   m_url,m_lockKey,m_grants);

	// . if it was locked by another then add to our lock cache so we do
	//   not try to lock it again
	// . if grants is not 0 then one host granted us the lock, but not
	//   all hosts, so we should probably keep trying on it until it is
	//   locked up by one host
	if ( m_grants == 0 ) {
		long now = getTimeGlobal();
		g_spiderLoop.m_lockCache.addLong(0,m_lockKey,now,NULL);
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
	// remove all locks we tried to get, BUT only if from our hostid!
	// no no! that doesn't quite work right... we might be the ones
	// locking it! i.e. another one of our spiders has it locked...
	if ( ! removeAllLocks ( ) ) return false; // true;
	// if did not block, how'd that happen?
	log("sploop: did not block in removeAllLocks: %s",mstrerror(g_errno));
	return true;
}

bool Msg12::removeAllLocks ( ) {
	// skip if injecting
	//if ( m_sreq->m_isInjecting ) return true;
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: removing all locks for %s %llu",
		     m_url,m_lockKey);
	// we are now removing 
	m_removing = true;

	// make that the request
	// . point to start of the 12 byte request buffer
	// . m_lockSequence should still be valid
	char *request     = (char *)&m_lockKey;
	long  requestSize = 12;

	// now the locking group is based on the probable docid
	unsigned long groupId = g_hostdb.getGroupIdFromDocId(m_lockKey);
	// ptr to list of hosts in the group
	Host *hosts = g_hostdb.getGroup ( groupId );
	// this must select the same group that is going to spider it!
	// i.e. our group! because we check our local lock table to see
	// if a doled url is locked before spidering it ourselves.
	//Host *hosts = g_hostdb.getMyGroup();
	// short cut
	UdpServer *us = &g_udpServer;
	// set the hi bit though for this one
	m_lockKey |= 0x8000000000000000LL;
	// get # of hosts in each mirror group
	long hpg = g_hostdb.getNumHostsPerGroup();
	// loop over hosts in that group
	for ( long i = 0 ; i < hpg ; i++ ) {
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

void handleRequest12 ( UdpSlot *udpSlot , long niceness ) {
	// get request
	char *request = udpSlot->m_readBuf;
	long  reqSize = udpSlot->m_readBufSize;
	// short cut
	UdpServer *us = &g_udpServer;
	// breathe
	QUICKPOLL ( niceness );
	// sanity check
	if ( reqSize != 12 ) {
		log("spider: bad msg12 request size");
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
	unsigned long long lockKey = *(long long *)request;
	long lockSequence = *(long *)(request+8);
	// is this a remove operation? assume not
	bool remove = false;
	// get top bit
	if ( lockKey & 0x8000000000000000LL ) remove = true;
	// mask it out
	lockKey &= 0x7fffffffffffffffLL;
	// sanity check, just 6 bytes! (48 bits)
	if ( lockKey & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }
	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: got msg12 request remove=%li",(long)remove);
	// shortcut
	char *reply = udpSlot->m_tmpBuf;
	// get time
	long nowGlobal = getTimeGlobal();
	// shortcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

	long hostId = g_hostdb.getHostId ( udpSlot->m_ip , udpSlot->m_port );
	// this must be legit - sanity check
	if ( hostId < 0 ) { char *xx=NULL;*xx=0; }


	// when we last cleaned them out
	static time_t s_lastTime = 0;
 restart:
	// scan the slots
	long ns = ht->m_numSlots;
	// only do this once per second at the most
	if ( nowGlobal <= s_lastTime ) ns = 0;
	// . clean out expired locks...
	// . if lock was there and m_expired is up, then nuke it!
	// . when Rdb.cpp receives the "fake" title rec it removes the
	//   lock, only it just sets the m_expired to a few seconds in the
	//   future to give the negative doledb key time to be absorbed.
	//   that way we don't repeat the same url we just got done spidering.
	// . this happens when we launch our lock request on a url that we
	//   or a twin is spidering or has just finished spidering, and
	//   we get the lock, but we avoided the negative doledb key.
	for ( long i = 0 ; i < ns ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		// skip if not yet expired
		if ( lock->m_expires == 0 ) continue;
		if ( lock->m_expires >= nowGlobal ) continue;
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock after waiting. elapsed=%li."
			    " lockKey=%llu hid=%li expires=%lu nowGlobal=%lu",
			    (nowGlobal - lock->m_timestamp),
			    lockKey,hostId,lock->m_expires,nowGlobal);
		// nuke the slot and possibly re-chain
		ht->removeSlot ( i );
		// gotta restart from the top since table may have shrunk
		goto restart;
	}
	// store it
	s_lastTime = nowGlobal;
		


	// check tree
	long slot = ht->getSlot ( &lockKey );
	// put it here
	UrlLock *lock = NULL;
	// if there say no no
	if ( slot >= 0 ) lock = (UrlLock *)ht->getValueFromSlot ( slot );

	// if doing a remove operation and that was our hostid then unlock it
	if ( remove && 
	     lock && 
	     lock->m_hostId == hostId &&
	     lock->m_lockSequence == lockSequence ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock for lockkey=%llu hid=%li",
			    lockKey,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// ok, at this point all remove ops return
	if ( remove ) {
		reply[0] = 1;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}

	// if lock > 1 hour old then remove it automatically!!
	if ( lock && nowGlobal - lock->m_timestamp > MAX_LOCK_AGE ) {
		// note it for now
		log("spider: removing lock after %li seconds "
		    "for lockKey=%llu hid=%li",
		    (nowGlobal - lock->m_timestamp),
		    lockKey,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// if lock still there, do not grant another lock
	if ( lock ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: refusing lock for lockkey=%llu hid=%li",
			    lockKey,hostId);
		reply[0] = 0;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}
	// make the new lock
	UrlLock tmp;
	tmp.m_hostId       = hostId;
	tmp.m_lockSequence = lockSequence;
	tmp.m_timestamp    = nowGlobal;
	tmp.m_expires      = 0;
	// put it into the table
	if ( ! ht->addKey ( &lockKey , &tmp ) ) {
		// return error if that failed!
		us->sendErrorReply ( udpSlot , g_errno );
		return;
	}
	// note it for now
	if ( g_conf.m_logDebugSpider )
		log("spider: granting lock for lockKey=%llu hid=%li",
		    lockKey,hostId);
	// grant the lock
	reply[0] = 1;
	us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
	return;
}


/////////////////////////
/////////////////////////      PAGESPIDER
/////////////////////////

// don't change name to "State" cuz that might conflict with another
class State11 {
public:
	long          m_numRecs;
	Msg5          m_msg5;
	RdbList       m_list;
	TcpSocket    *m_socket;
	HttpRequest   m_r;
	char         *m_coll;
	long          m_count;
	key_t         m_startKey;
	key_t         m_endKey;
	long          m_minRecSizes;
	bool          m_done;
	SafeBuf       m_safeBuf;
	long          m_priority;
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
		    sizeof(State11),mstrerror(g_errno));
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
				     st->m_coll          ,
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
		long spiderTime = g_doledb.getSpiderTime ( &dk );
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
			char*xx=NULL;*xx=0;}
		// get the spider rec, encapsed in the data of the doledb rec
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// print it into sbTable
		if ( ! sreq->printToTable ( sbTable,"ready",NULL))return false;
	}
	// need to load more?
	if ( st->m_count >= st->m_numRecs ||
	     // if list was a partial, this priority is short then
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

	// shortcut
	SafeBuf *sbTable = &st->m_safeBuf;

	// generate a query string to pass to host bar
	char qs[64]; sprintf ( qs , "&n=%li", st->m_numRecs );

	// store the page in here!
	SafeBuf sb;
	sb.reserve ( 64*1024 );

	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r , qs );

	// begin the table
	sb.safePrintf ( "<table width=100%% border=1 cellpadding=4 "
			"bgcolor=#%s>\n" 
			"<tr><td colspan=50 bgcolor=#%s>"
			"<b>Currently Spidering</b> (%li spiders)"
			"</td></tr>\n" ,
			LIGHT_BLUE,
			DARK_BLUE,
			(long)g_spiderLoop.m_numSpidersOut);
	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb , true ) ) return false;
	// shortcut
	XmlDoc **docs = g_spiderLoop.m_docs;
	// first print the spider recs we are spidering
	for ( long i = 0 ; i < (long)MAX_SPIDERS ; i++ ) {
		// get it
		XmlDoc *xd = docs[i];
		// skip if empty
		if ( ! xd ) continue;
		// sanity check
		if ( ! xd->m_oldsrValid ) { char *xx=NULL;*xx=0; }
		// grab it
		SpiderRequest *oldsr = &xd->m_oldsr;
		// get status
		char *status = xd->m_statusMsg;
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd) ) return false;
	}
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );


	// begin the table
	sb.safePrintf ( "<table width=100%% border=1 cellpadding=4 "
			"bgcolor=#%s>\n" 
			"<tr><td colspan=50 bgcolor=#%s>"
			"<b>Waiting to Spider (coll = "
			"<font color=red><b>%s</b>"
			"</font>)"
			,
			LIGHT_BLUE,
			DARK_BLUE ,
			st->m_coll );

	// print time format: 7/23/1971 10:45:32
	time_t nowUTC = getTimeGlobal();
	struct tm *timeStruct ;
	char time[256];
	timeStruct = gmtime ( &nowUTC );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb.safePrintf("</b> (current time = %s = %lu) "
		      "</td></tr>\n" 
		      ,time,nowUTC);

	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb ,false ) ) return false;
	// the the doledb spider recs
	char *bs = sbTable->getBufStart();
	if ( bs && ! sb.safePrintf("%s",bs) ) return false;
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );



	// get spider coll
	collnum_t collnum = g_collectiondb.getCollnum ( st->m_coll );
	// then spider collection
	//SpiderColl *sc = g_spiderCache.m_spiderColls[collnum];
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);


	/////////////////
	//
	// PRINT WAITING TREE
	//
	// each row is an ip. print the next url to spider for that ip.
	//
	/////////////////
	sb.safePrintf ( "<table width=100%% border=1 cellpadding=4 "
			"bgcolor=#%s>\n" 
			"<tr><td colspan=50 bgcolor=#%s>"
			"<b>Next Url to Spider per IP (coll = "
			"<font color=red><b>%s</b>"
			"</font>)"
			,
			LIGHT_BLUE,
			DARK_BLUE ,
			st->m_coll );
	// print time format: 7/23/1971 10:45:32
	long long timems = gettimeofdayInMillisecondsGlobal();
	sb.safePrintf("</b> (current time = %llu)(totalcount=%li)"
		      "(waittablecount=%li)</td></tr>\n",
		      timems,
		      sc->m_waitingTree.getNumUsedNodes(),
		      sc->m_waitingTable.getNumUsedSlots());
	sb.safePrintf("<tr>");
	sb.safePrintf("<td><b>spidertime (MS)</b></td>\n");
	sb.safePrintf("<td><b>firstip</b></td>\n");
	sb.safePrintf("</tr>\n");
	// the the waiting tree
	long node = sc->m_waitingTree.getFirstNode();
	long count = 0;
	for ( ; node >= 0 ; node = sc->m_waitingTree.getNextNode(node) ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// get key
		key_t *key = (key_t *)sc->m_waitingTree.getKey(node);
		// get ip from that
		long firstIp = (key->n0) & 0xffffffff;
		// get the time
		unsigned long long spiderTimeMS = key->n1;
		// shift upp
		spiderTimeMS <<= 32;
		// or in
		spiderTimeMS |= (key->n0 >> 32);
		// get the rest of the data
		sb.safePrintf("<tr>"
			      "<td>%llu</td>"
			      "<td>%s</td>"
			      "</tr>\n",
			      spiderTimeMS,
			      iptoa(firstIp));
		// stop after 20
		if ( ++count == 20 ) break;
	}
	// ...
	if ( count ) 
		sb.safePrintf("<tr><td colspan=10>...</td></tr>\n");
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
		for(long i = 0; i < m->getNumSlots();i++) {
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
		       "<td>checking quota exact</td><td>doing an exact "
		       "quota check. Does a realtime site: query to see how "
		       "many pages are indexed from this site. Uses a special "
		       "counting cache to speed this procedure up."
		       "</td></tr>"

		       "<tr>"
		       "<td>checking quota</td><td>doing a fuzzy (but fast) "
		       "quota check. Usually accurate to the nearest 5000 "
		       "pages or so."
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

	//
	// spiderdb rec stats, from scanning spiderdb
	//

	// if not there, forget about it
	if ( sc ) sc->printStats ( sb );

	//
	// Spiders Table
	//
	long long totalPoints = g_stats.m_totalSpiderSuccessNew +
				g_stats.m_totalSpiderErrorsNew +
				g_stats.m_totalSpiderSuccessOld +
				g_stats.m_totalSpiderErrorsOld;
	long long totalNew = g_stats.m_totalSpiderSuccessNew +
			     g_stats.m_totalSpiderErrorsNew;
	long long totalOld = g_stats.m_totalSpiderSuccessOld +
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
	long points = g_stats.m_spiderSample;
	if ( points > 1000 ) points = 1000;
	long sampleNew = g_stats.m_spiderNew;
	long sampleOld = points - g_stats.m_spiderNew;
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

	sb.safePrintf (
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=7 bgcolor=#%s>"
		       "<center><b>Spider Stats</b></td></tr>\n"
		       "<tr><td>"
		       "</td><td><b>Total</b></td>"
		       "<td><b>Total New</b></td>"
		       "<td><b>Total Old</b></td>"
		       "<td><b>Sample</b></td>"
		       "<td><b>Sample New</b></td>"
		       "<td><b>Sample Old</b></b>"
		       "</td></tr>"

		       "<tr><td><b>Total Spiders</n>"
		       "</td><td>%lli</td><td>%lli</td><td>%lli</td>\n"
		       "</td><td>%li</td><td>%li</td><td>%li</td></tr>\n"
		       //"<tr><td><b>Successful Spiders</n>"
		       //"</td><td>%lli</td><td>%lli</td><td>%lli</td>\n"
		       //"</td><td>%li</td><td>%li</td><td>%li</td></tr>\n"
		       //"<tr><td><b>Failed Spiders</n>"
		       //"</td><td>%lli</td><td>%lli</td><td>%lli</td>\n"
		       //"</td><td>%li</td><td>%li</td><td>%li</td></tr>\n"
		       "<tr><td><b>Success Rate</b>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td></tr>",
		       LIGHT_BLUE,  DARK_BLUE,
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

	long bucketsNew[65536];
	long bucketsOld[65536];
	memset ( bucketsNew , 0 , 65536*4 );
	memset ( bucketsOld , 0 , 65536*4 );
	for ( long i = 0 ; i < points; i++ ) {
		long n = g_stats.m_errCodes[i];
		if ( n < 0 || n > 65535 ) {
			log("admin: Bad spider error code.");
			continue;
		}
		if ( g_stats.m_isSampleNew[i] )
			bucketsNew[n]++;
		else
			bucketsOld[n]++;
	}
	for ( long i = 0 ; i < 65536 ; i++ ) {
		if ( g_stats.m_allErrorsNew[i] == 0 &&
		     g_stats.m_allErrorsOld[i] == 0 &&
		     bucketsNew[i] == 0 && bucketsOld[i] == 0 ) continue;
		sb.safePrintf (
			       "<tr><td><b>%s</b></td>"
			       "<td>%lli</td>"
			       "<td>%lli</td>"
			       "<td>%lli</td>"
			       "<td>%li</td>"
			       "<td>%li</td>"
			       "<td>%li</td>"
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

	sb.safePrintf ( "</table><br><br>\n" );



	// describe the various parms
	/*
	sb.safePrintf ( 
		       "<table width=100%% bgcolor=#%s "
		       "cellpadding=4 border=1>"
		       "<tr>"
		       "<td colspan=2 bgcolor=#%s>"
		       "<b>Field descriptions</b>"
		       "</td>"
		       "</tr>\n"
		       "<tr>"
		       "<td>hits</td><td>The number of  attempts that were "
		       "made by the spider to read a url from the spider "
		       "queue cache.</td>"
		       "</tr>\n"


		       "<tr>"
		       "<td>misses</td><td>The number of those attempts that "
		       "failed to get a url to spider.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>cached</td><td>The number of urls that are "
		       "currently in the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>water</td><td>The number of urls that were in the "
		       "spider queue cache at any one time, since the start "
		       "of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>kicked</td><td>The number of urls that were "
		       "replaced in the spider queue cache with urls loaded "
		       "from disk, since the start of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>added</td><td>The number of urls that were added "
		       "to the spider queue cache since the start of the last "
		       "disk scan. After a document is spidered its url "
		       "if often added again to the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr>"
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

		       "<tr>"
		       "<td>nl</td><td>This is 1 iff Gigablast currently "
		       "needs to reload the spider queue cache from disk.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>rnl</td><td>This is 1 iff Gigablast currently "
		       "really needs to reload the spider queue cache from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>more</td><td>This is 1 iff there are urls on "
		       "the disk that are not in the spider queue cache.</td>"
		       "</tr>\n"


		       "<tr>"
		       "<td>loading</td><td>This is 1 iff Gigablast is "
		       "currently loading this spider cache queue from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>scanned</td><td>The number of bytes that were "
		       "read from disk since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>reads</td><td>The number of disk read "
		       "operations since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr>"
		       "<td>elapsed</td><td>The time in seconds that has "
		       "elapsed since the start or end of the last disk "
		       "scan, depending on if a scan is currently in "
		       "progress.</td>"
		       "</tr>\n"

		       "</table>\n",

		       LIGHT_BLUE ,
		       DARK_BLUE  );
	*/

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
static long s_craigsList;

bool initAggregatorTable ( ) {
	// this hashtable is used for "isyellowpages" and "iseventaggregator"
	if ( s_init ) return true;
	// use niceness 0
	s_table.set(4,1,4096,s_buf,25000,false,0,"spsitetbl");
	// now stock it with yellow pages sites
	long n = (long)sizeof(s_ypSites)/ sizeof(char *); 
	for ( long i = 0 ; i < n ; i++ ) {
		char *s    = s_ypSites[i];
		long  slen = gbstrlen ( s );
		long h32 = hash32 ( s , slen );
		char val = 1;
		if ( ! s_table.addKey(&h32,&val)) {char*xx=NULL;*xx=0;}
	}
	// then stock with event aggregator sites
	n = (long)sizeof(s_aggSites)/ sizeof(char *); 
	for ( long i = 0 ; i < n ; i++ ) {
		char *s    = s_aggSites[i];
		long  slen = gbstrlen ( s );
		long h32 = hash32 ( s , slen );
		char val = 2;
		if ( ! s_table.addKey(&h32,&val)) {char*xx=NULL;*xx=0;}
	}
	// do not repeat this
	s_init = true;
	s_craigsList = hash32n("craigslist.org");
	return true;
}

bool isAggregator ( long siteHash32,long domHash32,char *url,long urlLen ) {
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

// . this is called by SpiderCache.cpp for every url it scans in spiderdb
// . we must skip certain rules in getUrlFilterNum() when doing to for Msg20
//   because things like "parentIsRSS" can be both true or false since a url
//   can have multiple spider recs associated with it!
long getUrlFilterNum2 ( SpiderRequest *sreq       ,
		       SpiderReply   *srep       ,
		       long           nowGlobal  ,
		       bool           isForMsg20 ,
		       long           niceness   ,
		       CollectionRec *cr         ,
		       bool           isOutlink  ) {

	// convert lang to string
	char *lang    = NULL;
	long  langLen = 0;
	if ( srep ) {
		// this is NULL on corruption
		lang = getLanguageAbbr ( srep->m_langId );	
		langLen = gbstrlen(lang);
	}

	char *tld = (char *)-1;
	long  tldLen;

	long  urlLen = sreq->getUrlLen();
	char *url    = sreq->m_url;

	//if ( strstr(url,"login.yahoo.com/") )
	//	log("hey");

	//initAggregatorTable();

	//long tldlen2;
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

	// CONSIDER COMPILING FOR SPEED:
	// 1) each command can be combined into a bitmask on the spiderRequest
	//    bits, or an access to m_siteNumInlinks, or a substring match
	// 2) put all the strings we got into the list of Needles
	// 3) then generate the list of needles the SpiderRequest/url matches
	// 4) then reduce each line to a list of needles to have, a
	//    min/max/equal siteNumInlinks, min/max/equal hopCount,
	//    and a bitMask to match the bit flags in the SpiderRequest

	// stop at first regular expression it matches
	for ( long i = 0 ; i < cr->m_numRegExs ; i++ ) {
		// breathe
		QUICKPOLL ( niceness );
		// get the ith rule
		char *p = cr->m_regExs[i];

	checkNextRule:

		// skip leading whitespace
		while ( *p && isspace(*p) ) p++;

		// do we have a leading '!'
		bool val = 0;
		if ( *p == '!' ) { val = 1; p++; }
		// skip whitespace after the '!'
		while ( *p && isspace(*p) ) p++;

		if ( *p=='h' && strncmp(p,"hasauthorityinlink",18) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! sreq->m_hasAuthorityInlinkValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_hasAuthorityInlink==val)continue;
			// allow "!isindexed" if no SpiderReply at all
			//if ( ! srep && val == 0 ) continue;
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
			// allow "!isindexed" if no SpiderReply at all
			//if ( ! srep && val == 0 ) continue;
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
			// allow "!isindexed" if no SpiderReply at all
			//if ( ! srep && val == 0 ) continue;
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
			// allow "!isindexed" if no SpiderReply at all
			//if ( ! srep && val == 0 ) continue;
			// skip
			p += 6;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
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
			long errCode = srep->m_errCode;
			// . make it zero if not tmp error
			// . now have EDOCUNCHANGED and EDOCNOGOODDATE from
			//   Msg13.cpp, so don't count those here...
			if ( errCode != EDNSTIMEDOUT &&
			     errCode != ETCPTIMEDOUT &&
			     errCode != EDNSDEAD &&
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

		if ( *p != 'i' ) goto skipi;

		if ( strncmp(p,"isinjected",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_isInjecting==val ) continue;
			// allow "!isindexed" if no SpiderReply at all
			//if ( ! srep && val == 0 ) continue;
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
			if ( (bool)sreq->m_urlIsDocId==val ) continue;
			// skip
			p += 10;
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
			// allow "!isindexed" if no SpiderReply at all
			//if ( ! srep && val == 0 ) continue;
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
			// must have a reply
			//if ( ! srep ) continue;
			// skip for msg20
			if ( isForMsg20 ) continue;
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
			// must have a reply
			//if ( ! srep ) continue;
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
		//   www.weblogs.com/shortChanges.xml and if it is we put
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

		if ( strncmp ( p , "isonsite",6 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			if ( val == 0 &&
			     sreq->m_parentHostHash32 != sreq->m_hostHash32 ) 
				continue;
			if ( val == 1 &&
			     sreq->m_parentHostHash32 != sreq->m_hostHash32 ) 
				continue;
			p += 6;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}


		// check for "isrss" aka "rss"
		if ( strncmp(p,"isrss",5) == 0 ) {
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
			if ( (bool)srep != (bool)val ) continue;
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
			// now this is a bit
			if ( (bool)sreq->m_isWWWSubdomain == (bool)val ) 
				continue;
			/*
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
			*/
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

		// set the sign
		char *s = p;
		// skip s to after
		while ( *s && is_alpha_a(*s) ) s++;
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
			//long  alen = tldLen;
			char *b    = s;
			// loop for the comma-separated list of tlds
			// like tld:us,uk,fr,it,de
		subloop1:
			// get length of it in the regular expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			long  blen = b - start;
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
			if ( ! srep ) continue;
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
			long  blen = b - start;
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

		// hopcount == 20 [&&]
		if ( *p=='h' && strncmp(p, "hopcount", 8) == 0){
			// skip if not valid
			if ( ! sreq->m_hopCountValid ) continue;
			// shortcut
			long a = sreq->m_hopCount;
			// make it point to the priority
			long b = atoi(s);
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

		if ( *p=='e' && strncmp(p,"errorcount",10) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// shortcut
			long a = srep->m_errCount;
			// make it point to the retry count
			long b = atoi(s);
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

		// siteNumInlinks >= 300 [&&]
		if ( *p=='s' && strncmp(p, "sitenuminlinks", 14) == 0){
			// these are -1 if they are NOT valid
			long a1 = sreq->m_siteNumInlinks;
			// only assign if valid
			long a2 = -1; if ( srep ) a2 = srep->m_siteNumInlinks;
			// assume a1 is the best
			long a ;
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
			long b = atoi(s);
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
			// shortcut
			long a = sr->m_retryNum;
			// make it point to the priority
			long b = atoi(s);
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
			if ( srep->m_spideredTime ==  0 ) {char*xx=NULL;*xx=0;}
			if ( srep->m_spideredTime == -1 ) {char*xx=NULL;*xx=0;}
			// shortcut
			float af = (srep->m_spideredTime - nowGlobal);
			// make into days
			af /= (3600.0*24.0);
			// back to a long, round it
			long a = (long)(af + 0.5);
			// make it point to the priority
			long b = atoi(s);
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
			// shortcut
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
			// shortcut (errCode doubles as g_errno)
			long a = srep->m_errCode;
			// make it point to the priority
			long b = atoi(s);
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
			// shortcut
			long age;
			if ( srep->m_pubDate <= 0 ) age = -1;
			else age = nowGlobal - srep->m_pubDate;
			// we can not match if invalid
			if ( age <= 0 ) continue;
			// make it point to the priority
			long b = atoi(s);
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
			long a = srep->m_newRequests;
			long b = atoi(s);
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
			long plen = p - pstart;
			// . do we match it?
			// . url has to be at least as big
			if ( urlLen>=plen && plen>0 &&
			     strncmp(pstart,url,plen)==0 )
				return i;
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
			long plen = p - pstart;
			// . do we match it?
			// . url has to be at least as big
			// . match our tail
			if ( urlLen>=plen && plen>0 &&
			     strncmp(pstart,url+urlLen-plen,plen)==0)
				return i;
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
		long plen = p - pstart;
		// need something...
		if ( plen <= 0 ) continue;
		// must be at least as big
		if ( urlLen < plen ) continue;
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

		// was it a match?
		if ( found ) 
			return i;
	}
	// sanity check ... must be a default rule!
	char *xx=NULL;*xx=0;
	// return -1 if no match, caller should use a default
	return -1;
}

//static bool s_ufnInit = false;
static HashTableX s_ufnTable;

void clearUfnTable ( ) { 
	s_ufnTable.clear(); 
	s_ufnTree.clear();
}

long getUrlFilterNum ( SpiderRequest *sreq       ,
		       SpiderReply   *srep       ,
		       long           nowGlobal  ,
		       bool           isForMsg20 ,
		       long           niceness   ,
		       CollectionRec *cr         ,
		       bool           isOutlink  ) {

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
	long long key64 = sreq->getUrlHash48();
	key64 ^= (long long)sreq->m_addedTime;
	if ( srep ) key64 ^= ((long long)srep->m_spideredTime)<<32;
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
				      isOutlink);

	/*
	// is table full? clear it if so
	if ( s_ufnTable.getNumSlotsUsed() > 2000000 ) {
		log("spider: resetting ufn table");
		s_ufnTable.clear();
	}
	// cache it
	s_ufnTable.addKey ( &key64 , &ufn );
	*/

	return (long)ufn;
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
void dedupSpiderdbList ( RdbList *list , long niceness , bool removeNegRecs ) {

	//long  need = list->m_listSize;
	char *newList = list->m_list;//(char *)mmalloc (need,"dslist");
	//if ( ! newList ) {
	//	log("spider: could not dedup spiderdb list: %s",
	//	    mstrerror(g_errno));
	//	return;
	//}
	char *dst          = newList;
	char *restorePoint = newList;
	long long reqUh48  = 0LL;
	long long repUh48  = 0LL;
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
			// shortcut
			long long uh48 = srep->getUrlHash48();
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
			long recSize = srep->getRecSize();
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

		// shortcut
		SpiderRequest *sreq = (SpiderRequest *)rec;

		// shortcut
		long long uh48 = sreq->getUrlHash48();

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
			if ( sreq->m_urlIsDocId ) continue;

			SpiderReply *old = oldRep;
			sreq->m_inGoogle           = old->m_inGoogle;
			sreq->m_hasAuthorityInlink = old->m_hasAuthorityInlink;
			sreq->m_hasContactInfo     = old->m_hasContactInfo;
			sreq->m_hasSiteVenue       = old->m_hasSiteVenue;
		}

		// if we are not the same url as last request, add it
		if ( uh48 != reqUh48 ) {
			// a nice hook in
		addIt:
			// save in case we get erased
			restorePoint = dst;
			prevLastKey  = lastKey;
			// get our size
			long recSize = sreq->getRecSize();
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
		if ( sreq->m_hopCountValid && oldReq->m_hopCountValid ) {
			if ( oldReq->m_hopCount < sreq->m_hopCount )
				sreq->m_hopCount = oldReq->m_hopCount;
			else
				oldReq->m_hopCount = sreq->m_hopCount;
		}

		// if he's essentially different input parms but for the
		// same url, we want to keep him because he might map the
		// url to a different url priority!
		if ( oldReq->m_siteHash32    != sreq->m_siteHash32    ||
		     oldReq->m_isNewOutlink  != sreq->m_isNewOutlink  ||
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
		if ( sreq->m_addedTime <= oldReq->m_addedTime ) continue;
		// otherwise, erase over him
		dst     = restorePoint;
		lastKey = prevLastKey;
		// and add us over top of him
		goto addIt;
	}

	// free the old list
	//char *oldbuf  = list->m_alloc;
	//long  oldSize = list->m_allocSize;

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
