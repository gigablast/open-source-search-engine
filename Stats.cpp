#include "gb-include.h"

#include <errno.h>
#include "Stats.h"
//#define X_DISPLAY_MISSING 1
//#include <plotter.h>
#include <math.h>
#include "Conf.h"
#include "PingServer.h"
//#include "Statsdb.h"

class Stats g_stats;

// just clear our points array when we're born
Stats::Stats ( ) { 
	//m_gotLock            = false;
	m_next               = 0;
	//m_minWindowStartTime = 0;
	memset ( m_pts , 0 , sizeof(StatPoint)*MAX_POINTS );

	//m_readSignals = 0;
	//m_writeSignals = 0;
	m_slowDiskReads = 0;
	m_queryTimes = 0;
	m_numQueries = 0;
	m_numSuccess = 0;
	m_numFails   = 0;
	m_avgQueryTime = 0;
	m_successRate = 1.0;
	m_totalNumQueries = 0;
	m_totalNumSuccess = 0;
	m_totalNumFails   = 0;
	m_avgQueriesPerSec = 0;
	m_lastQueryLogTime = gettimeofdayInMilliseconds();
	m_startTime = m_lastQueryLogTime;
        m_upTime = 0;
	m_closedSockets = 0;
	m_spiderSample = 0;
	m_spiderErrors = 0;
	m_spiderNew = 0;
	m_spiderErrorsNew = 0;
	m_totalSpiderSuccessNew = 0;
	m_totalSpiderErrorsNew = 0;
	m_totalSpiderSuccessOld = 0;
	m_totalSpiderErrorsOld = 0;
	m_msg3aRecallCnt = 0;
	// m_tierHits[0] = 0;
	// m_tierHits[1] = 0;
	// m_tierHits[2] = 0;
	// m_tier2Misses = 0;
	// m_tierTimes[0] = 0;
	// m_tierTimes[1] = 0;
	// m_tierTimes[2] = 0;
	//m_totalDedupCand = 0;
	//m_dedupedCand = 0;
	//m_bannedDups = 0;
	//m_bigHackDups = 0;
	//m_summaryDups = 0;
	//m_contentDups = 0;
	//m_clusteredTier1 = 0;
	//m_clusteredTier2 = 0;
	//m_errored = 0;
	m_msg3aRecalls[0] = 0;
	m_msg3aRecalls[1] = 0;
	m_msg3aRecalls[2] = 0;
	m_msg3aRecalls[3] = 0;
	m_msg3aRecalls[4] = 0;
	m_msg3aRecalls[5] = 0;

	memset(m_errCodes, 0, 1000*4);
	memset(m_isSampleNew, 0, 1000);
	memset(m_allErrorsNew, 0, 65536*8);
	memset(m_allErrorsOld, 0, 65536*8);
	clearMsgStats();
};


void Stats::clearMsgStats() {
	char *start = &m_start;
	char *end   = &m_end;
	memset ( start , 0 , end - start );
}

//static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

void Stats::addStat_r ( int32_t        numBytes    , 
			int64_t   startTime   ,
			int64_t   endTime     ,
			int32_t        color       ,
			char        type        ,
			char *fname) {

	// lock up
	//pthread_mutex_lock ( &s_lock );
	// . is there a point in slot we're about to occupy?
	// . we might have to advance the min time for the graph window
	//   so we don't exclude any disk reads/writes in the beginning
	//if ( m_pts [ m_next ].m_startTime > 0                    &&
	//     m_pts [ m_next ].m_endTime   > m_minWindowStartTime   )
	//		m_minWindowStartTime = m_pts[m_next].m_endTime;
	// claim next before another thread does
	int32_t n = m_next++;
	// watch out if another thread just inc'ed n
	if ( n >= MAX_POINTS ) n = 0;
	// stick our point in the array
	StatPoint *p = & m_pts [ n ];
	p->m_numBytes  = numBytes;
	p->m_startTime = startTime ;
	p->m_endTime   = endTime;
	p->m_color     = color;
	p->m_type      = type;

	if(fname) {
		if(m_keyCols.length() > 512) m_keyCols.reset();
		p->m_color     = hash32n(fname);
		p->m_color &= 0xffffff;
		m_keyCols.safePrintf(""
				     "<td bgcolor=#%x>"
				     "&nbsp; &nbsp;</td>"
				     "<td> %s "
				     "</td>"
				     "", p->m_color,fname);
	}
	// we may be the first point
	//if ( m_minWindowStartTime == 0 ) m_minWindowStartTime = startTime;
	// this too
	//if (startTime <m_minWindowStartTime ) m_minWindowStartTime=startTime;
	// advance the next available slot ptr, wrap if necessary
	if ( m_next >= MAX_POINTS ) m_next = 0;

	// add it to statsdb now too!
	//g_statsdb.addStat ( label, startTime,endTime, numBytes );

	// unlock
	//pthread_mutex_unlock ( &s_lock );
}

