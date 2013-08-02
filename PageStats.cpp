#include "gb-include.h"

#include "Indexdb.h"     // makeKey(long long docId)
#include "Datedb.h"
#include "Titledb.h"
//#include "Revdb.h"
#include "Spider.h"
#include "Tfndb.h"
#include "Tagdb.h"
#include "Cachedb.h"
#include "Monitordb.h"
#include "Catdb.h"
//#include "Checksumdb.h"
#include "Clusterdb.h"
#include "Statsdb.h"
#include "Linkdb.h"
#include "Dns.h"
#include "TcpServer.h"
#include "UdpServer.h"
#include "Msg40.h"       // g_resultsCache
#include "Pages.h"
//#include "Msg36.h" // g_qtable
#include "Stats.h"
#include <sys/time.h>      // getrlimit()
#include <sys/resource.h>  // getrlimit()
//#include "GBVersion.h"
//#include "Msg10.h" // g_deadWaitCache
#include "Proxy.h"
#include "Placedb.h"
#include "Sections.h"
//#include "Msg0.h" // g_termlistCache

bool printNumAbbr ( SafeBuf &p, long long vvv ) {
	float val = (float)vvv;
	char *suffix = "K";
	val /= 1024;
	if ( val > 1000.0 ) { val /= 1024.0; suffix = "M"; }
	if ( val == 0.0 ) 
		p.safePrintf("<td>0</td>");
	else if ( suffix[0] =='K' )
		p.safePrintf("<td>%.00f%s</td>",val,suffix);
	else
		p.safePrintf("<td><b>%.01f%s</b></td>",val,suffix);
	return true;
}

