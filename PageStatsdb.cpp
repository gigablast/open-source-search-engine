#include "gb-include.h"

#define X_DISPLAY_MISSING 1
//#include <plotter.h>
//#include <fstream.h>
#include <math.h>

//#include "CollectionRec.h"
#include "Pages.h"
#include "Statsdb.h"
#include "Hostdb.h"
#include "SafeBuf.h"
#include "SafeList.h"
#include "Threads.h"

class StateStatsdb {
 public:
 	TcpSocket	*m_socket;
	HttpRequest	 m_request;

	SafeBuf m_sb2;

	// Original timestamp request data
 	time_t m_startDate;
	time_t m_endDate;
	int32_t   m_datePeriod;
	int32_t   m_dateUnits;
	// Timestamp data modified for the request
 	time_t m_startDateR;
	time_t m_endDateR;

	// For the auto-update AJAX script
	bool m_autoUpdate;

	int32_t m_samples;

	// Misc. request data
	int32_t m_hostId;

	// Request & build flags
	bool m_dateLimit;
	bool m_dateCustom;
	bool m_cacti;
	bool m_now;

	int32_t m_niceness;
};

static time_t genDate( char *date, int32_t dateLen ) ;
static void   sendReply ( void *st ) ;

static bool s_graphInUse = false;

// . returns false if blocked, otherwise true
// . sets g_errno on error
bool sendPageGraph ( TcpSocket *s, HttpRequest *r ) {

	if ( s_graphInUse ) {
		char *msg = "stats graph calculating for another user. "
			"Try again later.";
		g_httpServer.sendErrorReply(s,500,msg);
		return true;
	}
	
	char *cgi;
	int32_t cgiLen;
	StateStatsdb *st;
	try { st = new StateStatsdb; }
	catch ( ... ) {
		g_errno = ENOMEM;
		log(LOG_INFO, "PageStatsdb: failed to allocate state memory.");
		return true;
	}
	mnew( st, sizeof(StateStatsdb), "PageStatsdb" );

	st->m_niceness     = MAX_NICENESS;

	st->m_socket  	   = s;
	//st->m_request 	   = *r;
	st->m_request.copy ( r );

	// hostId must be one of the following:
	// 	 0-n - a valid hostId
	//	-1   - a sample (subset) of the hosts
	//	-2   - all hosts
	//	-3   - this host
	st->m_hostId = r->getLong( "host", -3 );
	if ( st->m_hostId == -3 )
		st->m_hostId = g_hostdb.getMyHostId();

        // If we are pulling from multiple hosts, are we merging
        // the data into a single graph?
	// TODO:
	// - Make sure this always happens. Now our only concern
	//   is how many stats we will be drawing.
	//st->m_mergeResults = (bool    )r->getLong( "merge_results" , 1 );

	// get session parameters
	st->m_cacti	   = (bool   )r->getLong( "cacti"       , 0 );

	// get date parameters
	cgi = r->getString( "sdate" , &cgiLen , NULL );
	st->m_startDate = genDate( cgi, cgiLen );
	cgi = r->getString( "edate" , &cgiLen , NULL );
	st->m_endDate = genDate( cgi, cgiLen );
	st->m_dateCustom   = (bool)r->getLong( "custom",  0 );
	// default to 10 hours, i would do 1 day except that there are
	// some bugs that mess up the display a lot when i do that
	st->m_datePeriod   = r->getLong( "date_period" , 300 );//36000 );
	st->m_dateUnits    = r->getLong( "date_units"  , 1 );//SECS_PER_MIN
	st->m_now	   = (bool)r->getLong( "date_now"   , 1 );
	st->m_autoUpdate   = (bool)r->getLong( "auto_update" , 0 );

	// # samples in moving average
	st->m_samples      = r->getLong( "samples" , 300 );


	//if ( st->m_columns < MIN_COLUMNS || st->m_columns > MAX_COLUMNS )
	//	st->m_columns = DEF_COLUMNS;

	if ( st->m_now )
		st->m_startDate = (time_t)getTimeGlobalNoCore();

	st->m_startDateR = st->m_startDate;
	st->m_endDateR   = st->m_endDate; 

	if ( ! st->m_dateCustom ) {
		st->m_endDateR = st->m_startDateR - ( st->m_datePeriod *
						      st->m_dateUnits );
		st->m_endDate = st->m_endDateR;
	}

	//
	// this is no longer a gif, but an html graph in g_statsdb.m_sb
	//
	if ( ! g_statsdb.makeGIF ( st->m_endDateR   ,
				   st->m_startDateR ,
				   st->m_samples ,
				   &st->m_sb2 ,
				   st               ,
				   sendReply        ) ) {
		s_graphInUse = true;
		return false;
	}

	// if we didn't block call it ourselves directly
	sendReply ( st );

	return true;
}