/*
// . dump a graph to /tmp/diskGraph.gif
// . use libplotter.a or .so ?
// . docs at http://www.gnu.org/manual/plotutils/html_mono/plotutils.html#SEC54
void Stats::dumpGIF ( int64_t startTime , int64_t endTime ) {

	char fname [ 1024 ];
	sprintf ( fname , "%s/diskGraph%"INT32".gif" ,
		  g_hostdb.m_httpRootDir , g_hostdb.m_hostId );
	FILE *fd = fopen ( fname,"w" );
	if ( ! fd ) { 
		log("admin: Stats::dumpGraph: fopen ( %s , \"w\" ) : %s", 
		    fname , mstrerror(errno) );
		return;
	}
	//FILE *fde = fopen ("/tmp/diskGraphError","w" );
	//if ( ! fde ) { 
	//	log("Stats::dumpGraph: /tmp/diskGraphError: %s", 
	//	    mstrerror(errno) );
	//	return;
	//}
	
	// add some test stats
	//addStat_r ( 100 , true , gettimeofdayInMilliseconds() - 10 ,
	//	  gettimeofdayInMilliseconds() -3 );
	//addStat_r ( 2*1024*1024 , true , gettimeofdayInMilliseconds() - 5 ,
	//	  gettimeofdayInMilliseconds() );

	// plotter uses malloc, so we must get the lock from Mem.cpp
	//mutexLock();
	//m_gotLock = true;

	// gif size
	char tmp[64];
	sprintf ( tmp , "%"INT32"x%"INT32"", (int32_t)DX+40 , (int32_t)DY+40 ); // "1040x440"

#ifdef _USEPLOTTER_

	GIFPlotter::parampl ( "BITMAPSIZE" , (void *)tmp );
	//int dx = 1000;
	//int dy = 400;
	// 20 pixel borders
	int bx = 10;
	int by = 30;
	// create one
	GIFPlotter plotter ( NULL , fd , NULL );
	// open it
	plotter.openpl ( );
	// define the space with boundaries 100 unit wide boundaries
	plotter.space ( -bx , -by , DX + bx , DY + by );
	// line thickness in user coordinates (pixels for us)
	plotter.linewidth ( 1 );       
	// set bg color to gray (r/g/b) 
	plotter.bgcolor ( 0xd600 , 0xce00 , 0xd600 );
	// set bg color to white (r/g/b) 
	//plotter.bgcolor ( 0xff00 , 0xff00 , 0xff00 );
	// erase Plotter's graphics display
	plotter.erase ();                
	// draw axises in black
	plotter.pencolorname ("black");    
	// draw the x-axis
	plotter.line ( 0 , 0 , DX , 0  );
	// draw the y-axis
	plotter.line ( 0 , 0 ,  0 , DY );
	// x-axis label
	//plotter.move       ( DX / 2 , -by / 2 );
	//plotter.alabel     ( 'c' , 'c' , "disk accesses" );
	// y-axis lavel
	//plotter.move      ( x1 - bx / 2   , y1 + dy / 2 );
	//plotter.textangle ( 90 );
	//plotter.alabel    ( 'c' , 'c' , "disk accesses" );

	// find time ranges
	int64_t t2 = 0;
	for ( int32_t i = 0 ; i < MAX_POINTS ; i++ ) {
		// skip empties
		if ( m_pts[i].m_startTime == 0 ) continue;
		// set min/max
		if ( m_pts[i].m_endTime   > t2 ) t2 = m_pts[i].m_endTime;
	}
	// now compute the start time for the graph
	int64_t t1 = 0x7fffffffffffffffLL;
	// now recompute t1
	for ( int32_t i = 0 ; i < MAX_POINTS ; i++ ) {
		// skip empties
		if ( m_pts[i].m_startTime == 0 ) continue;
		// can't be behind more than 1 second
		if ( m_pts[i].m_startTime   < t2 - DT ) continue;
		// otherwise, it's a candidate for the first time
		if ( m_pts[i].m_startTime < t1 ) t1 = m_pts[i].m_startTime;
	}
	// what's the delta t? just make it 1000 ms
	//int64_t dt = 1000; // t2 - t1;

	// get time range scaling info
	//int64_t t1 = m_minWindowStartTime;
	//int64_t t2 = gettimeofdayInMilliseconds();
	//if ( startTime > 0 && t1 < startTime ) t1 = startTime;
	//if ( endTime   > 0 && t2 > endTime   ) t2 = endTime;

	// 10 x-axis tick marks
	for ( int x = DX/10 ; x < DX ; x += DX/10 ) {
		// tick mark
		plotter.line ( x , -20 , x , 20 );
		// generate label
		char buf [ 32 ];
		sprintf ( buf , "%"INT32"" , 
			  (int32_t)(DT * (int64_t)x / (int64_t)DX) );
		// move cursor
		plotter.move ( x , -by / 2 - 9 );
		// plot label
		plotter.alabel     ( 'c' , 'c' , buf );
	}

	// . each line consists of several points
	// . we need to know each point for adding otherlines
	// . is about [400/6][1024] = 70k
	// . each line can contain multiple data points
	// . each data point is expressed as a horizontal line segment
	void *lrgBuf;
	int32_t lrgSize = 0;
	lrgSize += MAX_LINES * MAX_POINTS * sizeof(StatPoint *);
	lrgSize += MAX_LINES * sizeof(int32_t);
	lrgBuf = (char *) mmalloc(lrgSize, "Stats.cpp"); 
	if (! lrgBuf) {
	    log("could not allocate memory for local buffer in Stats.cpp"
		"%"INT32" bytes needed", lrgSize);
	    return;
	}
	char *lrgPtr = (char *)lrgBuf;
	StatPoint **points = (StatPoint **)lrgPtr;   
	lrgPtr += MAX_LINES * MAX_POINTS * sizeof(StatPoint *);
	int32_t *numPoints = (int32_t *)lrgPtr;
	lrgPtr += MAX_LINES * sizeof(int32_t);
	memset ( (char *)numPoints , 0 , MAX_LINES * sizeof(int32_t) );

	// store the data points into "lines"
	int32_t count = MAX_POINTS;
	for ( int32_t i = m_next ; count >= 0 ; i++ , count-- ) {
		// wrap around the array
		if ( i >= MAX_POINTS ) i = 0;
		// skip point if empty
		if ( m_pts[i].m_startTime == 0 ) continue;
		// skip if too early
		if ( m_pts[i].m_endTime < t1 ) continue;
		// . find the lowest line the will hold us
		// . this adds point to points[x][n] where x is determined
		addPoint ( points , numPoints , &m_pts[i] );
	}

	int y1 = 21;
	// plot the points (lines) in each line
	for ( int32_t i = 0 ; i < MAX_LINES    ; i++ ) {
		// increase vert
		y1 += MAX_WIDTH + 1;
		// wrap back down if necessary
		if ( y1 >= DY ) y1 = 21;
		// plt all points in this row
	for ( int32_t j = 0 ; j < numPoints[i] ; j++ ) {
		// get the point
		StatPoint *p =  points[MAX_POINTS * i + j];
		// transform time to x coordinates
		int x1 = (p->m_startTime - t1) * (int64_t)DX / DT;
		int x2 = (p->m_endTime   - t1) * (int64_t)DX / DT;
		// if x2 is negative, skip it
		if ( x2 < 0 ) continue;
		// if x1 is negative, boost it to -2
		if ( x1 < 0 ) x1 = -2;
		// . line thickness is function of read/write size
		// . take logs
		int w = (int)log(((double)p->m_numBytes)/8192.0) + 3;
		//log("log of %"INT32" is %i",m_pts[i].m_numBytes,w);
		if ( w < 3         ) w = 3;
		if ( w > MAX_WIDTH ) w = MAX_WIDTH;
		plotter.linewidth ( w );       
		// use the color specified from addStat_r() for this line/pt
		plotter.pencolor ( ((p->m_color >> 16) & 0xff) << 8 ,
				   ((p->m_color >>  8) & 0xff) << 8 ,
				   ((p->m_color >>  0) & 0xff) << 8 );
		// ensure at least 3 units wide for visibility
		if ( x2 < x1 + 3 ) x2 = x1 + 3;
		// . flip the y so we don't have to scroll the browser down
		// . DY does not include the axis and tick marks
		int32_t fy1 = DY - y1 + 20 ;
		// plot it
		plotter.line ( x1 , fy1 , x2 , fy1 );
		// debug msg
		//log("line (%i,%i, %i,%i) ", x1 , vert , x2 , vert );
		//log("bytes = %"INT32" width = %"INT32" ", m_pts[i].m_numBytes,w);
		//log("st=%i, end=%i color=%"XINT32" " ,
		//      (int)m_pts[i].m_startTime , 
		//      (int)m_pts[i].m_endTime   , 
		//      m_pts[i].m_color );
	}
	}

	// all done
	if ( plotter.closepl () < 0 ) 
		log("admin: Could not close performance graph object.");

	// plotter uses malloc, so we must get the lock from Mem.cpp
	//m_gotLock = false;
	//mutexUnlock();

	// close the file
	fclose ( fd );
	mfree(lrgBuf, lrgSize, "Stats.cpp");
#endif
}
*/

