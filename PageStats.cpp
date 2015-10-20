#include "gb-include.h"

#include "Indexdb.h"     // makeKey(int64_t docId)
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
#include "Msg13.h"
#include "Msg3.h"

bool printNumAbbr ( SafeBuf &p, int64_t vvv ) {
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
	int32_t uptime = time(NULL) - g_stats.m_uptimeStart ;
	// sanity check... wtf?
	if ( uptime < 0 ) { uptime = 0; };

	int32_t days  = uptime / 86400; uptime -= days  * 86400;
	int32_t hours = uptime /  3600; uptime -= hours * 3600;
	int32_t mins  = uptime /    60; uptime -= mins  * 60;
	int32_t secs  = uptime;

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
		sb.safePrintf("%"INT32" %s ",days,ds);

	if ( hours >= 1 )
		sb.safePrintf("%"INT32" %s ", hours,hs);

	if ( mins >= 1 )
		sb.safePrintf("%"INT32" %s ", mins,ms);

	if ( secs != 0 ) 
		sb.safePrintf(" %"INT32" %s",secs,ss);
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
	//int32_t pwdLen = 0;
	//char *pwd = r->getString ( "pwd" , &pwdLen );
	//if ( pwdLen > 31 ) pwdLen = 31;
	//char pbuf [32];
	//if ( pwdLen > 0 ) strncpy ( pbuf , pwd , pwdLen );
	//pbuf[pwdLen]='\0';

	char format = r->getReplyFormat();

	// print standard header
	// 	char *ss = p.getBuf();
	// 	char *ssend = p.getBufEnd();
	if ( format == FORMAT_HTML ) g_pages.printAdminTop ( &p , s , r );
	//      p.incrementLength(sss - ss);

	struct rusage ru;
	if ( getrusage ( RUSAGE_SELF , &ru ) )
		log("admin: getrusage: %s.",mstrerror(errno));

	if ( format == FORMAT_HTML )
		p.safePrintf(
			     "<style>"
			     ".poo { background-color:#%s;}\n"
			     "</style>\n" ,
			     LIGHT_BLUE );

	// memory in general table
	if ( format == FORMAT_HTML ) {
		p.safePrintf (
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=2>"
			      "<center><b>Memory</b></td></tr>\n"
			      "<tr class=poo><td><b>memory allocated</b>"
			      "</td><td>%"INT64"</td></tr>\n"
			      "<tr class=poo><td><b>max memory limit</b>"
			      "</td><td>%"INT64"</td></tr>\n" 
			      //"<tr class=poo><td>mem available</td>"
			      //"<td>%"INT64"</td></tr>\n"
			      "<tr class=poo><td>max allocated</td>"
			      "<td>%"INT64"</td></tr>\n",
			      TABLE_STYLE ,
			      g_mem.getUsedMem() ,
			      g_mem.getMaxMem() ,
			      //g_mem.getAvailMem(),
			      g_mem.getMaxAlloced()
			      );
		p.safePrintf (
			      "<tr class=poo><td>max single alloc</td>"
			      "<td>%"INT64"</td></tr>\n"
			      "<tr class=poo><td>max single alloc by</td>"
			      "<td>%s</td></tr>\n" 

			      // "<tr class=poo><td>shared mem used</td>"
			      // "<td>%"INT64"</td></tr>\n"

			      "<tr class=poo><td># out of memory errors</td>"
			      "<td>%"INT32"</td></tr>\n"

			      "<tr class=poo><td>swaps</td>"
			      "<td>%"INT64"</td></tr>\n"

			      // "<tr class=poo><td>"
			      // "collections swapped out"
			      // "</td>"
			      // "<td>%"INT32"</td></tr>\n" 

			      ,
			      //"<tr class=poo><td>num alloc chunks</td>
			      //<td>%"INT32"</td></tr>\n",
			      g_mem.getMaxAlloc(),
			      g_mem.getMaxAllocBy() ,

			      //g_mem.m_sharedUsed,
			      g_mem.m_outOfMems,

			      (int64_t)ru.ru_nswap// idrss,
			      //g_collectiondb.m_numCollsSwappedOut
			      ); 
		p.safePrintf (
			      "<tr class=poo><td><b>current allocations</b>"
			      "</td>"
			      "<td>%"INT32"</td></tr>\n" 


			      "<tr class=poo><td><b>max allocations</b>"
			      "</td>"
			      "<td>%"INT32"</td></tr>\n" 


			      "<tr class=poo><td><b>total allocations</b></td>"
			      "<td>%"INT64"</td></tr>\n" ,
			      g_mem.getNumAllocated() ,
			      g_mem.m_memtablesize ,
			      (int64_t)g_mem.getNumTotalAllocated() );

	}



	if ( format == FORMAT_XML ) 
		p.safePrintf("<response>\n"
			     "\t<statusCode>0</statusCode>\n"
			     "\t<statusMsg>Success</statusMsg>\n");

	if ( format == FORMAT_JSON ) 
		p.safePrintf("{\"response\":{\n"
			     "\t\"statusCode\":0,\n"
			     "\t\"statusMsg\":\"Success\",\n");


	if ( format == FORMAT_XML ) 
		p.safePrintf ("\t<memoryStats>\n"
			      "\t\t<allocated>%"INT64"</allocated>\n"
			      "\t\t<max>%"INT64"</max>\n" 
			      "\t\t<maxAllocated>%"INT64"</maxAllocated>\n"
			      "\t\t<maxSingleAlloc>%"INT64"</maxSingleAlloc>\n"
			      "\t\t<maxSingleAllocBy>%s</maxSingleAllocBy>\n"
			      "\t\t<currentAllocations>%"INT32""
			      "</currentAllocations>\n"
			      "\t\t<totalAllocations>%"INT64"</totalAllocations>\n"
			      "\t</memoryStats>\n"
			      , g_mem.getUsedMem()
			      , g_mem.getMaxMem() 
			      , g_mem.getMaxAlloced() 
			      , g_mem.getMaxAlloc()
			      , g_mem.getMaxAllocBy() 
			      , g_mem.getNumAllocated() 
			      , (int64_t)g_mem.getNumTotalAllocated() );

	if ( format == FORMAT_JSON ) 
		p.safePrintf ("\t\"memoryStats\":{\n"
			      "\t\t\"allocated\":%"INT64",\n"
			      "\t\t\"max\":%"INT64",\n" 
			      "\t\t\"maxAllocated\":%"INT64",\n"
			      "\t\t\"maxSingleAlloc\":%"INT64",\n"
			      "\t\t\"maxSingleAllocBy\":\"%s\",\n"
			      "\t\t\"currentAllocations\":%"INT32",\n"
			      "\t\t\"totalAllocations\":%"INT64"\n"
			      "\t},\n"
			      , g_mem.getUsedMem()
			      , g_mem.getMaxMem() 
			      , g_mem.getMaxAlloced() 
			      , g_mem.getMaxAlloc()
			      , g_mem.getMaxAllocBy() 
			      , g_mem.getNumAllocated() 
			      , (int64_t)g_mem.getNumTotalAllocated() );


			     

	// end table
	if ( format == FORMAT_HTML ) p.safePrintf ( "</table><br>" );

	//Query performance stats
	g_stats.calcQueryStats();
	int32_t days;
	int32_t hours;
	int32_t minutes;
	int32_t secs;
	int32_t msecs;
	getCalendarFromMs(g_stats.m_upTime,
			  &days, 
			  &hours, 
			  &minutes, 
			  &secs,
			  &msecs);

	// int64_t avgTier0Time = 0;
	// int64_t avgTier1Time = 0;
	// int64_t avgTier2Time = 0;
	// if ( g_stats.m_tierHits[0] > 0 )
	// 	avgTier0Time = g_stats.m_tierTimes[0] /
	// 		(int64_t)g_stats.m_tierHits[0];
	// if ( g_stats.m_tierHits[1] > 0 )
	// 	avgTier1Time = g_stats.m_tierTimes[1] /
	// 		(int64_t)g_stats.m_tierHits[1];
	// if ( g_stats.m_tierHits[2] > 0 )
	// 	avgTier2Time = g_stats.m_tierTimes[2] /
	// 		(int64_t)g_stats.m_tierHits[2];

	if ( format == FORMAT_HTML )
		p.safePrintf ( 
			      "<br>"
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=2>"
			      "<center><b>Queries</b></td></tr>\n"

			      "<tr class=poo><td><b>"
			      "Average Query Latency for last %"INT32" queries"
			      ,TABLE_STYLE
			      ,g_stats.m_numQueries
			       );


	if ( format == FORMAT_XML ) {
		p.safePrintf("\t<queryStats>\n"
			     "\t\t<sample>\n"
			     "\t\t\t<size>last %"INT32" queries</size>\n"
			     ,g_stats.m_numQueries
			     );

		if ( g_stats.m_numQueries == 0 )
		p.safePrintf("\t\t\t<averageLatency></averageLatency>\n"
			     );
		else
		p.safePrintf("\t\t\t<averageLatency>%.1f ms</averageLatency>\n"
			     ,g_stats.m_avgQueryTime
			     );


		p.safePrintf("\t\t\t<queriesPerSecond>%f"
			     "</queriesPerSecond>\n"
			     "\t\t</sample>\n"
			     ,g_stats.m_avgQueriesPerSec
			     );

		p.safePrintf(
			     "\t\t<total>\n"
			     "\t\t\t<numQueries>%"INT32"</numQueries>\n"
			     "\t\t\t<numSuccesses>%"INT32"</numSuccesses>\n"
			     "\t\t\t<numFailures>%"INT32"</numFailures>\n"
			     // total
			     , g_stats.m_totalNumQueries +
			       g_stats.m_numSuccess + 
			       g_stats.m_numFails
			     // total successes
			     ,g_stats.m_totalNumSuccess + g_stats.m_numSuccess
			     // total failures
			     ,g_stats.m_totalNumFails   + g_stats.m_numFails
			     );

		if ( g_stats.m_totalNumSuccess + g_stats.m_totalNumFails )
			p.safePrintf("\t\t\t<successRate>%.01f%%"
				     "</successRate>\n"
				     // total success rate
				     , (float)g_stats.m_totalNumSuccess / 
				       (float)(g_stats.m_totalNumSuccess + 
					     g_stats.m_totalNumFails)
				     );
		else
			p.safePrintf("\t\t\t<successRate>"
				     "</successRate>\n");

		p.safePrintf("\t\t</total>\n"
			     "\t</queryStats>\n"

			     "\t<socketsClosedFromOverload>%"INT32""
			     "</socketsClosedFromOverload>\n"

			     //days, hours, minutes, secs,
			     , g_stats.m_closedSockets );
	}


	if ( format == FORMAT_JSON ) {
		p.safePrintf("\t\"queryStats\":{\n"
			     "\t\t\"sample\":{\n"
			     "\t\t\t\"size\":\"last %"INT32" queries\",\n"
			     ,g_stats.m_numQueries
			     );

		if ( g_stats.m_numQueries == 0 )
		p.safePrintf("\t\t\t\"averageLatency\":\"\",\n"
			     );
		else
		p.safePrintf("\t\t\t\"averageLatency\":\"%.1f ms\",\n"
			     ,g_stats.m_avgQueryTime
			     );


		p.safePrintf("\t\t\t\"queriesPerSecond\":%f\n"
			     "\t\t}\n," // sample
			     ,g_stats.m_avgQueriesPerSec
			     );

		p.safePrintf(
			     "\t\t\"total\":{\n"
			     "\t\t\t\"numQueries\":%"INT32",\n"
			     "\t\t\t\"numSuccesses\":%"INT32",\n"
			     "\t\t\t\"numFailures\":%"INT32",\n"
			     // total
			     , g_stats.m_totalNumQueries +
			       g_stats.m_numSuccess + 
			       g_stats.m_numFails
			     // total successes
			     ,g_stats.m_totalNumSuccess + g_stats.m_numSuccess
			     // total failures
			     ,g_stats.m_totalNumFails   + g_stats.m_numFails
			     );

		if ( g_stats.m_totalNumSuccess + g_stats.m_totalNumFails )
			p.safePrintf("\t\t\t\"successRate\":\"%.01f%%\"\n"
				     // total success rate
				     , (float)g_stats.m_totalNumSuccess / 
				       (float)(g_stats.m_totalNumSuccess + 
					     g_stats.m_totalNumFails)
				     );
		else
			p.safePrintf("\t\t\t\"successRate\":\"\"\n");

		p.safePrintf("\t\t}\n" // total
			     "\t},\n" // querystats

			     "\t\"socketsClosedFromOverload\":%"INT32",\n"
			     //days, hours, minutes, secs,
			     , g_stats.m_closedSockets );
	}


	if ( format == FORMAT_HTML ) {
		if ( g_stats.m_numQueries == 0 )
			p.safePrintf("</b></td><td></td></tr>\n");
		else
			p.safePrintf("</b></td><td>%f seconds</td></tr>\n"
				     ,g_stats.m_avgQueryTime);
	}
			     
	if ( format == FORMAT_HTML )
		p.safePrintf(

			     "<tr class=poo><td><b>Average queries/sec. for "
			     "last %"INT32" queries"
			     "</b></td><td>%f queries/sec.</td></tr>\n"

			     "<tr class=poo><td><b>Query Success Rate "
			     "for last "
			     "%"INT32" queries"
			     "</b></td><td>%f"

			     "<tr class=poo><td><b>Total Queries "
			     "Served</b></td>"
			     "<td>%"INT32""
			     "<tr class=poo><td><b>Total Successful "
			     "Queries</b></td>"
			     "<td>%"INT32""
			     "<tr class=poo><td><b>Total Failed "
			     "Queries</b></td>"
			     "<td>%"INT32""
			     "<tr class=poo><td><b>Total Query "
			     "Success Rate</b></td>"
			     "<td>%f"
			     "</td></tr>"
			     "<tr class=poo><td><b>Uptime"
			     "</b></td><td>%"INT32" days %"INT32" "
			     "hrs %"INT32" min %"INT32" sec"
			     "</td></tr>"
			     "<tr class=poo><td><b>"
			     "Sockets Closed Because We Hit "
			     "the Limit"
			     "</b></td><td>%"INT32""
			     "</td></tr>",

			     //g_stats.m_avgQueryTime, 
	
			     g_stats.m_numQueries,
			     g_stats.m_avgQueriesPerSec, 

			     g_stats.m_numQueries,
			     g_stats.m_successRate,
			     g_stats.m_totalNumQueries + g_stats.m_numSuccess+ 
			     g_stats.m_numFails,
			     g_stats.m_totalNumSuccess + g_stats.m_numSuccess ,
			     g_stats.m_totalNumFails   + g_stats.m_numFails   ,
			     (float)g_stats.m_totalNumSuccess / 
			     (float)(g_stats.m_totalNumSuccess + 
				     g_stats.m_totalNumFails),
			     days, hours, minutes, secs,
			     g_stats.m_closedSockets );

	int64_t total = 0;
	for ( int32_t i = 0 ; i <= CR_OK ; i++ )
		total += g_stats.m_filterStats[i];


	if ( format == FORMAT_HTML )
		p.safePrintf ( "<tr class=poo><td><b>Total DocIds Generated"
			       "</b></td><td>%"INT64""
			       "</td></tr>\n" , total );


	if ( format == FORMAT_XML )
		p.safePrintf ( "\t<totalDocIdsGenerated>%"INT64""
			       "</totalDocIdsGenerated>\n" , total );

	if ( format == FORMAT_JSON )
		p.safePrintf ( "\t\"totalDocIdsGenerated\":%"INT64",\n",total);

	// print each filter stat
	for ( int32_t i = 0 ; i < CR_END ; i++ ) {
		if ( format == FORMAT_HTML )
			p.safePrintf("<tr class=poo><td>&nbsp;&nbsp;%s</td>"
				     "<td>%"INT32"</td></tr>\n" , 
				     g_crStrings[i],g_stats.m_filterStats[i] );
		if ( format == FORMAT_XML )
			p.safePrintf("\t<queryStat>\n"
				     "\t\t<status><![CDATA[%s]]>"
				     "</status>\n"
				     "\t\t<count>%"INT32"</count>\n"
				     "\t</queryStat>\n"
				     ,g_crStrings[i],g_stats.m_filterStats[i]);
		if ( format == FORMAT_JSON )
			p.safePrintf("\t\"queryStat\":{\n"
				     "\t\t\"status\":\"%s\",\n"
				     "\t\t\"count\":%"INT32"\n"
				     "\t},\n"
				     ,g_crStrings[i],g_stats.m_filterStats[i]);
	}

	// unless we bring indexdb back, don't need these
	/*
	p.safePrintf(
		     "<tr class=poo><td><b>Tier 0 Hits</b></td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td><b>Tier 1 Hits</b></td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td><b>Tier 2 Hits</b></td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td><b>Tier 2 Exhausted</b></td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td><b>Avg Tier 0 Time</b></td><td>%"INT64"ms</td></tr>"
		     "<tr class=poo><td><b>Avg Tier 1 Time</b></td><td>%"INT64"ms</td></tr>"
		     "<tr class=poo><td><b>Avg Tier 2 Time</b></td><td>%"INT64"ms</td></tr>",
		     g_stats.m_tierHits[0], g_stats.m_tierHits[1],
		     g_stats.m_tierHits[2], g_stats.m_tier2Misses,
		     avgTier0Time, avgTier1Time, avgTier2Time,
);
	*/

	/*
	p.safePrintf(
		     "<tr class=poo><td><b>Msg3a Slow Recalls</b></td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td><b>Msg3a Quick Recalls</b></td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td><b>Msg3a Msg40 Recalls</b></td><td>%"INT32"</td></tr>"

		     "<tr class=poo><td><b>Unjustified iCache Misses</b></td>"
		     "<td>%"INT32"</td></tr>"

		     "<tr class=poo><td>&nbsp;&nbsp;&nbsp;2</td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td>&nbsp;&nbsp;&nbsp;3</td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td>&nbsp;&nbsp;&nbsp;4</td><td>%"INT32"</td></tr>"
		     "<tr class=poo><td>&nbsp;&nbsp;&nbsp;5+</td><td>%"INT32"</td></tr>"
		     g_stats.m_msg3aSlowRecalls,
		     g_stats.m_msg3aFastRecalls,
		     g_stats.m_msg3aRecallCnt,

		     // unjustified icache misses
		     g_stats.m_recomputeCacheMissess-g_stats.m_icacheTierJumps,

		     g_stats.m_msg3aRecalls[2], g_stats.m_msg3aRecalls[3],
		     g_stats.m_msg3aRecalls[4], g_stats.m_msg3aRecalls[5]);
	*/

	if ( format == FORMAT_HTML ) p.safePrintf("</table><br><br>\n");

	// stripe loads
	if ( g_hostdb.m_myHost->m_isProxy ) {
		p.safePrintf ( 
			      "<br>"
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=4>"
			      "<center><b>Stripe Loads</b></td></tr>\n" ,
			      TABLE_STYLE  );
		p.safePrintf("<tr class=poo><td><b>Stripe #</b></td>"
			     "<td><b>Queries Out</b></td>"
			     "<td><b>Query Terms Out</b></td>"
			     "<td><b>Last HostId Used</b></td>"
			     "</tr>\n" );
		// print out load of each stripe
		int32_t numStripes = g_hostdb.getNumStripes();
		for ( int32_t i = 0 ; i < numStripes ; i++ )
			p.safePrintf("<tr class=poo><td>%"INT32"</td>"
				     "<td>%"INT32"</td>"
				     "<td>%"INT32"</td>"
				     "<td>%"INT32"</td></tr>\n" ,
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
	caches[5] = &g_spiderLoop.m_winnerListCache;
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
	int32_t numCaches = 6;

	if ( format == FORMAT_HTML )
		p.safePrintf (
		  "<table %s>"
		  "<tr class=hdrow>"
		  "<td colspan=%"INT32">"
		  "<center><b>Caches"
		  "</b></td></tr>\n",
		  TABLE_STYLE,
		  numCaches+2 );

	// hack since some are not init'd yet
	//g_qtable.m_dbname = "quota";
	//g_deadWaitCache.m_dbname = "deadwait";
	//g_alreadyAddedCache.m_dbname = "alreadyAdded";
	//g_forcedCache.m_dbname = "forced";
	//g_msg20Cache.m_dbname = "parser";
	//g_tagdb.m_listCache.m_dbname = "tagdbList";

	if ( format == FORMAT_HTML )
		// 1st column is empty
		p.safePrintf ("<tr class=poo><td>&nbsp;</td>");  

	for ( int32_t i = 0 ; format == FORMAT_XML && i < numCaches ; i++ ) {
		p.safePrintf("\t<cacheStats>\n");
		p.safePrintf("\t\t<name>%s</name>\n",caches[i]->getDbname());
		int64_t a = caches[i]->getNumHits();
		int64_t b = caches[i]->getNumMisses();
		double r = 100.0 * (double)a / (double)(a+b);
		p.safePrintf("\t\t<hitRatio>");
		if ( a+b > 0.0 ) p.safePrintf("%.1f%%",r);
		p.safePrintf("</hitRatio>\n");
		p.safePrintf("\t\t<numHits>%"INT64"</numHits>\n",a);
		p.safePrintf("\t\t<numMisses>%"INT64"</numMisses>\n",b);
		p.safePrintf("\t\t<numTries>%"INT64"</numTries>\n",a+b);

		p.safePrintf("\t\t<numUsedSlots>%"INT32"</numUsedSlots>\n",
			     caches[i]->getNumUsedNodes());
		p.safePrintf("\t\t<numTotalSlots>%"INT32"</numTotalSlots>\n",
			     caches[i]->getNumTotalNodes());
		p.safePrintf("\t\t<bytesUsed>%"INT32"</bytesUsed>\n",
			     caches[i]->getMemOccupied());
		p.safePrintf("\t\t<maxBytes>%"INT32"</maxBytes>\n",
			     caches[i]->getMaxMem());
		p.safePrintf("\t\t<saveToDisk>%"INT32"</saveToDisk>\n",
			     (int32_t)caches[i]->useDisk());
		p.safePrintf("\t</cacheStats>\n");
	}

	for ( int32_t i = 0 ; format == FORMAT_JSON && i < numCaches ; i++ ) {
		p.safePrintf("\t\"cacheStats\":{\n");
		p.safePrintf("\t\t\"name\":\"%s\",\n",caches[i]->getDbname());
		int64_t a = caches[i]->getNumHits();
		int64_t b = caches[i]->getNumMisses();
		double r = 100.0 * (double)a / (double)(a+b);
		p.safePrintf("\t\t\"hitRatio\":\"");
		if ( a+b > 0.0 ) p.safePrintf("%.1f%%",r);
		p.safePrintf("\",\n");
		p.safePrintf("\t\t\"numHits\":%"INT64",\n",a);
		p.safePrintf("\t\t\"numMisses\":%"INT64",\n",b);
		p.safePrintf("\t\t\"numTries\":%"INT64",\n",a+b);

		p.safePrintf("\t\t\"numUsedSlots\":%"INT32",\n",
			     caches[i]->getNumUsedNodes());
		p.safePrintf("\t\t\"numTotalSlots\":%"INT32",\n",
			     caches[i]->getNumTotalNodes());
		p.safePrintf("\t\t\"bytesUsed\":%"INT32",\n",
			     caches[i]->getMemOccupied());
		p.safePrintf("\t\t\"maxBytes\":%"INT32",\n",
			     caches[i]->getMaxMem());
		p.safePrintf("\t\t\"saveToDisk\":%"INT32"\n",
			     (int32_t)caches[i]->useDisk());
		p.safePrintf("\t},\n");
	}


	// do not print any more if xml or json
	if ( format == FORMAT_XML || format == FORMAT_JSON )
		goto skip1;

	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		p.safePrintf("<td><b>%s</b></td>",caches[i]->getDbname() );
	}
	//p.safePrintf ("<td><b><i>Total</i></b></td></tr>\n" );

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>hit ratio</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->getNumHits();
		int64_t b = caches[i]->getNumMisses();
		double r = 100.0 * (double)a / (double)(a+b);
		if ( a+b > 0.0 ) 
			p.safePrintf("<td>%.1f%%</td>",r);
		else
			p.safePrintf("<td>--</td>");
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>hits</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->getNumHits();
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>tries</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->getNumHits();
		int64_t b = caches[i]->getNumMisses();
		p.safePrintf("<td>%"INT64"</td>",a+b);
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>used slots</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->getNumUsedNodes();
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>max slots</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->getNumTotalNodes();
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>used bytes</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->getMemOccupied();
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>max bytes</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->getMaxMem();
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>dropped recs</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->m_deletes;
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>added recs</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->m_adds;
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	//p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>max age</td>" );
	//for ( int32_t i = 0 ; i < numCaches ; i++ ) {
	//	int64_t a = caches[i]->getMaxMem();
	//	p.safePrintf("<td>%"INT64"</td>",a);
	//}

	p.safePrintf ("</tr>\n<tr class=poo><td><b><nobr>save to disk</td>" );
	for ( int32_t i = 0 ; i < numCaches ; i++ ) {
		int64_t a = caches[i]->useDisk();
		p.safePrintf("<td>%"INT64"</td>",a);
	}

	// end the table now
	p.safePrintf ( "</tr>\n</table><br><br>" );



	//
	// Query Term Tiers Tables
	//
	/*
	p.safePrintf ("<tr class=poo><td><b>Query Terms</b></td>" );
	for ( int32_t i = 0; i < MAX_TIERS; i++ )
		p.safePrintf ( "<td><b>Tier #%"INT32"</b></td>",i );
	p.safePrintf ( "</tr><tr class=poo>");
	for ( int32_t i = 0; i < 14; i++ ){
		p.safePrintf ( "<td>&nbsp;&nbsp;&nbsp;%"INT32"</td>", i+1 );
		for ( int32_t j = 0; j < MAX_TIERS; j++ )
			p.safePrintf ( "<td>%"INT32"</td>",
				       g_stats.m_numTermsVsTier[i][j] );
		p.safePrintf ( "</tr>" );
	}
	p.safePrintf("</table><br>\n");
	
	p.safePrintf ( "<br>"
		       "<table cellpadding=4 width=100%% bgcolor=#%s border=1>"
		       "<tr class=poo><td colspan=8 bgcolor=#%s>"
		       "<center><b>Query Terms and Tier Vs Explicit "
		       "Matches</b></td></tr>\n"
		       "<tr class=poo><td width=\"40%%\"><b>Query Terms and Tier</b></td>"
		       "<td><b> 0+</b></td><td><b> 8+</b></td>"
		       "<td><b> 16+</b></td><td><b> 32+</b></td>"
		       "<td><b> 64+</b></td><td><b> 128+</b></td>"
		       "<td><b> 256+</b></td></tr>\n",
		       LIGHT_BLUE , DARK_BLUE );
	for ( int32_t i = 0; i < 14; i++ )
		for ( int32_t j = 0; j < MAX_TIERS; j++ ){
			p.safePrintf( "<tr class=poo><td>query terms=%"INT32", tier=%"INT32"</td>",
				      i+1, j );
			for ( int32_t k = 0; k < 7; k++ ){
				int32_t n = g_stats.m_termsVsTierExp[i][j][k];
				p.safePrintf( "<td>%"INT32"</td>",n );
			}
			p.safePrintf( "</tr>" );
		}
	
	p.safePrintf("</table><br><br>\n");
	*/

 skip1:

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

	// replace \n in nowstr with space
	char *nn = strstr(nowStr,"\n");
	if ( nn ) *nn = ' ';


	//
	// get the uptime as a string
	//
	SafeBuf ubuf;
	printUptime ( ubuf );

	int arch = 32;
	if ( __WORDSIZE == 64 ) arch = 64;
	if ( __WORDSIZE == 128 ) arch = 128;

	if ( format == FORMAT_HTML )
		p.safePrintf (
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=2>"
			      "<center><b>General Info</b></td></tr>\n"
			      "<tr class=poo><td><b>Uptime</b></td><td>%s</td></tr>\n"
			      "<tr class=poo><td><b>Process ID</b></td><td>%"UINT32"</td></tr>\n"
			      "<tr class=poo><td><b>Corrupted Disk Reads</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>SIGALRMS</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>SIGVTALRMS</b></td><td>%"INT32"</td></tr>\n"

			      "<tr class=poo><td><b>SIGCHLDS</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>SIGQUEUES</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>SIGPIPES</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>SIGIOS</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>SIGOTHERS</b></td><td>%"INT32"</td></tr>\n"

			      //"<tr class=poo><td><b>read signals</b></td><td>%"INT64"</td></tr>\n"
			      //"<tr class=poo><td><b>write signals</b></td><td>%"INT64"</td></tr>\n"
			      "<tr class=poo><td><b>quickpolls</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>Kernel Version</b></td><td>%s</td></tr>\n"
			      "<tr class=poo><td><b>Gigablast Architecture</b></td><td>%i bit</td></tr>\n"
			      
			      //"<tr class=poo><td><b>Gigablast Version</b></td><td>%s %s</td></tr>\n"
			      "<tr class=poo><td><b>Parsing Inconsistencies</b></td><td>%"INT32"</td>\n"

			      // overflows. when we have too many unindexed 
			      // spiderrequests for a particular firstip, we 
			      // start dropping so we don't spam spiderdb
			      "<tr class=poo><td><b>Dropped Spider Requests</b></td><td>%"INT32"</td>\n"

			      "<tr class=poo><td><b>Index Shards</b></td><td>%"INT32"</td>\n"
			      "<tr class=poo><td><b>Hosts per Shard</b></td><td>%"INT32"</td>\n"
			      //"<tr class=poo><td><b>Fully Split</b></td><td>%"INT32"</td>\n"
			      //"<tr class=poo><td><b>Tfndb Extension Bits</b></td><td>%"INT32"</td>\n"
			      "</tr>\n"
			      "<tr class=poo><td><b>Spider Locks</b></td><td>%"INT32"</td></tr>\n"
			      "<tr class=poo><td><b>Local Time</b></td><td>%s (%"INT32")</td></tr>\n"
			      ,
			      TABLE_STYLE ,
			      ubuf.getBufStart(),
			      (uint32_t)getpid(),
			      g_numCorrupt,
			      g_numAlarms,
			      g_numVTAlarms,

			      g_numSigChlds,
			      g_numSigQueues,
			      g_numSigPipes,
			      g_numSigIOs,
			      g_numSigOthers,

			      //g_stats.m_readSignals,
			      //g_stats.m_writeSignals,
			      g_numQuickPolls,
			      kv , 
			      arch,
			      //GBPROJECTNAME,
			      //GBVersion ,
			      g_stats.m_parsingInconsistencies ,
			      g_stats.m_totalOverflows,
			      (int32_t)g_hostdb.getNumShards(),//g_hostdb.m_indexSplits,
			      (int32_t)g_hostdb.getNumHostsPerShard(),
			      g_spiderLoop.m_lockTable.m_numSlotsUsed,
			      //(int32_t)g_conf.m_fullSplit,
			      //(int32_t)g_conf.m_tfndbExtBits,
			      nowStr,
			      (int32_t)now);//ctime(&time));

	if ( format == FORMAT_XML ) 
		p.safePrintf (
			      "\t<generalStats>\n"
			      "\t\t<uptime>%s</uptime>\n"

			      "\t\t<corruptedDiskReads>%"INT32""
			      "</corruptedDiskReads>\n"

			      "\t\t<SIGVTALARMS>%"INT32"</SIGVTALARMS>\n"
			      "\t\t<quickpolls>%"INT32"</quickpolls>\n"

			      "\t\t<kernelVersion><![CDATA[%s]]>"
			      "</kernelVersion>\n"

			      "\t\t<gigablastArchitecture><![CDATA[%i bit]]>"
			      "</gigablastArchitecture>\n"

			      "\t\t<parsingInconsistencies>%"INT32""
			      "</parsingInconsistencies>\n"

			      "\t\t<numShards>%"INT32"</numShards>\n"

			      "\t\t<hostsPerShard>%"INT32"</hostsPerShard>\n"

			      "\t\t<spiderLocks>%"INT32"</spiderLocks>\n"

			      "\t\t<localTimeStr>%s</localTimeStr>\n"
			      "\t\t<localTime>%"INT32"</localTime>\n"
			      ,
			      ubuf.getBufStart(),
			      g_numCorrupt,
			      g_numAlarms,
			      g_numQuickPolls,
			      kv , 
			      arch,
			      g_stats.m_parsingInconsistencies ,
			      (int32_t)g_hostdb.getNumShards(),
			      (int32_t)g_hostdb.getNumHostsPerShard(),
			      g_spiderLoop.m_lockTable.m_numSlotsUsed,
			      nowStr,
			      (int32_t)now);

	if ( format == FORMAT_JSON ) {
		p.safePrintf (
			      "\t\"generalStats\":{\n"
			      "\t\t\"uptime\":\"%s\",\n"

			      "\t\t\"corruptedDiskReads\":%"INT32",\n"

			      "\t\t\"SIGVTALARMS\":%"INT32",\n"
			      "\t\t\"quickpolls\":%"INT32",\n"

			      "\t\t\"kernelVersion\":\""
			      ,
			      ubuf.getBufStart(),
			      g_numCorrupt,
			      g_numAlarms,
			      g_numQuickPolls);
		// this has quotes in it
		p.jsonEncode(kv);
		p.safePrintf( "\",\n"

			      "\t\t\"gigablastArchitecture\":\"%i bit\",\n"
			      "\t\t\"parsingInconsistencies\":%"INT32",\n"

			      "\t\t\"numShards\":%"INT32",\n"

			      "\t\t\"hostsPerShard\":%"INT32",\n"

			      "\t\t\"spiderLocks\":%"INT32",\n"

			      "\t\t\"localTimeStr\":\"%s\",\n"
			      "\t\t\"localTime\":%"INT32",\n"
			      ,
			      arch,
			      g_stats.m_parsingInconsistencies ,
			      (int32_t)g_hostdb.getNumShards(),
			      (int32_t)g_hostdb.getNumHostsPerShard(),
			      g_spiderLoop.m_lockTable.m_numSlotsUsed,
			      nowStr,
			      (int32_t)now);
	}



	time_t nowg = 0;

	// end table
        if ( ! isClockInSync() ) {
		sprintf(nowStr,"not in sync with host #0");
	}
	else {
		nowg = getTimeGlobal();
		sprintf ( nowStr , "%s UTC", asctime(gmtime(&nowg)) );
	}

	// replace \n in nowstr with space
	char *sn = strstr(nowStr,"\n");
	if ( sn ) *sn = ' ';


	if ( format == FORMAT_HTML )
		p.safePrintf ( "<tr class=poo><td><b>Global Time</b></td>"
			       "<td>%s (%"INT32")</td></tr>\n" 
			       "</table><br><br>", nowStr,
			       (int32_t)nowg);//ctime(&time))

	if ( format == FORMAT_XML )
		p.safePrintf ( "\t\t<globalTimeStr>%s</globalTimeStr>\n"
			       "\t\t<globalTime>%"INT32"</globalTime>\n"
			       "\t</generalStats>\n"
			       ,nowStr,(int32_t)nowg);

	if ( format == FORMAT_JSON )
		p.safePrintf ( "\t\t\"globalTimeStr\":\"%s\",\n"
			       "\t\t\"globalTime\":%"INT32"\n"
			       "\t},\n"
			       ,nowStr,(int32_t)nowg);


	//
	// print network stats
	//
	if ( format == FORMAT_HTML )
		p.safePrintf ( 
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=2 class=hdrow>"
			      "<center><b>Network</b></td></tr>\n"

			      "<tr class=poo><td><b>http server "
			      "bytes downloaded</b>"
			      "</td><td>%"UINT64"</td></tr>\n" 

			      "<tr class=poo><td><b>http server "
			      "bytes downloaded (uncompressed)</b>"
			      "</td><td>%"UINT64"</td></tr>\n" 

			      "<tr class=poo><td><b>http server "
			      "compression ratio</b>"
			      "</td><td>%.02f</td></tr>\n" 
			      

			      "<tr class=poo><td><b>ip1 bytes/packets in</b>"
			      "</td><td>%"UINT64" / %"UINT64"</td></tr>\n" 

			      "<tr class=poo><td><b>ip1 bytes/packets out</b>"
			      "</td><td>%"UINT64" / %"UINT64"</td></tr>\n" 

			      "<tr class=poo><td><b>ip2 bytes/packets in</b>"
			      "</td><td>%"UINT64" / %"UINT64"</td></tr>\n" 

			      "<tr class=poo><td><b>ip2 bytes/packets out</b>"
			      "</td><td>%"UINT64" / %"UINT64"</td></tr>\n" 

			      "<tr class=poo><td><b>cancel acks sent</b>"
			      "</td><td>%"INT32"</td></tr>\n" 
			      "<tr class=poo><td><b>cancel acks read</b>"
			      "</td><td>%"INT32"</td></tr>\n" 
			      "<tr class=poo><td><b>dropped dgrams</b>"
			      "</td><td>%"INT32"</td></tr>\n" 
			      "<tr class=poo><td><b>corrupt dns reply "
			      "dgrams</b>"
			      "</td><td>%"INT32"</td></tr>\n" 

			      ,
			      TABLE_STYLE,

			      g_httpServer.m_bytesDownloaded,
			      g_httpServer.m_uncompressedBytes,
			      g_httpServer.getCompressionRatio(),

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


	if ( format == FORMAT_XML )
		p.safePrintf ( 
			      "\t<networkStats>\n"

			      "\t\t<httpServerBytesDownloaded>%"UINT64""
			      "</httpServerBytesDownloaded>\n"

			      "\t\t<httpServerBytesDownloadedUncompressed>%"UINT64""
			      "</httpServerBytesDownloadedUncompressed>\n"

			      "\t\t<httpServerCompressionRatio>%.02f"
			      "</httpServerCompressionRatio>\n"

			      "\t\t<ip1BytesIn>%"UINT64"</ip1BytesIn>\n"
			      "\t\t<ip1PacketsIn>%"UINT64"</ip1PacketsIn>\n"

			      "\t\t<ip1BytesOut>%"UINT64"</ip1BytesOut>\n"
			      "\t\t<ip1PacketsOut>%"UINT64"</ip1PacketsOut>\n"

			      "\t\t<ip2BytesIn>%"UINT64"</ip2BytesIn>\n"
			      "\t\t<ip2PacketsIn>%"UINT64"</ip2PacketsIn>\n"

			      "\t\t<ip2BytesOut>%"UINT64"</ip2BytesOut>\n"
			      "\t\t<ip2PacketsOut>%"UINT64"</ip2PacketsOut>\n"

			      "\t\t<cancelAcksSent>%"INT32"</cancelAcksSent>\n"

			      "\t\t<cancelAcksRead>%"INT32"</cancelAcksRead>\n"

			      "\t\t<droppedDgrams>%"INT32"</droppedDgrams>\n"

			      "\t\t<corruptDnsReplyDgrams>%"INT32""
			      "</corruptDnsReplyDgrams>\n"
			      "\t</networkStats>\n"

			      ,

			      g_httpServer.m_bytesDownloaded,
			      g_httpServer.m_uncompressedBytes,
			      g_httpServer.getCompressionRatio(),


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

	if ( format == FORMAT_JSON )
		p.safePrintf ( 
			      "\t\"networkStats\":{\n"


			      "\t\t\"httpServerBytesDownloaded\":%"UINT64",\n"
			      "\t\t\"httpServerBytesDownloadedUncompressed\""
			      ":%"UINT64",\n"
			      "\t\t\"httpServerCompressionRatio\":%.02f,\n"

			      "\t\t\"ip1BytesIn\":%"UINT64",\n"
			      "\t\t\"ip1PacketsIn\":%"UINT64",\n"

			      "\t\t\"ip1BytesOut\":%"UINT64",\n"
			      "\t\t\"ip1PacketsOut\":%"UINT64",\n"

			      "\t\t\"ip2BytesIn\":%"UINT64",\n"
			      "\t\t\"ip2PacketsIn\":%"UINT64",\n"

			      "\t\t\"ip2BytesOut\":%"UINT64",\n"
			      "\t\t\"ip2PacketsOut\":%"UINT64",\n"

			      "\t\t\"cancelAcksSent\":%"INT32",\n"

			      "\t\t\"cancelAcksRead\":%"INT32",\n"

			      "\t\t\"droppedDgrams\":%"INT32",\n"

			      "\t\t\"corruptDnsReplyDgrams\":%"INT32"\n"

			      "\t},\n"

			      ,

			      g_httpServer.m_bytesDownloaded,
			      g_httpServer.m_uncompressedBytes,
			      g_httpServer.getCompressionRatio(),


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
	//for ( int32_t i = 0 ; i < 128 ; i++ ) {
	//	if ( ! g_udpServer.m_droppedNiceness0[i] ) continue;
	//	p.safePrintf("<tr class=poo><td>msg%x niceness 0 dropped</td>"
	//		     "<td>%"INT32"</td></tr>\n",
	//		     i,g_udpServer.m_droppedNiceness0[i]);
	//}
	//for ( int32_t i = 0 ; i < 128 ; i++ ) {
	//	if ( ! g_udpServer.m_droppedNiceness1[i] ) continue;
	//	p.safePrintf("<tr class=poo><td>msg%x dropped</td>"
	//		     "<td>%"INT32"</td></tr>\n",
	//		     i,g_udpServer.m_droppedNiceness1[i]);
	//}


	if ( format == FORMAT_HTML ) 
		p.safePrintf ( "</table><br><br>\n" );


	if ( g_hostdb.m_myHost->m_isProxy ) {

	p.safePrintf ( 
		       "<table %s>"
		       "<tr class=hdrow>"
		       "<td colspan=50>"
		       "<center><b>Spider Compression Proxy Stats</b> "

		       " &nbsp; [<a href=\"/admin/stats?reset=2\">"
		       "reset</a>]</td></tr>\n"

		       "<tr class=poo>"
		       "<td><b>type</b></td>\n"
		       "<td><b>#docs</b></td>\n"
		       "<td><b>bytesIn / bytesOut (ratio)</b></td>\n"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td>%s</td>"
		       "<td>%"UINT64"</td>"
		       "<td>%"UINT64" / %"UINT64" (%.02f)</td>"
		       "</tr>"
		       ,
		       TABLE_STYLE,

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
	if ( format == FORMAT_HTML ) {
		p.safePrintf ( 
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=50>"
			      "<center><b>Message Stats</b> "

			      " &nbsp; [<a href=\"/admin/stats?reset=1\">"
			      "reset</a>]</td></tr>\n"

			      "<tr class=poo>"
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
			      TABLE_STYLE);
		p.safePrintf("</tr>\n");
	}

	// if ( format == FORMAT_XML )
	// 	p.safePrintf("\t<messageStats>\n");

	// if ( format == FORMAT_JSON )
	// 	p.safePrintf("\t\"messageStats\":{\n");

	// loop over niceness
	for ( int32_t i3 = 0 ; i3 < 2 ; i3++ ) {
	// print each msg stat
	for ( int32_t i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
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
		if ( format == FORMAT_HTML )
			p.safePrintf( 
				     "<tr class=poo>"
				     "<td>%"INT32"</td>"    // niceness, 0 or 1
				     "<td>0x%hhx</td>" // msgType
				     //"<td>%"INT32"</td>"    // request?
				     "<td>%"INT32"</td>" // packets in
				     "<td>%"INT32"</td>" // packets out
				     "<td>%"INT32"</td>" // acks in
				     "<td>%"INT32"</td>" // acks out
				     "<td>%"INT32"</td>" // reroutes
				     "<td>%"INT32"</td>" // dropped
				     "<td>%"INT32"</td>" // cancel read
				     "<td>%"INT32"</td>" // errors
				     "<td>%"INT32"</td>" // timeouts
				     "<td>%"INT32"</td>" // nomem
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
		if ( format == FORMAT_XML )
			p.safePrintf(
				     "\t<messageStat>\n"
				     "\t\t<niceness>%"INT32"</niceness>\n"
				     "\t\t<msgType>0x%hhx</msgType>\n"
				     "\t\t<packetsIn>%"INT32"</packetsIn>\n"
				     "\t\t<packetsOut>%"INT32"</packetsOut>\n"
				     "\t\t<acksIn>%"INT32"</acksIn>\n"
				     "\t\t<acksOut>%"INT32"</acksOut>\n"
				     "\t\t<reroutes>%"INT32"</reroutes>\n"
				     "\t\t<dropped>%"INT32"</dropped>\n"
				     "\t\t<cancelsRead>%"INT32"</cancelsRead>\n"
				     "\t\t<errors>%"INT32"</errors>\n"
				     "\t\t<timeouts>%"INT32"</timeouts>\n"
				     "\t\t<noMem>%"INT32"</noMem>\n"
				     "\t</messageStat>\n"
				     ,i3, // niceness
				     (unsigned char)i1, // msgType
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
		if ( format == FORMAT_JSON )
			p.safePrintf(
				     "\t\"messageStat\":{\n"
				     "\t\t\"niceness\":%"INT32",\n"
				     "\t\t\"msgType\":\"0x%hhx\",\n"
				     "\t\t\"packetsIn\":%"INT32",\n"
				     "\t\t\"packetsOut\":%"INT32",\n"
				     "\t\t\"acksIn\":%"INT32",\n"
				     "\t\t\"acksOut\":%"INT32",\n"
				     "\t\t\"reroutes\":%"INT32",\n"
				     "\t\t\"dropped\":%"INT32",\n"
				     "\t\t\"cancelsRead\":%"INT32",\n"
				     "\t\t\"errors\":%"INT32",\n"
				     "\t\t\"timeouts\":%"INT32",\n"
				     "\t\t\"noMem\":%"INT32"\n"
				     "\t},\n"
				     ,i3, // niceness
				     (unsigned char)i1, // msgType
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


	//if ( format == FORMAT_XML )
	//	p.safePrintf("\t</messageStats>\n");

	if ( format == FORMAT_XML ) {
		p.safePrintf("</response>\n");
		return g_httpServer.sendDynamicPage(s,p.getBufStart(),
						    p.length(),
						    0,false,"text/xml");
	}

	if ( format == FORMAT_JSON ) {
		// remove last ,\n
		p.m_length -= 2;
		p.safePrintf("\n}\n}\n");
		return g_httpServer.sendDynamicPage(s,p.getBufStart(),
						    p.length(),
						   0,false,"application/json");
	}

	if ( format == FORMAT_HTML )
		p.safePrintf ( "</table><br><br>\n" );


	//
	// print msg send times
	//
	if ( format == FORMAT_HTML )
		p.safePrintf ( 
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=50>"
			      "<center><b>Message Send Times</b></td></tr>\n"

			      "<tr class=poo>"
			      "<td><b>niceness</td>\n"
			      "<td><b>request?</td>\n"
			      "<td><b>msgtype</td>\n"
			      "<td><b>total sent</td>\n"
			      "<td><b>avg send time</td>\n" ,
			      TABLE_STYLE);

	if ( format == FORMAT_HTML ) {
		// print bucket headers
		for ( int32_t i = 0 ; i < MAX_BUCKETS ; i++ ) 
			p.safePrintf("<td>%i+</td>\n",(1<<i)-1);
		p.safePrintf("</tr>\n");
	}

	// loop over niceness
	for ( int32_t i3 = 0 ; i3 < 2 ; i3++ ) {
	// loop over isRequest?
	for ( int32_t i2 = 0 ; i2 < 2 ; i2++ ) {
	// print each msg stat
	for ( int32_t i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
	// skip it if has no handler
	if ( ! g_udpServer.m_handlers[i1] ) continue;
	// skip if xml
	if ( format != FORMAT_HTML ) continue;
		// print it all out.
		// careful, second index is the nicenss, and third is
		// the isReply. our loops are reversed.
		int64_t total = g_stats.m_msgTotalOfSendTimes[i1][i3][i2];
		int64_t nt    = g_stats.m_msgTotalSent       [i1][i3][i2];
		// skip if no stat
		if ( nt == 0 ) continue;
		int32_t      avg   = 0;
		if ( nt > 0 ) avg = total / nt;
		p.safePrintf( 
			     "<tr class=poo>"
			      "<td>%"INT32"</td>"    // niceness, 0 or 1
			      "<td>%"INT32"</td>"    // request?
			      "<td>0x%hhx</td>" // msgType
			      "<td>%"INT64"</td>" // total sent
			      "<td>%"INT32"ms</td>" ,// avg send time in ms
			      i3, // niceness
			      i2, // request?
			      (unsigned char)i1, // msgType
			      nt,
			      avg );
		// print buckets
		for ( int32_t i4 = 0 ; i4 < MAX_BUCKETS ; i4++ ) {
			int64_t count ;
			count = g_stats.m_msgTotalSentByTime[i1][i3][i2][i4];
			p.safePrintf("<td>%"INT64"</td>",count);
		}
		p.safePrintf("</tr>\n");
	}
	}
	}

	if ( format == FORMAT_HTML )
		p.safePrintf ( "</table><br><br>\n" );


	//
	// print msg queued times
	//
	if ( format == FORMAT_HTML ) {
		p.safePrintf ( 
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=50>"
			      "<center><b>Message Queued Times</b></td></tr>\n"

			      "<tr class=poo>"
			      //"<td>request?</td>\n"
			      "<td><b>niceness</td>\n"
			      "<td><b>msgtype</td>\n"
			      "<td><b>total queued</td>\n"
			      "<td><b>avg queued time</td>\n" ,
			      TABLE_STYLE);
		// print bucket headers
		for ( int32_t i = 0 ; i < MAX_BUCKETS ; i++ ) 
			p.safePrintf("<td>%i+</td>\n",(1<<i)-1);
		p.safePrintf("</tr>\n");
	}

	// loop over niceness
	for ( int32_t i3 = 0 ; i3 < 2 ; i3++ ) {
	// print each msg stat
	for ( int32_t i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
		// only html
		if ( format != FORMAT_HTML ) break;
		// skip it if has no handler
		if ( ! g_udpServer.m_handlers[i1] ) continue;
		// print it all out
		int64_t total = g_stats.m_msgTotalOfQueuedTimes[i1][i3];
		int64_t nt    = g_stats.m_msgTotalQueued       [i1][i3];
		// skip if no stat
		if ( nt == 0 ) continue;
		int32_t      avg   = 0;
		if ( nt > 0 ) avg = total / nt;
		p.safePrintf( 
			     "<tr class=poo>"
			      "<td>%"INT32"</td>"    // niceness, 0 or 1
			     "<td>0x%hhx</td>" // msgType
			      //"<td>%"INT32"</td>"    // request?
			      "<td>%"INT64"</td>" // total done
			      "<td>%"INT32"ms</td>" ,// avg handler time in ms
			      i3, // niceness
			      (unsigned char)i1, // msgType
			      //i2, // request?
			      nt ,
			      avg );
		// print buckets
		for ( int32_t i4 = 0 ; i4 < MAX_BUCKETS ; i4++ ) {
			int64_t count ;
			count = g_stats.m_msgTotalQueuedByTime[i1][i3][i4];
			p.safePrintf("<td>%"INT64"</td>",count);
		}
		p.safePrintf("</tr>\n");
	}
	}

	if ( format == FORMAT_HTML )
		p.safePrintf ( "</table><br><br>\n" );


	//
	// print msg handler times
	//
	if ( format == FORMAT_HTML ) {
		p.safePrintf ( 
			      "<table %s>"
			      "<tr class=hdrow>"
			      "<td colspan=50>"
			      "<center><b>Message Reply Generation Times</b>"
			      "</td></tr>\n"

			      "<tr class=poo>"
			      //"<td>request?</td>\n"
			      "<td><b>niceness</td>\n"
			      "<td><b>msgtype</td>\n"
			      "<td><b>total replies</td>\n"
			      "<td><b>avg gen time</td>\n" ,
			      TABLE_STYLE);
		// print bucket headers
		for ( int32_t i = 0 ; i < MAX_BUCKETS ; i++ ) 
			p.safePrintf("<td>%i+</td>\n",(1<<i)-1);
		p.safePrintf("</tr>\n");
	}

	// loop over niceness
	for ( int32_t i3 = 0 ; i3 < 2 ; i3++ ) {
	// print each msg stat
	for ( int32_t i1 = 0 ; i1 < MAX_MSG_TYPES ; i1++ ) {
		// only hyml
		if ( format != FORMAT_HTML ) break;
		// skip it if has no handler
		if ( i1 != 0x41 && ! g_udpServer.m_handlers[i1] ) continue;
		// print it all out
		int64_t total = g_stats.m_msgTotalOfHandlerTimes[i1][i3];
		int64_t nt    = g_stats.m_msgTotalHandlersCalled[i1][i3];
		// skip if no stat
		if ( nt == 0 ) continue;
		int32_t      avg   = 0;
		if ( nt > 0 ) avg = total / nt;
		p.safePrintf( 
			     "<tr class=poo>"
			      "<td>%"INT32"</td>"    // niceness, 0 or 1
			     "<td>0x%hhx</td>" // msgType
			      //"<td>%"INT32"</td>"    // request?
			      "<td>%"INT64"</td>" // total called
			      "<td>%"INT32"ms</td>" ,// avg handler time in ms
			      i3, // niceness
			      (unsigned char)i1, // msgType
			      //i2, // request?
			      nt ,
			      avg );
		// print buckets
		for ( int32_t i4 = 0 ; i4 < MAX_BUCKETS ; i4++ ) {
			int64_t count ;
			count = g_stats.m_msgTotalHandlersByTime[i1][i3][i4];
			p.safePrintf("<td>%"INT64"</td>",count);
		}
		p.safePrintf("</tr>\n");
	}
	}


	if ( format == FORMAT_HTML )
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
		  "<table %s>"
		  "<tr class=hdrow>"
		  "<td colspan=50>"
		  "<center><b>Databases"
		  "</b></td>"
		  "</tr>\n" ,
		  TABLE_STYLE );

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
		g_linkdb.getRdb(),
		//g_cachedb.getRdb(),
		//g_serpdb.getRdb(),
		//g_monitordb.getRdb(),
		g_statsdb.getRdb(),
		g_catdb.getRdb()
		//g_placedb.getRdb() ,
		//g_sectiondb.getRdb()
	};
	int32_t nr = sizeof(rdbs) / sizeof(Rdb *);

	// print dbname
	p.safePrintf("<tr class=poo><td>&nbsp;</td>");
	for ( int32_t i = 0 ; i < nr ; i++ ) 
		p.safePrintf("<td><b>%s</b></td>",rdbs[i]->m_dbname);
	p.safePrintf("<td><i><b>Total</b></i></tr>\n");

	//int64_t total ;
	float totalf ;

	// print # big files
	p.safePrintf("<tr class=poo><td><b># big files</b>*</td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumFiles();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);


	// print # small files
	p.safePrintf("<tr class=poo><td><b># small files</b>*</td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumSmallFiles();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);


	// print disk space used
	p.safePrintf("<tr class=poo><td><b>disk space (MB)</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getDiskSpaceUsed()/1000000;
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);


	// print # recs total
	p.safePrintf("<tr class=poo><td><b># recs</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumTotalRecs();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);


	// print # recs in mem
	p.safePrintf("<tr class=poo><td><b># recs in mem</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumUsedNodes();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);

	// print # negative recs in mem
	p.safePrintf("<tr class=poo><td><b># negative in mem</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumNegativeKeys();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);

	// print mem occupied
	p.safePrintf("<tr class=poo><td><b>mem occupied</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getTreeMemOccupied();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);

	// print mem allocated
	p.safePrintf("<tr class=poo><td><b>mem allocated</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getTreeMemAlloced();
		total += val;
		printNumAbbr ( p , val );
		//p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);

	// print mem max
	p.safePrintf("<tr class=poo><td><b>mem max</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getMaxTreeMem();
		total += val;
		printNumAbbr ( p , val );
		//p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);

	// print rdb mem used
	p.safePrintf("<tr class=poo><td><b>rdb mem used</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getRdbMem()->getUsedMem();
		total += val;
		//p.safePrintf("<td>%"UINT64"</td>",val);
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);

	// print rdb mem avail
	p.safePrintf("<tr class=poo><td><b><nobr>rdb mem available</nobr></b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getRdbMem()->getAvailMem();
		total += val;
		//p.safePrintf("<td>%"UINT64"</td>",val);
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);


	// print map mem
	p.safePrintf("<tr class=poo><td><b>map mem</b></td>");
	total = 0LL;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getMapMemAlloced();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"INT64"</td></tr>\n",total);

	/*
	// print rec cache hits %
	p.safePrintf("<tr class=poo><td><b>rec cache hits %%</b></td>");
	totalf = 0.0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t hits   = rdbs[i]->m_cache.getNumHits();
		int64_t misses = rdbs[i]->m_cache.getNumHits();
		int64_t sum    = hits + misses;
		float val = 0.0;
		if ( sum > 0.0 ) val = ((float)hits * 100.0) / (float)sum;
		totalf += val;
		p.safePrintf("<td>%.1f</td>",val);
	}
	p.safePrintf("<td>%.1f</td></tr>\n",totalf);


	// print rec cache hits
	p.safePrintf("<tr class=poo><td><b>rec cache hits</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->m_cache.getNumHits();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	// print rec cache misses
	p.safePrintf("<tr class=poo><td><b>rec cache misses</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->m_cache.getNumMisses();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);

	// print rec cache tries
	p.safePrintf("<tr class=poo><td><b>rec cache tries</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t hits   = rdbs[i]->m_cache.getNumHits();
		int64_t misses = rdbs[i]->m_cache.getNumHits();
		int64_t val    = hits + misses;
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);

	p.safePrintf("<tr class=poo><td><b>rec cache used slots</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->m_cache.getNumUsedNodes();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>rec cache max slots</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->m_cache.getNumTotalNodes();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>rec cache used bytes</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->m_cache.getMemOccupied();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>rec cache max bytes</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->m_cache.getMaxMem();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);
	*/


	p.safePrintf("<tr class=poo><td><b>file cache hits %%</b></td>");
	totalf = 0.0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		int64_t hits   = rpc->getNumHits();
		int64_t misses = rpc->getNumMisses();
		int64_t sum    = hits + misses;
		float val = 0.0;
		if ( sum > 0.0 ) val = ((float)hits * 100.0) / (float)sum;
		//totalf += val;
		p.safePrintf("<td>%.1f%%</td>",val);
	}
	p.safePrintf("<td>--</td></tr>\n");





	p.safePrintf("<tr class=poo><td><b>file cache hits</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		int64_t val = rpc->getNumHits();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>file cache misses</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		int64_t val = rpc->getNumMisses();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>file cache tries</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		int64_t hits   = rpc->getNumHits();
		int64_t misses = rpc->getNumMisses();
		int64_t val    = hits + misses;
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>file cache adds</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		p.safePrintf("<td>%"UINT64"</td>",rpc->m_adds);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>file cache drops</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		p.safePrintf("<td>%"UINT64"</td>",rpc->m_deletes);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b>file cache used</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		int64_t val = rpc->getMemOccupied();
		total += val;
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b><nobr>file cache allocated</nobr></b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		Rdb *rdb = rdbs[i];
		RdbCache *rpc = getDiskPageCache ( rdb->m_rdbId );
		if ( ! rpc ) {
			p.safePrintf("<td>--</td>");
			continue;
		}
		int64_t val = rpc->getMemAlloced();
		total += val;
		printNumAbbr ( p , val );
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);




	p.safePrintf("<tr class=poo><td><b># disk seeks</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumSeeks();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># disk re-seeks</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumReSeeks();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># bytes read</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumRead();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># get requests read</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumRequestsGet();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># get requests bytes</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNetReadGet();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># get replies sent</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumRepliesGet();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># get reply bytes</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNetSentGet();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);



	p.safePrintf("<tr class=poo><td><b># add requests read</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumRequestsAdd();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># add requests bytes</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNetReadAdd();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);

	p.safePrintf("<tr class=poo><td><b># add replies sent</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNumRepliesAdd();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);


	p.safePrintf("<tr class=poo><td><b># add reply bytes</b></td>");
	total = 0;
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		int64_t val = rdbs[i]->getNetSentAdd();
		total += val;
		p.safePrintf("<td>%"UINT64"</td>",val);
	}
	p.safePrintf("<td>%"UINT64"</td></tr>\n",total);






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
		  "do a 'gb ddump' then when that finishes, a, 'gb pmerge'"
		  "for posdb or a 'gb tmerge' for titledb.\n");

	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );

	// calculate buffer length
	//int32_t bufLen = p - buf;
	int32_t bufLen = p.length();

	if ( format == FORMAT_XML ) {
		p.safePrintf("</response>\n");
		return g_httpServer.sendDynamicPage(s,p.getBufStart(),bufLen,
						    0,false,"text/xml");
	}



	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	//return g_httpServer.sendDynamicPage ( s , buf , bufLen );
	return g_httpServer.sendDynamicPage ( s, p.getBufStart(), bufLen );
}
