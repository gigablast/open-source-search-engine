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
	
	long long now = gettimeofdayInMilliseconds();

	//p.safePrintf("the sizes are %li %li", g_conf.m_medReadSize ,g_conf.m_smaReadSize );

	ThreadQueue* disk;
	
	ThreadQueue* queues = g_threads.getThreadQueues(); 
	for ( long i = 0 ; i < g_threads.getNumThreadQueues(); i++ ) {
		ThreadQueue* q = &queues[i];

		//if ( q->m_top <= 0 ) continue;



		long loActive = q->m_loLaunched - q->m_loReturned;
		long mdActive = q->m_mdLaunched - q->m_mdReturned;
		long hiActive = q->m_hiLaunched - q->m_hiReturned;
		long      total    = loActive + mdActive + hiActive;
		
		p.safePrintf ( "<table width=100%% bgcolor=#d0d0f0 border=1>"
			       "<tr><td bgcolor=#c0c0f0 colspan=\"11\">"
			       //"<center>"
				//"<font size=+1>"
				"<b>Thread Type: %s"
				"  (low: %li"
				"  med: %li"
				"  high: %li"
				"  total: %li)</td></tr>",
				q->getThreadType(), 
				loActive, mdActive, 
				hiActive, total);


		p.safePrintf ("<tr>"
			      "<td><b>Status</b></td>"
			      "<td><b>Niceness</b></td>"
			      "<td><b>Queued Time</b></td>"
			      "<td><b>Run Time</b></td>"
			      "<td><b>Wait for Cleanup</b></td>"
			      "<td><b>Time So Far</b></td>"
			      "<td><b>Callback</b></td>"
			      "<td><b>Routine</b></td>"
			      "<td><b>Bytes Done</b></td>"
			      "<td><b>KBytes/Sec</b></td>"
			      "<td><b>Read|Write</b></td>"
			      "</tr>");

		for ( long j = 0 ; j < q->m_top ; j++ ) {
			ThreadEntry *t = &q->m_entries[j];
			if(!t->m_isOccupied) continue;

			FileState *fs = (FileState *)t->m_state;
			bool diskThread = false;
			if(q->m_threadType == DISK_THREAD && fs) diskThread = true;

			// might have got pre-called from EDISKSTUCK
			if ( ! t->m_callback ) fs = NULL;

			p.safePrintf("<tr>"); 
			
			if(t->m_isDone) {
				p.safePrintf("<td><font color='red'><b>done</b></font></td>");
				p.safePrintf("<td>%li</td>", t->m_niceness);
				p.safePrintf("<td>%lli</td>", t->m_launchedTime - t->m_queuedTime); //queued
				p.safePrintf("<td>%lli</td>", t->m_exitTime - t->m_launchedTime); //run time
				p.safePrintf("<td>%lli</td>", now - t->m_exitTime); //cleanup
				p.safePrintf("<td>%lli</td>", now - t->m_queuedTime); //total
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((long)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((long)t->m_startRoutine));
				if(diskThread && fs) {
					long long took = (t->m_exitTime - t->m_launchedTime);
					if(took <= 0) took = 1;
					p.safePrintf("<td>%li/%li</td>", t->m_bytesToGo, t->m_bytesToGo);
					p.safePrintf("<td>%.2f kbps</td>", (float)t->m_bytesToGo/took);
					p.safePrintf("<td>%s</td>",t->m_doWrite? "Write":"Read");
				}
				else {
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
					p.safePrintf("<td>--</td>");
				}
			}
			else if(t->m_isLaunched) {
				p.safePrintf("<td><font color='red'><b>running</b></font></td>");
				p.safePrintf("<td>%li</td>", t->m_niceness);
				p.safePrintf("<td>%lli</td>", t->m_launchedTime - t->m_queuedTime);
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>%lli</td>", now - t->m_queuedTime);
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((long)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((long)t->m_startRoutine));
				if(diskThread && fs ) {
					long long took = (now - t->m_launchedTime);
					if(took <= 0) took = 1;
					p.safePrintf("<td>???/%li</td>", t->m_bytesToGo);
					p.safePrintf("<td>%.2f kbps</td>", 0.0);//(float)fs->m_bytesDone/took);
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
				p.safePrintf("<td>%li</td>", t->m_niceness);
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>--</td>");
				p.safePrintf("<td>%lli</td>", now - t->m_queuedTime);
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((long)t->m_callback));
				p.safePrintf("<td>%s</td>",  g_profiler.getFnName((long)t->m_startRoutine));
				if(diskThread && fs) {
					p.safePrintf("<td>0/%li</td>", t->m_bytesToGo);
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


	long loActiveBig = disk->m_loLaunchedBig - disk->m_loReturnedBig;
	long loActiveMed = disk->m_loLaunchedMed - disk->m_loReturnedMed;
	long loActiveSma = disk->m_loLaunchedSma - disk->m_loReturnedSma;
	long mdActiveBig = disk->m_mdLaunchedBig - disk->m_mdReturnedBig;
	long mdActiveMed = disk->m_mdLaunchedMed - disk->m_mdReturnedMed;
	long mdActiveSma = disk->m_mdLaunchedSma - disk->m_mdReturnedSma;
	long hiActiveBig = disk->m_hiLaunchedBig - disk->m_hiReturnedBig;
	long hiActiveMed = disk->m_hiLaunchedMed - disk->m_hiReturnedMed;
	long hiActiveSma = disk->m_hiLaunchedSma - disk->m_hiReturnedSma;
	long activeWrites = disk->m_writesLaunched - disk->m_writesReturned;
	p.safePrintf ( "<table width=100%% bgcolor=#d0d0f0 border=1>"
		       "<tr><td bgcolor=#c0c0f0 colspan=\"5\">");
	p.safePrintf ( "<center><b>Active Read Threads</b></center></td></tr>"
		       "<tr><td></td><td colspan='3'><center><b>Priority</b></center></td></tr>"
		       "<tr>"
		       "<td><b>Size</b></td><td>Low</td><td>Medium</td><td>High</td>"
		       "</tr>"
		       // 			       "<tr>"
		       // 			       "<td>Size</td>"
		       // 			       "</tr>"
		       "<tr>"
		       "<td>Small</td> <td>%li</td><td>%li</td><td>%li</td>"
		       "</tr>"
		       "<tr>"
		       "<td>Medium</td> <td>%li</td><td>%li</td><td>%li</td>"
		       "</tr>"
		       "<tr>"
		       "<td>Large</td> <td>%li</td><td>%li</td><td>%li</td>"
		       "</tr>"
		       "</table><br><br>",
		       loActiveSma,
		       mdActiveSma,
		       hiActiveSma,

		       loActiveMed,
		       mdActiveMed,
		       hiActiveMed,

		       loActiveBig,
		       mdActiveBig,
		       hiActiveBig);

	p.safePrintf ("<table width=100%% bgcolor=#d0d0f0 border=1>");
	p.safePrintf ("<tr>"
		      "<td bgcolor=#c0c0f0><b>Active Write Threads</b></td><td>%li</td>"
		      "</tr></table>",
		      activeWrites);


	return g_httpServer.sendDynamicPage ( s , (char*) p.getBufStart() ,
						p.length() );

}