bool printUptime ( SafeBuf &sb ) {
	long uptime = time(NULL) - g_stats.m_uptimeStart ;
	// sanity check... wtf?
	if ( uptime < 0 ) { uptime = 0; };

	long days  = uptime / 86400; uptime -= days  * 86400;
	long hours = uptime /  3600; uptime -= hours * 3600;
	long mins  = uptime /    60; uptime -= mins  * 60;
	long secs  = uptime;

	// singular plural
	char *ds = "day";
	if ( days != 1 ) ds = "days";
	char *hs = "hour";
	if ( hours != 1 ) hs = "hours";
	char *ms = "minute";
	if ( mins != 1 ) ms = "minutes";
	char *ss = "seconds";
	if ( secs == 1 ) ss = "second";
	
	if ( days >= 1 )
		sb.safePrintf("%li %s ",days,ds);

	if ( hours >= 1 )
		sb.safePrintf("%li %s ", hours,hs);

	if ( mins >= 1 )
		sb.safePrintf("%li %s ", mins,ms);

	if ( secs != 0 ) 
		sb.safePrintf(" %li %s",secs,ss);
	return true;
}

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageStats ( TcpSocket *s , HttpRequest *r ) {
	// don't allow pages bigger than 128k in cache
	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);
	//char *bufEnd = buf + 256*1024;
	// a ptr into "buf"
	//char *p    = buf;
	//char *pend = buf + 64*1024;
	// password, too
	//long pwdLen = 0;
	//char *pwd = r->getString ( "pwd" , &pwdLen );
	//if ( pwdLen > 31 ) pwdLen = 31;
	//char pbuf [32];
	//if ( pwdLen > 0 ) strncpy ( pbuf , pwd , pwdLen );
	//pbuf[pwdLen]='\0';

	// print standard header
	// 	char *ss = p.getBuf();
	// 	char *ssend = p.getBufEnd();
	g_pages.printAdminTop ( &p , s , r );
	//      p.incrementLength(sss - ss);

	struct rusage ru;
	if ( getrusage ( RUSAGE_SELF , &ru ) )
		log("admin: getrusage: %s.",mstrerror(errno));

	// memory in general table
	p.safePrintf (
		  "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		  "<tr>"
		  "<td colspan=2 bgcolor=#%s>"
		  "<center><b>Memory</b></td></tr>\n"
		  "<tr><td><b>memory allocated</b></td><td>%lli</td></tr>\n"
		  "<tr><td><b>max memory limit</b></td><td>%lli</td></tr>\n" 
		  //"<tr><td>mem available</td><td>%lli</td></tr>\n"
		  "<tr><td>max allocated</td><td>%lli</td></tr>\n"
		  "<tr><td>max single alloc</td><td>%lli</td></tr>\n"
		  "<tr><td>max single alloc by</td><td>%s</td></tr>\n" 
		  "<tr><td>shared mem used</td><td>%lli</td></tr>\n"
		  "<tr><td>swaps</td><td>%lli</td></tr>\n",
		  //"<tr><td>num alloc chunks</td><td>%li</td></tr>\n",
		  LIGHT_BLUE , DARK_BLUE ,
		  g_mem.getUsedMem() ,
		  g_mem.getMaxMem() ,
		  //g_mem.getAvailMem(),
		  g_mem.getMaxAlloced() ,
		  g_mem.getMaxAlloc(),
		  g_mem.getMaxAllocBy() ,
		  g_mem.m_sharedUsed,
		  (long long)ru.ru_nswap); // idrss,
	          //g_mem.getNumChunks());

	p.safePrintf (
		       "<tr><td><b>current allocations</b></td>"
		       "<td>%li</td></tr>\n" 
		       "<tr><td><b>total allocations</b></td>"
		       "<td>%lli</td></tr>\n" ,
		       g_mem.getNumAllocated() ,
		       (long long)g_mem.getNumTotalAllocated() );

	// end table
	p.safePrintf ( "</table><br>" );

	//Query performance stats
	g_stats.calcQueryStats();
	long days;
	long hours;
	long minutes;
	long secs;
	long msecs;
	getCalendarFromMs(g_stats.m_upTime,
			  &days, 
			  &hours, 
			  &minutes, 
			  &secs,
			  &msecs);

	long long avgTier0Time = 0;
	long long avgTier1Time = 0;
	long long avgTier2Time = 0;
	if ( g_stats.m_tierHits[0] > 0 )
		avgTier0Time = g_stats.m_tierTimes[0] /
			(long long)g_stats.m_tierHits[0];
	if ( g_stats.m_tierHits[1] > 0 )
		avgTier1Time = g_stats.m_tierTimes[1] /
			(long long)g_stats.m_tierHits[1];
	if ( g_stats.m_tierHits[2] > 0 )
		avgTier2Time = g_stats.m_tierTimes[2] /
			(long long)g_stats.m_tierHits[2];
	p.safePrintf ( 
		       "<br>"
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=2 bgcolor=#%s>"
		       "<center><b>Queries</b></td></tr>\n"

		       "<tr><td><b>Average Query Latency for last %li queries"
		       "</b></td><td>%f seconds</td></tr>\n"

		       "<tr><td><b>Average queries/sec. for last %li queries"
		       "</b></td><td>%f queries/sec.</td></tr>\n"

		       "<tr><td><b>Query Success Rate for last %li queries"
		       "</b></td><td>%f"

		       "<tr><td><b>Total Queries Served</b></td><td>%li"
		       "<tr><td><b>Total Successful Queries</b></td><td>%li"
		       "<tr><td><b>Total Failed Queries</b></td><td>%li"
		       "<tr><td><b>Total Query Success Rate</b></td><td>%f"
		       "</td></tr>"
		       "<tr><td><b>Uptime</b></td><td>%li days %li hrs %li min %li sec"
		       "</td></tr>"
		       "<tr><td><b>Sockets Closed Because We Hit the Limit"
		       "</b></td><td>%li"
		       "</td></tr>",
		       LIGHT_BLUE , DARK_BLUE ,
		       g_stats.m_numQueries,
		       g_stats.m_avgQueryTime, 

		       g_stats.m_numQueries,
		       g_stats.m_avgQueriesPerSec, 

		       g_stats.m_numQueries,
		       g_stats.m_successRate,
		       g_stats.m_totalNumQueries + g_stats.m_numSuccess + 
		       g_stats.m_numFails,
		       g_stats.m_totalNumSuccess + g_stats.m_numSuccess ,
		       g_stats.m_totalNumFails   + g_stats.m_numFails   ,
		       (float)g_stats.m_totalNumSuccess / 
		       (float)(g_stats.m_totalNumSuccess + 
			       g_stats.m_totalNumFails),
		       days, hours, minutes, secs,
		       g_stats.m_closedSockets );

	long long total = 0;
	for ( long i = 0 ; i <= CR_OK ; i++ )
		total += g_stats.m_filterStats[i];

	p.safePrintf ( "<tr><td><b>Total DocIds Generated</b></td><td>%lli"
		       "</td></tr>\n" , total );

	// print each filter stat
	for ( long i = 0 ; i < CR_END ; i++ )
		p.safePrintf("<tr><td>&nbsp;&nbsp;%s</td>"
			     "<td>%li</td></tr>\n" , 
			     g_crStrings[i],g_stats.m_filterStats[i] );

	p.safePrintf(
		     "<tr><td><b>Tier 0 Hits</b></td><td>%li</td></tr>"
		     "<tr><td><b>Tier 1 Hits</b></td><td>%li</td></tr>"
		     "<tr><td><b>Tier 2 Hits</b></td><td>%li</td></tr>"
		     "<tr><td><b>Tier 2 Exhausted</b></td><td>%li</td></tr>"
		     "<tr><td><b>Avg Tier 0 Time</b></td><td>%llims</td></tr>"
		     "<tr><td><b>Avg Tier 1 Time</b></td><td>%llims</td></tr>"
		     "<tr><td><b>Avg Tier 2 Time</b></td><td>%llims</td></tr>"
		     "<tr><td><b>Msg3a Slow Recalls</b></td><td>%li</td></tr>"
		     "<tr><td><b>Msg3a Quick Recalls</b></td><td>%li</td></tr>"
		     "<tr><td><b>Msg3a Msg40 Recalls</b></td><td>%li</td></tr>"

		     "<tr><td><b>Unjustified iCache Misses</b></td>"
		     "<td>%li</td></tr>"

		     "<tr><td>&nbsp;&nbsp;&nbsp;2</td><td>%li</td></tr>"
		     "<tr><td>&nbsp;&nbsp;&nbsp;3</td><td>%li</td></tr>"
		     "<tr><td>&nbsp;&nbsp;&nbsp;4</td><td>%li</td></tr>"
		     "<tr><td>&nbsp;&nbsp;&nbsp;5+</td><td>%li</td></tr>"
		     "</table><br><br>\n",
		     g_stats.m_tierHits[0], g_stats.m_tierHits[1],
		     g_stats.m_tierHits[2], g_stats.m_tier2Misses,
		     avgTier0Time, avgTier1Time, avgTier2Time,
		     g_stats.m_msg3aSlowRecalls,
		     g_stats.m_msg3aFastRecalls,
		     g_stats.m_msg3aRecallCnt,

		     // unjustified icache misses
		     g_stats.m_recomputeCacheMissess-g_stats.m_icacheTierJumps,

		     g_stats.m_msg3aRecalls[2], g_stats.m_msg3aRecalls[3],
		     g_stats.m_msg3aRecalls[4], g_stats.m_msg3aRecalls[5]);
	


	// stripe loads
	if ( g_hostdb.m_myHost->m_isProxy ) {
		p.safePrintf ( 
			      "<br>"
			      "<table cellpadding=4 width=100%% "
			      "bgcolor=#%s border=1>"
			      "<tr>"
			      "<td colspan=4 bgcolor=#%s>"
			      "<center><b>Stripe Loads</b></td></tr>\n" ,
			      LIGHT_BLUE , DARK_BLUE );
		p.safePrintf("<tr><td><b>Stripe #</b></td>"
			     "<td><b>Queries Out</b></td>"
			     "<td><b>Query Terms Out</b></td>"
			     "<td><b>Last HostId Used</b></td>"
			     "</tr>\n" );
		// print out load of each stripe
		long numStripes = g_hostdb.getNumStripes();
		for ( long i = 0 ; i < numStripes ; i++ )
			p.safePrintf("<tr><td>%li</td>"
				     "<td>%li</td>"
				     "<td>%li</td>"
				     "<td>%li</td></tr>\n" ,
				     i , 
				     g_proxy.m_queriesOutOnStripe [i],
				     g_proxy.m_termsOutOnStripe   [i],
				     g_proxy.m_stripeLastHostId   [i]);
		// close table
		p.safePrintf("</table><br><br>\n");
	}

	//
	// print cache table
	// columns are the caches
	//

	RdbCache *resultsCache  = &g_genericCache[SEARCHRESULTS_CACHEID];
	//RdbCache *siteLinkCache = &g_genericCache[SITELINKINFO_CACHEID];
	//RdbCache *siteQualityCache = &g_genericCache[SITEQUALITY_CACHEID];

	RdbCache *caches[20];
	caches[0] = Msg13::getHttpCacheRobots();//&g_robotdb.m_rdbCache;
	caches[1] = Msg13::getHttpCacheOthers();//&g_robotdb.m_rdbCache;
	caches[2] = g_dns.getCache();
	caches[3] = g_dns.getCacheLocal();
	caches[4] = resultsCache;
	//caches[5] = &g_termListCache;
	//caches[6] = &g_genericCache[SEORESULTS_CACHEID];
	//caches[5] = &g_qtable;
	//caches[5] = siteLinkCache;
	//caches[6] = siteQualityCache;
	//caches[7]=&g_deadWaitCache;
	//caches[5] = &g_alreadyAddedCache;
	//caches[6] = &g_forcedCache;
	//caches[9] = &g_msg20Cache;
	//caches[10] = &g_tagdb.m_listCache;
	long numCaches = 5;

	p.safePrintf (
		  "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		  "<tr>"
		  "<td colspan=%li bgcolor=#%s>"
		  "<center><b>Caches"
		  "</b></td></tr>\n",
		  LIGHT_BLUE , numCaches+2, DARK_BLUE );

	// hack since some are not init'd yet
	//g_qtable.m_dbname = "quota";
	//g_deadWaitCache.m_dbname = "deadwait";
	//g_alreadyAddedCache.m_dbname = "alreadyAdded";
	//g_forcedCache.m_dbname = "forced";
	//g_msg20Cache.m_dbname = "parser";
	//g_tagdb.m_listCache.m_dbname = "tagdbList";

	p.safePrintf ("<tr><td>&nbsp;</td>");  // 1st column is empty
	for ( long i = 0 ; i < numCaches ; i++ ) {
		p.safePrintf("<td><b>%s</b></td>",caches[i]->getDbname() );
	}
	//p.safePrintf ("<td><b><i>Total</i></b></td></tr>\n" );

	p.safePrintf ("</tr>\n<tr><td><b><nobr>hit ratio</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->getNumHits();
		long long b = caches[i]->getNumMisses();
		double r = 100.0 * (double)a / (double)(a+b);
		if ( a+b > 0.0 ) 
			p.safePrintf("<td>%.1f%%</td>",r);
		else
			p.safePrintf("<td>--</td>");
	}

	p.safePrintf ("</tr>\n<tr><td><b><nobr>hits</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->getNumHits();
		p.safePrintf("<td>%lli</td>",a);
	}

	p.safePrintf ("</tr>\n<tr><td><b><nobr>tries</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->getNumHits();
		long long b = caches[i]->getNumMisses();
		p.safePrintf("<td>%lli</td>",a+b);
	}

	p.safePrintf ("</tr>\n<tr><td><b><nobr>used slots</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->getNumUsedNodes();
		p.safePrintf("<td>%lli</td>",a);
	}

	p.safePrintf ("</tr>\n<tr><td><b><nobr>max slots</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->getNumTotalNodes();
		p.safePrintf("<td>%lli</td>",a);
	}

	p.safePrintf ("</tr>\n<tr><td><b><nobr>used bytes</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->getMemOccupied();
		p.safePrintf("<td>%lli</td>",a);
	}

	p.safePrintf ("</tr>\n<tr><td><b><nobr>max bytes</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->getMaxMem();
		p.safePrintf("<td>%lli</td>",a);
	}

	//p.safePrintf ("</tr>\n<tr><td><b><nobr>max age</td>" );
	//for ( long i = 0 ; i < numCaches ; i++ ) {
	//	long long a = caches[i]->getMaxMem();
	//	p.safePrintf("<td>%lli</td>",a);
	//}

	p.safePrintf ("</tr>\n<tr><td><b><nobr>save to disk</td>" );
	for ( long i = 0 ; i < numCaches ; i++ ) {
		long long a = caches[i]->useDisk();
		p.safePrintf("<td>%lli</td>",a);
	}

	// end the table now
	p.safePrintf ( "</tr>\n</table><br><br>" );



	//
	// Query Term Tiers Tables
	//
	/*
	p.safePrintf ("<tr><td><b>Query Terms</b></td>" );
	for ( long i = 0; i < MAX_TIERS; i++ )
		p.safePrintf ( "<td><b>Tier #%li</b></td>",i );
	p.safePrintf ( "</tr><tr>");
	for ( long i = 0; i < 14; i++ ){
		p.safePrintf ( "<td>&nbsp;&nbsp;&nbsp;%li</td>", i+1 );
		for ( long j = 0; j < MAX_TIERS; j++ )
			p.safePrintf ( "<td>%li</td>",
				       g_stats.m_numTermsVsTier[i][j] );
		p.safePrintf ( "</tr>" );
	}
	p.safePrintf("</table><br>\n");
	
	p.safePrintf ( "<br>"
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr><td colspan=8 bgcolor=#%s>"
		       "<center><b>Query Terms and Tier Vs Explicit "
		       "Matches</b></td></tr>\n"
		       "<tr><td width=\"40%%\"><b>Query Terms and Tier</b></td>"
		       "<td><b> 0+</b></td><td><b> 8+</b></td>"
		       "<td><b> 16+</b></td><td><b> 32+</b></td>"
		       "<td><b> 64+</b></td><td><b> 128+</b></td>"
		       "<td><b> 256+</b></td></tr>\n",
		       LIGHT_BLUE , DARK_BLUE );
	for ( long i = 0; i < 14; i++ )
		for ( long j = 0; j < MAX_TIERS; j++ ){
			p.safePrintf( "<tr><td>query terms=%li, tier=%li</td>",
				      i+1, j );
			for ( long k = 0; k < 7; k++ ){
				long n = g_stats.m_termsVsTierExp[i][j][k];
				p.safePrintf( "<td>%li</td>",n );
			}
			p.safePrintf( "</tr>" );
		}
	
	p.safePrintf("</table><br><br>\n");
	*/


	// 
	// General Info Table
	//
	FILE *ff = fopen ("/proc/version", "r" );
	char *kv = "unknown";
	char kbuf[1024];
	//char kbuf2[1024];
	if ( ff ) {
		fgets ( kbuf , 1000 , ff );
		//sscanf ( kbuf , "%*s %*s %s %*s", kbuf2 );
		//kv = kbuf2;
		kv = kbuf;
		fclose(ff);
	}
        time_t now = getTimeLocal();
	char nowStr[64];
	sprintf ( nowStr , "%s UTC", asctime(gmtime(&now)) );

	//
	// get the uptime as a string
	//
	SafeBuf ubuf;
	printUptime ( ubuf );
	
	p.safePrintf (
		  "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		  "<tr>"
		  "<td colspan=2 bgcolor=#%s>"
		  "<center><b>General Info</b></td></tr>\n"
		  "<tr><td><b>Uptime</b></td><td>%s</td></tr>\n"
		  "<tr><td><b>Corrupted Disk Reads</b></td><td>%li</td></tr>\n"
		  "<tr><td><b>SIGVTALRMS</b></td><td>%li</td></tr>\n"
		  "<tr><td><b>quickpolls</b></td><td>%li</td></tr>\n"
		  "<tr><td><b>Kernel Version</b></td><td>%s</td></tr>\n"
		  //"<tr><td><b>Gigablast Version</b></td><td>%s %s</td></tr>\n"
		  "<tr><td><b>Parsing Inconsistencies</b></td><td>%li</td>\n"
		  "<tr><td><b>Indexdb Splits</b></td><td>%li</td>\n"
		  //"<tr><td><b>Fully Split</b></td><td>%li</td>\n"
		  //"<tr><td><b>Tfndb Extension Bits</b></td><td>%li</td>\n"
		  "</tr>\n"
		  "<tr><td><b>Spider Locks</b></td><td>%li</td></tr>\n"
                  "<tr><td><b>Local Time</b></td><td>%s</td></tr>\n",
		  LIGHT_BLUE , DARK_BLUE ,
		  ubuf.getBufStart(),
		  g_numCorrupt,
		  g_numAlarms,
		  g_numQuickPolls,
		  kv , 
		  //GBPROJECTNAME,
		  //GBVersion ,
		  g_stats.m_parsingInconsistencies ,
		  (long)g_hostdb.m_indexSplits,//g_hostdb.m_indexSplits,
		  g_spiderLoop.m_lockTable.m_numSlotsUsed,
		  //(long)g_conf.m_fullSplit,
		  //(long)g_conf.m_tfndbExtBits,
                  nowStr);//ctime(&time));

	// end table
        if ( ! isClockInSync() ) {
		sprintf(nowStr,"not in sync with host #0");
	}
	else {
		long nowg = getTimeGlobal();
		sprintf ( nowStr , "%s UTC", asctime(gmtime(&nowg)) );
	}
	p.safePrintf ( "<tr><td><b>Global Time</b></td><td>%s</td></tr>\n" 
                       "</table><br><br>", nowStr);//ctime(&time));


	//
	// print network stats
	//
	p.safePrintf ( 
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=2 bgcolor=#%s>"
		       "<center><b>Network</b></td></tr>\n"

		       "<tr><td><b>ip1 bytes/packets in</b>"
		       "</td><td>%llu / %llu</td></tr>\n" 

		       "<tr><td><b>ip1 bytes/packets out</b>"
		       "</td><td>%llu / %llu</td></tr>\n" 

		       "<tr><td><b>ip2 bytes/packets in</b>"
		       "</td><td>%llu / %llu</td></tr>\n" 

		       "<tr><td><b>ip2 bytes/packets out</b>"
		       "</td><td>%llu / %llu</td></tr>\n" 

		       "<tr><td><b>cancel acks sent</b>"
		       "</td><td>%li</td></tr>\n" 
		       "<tr><td><b>cancel acks read</b>"
		       "</td><td>%li</td></tr>\n" 
		       "<tr><td><b>dropped dgrams</b>"
		       "</td><td>%li</td></tr>\n" 
		       "<tr><td><b>corrupt dns reply dgrams</b>"
		       "</td><td>%li</td></tr>\n" 

		       ,
		       LIGHT_BLUE,  DARK_BLUE, 
		       g_udpServer.m_eth0BytesIn,
		       g_udpServer.m_eth0PacketsIn,

		       g_udpServer.m_eth0BytesOut,
		       g_udpServer.m_eth0PacketsOut,

		       g_udpServer.m_eth1BytesIn,
		       g_udpServer.m_eth1PacketsIn,

		       g_udpServer.m_eth1BytesOut,
		       g_udpServer.m_eth1PacketsOut ,

		       g_cancelAcksSent,
		       g_cancelAcksRead,
		       g_dropped,
		       g_corruptPackets
		       );

	// break dropped dgrams down by msg type
	//for ( long i = 0 ; i < 128 ; i++ ) {
	//	if ( ! g_udpServer.m_droppedNiceness0[i] ) continue;
	//	p.safePrintf("<tr><td>msg%x niceness 0 dropped</td>"
	//		     "<td>%li</td></tr>\n",
	//		     i,g_udpServer.m_droppedNiceness0[i]);
	//}
	//for ( long i = 0 ; i < 128 ; i++ ) {
	//	if ( ! g_udpServer.m_droppedNiceness1[i] ) continue;
	//	p.safePrintf("<tr><td>msg%x dropped</td>"
	//		     "<td>%li</td></tr>\n",
	//		     i,g_udpServer.m_droppedNiceness1[i]);
	//}


	p.safePrintf ( "</table><br><br>\n" );


	if ( g_hostdb.m_myHost->m_isProxy ) {

	p.safePrintf ( 
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=50 bgcolor=#%s>"
		       "<center><b>Spider Compression Proxy Stats</b> "

		       " &nbsp; [<a href=\"/master/stats?reset=2\">"
		       "reset</a>]</td></tr>\n"

		       "<tr>"
		       "<td><b>type</b></td>\n"
		       "<td><b>#docs</b></td>\n"
		       "<td><b>bytesIn / bytesOut (ratio)</b></td>\n"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"

		       "<tr>"
		       "<td>%s</td>"
		       "<td>%llu</td>"
		       "<td>%llu / %llu (%.02f)</td>"
		       "</tr>"
		       ,
		       LIGHT_BLUE,  DARK_BLUE ,

		       "All",
		       g_stats.m_compressAllDocs,
		       g_stats.m_compressAllBytesIn,
		       g_stats.m_compressAllBytesOut,
		       (float)g_stats.m_compressAllBytesIn/
		       (float)g_stats.m_compressAllBytesOut,

		       "MimeError",
		       g_stats.m_compressMimeErrorDocs,
		       g_stats.m_compressMimeErrorBytesIn,
		       g_stats.m_compressMimeErrorBytesOut,
		       (float)g_stats.m_compressMimeErrorBytesIn/
		       (float)g_stats.m_compressMimeErrorBytesOut,

		       "Unchanged",
		       g_stats.m_compressUnchangedDocs,
		       g_stats.m_compressUnchangedBytesIn,
		       g_stats.m_compressUnchangedBytesOut,
		       (float)g_stats.m_compressUnchangedBytesIn/
		       (float)g_stats.m_compressUnchangedBytesOut,

		       "BadContent",
		       g_stats.m_compressBadContentDocs,
		       g_stats.m_compressBadContentBytesIn,
		       g_stats.m_compressBadContentBytesOut,
		       (float)g_stats.m_compressBadContentBytesIn/
		       (float)g_stats.m_compressBadContentBytesOut,


		       "BadCharset",
		       g_stats.m_compressBadCharsetDocs,
		       g_stats.m_compressBadCharsetBytesIn,
		       g_stats.m_compressBadCharsetBytesOut,
		       (float)g_stats.m_compressBadCharsetBytesIn/
		       (float)g_stats.m_compressBadCharsetBytesOut,

		       "BadContentType",
		       g_stats.m_compressBadCTypeDocs,
		       g_stats.m_compressBadCTypeBytesIn,
		       g_stats.m_compressBadCTypeBytesOut,
		       (float)g_stats.m_compressBadCTypeBytesIn/
		       (float)g_stats.m_compressBadCTypeBytesOut,

		       "BadLang",
		       g_stats.m_compressBadLangDocs,
		       g_stats.m_compressBadLangBytesIn,
		       g_stats.m_compressBadLangBytesOut,
		       (float)g_stats.m_compressBadLangBytesIn/
		       (float)g_stats.m_compressBadLangBytesOut,
		       
		       //"HasIframe",
		       //g_stats.m_compressHasIframeDocs,
		       //g_stats.m_compressHasIframeBytesIn,
		       //g_stats.m_compressHasIframeBytesOut,
		       //(float)g_stats.m_compressHasIframeBytesIn/
		       //(float)g_stats.m_compressHasIframeBytesOut,
		       

		       "FullPageRequested",
		       g_stats.m_compressFullPageDocs,
		       g_stats.m_compressFullPageBytesIn,
		       g_stats.m_compressFullPageBytesOut,
		       (float)g_stats.m_compressFullPageBytesIn/
		       (float)g_stats.m_compressFullPageBytesOut,

		       "PlainLink",
		       g_stats.m_compressPlainLinkDocs,
		       g_stats.m_compressPlainLinkBytesIn,
		       g_stats.m_compressPlainLinkBytesOut,
		       (float)g_stats.m_compressPlainLinkBytesIn/
		       (float)g_stats.m_compressPlainLinkBytesOut,
		       
		       "EmptyLink",
		       g_stats.m_compressEmptyLinkDocs,
		       g_stats.m_compressEmptyLinkBytesIn,
		       g_stats.m_compressEmptyLinkBytesOut,
		       (float)g_stats.m_compressEmptyLinkBytesIn/
		       (float)g_stats.m_compressEmptyLinkBytesOut,

		       "HasDateAndAddress",
		       g_stats.m_compressHasDateDocs,
		       g_stats.m_compressHasDateBytesIn,
		       g_stats.m_compressHasDateBytesOut,
		       (float)g_stats.m_compressHasDateBytesIn/
		       (float)g_stats.m_compressHasDateBytesOut,

		       "RobotsTxt",
		       g_stats.m_compressRobotsTxtDocs,
		       g_stats.m_compressRobotsTxtBytesIn,
		       g_stats.m_compressRobotsTxtBytesOut,
		       (float)g_stats.m_compressRobotsTxtBytesIn/
		       (float)g_stats.m_compressRobotsTxtBytesOut,

		       "UnknownType",
		       // "dates: pools overflowed" etc.
		       //"DateXmlSetError",
		       g_stats.m_compressUnknownTypeDocs,
		       g_stats.m_compressUnknownTypeBytesIn,
		       g_stats.m_compressUnknownTypeBytesOut,
		       (float)g_stats.m_compressUnknownTypeBytesIn/
		       (float)g_stats.m_compressUnknownTypeBytesOut );

	p.safePrintf ( "</table><br><br>\n" );

	}


	if ( r->getLong("reset",0) == 1 ) 
		g_stats.clearMsgStats();

	//
	// print msg re-routes
	//
	p.safePrintf ( 
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=50 bgcolor=#%s>"
		       "<center><b>Message Stats</b> "

		       " &nbsp; [<a href=\"/master/stats?reset=1\">"
		       "reset</a>]</td></tr>\n"

		       "<tr>"
		       "<td><b>niceness</td>\n"
		       "<td><b>msgtype</td>\n"

		       "<td><b>packets in</td>\n"
		       "<td><b>packets out</td>\n"

		       "<td><b>acks in</td>\n"
		       "<td><b>acks out</td>\n"

		       "<td><b>reroutes</td>\n"
		       "<td><b>dropped</td>\n"
		       "<td><b>cancels read</td>\n"
		       "<td><b>errors</td>\n"
		       "<td><b>timeouts</td>\n" 
		       "<td><b>no mem</td>\n" 
		       ,
		       LIGHT_BLUE,  DARK_BLUE );
	p.safePrintf("</tr>\n");
	// loop over niceness
	for ( long i3 = 0 ; i3 < 2 ; i3++ ) {
	// print each msg stat
	for ( long i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
	// skip it if has no handler
	if ( ! g_udpServer.m_handlers[i1] ) continue;
	if ( ! g_stats.m_reroutes   [i1][i3] &&
	     ! g_stats.m_packetsIn  [i1][i3] &&
	     ! g_stats.m_packetsOut [i1][i3] &&
	     ! g_stats.m_errors     [i1][i3] &&
	     ! g_stats.m_timeouts   [i1][i3] &&
	     ! g_stats.m_nomem      [i1][i3] &&
	     ! g_stats.m_dropped    [i1][i3] &&
	     ! g_stats.m_cancelRead [i1][i3]  )
		continue;
		// print it all out.
		p.safePrintf( 
			     "<tr>"
			      "<td>%li</td>"    // niceness, 0 or 1
			     "<td>0x%hhx</td>" // msgType
			      //"<td>%li</td>"    // request?
			      "<td>%li</td>" // packets in
			      "<td>%li</td>" // packets out
			      "<td>%li</td>" // acks in
			      "<td>%li</td>" // acks out
			      "<td>%li</td>" // reroutes
			      "<td>%li</td>" // dropped
			      "<td>%li</td>" // cancel read
			      "<td>%li</td>" // errors
			      "<td>%li</td>" // timeouts
			      "<td>%li</td>" // nomem
			      ,
			      i3, // niceness
			      (unsigned char)i1, // msgType
			      //i2, // request?
			      g_stats.m_packetsIn [i1][i3],
			      g_stats.m_packetsOut[i1][i3],
			      g_stats.m_acksIn [i1][i3],
			      g_stats.m_acksOut[i1][i3],
			      g_stats.m_reroutes[i1][i3],
			      g_stats.m_dropped[i1][i3],
			      g_stats.m_cancelRead[i1][i3],
			      g_stats.m_errors[i1][i3],
			      g_stats.m_timeouts[i1][i3],
			      g_stats.m_nomem[i1][i3]
			      );
	}
	}
	p.safePrintf ( "</table><br><br>\n" );


	//
	// print msg send times
	//
	p.safePrintf ( 
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=50 bgcolor=#%s>"
		       "<center><b>Message Send Times</b></td></tr>\n"

		       "<tr>"
		       "<td><b>niceness</td>\n"
		       "<td><b>request?</td>\n"
		       "<td><b>msgtype</td>\n"
		       "<td><b>total sent</td>\n"
		       "<td><b>avg send time</td>\n" ,
		       LIGHT_BLUE,  DARK_BLUE );
	// print bucket headers
	for ( long i = 0 ; i < MAX_BUCKETS ; i++ ) 
		p.safePrintf("<td>%i+</td>\n",(1<<i)-1);
	p.safePrintf("</tr>\n");
	// loop over niceness
	for ( long i3 = 0 ; i3 < 2 ; i3++ ) {
	// loop over isRequest?
	for ( long i2 = 0 ; i2 < 2 ; i2++ ) {
	// print each msg stat
	for ( long i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
	// skip it if has no handler
	if ( ! g_udpServer.m_handlers[i1] ) continue;
		// print it all out.
		// careful, second index is the nicenss, and third is
		// the isReply. our loops are reversed.
		long long total = g_stats.m_msgTotalOfSendTimes[i1][i3][i2];
		long long nt    = g_stats.m_msgTotalSent       [i1][i3][i2];
		// skip if no stat
		if ( nt == 0 ) continue;
		long      avg   = 0;
		if ( nt > 0 ) avg = total / nt;
		p.safePrintf( 
			     "<tr>"
			      "<td>%li</td>"    // niceness, 0 or 1
			      "<td>%li</td>"    // request?
			      "<td>0x%hhx</td>" // msgType
			      "<td>%lli</td>" // total sent
			      "<td>%lims</td>" ,// avg send time in ms
			      i3, // niceness
			      i2, // request?
			      (unsigned char)i1, // msgType
			      nt,
			      avg );
		// print buckets
		for ( long i4 = 0 ; i4 < MAX_BUCKETS ; i4++ ) {
			long long count ;
			count = g_stats.m_msgTotalSentByTime[i1][i3][i2][i4];
			p.safePrintf("<td>%lli</td>",count);
		}
		p.safePrintf("</tr>\n");
	}
	}
	}
	p.safePrintf ( "</table><br><br>\n" );


	//
	// print msg queued times
	//
	p.safePrintf ( 
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=50 bgcolor=#%s>"
		       "<center><b>Message Queued Times</b></td></tr>\n"

		       "<tr>"
		       //"<td>request?</td>\n"
		       "<td><b>niceness</td>\n"
		       "<td><b>msgtype</td>\n"
		       "<td><b>total queued</td>\n"
		       "<td><b>avg queued time</td>\n" ,
		       LIGHT_BLUE,  DARK_BLUE );
	// print bucket headers
	for ( long i = 0 ; i < MAX_BUCKETS ; i++ ) 
		p.safePrintf("<td>%i+</td>\n",(1<<i)-1);
	p.safePrintf("</tr>\n");
	// loop over niceness
	for ( long i3 = 0 ; i3 < 2 ; i3++ ) {
	// print each msg stat
	for ( long i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
	// skip it if has no handler
	if ( ! g_udpServer.m_handlers[i1] ) continue;
		// print it all out
		long long total = g_stats.m_msgTotalOfQueuedTimes[i1][i3];
		long long nt    = g_stats.m_msgTotalQueued       [i1][i3];
		// skip if no stat
		if ( nt == 0 ) continue;
		long      avg   = 0;
		if ( nt > 0 ) avg = total / nt;
		p.safePrintf( 
			     "<tr>"
			      "<td>%li</td>"    // niceness, 0 or 1
			     "<td>0x%hhx</td>" // msgType
			      //"<td>%li</td>"    // request?
			      "<td>%lli</td>" // total done
			      "<td>%lims</td>" ,// avg handler time in ms
			      i3, // niceness
			      (unsigned char)i1, // msgType
			      //i2, // request?
			      nt ,
			      avg );
		// print buckets
		for ( long i4 = 0 ; i4 < MAX_BUCKETS ; i4++ ) {
			long long count ;
			count = g_stats.m_msgTotalQueuedByTime[i1][i3][i4];
			p.safePrintf("<td>%lli</td>",count);
		}
		p.safePrintf("</tr>\n");
	}
	}
	p.safePrintf ( "</table><br><br>\n" );


	//
	// print msg handler times
	//
	p.safePrintf ( 
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr>"
		       "<td colspan=50 bgcolor=#%s>"
		       "<center><b>Message Reply Generation Times</b>"
		       "</td></tr>\n"

		       "<tr>"
		       //"<td>request?</td>\n"
		       "<td><b>niceness</td>\n"
		       "<td><b>msgtype</td>\n"
		       "<td><b>total replies</td>\n"
		       "<td><b>avg gen time</td>\n" ,
		       LIGHT_BLUE,  DARK_BLUE );
	// print bucket headers
	for ( long i = 0 ; i < MAX_BUCKETS ; i++ ) 
		p.safePrintf("<td>%i+</td>\n",(1<<i)-1);
	p.safePrintf("</tr>\n");
	// loop over niceness
	for ( long i3 = 0 ; i3 < 2 ; i3++ ) {
	// print each msg stat
	for ( long i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
		// skip it if has no handler
		if ( i1 != 0x41 && ! g_udpServer.m_handlers[i1] ) continue;
		// print it all out
		long long total = g_stats.m_msgTotalOfHandlerTimes[i1][i3];
		long long nt    = g_stats.m_msgTotalHandlersCalled[i1][i3];
		// skip if no stat
		if ( nt == 0 ) continue;
		long      avg   = 0;
		if ( nt > 0 ) avg = total / nt;
		p.safePrintf( 
			     "<tr>"
			      "<td>%li</td>"    // niceness, 0 or 1
			     "<td>0x%hhx</td>" // msgType
			      //"<td>%li</td>"    // request?
			      "<td>%lli</td>" // total called
			      "<td>%lims</td>" ,// avg handler time in ms
			      i3, // niceness
			      (unsigned char)i1, // msgType
			      //i2, // request?
			      nt ,
			      avg );
		// print buckets
		for ( long i4 = 0 ; i4 < MAX_BUCKETS ; i4++ ) {
			long long count ;
			count = g_stats.m_msgTotalHandlersByTime[i1][i3][i4];
			p.safePrintf("<td>%lli</td>",count);
		}
		p.safePrintf("</tr>\n");
	}
	}
	p.safePrintf ( "</table><br><br>\n" );

	// print out whos is using the most mem
