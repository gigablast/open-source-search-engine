#include "gb-include.h"

#include "TcpServer.h"
#include "Pages.h"
#include "Threads.h"
#include "SafeBuf.h"
#include "Profiler.h"


bool sendPageThreads ( TcpSocket *s , HttpRequest *r ) {
	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);
	// 	char *ss = p.getBuf();
	// 	char *ssend = p.getBufEnd();
	g_pages.printAdminTop ( &p , s , r );
	//p.incrementLength(sss - ss);
	
	int64_t now = gettimeofdayInMilliseconds();

	//p.safePrintf("the sizes are %"INT32" %"INT32"", g_conf.m_medReadSize ,g_conf.m_smaReadSize );

	ThreadQueue* disk;
	
	ThreadQueue* queues = g_threads.getThreadQueues(); 
	for ( int32_t i = 0 ; i < g_threads.getNumThreadQueues(); i++ ) {
		ThreadQueue* q = &queues[i];

		//if ( q->m_top <= 0 ) continue;



		// int32_t loActive = q->m_loLaunched - q->m_loReturned;
		// int32_t mdActive = q->m_mdLaunched - q->m_mdReturned;
		// int32_t hiActive = q->m_hiLaunched - q->m_hiReturned;
		// int32_t      total    = loActive + mdActive + hiActive;

		int32_t total = q->m_launched - q->m_returned;
		
		p.safePrintf ( "<table %s>"
			       "<tr class=hdrow><td colspan=\"11\">"
			       //"<center>"
				//"<font size=+1>"
				"<b>Thread Type: %s"
				// "  (low: %"INT32""
				// "  med: %"INT32""
				// "  high: %"INT32""
				" (launched: %"INT32" "
			       "returned: %"INT32" "
			       "total: %"INT32" maxpossibleout: %i)</td></tr>",
			       TABLE_STYLE,
				q->getThreadType(), 
				// loActive, mdActive, 
				// hiActive, 
			       (int32_t)q->m_launched,
			       (int32_t)q->m_returned,
			       total,
			       (int)MAX_STACKS);


		p.safePrintf ("<tr bgcolor=#%s>"
			      "<td><b>Status</b></td>"
			      "<td><b>Niceness</b></td>"
			      "<td><b>Queued Time</b></td>"
			      "<td><b>Run Time</b></td>"
			      "<td><b>Wait for Cleanup</b></td>"
			      "<td><b>Time So Far</b></td>"
			      "<td><b>Callback</b></td>"
			      "<td><b>Routine</b></td>"
			      "<td><b>Bytes Done</b></td>"
			      "<td><b>Megabytes/Sec</b></td>"
			      "<td><b>Read|Write</b></td>"
			      "</tr>"
			      , LIGHT_BLUE
			      );

		for ( int32_t j = 0 ; j < q->m_maxEntries ; j++ ) {
			ThreadEntry *t = &q->m_entries[j];
			if(!t->m_isOccupied) continue;

			FileState *fs = (FileState *)t->m_state;
			bool diskThread = false;
			if(q->m_threadType == DISK_THREAD && fs) 
				diskThread = true;

			// might have got pre-called from EDISKSTUCK
			if ( ! t->m_callback ) fs = NULL;

			p.safePrintf("<tr bgcolor=#%s>", DARK_BLUE ); 
			
			if(t->m_isDone) {
				p.safePrintf("<td><font color='red'><b>done</b></font></td>");
				p.safePrintf("<td>%"INT32"</td>", t->m_niceness);
				p.safePrintf("<td>%"INT64"ms</td>", t->m_launchedTime - t->m_queuedTime); //queued
				p.safePrintf("<td>%"INT64"ms</td>", t->m_exitTime - t->m_launchedTime); //run time
				p.safePrintf("<td>%"INT64"ms</td>", now - t->m_exitTime); //cleanup
				p.safePrintf("<td>%"INT64"ms</td>", now - t->m_queuedTime); //total
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_startRoutine));
				if(diskThread && fs) {
					int64_t took = (t->m_exitTime - t->m_launchedTime);
					char *sign = "";
					if(took <= 0) {sign=">";took = 1;}
					p.safePrintf("<td>%"INT32"/%"INT32""
						     "</td>", 
						     t->m_bytesToGo, 
						     t->m_bytesToGo);
					p.safePrintf("<td>%s%.2f MB/s</td>", 
						     sign,
						     (float)t->m_bytesToGo/
						     (1024.0*1024.0)/
						     ((float)took/1000.0));
					p.safePrintf("<td>%s</td>",
						     t->m_doWrite? 
						     "<font color=red>"
						     "Write</font>":"Read");
				}
				else {
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
				}
			}
			else if(t->m_isLaunched) {
				p.safePrintf("<td><font color='red'><b>running</b></font></td>");
				p.safePrintf("<td>%"INT32"</td>", t->m_niceness);
				p.safePrintf("<td>%"INT64"</td>", t->m_launchedTime - t->m_queuedTime);
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>%"INT64"</td>", now - t->m_queuedTime);
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_startRoutine));
				if(diskThread && fs ) {
					int64_t took = (now - t->m_launchedTime);
					if(took <= 0) took = 1;
					p.safePrintf("<td>%c%c%c/%"INT32"</td>", '?','?','?',t->m_bytesToGo);
					p.safePrintf("<td>%.2f MB/s</td>", 0.0);//(float)fs->m_bytesDone/took);
					p.safePrintf("<td>%s</td>",t->m_doWrite? "Write":"Read");
				}
				else {
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
				}
			}
			else {
				p.safePrintf("<td><font color='red'><b>queued</b></font></td>");
				p.safePrintf("<td>%"INT32"</td>", t->m_niceness);
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>%"INT64"</td>", now - t->m_queuedTime);
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((PTRTYPE)t->m_startRoutine));
				if(diskThread && fs) {
					p.safePrintf("<td>0/%"INT32"</td>", t->m_bytesToGo);
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>%s</td>",t->m_doWrite? "Write":"Read");
				}
				else {
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
				}
			}
			p.safePrintf("</tr>"); 
		}
		p.safePrintf("</table><br><br>"); 

		
		if(q->m_threadType == DISK_THREAD) disk = q;

	}

	/*
	int32_t loActiveBig = disk->m_loLaunchedBig - disk->m_loReturnedBig;
	int32_t loActiveMed = disk->m_loLaunchedMed - disk->m_loReturnedMed;
	int32_t loActiveSma = disk->m_loLaunchedSma - disk->m_loReturnedSma;
	int32_t mdActiveBig = disk->m_mdLaunchedBig - disk->m_mdReturnedBig;
	int32_t mdActiveMed = disk->m_mdLaunchedMed - disk->m_mdReturnedMed;
	int32_t mdActiveSma = disk->m_mdLaunchedSma - disk->m_mdReturnedSma;
	int32_t hiActiveBig = disk->m_hiLaunchedBig - disk->m_hiReturnedBig;
	int32_t hiActiveMed = disk->m_hiLaunchedMed - disk->m_hiReturnedMed;
	int32_t hiActiveSma = disk->m_hiLaunchedSma - disk->m_hiReturnedSma;
	int32_t activeWrites = disk->m_writesLaunched - disk->m_writesReturned;
	p.safePrintf ( "<table %s>"
		       "<tr class=hdrow><td colspan=\"5\">"
		       , TABLE_STYLE );
	p.safePrintf ( "<center><b>Active Read Threads</b></center></td></tr>"
		       "<tr bgcolor=#%s>"
		       "<td></td><td colspan='3'>"
		       "<center><b>Priority</b></center></td></tr>"
		       "<tr bgcolor=#%s>"
		       "<td><b>Size</b></td><td>Low</td><td>Medium</td><td>High</td>"
		       "</tr>"
		       // 			       "<tr>"
		       // 			       "<td>Size</td>"
		       // 			       "</tr>"
		       "<tr bgcolor=#%s>"
		       "<td>Small</td> <td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td>"
		       "</tr>"
		       "<tr bgcolor=#%s>"
		       "<td>Medium</td> <td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td>"
		       "</tr>"
		       "<tr bgcolor=#%s>"
		       "<td>Large</td> <td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td>"
		       "</tr>"
		       "</table><br><br>",
		       LIGHT_BLUE,
		       LIGHT_BLUE,

		       DARK_BLUE,
		       loActiveSma,
		       mdActiveSma,
		       hiActiveSma,

		       DARK_BLUE,
		       loActiveMed,
		       mdActiveMed,
		       hiActiveMed,

		       DARK_BLUE,
		       loActiveBig,
		       mdActiveBig,
		       hiActiveBig);

	p.safePrintf ("<table %s>",TABLE_STYLE);
	p.safePrintf ("<tr class=hdrow>"
		      "<td><b>Active Write Threads</b></td><td>%"INT32"</td>"
		      "</tr></table>",
		      activeWrites);
	*/

	return g_httpServer.sendDynamicPage ( s , (char*) p.getBufStart() ,
						p.length() );

}
