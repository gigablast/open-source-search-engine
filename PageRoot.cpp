#include "gb-include.h"

#include "Indexdb.h"     // makeKey(long long docId)
#include "Titledb.h"
#include "Spider.h"
#include "Tagdb.h"
#include "Dns.h"
//#include "PageResults.h" // for query buf, g_qbuf
#include "Collectiondb.h"
#include "CollectionRec.h"
#include "Clusterdb.h"    // for getting # of docs indexed
//#include "Checksumdb.h"   // should migrate to this one, though
#include "Pages.h"
#include "Query.h"        // MAX_QUERY_LEN
#include "SafeBuf.h"
#include "LanguageIdentifier.h"
#include "LanguagePages.h"
#include "Users.h"
#include "Address.h" // getIPLocation
#include "Proxy.h"

//char *printNumResultsDropDown ( char *p, long n, bool *printedDropDown);
bool printNumResultsDropDown ( SafeBuf& sb, long n, bool *printedDropDown);
//static char *printTopDirectory ( char *p, char *pend );
static bool printTopDirectory ( SafeBuf& sb );

// this prints the last five queries
//static long printLastQueries ( char *p , char *pend ) ;

//static char *expandRootHtml ( char *p    , long plen    ,
/*
static bool expandRootHtml  ( SafeBuf& sb,
			      uint8_t *html , long htmlLen ,
			      char *q    , long qlen    ,
			      HttpRequest *r ,
			      TcpSocket   *s ,
			      long long docsInColl ,
			      CollectionRec *cr ) ;
*/

bool sendPageRoot ( TcpSocket *s, HttpRequest *r ){
	return sendPageRoot ( s, r, NULL );
}

bool printNav ( SafeBuf &sb , HttpRequest *r ) {

	char *root       = "";
	char *rootSecure = "";
	if ( g_conf.m_isMattWells ) {
		root       = "http://www.gigablast.com";
		rootSecure = "https://www.gigablast.com";
	}

	sb.safePrintf("<center><b><p class=nav>"
		      "<a href=%s/about.html>About</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/contact.html>Contact</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/help.html>Help</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/privacy.html>Privacy Policy</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/searchfeed.html>Search API</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/seoapi.html>SEO API</a>"
		      " &nbsp; &nbsp; "
		      "<a href=%s/account>My Account</a> "
		      , root
		      , root
		      , root
		      , root
		      , root
		      , root
		      , rootSecure

		      //" &nbsp; &nbsp; <a href=/logout>Logout</a>"
		      );
	if ( r->isLocal() )
	     sb.safePrintf("&nbsp; &nbsp;[<a href=\"/crawlbot?\">Admin</a>]");
	sb.safePrintf("</p></b></center></body></html>");
	return true;
}