void genStatsDataset(SafeBuf *buf, StateStatsdb *st) {
	if ( ! g_conf.m_useStatsdb ) {
		buf->safePrintf("{\"error\":\"statsdb disabled\"}\n" );
        return;
    }
    

}

static void writeControls ( SafeBuf *buf, StateStatsdb *st ) ;
void genStatsGraphTable(SafeBuf *buf, StateStatsdb *st) {
	if ( ! g_conf.m_useStatsdb ) 
		buf->safePrintf("<font color=red><b>Statsdb disabled. "
			       "Turn on in the master controls.</b>"
			       "</font>\n" );


	buf->safePrintf("<table %s>\n",TABLE_STYLE);

	buf->safePrintf("<tr><td bgcolor=#%s>"
		       "<center>",LIGHT_BLUE);

	/////////////////////////
	//
	// insert the div graph here
	//
	/////////////////////////
	buf->cat ( g_statsdb.m_gw );

	// purge it
	g_statsdb.m_gw.purge();
	g_statsdb.m_dupTable.reset();

	//"<img src=\"/stats%"INT32".gif\" height=%"INT32" width=%"INT32" "
	//"border=\"0px\">"
	//st->m_hostId,
	//g_statsdb.getImgHeight(),
	//g_statsdb.getImgWidth());

	buf->safePrintf("</center>"
		       //"class=\"statsdb_image\">"
		       "</td></tr>\n");

	// the map key
	buf->safePrintf("<tr><td>");
	buf->cat ( st->m_sb2 );
	buf->safePrintf("</td></tr>\n");

	buf->safePrintf( "</table>\n" );
}




void sendReply ( void *state ) {

	s_graphInUse = false;

	StateStatsdb *st = (StateStatsdb *)state;

	if ( g_errno ) {
		g_httpServer.sendErrorReply(st->m_socket,
					    500,mstrerror(g_errno));
		return;
	}

	TcpSocket *s = st->m_socket;

	if(st->m_request.getLong("json", 0)) {
        //xxxxxxxxxxxxxxxxxxxxxxxxx
    }

	if(st->m_request.getLong("justgraph", 0)) {
		SafeBuf buf( 1024*32 , "tmpbuf0" );
		genStatsGraphTable(&buf, st);
		g_statsdb.m_gw.purge();
		g_statsdb.m_dupTable.reset();
		g_httpServer.sendDynamicPage ( s, buf.getBufStart(), buf.length() ); 
		return;
	}

	SafeBuf buf( 1024*32 , "tmpbuf0" );
	SafeBuf tmpBuf( 1024 , "tmpbuf1" );

	//
	// take these out until we need them!
	//
	/*
	// print the top of the page
	tmpBuf.safePrintf( 
			  //"<style type=\"text/css\">"
			  //"@import url(/styles/statsdb.css);</style>\n"
		"<script type=\"text/javascript\" "
		"src=\"/scripts/statsdb.js\"></script>\n"
		"<!-- DHTML Calendar -->"
		"<style type=\"text/css\">"
		"@import url(/jsc/calendar-win2k-1.css);"
		"</style>\n"
		"<script type=\"text/javascript\" "
		"src=\"/jsc/calendar.js\"></script>\n"
		"<script type=\"text/javascript\" "
		"src=\"/jsc/lang/calendar-en.js\"></script>\n"
		"<script type=\"text/javascript\" "
		"src=\"/jsc/calendar-setup.js\"></script>\n"
	);
	*/

	// make the query string
	char qs[1024];
	sprintf(qs,"&date_period=%"INT32"&date_units=%"INT32"&samples=%"INT32"",
		st->m_datePeriod,
		st->m_dateUnits,
		st->m_samples);

	// print standard header
	g_pages.printAdminTop ( &buf , st->m_socket , &st->m_request ,
				qs );

	buf.cat ( tmpBuf );

	//g_pages.printAdminTop2 ( &buf , st->m_socket , &st->m_request, NULL ,
	//			 tmpBuf.getBufStart(), tmpBuf.length() ); 

	// Debug print of CGI parameters and errors
	char startTimeStr[30];
	char endTimeStr[30];

	strncpy( startTimeStr, ctime( &st->m_startDate ), 30 );
	strncpy( endTimeStr, ctime( &st->m_endDate ), 30 );

	buf.safePrintf(
		       "<b>Graph of various query performance statistics.</b>"
		       "<br>"
		       "<br>"
		       );


	buf.safePrintf("<center id=\"graph-container\">\n");

	genStatsGraphTable(&buf, st);
	buf.safePrintf("</center>");

	// write the controls section of the page
	writeControls( &buf, st );

	// print the bottom of the page
	g_pages.printAdminBottom2( &buf );
	
	g_errno = 0;
	mdelete ( st, sizeof(StateStatsdb), "PageStatsdb" );
	delete st;

	g_httpServer.sendDynamicPage ( s, buf.getBufStart(), buf.length() );
}