// 	ss = p.getBuf();
// 	ssend = p.getBufEnd();
	g_mem.printMemBreakdownTable (&p,
				      (char *)LIGHT_BLUE , 
				      (char *)DARK_BLUE );
	//p.incrementLength(sss - ss);

	p.safePrintf ( "<br><br>\n" );


	// print db table
	// columns are the dbs
	p.safePrintf (
		  "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		  "<tr>"
		  "<td colspan=50 bgcolor=#%s>"
		  "<center><b>Databases"
		  "</b></td>"
		  "</tr>\n" ,
		  LIGHT_BLUE , DARK_BLUE );

	// make the rdbs
	Rdb *rdbs[] = {
		g_posdb.getRdb(),
		//g_datedb.getRdb(),
		g_titledb.getRdb(),
		//g_revdb.getRdb(),
		g_spiderdb.getRdb(),
		g_doledb.getRdb() ,
		//g_tfndb.getRdb(),
		g_tagdb.getRdb(),
		g_clusterdb.getRdb(),
		//g_catdb.getRdb(),
		g_linkdb.getRdb(),
		g_cachedb.getRdb(),
		g_serpdb.getRdb(),
		g_monitordb.getRdb(),
		g_statsdb.getRdb()
		//g_placedb.getRdb() ,
		//g_sectiondb.getRdb()
	};
	long nr = sizeof(rdbs) / 4;

	// print dbname
	p.safePrintf("<tr><td>&nbsp;</td>");
	for ( long i = 0 ; i < nr ; i++ ) 
		p.safePrintf("<td><b>%s</b></td>",rdbs[i]->m_dbname);
	p.safePrintf("<td><i><b>Total</b></i></tr>\n");

	//long long total ;
	float totalf ;

	// print # big files
	p.safePrintf("<tr><td><b># big files</b>*</td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumFiles();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);


	// print # small files
	p.safePrintf("<tr><td><b># small files</b>*</td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumSmallFiles();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);


	// print disk space used
	p.safePrintf("<tr><td><b>disk space (MB)</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getDiskSpaceUsed()/1000000;
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);


	// print # recs total
	p.safePrintf("<tr><td><b># recs</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumTotalRecs();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);


	// print # recs in mem
	p.safePrintf("<tr><td><b># recs in mem</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumUsedNodes();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);

	// print # negative recs in mem
	p.safePrintf("<tr><td><b># negative in mem</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumNegativeKeys();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);

	// print mem occupied
	p.safePrintf("<tr><td><b>mem occupied</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getTreeMemOccupied();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);

	// print mem allocated
	p.safePrintf("<tr><td><b>mem allocated</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getTreeMemAlloced();
		total += val;
		printNumAbbr ( p , val );
		//p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);

	// print mem max
	p.safePrintf("<tr><td><b>mem max</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getMaxTreeMem();
		total += val;
		printNumAbbr ( p , val );
		//p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);

	// print rdb mem used
	p.safePrintf("<tr><td><b>rdb mem used</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getRdbMem()->getUsedMem();
		total += val;
		//p.safePrintf("<td>%llu</td>",val);
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);

	// print rdb mem avail
	p.safePrintf("<tr><td><b><nobr>rdb mem available</nobr></b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getRdbMem()->getAvailMem();
		total += val;
		//p.safePrintf("<td>%llu</td>",val);
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);


	// print map mem
	p.safePrintf("<tr><td><b>map mem</b></td>");
	total = 0LL;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getMapMemAlloced();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%lli</td></tr>\n",total);

	/*
	// print rec cache hits %
	p.safePrintf("<tr><td><b>rec cache hits %%</b></td>");
	totalf = 0.0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long hits   = rdbs[i]->m_cache.getNumHits();
		long long misses = rdbs[i]->m_cache.getNumHits();
		long long sum    = hits + misses;
		float val = 0.0;
		if ( sum > 0.0 ) val = ((float)hits * 100.0) / (float)sum;
		totalf += val;
		p.safePrintf("<td>%.1f</td>",val);
	}
	p.safePrintf("<td>%.1f</td></tr>\n",totalf);


	// print rec cache hits
	p.safePrintf("<tr><td><b>rec cache hits</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->m_cache.getNumHits();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	// print rec cache misses
	p.safePrintf("<tr><td><b>rec cache misses</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->m_cache.getNumMisses();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);

	// print rec cache tries
	p.safePrintf("<tr><td><b>rec cache tries</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long hits   = rdbs[i]->m_cache.getNumHits();
		long long misses = rdbs[i]->m_cache.getNumHits();
		long long val    = hits + misses;
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);

	p.safePrintf("<tr><td><b>rec cache used slots</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->m_cache.getNumUsedNodes();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b>rec cache max slots</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->m_cache.getNumTotalNodes();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b>rec cache used bytes</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->m_cache.getMemOccupied();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b>rec cache max bytes</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->m_cache.getMaxMem();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);
	*/


	p.safePrintf("<tr><td><b>page cache hits %%</b></td>");
	totalf = 0.0;
	for ( long i = 0 ; i < nr ; i++ ) {
		if ( ! rdbs[i]->m_pc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		long long hits   = rdbs[i]->m_pc->getNumHits();
		long long misses = rdbs[i]->m_pc->getNumMisses();
		long long sum    = hits + misses;
		float val = 0.0;
		if ( sum > 0.0 ) val = ((float)hits * 100.0) / (float)sum;
		totalf += val;
		p.safePrintf("<td>%.1f</td>",val);
	}
	p.safePrintf("<td>%.1f</td></tr>\n",totalf);





	p.safePrintf("<tr><td><b>page cache hits</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		if ( ! rdbs[i]->m_pc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		long long val = rdbs[i]->m_pc->getNumHits();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b>page cache misses</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		if ( ! rdbs[i]->m_pc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		long long val = rdbs[i]->m_pc->getNumMisses();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b>page cache tries</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		if ( ! rdbs[i]->m_pc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		long long hits   = rdbs[i]->m_pc->getNumHits();
		long long misses = rdbs[i]->m_pc->getNumMisses();
		long long val    = hits + misses;
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b>page cache used</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		if ( ! rdbs[i]->m_pc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		long long val = rdbs[i]->m_pc->getMemUsed();
		total += val;
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b><nobr>page cache allocated</nobr></b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		if ( ! rdbs[i]->m_pc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		long long val = rdbs[i]->m_pc->getMemAlloced();
		total += val;
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);




	p.safePrintf("<tr><td><b># disk seeks</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumSeeks();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># disk re-seeks</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumReSeeks();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># bytes read</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumRead();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># get requests read</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumRequestsGet();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># get requests bytes</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNetReadGet();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># get replies sent</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumRepliesGet();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># get reply bytes</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNetSentGet();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);



	p.safePrintf("<tr><td><b># add requests read</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumRequestsAdd();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># add requests bytes</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNetReadAdd();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);

	p.safePrintf("<tr><td><b># add replies sent</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNumRepliesAdd();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);


	p.safePrintf("<tr><td><b># add reply bytes</b></td>");
	total = 0;
	for ( long i = 0 ; i < nr ; i++ ) {
		long long val = rdbs[i]->getNetSentAdd();
		total += val;
		p.safePrintf("<td>%llu</td>",val);
	}
	p.safePrintf("<td>%llu</td></tr>\n",total);






	// end the table now
	p.safePrintf ( "</table>\n" );
	// end the table now

	p.safePrintf (
		  "</center>"
		  "<br><i>*: # of files from all collection sub dirs.</i>\n");
	p.safePrintf (
		  "</center>"
		  "<br><i>Note: # recs for spiderdb or titledb may be lower "
		  "than the actual count because of unassociated negative "
		  "recs</i>\n");
	p.safePrintf (
		  "<br><i>Note: # recs for titledb may be higher "
		  "than the actual count because of duplicate positives "
		  "recs</i>\n");
	p.safePrintf (
		  "<br><i>Note: requests/replies does not include packet "
		  "header and ACK overhead</i>\n");
	p.safePrintf (
		  "<br><i>Note: twins may differ in rec counts but still have "
		  "the same data because they dump at different times which "
		  "leads to different reactions. To see if truly equal, "
		  "do a 'gb ddump' then when that finishes, a, 'gb imerge'"
		  "and a 'gb tmerge'\n");

	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );

	// calculate buffer length
	//long bufLen = p - buf;
	long bufLen = p.length();
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	//return g_httpServer.sendDynamicPage ( s , buf , bufLen );
	return g_httpServer.sendDynamicPage ( s, p.getBufStart(), bufLen );
}