bool printWebHomePage ( SafeBuf &sb , HttpRequest *r ) {

	sb.safePrintf("<html>\n");
	sb.safePrintf("<head>\n");
	//sb.safePrintf("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf8\">");
	sb.safePrintf("<meta name=\"description\" content=\"A powerful, new search engine that does real-time indexing!\">\n");
	sb.safePrintf("<meta name=\"keywords\" content=\"search, search engine, search engines, search the web, fresh index, green search engine, green search, clean search engine, clean search\">\n");
	sb.safePrintf("<title>Gigablast - "
		      "An Alternative Open Source Search Engine</title>\n");
	sb.safePrintf("<style><!--\n");
	sb.safePrintf("body {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("margin: 20px 5px;\n");
	sb.safePrintf("letter-spacing: 0.04em;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("a:link {color:#00c}\n");
	sb.safePrintf("a:visited {color:#551a8b}\n");
	sb.safePrintf("a:active {color:#f00}\n");
	sb.safePrintf(".bold {font-weight: bold;}\n");
	sb.safePrintf(".bluetable {background:#d1e1ff;margin-bottom:15px;font-size:12px;}\n");
	sb.safePrintf(".url {color:#008000;}\n");
	sb.safePrintf(".cached, .cached a {font-size: 10px;color: #666666;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("table {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("}\n");
	sb.safePrintf(".directory {font-size: 16px;}\n");
	sb.safePrintf("-->\n");
	sb.safePrintf("</style>\n");
	sb.safePrintf("\n");
	sb.safePrintf("</head>\n");
	sb.safePrintf("<script>\n");
	sb.safePrintf("<!--\n");
	sb.safePrintf("function x(){document.f.q.focus();}\n");
	sb.safePrintf("// --></script>\n");
	sb.safePrintf("<body onload=\"x()\">\n");
	//sb.safePrintf("<body>\n");
	//g_proxy.insertLoginBarDirective ( &sb );
	sb.safePrintf("<br><br>\n");
// try to avoid using https for images. it is like 10ms slower.

	if ( g_conf.m_isMattWells )
		sb.safePrintf("<center><a href=/><img border=0 width=500 "
			      "height=122 src=http://www.gigablast.com/logo-"
			      "med.jpg></a>\n");
	else
		sb.safePrintf("<center><a href=/><img border=0 width=500 "
			      "height=122 src=/logo-med.jpg></a>\n");

	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");
	sb.safePrintf("<b>web</b> &nbsp;&nbsp;&nbsp;&nbsp; "
		      "<a href=http://www.gigablast.com/seo>seo</a> "
		      "&nbsp;&nbsp;&nbsp;&nbsp; "
		      "<a href=\"/Top\">directory</a> "
		      "&nbsp;&nbsp;&nbsp;&nbsp; \n");
	sb.safePrintf("<a href=/adv.html>advanced search</a>");
	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; ");
	sb.safePrintf("<a href=/addurl title=\"Instantly add your url to "
		      "Gigablast's index\">add url</a>");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");
	// submit to https now
	sb.safePrintf("<form method=get "
		      "action=/search name=f>\n");
	sb.safePrintf("<input name=q type=text size=60 value=\"\">&nbsp;<input type=\"submit\" value=\"Search\">\n");
	sb.safePrintf("\n");
	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<table cellpadding=3>\n");
	sb.safePrintf("\n");


	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td align=center><div style=width:50px;height:50px;display:inline-block;background-color:red;></div></td>\n");
	sb.safePrintf("<td><font size=+1><b>Open Source!</b>"
	"</font><br>\n");
	sb.brify2("Gigablast is now available as an <a href=https://github.com/gigablast/open-source-search-engine>open source search engine</a> on github.com. Download it today. Finally a robust, scalable search solution in C/C++ that has been in development and used commercially since 2000. <a href=/admin.html#features>Features.</a> Limited support available for free."
		  ,80);
	sb.safePrintf("<br><br>");
	sb.safePrintf("</td></tr>\n");


	char *root = "";
	if ( g_conf.m_isMattWells )
		root = "http://www.gigablast.com";

	sb.safePrintf("<tr valign=top>\n");
	// 204x143
	sb.safePrintf("<td><img height=52px width=75px "
		      "src=%s/eventguru.png></td>\n"
		      , root );
	sb.safePrintf("<td><font size=+1><b>Event Guru Returns</b></font><br>\n");
	sb.brify2("<a href=http://www.eventguru.com/>Event Guru</a> datamines events from the web. It identifies events on a web page, or even plain text, using the same rules of deduction used by the human mind. It also has Facebook integration and lots of other cool things.",80);
	sb.safePrintf("<br><br></td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");


	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td align=center><div style=width:50px;height:50px;display:inline-block;background-color:green;></div></td>\n");
	sb.safePrintf("<td><font size=+1><b>The Green Search Engine</b></font><br>\n");
	sb.brify2("Gigablast is the only clean-powered web search engine. 90% of its power usage comes from wind energy. Astoundingly, Gigablast is one of ONLY four search engines in the United States indexing over a billion pages.",80);
	sb.safePrintf("<br><br></td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");
	*/


	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td align=center><img src=%s/gears.png "
		      "height=50 width=50></div></td>\n"
		      , root );
	sb.safePrintf("<td><font size=+1><b>The Transparent Search Engine</b></font><br>\n");
	sb.brify2("Gigablast is the first truly transparent search engine. It tells you exactly why the search results are ranked the way they are. There is nothing left to the imagination.",85);
	sb.safePrintf("<br><br>");
	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");

	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td align=center><center><img src=%s/dollargear.png "
		      "height=50 width=50></center></div></center></td>\n"
		      , root );
	sb.safePrintf("<td><font size=+1><b>The SEO Search Engine</b></font><br>\n");
	sb.brify2("When it comes to search-engine based SEO, Gigablast is the place to be. With a frothy set of unique and effective <a href=http://www.gigablast.com/seo>SEO tools</a>, you will find all you need to execute a simple yet effective SEO strategy. Stop the guesswork, and let a search engine tell you how to SEO it.",85);
	sb.safePrintf("</td></tr>\n");


	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:ff3030;></td>\n");
	sb.safePrintf("<td><font size=+1><b>Xml Search Feed</b></font><br>\n");
	sb.brify2("Utilize Gigablast's results on your own site or product by connecting with Gigablast's <a href=/searchfeed.html>XML search feed</a>. It's now simpler than ever to setup and use. You can also add the web pages you want into the index in near real-time.",85);
	sb.safePrintf("</td></tr>\n");
	*/

	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:black;></td>\n");
	sb.safePrintf("<td><font size=+1><b>The Private Search Engine</b>"
	"</font><br>\n");
	sb.brify2("Gigablast does not allow the NSA or any third party "
		  "to spy on the queries your IP address is doing, "
		  "unlike "
		  "<a href=http://www.guardian.co.uk/world/2013/jun/"
		  "06/us-tech-giants-nsa-data>"
		  "other large search engines</a>. "
		  "Gigablast is the only "
		  "<a href=/privacy.html>truly private search engine</a> "
		  "in the United States."
		  //" Everyone else has fundamental "
		  //"gaps in their "
		  //"security as explained by the above link."
		  //"Tell Congress "
		  //"to <a href=https://optin.stopwatching.us/>stop spying "
		  //"on you</a>."
		  ,85);
	sb.safePrintf("</td></tr>\n");
	*/

	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:black;></td>\n");
	sb.safePrintf("<td><font size=+1><b>No Tax Dodging</b></font><br>\n");
	sb.brify2("Gigablast pays its taxes when it makes a profit. "
		  "Google and Bing <a href=http://www.bloomberg.com/news/"
		  "2010-10-21/google-2-4-rate-shows-how-60-billion-u-s-"
		  "revenue-lost-to-tax-loopholes.html>do not</a>. They "
		  "stash their profits in "
		  "offshore tax havens to avoid paying taxes. "
		  //"The end result is that taxes are higher for you. "
		  "You may think Google and Bing are free to use, but in "
		  "reality, <u>you</u> pay for it in increased taxes."
		  ,85);
	sb.safePrintf("</td></tr>\n");
	*/


	sb.safePrintf("\n");
	sb.safePrintf("\n");
	sb.safePrintf("</table>\n");
	sb.safePrintf("<br><br>\n");
	printNav ( sb , r );
	return true;
}

bool printAddUrlHomePage ( SafeBuf &sb , char *url , HttpRequest *r ) {

	sb.safePrintf("<html>\n");
	sb.safePrintf("<head>\n");
	sb.safePrintf("<title>Gigablast - Add Url</title>\n");
	sb.safePrintf("<style><!--\n");
	sb.safePrintf("body {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("margin: 20px 5px;\n");
	sb.safePrintf("letter-spacing: 0.04em;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("a:link {color:#00c}\n");
	sb.safePrintf("a:visited {color:#551a8b}\n");
	sb.safePrintf("a:active {color:#f00}\n");
	sb.safePrintf(".bold {font-weight: bold;}\n");
	sb.safePrintf(".bluetable {background:#d1e1ff;margin-bottom:15px;font-size:12px;}\n");
	sb.safePrintf(".url {color:#008000;}\n");
	sb.safePrintf(".cached, .cached a {font-size: 10px;color: #666666;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("table {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("}\n");
	sb.safePrintf(".directory {font-size: 16px;}\n");
	sb.safePrintf("-->\n");
	sb.safePrintf("</style>\n");
	sb.safePrintf("\n");
	sb.safePrintf("</head>\n");
	sb.safePrintf("<script>\n");
	sb.safePrintf("<!--\n");
	sb.safePrintf("function x(){document.f.q.focus();}\n");
	sb.safePrintf("// --></script>\n");
	//sb.safePrintf("<body onload=\"x()\">\n");
	/*
	if ( url ) {
		sb.safePrintf(
			      "<body "
			      "onLoad=\""
			      "var client = new XMLHttpRequest();\n"
			      "client.onreadystatechange = handler;\n"
			      "var url='/addurl?u="
			      );
		sb.urlEncode ( url );
		// propagate "admin" if set
		//long admin = hr->getLong("admin",-1);
		//if ( admin != -1 ) sb.safePrintf("&admin=%li",admin);
		// provide hash of the query so clients can't just pass in
		// a bogus id to get search results from us
		unsigned long h32 = hash32n(url);
		if ( h32 == 0 ) h32 = 1;
		unsigned long long rand64 = gettimeofdayInMillisecondsLocal();
		sb.safePrintf("&id=%lu&rand=%llu';\n"
			      "client.open('GET', url );\n"
			      "client.send();\n"
			      "\">"
			      , h32
			      , rand64
			      );

	}
	else {
		sb.safePrintf("<body>");
	}
	*/
	sb.safePrintf("<body>");


	sb.safePrintf("<script type=\"text/javascript\">\n"
		      "function handler() {\n" 
		      "if(this.readyState == 4 ) {\n"
		      "document.getElementById('msgbox').innerHTML="
		      "this.responseText;\n"
		      //"alert(this.status+this.statusText+"
		      //"this.responseXML+this.responseText);\n"
		      "}}\n"
		      "</script>\n");


	//g_proxy.insertLoginBarDirective ( &sb );

	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");

	if ( g_conf.m_isMattWells )
		sb.safePrintf("<center><a href=/><img border=0 width=500 "
			      "height=122 src=http://www.gigablast.com/logo-"
			      "med.jpg></a>\n");
	else
		sb.safePrintf("<center><a href=/><img border=0 width=500 "
			      "height=122 src=/logo-med.jpg></a>\n");

	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");
	sb.safePrintf("<a href=/>web</a> &nbsp;&nbsp;&nbsp;&nbsp; <a href=http://www.gigablast.com/seo>seo</a> &nbsp;&nbsp;&nbsp;&nbsp; <a href=\"/Top\">directory</a> &nbsp;&nbsp;&nbsp;&nbsp; \n");
	sb.safePrintf("<a href=/adv.html>advanced search</a>");
	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; ");
	sb.safePrintf("<b title=\"Instantly add your url to Gigablast's "
		      "index\">"
		      "add url</b>");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<form method=get action=/addurl name=f>\n");
	sb.safePrintf("<input name=u type=text size=60 value=\"");
	if ( url ) {
		SafeBuf tmp;
		tmp.safePrintf("%s",url);
		// don't let double quotes in the url close our val attribute
		tmp.replace("\"","%22");
		sb.safeMemcpy(&tmp);
	}
	else
		sb.safePrintf("http://");
	sb.safePrintf("\">&nbsp;<input type=\"submit\" value=\"Add Url\">\n");
	sb.safePrintf("\n");

	// if addurl is turned off, just print "disabled" msg
	char *msg = NULL;
	if ( ! g_conf.m_addUrlEnabled ) 
		msg = "Add url is temporarily disabled";
	// can also be turned off in the collection rec
	CollectionRec *cr = g_collectiondb.getRec ( "main" );
	if ( ! cr->m_addUrlEnabled    ) 
		msg = "Add url is temporarily disabled";
	// or if in read-only mode
	if (   g_conf.m_readOnlyMode  ) 
		msg = "Add url is temporarily disabled";
	// if url is non-empty the ajax will receive this identical msg
	// and display it in the div, so do not duplicate the msg!
	if ( msg && ! url )
		sb.safePrintf("<br><br>%s",msg);


	// . the ajax msgbox div
	// . when loaded with the main page for the first time it will
	//   immediately replace its content...
	if ( url ) {
		char *root = "";
		if ( g_conf.m_isMattWells )
			root = "http://www.gigablast.com";
		sb.safePrintf("<br>"
			      "<br>"
			      "<div id=msgbox>"
			      //"<b>Injecting your url. Please wait...</b>"
			      "<center>"
			      "<img src=%s/gears.gif width=50 height=50>"
			      "</center>"
			      "<script type=text/javascript>"
			      //"alert('shit');"
			      "var client = new XMLHttpRequest();\n"
			      "client.onreadystatechange = handler;\n"
			      "var url='/addurl?u="
			      , root );
		sb.urlEncode ( url );
		// propagate "admin" if set
		//long admin = hr->getLong("admin",-1);
		//if ( admin != -1 ) sb.safePrintf("&admin=%li",admin);
		// provide hash of the query so clients can't just pass in
		// a bogus id to get search results from us
		unsigned long h32 = hash32n(url);
		if ( h32 == 0 ) h32 = 1;
		unsigned long long rand64 = gettimeofdayInMillisecondsLocal();
		sb.safePrintf("&id=%lu&rand=%llu';\n"
			      "client.open('GET', url );\n"
			      "client.send();\n"
			      "</script>\n"
			      , h32
			      , rand64
			      );
		sb.safePrintf("</div>\n");
	}

	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");

	printNav ( sb , r );
	return true;
}


bool printDirHomePage ( SafeBuf &sb , HttpRequest *r ) {

	sb.safePrintf("<html>\n");
	sb.safePrintf("<head>\n");
	//sb.safePrintf("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">");
	sb.safePrintf("<meta name=\"description\" content=\"A powerful, new search engine that does real-time indexing!\">\n");
	sb.safePrintf("<meta name=\"keywords\" content=\"search, search engine, search engines, search the web, fresh index, green search engine, green search, clean search engine, clean search\">\n");
	sb.safePrintf("<title>Gigablast</title>\n");
	sb.safePrintf("<style><!--\n");
	sb.safePrintf("body {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("margin: 20px 5px;\n");
	sb.safePrintf("letter-spacing: 0.04em;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("a:link {color:#00c}\n");
	sb.safePrintf("a:visited {color:#551a8b}\n");
	sb.safePrintf("a:active {color:#f00}\n");
	sb.safePrintf(".bold {font-weight: bold;}\n");
	sb.safePrintf(".bluetable {background:#d1e1ff;margin-bottom:15px;font-size:12px;}\n");
	sb.safePrintf(".url {color:#008000;}\n");
	sb.safePrintf(".cached, .cached a {font-size: 10px;color: #666666;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("table {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("}\n");
	sb.safePrintf(".directory {font-size: 16px;}\n");
	sb.safePrintf("-->\n");
	sb.safePrintf("</style>\n");
	sb.safePrintf("\n");
	sb.safePrintf("</head>\n");
	sb.safePrintf("<script>\n");
	sb.safePrintf("<!--\n");
	sb.safePrintf("function x(){document.f.q.focus();}\n");
	sb.safePrintf("// --></script>\n");
	sb.safePrintf("<body onload=\"x()\">\n");
	sb.safePrintf("<body>\n");
	sb.safePrintf("<br><br>\n");
// try to avoid using https for images. it is like 10ms slower.

	if ( g_conf.m_isMattWells )
		sb.safePrintf("<center><a href=/><img border=0 width=500 "
			      "height=122 src=http://www.gigablast.com/logo-"
			      "med.jpg></a>\n");
	else
		sb.safePrintf("<center><a href=/><img border=0 width=500 "
			      "height=122 src=/logo-med.jpg></a>\n");

	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");
	sb.safePrintf("<a href=/>web</a> &nbsp;&nbsp;&nbsp;&nbsp; <a href=http://www.gigablast.com/seo>seo</a> &nbsp;&nbsp;&nbsp;&nbsp; <b>directory</b> &nbsp;&nbsp;&nbsp;&nbsp; \n");
	sb.safePrintf("<a href=http://www.gigablast.com/events>events</a>"
		      " &nbsp;&nbsp;&nbsp;&nbsp; \n");
	sb.safePrintf("<a href=/adv.html>advanced search</a>");
	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; ");
	char *root = "";
	if ( g_conf.m_isMattWells )
		root = "http://www.gigablast.com";
	sb.safePrintf("<a href=%s/addurl title=\"Instantly add your url to "
		      "Gigablast's index\">add url</a>"
		      , root );
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");
	// submit to HTTPS now
	sb.safePrintf("<form method=get "
		      "action=/search name=f>\n");
	sb.safePrintf("<input name=q type=text size=60 value=\"\">&nbsp;<input type=\"submit\" value=\"Search\">\n");
	sb.safePrintf("<input type=hidden "
		      "name=prepend value=\"gbipcatid:2\">");
	sb.safePrintf("\n");
	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");


	printTopDirectory ( sb );

	sb.safePrintf("<br><br>\n");

	printNav ( sb , r);

	return true;
}


// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageRoot ( TcpSocket *s , HttpRequest *r, char *cookie ) {
	// don't allow pages bigger than 128k in cache
	char  buf [ 10*1024 + MAX_QUERY_LEN ];
	// a ptr into "buf"
	//char *p    = buf;
	//char *pend = buf + 10*1024 + MAX_QUERY_LEN - 100 ;
	SafeBuf sb(buf, 10*1024 + MAX_QUERY_LEN);
	// print bgcolors, set focus, set font style
	//p = g_httpServer.printFocus  ( p , pend );
	//p = g_httpServer.printColors ( p , pend );
	//long  qlen;
	//char *q = r->getString ( "q" , &qlen , NULL );
	// insert collection name too
	long collLen;
	char *coll    = r->getString("c",&collLen);
	if ( ! coll || ! coll[0] ) {
		//coll    = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
		collLen = gbstrlen(coll);
	}
	// ensure collection not too big
	if ( collLen >= MAX_COLL_LEN ) { 
		g_errno = ECOLLTOOBIG; 
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); 
	}
	// get the collection rec
	/*
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	uint8_t *hp = NULL;
	long  hpLen;
	long long  docsInColl = -1;
	if ( ! cr ) {
		// use the default 
		Parm *pp = g_parms.getParm ( "hp" );
		if ( ! pp ) {
			g_errno = ENOTFOUND;
			g_msg = " (error: no such collection)";		
			return g_httpServer.sendErrorReply(s,500,
							   mstrerror(g_errno));
		}
		hp       = (uint8_t*)pp->m_def;
		if ( hp ) hpLen = uint8strlen ( hp );
		if ( hpLen <= 0 || ! hp )
			log(LOG_INFO,"http: No root page html present.");
	} else {
		if(cr->m_useLanguagePages) {
			uint8_t lang = g_langId.guessGBLanguageFromUrl(r->getHost());
			if(lang && (hp = g_languagePages.getLanguagePage(lang)) != NULL) {
					hpLen = uint8strlen(hp);
					// Set sort language as well
					// This might not be a good idea, as it
					// overrides any other setting. May be
					// better to let the user agent string
					// tell us what the user wants.
					strcpy(cr->m_defaultSortLanguage,
							getLanguageAbbr(lang));
			}
		}
		if(!hp) {
			hp    = (uint8_t*)cr->m_htmlRoot;
			hpLen = cr->m_htmlRootLen;
		}
		//RdbBase *base = getRdbBase ( RDB_CHECKSUMDB , coll );
		RdbBase *base = getRdbBase ( (uint8_t)RDB_CLUSTERDB , coll );
		if ( base ) docsInColl = base->getNumGlobalRecs();
	}
	*/
	// print the page out
	/*
	expandRootHtml     ( sb, 
			     hp , hpLen ,
			     q , qlen , r , s , docsInColl ,
			     cr );
	*/


	//if ( ! strcmp(coll,"dmoz" ) )
	//	printDirHomePage(sb,r);
	//else
	printWebHomePage(sb,r);


	// . print last 5 queries
	// . put 'em in a table
	// . disable for now, impossible to monitor/control
	//p += printLastQueries ( p , pend );
	// are we the admin?
	//bool isAdmin = g_collectiondb.isAdmin ( r , s );

	// calculate bufLen
	//long bufLen = p - buf;
	// . now encapsulate it in html head/tail and send it off
	// . the 0 means browser caches for however long it's set for
	// . but we don't use 0 anymore, use -2 so it never gets cached so
	//   our display of the # of pages in the index is fresh
	// . no, but that will piss people off, its faster to keep it cached
	//return g_httpServer.sendDynamicPage ( s , buf , bufLen , -1 );
	return g_httpServer.sendDynamicPage ( s,
					      (char*) sb.getBufStart(),
					      sb.length(),
					      // 120 seconds cachetime
					      // don't cache anymore since
					      // we have the login bar at
					      // the top of the page
					      0,//120, // cachetime
					      false,// post?
					      "text/html",
					      200,
					      NULL, // cookie
					      "UTF-8",
					      r);
}

/*
//char *expandRootHtml ( char *p    , long plen ,
bool expandRootHtml (  SafeBuf& sb,
		       uint8_t *head , long hlen ,
		       char *q    , long qlen ,
		       HttpRequest *r ,
		       TcpSocket   *s ,
		       long long docsInColl ,
		       CollectionRec *cr ) {
	//char *pend = p + plen;
	// store custom header into buf now
	//for ( long i = 0 ; i < hlen && p+10 < pend ; i++ ) {
	for ( long i = 0 ; i < hlen; i++ ) {
		if ( head[i] != '%'   ) {
			// *p++ = head[i];
			sb.safeMemcpy((char*)&head[i], 1);
			continue;
		}
		if ( i + 1 >= hlen    ) {
			// *p++ = head[i];
			sb.safeMemcpy((char*)&head[i], 1);
			continue;
		}
		if ( head[i+1] == 'S' ) { 
			// now we got the %S, insert "spiders are [on/off]"
			bool spidersOn = true;
			if ( ! g_conf.m_spideringEnabled ) spidersOn = false;
			if ( ! cr->m_spideringEnabled ) spidersOn = false;
			if ( spidersOn ) 
				sb.safePrintf("Spiders are on");
			else
				sb.safePrintf("Spiders are off");
			// skip over %S
			i += 1;
			continue;
		}

		if ( head[i+1] == 'q' ) { 
			// now we got the %q, insert the query
			char *p    = (char*) sb.getBuf();
			char *pend = (char*) sb.getBufEnd();
			long eqlen = dequote ( p , pend , q , qlen );
			//p += eqlen;
			sb.incrementLength(eqlen);
			// skip over %q
			i += 1;
			continue;
		}
		if ( head[i+1] == 'w' &&
		     head[i+2] == 'h' &&
		     head[i+3] == 'e' &&
		     head[i+4] == 'r' &&
		     head[i+5] == 'e' ) {
			// insert the location
			long whereLen;
			char *where = r->getString("where",&whereLen);
			// get it from cookie as well!
			if ( ! where ) 
				where = r->getStringFromCookie("where",
							       &whereLen);
			// fix for getStringFromCookie
			if ( where && ! where[0] ) where = NULL;
			// skip over the %where
			i += 5;
			// if empty, base it on IP
			if ( ! where ) {
				double lat;
				double lon;
				double radius;
				char *city,*state,*ctry;
				// use this by default
				long ip = r->m_userIP;
				// ip for testing?
				long iplen;
				char *ips = r->getString("uip",&iplen);
				if ( ips ) ip = atoip(ips);
				// returns true if found in db
				char buf[128];
				getIPLocation ( ip ,
						&lat , 
						&lon , 
						&radius,
						&city ,
						&state ,
						&ctry  ,
						buf    ,
						128    ) ;
				if ( city && state )
					sb.safePrintf("%s, %s",city,state);
			}
			else
				sb.dequote (where,whereLen);
			continue;
		}
		if ( head[i+1] == 'w' &&
		     head[i+2] == 'h' &&
		     head[i+3] == 'e' &&
		     head[i+4] == 'n' ) {
			// insert the location
			long whenLen;
			char *when = r->getString("when",&whenLen);
			// skip over the %when
			i += 4;
			if ( ! when ) continue;
			sb.dequote (when,whenLen);
			continue;
		}
		// %sortby
		if ( head[i+1] == 's' &&
		     head[i+2] == 'o' &&
		     head[i+3] == 'r' &&
		     head[i+4] == 't' &&
		     head[i+5] == 'b' &&
		     head[i+6] == 'y' ) {
			// insert the location
			long sortBy = r->getLong("sortby",1);
			// print the radio buttons
			char *cs[5];
			cs[0]="";
			cs[1]="";
			cs[2]="";
			cs[3]="";
			cs[4]="";
			if ( sortBy >=1 && sortBy <=4 )
				cs[sortBy] = " checked";
			sb.safePrintf(
			 "<input type=radio name=sortby value=1%s>date "
			 "<input type=radio name=sortby value=2%s>distance "
			 "<input type=radio name=sortby value=3%s>relevancy "
			 "<input type=radio name=sortby value=4%s>popularity",
			 cs[1],cs[2],cs[3],cs[4]);
			// skip over the %sortby
			i += 6;
			continue;
		}
		if ( head[i+1] == 'e' ) { 
			// now we got the %e, insert the query
			char *p    = (char*) sb.getBuf();
			long  plen = sb.getAvail();
			long eqlen = urlEncode ( p , plen , q , qlen );
			//p += eqlen;
			sb.incrementLength(eqlen);
			// skip over %e
			i += 1;
			continue;
		}
		if ( head[i+1] == 'N' ) { 
			// now we got the %N, insert the global doc count
			//long long c=g_checksumdb.getRdb()->getNumGlobalRecs();
			//now each host tells us how many docs it has in itsping
			long long c = g_hostdb.getNumGlobalRecs();
			c += g_conf.m_docCountAdjustment;
			// never allow to go negative
			if ( c < 0 ) c = 0;
			//p+=ulltoa(p,c);
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			long len = ulltoa(p, c);
			sb.incrementLength(len);
			// skip over %N
			i += 1;
			continue;
		}
		if ( head[i+1] == 'E' ) { 
			// now each host tells us how many docs it has in its
			// ping request
			long long c = g_hostdb.getNumGlobalEvents();
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			long len = ulltoa(p, c);
			sb.incrementLength(len);
			// skip over %E
			i += 1;
			continue;
		}
		if ( head[i+1] == 'n' ) { 
			// now we got the %n, insert the collection doc count
			//p+=ulltoa(p,docsInColl);
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			long len = ulltoa(p, docsInColl);
			sb.incrementLength(len);
			// skip over %n
			i += 1;
			continue;
		}
		if ( head[i+1] == 'T' ) { 
			// . print the final tail
			// . only print admin link if we're local
			//long  user = g_pages.getUserType ( s , r );
			//char *username = g_users.getUsername(r);
			//char *pwd  = r->getString ( "pwd" );
			char *p    = (char*) sb.getBuf();
			long  plen = sb.getAvail();
			//p = g_pages.printTail ( p , p + plen , user , pwd );
			char *n = g_pages.printTail(p , p + plen ,
						    r->isLocal());
			sb.incrementLength(n - p);
			// skip over %T
			i += 1;
			continue;
		}
		// print the drop down menu for selecting the # of reslts
		if ( head[i+1] == 'D' ) {
			// skip over %D
			i += 1;
			// skip if not enough buffer
			//if ( p + 1000 >= pend ) continue; 
			// # results
			//long n = r->getLong("n",10);
			//bool printedDropDown;
			//p = printNumResultsDropDown(p,n,&printedDropDown);
			//printNumResultsDropDown(sb,n,&printedDropDown);
			continue;
		}
		if ( head[i+1] == 'H' ) { 
			// . insert the secret key here, to stop seo bots
			// . TODO: randomize its position to make parsing more 
			//         difficult
			// . this secret key is for submitting a new query
			long key;
			char kname[4];
			g_httpServer.getKey (&key,kname,NULL,0,time(NULL),0,
					     10);
			//sprintf ( p , "<input type=hidden name=%s value=%li>",
			//	  kname,key);
			//p += gbstrlen ( p );
			sb.safePrintf( "<input type=hidden name=%s value=%li>",
				       kname,key);

			//adds param for default screen size
			//if(cr)
			//	sb.safePrintf("<input type=hidden id='screenWidth' name='ws' value=%li>", cr->m_screenWidth);

			// insert collection name too
			long collLen;
			char *coll = r->getString ( "c" , &collLen );
			if ( collLen > 0 && collLen < MAX_COLL_LEN ) {
			        //sprintf (p,"<input type=hidden name=c "
				//	 "value=\"");
				//p += gbstrlen ( p );	
				sb.safePrintf("<input type=hidden name=c "
					      "value=\"");
				//memcpy ( p , coll , collLen );
				//p += collLen;
				sb.safeMemcpy(coll, collLen);
				//sprintf ( p , "\">\n");
				//p += gbstrlen ( p );	
				sb.safePrintf("\">\n");
			}

			// pass this crap on so zak can do searches
			char *username = g_users.getUsername(r);
			// this is null because not in the cookie and we are
			// logged in
			//char *pwd  = r->getString ( "pwd" );
			//sb.safePrintf("<input type=hidden name=pwd value=\"%s\">\n",
			//pwd);
			sb.safePrintf("<input type=hidden name=username "
				      "value=\"%s\">\n",username);

			// skip over %H
			i += 1;
			continue;
		}
		// %t, print Top Directory section
		if ( head[i+1] == 't' ) {
			i += 1;
			//p = printTopDirectory ( p, pend );
			printTopDirectory ( sb );
			continue;
		}

		// *p++ = head[i];
		sb.safeMemcpy((char*)&head[i], 1);
		continue;
	}
	//return p;
	return true;
}
*/

// . store into "p"
// . returns bytes stored into "p"
// . used for entertainment purposes
/*
long printLastQueries ( char *p , char *pend ) {
	// if not 512 bytes left, bail
	if ( pend - p < 512 ) return 0;
	// return w/ no table if no queries have been added to g_qbuf yet
	if ( ! g_nextq == -1 ) return 0;
	// remember start for returning # of bytes stored
	char *start = p;
	// begin table (no border)
	sprintf (p,"<br><table border=0><tr><td><center>Last %li queries:"
		 "</td></tr>", (long)QBUF_NUMQUERIES );
	p += gbstrlen ( p );		
	// point to last query added
	long n = g_nextq - 1;
	// . wrap it if we need to
	// . QBUF_NUMQUERIES is defined to be 5 in PageResults.h
	if ( n < 0 ) n = QBUF_NUMQUERIES - 1;
	// . print up to five queries
	// . queries are stored by advancing g_nextq, so "i" should go backward
	long count = 0;
	for ( long i = n ; count < QBUF_NUMQUERIES ; count++ , i-- ) {
		// wrap i if we need to
		if ( i == -1 ) i = QBUF_NUMQUERIES - 1;
		// if this query is empty, skip it (might be uninitialized)
		if ( g_qbuf[i][0] == '\0' ) continue;
		// point to the query (these are NULL terminated)
		char *q    = g_qbuf[i];
		long  qlen = gbstrlen(q);
		// bail if too big 
		if ( p + qlen + 32 + 1024 >= pend ) return p - start;
		// otherwise, print this query to the page
		sprintf ( p , "<tr><td><a href=/cgi/0.cgi?q=" );
		p += gbstrlen ( p );
		// store encoded query as cgi parm
		p += urlEncode ( p , q , qlen );
		// end a href tag
		*p++ = '>';
		// . then print the actual query to the page
		// . use htmlEncode so nobody can abuse it
		p += saftenTags ( p , pend - p , q , qlen );
		// wrap it up
		sprintf ( p , "</a></td></tr>" );
		p += gbstrlen ( p );		
	}
	// end the table
	sprintf ( p , "</table>");
	p += gbstrlen ( p );
	// return bytes written
	return p - start;
}
*/


//char *printTopDirectory ( char *p, char *pend ) {
bool printTopDirectory ( SafeBuf& sb ) {

	// if no recs in catdb, print instructions
	if ( g_catdb.getRdb()->getNumTotalRecs() == 0 )
		return sb.safePrintf("<center>"
				     "<b>DMOZ functionality is not set up.</b>"
				     "<br>"
				     "<br>"
				     "<b>"
				     "Please follow the set up "
				     "<a href=/admin.html#dmoz>"
				     "instructions"
				     "</a>."
				     "</b>"
				     "</center>");

	//char topList[4096];
	//sprintf(topList, 
	return sb.safePrintf (
	"<center>"
	"<table cellspacing=\"4\" cellpadding=\"4\"><tr><td valign=top>\n"
	"<b><a href=\"/Top/Arts/\">Arts</a></b><br>"
	"<small>"
	"<a href=\"/Top/Arts/Movies/\">Movies</a>, "
	"<a href=\"/Top/Arts/Television/\">Television</a>, "
	"<a href=\"/Top/Arts/Music/\">Music</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Business/\">Business</a></b><br>"
	"<small>"
	"<a href=\"/Top/Business/Employment/\">Jobs</a>, "
	"<a href=\"/Top/Business/Real_Estate/\">Real Estate</a>, "
	"<a href=\"/Top/Business/Investing/\">Investing</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Computers/\">Computers</a></b><br>"
	"<small>"
	"<a href=\"/Top/Computers/Internet/\">Internet</a>, "
	"<a href=\"/Top/Computers/Software/\">Software</a>, "
	"<a href=\"/Top/Computers/Hardware/\">Hardware</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	"<b><a href=\"/Top/Games/\">Games</a></b><br>"
	"<small>"
	"<a href=\"/Top/Games/Video_Games/\">Video Games</a>, "
	"<a href=\"/Top/Games/Roleplaying/\">RPGs</a>, "
	"<a href=\"/Top/Games/Gambling/\">Gambling</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Health/\">Health</a></b><br>"
	"<small>"
	"<a href=\"/Top/Health/Fitness/\">Fitness</a>, "
	"<a href=\"/Top/Health/Medicine/\">Medicine</a>, "
	"<a href=\"/Top/Health/Alternative/\">Alternative</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Home/\">Home</a></b><br>"
	"<small>"
	"<a href=\"/Top/Home/Family/\">Family</a>, "
	"<a href=\"/Top/Home/Consumer_Information/\">Consumers</a>, "
	"<a href=\"/Top/Home/Cooking/\">Cooking</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	//"<b><a href=\"/Top/Kids_and_Teens/\">"
	//"<font color=\"#ff0000\">K</font>"
	//"<font color=\"339900\">i</font>"
	//"<font color=\"#ff6600\">d</font>"
	//"<font color=\"#0066ff\">s</font>"
	//" and Teens</a></b><br>"
	"<b><a href=\"/Top/Kids_and_Teens/\">Kids and Teens</a></b><br>"
	"<small>"
	"<a href=\"/Top/Kids_and_Teens/Arts/\">Arts</a>, "
	"<a href=\"/Top/Kids_and_Teens/School_Time/\">School Time</a>, "
	"<a href=\"/Top/Kids_and_Teens/Teen_Life/\">Teen Life</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/News/\">News</a></b><br>"
	"<small>"
	"<a href=\"/Top/News/Media/\">Media</a>, "
	"<a href=\"/Top/News/Newspapers/\">Newspapers</a>, "
	"<a href=\"/Top/News/Weather/\">Weather</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Recreation/\">Recreation</a></b><br>"
	"<small>"
	"<a href=\"/Top/Recreation/Travel/\">Travel</a>, "
	"<a href=\"/Top/Recreation/Food/\">Food</a>, "
	"<a href=\"/Top/Recreation/Outdoors/\">Outdoors</a>, "
	"<a href=\"/Top/Recreation/Humor/\">Humor</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	"<b><a href=\"/Top/Reference/\">Reference</a></b><br>"
	"<small>"
	"<a href=\"/Top/Reference/Maps/\">Maps</a>, "
	"<a href=\"/Top/Reference/Education/\">Education</a>, "
	"<a href=\"/Top/Reference/Libraries/\">Libraries</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Regional/\">Regional</a></b><br>"
	"<small>"
	"<a href=\"/Top/Regional/North_America/United_States/\">US</a>, "
	"<a href=\"/Top/Regional/North_America/Canada/\">Canada</a>, "
	"<a href=\"/Top/Regional/Europe/United_Kingdom/\">UK</a>, "
	"<a href=\"/Top/Regional/Europe/\">Europe</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Science/\">Science</a></b><br>"
	"<small>"
	"<a href=\"/Top/Science/Biology/\">Biology</a>, "
	"<a href=\"/Top/Science/Social_Sciences/Psychology/\">Psychology</a>, "
	"<a href=\"/Top/Science/Physics/\">Physics</a>..."
	"</small>\n"
	"</td></tr><tr><td valign=top>"
	"<b><a href=\"/Top/Shopping/\">Shopping</a></b><br>"
	"<small>"
	"<a href=\"/Top/Shopping/Vehicles/Autos/\">Autos</a>, "
	"<a href=\"/Top/Shopping/Clothing/\">Clothing</a>, "
	"<a href=\"/Top/Shopping/Gifts/\">Gifts</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Society/\">Society</a></b><br>"
	"<small>"
	"<a href=\"/Top/Society/People/\">People</a>, "
	"<a href=\"/Top/Society/Religion_and_Spirituality/\">Religion</a>, "
	"<a href=\"/Top/Society/Issues/\">Issues</a>..."
	"</small>\n"
	"</td><td valign=top>"
	"<b><a href=\"/Top/Sports/\">Sports</a></b><br>"
	"<small>"
	"<a href=\"/Top/Sports/Baseball/\">Baseball</a>, "
	"<a href=\"/Top/Sports/Soccer/\">Soccer</a>, "
	"<a href=\"/Top/Sports/Basketball/\">Basketball</a>..."
	"</small>\n"
	"</td></tr>"
	"<tr><td colspan=3 valign=top>"
	"<b><a href=\"/Top/World/\">World</a></b><br>"
	"<small>"
	"<a href=\"/Top/World/Deutsch/\">Deutsch</a>, "
	"<a href=\"/Top/World/Espa%%c3%%b1ol/\">Espa%c%col</a>, "
	"<a href=\"/Top/World/Fran%%c3%%a7ais/\">Fran%c%cais</a>, "
	"<a href=\"/Top/World/Italiano/\">Italiano</a>, "
	"<a href=\"/Top/World/Japanese/\">Japanese</a>, "
	"<a href=\"/Top/World/Nederlands/\">Nederlands</a>, "
	"<a href=\"/Top/World/Polska/\">Polska</a>, "
	"<a href=\"/Top/World/Dansk/\">Dansk</a>, "
	"<a href=\"/Top/World/Svenska/\">Svenska</a>..."
	"</small>\n"
	"</td></tr></table></center>\n",
	195, 177, 195, 167);
	// make sure there's room
	//long topListLen = gbstrlen(topList);
	//if (pend - p <= topListLen+1)
	//	return p;
	// copy it in
	//memcpy(p, topList, topListLen);
	//p += topListLen;
	//*p = '\0';
	//return p;
}

/////////////////
//
// ADD URL PAGE
//
/////////////////

#include "PageInject.h"
#include "TuringTest.h"
#include "AutoBan.h"
#include "CollectionRec.h"
#include "Users.h"
#include "Spider.h"

//static bool sendReply        ( void *state  , bool addUrlEnabled );
static bool canSubmit        (unsigned long h, long now, long maxUrlsPerIpDom);

//static void addedStuff ( void *state );

void resetPageAddUrl ( ) ;

/*
class State2 {
public:
	Url        m_url;
	//char      *m_buf;
	//long       m_bufLen;
	//long       m_bufMaxLen;
};
*/

class State1 {
public:
	//Msg4       m_msg4;
	Msg7       m_msg7;
	TcpSocket *m_socket;
        bool       m_isAdmin;
	char       m_coll[MAX_COLL_LEN+1];
	bool       m_goodAnswer;
	bool       m_doTuringTest;
	long       m_ufuLen;
	char       m_ufu[MAX_URL_LEN];

	//long       m_urlLen;
	//char       m_url[MAX_URL_LEN];

	//char       m_username[MAX_USER_SIZE];
	bool       m_strip;
	bool       m_spiderLinks;
	bool       m_forceRespider;
 	// buf filled by the links coming from google, msn, yahoo, etc
	//State2     m_state2[5]; // gb, goog, yahoo, msn, ask
	long       m_numSent;
	long       m_numReceived;
	//long       m_raw;
	//SpiderRequest m_sreq;
};

static void doneInjectingWrapper3 ( void *st1 ) ;

// only allow up to 1 Msg10's to be in progress at a time
static bool s_inprogress = false;

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageAddUrl ( TcpSocket *s , HttpRequest *r ) {
	// . get fields from cgi field of the requested url
	// . get the search query
	long  urlLen = 0;
	char *url = r->getString ( "u" , &urlLen , NULL /*default*/);

	// see if they provided a url of a file of urls if they did not
	// provide a url to add directly
	bool isAdmin = g_collectiondb.isAdmin ( r , s );
	long  ufuLen = 0;
	char *ufu = NULL;
	if ( isAdmin )
		// get the url of a file of urls (ufu)
		ufu = r->getString ( "ufu" , &ufuLen , NULL );

	// can't be too long, that's obnoxious
	if ( urlLen > MAX_URL_LEN || ufuLen > MAX_URL_LEN ) {
		g_errno = EBUFTOOSMALL;
		g_msg = " (error: url too long)";
		return g_httpServer.sendErrorReply(s,500,"url too long");
	}
	// get the collection
	long  collLen = 0;
	char *coll    = r->getString("c",&collLen);
	if ( ! coll || ! coll[0] ) {
		//coll    = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
		collLen = gbstrlen(coll);
	}
	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		g_msg = " (error: no collection)";
		return g_httpServer.sendErrorReply(s,500,"no coll rec");
	}
	// . make sure the ip is not banned
	// . we may also have an exclusive list of IPs for private collections
	if ( ! cr->hasSearchPermission ( s ) ) {
		g_errno = ENOPERM;
		g_msg = " (error: permission denied)";
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}


	//
	// if no url, print the main homepage page
	//
	if ( ! url ) {
		SafeBuf sb;
		printAddUrlHomePage ( sb , NULL , r );
		return g_httpServer.sendDynamicPage(s, 
						    sb.getBufStart(), 
						    sb.length(),
						    // 120 secs cachetime
						    // don't cache any more
						    // since we have the
						    // login bar at top of page
						    0,//120 ,// cachetime
						    false,// post?
						    "text/html",
						    200,
						    NULL, // cookie
						    "UTF-8",
						    r);
	}

	//
	// run the ajax script on load to submit the url now 
	//
	long id = r->getLong("id",0);
	// if we are not being called by the ajax loader, the put the
	// ajax loader script into the html now
	if ( id == 0 ) {
		SafeBuf sb;
		printAddUrlHomePage ( sb , url , r );
		return g_httpServer.sendDynamicPage ( s, 
						      sb.getBufStart(), 
						      sb.length(),
						      // don't cache any more
						      // since we have the
						      // login bar at top of 
						      //page
						      0,//3600,// cachetime
						      false,// post?
						      "text/html",
						      200,
						      NULL, // cookie
						      "UTF-8",
						      r);
	}

	//
	// ok, inject the provided url!!
	//

	//
	// check for errors first
	//

	// if addurl is turned off, just print "disabled" msg
	char *msg = NULL;
	if ( ! g_conf.m_addUrlEnabled ) 
		msg = "Add url is temporarily disabled";
	// can also be turned off in the collection rec
	if ( ! cr->m_addUrlEnabled    ) 
		msg = "Add url is temporarily disabled";
	// or if in read-only mode
	if (   g_conf.m_readOnlyMode  ) 
		msg = "Add url is temporarily disabled";
	// cannot add if another Msg10 from here is still in progress
	if ( s_inprogress ) 
		msg = "Add url is currently busy! Try again in a second.";

	// . send msg back to the ajax request
	// . use cachetime of 3600 so it does not re-inject if you hit the
	//   back button!
	if  ( msg ) {
		SafeBuf sb;
		sb.safePrintf("%s",msg);
		g_httpServer.sendDynamicPage (s, 
					      sb.getBufStart(), 
					      sb.length(),
					      3600,//-1, // cachetime
					      false,// post?
					      "text/html",
					      200, // http status
					      NULL, // cookie
					      "UTF-8");
		return true;
	}




	// make a new state
	State1 *st1 ;
	try { st1 = new (State1); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageAddUrl: new(%i): %s", 
		    sizeof(State1),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); }
	mnew ( st1 , sizeof(State1) , "PageAddUrl" );
	// save socket and isAdmin
	st1->m_socket  = s;
	st1->m_isAdmin = isAdmin;

	/*
	// save the url
	st1->m_url[0] = '\0';
	if ( url ) {
		// normalize and add www. if it needs it
		Url uu;
		uu.set ( url , gbstrlen(url) , true );
		// remove >'s i guess and store in st1->m_url[] buffer
		st1->m_urlLen=cleanInput ( st1->m_url,
					   MAX_URL_LEN, 
					   uu.getUrl(),
					   uu.getUrlLen() );
	}
	*/

	// save the "ufu" (url of file of urls)
	st1->m_ufu[0] = '\0';
	st1->m_ufuLen  = ufuLen;
	memcpy ( st1->m_ufu , ufu , ufuLen );
	st1->m_ufu[ufuLen] = '\0';

	st1->m_doTuringTest = cr->m_doTuringTest;
	st1->m_spiderLinks = true;
	st1->m_strip   = true;

	// save the collection name in the State1 class
	if ( collLen > MAX_COLL_LEN ) collLen = MAX_COLL_LEN;
	strncpy ( st1->m_coll , coll , collLen );
	st1->m_coll [ collLen ] = '\0';

	// assume they answered turing test correctly
	st1->m_goodAnswer = true;

	// get ip of submitter
	//unsigned long h = ipdom ( s->m_ip );
	// . use top 2 bytes now, some isps have large blocks
	// . if this causes problems, then they can do pay for inclusion
	unsigned long h = iptop ( s->m_ip );
	long codeLen;
	char* code = r->getString("code", &codeLen);
	if(g_autoBan.hasCode(code, codeLen, s->m_ip)) {
		long uipLen = 0;
		char* uip = r->getString("uip",&uipLen);
		long hip = 0;
		//use the uip when we have a raw query to test if 
		//we can submit
		if(uip) {
			hip = atoip(uip, uipLen);
			h = iptop( hip );
		}
	}


	st1->m_strip = r->getLong("strip",0);
	// . Remember, for cgi, if the box is not checked, then it is not 
	//   reported in the request, so set default return value to 0
	// . support both camel case and all lower-cases
	st1->m_spiderLinks = r->getLong("spiderLinks",0);
	st1->m_spiderLinks = r->getLong("spiderlinks",st1->m_spiderLinks);

	// . should we force it into spiderdb even if already in there
	// . use to manually update spider times for a url
	// . however, will not remove old scheduled spider times
	// . mdw: made force on the default
	st1->m_forceRespider = r->getLong("force",1); // 0);

	long now = getTimeGlobal();
	// . allow 1 submit every 1 hour
	// . restrict by submitter domain ip
	if ( ! st1->m_isAdmin &&
	     ! canSubmit ( h , now , cr->m_maxAddUrlsPerIpDomPerDay ) ) {
		// return error page
		//g_errno = ETOOEARLY;
		SafeBuf sb;
		sb.safePrintf("You breached your add url quota.");
		mdelete ( st1 , sizeof(State1) , "PageAddUrl" );
		delete (st1);
		// use cachetime of 3600 so it does not re-inject if you hit 
		// the back button!
		g_httpServer.sendDynamicPage (s, 
					      sb.getBufStart(), 
					      sb.length(),
					      3600,//-1, // cachetime
					      false,// post?
					      "text/html",
					      200, // http status
					      NULL, // cookie
					      "UTF-8");
		return true;
	}

	//st1->m_query = r->getString( "qts", &st1->m_queryLen );

	// check it, if turing test is enabled for this collection
	/*
	if ( ! st1->m_isAdmin && cr->m_doTuringTest && 
	     ! g_turingTest.isHuman(r) )  {
		// log note so we know it didn't make it
		g_msg = " (error: bad answer)";
		//log("PageAddUrl:: addurl failed for %s : bad answer",
		//    iptoa(s->m_ip));
		st1->m_goodAnswer = false;
		return sendReply ( st1 , true ); // addUrl enabled?
	}
	*/


	//
	// inject using msg7
	//

	// . pass in the cleaned url
	// . returns false if blocked, true otherwise
	if ( ! st1->m_msg7.inject ( s ,
				    r ,
				    st1 ,
				    doneInjectingWrapper3 ) )
		return false;

	// some kinda error, g_errno should be set i guess
	doneInjectingWrapper3  ( st1 );
	// we did not block
	return true;
}


void doneInjectingWrapper3 ( void *st ) {
	State1 *st1 = (State1 *)st;
	// allow others to add now
	s_inprogress = false;
	// get the state properly
	//State1 *st1 = (State1 *) state;
	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	char *url = st1->m_msg7.m_xd.m_firstUrl.m_url;
	log(LOG_INFO,"http: add url %s (%s)",url ,mstrerror(g_errno));
	// extract info from state
	TcpSocket *s       = st1->m_socket;
	//bool       isAdmin = st1->m_isAdmin;
	//char      *url     = NULL;
	//if ( st1->m_urlLen ) url = st1->m_url;
	// re-null it out if just http://
	//bool printUrl = true;
	//if ( st1->m_urlLen == 0 ) printUrl = false;
	//if ( ! st1->m_url       ) printUrl = false;
	//if(st1->m_urlLen==7&&st1->m_url&&!strncasecmp(st1->m_url,"http://",7)
	//	printUrl = false;

	// page is not more than 32k
	char buf[1024*32+MAX_URL_LEN*2];
	SafeBuf sb(buf, 1024*32+MAX_URL_LEN*2);
	
	//char rawbuf[1024*8];
	//SafeBuf rb(rawbuf, 1024*8);	
	//rb.safePrintf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	//rb.safePrintf("<status>\n");
	//CollectionRec *cr = g_collectiondb.getRec ( st1->m_coll );
	
	// collection name

	//char tt [ 128 ];
	//tt[0] = '\0';
	//if ( st1->m_coll[0] != '\0' && ! isAdmin ) 
	//	sprintf ( tt , " for %s", st1->m_coll );


	//
	// what we print here will just be the error msg, because the
	// ajax will fill the text we print here into the div below
	// the add url box
	//

	// if there was an error let them know
	//char msg[MAX_URL_LEN + 1024];
	char *pm = "";
	if ( g_errno ) {
		if ( g_errno == ETOOEARLY ) {
			pm = "Error. 100 urls have "
			"already been submitted by "
			"this IP address for the last 24 hours. "
			"<a href=/addurlerror.html>Explanation</a>.";
			log("addurls: Failed for user at %s: "
			    "quota breeched.", iptoa(s->m_ip));

			//rb.safePrintf("Error. %li urls have "
			//	      "already been submitted by "
			//	      "this IP address for the "
			//	      "last 24 hours. ",
			//	      cr->m_maxAddUrlsPerIpDomPerDay);
			sb.safePrintf("%s",pm);
		}
		else {
			sb.safePrintf("Error adding url(s): <b>%s[%i]</b>", 
				      mstrerror(g_errno) , g_errno);
			//pm = msg;
			//rb.safePrintf("Error adding url(s): %s[%i]", 
			//	      mstrerror(g_errno) , g_errno);
			//sb.safePrintf("%s",pm);
		}
	}
	else {
		if ( ! g_conf.m_addUrlEnabled ) {
			pm = "<font color=#ff0000>"
				"Sorry, this feature is temporarily disabled. "
				"Please try again later.</font>";
			if ( url )
				log("addurls: failed for user at %s: "
				    "add url is disabled. "
				    "Enable add url on the "
				    "Master Controls page and "
				    "on the Spider Controls page for "
				    "this collection.", 
				    iptoa(s->m_ip));

			sb.safePrintf("%s",pm);
			//rb.safePrintf("Sorry, this feature is temporarily "
			//	      "disabled. Please try again later.");
		}
		else if ( s_inprogress ) {
			pm = "Add url busy. Try again later.";
			log("addurls: Failed for user at %s: "
			    "busy adding another.", iptoa(s->m_ip));
			//rb.safePrintf("Add url busy. Try again later.");
			sb.safePrintf("%s",pm);
		}
		// did they fail the turing test?
		else if ( ! st1->m_goodAnswer ) {
			pm = "<font color=#ff0000>"
				"Oops, you did not enter the 4 large letters "
				"you see below. Please try again.</font>";
			//rb.safePrintf("could not add the url"
			//	      " because the turing test"
			//	      " is enabled.");
			sb.safePrintf("%s",pm);
		}
		else if ( st1->m_msg7.m_xd.m_indexCodeValid &&
			  st1->m_msg7.m_xd.m_indexCode ) {
			long ic = st1->m_msg7.m_xd.m_indexCode;
			sb.safePrintf("<b>Had error injecting url: %s</b>",
				      mstrerror(ic));
		}
		/*
		if ( url && ! st1->m_ufu[0] && url[0] && printUrl ) {
				sprintf ( msg ,"<u>%s</u> added to spider "
					  "queue "
					  "successfully", url );
				//rb.safePrintf("%s added to spider "
				//	      "queue successfully", url );
		}
		else if ( st1->m_ufu[0] ) {
			sprintf ( msg ,"urls in <u>%s</u> "
				  "added to spider queue "
				  "successfully", st1->m_ufu );

			//rb.safePrintf("urls in %s added to spider "
			//	      "queue successfully", url );

		}
		*/
		else {
			//rb.safePrintf("Add the url you want:");
			// avoid hitting browser page cache
			unsigned long rand32 = rand();
			// in the mime to 0 seconds!
			sb.safePrintf("<b>Url successfully added. "
				      "<a href=/search?rand=%lu&q=url%%3A",
				      rand32);
			sb.urlEncode(url);
			sb.safePrintf(">Check it</a> or "
				      "<a href=http://www.gigablast.com/seo?u=");
			sb.urlEncode(url);
			sb.safePrintf(">SEO it</a>"
				      ".</b>");
		}
			
		//pm = msg;
		//url = "http://";
		//else
		//	pm = "Don't forget to <a href=/gigaboost.html>"
		//		"Gigaboost</a> your URL.";
	}

	// store it
	sb.safePrintf("<b>%s</b>",pm );

	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;


	// nuke state
	mdelete ( st1 , sizeof(State1) , "PageAddUrl" );
	delete (st1);

	// this reply should be loaded from the ajax loader so use a cache
	// time of 1 hour so it does not re-inject the url if you hit the
	// back button
	g_httpServer.sendDynamicPage (s, 
				      sb.getBufStart(), 
				      sb.length(),
				      3600, // cachetime
				      false,// post?
				      "text/html",
				      200, // http status
				      NULL, // cookie
				      "UTF-8");
}


// we get like 100k submissions a day!!!
static HashTable s_htable;
static bool      s_init = false;
static long      s_lastTime = 0;
bool canSubmit ( unsigned long h , long now , long maxAddUrlsPerIpDomPerDay ) {
	// . sometimes no limit
	// . 0 means no limit because if they don't want any submission they
	//   can just turn off add url and we want to avoid excess 
	//   troubleshooting for why a url can't be added
	if ( maxAddUrlsPerIpDomPerDay <= 0 ) return true;
	// init the table
	if ( ! s_init ) {
		s_htable.set ( 50000 );
		s_init = true;
	}
	// clean out table every 24 hours
	if ( now - s_lastTime > 24*60*60 ) {
		s_lastTime = now;
		s_htable.clear();
	}
	// . if table almost full clean out ALL slots
	// . TODO: just clean out oldest slots
	if ( s_htable.getNumSlotsUsed() > 47000 ) s_htable.clear ();
	// . how many times has this IP domain submitted?
	// . allow 10 times per day
	long n = s_htable.getValue ( h );
	// if over 24hr limit then bail
	if ( n >= maxAddUrlsPerIpDomPerDay ) return false;
	// otherwise, inc it
	n++;
	// add to table, will replace old values
	s_htable.addKey ( h , n );
	return true;
}


void resetPageAddUrl ( ) {
	s_htable.reset();
}