void writeControls ( SafeBuf *buf, StateStatsdb *st ) {

	// Print the controls.
	struct tm *tmBuild;
	buf->safePrintf (
		"<div id=\"control\">\n"
		"<div id=\"controls\" class=\"show\">\n"
	);

	g_pages.printFormTop( buf, &st->m_request );

	// Print the start date (most recent date)
	buf->safePrintf (
		"<div id=\"time_limits\" class=\"section\">\n"
		//"<div class=\"section_header\">"
		//"<span class=\"section_title\">Time Selection"
		//"</span>"
		//"</div>\n"
		"<table>\n"
		"<tr>"
		"<td>Moving Average Samples</td>"
		"<td>"
		"<input type=text name=samples length=20 value=\"%"INT32"\">"
		"</td>"
		"</tr>"
		"<tr class=\"show\" id=\"e_date_start\">\n"
		"<td> Start </td>\n"
		"<td>\n",
		st->m_samples
	);

	tmBuild = localtime( &st->m_startDate );
	buf->safePrintf ( 
		"<input type=\"text\" name=\"sdate\" id=\"s_date_field\" "
		"value=\"%02d/%02d/%04d %02d:%02d\" />\n",
		tmBuild->tm_mon + 1, tmBuild->tm_mday, tmBuild->tm_year + 1900,
		tmBuild->tm_hour, tmBuild->tm_min
	);

	buf->safePrintf (
		"<button type=\"reset\" name=\"strigger\" "
		"id=\"s_date_trigger\">...</button>\n"
		"<script type=\"text/javascript\">\n"
		// "Calendar.setup({\n"
		// "inputField     :    \"s_date_field\"	,\n"
		// "ifFormat       :    \"%%m/%%d/%%Y %%H:%%M\"	,\n"
		// "showsTime      :    true		,\n"
		// "button         :    \"s_date_trigger\"	,\n"
		// "singleClick    :    false		,\n"
		// "step           :    1			,\n"
		// "timeFormat     :    \"24\"\n"
		// "});\n"
		"</script>\n"
		"</td>\n"
		"</tr>\n"
		);

	// "start at current time"
	buf->safePrintf("<tr><td>");
	buf->safePrintf("<input type=\"checkbox\" name=\"date_now\" "
			"id=\"date_now_trigger\" value=\"1\" "
			"onclick=\"javascript:st_date_now()\"");
	if ( st->m_now ) buf->safePrintf( " checked" );
	buf->safePrintf (
		" /> Start at Current Time\n"
		"</td>\n"
		"</tr>\n");




	buf->safePrintf(
		"<tr class=\"show\" id=\"e_date_period\">\n"
		"<td> Go back </td>\n"
		"<td>\n"
		"<input type=\"text\" name=\"date_period\" value=\"%i\" "
		"size=\"3\" />\n"
		"<select name=\"date_units\">\n"
		"<option value=1" ,
		(int)st->m_datePeriod);

	if  ( st->m_dateUnits == 1 ) // SECS_PER_MINUTE )
		buf->safePrintf( " selected " );

	buf->safePrintf(">seconds</option>\n");

	buf->safePrintf("<option value=60");

	// Print the time unit selection drop down menu.
	// - gives the user to go back from the selected
	//   time by x number of minutes/hours/days
	if  ( st->m_dateUnits == 60 ) // SECS_PER_MINUTE )
		buf->safePrintf( " selected " );

	buf->safePrintf (
		">minute(s)</option>\n"
		"<option value=\"%d\"",
		(int)3600);//SECS_PER_HOUR

	if  ( st->m_dateUnits == 3600 ) // SECS_PER_HOUR )
		buf->safePrintf( " selected " );
		
	buf->safePrintf (
		">hour(s)</option>\n"
		"<option value=\"%d\"",
		3600*24);//(int)SECS_PER_DAY

	if  ( st->m_dateUnits == 3600*24 )// SECS_PER_DAY )
		buf->safePrintf( " selected " );
		
	// Print the end date selector.
	buf->safePrintf (
		">day(s)</option>\n"
		"</select>\n"
		"</td>\n"
		"</tr>\n" );

	buf->safePrintf(
		"<tr class=\"show\" id=\"e_date_custom\">\n"
		"<td> Go back to </td>\n"
		"<td>\n"//,
		//st->m_datePeriod
	);

	tmBuild = localtime( &st->m_endDate );
	buf->safePrintf (
		"<input type=\"text\" name=\"edate\" id=\"e_date_field\" "
		"value=\"%02d/%02d/%04d %02d:%02d\" />\n",
		tmBuild->tm_mon + 1, tmBuild->tm_mday, tmBuild->tm_year + 1900,
		tmBuild->tm_hour, tmBuild->tm_min

	);

	buf->safePrintf (
		"<button type=\"reset\" name=\"etrigger\" "
		"id=\"e_date_trigger\">...</button>\n"
        "<style>"
        ".hidden {display:none;}"
        "</style>"
		"<script type=\"text/javascript\">\n"
		// "Calendar.setup({\n"
		// "inputField     :    \"e_date_field\"	,\n"
		// "ifFormat       :    \"%%m/%%d/%%Y %%H:%%M\"	,\n"
		// "showsTime      :    true		,\n"
		// "button         :    \"e_date_trigger\"	,\n"
		// "singleClick    :    false		,\n"
		// "step           :    1			,\n"
		// "timeFormat     :    \"24\"\n"
		// "});\n"
		"function hideColor(val, checked) {"
		"  var elmsToToggle = document.querySelectorAll('.color-'+ val);"
		"  for(var i = 0; i < elmsToToggle.length; i++) {"
        "    if(checked) {"
        "      elmsToToggle[i].className = elmsToToggle[i].className.replace(' hidden', '');"
        "    } else {"
        "      elmsToToggle[i].className = elmsToToggle[i].className + ' hidden';"
        "    } "
        "  } "
		"}"
		"function toggleVisible(ev) {"
		"  var val = ev.target.value;"
		"  var checked = ev.target.checked;"
		"  window.localStorage.setItem(val, checked);"
		"  console.log('toggling', val, checked , 	  window.localStorage.getItem(val));"
		"  hideColor(val, checked)"
		"}"

		"function initToggles() {"
		"  var graphToggles = document.querySelectorAll('.graph-toggles');"
		"  for(var i = 0; i < graphToggles.length; i++) {"
		"    graphToggles[i].addEventListener('click', toggleVisible);"
		"    if(window.localStorage.getItem(graphToggles[i].value) == 'false') {"
		"      graphToggles[i].checked = false;"
		"      hideColor(graphToggles[i].value, false);"
		"    } else {"
		"      graphToggles[i].checked = true;"
		"    }"
		"    console.log('xxxx', graphToggles[i].value, 'yyy', window.localStorage.getItem(graphToggles[i].value));"
		"  }"
		"} "
		"function callAjax(url, callback) { "
		"    var xmlhttp;"
		"    xmlhttp = new XMLHttpRequest();"
		"    xmlhttp.onreadystatechange = function() {"
		"        if (xmlhttp.readyState == 4 && xmlhttp.status == 200) {"
		"            callback(xmlhttp.responseText);"
		"        } "
		"    }; "
		"    xmlhttp.open('GET', url, true);"
		"    xmlhttp.send();"
		"} "
		"initToggles();"
		
		"function refreshGraph() {"
		"  var autoUpdate = document.querySelector('#auto_update_trigger');"
		"  if(!autoUpdate.checked) return;"
		"  callAjax(document.location + '&justgraph=1&dontlog=1', function(elm) {"
		"    var gc = document.querySelector('#graph-container');"
		"    gc.innerHTML = elm;"
		"    initToggles();"
		"  });"
		"}"
		"setInterval(refreshGraph, 2000);"


		"</script>\n"
		"</td>\n"
		"</tr>\n"
		//"<tr class=\"show\" id=\"e_custom_trigger\">\n"
		//"<td colspan=\"2\">\n"
		//"<input type=\"checkbox\" name=\"custom\" "
		//"id=\"date_custom_trigger\" value=\"1\" "
		//"onclick=\"javascript:st_date_custom()\""
	);

	/*
	// This checkbox enables the end date dialog, which will
	// be used rather than the time period dialog.
	if ( st->m_dateCustom ) buf->safePrintf( " checked" );
	
	buf->safePrintf (
		" /> Custom End Time\n"
		"</td>\n"
		"</tr>\n"
		"<tr class=\"show\" id=\"e_show_trigger\">\n"
		"<td colspan=\"2\">\n"
		"<input type=\"checkbox\" name=\"date_now\" "
		"id=\"date_now_trigger\" value=\"1\" "
		"onclick=\"javascript:st_date_now()\""
	);

	if ( st->m_now ) buf->safePrintf( " checked" );

	buf->safePrintf (
		" /> Start at Current Time\n"
		"</td>\n"
		"</tr>\n");
	*/

	buf->safePrintf(
		"<tr class=\"show\" id=\"e_auto_trigger\">\n"
		"<td colspan=\"2\">\n"
		"<input type=\"checkbox\" name=\"auto_update\" "
		"id=\"auto_update_trigger\" value=\"1\" "
		//"onclick=\"javascript:st_auto_update()\""
	);

	if ( st->m_autoUpdate ) buf->safePrintf( " checked" );

	buf->safePrintf (
		" /> Auto Update Stats\n"
	);
	
	buf->safePrintf (
		"</td>\n"
		"</tr>\n"
		"</table>\n"
		"<input type=\"hidden\" name=\"genstats\" value=\"1\" />\n"
	);

	g_pages.printFormData( buf, st->m_socket, &st->m_request );

	buf->safePrintf (
		"<input type=\"submit\" name=\"action\" value=\"submit\" />"
		"</div>\n</form>\n</div>\n</div>\n"
	);

	// This checkbox pulls the current time from the server,
	// and uses it for the request. Can only use the time
	// period dialog when this is selected.
	buf->safePrintf (
		"</table>\n"
	);

	buf->safePrintf ("</div>\n");

	/*
	buf->safePrintf (
		"<div id=\"stat_selection\" class=\"section\">\n"
		"<div class=\"section_header\">"
		"<span class=\"section_title\">Stat Selection"
		"</span></div>\n"
		"<table>\n"
		"<tr>\n"
		"<td> Host(s) </td>\n"
		"<td>\n"
		"<select name=\"host\"> \n"
		"<option value=\"-3\""
	);

	// Print the host selector.
	if ( st->m_hostId == -3 ) buf->safePrintf ( " selected " );
	buf->safePrintf ( ">This Host</option>\n<option value=\"-2\"");

	if ( st->m_hostId == -2 ) buf->safePrintf ( " selected " );
	buf->safePrintf ( ">All</option>\n<option value=\"-1\"");

	if ( st->m_hostId == -1 ) buf->safePrintf ( " selected " );
	buf->safePrintf ( ">Sample</option>\n");

	for (int32_t i = 0; i < g_hostdb.getNumHosts(); i++) {
		buf->safePrintf ( "<option value=\"%"INT32"\"", i );
		if ( st->m_hostId == i ) buf->safePrintf ( " selected " );
		buf->safePrintf ( ">Host %"INT32"</option>\n", i );
	}

	// Print the statistic selector.
	buf->safePrintf (
		"</select>\n"
		"</td>\n"
		"</tr>\n"
		//"<tr class=\"show\" id=\"e_stat_single\">\n"
		//"<td> Statistic </td>\n"
		//"<td>\n"
		//"<select name=\"stat\"> \n"
		//"<option value=\"-1\""
	);

	if ( st->m_statIndex == -1 ) buf->safePrintf( " selected " );

	buf->safePrintf (
		">List (one snapshot)</option>\n"
	);

	for (int32_t i = 0; (uint32_t)i < g_statsdb.m_numStats; i++) {
		buf->safePrintf ( "<option value=\"%i\"", i );
		if ( st->m_statIndex == i ) buf->safePrintf ( " selected " );
		buf->safePrintf ( ">%s</option>\n",
				  g_statsdb.m_stats[i].m_title );
	}

	buf->safePrintf (
		"</select>"
		"</td>\n"
		"</tr>\n"
		"<tr class=\"show\" id=\"e_multi_trigger\">\n"
		"<td colspan=\"2\">\n"
		"<input type=\"checkbox\" name=\"multi_stat\" "
		"id=\"multi_stat_trigger\" value=\"1\" "
		"onclick=\"javascript:st_multi_stat()\""
	);

	if ( st->m_multiStat ) buf->safePrintf( " checked" );

	buf->safePrintf (
		" /> Select Multiple Stats\n"
		"</td>\n"
		"</tr>\n"
		"<tr class=\"show\" id=\"e_stat_multi\">\n"
		"<td colspan=\"2\">\n"
		"<select name=\"stats\" multiple size=\"5\"> \n"
	);

	for (int32_t i = 0; (uint32_t)i < g_statsdb.m_numStats; i++) {
		buf->safePrintf ( "<option value=\"%i\"", i );
		if ( st->m_statListOrig.exists(i) )
			buf->safePrintf ( " selected " );
		buf->safePrintf ( ">%s</option>\n",
				  g_statsdb.m_stats[i].m_title );
	}

	buf->safePrintf (
			 "</select>"
			 "</td>\n"
			 "</tr>\n"
			 "</table>\n"
			 "</div>\n" );

		
	// Print the graph type selection, can select one of:
	// . Line  - just a line drawn through each point
	// . Fill  - like the above, but fills under the line
	// . Block - uses vertical bars to draw the graph
	buf->safePrintf (
		"<div id=\"appearance\" class=\"section\">\n"
		"<div class=\"section_header\">"
		"<span class=\"section_title\">Display Properties"
		"</span></div>\n"
		"<table>\n"
		"<tr>\n"
		"<td>Graph Style</td>\n"
		"<td>\n"
		"<input type=\"radio\" name=\"graph_style\" value=\"line\""
	);

	if ( st->m_linePlot )  buf->safePrintf( " checked" );

	buf->safePrintf (
		" />"
		"Line"
		"&nbsp;\n"
		"<input type=\"radio\" name=\"graph_style\" value=\"fill\""
	);

	if ( st->m_lineFill )  buf->safePrintf( " checked" );

	buf->safePrintf (
		" />"
		"Fill"
		"&nbsp;\n"
		"<input type=\"radio\" name=\"graph_style\" value=\"block\""
	);

	if ( st->m_lineBlock )  buf->safePrintf( " checked" );

	buf->safePrintf (
		" />"
		"Block\n"
		"</td>\n"
		"</tr>\n"
		"<tr>\n"
		"<td>GIF Size</td>\n"
		"<td>\n"
		"<select name=\"graph_dim\">\n"
	);

	// Print out the graph dimesion selector.
	uint16_t width  = 0;
	uint16_t height = 0;
	bool dimPrinted = false;
	for ( int i = 0; i < GIF_NUM_DIM; i++ ) {
		width = s_widths[i];
		height = s_heights[i];
		if ( !dimPrinted && (st->m_gifWidth < width || 
		     ( (st->m_gifWidth == width) &&
		       (st->m_gifHeight < height) )) ) {
			buf->safePrintf(
				"<option selected >%dx%d</option>\n",
				st->m_gifWidth, st->m_gifHeight
			);
			dimPrinted = true;
		}

		buf->safePrintf ( "<option ");
		if ( (st->m_gifHeight == height) && 
		     (st->m_gifWidth == width) ) {
			buf->safePrintf ( " selected ");
			dimPrinted = true;
		}
		buf->safePrintf (
			">%dx%d</option>\n",
			s_widths[i], s_heights[i]
		);
	}
	buf->safePrintf (
		"</select>\n"
		"</td>\n"
		"</tr>\n"
		"<tr>\n"
		"<td>Columns</td>\n"
		"<td>\n"
		"<select name=\"columns\">\n"
	);

	buf->safePrintf(
		"</select>\n"
		"</td>\n"
		"</tr>\n" );

	*/

	//for ( uint32_t i=MIN_COLUMNS; i <= MAX_COLUMNS; i++ ) {
	//	buf->safePrintf( "<option" );
	//	if ( st->m_columns == i )
	//		buf->safePrintf ( " selected" );
	//	buf->safePrintf( ">%u</option>\n", i );
	//}


}