void Stats::addPoint (StatPoint **points    , 
		      int32_t       *numPoints ,
		      StatPoint  *p         ) {
	// go down each line of points
	for ( int32_t i = 0 ; i < MAX_LINES ; i++ ) {
		// is there room for us in this line?
		int32_t n = numPoints[i];
		// if line is full, skip it
		if ( n >= MAX_POINTS ) continue;
		int32_t j;
		// make a boundary around point there already
		int64_t a = p->m_startTime;
		int64_t b = p->m_endTime;
		// . for a space to appear we need to be separated
		//   by this many milliseconds
		// . this is milliseconds per pixel
		// . right now it's about 5
		int32_t border = DT / DX ;
		if ( border <= 0 ) border = 1;
		a -= 4*border;
		b += 4*border;
		// debug
		//log("a=%"INT64" b=%"INT64" d=%"INT32"",a,b,4*border);
		for ( j = 0 ; j < n ; j++ ) {
			// get that point
			StatPoint *pp = points[MAX_POINTS * i + j];
			// . do we intersect this point (horizontal line)?
			// . if so, break out
			if ( pp->m_startTime >= a && pp->m_startTime <= b )
				break;
			if ( pp->m_endTime   >= a && pp->m_endTime   <= b )
				break;
		}
		// if j is < n then there's no room
		if ( j < n ) continue;
		// otherwise, add our point
		points[MAX_POINTS * i + n] = p;
		numPoints[i]++;
		return;
	}
}


