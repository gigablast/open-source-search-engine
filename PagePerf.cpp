#include "gb-include.h"

#include "Stats.h"
#include "Pages.h"

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPagePerf ( TcpSocket *s , HttpRequest *r ) {
	// if ip is not from matt wells, don't print this stuff, too sensitive
	//int32_t matt1 = atoip ( MATTIP1 , gbstrlen(MATTIP1) );
	//int32_t matt2 = atoip ( MATTIP2 , gbstrlen(MATTIP2) );
	// allow connection if i'm running this on lenny, too
	//if ( s->m_ip != matt1 && s->m_ip != matt2 )
	//	return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	//int32_t refreshLen = 0;
	//if(r->getString ( "refresh" , &refreshLen) ) {
	//	g_stats.dumpGIF ();
	//	return g_httpServer.sendDynamicPage ( s , "x", 1 );
	//}

	// don't allow pages bigger than 128k in cache
	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);
	p.setLabel ( "perfgrph" );

	// print standard header
	g_pages.printAdminTop ( &p , s , r );



	// password, too
	//int32_t pwdLen = 0;
	//char *pwd = r->getString ( "pwd" , &pwdLen );

	int32_t autoRefresh = r->getLong("rr", 0);
	if(autoRefresh > 0) {
		p.safePrintf("<script language=\"JavaScript\"><!--\n ");

		p.safePrintf( "\nfunction timeit() {\n"
			     " setTimeout('loadXMLDoc(\"%s&refresh=1"
			     "&dontlog=1\")',\n"
			     " 500);\n }\n ",  
			     r->getRequest() + 4/*skip over GET*/); 
		p.safePrintf(
		     "var req;"
		     "function loadXMLDoc(url) {"
		     "    if (window.XMLHttpRequest) {"
		     "        req = new XMLHttpRequest();" 
		     "        req.onreadystatechange = processReqChange;"
		     "        req.open(\"GET\", url, true);"
		     "        req.send(null);"
		     "    } else if (window.ActiveXObject) {"
		     "        req = new ActiveXObject(\"Microsoft.XMLHTTP\");"
		     "        if (req) {"
		     "            req.onreadystatechange = processReqChange;"
		     "            req.open(\"GET\", url, true);"
		     "            req.send();"
		     "        }"
		     "    }"
		     "}"
		     "function processReqChange() {"
		     "    if (req.readyState == 4) {"
		     "        if (req.status == 200) {"
		     "        var uniq = new Date();"
		     "        uniq.getTime();"
		     "   document.diskgraph.src=\"/diskGraph%"INT32".gif?\" + uniq;"
		     "        timeit();"
		     "    } else {"
	     //   "            alert(\"There was a problem retrieving \");"
		     "        }"
		     "    }"
		     "} \n ",g_hostdb.m_hostId);

		p.safePrintf( "// --></script>");
	}



	// dump stats to /tmp/diskGraph.gif
	//g_stats.dumpGIF ();

	if(autoRefresh > 0) 
		p.safePrintf("<body onLoad=\"timeit();\">"); 


	//get the 'path' part of the request.
	char rbuf[1024];
	if(r->getRequestLen() > 1023) {
		gbmemcpy( rbuf, r->getRequest(), 1023);
	}
	else {
		gbmemcpy( rbuf, r->getRequest(), r->getRequestLen());
	}
	char* rbufEnd = rbuf;
	//skip GET
	while (!isspace(*rbufEnd)) rbufEnd++;
	//skip space(s)
	while (isspace(*rbufEnd)) rbufEnd++;
	//skip request path
	while (!isspace(*rbufEnd)) rbufEnd++;
	*rbufEnd = '\0';
	//char* refresh = strstr(rbuf, "&rr=");


	// print resource table
	// columns are the dbs
	p.safePrintf(
		       //"<center>Disk Statistics<br><br>"
		       "<center>"
		       //"<br>"
		       //"<img name=\"diskgraph\" 
		       //src=/diskGraph%"INT32".gif><br><br>",
		       //g_hostdb.m_hostId );
		     );

	// now try using absolute divs instead of a GIF
	g_stats.printGraphInHtml ( p );

	/*
	if(autoRefresh > 0) {
		if(refresh) *(refresh+4) = '0';
		p.safePrintf(
			     "<center><a href=\"%s\">Auto Refresh Off</a>"
			     "</center>",
			     rbuf + 4);  // skip over GET
		p.safePrintf( "<input type=\"hidden\" "
			      "name=\"dontlog\" value=\"1\">");
		
	}
	else {
		char* rr = "";
		if(refresh) *(refresh+4) = '1';
		else rr = "&rr=1";
		p.safePrintf(
			     "<center><a href=\"%s%s\">Auto Refresh</a>"
			     "</center>",
			     rbuf + 4, rr);  // skip over "GET "
	}
	*/

	// print the key
	p.safePrintf (
		      "<br>"
		       "<center>"
		       //"<table %s>"
		       //"<tr>%s</tr></table>"

		       "<style>"
		       ".poo { background-color:#%s;}\n"
		       "</style>\n"


		       "<table %s>"

		       // black
		       "<tr class=poo>"
		       "<td bgcolor=#000000>&nbsp; &nbsp;</td>"
		       "<td> High priority disk read. "
		       "Thicker lines for bigger reads.</td>"

		       // grey
		       "<td bgcolor=#808080>&nbsp; &nbsp;</td>"
		       "<td> Low priority disk read. "
		       "Thicker lines for bigger reads.</td>"
		       "</tr>"


		       // red
		       "<tr class=poo>"
		       "<td bgcolor=#ff0000>&nbsp; &nbsp;</td>"
		       "<td> Disk write. "
		       "Thicker lines for bigger writes.</td>"

		       // light brown
		       "<td bgcolor=#b58869>&nbsp; &nbsp;</td>"
		       "<td> Processing end user query. No raw= parm.</td>"
		       "</tr>"


		       // dark brown
		       "<tr class=poo>"
		       "<td bgcolor=#753d30>&nbsp; &nbsp;</td>"
		       "<td> Processing raw query. Has raw= parm.</td>"

		       // blue
		       "<td bgcolor=#0000ff>&nbsp; &nbsp;</td>"
		       "<td> Summary extraction for one document.</td>"
		       "</tr>"


		       // pinkish purple
		       "<tr class=poo>"
		       "<td bgcolor=#aa00aa>&nbsp; &nbsp;</td>"
		       "<td> Send data over network. (low priority)"
		       "Thicker lines for bigger sends.</td>"

		       // yellow
		       "<td bgcolor=#aaaa00>&nbsp; &nbsp;</td>"
		       "<td> Read all termlists (msg2). (low priority)"
		       "Thicker lines for bigger reads.</td>"
		       "</tr>"

		       // pinkish purple
		       "<tr class=poo>"
		       "<td bgcolor=#ff00ff>&nbsp; &nbsp;</td>"
		       "<td> Send data over network.  (high priority)"
		       "Thicker lines for bigger sends.</td>"

		       // light yellow
		       "<td bgcolor=#ffff00>&nbsp; &nbsp;</td>"
		       "<td> Read all termlists (msg2). (high priority)"
		       "Thicker lines for bigger reads.</td>"
		       "</tr>"


		       // dark purple
		       "<tr class=poo>"
		       "<td bgcolor=#8220ff>&nbsp; &nbsp;</td>"
		       "<td> Get all summaries for results.</td>"

		       // turquoise
		       "<td bgcolor=#00ffff>&nbsp; &nbsp;</td>"
		       "<td> Merge multiple disk reads. Real-time searching. "
		       "Thicker lines for bigger merges.</td>"
		       "</tr>"


		       // white
		       "<tr class=poo>"
		       "<td bgcolor=#ffffff>&nbsp; &nbsp;</td>"
		       "<td> Uncompress cached document.</td>"

		       // orange
		       "<td bgcolor=#fea915>&nbsp; &nbsp;</td>"
		       "<td> Parse a document. Blocks CPU.</td>"
		       "</tr>"


		       // bright green
		       "<tr class=poo>"
		       "<td bgcolor=#00ff00>&nbsp; &nbsp;</td>"
		       "<td> Compute search results. "
		       "All terms required. rat=1.</td>"

		       // dark green
		       "<td bgcolor=#008000>&nbsp; &nbsp;</td>"
		       "<td> Compute search results. "
		       "Not all terms required. rat=0.</td>"
		       "</tr>"

		       // bright green
		       "<tr class=poo>"
		       "<td bgcolor=#ccffcc>&nbsp; &nbsp;</td>"
		       "<td> Inject a document"
		       "</td>"

		       // dark green
		       "<td bgcolor=#FFFACD>&nbsp; &nbsp;</td>"
		       "<td> Compute related pages. "
		       "</td>"
		       "</tr>"

		       "<tr class=poo>"

		       "<td bgcolor=#d1e1ff>&nbsp; &nbsp;</td>"
		       "<td> Compute Gigabits. "
		       "</td>"

		       "<td bgcolor=#009fe5>&nbsp; &nbsp;</td>"
		       "<td> Quick Poll. "
		       "</td>"

		       "</tr>"


		       "<tr class=poo>"

		       "<td bgcolor=#0000b0>&nbsp; &nbsp;</td>"
		       "<td> \"Summary\" extraction (low priority) "
		       "</td>"

		       "<td bgcolor=#ffffff>&nbsp; &nbsp;</td>"
		       "<td> &nbsp; "
		       "</td>"

		       "</tr>"
		       

		       "</table>"
		       "</center>"
		       , LIGHT_BLUE 
		       , TABLE_STYLE
		       //,g_stats.m_keyCols.getBufStart() && 
		       //g_conf.m_dynamicPerfGraph ? 
		       //g_stats.m_keyCols.getBufStart() : ""
		       );

	if(autoRefresh > 0) p.safePrintf("</body>"); 

	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );

	int32_t bufLen = p.length();
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage ( s, p.getBufStart(), bufLen );
}