time_t genDate( char *date, int32_t dateLen ) {

	time_t result = -1;
	// the date string should always be the same length
	if ( ! date || dateLen != 16 )
		return result;

	struct tm tmRef;
	struct tm tmBuild;

	//*
	memset( (char *)&tmRef, 0, sizeof( tmRef ) );
	time_t now = (time_t)getTimeGlobalNoCore();
	localtime_r( &now, &tmRef );
	now = mktime( &tmRef );
	// */

	char tmp[18];
	char *p = tmp;
	gbmemcpy( p, date, dateLen );

	p[2]  = '\0';
	p[5]  = '\0';
	p[10] = '\0';
	p[13] = '\0';
	p[16] = '\0';

	memset( (char *)&tmBuild, 0, sizeof( tmBuild ) );
	tmBuild.tm_mon   = atoi( p ) - 1;	p += 3;
	tmBuild.tm_mday  = atoi( p );		p += 3;
	tmBuild.tm_year  = atoi( p ) - 1900;	p += 5;
	tmBuild.tm_hour  = atoi( p );		p += 3;
	tmBuild.tm_min   = atoi( p );		p += 3;
	tmBuild.tm_isdst = tmRef.tm_isdst;	p += 3;

	// We must manually adjust for DST difference
	// if the current state of DST does not match
	// that of the date that was requested.
	/*
	struct tm nowDST;
	struct tm resultDST;
	localtime_r( &now, &nowDST );
	localtime_r( &result, &resultDST );
	if ( nowDST.tm_isdst && !resultDST.tm_isdst )
		tmBuild.tm_hour++;
	else if ( !nowDST.tm_isdst && resultDST.tm_isdst )
		tmBuild.tm_hour--;

	gbmemcpy( p, date, dateLen );
	p[16] = '\0';
	log ( LOG_DEBUG, "stats: user string        [%s]", p );
	log ( LOG_DEBUG, "stats: user provided time [%s]", ctime( &result ) );
	log ( LOG_DEBUG, "stats: our timestamp      [%s]", ctime( &now    ) );
	// */
	
	result = mktime( &tmBuild );
	return result;
}