void Stats::calcQueryStats() {
	int64_t now = gettimeofdayInMilliseconds();
	m_upTime = now - m_startTime;
	m_avgQueryTime  = (float)m_queryTimes /
		((float)m_numQueries * 1000.0);
	m_successRate = (float)m_numSuccess / 
		(float)(m_numSuccess + m_numFails);
	//(number of queries) / seconds that it took to get this many queries
	m_avgQueriesPerSec = ((float)m_numQueries * 1000.0) / 
		(float)(now - m_lastQueryLogTime);
}


void Stats::logAvgQueryTime(int64_t startTime) {
	int64_t now = gettimeofdayInMilliseconds();
	int64_t took = now - startTime;
	static int32_t s_lastSendTime = 0;
	// if just one query took an insanely int32_t time,
	// do not sound the alarm. this is in seconds,
	// so multiply by 1000.
	//int64_t maxTook = 
	//	(int64_t)(g_conf.m_maxQueryTime*1000.0) ;
	//if ( took > maxTook ) took = maxTook;
	m_queryTimes += took;
	m_numQueries++;

	if ( m_numQueries > g_conf.m_numQueryTimes )
		goto reset;

	if (m_numQueries != g_conf.m_numQueryTimes) return;
	// otherwise, store this info
	m_avgQueryTime  = (float)m_queryTimes /
		((float)m_numQueries * 1000.0);
	m_successRate = (float)m_numSuccess / 
		(float)(m_numSuccess + m_numFails);
	//(number of queries) / seconds that it took to get this many queries
	m_avgQueriesPerSec = ((float)m_numQueries * 1000.0) / 
		(float)(now - m_lastQueryLogTime);
	m_lastQueryLogTime = now;

	if(m_avgQueryTime > g_conf.m_avgQueryTimeThreshold ||
	   m_successRate  < g_conf.m_querySuccessThreshold) {
		char msgbuf[1024];
		Host *h = g_hostdb.getHost ( 0 );
		snprintf(msgbuf, 1024,
			 "Average latency: %f sec. "
			 "success rate: %f.  "
			 "queries/sec: %f.  "
			 "host: %s.",
			 m_avgQueryTime, m_successRate, m_avgQueriesPerSec,
			 iptoa(h->m_ip));
		log(LOG_WARN, "query: %s",msgbuf);
		// prevent machinegunning text msgs
		int32_t now = getTimeLocal();
		if ( now - s_lastSendTime > 300 ) {
			s_lastSendTime = now;
			g_pingServer.sendEmail(NULL, msgbuf);
		}
	}
	else {
		log(LOG_INFO, "query: Average latency is %f seconds, "
		    "succeeding at a rate of %f, serving %f queries/sec.",
		    m_avgQueryTime, m_successRate, m_avgQueriesPerSec);
	}
 reset:
	m_totalNumQueries += m_numSuccess + m_numFails;
	m_totalNumSuccess += m_numSuccess;
	m_totalNumFails   += m_numFails;
	
	m_numQueries = 0;
	m_queryTimes = 0;
	m_numSuccess = 0;
	m_numFails = 0;
}

void Stats::addSpiderPoint ( int32_t errCode, bool isNew ) {
	// keep track of last 1000 urls spidered
	int32_t i = m_spiderSample % 1000;
	if ( m_errCodes[i] ) m_spiderErrors--;
	if ( m_isSampleNew[i] ) m_spiderNew--;
	if ( m_errCodes[i] && m_isSampleNew[i] ) m_spiderErrorsNew--;
	m_errCodes[i] = errCode;
	m_isSampleNew[i] = (char)isNew;
	if ( m_errCodes[i] ) m_spiderErrors++;
	if ( m_isSampleNew[i] ) m_spiderNew++;
	if ( m_errCodes[i] && m_isSampleNew[i] ) m_spiderErrorsNew++;
	m_spiderSample++;

	// keep track of total spiders
	if ( isNew ) {
		if ( errCode ) m_totalSpiderErrorsNew++;
		else           m_totalSpiderSuccessNew++;
		m_allErrorsNew[errCode]++;
	}
	else {
		if ( errCode ) m_totalSpiderErrorsOld++;
		else           m_totalSpiderSuccessOld++;
		m_allErrorsOld[errCode]++;
	}
}

// draw a HORIZONTAL line in html
void drawLine2 ( SafeBuf &sb ,
		 int32_t x1 , 
		 int32_t x2 ,
		 int32_t fy1 , 
		 int32_t color ,
		 int32_t width ) {

	sb.safePrintf("<div style=\"position:absolute;"
		      "left:%"INT32";"
		      "top:%"INT32";"
		      "background-color:#%06"XINT32";"
		      "z-index:5;"
		      "min-height:%"INT32"px;"
		      "min-width:%"INT32"px;\"></div>\n"
		      , x1
		      , (fy1 - width/2) - 20 //- 300
		      , color
		      , width
		      , x2 - x1
		      );
}


//
// new code for drawing graph in html with absolute divs instead
// of using GIF plotter library which had issues
//
void Stats::printGraphInHtml ( SafeBuf &sb ) {

	// gif size
	char tmp[64];
	sprintf ( tmp , "%"INT32"x%"INT32"", (int32_t)DX+40 , (int32_t)DY+40 ); // "1040x440"

	// 20 pixel borders
	//int bx = 10;
	//int by = 30;
	// define the space with boundaries 100 unit wide boundaries
	//plotter.space ( -bx , -by , DX + bx , DY + by );
	// draw the x-axis
	//plotter.line ( 0 , 0 , DX , 0  );
	// draw the y-axis
	//plotter.line ( 0 , 0 ,  0 , DY );

	// find time ranges
	int64_t t2 = 0;
	for ( int32_t i = 0 ; i < MAX_POINTS ; i++ ) {
		// skip empties
		if ( m_pts[i].m_startTime == 0 ) continue;
		// set min/max
		if ( m_pts[i].m_endTime   > t2 ) t2 = m_pts[i].m_endTime;
	}
	// now compute the start time for the graph
	int64_t t1 = 0x7fffffffffffffffLL;
	// now recompute t1
	for ( int32_t i = 0 ; i < MAX_POINTS ; i++ ) {
		// skip empties
		if ( m_pts[i].m_startTime == 0 ) continue;
		// can't be behind more than 1 second
		if ( m_pts[i].m_startTime   < t2 - DT ) continue;
		// otherwise, it's a candidate for the first time
		if ( m_pts[i].m_startTime < t1 ) t1 = m_pts[i].m_startTime;
	}

	//
	// main graphing window
	//
	sb.safePrintf("<div style=\"position:relative;"
		      "background-color:#c0c0c0;"

		      // match style of tables
		      "border-radius:10px;"
		      "border:#6060f0 2px solid;"
		      
		      //"overflow-y:hidden;"
		      "overflow-x:hidden;"
		      //"z-index:-10;"
		      // the tick marks we print below are based on it
		      // being a window of the last 20 seconds... and using
		      // DX pixels
		      "min-width:%"INT32"px;"
		      "min-height:%"INT32"px;"
		      //"width:100%%;"
		      //"min-height:600px;"
		      //"margin-top:10px;"
		      "margin-bottom:10px;"
		      //"margin-right:10px;"
		      //"margin-left:10px;"
		      "\">"
		      ,(int32_t)DX
		      ,(int32_t)DY +20); // add 10 more for "2s" labels etc.

	// 10 x-axis tick marks
	for ( int x = DX/20 ; x <= DX ; x += DX/20 ) {
		// tick mark
		//plotter.line ( x , -20 , x , 20 );
		sb.safePrintf("<div style=\"position:absolute;"
			      "left:%"INT32";"
			      "bottom:0;"
			      "background-color:#000000;"
			      "z-index:110;"
			      "min-height:20px;"
			      "min-width:3px;\"></div>\n"
			      , (int32_t)x-1
			      );
		// generate label
		//char buf [ 32 ];
		//sprintf ( buf , "%"INT32"" , 
		//	  (int32_t)(DT * (int64_t)x / (int64_t)DX) );
		// LABEL
		sb.safePrintf("<div style=\"position:absolute;"
			      "left:%"INT32";"
			      "bottom:20;"
			      //"background-color:#000000;"
			      "z-index:110;"
			      "min-height:20px;"
			      "min-width:3px;\">%.01fs</div>\n"
			      , (int32_t)x-10
			      // the label:
			      ,(float)(DT* (int64_t)x / (int64_t)DX)/1000.0
			      );

		// move cursor
		//plotter.move ( x , -by / 2 - 9 );
		// plot label
		//plotter.alabel     ( 'c' , 'c' , buf );
	}

	// . each line consists of several points
	// . we need to know each point for adding otherlines
	// . is about [400/6][1024] = 70k
	// . each line can contain multiple data points
	// . each data point is expressed as a horizontal line segment
	void *lrgBuf;
	int32_t lrgSize = 0;
	lrgSize += MAX_LINES * MAX_POINTS * sizeof(StatPoint *);
	lrgSize += MAX_LINES * sizeof(int32_t);
	lrgBuf = (char *) mmalloc(lrgSize, "Stats.cpp"); 
	if (! lrgBuf) {
	    log("could not allocate memory for local buffer in Stats.cpp"
		"%"INT32" bytes needed", lrgSize);
	    return;
	}
	char *lrgPtr = (char *)lrgBuf;
	StatPoint **points = (StatPoint **)lrgPtr;   
	lrgPtr += MAX_LINES * MAX_POINTS * sizeof(StatPoint *);
	int32_t *numPoints = (int32_t *)lrgPtr;
	lrgPtr += MAX_LINES * sizeof(int32_t);
	memset ( (char *)numPoints , 0 , MAX_LINES * sizeof(int32_t) );

	// store the data points into "lines"
	int32_t count = MAX_POINTS;
	for ( int32_t i = m_next ; count >= 0 ; i++ , count-- ) {
		// wrap around the array
		if ( i >= MAX_POINTS ) i = 0;
		// skip point if empty
		if ( m_pts[i].m_startTime == 0 ) continue;
		// skip if too early
		if ( m_pts[i].m_endTime < t1 ) continue;
		// . find the lowest line the will hold us
		// . this adds point to points[x][n] where x is determined
		addPoint ( points , numPoints , &m_pts[i] );
	}

	int y1 = 21;
	// plot the points (lines) in each line
	for ( int32_t i = 0 ; i < MAX_LINES    ; i++ ) {
		// increase vert
		y1 += MAX_WIDTH + 1;
		// wrap back down if necessary
		if ( y1 >= DY ) y1 = 21;
		// plt all points in this row
	for ( int32_t j = 0 ; j < numPoints[i] ; j++ ) {
		// get the point
		StatPoint *p =  points[MAX_POINTS * i + j];
		// transform time to x coordinates
		int x1 = (p->m_startTime - t1) * (int64_t)DX / DT;
		int x2 = (p->m_endTime   - t1) * (int64_t)DX / DT;
		// if x2 is negative, skip it
		if ( x2 < 0 ) continue;
		// if x1 is negative, boost it to -2
		if ( x1 < 0 ) x1 = -2;
		// . line thickness is function of read/write size
		// . take logs
		int w = (int)log(((double)p->m_numBytes)/8192.0) + 3;
		//log("log of %"INT32" is %i",m_pts[i].m_numBytes,w);
		if ( w < 3         ) w = 3;
		if ( w > MAX_WIDTH ) w = MAX_WIDTH;
		//plotter.linewidth ( w );       
		// use the color specified from addStat_r() for this line/pt
		//plotter.pencolor ( ((p->m_color >> 16) & 0xff) << 8 ,
		//		   ((p->m_color >>  8) & 0xff) << 8 ,
		//		   ((p->m_color >>  0) & 0xff) << 8 );
		// ensure at least 3 units wide for visibility
		if ( x2 < x1 + 3 ) x2 = x1 + 3;
		// . flip the y so we don't have to scroll the browser down
		// . DY does not include the axis and tick marks
		int32_t fy1 = DY - y1 + 20 ;
		// plot it
		//plotter.line ( x1 , fy1 , x2 , fy1 );
		drawLine2 ( sb , x1 , x2 , fy1 , p->m_color , w );
		// debug msg
		//log("line (%i,%i, %i,%i) ", x1 , vert , x2 , vert );
		//log("bytes = %"INT32" width = %"INT32" ", m_pts[i].m_numBytes,w);
		//log("st=%i, end=%i color=%"XINT32" " ,
		//      (int)m_pts[i].m_startTime , 
		//      (int)m_pts[i].m_endTime   , 
		//      m_pts[i].m_color );
	}
	}

	sb.safePrintf("</div>\n");

	mfree(lrgBuf, lrgSize, "Stats.cpp");
}
